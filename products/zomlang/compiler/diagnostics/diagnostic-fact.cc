// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/diagnostics/diagnostic-fact.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/diagnostics/diagnostic-info.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr zc::StringPtr kDiagnosticFactsDomain = "zom.source-diagnostic-facts"_zc;
constexpr uint64_t kMaximumArguments = 3;
constexpr uint64_t kMaximumRanges = 64;
constexpr uint64_t kMaximumFixIts = 64;
constexpr uint64_t kMaximumSecondary = 64;
constexpr uint64_t kMaximumEmitterBytes = 4096;
constexpr uint64_t kMaximumTextBytes = 64 * 1024 * 1024;
constexpr uint64_t kMinimumEncodedFactBytes = 74;
constexpr uint64_t kMinimumEncodedStringBytes = 8;
constexpr uint64_t kEncodedRangeBytes = 17;
constexpr uint64_t kMinimumEncodedFixItBytes = 25;
constexpr uint64_t kMinimumEncodedSecondaryBytes = 28;

class EncodedSize final {
public:
  explicit EncodedSize(uint64_t maximum) : maximum(maximum) {}

  bool add(uint64_t addition) {
    if (value > maximum || addition > maximum - value) { return false; }
    value += addition;
    return true;
  }

  bool addByteString(uint64_t byteCount) { return add(sizeof(uint64_t)) && add(byteCount); }
  uint64_t get() const noexcept { return value; }

private:
  uint64_t maximum;
  uint64_t value = 0;
};

zc::Vector<zc::String> cloneStrings(zc::ArrayPtr<const zc::String> values) {
  zc::Vector<zc::String> result(values.size());
  for (const auto& value : values) { result.add(zc::str(value)); }
  return result;
}

template <typename T>
int compareScalar(const T& left, const T& right) {
  if (left < right) { return -1; }
  if (right < left) { return 1; }
  return 0;
}

