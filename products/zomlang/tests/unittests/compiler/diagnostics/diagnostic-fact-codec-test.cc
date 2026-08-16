// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::diagnostics {
namespace {

constexpr DiagnosticFactCodecLimits kFactLimits{
    .maximumFacts = 4096,
    .maximumEncodedBytes = 64 * 1024 * 1024,
    .maximumProvenanceComponentsPerKey = 3,
    .maximumArgumentBytesPerRecord = 64 * 1024 * 1024,
    .maximumSecondaryPerFact = 128,
};
constexpr DiagnosticProvenanceCodecLimits kProvenanceLimits{
    .maximumEntries = 528384,
    .maximumEncodedBytes = 64 * 1024 * 1024,
    .maximumProvenanceComponentsPerKey = 3,
    .maximumSourceByteOffset = 32,
};

zc::Vector<uint32_t> path(uint32_t occurrence, uint32_t slot,
                          zc::Maybe<uint32_t> ordinal = zc::none) {
  zc::Vector<uint32_t> result;
  result.add(occurrence);
  result.add(slot);
  ZC_IF_SOME(value, ordinal) { result.add(value); }
  return result;
}

DiagnosticProvenanceKey provenanceKey(uint32_t occurrence, uint32_t slot,
                                      zc::Maybe<uint32_t> ordinal = zc::none) {
  return ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, path(occurrence, slot, ordinal)));
}

DiagnosticFact fact(uint32_t occurrence, bool populated = false) {
  auto occurrenceKey = ZC_REQUIRE_NONNULL(DiagnosticOccurrenceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, occurrence));
  auto primary = provenanceKey(occurrence, 0);
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  if (populated) {
    secondary.add(ZC_REQUIRE_NONNULL(
        DiagnosticSecondary::highlight(provenanceKey(occurrence, 1, uint32_t{0}))));
    zc::Vector<zc::String> noteArguments;
    noteArguments.add(zc::str("identifier"_zc));
    secondary.add(ZC_REQUIRE_NONNULL(DiagnosticSecondary::note(
        DiagID::ExpectedToken, provenanceKey(occurrence, 2, uint32_t{0}), zc::mv(noteArguments))));
  }
  return ZC_REQUIRE_NONNULL(DiagnosticFact::from(zc::mv(occurrenceKey), DiagID::InvalidCharacter,
                                                 zc::mv(arguments), zc::mv(primary),
                                                 zc::mv(secondary)));
}

SourceDiagnosticProvenanceMap provenanceMap(bool populated = false) {
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(
      SourceDiagnosticProvenanceEntry{provenanceKey(0, 0), DiagnosticSourceRange{4, 4, false}});
  if (populated) {
    entries.add(SourceDiagnosticProvenanceEntry{provenanceKey(0, 1, uint32_t{0}),
                                                DiagnosticSourceRange{4, 8, true}});
    entries.add(SourceDiagnosticProvenanceEntry{provenanceKey(0, 2, uint32_t{0}),
                                                DiagnosticSourceRange{9, 9, false}});
  }
  return ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(entries), 32));
}

zc::Array<uint8_t> encodeOne(bool populated = false) {
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(0, populated));
  return ZC_REQUIRE_NONNULL(encodeDiagnosticFacts(zc::none, facts.asPtr(), kFactLimits));
}

}  // namespace

ZC_TEST("DiagnosticFactCodec round-trips the closed source fact model") {
  zc::Vector<DiagnosticFact> expected;
  expected.add(fact(0, true));
  auto encoded = ZC_REQUIRE_NONNULL(encodeDiagnosticFacts(zc::none, expected.asPtr(), kFactLimits));
  auto decoded = ZC_REQUIRE_NONNULL(decodeDiagnosticFacts(zc::none, encoded.asPtr(), kFactLimits));
  ZC_EXPECT(decoded == expected);
  auto reencoded =
      ZC_REQUIRE_NONNULL(encodeDiagnosticFacts(zc::none, decoded.asPtr(), kFactLimits));
  ZC_EXPECT(reencoded.asPtr() == encoded.asPtr());
}

ZC_TEST("DiagnosticFactCodec enforces complete sequence and record limits") {
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(0, true));
  auto encoded = ZC_REQUIRE_NONNULL(encodeDiagnosticFacts(zc::none, facts.asPtr(), kFactLimits));

  auto limits = kFactLimits;
  limits.maximumFacts = 0;
  ZC_EXPECT(encodeDiagnosticFacts(zc::none, facts.asPtr(), limits) == zc::none);
  limits = kFactLimits;
  limits.maximumEncodedBytes = encoded.size() - 1;
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  ZC_EXPECT(encodeDiagnosticFacts(resource, facts.asPtr(), limits) == zc::none);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_EXPECT(resource.peakAllocatedBytes() == 0);
  limits = kFactLimits;
  limits.maximumProvenanceComponentsPerKey = 2;
  ZC_EXPECT(encodeDiagnosticFacts(zc::none, facts.asPtr(), limits) == zc::none);
  limits = kFactLimits;
  limits.maximumSecondaryPerFact = 1;
  ZC_EXPECT(encodeDiagnosticFacts(zc::none, facts.asPtr(), limits) == zc::none);
  limits = kFactLimits;
  limits.maximumArgumentBytesPerRecord = 9;
  ZC_EXPECT(encodeDiagnosticFacts(zc::none, facts.asPtr(), limits) == zc::none);
}

