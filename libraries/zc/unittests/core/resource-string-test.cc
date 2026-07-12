// Copyright (c) 2026 ZOM contributors
// Licensed under the MIT License.

#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zc/ztest/gtest.h"

namespace zc {
namespace {

TEST(ResourceString, EmptyStringOwnsOneTerminator) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  {
    String value = resourceHeapString(resource, 0);
    EXPECT_EQ(0u, value.size());
    EXPECT_EQ('\0', value.cStr()[0]);
    EXPECT_TRUE(resource.currentAllocatedBytes() > 0);
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceString, CopiesArbitraryUtf8AndEmbeddedNulBytes) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);
  const char bytes[] = {char(0xc3), char(0xa9), '\0', char(0xff)};

  {
    String value = resourceHeapString(resource, bytes, sizeof(bytes));
    EXPECT_EQ(sizeof(bytes), value.size());
    for (size_t i = 0; i < sizeof(bytes); ++i) { EXPECT_EQ(bytes[i], value[i]); }
    EXPECT_EQ('\0', value.cStr()[sizeof(bytes)]);
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceString, RejectsNonTerminatedCharArrayBeforeAllocation) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);
  const char bytes[] = {'n', 'o'};

  EXPECT_ANY_THROW(resourceHeapString(resource, bytes));
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
  EXPECT_EQ(0u, resource.peakAllocatedBytes());
}

TEST(ResourceString, MoveAndReleasePreserveResourceDisposer) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);
  Array<char> backing;

  {
    String source = resourceHeapString(resource, "move");
    size_t liveBytes = resource.currentAllocatedBytes();
    String moved = zc::mv(source);
    EXPECT_EQ(liveBytes, resource.currentAllocatedBytes());
    backing = moved.releaseArray();
    EXPECT_EQ(5u, backing.size());
  }
  EXPECT_TRUE(resource.currentAllocatedBytes() > 0);
  backing = nullptr;
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

TEST(ResourceString, ResourceStrUsesExistingStringifiers) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  {
    String value = resourceStr(resource, "value=", 42, ", utf8=", u8"\u4e16\u754c");
    EXPECT_EQ("value=42, utf8=\xe4\xb8\x96\xe7\x95\x8c", value);
    EXPECT_EQ('\0', value.cStr()[value.size()]);
  }
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

struct ThrowingStringIterator {
  bool atEnd;

  char operator*() const { ZC_FAIL_REQUIRE("injected resourceStr fill failure"); }
  ThrowingStringIterator& operator++() {
    atEnd = true;
    return *this;
  }
  ThrowingStringIterator operator++(int) {
    ThrowingStringIterator previous = *this;
    ++*this;
    return previous;
  }
  bool operator!=(const ThrowingStringIterator& other) const { return atEnd != other.atEnd; }
};

struct ThrowingStringSequence {
  size_t size() const { return 1; }
  ThrowingStringIterator begin() const { return {false}; }
  ThrowingStringIterator end() const { return {true}; }
};

struct ThrowingStringValue {};

ThrowingStringSequence operator*(const _::Stringifier&, const ThrowingStringValue&) { return {}; }

TEST(ResourceString, ResourceStrRollsBackFailedFill) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  EXPECT_ANY_THROW(resourceStr(resource, ThrowingStringValue{}));
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
  EXPECT_TRUE(resource.peakAllocatedBytes() > 0);
}

struct OversizedStringSequence {
  size_t size() const { return static_cast<size_t>(zc::maxValue); }
  const char* begin() const { return ""; }
  const char* end() const { return ""; }
};

struct OversizedStringValue {};

OversizedStringSequence operator*(const _::Stringifier&, const OversizedStringValue&) { return {}; }

TEST(ResourceString, RejectsCombinedSizeOverflowBeforeAllocation) {
  MemoryResource upstream;
  CountingMemoryResource resource(upstream);

  EXPECT_ANY_THROW(resourceStr(resource, OversizedStringValue{}, "x"));
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
  EXPECT_EQ(0u, resource.peakAllocatedBytes());
  EXPECT_ANY_THROW(resourceHeapString(resource, static_cast<size_t>(zc::maxValue)));
  EXPECT_EQ(0u, resource.currentAllocatedBytes());
}

}  // namespace
}  // namespace zc