int compareStrings(zc::ArrayPtr<const zc::String> left, zc::ArrayPtr<const zc::String> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    const int comparison = compareScalar(zc::StringPtr(left[index]), zc::StringPtr(right[index]));
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareRanges(zc::ArrayPtr<const DiagnosticFactRange> left,
                  zc::ArrayPtr<const DiagnosticFactRange> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    int comparison = compareScalar(left[index].byteStart, right[index].byteStart);
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(left[index].byteEnd, right[index].byteEnd);
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(left[index].isTokenRange, right[index].isTokenRange);
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareFixIts(zc::ArrayPtr<const DiagnosticFixItFact> left,
                  zc::ArrayPtr<const DiagnosticFixItFact> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    const DiagnosticFactRange leftRange[] = {left[index].range};
    const DiagnosticFactRange rightRange[] = {right[index].range};
    int comparison = compareRanges(zc::arrayPtr(leftRange), zc::arrayPtr(rightRange));
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(zc::StringPtr(left[index].replacementText),
                               zc::StringPtr(right[index].replacementText));
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareSecondary(zc::ArrayPtr<const SecondaryDiagnosticFact> left,
                     zc::ArrayPtr<const SecondaryDiagnosticFact> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    int comparison = compareScalar(static_cast<uint32_t>(left[index].code),
                                   static_cast<uint32_t>(right[index].code));
    if (comparison != 0) { return comparison; }
    comparison = compareScalar(left[index].primaryByteOffset, right[index].primaryByteOffset);
    if (comparison != 0) { return comparison; }
    comparison = compareStrings(left[index].arguments.asPtr(), right[index].arguments.asPtr());
    if (comparison != 0) { return comparison; }
    comparison = compareRanges(left[index].ranges.asPtr(), right[index].ranges.asPtr());
    if (comparison != 0) { return comparison; }
  }
  return compareScalar(left.size(), right.size());
}

int compareFactBase(const DiagnosticFact& left, const DiagnosticFact& right) {
  int comparison = compareScalar(left.primaryByteOffset, right.primaryByteOffset);
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(static_cast<uint8_t>(left.phase), static_cast<uint8_t>(right.phase));
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(zc::StringPtr(left.emitterFile), zc::StringPtr(right.emitterFile));
  if (comparison != 0) { return comparison; }
  comparison =
      compareScalar(zc::StringPtr(left.emitterFunction), zc::StringPtr(right.emitterFunction));
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(left.emitterLine, right.emitterLine);
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(left.emitterColumn, right.emitterColumn);
  if (comparison != 0) { return comparison; }
  comparison = compareScalar(static_cast<uint32_t>(left.code), static_cast<uint32_t>(right.code));
  if (comparison != 0) { return comparison; }
  comparison = compareStrings(left.arguments.asPtr(), right.arguments.asPtr());
  if (comparison != 0) { return comparison; }
  comparison = compareRanges(left.ranges.asPtr(), right.ranges.asPtr());
  if (comparison != 0) { return comparison; }
  comparison = compareFixIts(left.fixIts.asPtr(), right.fixIts.asPtr());
  if (comparison != 0) { return comparison; }
  return compareSecondary(left.secondary.asPtr(), right.secondary.asPtr());
}

void encodeRange(identity::CanonicalEncoder& encoder, const DiagnosticFactRange& range) {
  encoder.encodeUint64(range.byteStart);
  encoder.encodeUint64(range.byteEnd);
  encoder.encodeBool(range.isTokenRange);
}

zc::Maybe<DiagnosticFactRange> decodeRange(identity::CanonicalDecoder& decoder,
                                           uint64_t sourceByteLength) {
  auto start = decoder.decodeUint64();
  auto end = decoder.decodeUint64();
  auto isTokenRange = decoder.decodeBool();
  if (start == zc::none || end == zc::none || isTokenRange == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(start) > ZC_ASSERT_NONNULL(end) ||
      ZC_ASSERT_NONNULL(end) > sourceByteLength) {
    return zc::none;
  }
  return DiagnosticFactRange{ZC_ASSERT_NONNULL(start), ZC_ASSERT_NONNULL(end),
                             ZC_ASSERT_NONNULL(isTokenRange)};
}

void encodeStrings(identity::CanonicalEncoder& encoder, zc::ArrayPtr<const zc::String> values) {
  encoder.encodeSequenceSize(values.size());
  for (const auto& value : values) { encoder.encodeByteString(value.asBytes()); }
}

zc::Maybe<uint64_t> decodeFeasibleCount(identity::CanonicalDecoder& decoder, uint64_t maximumCount,
                                        uint64_t minimumElementBytes) {
  auto count = decoder.decodeSequenceSize(maximumCount);
  if (count == zc::none ||
      ZC_ASSERT_NONNULL(count) > static_cast<uint64_t>(static_cast<size_t>(zc::maxValue)) ||
      ZC_ASSERT_NONNULL(count) > decoder.remaining() / minimumElementBytes) {
    return zc::none;
  }
  return count;
}

template <typename T>
zc::Vector<T> emptyVector(zc::Maybe<zc::MemoryResource&> resource) {
  ZC_IF_SOME(value, resource) { return zc::Vector<T>(value); }
  return zc::Vector<T>();
}

zc::String ownedString(zc::Maybe<zc::MemoryResource&> resource, zc::ArrayPtr<const char> value) {
  ZC_IF_SOME(memory, resource) { return zc::resourceHeapString(memory, value); }
  return zc::heapString(value);
}

zc::Maybe<zc::Vector<zc::String>> decodeStrings(identity::CanonicalDecoder& decoder,
                                                zc::Maybe<zc::MemoryResource&> resultResource,
                                                uint64_t maximumCount) {
  auto count = decodeFeasibleCount(decoder, maximumCount, kMinimumEncodedStringBytes);
  if (count == zc::none) { return zc::none; }
  auto result = emptyVector<zc::String>(resultResource);
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto bytes = decoder.decodeByteString(kMaximumTextBytes);
    if (bytes == zc::none) { return zc::none; }
    result.add(ownedString(resultResource, ZC_ASSERT_NONNULL(bytes).asChars()));
  }
  return zc::mv(result);
}

void encodeRanges(identity::CanonicalEncoder& encoder,
                  zc::ArrayPtr<const DiagnosticFactRange> ranges) {
  encoder.encodeSequenceSize(ranges.size());
  for (const auto& range : ranges) { encodeRange(encoder, range); }
}

zc::Maybe<zc::Vector<DiagnosticFactRange>> decodeRanges(
    identity::CanonicalDecoder& decoder, zc::Maybe<zc::MemoryResource&> resultResource,
    uint64_t sourceByteLength) {
  auto count = decodeFeasibleCount(decoder, kMaximumRanges, kEncodedRangeBytes);
  if (count == zc::none) { return zc::none; }
  auto result = emptyVector<DiagnosticFactRange>(resultResource);
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto range = decodeRange(decoder, sourceByteLength);
    if (range == zc::none) { return zc::none; }
    result.add(ZC_ASSERT_NONNULL(range));
  }
  return zc::mv(result);
}

