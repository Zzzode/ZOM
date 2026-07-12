// Copyright (c) 2026 ZOM contributors
// Licensed under the MIT License.

#include "zc/core/vector.h"

#include "zc/core/debug.h"
#include "zc/ztest/gtest.h"

namespace zc {
namespace {

TEST(ResourceVector, EmptyVectorAllocatesOnDemand) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  {
    Vector<uint> values(resource);
    EXPECT_EQ(0u, values.size());
    EXPECT_EQ(0u, values.capacity());
    EXPECT_EQ(0u, resource.currentAllocatedBytes());

    auto empty = values.releaseAsArray();
    EXPECT_EQ(0u, empty.size());
    EXPECT_EQ(0u, resource.currentAllocatedBytes());

    values.add(7u);
    EXPECT_EQ(7u, values[0]);
    EXPECT_TRUE(resource.currentAllocatedBytes() > 0);
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceVector, GrowthUsesOneResourceAndPreservesValues) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  {
    Vector<uint> values(resource, 1);
    for (uint value = 0; value < 33; ++value) { values.add(value); }
    EXPECT_EQ(33u, values.size());
    for (uint value = 0; value < 33; ++value) { EXPECT_EQ(value, values[value]); }
    EXPECT_TRUE(resource.peakAllocatedBytes() > resource.currentAllocatedBytes());
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceVector, MoveConstructionPreservesResourceIdentity) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  {
    Vector<uint> source(resource, 1);
    source.add(1u);
    size_t liveBytes = resource.currentAllocatedBytes();
    Vector<uint> destination(zc::mv(source));
    EXPECT_EQ(liveBytes, resource.currentAllocatedBytes());
    destination.add(2u);
    EXPECT_EQ(2u, destination.size());
    EXPECT_EQ(1u, destination[0]);
    EXPECT_EQ(2u, destination[1]);
    source.add(3u);
    EXPECT_EQ(3u, source[0]);
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceVector, MoveAssignmentTransfersResourceIdentity) {
  MemoryResource firstUpstream;
  MemoryResource secondUpstream;
  CountingMemoryResource firstResource(firstUpstream);
  CountingMemoryResource secondResource(secondUpstream);

  {
    Vector<uint> destination(firstResource, 2);
    destination.add(9u);
    Vector<uint> source(secondResource, 1);
    source.add(3u);
    size_t sourceBytes = secondResource.currentAllocatedBytes();

    destination = zc::mv(source);
    EXPECT_EQ(0u, firstResource.currentAllocatedBytes());
    EXPECT_EQ(sourceBytes, secondResource.currentAllocatedBytes());
    destination.add(4u);
    EXPECT_EQ(3u, destination[0]);
    EXPECT_EQ(4u, destination[1]);
    size_t destinationBytes = secondResource.currentAllocatedBytes();
    source.add(8u);
    EXPECT_TRUE(secondResource.currentAllocatedBytes() > destinationBytes);
  }
  EXPECT_EQ(0u, firstResource.currentAllocatedBytes());
  EXPECT_EQ(0u, secondResource.currentAllocatedBytes());
}

struct TrackedVectorValue {
  TrackedVectorValue(uint& live, uint value) : live(live), value(value) { ++live; }
  TrackedVectorValue(TrackedVectorValue&& other) noexcept : live(other.live), value(other.value) {
    ++live;
  }
  ~TrackedVectorValue() { --live; }

  ZC_DISALLOW_COPY(TrackedVectorValue);

  uint& live;
  uint value;
};

TEST(ResourceVector, NontrivialElementsAreDestroyedAcrossGrowth) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);
  uint live = 0;

  {
    Vector<TrackedVectorValue> values(resource, 1);
    for (uint value = 0; value < 10; ++value) { values.add(live, value); }
    EXPECT_EQ(10u, live);
    values.truncate(4);
    EXPECT_EQ(4u, live);
  }
  EXPECT_EQ(0u, live);
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

struct ThrowingMoveVectorValue {
  ThrowingMoveVectorValue(bool& throwOnMove, uint& live) : throwOnMove(throwOnMove), live(live) {
    ++live;
  }
  ThrowingMoveVectorValue(ThrowingMoveVectorValue&& other)
      : throwOnMove(other.throwOnMove), live(other.live) {
    ZC_REQUIRE(!throwOnMove, "injected vector move failure");
    ++live;
  }
  ~ThrowingMoveVectorValue() { --live; }

  ZC_DISALLOW_COPY(ThrowingMoveVectorValue);

  bool& throwOnMove;
  uint& live;
};

TEST(ResourceVector, FailedGrowthReturnsTemporaryAllocation) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);
  bool throwOnMove = false;
  uint live = 0;

  {
    Vector<ThrowingMoveVectorValue> values(resource, 1);
    values.add(throwOnMove, live);
    size_t originalBytes = resource.currentAllocatedBytes();
    throwOnMove = true;
    EXPECT_ANY_THROW(values.add(throwOnMove, live));
    EXPECT_EQ(originalBytes, resource.currentAllocatedBytes());
    EXPECT_EQ(1u, live);
    throwOnMove = false;
  }
  EXPECT_EQ(0u, live);
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

struct alignas(128) OverAlignedVectorValue {
  explicit OverAlignedVectorValue(uint value) : value(value) {}
  uint value;
};

TEST(ResourceVector, OverAlignedElementsRemainAlignedAfterGrowth) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  {
    Vector<OverAlignedVectorValue> values(resource, 1);
    for (uint value = 0; value < 9; ++value) { values.add(value); }
    EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(values.begin()) % alignof(OverAlignedVectorValue));
    EXPECT_EQ(8u, values.back().value);
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceVector, StoresPointersToConstElements) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);
  uint first = 7;
  uint second = 9;

  {
    Vector<const uint*> values(resource, 1);
    values.add(&first);
    values.add(&second);
    EXPECT_EQ(7u, *values[0]);
    EXPECT_EQ(9u, *values[1]);
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceVector, ReleasedArrayRetainsResourceDisposer) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  Array<uint> array;
  {
    Vector<uint> values(resource, 8);
    values.add(5u);
    values.add(6u);
    array = values.releaseAsArray();
    EXPECT_EQ(2u, array.size());
    EXPECT_TRUE(resource.currentAllocatedBytes() > 0);
  }
  EXPECT_TRUE(resource.currentAllocatedBytes() > 0);
  array = nullptr;
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

}  // namespace
}  // namespace zc
