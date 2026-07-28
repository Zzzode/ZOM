// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr DiagnosticFactCodecLimits kBinderLimits{
    .maximumFacts = 1048576,
    .maximumEncodedBytes = 64 * 1024 * 1024,
    .maximumSourceByteOffset = static_cast<uint64_t>(zc::maxValue),
};
constexpr size_t kTopCountOffset = sizeof(uint64_t) + 27;
constexpr size_t kFirstFactOffset = kTopCountOffset + sizeof(uint64_t);
constexpr size_t kOrdinalOffsetInFact = 40;
constexpr size_t kCodeOffsetInFact = 44;
constexpr size_t kPrimaryOffsetInFact = 48;
constexpr size_t kArgumentsOffsetInFact = 56;
constexpr size_t kRangesOffsetInFact = 64;
constexpr size_t kFixItsOffsetInFact = 72;
constexpr size_t kSecondaryOffsetInFact = 80;
constexpr size_t kDefaultFactBytes = 88;
constexpr uint8_t kMinimalFactWire[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x7a, 0x6f, 0x6d, 0x2e, 0x73, 0x6f, 0x75,
    0x72, 0x63, 0x65, 0x2d, 0x64, 0x69, 0x61, 0x67, 0x6e, 0x6f, 0x73, 0x74, 0x69, 0x63, 0x2d,
    0x66, 0x61, 0x63, 0x74, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0xd2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static_assert(sizeof(kMinimalFactWire) == 117);

DiagnosticFact fact(uint64_t primaryByteOffset = 0) {
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticFactRange> ranges;
  zc::Vector<DiagnosticFixItFact> fixIts;
  zc::Vector<SecondaryDiagnosticFact> secondary;
  return {SourceDiagnosticPhase::Lex,
          zc::str("test.zom"_zc),
          zc::str("fixture"_zc),
          1,
          1,
          0,
          DiagID::InvalidCharacter,
          primaryByteOffset,
          zc::mv(arguments),
          zc::mv(ranges),
          zc::mv(fixIts),
          zc::mv(secondary)};
}

void expectEncodeRejectedWithoutAllocation(zc::ArrayPtr<const DiagnosticFact> facts,
                                           DiagnosticFactCodecLimits limits) {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  ZC_EXPECT(encodeDiagnosticFacts(resource, facts, limits) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() == 0);
}

zc::Array<uint8_t> encodedFact(DiagnosticFact&& value) {
  zc::Vector<DiagnosticFact> facts;
  facts.add(zc::mv(value));
  auto encoded = encodeDiagnosticFacts(zc::none, facts.asPtr(), kBinderLimits);
  ZC_REQUIRE(encoded != zc::none);
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

void writeUint32(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint32_t value) {
  for (size_t index = 0; index < 4; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> ((3 - index) * 8));
  }
}

void writeUint64(zc::ArrayPtr<uint8_t> bytes, size_t offset, uint64_t value) {
  for (size_t index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<uint8_t>(value >> ((7 - index) * 8));
  }
}

void expectDecodeRejected(zc::ArrayPtr<const uint8_t> encoded,
                          DiagnosticFactCodecLimits limits = kBinderLimits) {
  ZC_EXPECT(decodeDiagnosticFacts(zc::none, encoded, limits) == zc::none);
}

void expectOffsetBoundary(DiagnosticFact&& value, uint64_t maximumOffset) {
  auto encoded = encodedFact(zc::mv(value));
  auto atLimit = kBinderLimits;
  atLimit.maximumSourceByteOffset = maximumOffset;
  ZC_REQUIRE(decodeDiagnosticFacts(zc::none, encoded.asPtr(), atLimit) != zc::none);
  --atLimit.maximumSourceByteOffset;
  expectDecodeRejected(encoded.asPtr(), atLimit);
}

void expectTruncatedDeclaredCount(zc::ArrayPtr<const uint8_t> encoded, size_t countOffset) {
  auto truncated = zc::heapArray(encoded.first(countOffset + sizeof(uint64_t)));
  writeUint64(truncated.asPtr(), countOffset, 1);
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  ZC_EXPECT(decodeDiagnosticFacts(resource, truncated.asPtr(), kBinderLimits) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

size_t decodeResourcePeak(zc::ArrayPtr<const uint8_t> encoded, bool accepted) {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto decoded = decodeDiagnosticFacts(resource, encoded, kBinderLimits);
    ZC_EXPECT((decoded != zc::none) == accepted);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  return resource.peakAllocatedBytes();
}

void expectMinimumElementFeasibility(DiagnosticFact&& value, size_t countOffset,
                                     size_t minimumElementBytes) {
  auto encoded = encodedFact(zc::mv(value));
  ZC_REQUIRE(decodeDiagnosticFacts(zc::none, encoded.asPtr(), kBinderLimits) != zc::none);
  const size_t exactEnd = countOffset + sizeof(uint64_t) + minimumElementBytes;
  ZC_REQUIRE(exactEnd <= encoded.size());
  const bool exactPrefixIsComplete = exactEnd == encoded.size();
  const size_t exactPeak = decodeResourcePeak(encoded.first(exactEnd), exactPrefixIsComplete);
  const size_t shortPeak = decodeResourcePeak(encoded.first(exactEnd - 1), false);
  ZC_EXPECT(exactPeak > shortPeak);
}

}  // namespace

ZC_TEST("DiagnosticFactCodec bounded encoder round-trips one canonical fact") {
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact());
  auto encoded = encodeDiagnosticFacts(zc::none, facts.asPtr(), kBinderLimits);
  ZC_REQUIRE(encoded != zc::none);
  auto decoded = decodeDiagnosticFacts(zc::none, ZC_ASSERT_NONNULL(encoded).asPtr(), kBinderLimits);
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decoded) == facts);
}