bool validCodeAndArity(DiagID code, size_t argumentCount) {
  return isSourceSyntaxDiagnostic(code) && getDiagnosticInfo(code).argCount == argumentCount;
}

bool measureStrings(EncodedSize& size, zc::ArrayPtr<const zc::String> values,
                    uint64_t maximumCount) {
  if (values.size() > maximumCount || !size.add(sizeof(uint64_t))) { return false; }
  for (const auto& value : values) {
    if (value.size() > kMaximumTextBytes || !size.addByteString(value.size())) { return false; }
  }
  return true;
}

bool validRange(const DiagnosticFactRange& range, uint64_t maximumSourceByteOffset) {
  return range.byteStart <= range.byteEnd && range.byteEnd <= maximumSourceByteOffset;
}

bool measureRanges(EncodedSize& size, zc::ArrayPtr<const DiagnosticFactRange> ranges,
                   uint64_t maximumSourceByteOffset) {
  if (ranges.size() > kMaximumRanges || !size.add(sizeof(uint64_t))) { return false; }
  for (const auto& range : ranges) {
    if (!validRange(range, maximumSourceByteOffset) ||
        !size.add(sizeof(uint64_t) * 2 + sizeof(uint8_t))) {
      return false;
    }
  }
  return true;
}

bool measureFact(EncodedSize& size, const DiagnosticFact& fact, uint64_t maximumSourceByteOffset) {
  if ((fact.phase != SourceDiagnosticPhase::Lex && fact.phase != SourceDiagnosticPhase::Parse) ||
      fact.emitterFile.size() == 0 || fact.emitterFile.size() > kMaximumEmitterBytes ||
      fact.emitterFunction.size() > kMaximumEmitterBytes || fact.emitterLine == 0 ||
      fact.primaryByteOffset > maximumSourceByteOffset ||
      !validCodeAndArity(fact.code, fact.arguments.size()) || !size.add(sizeof(uint8_t)) ||
      !size.addByteString(fact.emitterFile.size()) ||
      !size.addByteString(fact.emitterFunction.size()) || !size.add(sizeof(uint32_t) * 4) ||
      !size.add(sizeof(uint64_t)) ||
      !measureStrings(size, fact.arguments.asPtr(), kMaximumArguments) ||
      !measureRanges(size, fact.ranges.asPtr(), maximumSourceByteOffset) ||
      fact.fixIts.size() > kMaximumFixIts || !size.add(sizeof(uint64_t))) {
    return false;
  }
  for (const auto& fixIt : fact.fixIts) {
    if (!validRange(fixIt.range, maximumSourceByteOffset) ||
        fixIt.replacementText.size() > kMaximumTextBytes ||
        !size.add(sizeof(uint64_t) * 2 + sizeof(uint8_t)) ||
        !size.addByteString(fixIt.replacementText.size())) {
      return false;
    }
  }
  if (fact.secondary.size() > kMaximumSecondary || !size.add(sizeof(uint64_t))) { return false; }
  for (const auto& child : fact.secondary) {
    if (child.primaryByteOffset > maximumSourceByteOffset ||
        !validCodeAndArity(child.code, child.arguments.size()) ||
        !size.add(sizeof(uint32_t) + sizeof(uint64_t)) ||
        !measureStrings(size, child.arguments.asPtr(), kMaximumArguments) ||
        !measureRanges(size, child.ranges.asPtr(), maximumSourceByteOffset)) {
      return false;
    }
  }
  return true;
}

zc::Maybe<uint64_t> measureFacts(zc::ArrayPtr<const DiagnosticFact> facts,
                                 DiagnosticFactCodecLimits limits) {
  if (facts.size() > limits.maximumFacts) { return zc::none; }
  EncodedSize size(limits.maximumEncodedBytes);
  if (!size.addByteString(kDiagnosticFactsDomain.size()) || !size.add(sizeof(uint64_t))) {
    return zc::none;
  }
  for (size_t index = 0; index < facts.size(); ++index) {
    const auto& fact = facts[index];
    if (!measureFact(size, fact, limits.maximumSourceByteOffset)) { return zc::none; }
    if (index == 0) {
      if (fact.occurrenceOrdinal != 0) { return zc::none; }
      continue;
    }
    const auto& previous = facts[index - 1];
    const int baseComparison = compareFactBase(previous, fact);
    if (baseComparison > 0) { return zc::none; }
    if (baseComparison == 0) {
      if (previous.occurrenceOrdinal == static_cast<uint32_t>(zc::maxValue) ||
          fact.occurrenceOrdinal != previous.occurrenceOrdinal + 1) {
        return zc::none;
      }
    } else if (fact.occurrenceOrdinal != 0) {
      return zc::none;
    }
  }
  return size.get();
}

}  // namespace

