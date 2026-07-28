// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/identity/canonical-encoder.h"

#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::identity {
namespace {

uint8_t hexNibble(char value) {
  if (value >= '0' && value <= '9') { return static_cast<uint8_t>(value - '0'); }
  if (value >= 'a' && value <= 'f') { return static_cast<uint8_t>(value - 'a' + 10); }
  ZC_FAIL_REQUIRE("invalid lowercase hexadecimal test oracle");
}

void expectDigest(const Sha256Digest& digest, zc::StringPtr expected) {
  ZC_REQUIRE(expected.size() == 64);
  const auto bytes = digest.bytes();
  for (size_t index = 0; index < bytes.size(); ++index) {
    const uint8_t value = static_cast<uint8_t>((hexNibble(expected[index * 2]) << 4) |
                                               hexNibble(expected[index * 2 + 1]));
    ZC_EXPECT(bytes[index] == value, index);
  }
}

Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("SHA-256 input length overflow during identity test");
}

Sha256Digest testDigest() {
  uint8_t bytes[32];
  for (uint8_t index = 0; index < sizeof(bytes); ++index) { bytes[index] = index; }
  auto digest = Sha256Digest::fromBytes(zc::arrayPtr(bytes));
  ZC_IF_SOME(value, digest) { return value; }
  ZC_FAIL_REQUIRE("fixed digest has an invalid length");
}

CanonicalEncoder requireExactEncoder(uint64_t encodedByteCount) {
  auto encoder = CanonicalEncoder::forExactSize(encodedByteCount);
  ZC_IF_SOME(value, encoder) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("fixed canonical encoder size is not representable");
}

CanonicalEncoder requireExactEncoder(zc::MemoryResource& resource, uint64_t encodedByteCount) {
  auto encoder = CanonicalEncoder::forExactSize(resource, encodedByteCount);
  ZC_IF_SOME(value, encoder) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("fixed canonical encoder size is not representable");
}

template <typename Function>
void expectExactOverfillBeforeWrite(uint64_t encodedByteCount, Function&& encode) {
  auto encoder = requireExactEncoder(encodedByteCount);
  ZC_EXPECT_THROW_MESSAGE("CanonicalEncoder exact capacity exceeded", encode(encoder));
  for (uint64_t index = 0; index < encodedByteCount; ++index) { encoder.encodeUint8(0x7a); }
  auto encoded = encoder.finish();
  ZC_EXPECT(encoded.size() == encodedByteCount);
  for (uint8_t byte : encoded) { ZC_EXPECT(byte == 0x7a); }
}

void encodeEveryField(CanonicalEncoder& encoder) {
  const uint8_t text[] = {'z', 'o', 'm'};
  encoder.encodeUint8(0xab);
  encoder.encodeUint32(0x12345678);
  encoder.encodeUint64(0x0123456789abcdef);
  encoder.encodeBool(false);
  encoder.encodeBool(true);
  encoder.encodeDigest(testDigest());
  encoder.encodeByteString(zc::arrayPtr(text));
  encoder.encodeSequenceSize(0xfedcba9876543210);
  encoder.encodeNone();
  encoder.encodeSome();
}

class FailSecondAllocationResource final : public zc::MemoryResource {
public:
  explicit FailSecondAllocationResource(zc::MemoryResource& upstream) : upstream(upstream) {}

protected:
  void* doAllocate(size_t size, size_t alignment) override {
    ++allocationCount;
    ZC_REQUIRE(allocationCount != 2, "injected canonical encoder allocation failure");
    return upstream.allocate(size, alignment);
  }

  void doDeallocate(void* pointer, size_t size, size_t alignment) override {
    upstream.deallocate(pointer, size, alignment);
  }

private:
  zc::MemoryResource& upstream;
  size_t allocationCount = 0;
};

}  // namespace