ZC_TEST("DiagnosticFactCodec encoder enforces the exact byte boundary before allocation") {
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact());
  auto measured = encodeDiagnosticFacts(zc::none, facts.asPtr(), kBinderLimits);
  ZC_REQUIRE(measured != zc::none);

  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto exactLimits = kBinderLimits;
    exactLimits.maximumEncodedBytes = ZC_ASSERT_NONNULL(measured).size();
    auto encoded = encodeDiagnosticFacts(resource, facts.asPtr(), exactLimits);
    ZC_REQUIRE(encoded != zc::none);
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);

  auto oneByteTooSmall = kBinderLimits;
  oneByteTooSmall.maximumEncodedBytes = ZC_ASSERT_NONNULL(measured).size() - 1;
  expectEncodeRejectedWithoutAllocation(facts.asPtr(), oneByteTooSmall);
}

ZC_TEST("DiagnosticFactCodec encoder rejects invalid facts before allocation") {
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(1));
  auto zeroOffsetLimits = kBinderLimits;
  zeroOffsetLimits.maximumSourceByteOffset = 0;
  expectEncodeRejectedWithoutAllocation(facts.asPtr(), zeroOffsetLimits);

  facts = nullptr;
  auto invalidOrdinal = fact();
  invalidOrdinal.occurrenceOrdinal = 1;
  facts.add(zc::mv(invalidOrdinal));
  expectEncodeRejectedWithoutAllocation(facts.asPtr(), kBinderLimits);

  facts = nullptr;
  auto invalidArity = fact();
  invalidArity.arguments.add(zc::str("unexpected"_zc));
  facts.add(zc::mv(invalidArity));
  expectEncodeRejectedWithoutAllocation(facts.asPtr(), kBinderLimits);

  facts = nullptr;
  facts.add(fact(1));
  facts.add(fact(0));
  expectEncodeRejectedWithoutAllocation(facts.asPtr(), kBinderLimits);
}