DiagnosticFixItFact DiagnosticFixItFact::clone() const {
  return DiagnosticFixItFact{range, zc::str(replacementText)};
}

bool DiagnosticFixItFact::operator==(const DiagnosticFixItFact& other) const noexcept {
  return range == other.range && replacementText == other.replacementText;
}

SecondaryDiagnosticFact SecondaryDiagnosticFact::clone() const {
  zc::Vector<DiagnosticFactRange> retainedRanges(ranges.size());
  retainedRanges.addAll(ranges);
  return SecondaryDiagnosticFact{code, primaryByteOffset, cloneStrings(arguments.asPtr()),
                                 zc::mv(retainedRanges)};
}

bool SecondaryDiagnosticFact::operator==(const SecondaryDiagnosticFact& other) const noexcept {
  return code == other.code && primaryByteOffset == other.primaryByteOffset &&
         arguments == other.arguments && ranges == other.ranges;
}

DiagnosticFact DiagnosticFact::clone() const {
  zc::Vector<DiagnosticFactRange> retainedRanges(ranges.size());
  retainedRanges.addAll(ranges);
  zc::Vector<DiagnosticFixItFact> retainedFixIts(fixIts.size());
  for (const auto& fixIt : fixIts) { retainedFixIts.add(fixIt.clone()); }
  zc::Vector<SecondaryDiagnosticFact> retainedSecondary(secondary.size());
  for (const auto& child : secondary) { retainedSecondary.add(child.clone()); }
  return DiagnosticFact{phase,
                        zc::str(emitterFile),
                        zc::str(emitterFunction),
                        emitterLine,
                        emitterColumn,
                        occurrenceOrdinal,
                        code,
                        primaryByteOffset,
                        cloneStrings(arguments.asPtr()),
                        zc::mv(retainedRanges),
                        zc::mv(retainedFixIts),
                        zc::mv(retainedSecondary)};
}

bool DiagnosticFact::operator==(const DiagnosticFact& other) const noexcept {
  return phase == other.phase && emitterFile == other.emitterFile &&
         emitterFunction == other.emitterFunction && emitterLine == other.emitterLine &&
         emitterColumn == other.emitterColumn && occurrenceOrdinal == other.occurrenceOrdinal &&
         code == other.code && primaryByteOffset == other.primaryByteOffset &&
         arguments == other.arguments && ranges == other.ranges && fixIts == other.fixIts &&
         secondary == other.secondary;
}

bool isSourceSyntaxDiagnostic(DiagID code) noexcept {
  switch (code) {
#define DIAG(Code, Name, ...) \
  case DiagID::Name:          \
    return true;
#include "zomlang/compiler/diagnostics/diagnostics-parse.def"
#undef DIAG
    default:
      return false;
  }
}

zc::Vector<DiagnosticFact> canonicalizeDiagnosticFacts(zc::Vector<DiagnosticFact>&& facts) {
  for (auto& fact : facts) { fact.occurrenceOrdinal = 0; }
  for (size_t index = 0; index < facts.size(); ++index) {
    size_t smallest = index;
    for (size_t candidate = index + 1; candidate < facts.size(); ++candidate) {
      if (compareFactBase(facts[candidate], facts[smallest]) < 0) { smallest = candidate; }
    }
    if (smallest != index) {
      DiagnosticFact retained = zc::mv(facts[index]);
      facts[index] = zc::mv(facts[smallest]);
      facts[smallest] = zc::mv(retained);
    }
  }
  for (size_t index = 1; index < facts.size(); ++index) {
    if (compareFactBase(facts[index - 1], facts[index]) == 0) {
      facts[index].occurrenceOrdinal = facts[index - 1].occurrenceOrdinal + 1;
    }
  }
  return zc::mv(facts);
}

