// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/parser/parse-source-query.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::parser {
namespace {

zc::Vector<uint32_t> primaryPath(uint32_t occurrence) {
  zc::Vector<uint32_t> result;
  result.add(occurrence);
  result.add(0);
  return result;
}

diagnostics::DiagnosticFact fact(uint32_t occurrence) {
  auto key = tests::test_identity_detail::source();
  auto occurrenceKey = ZC_REQUIRE_NONNULL(diagnostics::DiagnosticOccurrenceKey::from(
      key.clone(), diagnostics::SourceDiagnosticPhase::Lex,
      diagnostics::SourceDiagnosticEmitter::Lexer, occurrence));
  auto primary = ZC_REQUIRE_NONNULL(diagnostics::DiagnosticProvenanceKey::from(
      zc::mv(key), diagnostics::SourceDiagnosticPhase::Lex,
      diagnostics::SourceDiagnosticEmitter::Lexer, primaryPath(occurrence)));
  zc::Vector<zc::String> arguments;
  zc::Vector<diagnostics::DiagnosticSecondary> secondary;
  return ZC_REQUIRE_NONNULL(diagnostics::DiagnosticFact::from(
      zc::mv(occurrenceKey), diagnostics::DiagID::InvalidCharacter, zc::mv(arguments),
      zc::mv(primary), zc::mv(secondary)));
}

diagnostics::SourceDiagnosticProvenanceMap provenance(uint32_t count) {
  zc::Vector<diagnostics::SourceDiagnosticProvenanceEntry> entries(count);
  for (uint32_t occurrence = 0; occurrence < count; ++occurrence) {
    auto key = ZC_REQUIRE_NONNULL(diagnostics::DiagnosticProvenanceKey::from(
        tests::test_identity_detail::source(), diagnostics::SourceDiagnosticPhase::Lex,
        diagnostics::SourceDiagnosticEmitter::Lexer, primaryPath(occurrence)));
    entries.add(diagnostics::SourceDiagnosticProvenanceEntry{
        zc::mv(key), diagnostics::DiagnosticSourceRange{occurrence, occurrence, false}});
  }
  return ZC_REQUIRE_NONNULL(
      diagnostics::SourceDiagnosticProvenanceMap::from(zc::mv(entries), count));
}

zc::Array<uint8_t> sourceKey() {
  auto source = tests::test_identity_detail::source();
  auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
  ZC_REQUIRE(stable != zc::none);
  return zc::heapArray<uint8_t>(ZC_ASSERT_NONNULL(stable).canonicalSourceBytes());
}

zc::Maybe<ParseRejected> rejected(uint32_t count) {
  zc::Vector<diagnostics::DiagnosticFact> facts(count);
  for (uint32_t occurrence = 0; occurrence < count; ++occurrence) { facts.add(fact(occurrence)); }
  auto key = sourceKey();
  return ParseRejected::fromFacts(key.asPtr(), tests::test_identity_detail::digest(0x36), count,
                                  CanonicalParserOptions{true, false, true}, zc::mv(facts),
                                  provenance(count));
}

}  // namespace

ZC_TEST("ParseRejected admits exactly 4096 source diagnostic facts and provenance entries") {
  auto value = rejected(4096);
  ZC_REQUIRE(value != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(value).facts().size() == 4096);
  ZC_EXPECT(ZC_ASSERT_NONNULL(value).provenance().entries().size() == 4096);

  auto encoded = ZC_ASSERT_NONNULL(value).encodeCanonical();
  auto decoded = ParseRejected::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decoded).facts().size() == 4096);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decoded).encodeCanonical().asPtr() == encoded.asPtr());
}

ZC_TEST("ParseRejected rejects the 4097th source diagnostic fact") {
  ZC_EXPECT(rejected(4097) == zc::none);
}

ZC_TEST("ParseRejected rejects an incomplete provenance authority") {
  zc::Vector<diagnostics::DiagnosticFact> facts;
  facts.add(fact(0));
  auto empty = provenance(0);
  auto key = sourceKey();
  ZC_EXPECT(ParseRejected::fromFacts(key.asPtr(), tests::test_identity_detail::digest(0x36), 1,
                                     CanonicalParserOptions{true, false, true}, zc::mv(facts),
                                     zc::mv(empty)) == zc::none);
}

}  // namespace zomlang::compiler::parser