ZC_TEST("DiagnosticFactCodec encoder admits the Binder fact boundary") {
  zc::Vector<DiagnosticFact> facts(4097);
  for (uint64_t index = 0; index < 4097; ++index) { facts.add(fact(index)); }
  auto encoded = encodeDiagnosticFacts(zc::none, facts.asPtr(), kBinderLimits);
  ZC_REQUIRE(encoded != zc::none);

  const DiagnosticFactCodecLimits sourceLimits{
      .maximumFacts = 4096,
      .maximumEncodedBytes = 64 * 1024 * 1024,
      .maximumSourceByteOffset = static_cast<uint64_t>(zc::maxValue),
  };
  expectEncodeRejectedWithoutAllocation(facts.asPtr(), sourceLimits);
}

ZC_TEST("DiagnosticFactCodec decoder enforces complete byte and source offset boundaries") {
  auto baseline = encodedFact(fact());
  auto exactBytes = kBinderLimits;
  exactBytes.maximumEncodedBytes = baseline.size();
  ZC_EXPECT(decodeDiagnosticFacts(zc::none, baseline.asPtr(), exactBytes) != zc::none);
  --exactBytes.maximumEncodedBytes;
  expectDecodeRejected(baseline.asPtr(), exactBytes);

  expectOffsetBoundary(fact(8), 8);

  auto ranged = fact();
  ranged.ranges.add(DiagnosticFactRange{8, 8, false});
  expectOffsetBoundary(zc::mv(ranged), 8);

  auto fixed = fact();
  fixed.fixIts.add(DiagnosticFixItFact{{8, 8, true}, zc::str("x"_zc)});
  expectOffsetBoundary(zc::mv(fixed), 8);

  auto parentWithChild = fact();
  zc::Vector<zc::String> childArguments;
  zc::Vector<DiagnosticFactRange> childRanges;
  parentWithChild.secondary.add(SecondaryDiagnosticFact{
      DiagID::InvalidCharacter, 8, zc::mv(childArguments), zc::mv(childRanges)});
  expectOffsetBoundary(zc::mv(parentWithChild), 8);
}

ZC_TEST("DiagnosticFactCodec decoder admits the independent minimum canonical fact wire") {
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto decoded = decodeDiagnosticFacts(resource, zc::arrayPtr(kMinimalFactWire), kBinderLimits);
    ZC_REQUIRE(decoded != zc::none);
    ZC_REQUIRE(ZC_ASSERT_NONNULL(decoded).size() == 1);
    ZC_EXPECT(ZC_ASSERT_NONNULL(decoded)[0].emitterFile == "x"_zc);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);

  ZC_EXPECT(decodeDiagnosticFacts(
                resource, zc::arrayPtr(kMinimalFactWire).first(sizeof(kMinimalFactWire) - 1),
                kBinderLimits) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);

  zc::MemoryResource shortUpstream;
  zc::CountingMemoryResource shortResource(shortUpstream);
  ZC_EXPECT(decodeDiagnosticFacts(
                shortResource, zc::arrayPtr(kMinimalFactWire).first(sizeof(kMinimalFactWire) - 1),
                kBinderLimits) == zc::none);
  ZC_EXPECT(shortResource.peakAllocatedBytes() == 0);
}