zc::Maybe<zc::Array<uint8_t>> encodeDiagnosticFacts(zc::Maybe<zc::MemoryResource&> outputResource,
                                                    zc::ArrayPtr<const DiagnosticFact> facts,
                                                    DiagnosticFactCodecLimits limits) {
  auto encodedSize = measureFacts(facts, limits);
  if (encodedSize == zc::none) { return zc::none; }
  zc::Maybe<identity::CanonicalEncoder> exactEncoder;
  ZC_IF_SOME(resource, outputResource) {
    exactEncoder =
        identity::CanonicalEncoder::forExactSize(resource, ZC_ASSERT_NONNULL(encodedSize));
  } else {
    exactEncoder = identity::CanonicalEncoder::forExactSize(ZC_ASSERT_NONNULL(encodedSize));
  }
  if (exactEncoder == zc::none) { return zc::none; }
  auto encoder = zc::mv(ZC_ASSERT_NONNULL(exactEncoder));
  encoder.encodeByteString(kDiagnosticFactsDomain.asBytes());
  encoder.encodeSequenceSize(facts.size());
  for (const auto& fact : facts) {
    encoder.encodeUint8(static_cast<uint8_t>(fact.phase));
    encoder.encodeByteString(fact.emitterFile.asBytes());
    encoder.encodeByteString(fact.emitterFunction.asBytes());
    encoder.encodeUint32(fact.emitterLine);
    encoder.encodeUint32(fact.emitterColumn);
    encoder.encodeUint32(fact.occurrenceOrdinal);
    encoder.encodeUint32(static_cast<uint32_t>(fact.code));
    encoder.encodeUint64(fact.primaryByteOffset);
    encodeStrings(encoder, fact.arguments.asPtr());
    encodeRanges(encoder, fact.ranges.asPtr());
    encoder.encodeSequenceSize(fact.fixIts.size());
    for (const auto& fixIt : fact.fixIts) {
      encodeRange(encoder, fixIt.range);
      encoder.encodeByteString(fixIt.replacementText.asBytes());
    }
    encoder.encodeSequenceSize(fact.secondary.size());
    for (const auto& child : fact.secondary) {
      encoder.encodeUint32(static_cast<uint32_t>(child.code));
      encoder.encodeUint64(child.primaryByteOffset);
      encodeStrings(encoder, child.arguments.asPtr());
      encodeRanges(encoder, child.ranges.asPtr());
    }
  }
  return encoder.finish();
}