ZC_TEST("SHA-256 passes standard padding and block-boundary vectors") {
  const uint8_t abc[] = {'a', 'b', 'c'};
  constexpr char twoBlockInput[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

  expectDigest(requireDigest(zc::ArrayPtr<const uint8_t>()),
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  expectDigest(requireDigest(zc::arrayPtr(abc)),
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  expectDigest(requireDigest(zc::arrayPtr(twoBlockInput, sizeof(twoBlockInput) - 1).asBytes()),
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

ZC_TEST("CanonicalEncoder passes the fixed A byte-string vector") {
  const uint8_t text[] = {'A'};
  const uint8_t expected[] = {0, 0, 0, 0, 0, 0, 0, 1, 'A'};
  CanonicalEncoder encoder;
  encoder.encodeByteString(zc::arrayPtr(text));
  auto encoded = encoder.finish();

  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  expectDigest(requireDigest(encoded.asPtr()),
               "ead76f8e70b5dd3b1a07a92c25c425b2b27198728862103d65c31c621e52a6aa");
}

ZC_TEST("CanonicalEncoder passes the fixed empty-sequence vector") {
  const uint8_t expected[] = {0, 0, 0, 0, 0, 0, 0, 0};
  CanonicalEncoder encoder;
  encoder.encodeSequenceSize(0);
  auto encoded = encoder.finish();

  ZC_EXPECT(encoded.asPtr() == zc::arrayPtr(expected));
  expectDigest(requireDigest(encoded.asPtr()),
               "af5570f5a1810b7af78caf4bc70a660f0df51e42baf91d4de5b2328de0e83dfc");
}

ZC_TEST("CanonicalEncoder passes the fixed empty fingerprint-domain vector") {
  constexpr char domain[] = "zom.semantic-context";
  CanonicalEncoder encoder;
  for (size_t index = 0; index < sizeof(domain) - 1; ++index) {
    encoder.encodeUint8(static_cast<uint8_t>(domain[index]));
  }
  encoder.encodeUint8(0);
  for (size_t index = 0; index < 6; ++index) { encoder.encodeSequenceSize(0); }
  auto encoded = encoder.finish();

  ZC_EXPECT(encoded.size() == 69);
  expectDigest(requireDigest(encoded.asPtr()),
               "b0284a2767bcaa8ba0d0c5c9ea1475b9377ef2c4341bbca97d424d088517ba23");
}

ZC_TEST("CanonicalEncoder explicit resource preserves every encoded field") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    CanonicalEncoder defaultEncoder;
    CanonicalEncoder resourceEncoder(resource);

    encodeEveryField(defaultEncoder);
    encodeEveryField(resourceEncoder);
    auto expected = defaultEncoder.finish();
    auto actual = resourceEncoder.finish();

    ZC_EXPECT(actual.asPtr() == expected.asPtr());
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
    actual = nullptr;
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("CanonicalEncoder exact heap capacity preserves every encoded field") {
  CanonicalEncoder expectedEncoder;
  encodeEveryField(expectedEncoder);
  auto expected = expectedEncoder.finish();

  auto exactEncoder = requireExactEncoder(expected.size());
  encodeEveryField(exactEncoder);
  auto actual = exactEncoder.finish();

  ZC_EXPECT(actual.asPtr() == expected.asPtr());
}

ZC_TEST("CanonicalEncoder exact resource capacity does not grow") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  zc::Array<uint8_t> output;

  {
    auto encoder = requireExactEncoder(resource, 5);
    const size_t liveBytes = resource.currentAllocatedBytes();
    const size_t peakBytes = resource.peakAllocatedBytes();
    encoder.encodeUint8(0x7a);
    encoder.encodeUint32(0x01020304);
    ZC_EXPECT(resource.currentAllocatedBytes() == liveBytes);
    ZC_EXPECT(resource.peakAllocatedBytes() == peakBytes);
    output = encoder.finish();
    const uint8_t expected[] = {0x7a, 0x01, 0x02, 0x03, 0x04};
    ZC_EXPECT(output.asPtr() == zc::arrayPtr(expected));
  }

  ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  output = nullptr;
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("CanonicalEncoder exact zero capacity preserves heap and resource ownership") {
  auto heapEncoder = requireExactEncoder(0);
  auto heapOutput = heapEncoder.finish();
  ZC_EXPECT(heapOutput.size() == 0);

  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto resourceEncoder = requireExactEncoder(resource, 0);
    const size_t liveBytes = resource.currentAllocatedBytes();
    const size_t peakBytes = resource.peakAllocatedBytes();
    auto resourceOutput = resourceEncoder.finish();
    ZC_EXPECT(resourceOutput.size() == 0);
    ZC_EXPECT(resource.currentAllocatedBytes() == liveBytes);
    ZC_EXPECT(resource.peakAllocatedBytes() == peakBytes);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("CanonicalEncoder exact factories reject unrepresentable sizes before allocation") {
  if constexpr (sizeof(size_t) < sizeof(uint64_t)) {
    const uint64_t unrepresentable = static_cast<uint64_t>(static_cast<size_t>(zc::maxValue)) + 1;
    ZC_EXPECT(CanonicalEncoder::forExactSize(unrepresentable) == zc::none);

    zc::MemoryResource upstream;
    zc::CountingMemoryResource resource(upstream);
    ZC_EXPECT(CanonicalEncoder::forExactSize(resource, unrepresentable) == zc::none);
    ZC_EXPECT(resource.currentAllocatedBytes() == 0);
    ZC_EXPECT(resource.peakAllocatedBytes() == 0);
  } else {
    ZC_EXPECT(sizeof(size_t) == sizeof(uint64_t));
  }
}

ZC_TEST("CanonicalEncoder exact capacity rejects underfill") {
  auto encoder = requireExactEncoder(2);
  encoder.encodeUint8(1);
  ZC_EXPECT_THROW_MESSAGE("CanonicalEncoder exact capacity underfilled", encoder.finish());
}

ZC_TEST("CanonicalEncoder exact capacity rejects every independent writer before writing") {
  const uint8_t value[] = {0x7a};
  expectExactOverfillBeforeWrite(0, [](CanonicalEncoder& encoder) { encoder.encodeUint8(0x7a); });
  expectExactOverfillBeforeWrite(
      sizeof(uint32_t) - 1, [](CanonicalEncoder& encoder) { encoder.encodeUint32(0x01020304); });
  expectExactOverfillBeforeWrite(sizeof(uint64_t) - 1, [](CanonicalEncoder& encoder) {
    encoder.encodeUint64(0x0102030405060708);
  });
  expectExactOverfillBeforeWrite(testDigest().bytes().size() - 1, [](CanonicalEncoder& encoder) {
    encoder.encodeDigest(testDigest());
  });
  expectExactOverfillBeforeWrite(sizeof(uint64_t), [&](CanonicalEncoder& encoder) {
    encoder.encodeByteString(zc::arrayPtr(value));
  });
}

ZC_TEST("CanonicalEncoder resource storage survives growth and encoder destruction") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  zc::Array<uint8_t> output;

  {
    CanonicalEncoder encoder(resource);
    for (uint32_t value = 0; value < 4096; ++value) { encoder.encodeUint32(value); }
    output = encoder.finish();
    ZC_EXPECT(output.size() == 4096 * sizeof(uint32_t));
    ZC_EXPECT(resource.peakAllocatedBytes() > resource.currentAllocatedBytes());
  }

  ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  output = nullptr;
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("CanonicalEncoder move construction preserves resource ownership") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);

  {
    CanonicalEncoder source(resource);
    source.encodeUint8(0x7a);
    const size_t liveBytes = resource.currentAllocatedBytes();
    CanonicalEncoder destination(zc::mv(source));
    ZC_EXPECT(resource.currentAllocatedBytes() == liveBytes);
    destination.encodeUint32(0x01020304);
    auto output = destination.finish();
    const uint8_t expected[] = {0x7a, 0x01, 0x02, 0x03, 0x04};
    ZC_EXPECT(output.asPtr() == zc::arrayPtr(expected));
  }

  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("CanonicalEncoder move assignment transfers resource ownership") {
  zc::MemoryResource firstUpstream;
  zc::MemoryResource secondUpstream;
  zc::CountingMemoryResource firstResource(firstUpstream);
  zc::CountingMemoryResource secondResource(secondUpstream);

  {
    CanonicalEncoder destination(firstResource);
    destination.encodeUint64(9);
    CanonicalEncoder source(secondResource);
    source.encodeUint8(3);
    const size_t sourceBytes = secondResource.currentAllocatedBytes();

    destination = zc::mv(source);
    ZC_EXPECT(firstResource.currentAllocatedBytes() == 0);
    ZC_EXPECT(secondResource.currentAllocatedBytes() == sourceBytes);
    destination.encodeUint8(4);
    auto output = destination.finish();
    const uint8_t expected[] = {3, 4};
    ZC_EXPECT(output.asPtr() == zc::arrayPtr(expected));
  }

  ZC_EXPECT(firstResource.currentAllocatedBytes() == 0);
  ZC_EXPECT(secondResource.currentAllocatedBytes() == 0);
}

ZC_TEST("CanonicalEncoder releases its resource object after failed growth") {
  zc::MemoryResource upstream;
  FailSecondAllocationResource failingResource(upstream);
  zc::CountingMemoryResource resource(failingResource);

  {
    CanonicalEncoder encoder(resource);
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
    ZC_EXPECT_THROW_MESSAGE("injected canonical encoder allocation failure",
                            encoder.encodeUint8(1));
  }

  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("CanonicalEncoder exact construction releases its resource object after failed storage") {
  zc::MemoryResource upstream;
  FailSecondAllocationResource failingResource(upstream);
  zc::CountingMemoryResource resource(failingResource);

  ZC_EXPECT_THROW_MESSAGE("injected canonical encoder allocation failure",
                          CanonicalEncoder::forExactSize(resource, 16));
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

}  // namespace zomlang::compiler::identity
