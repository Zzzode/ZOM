// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/consumer/diagnostic-policy.h"

#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::diagnostics {
namespace {

zc::Vector<uint32_t> primaryPath(uint32_t occurrence) {
  zc::Vector<uint32_t> result;
  result.add(occurrence);
  result.add(0);
  return result;
}

DiagnosticFact fact(uint32_t occurrence, DiagID code) {
  auto occurrenceKey = ZC_REQUIRE_NONNULL(DiagnosticOccurrenceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, occurrence));
  auto primary = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, primaryPath(occurrence)));
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  return ZC_REQUIRE_NONNULL(DiagnosticFact::from(zc::mv(occurrenceKey), code, zc::mv(arguments),
                                                 zc::mv(primary), zc::mv(secondary)));
}

SourceDiagnosticProvenanceMap provenance(size_t count) {
  zc::Vector<SourceDiagnosticProvenanceEntry> entries(count);
  for (size_t index = 0; index < count; ++index) {
    entries.add(SourceDiagnosticProvenanceEntry{
        ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
            tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
            SourceDiagnosticEmitter::Lexer, primaryPath(static_cast<uint32_t>(index)))),
        DiagnosticSourceRange{index, index, false}});
  }
  return ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(entries), count));
}

}  // namespace

ZC_TEST("DiagnosticPolicy limits errors without mutating the authoritative batch") {
  constexpr size_t errorCount = 101;
  zc::Vector<DiagnosticFact> facts(errorCount + 1);
  for (uint32_t occurrence = 0; occurrence < errorCount; ++occurrence) {
    facts.add(fact(occurrence, DiagID::InvalidCharacter));
  }
  facts.add(fact(errorCount, DiagID::DanglingHashHelp));
  auto provenanceMap = provenance(facts.size());
  auto source = tests::test_identity_detail::source();
  SourceDiagnosticProvenanceResolver resolver(source, provenanceMap);
  auto materialized = materializeDiagnosticFacts(facts.asPtr(), resolver);
  ZC_REQUIRE(materialized.is<ResolvedDiagnosticBatch>());

  auto result = applyDiagnosticPolicy(zc::mv(materialized.get<ResolvedDiagnosticBatch>()),
                                      DiagnosticPolicy());
  ZC_EXPECT(result.authoritative().size() == errorCount + 1);
  ZC_EXPECT(result.errorCount() == errorCount);
  ZC_EXPECT(result.omittedCount() == 1);
  ZC_EXPECT(result.displayedCount() == errorCount);
  ZC_EXPECT(result.displayed(result.displayedCount() - 1).code() == DiagID::DanglingHashHelp);
}

ZC_TEST("DiagnosticPolicy zero error limit still admits non-error diagnostics") {
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(0, DiagID::InvalidCharacter));
  facts.add(fact(1, DiagID::DanglingHashHelp));
  auto provenanceMap = provenance(facts.size());
  auto source = tests::test_identity_detail::source();
  SourceDiagnosticProvenanceResolver resolver(source, provenanceMap);
  auto materialized = materializeDiagnosticFacts(facts.asPtr(), resolver);
  ZC_REQUIRE(materialized.is<ResolvedDiagnosticBatch>());

  auto result = applyDiagnosticPolicy(zc::mv(materialized.get<ResolvedDiagnosticBatch>()),
                                      DiagnosticPolicy(0));
  ZC_EXPECT(result.authoritative().size() == 2);
  ZC_EXPECT(result.errorCount() == 1);
  ZC_EXPECT(result.omittedCount() == 1);
  ZC_REQUIRE(result.displayedCount() == 1);
  ZC_EXPECT(result.displayed(0).code() == DiagID::DanglingHashHelp);
}

}  // namespace zomlang::compiler::diagnostics