ZC_TEST("DiagnosticFactCodec decoder enforces every nested minimum element size") {
  auto withArgument = fact();
  withArgument.emitterFunction = zc::str("fixture123"_zc);
  withArgument.code = DiagID::ExceptedIdentifier;
  withArgument.arguments.add(zc::str(""_zc));
  expectMinimumElementFeasibility(zc::mv(withArgument),
                                  kFirstFactOffset + kArgumentsOffsetInFact + 3, 8);

  auto withRange = fact();
  withRange.ranges.add(DiagnosticFactRange{0, 0, false});
  expectMinimumElementFeasibility(zc::mv(withRange), kFirstFactOffset + kRangesOffsetInFact, 17);

  auto withFixIt = fact();
  withFixIt.fixIts.add(DiagnosticFixItFact{{0, 0, false}, zc::str(""_zc)});
  expectMinimumElementFeasibility(zc::mv(withFixIt), kFirstFactOffset + kFixItsOffsetInFact, 25);

  auto withSecondary = fact();
  zc::Vector<zc::String> childArguments;
  zc::Vector<DiagnosticFactRange> childRanges;
  withSecondary.secondary.add(SecondaryDiagnosticFact{DiagID::InvalidCharacter, 0,
                                                      zc::mv(childArguments), zc::mv(childRanges)});
  expectMinimumElementFeasibility(zc::mv(withSecondary), kFirstFactOffset + kSecondaryOffsetInFact,
                                  28);
}

ZC_TEST("DiagnosticFactCodec decoder rejects infeasible declared counts without preallocation") {
  auto baseline = encodedFact(fact());
  auto hostileTop = zc::heapArray(baseline.first(kFirstFactOffset));
  writeUint64(hostileTop.asPtr(), kTopCountOffset, kBinderLimits.maximumFacts);
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  ZC_EXPECT(decodeDiagnosticFacts(resource, hostileTop.asPtr(), kBinderLimits) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() == 0);

  expectTruncatedDeclaredCount(baseline.asPtr(), kFirstFactOffset + kArgumentsOffsetInFact);
  expectTruncatedDeclaredCount(baseline.asPtr(), kFirstFactOffset + kRangesOffsetInFact);
  expectTruncatedDeclaredCount(baseline.asPtr(), kFirstFactOffset + kFixItsOffsetInFact);
  expectTruncatedDeclaredCount(baseline.asPtr(), kFirstFactOffset + kSecondaryOffsetInFact);

  auto withChild = fact();
  zc::Vector<zc::String> childArguments;
  zc::Vector<DiagnosticFactRange> childRanges;
  withChild.secondary.add(SecondaryDiagnosticFact{DiagID::InvalidCharacter, 0,
                                                  zc::mv(childArguments), zc::mv(childRanges)});
  auto childBytes = encodedFact(zc::mv(withChild));
  const size_t childArgumentsOffset = kFirstFactOffset + kSecondaryOffsetInFact + sizeof(uint64_t) +
                                      sizeof(uint32_t) + sizeof(uint64_t);
  expectTruncatedDeclaredCount(childBytes.asPtr(), childArgumentsOffset);
  expectTruncatedDeclaredCount(childBytes.asPtr(), childArgumentsOffset + sizeof(uint64_t));
}

ZC_TEST("DiagnosticFactCodec decoder rejects unrepresentable counts before allocation") {
  if constexpr (sizeof(size_t) < sizeof(uint64_t)) {
    auto hostile = zc::heapArray(zc::arrayPtr(kMinimalFactWire).first(kFirstFactOffset));
    const uint64_t unrepresentable = static_cast<uint64_t>(static_cast<size_t>(zc::maxValue)) + 1;
    writeUint64(hostile.asPtr(), kTopCountOffset, unrepresentable);
    auto limits = kBinderLimits;
    limits.maximumFacts = static_cast<uint64_t>(zc::maxValue);
    zc::MemoryResource upstream;
    zc::CountingMemoryResource resource(upstream);
    ZC_EXPECT(decodeDiagnosticFacts(resource, hostile.asPtr(), limits) == zc::none);
    ZC_EXPECT(resource.currentAllocatedBytes() == 0);
    ZC_EXPECT(resource.peakAllocatedBytes() == 0);
  } else {
    ZC_EXPECT(sizeof(size_t) == sizeof(uint64_t));
  }
}