ZC_TEST("DiagnosticFactCodec rejects truncation trailing bytes and order drift") {
  auto encoded = encodeOne(true);
  for (size_t size = 0; size < encoded.size(); ++size) {
    ZC_EXPECT(decodeDiagnosticFacts(zc::none, encoded.first(size), kFactLimits) == zc::none);
  }
  zc::Vector<uint8_t> trailing(encoded.size() + 1);
  trailing.addAll(encoded.asPtr());
  trailing.add(0);
  ZC_EXPECT(decodeDiagnosticFacts(zc::none, trailing.asPtr(), kFactLimits) == zc::none);

  zc::Vector<DiagnosticFact> reordered;
  reordered.add(fact(1));
  reordered.add(fact(0));
  ZC_EXPECT(encodeDiagnosticFacts(zc::none, reordered.asPtr(), kFactLimits) == zc::none);
}

ZC_TEST("DiagnosticProvenanceCodec round-trips and validates exact bijection") {
  auto map = provenanceMap(true);
  auto encoded =
      ZC_REQUIRE_NONNULL(encodeSourceDiagnosticProvenance(zc::none, map, kProvenanceLimits));
  auto decoded = ZC_REQUIRE_NONNULL(
      decodeSourceDiagnosticProvenance(zc::none, encoded.asPtr(), 32, kProvenanceLimits));
  ZC_EXPECT(decoded == map);

  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(0, true));
  ZC_EXPECT(validateDiagnosticProvenance(facts.asPtr(), decoded));
  facts = nullptr;
  facts.add(fact(0, false));
  ZC_EXPECT(!validateDiagnosticProvenance(facts.asPtr(), decoded));
}

ZC_TEST("DiagnosticProvenanceCodec rejects malformed framing and boundaries") {
  auto map = provenanceMap(true);
  auto encoded =
      ZC_REQUIRE_NONNULL(encodeSourceDiagnosticProvenance(zc::none, map, kProvenanceLimits));
  for (size_t size = 0; size < encoded.size(); ++size) {
    ZC_EXPECT(decodeSourceDiagnosticProvenance(zc::none, encoded.first(size), 32,
                                               kProvenanceLimits) == zc::none);
  }
  auto limits = kProvenanceLimits;
  limits.maximumEntries = 2;
  ZC_EXPECT(encodeSourceDiagnosticProvenance(zc::none, map, limits) == zc::none);
  limits = kProvenanceLimits;
  limits.maximumEncodedBytes = encoded.size() - 1;
  ZC_EXPECT(encodeSourceDiagnosticProvenance(zc::none, map, limits) == zc::none);
  limits = kProvenanceLimits;
  limits.maximumSourceByteOffset = 31;
  ZC_EXPECT(encodeSourceDiagnosticProvenance(zc::none, map, limits) == zc::none);
}

ZC_TEST("DiagnosticFact factories reject foreign topology and ordinal gaps") {
  auto occurrence = ZC_REQUIRE_NONNULL(
      DiagnosticOccurrenceKey::from(tests::test_identity_detail::source(),
                                    SourceDiagnosticPhase::Lex, SourceDiagnosticEmitter::Lexer, 0));
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  secondary.add(
      ZC_REQUIRE_NONNULL(DiagnosticSecondary::highlight(provenanceKey(0, 1, uint32_t{1}))));
  ZC_EXPECT(DiagnosticFact::from(zc::mv(occurrence), DiagID::InvalidCharacter, zc::mv(arguments),
                                 provenanceKey(0, 0), zc::mv(secondary)) == zc::none);

  ZC_EXPECT(DiagnosticOccurrenceKey::from(tests::test_identity_detail::source(),
                                          SourceDiagnosticPhase::Parse,
                                          SourceDiagnosticEmitter::Lexer, 0) == zc::none);
  ZC_EXPECT(DiagnosticProvenanceKey::from(tests::test_identity_detail::source(),
                                          SourceDiagnosticPhase::Lex,
                                          SourceDiagnosticEmitter::Lexer, path(0, 3)) == zc::none);
}

}  // namespace zomlang::compiler::diagnostics