zc::Maybe<zc::Vector<DiagnosticFact>> decodeDiagnosticFacts(
    zc::Maybe<zc::MemoryResource&> resultResource, zc::ArrayPtr<const uint8_t> encoded,
    DiagnosticFactCodecLimits limits) {
  if (encoded.size() > limits.maximumEncodedBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(encoded);
  auto domain = decoder.decodeByteString(kDiagnosticFactsDomain.size());
  if (domain == zc::none || ZC_ASSERT_NONNULL(domain).asPtr() != kDiagnosticFactsDomain.asBytes()) {
    return zc::none;
  }
  auto count = decodeFeasibleCount(decoder, limits.maximumFacts, kMinimumEncodedFactBytes);
  if (count == zc::none) { return zc::none; }

  auto facts = emptyVector<DiagnosticFact>(resultResource);
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto phase = decoder.decodeUint8();
    auto emitterFile = decoder.decodeByteString(kMaximumEmitterBytes);
    auto emitterFunction = decoder.decodeByteString(kMaximumEmitterBytes);
    auto emitterLine = decoder.decodeUint32();
    auto emitterColumn = decoder.decodeUint32();
    auto occurrenceOrdinal = decoder.decodeUint32();
    auto codeValue = decoder.decodeUint32();
    auto primary = decoder.decodeUint64();
    auto arguments = decodeStrings(decoder, resultResource, kMaximumArguments);
    auto ranges = decodeRanges(decoder, resultResource, limits.maximumSourceByteOffset);
    if (phase == zc::none || emitterFile == zc::none || emitterFunction == zc::none ||
        emitterLine == zc::none || emitterColumn == zc::none || occurrenceOrdinal == zc::none ||
        codeValue == zc::none || primary == zc::none || arguments == zc::none ||
        ranges == zc::none) {
      return zc::none;
    }
    const auto decodedPhase = static_cast<SourceDiagnosticPhase>(ZC_ASSERT_NONNULL(phase));
    const auto code = static_cast<DiagID>(ZC_ASSERT_NONNULL(codeValue));
    if ((decodedPhase != SourceDiagnosticPhase::Lex &&
         decodedPhase != SourceDiagnosticPhase::Parse) ||
        ZC_ASSERT_NONNULL(emitterFile).size() == 0 || ZC_ASSERT_NONNULL(emitterLine) == 0 ||
        ZC_ASSERT_NONNULL(primary) > limits.maximumSourceByteOffset ||
        !validCodeAndArity(code, ZC_ASSERT_NONNULL(arguments).size())) {
      return zc::none;
    }

    auto fixItCount = decodeFeasibleCount(decoder, kMaximumFixIts, kMinimumEncodedFixItBytes);
    if (fixItCount == zc::none) { return zc::none; }
    auto fixIts = emptyVector<DiagnosticFixItFact>(resultResource);
    for (uint64_t fixItIndex = 0; fixItIndex < ZC_ASSERT_NONNULL(fixItCount); ++fixItIndex) {
      auto range = decodeRange(decoder, limits.maximumSourceByteOffset);
      auto replacement = decoder.decodeByteString(kMaximumTextBytes);
      if (range == zc::none || replacement == zc::none) { return zc::none; }
      fixIts.add(DiagnosticFixItFact{
          ZC_ASSERT_NONNULL(range),
          ownedString(resultResource, ZC_ASSERT_NONNULL(replacement).asChars())});
    }

    auto secondaryCount =
        decodeFeasibleCount(decoder, kMaximumSecondary, kMinimumEncodedSecondaryBytes);
    if (secondaryCount == zc::none) { return zc::none; }
    auto secondary = emptyVector<SecondaryDiagnosticFact>(resultResource);
    for (uint64_t childIndex = 0; childIndex < ZC_ASSERT_NONNULL(secondaryCount); ++childIndex) {
      auto childCodeValue = decoder.decodeUint32();
      auto childPrimary = decoder.decodeUint64();
      auto childArguments = decodeStrings(decoder, resultResource, kMaximumArguments);
      auto childRanges = decodeRanges(decoder, resultResource, limits.maximumSourceByteOffset);
      if (childCodeValue == zc::none || childPrimary == zc::none || childArguments == zc::none ||
          childRanges == zc::none ||
          ZC_ASSERT_NONNULL(childPrimary) > limits.maximumSourceByteOffset) {
        return zc::none;
      }
      const auto childCode = static_cast<DiagID>(ZC_ASSERT_NONNULL(childCodeValue));
      if (!validCodeAndArity(childCode, ZC_ASSERT_NONNULL(childArguments).size())) {
        return zc::none;
      }
      secondary.add(SecondaryDiagnosticFact{childCode, ZC_ASSERT_NONNULL(childPrimary),
                                            zc::mv(ZC_ASSERT_NONNULL(childArguments)),
                                            zc::mv(ZC_ASSERT_NONNULL(childRanges))});
    }

    DiagnosticFact fact{decodedPhase,
                        ownedString(resultResource, ZC_ASSERT_NONNULL(emitterFile).asChars()),
                        ownedString(resultResource, ZC_ASSERT_NONNULL(emitterFunction).asChars()),
                        ZC_ASSERT_NONNULL(emitterLine),
                        ZC_ASSERT_NONNULL(emitterColumn),
                        ZC_ASSERT_NONNULL(occurrenceOrdinal),
                        code,
                        ZC_ASSERT_NONNULL(primary),
                        zc::mv(ZC_ASSERT_NONNULL(arguments)),
                        zc::mv(ZC_ASSERT_NONNULL(ranges)),
                        zc::mv(fixIts),
                        zc::mv(secondary)};
    if (facts.size() != 0) {
      const auto& previous = facts.back();
      const int baseComparison = compareFactBase(previous, fact);
      if (baseComparison > 0) { return zc::none; }
      if (baseComparison == 0 &&
          previous.occurrenceOrdinal == static_cast<uint32_t>(zc::maxValue)) {
        return zc::none;
      }
      const uint32_t expectedOrdinal = baseComparison == 0 ? previous.occurrenceOrdinal + 1 : 0;
      if (fact.occurrenceOrdinal != expectedOrdinal) { return zc::none; }
    } else if (fact.occurrenceOrdinal != 0) {
      return zc::none;
    }
    facts.add(zc::mv(fact));
  }
  if (!decoder.finished()) { return zc::none; }
  {
    auto canonical = encodeDiagnosticFacts(resultResource, facts.asPtr(), limits);
    if (canonical == zc::none || ZC_ASSERT_NONNULL(canonical).asPtr() != encoded) {
      return zc::none;
    }
  }
  return zc::mv(facts);
}

}  // namespace zomlang::compiler::diagnostics