ZC_TEST("DiagnosticFactCodec decoder rejects malformed and non-canonical payloads") {
  auto unknownCode = encodedFact(fact());
  writeUint32(unknownCode.asPtr(), kFirstFactOffset + kCodeOffsetInFact,
              static_cast<uint32_t>(zc::maxValue));
  expectDecodeRejected(unknownCode.asPtr());

  auto invalidArity = fact();
  invalidArity.code = DiagID::ExceptedIdentifier;
  invalidArity.arguments.add(zc::str("name"_zc));
  auto invalidArityBytes = encodedFact(zc::mv(invalidArity));
  writeUint32(invalidArityBytes.asPtr(), kFirstFactOffset + kCodeOffsetInFact,
              static_cast<uint32_t>(DiagID::InvalidCharacter));
  expectDecodeRejected(invalidArityBytes.asPtr());

  zc::Vector<DiagnosticFact> duplicates;
  duplicates.add(fact());
  duplicates.add(fact());
  duplicates = canonicalizeDiagnosticFacts(zc::mv(duplicates));
  auto duplicateBytes = encodeDiagnosticFacts(zc::none, duplicates.asPtr(), kBinderLimits);
  ZC_REQUIRE(duplicateBytes != zc::none);
  writeUint32(ZC_ASSERT_NONNULL(duplicateBytes).asPtr(),
              kFirstFactOffset + kDefaultFactBytes + kOrdinalOffsetInFact, 0);
  expectDecodeRejected(ZC_ASSERT_NONNULL(duplicateBytes).asPtr());

  zc::Vector<DiagnosticFact> ordered;
  ordered.add(fact(1));
  ordered.add(fact(2));
  auto orderedBytes = encodeDiagnosticFacts(zc::none, ordered.asPtr(), kBinderLimits);
  ZC_REQUIRE(orderedBytes != zc::none);
  writeUint64(ZC_ASSERT_NONNULL(orderedBytes).asPtr(),
              kFirstFactOffset + kDefaultFactBytes + kPrimaryOffsetInFact, 0);
  expectDecodeRejected(ZC_ASSERT_NONNULL(orderedBytes).asPtr());

  auto baseline = encodedFact(fact());
  for (size_t size = 0; size < baseline.size(); ++size) {
    expectDecodeRejected(baseline.first(size));
  }
  zc::Vector<uint8_t> trailing(baseline.size() + 1);
  trailing.addAll(baseline);
  trailing.add(0);
  expectDecodeRejected(trailing.asPtr());
}

ZC_TEST("DiagnosticFactCodec decoder returns resource-owned canonical facts") {
  zc::Vector<DiagnosticFact> expected;
  auto populated = fact(1);
  populated.code = DiagID::ExceptedIdentifier;
  populated.arguments.add(zc::str("parent"_zc));
  populated.ranges.add(DiagnosticFactRange{1, 2, true});
  populated.fixIts.add(DiagnosticFixItFact{{2, 3, false}, zc::str("replace"_zc)});
  zc::Vector<zc::String> childArguments;
  childArguments.add(zc::str("child"_zc));
  zc::Vector<DiagnosticFactRange> childRanges;
  childRanges.add(DiagnosticFactRange{3, 4, false});
  populated.secondary.add(SecondaryDiagnosticFact{DiagID::ExpectedToken, 4, zc::mv(childArguments),
                                                  zc::mv(childRanges)});
  expected.add(zc::mv(populated));
  auto baseline = encodeDiagnosticFacts(zc::none, expected.asPtr(), kBinderLimits);
  ZC_REQUIRE(baseline != zc::none);
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto decoded =
        decodeDiagnosticFacts(resource, ZC_ASSERT_NONNULL(baseline).asPtr(), kBinderLimits);
    ZC_REQUIRE(decoded != zc::none);
    ZC_EXPECT(ZC_ASSERT_NONNULL(decoded) == expected);
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
    ZC_EXPECT(resource.peakAllocatedBytes() > resource.currentAllocatedBytes());
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

}  // namespace zomlang::compiler::diagnostics
