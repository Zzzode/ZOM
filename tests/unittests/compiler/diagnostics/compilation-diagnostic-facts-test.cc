// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/compilation-diagnostic-facts.h"

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

DiagnosticFact fact(uint32_t occurrence, DiagID code = DiagID::InvalidCharacter) {
  auto occurrenceKey = ZC_REQUIRE_NONNULL(DiagnosticOccurrenceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, occurrence));
  auto primary = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, primaryPath(occurrence)));
  zc::Vector<zc::String> arguments;
  if (code == DiagID::ExpectedToken) { arguments.add(zc::str("identifier"_zc)); }
  zc::Vector<DiagnosticSecondary> secondary;
  return ZC_REQUIRE_NONNULL(DiagnosticFact::from(zc::mv(occurrenceKey), code, zc::mv(arguments),
                                                 zc::mv(primary), zc::mv(secondary)));
}

SourceDiagnosticProvenanceEntry provenance(uint32_t occurrence, uint64_t offset) {
  return SourceDiagnosticProvenanceEntry{
      ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
          tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
          SourceDiagnosticEmitter::Lexer, primaryPath(occurrence))),
      DiagnosticSourceRange{offset, offset, false}};
}

zc::Array<uint8_t> documentIdentity(uint8_t discriminator) {
  const uint8_t bytes[] = {0x44, 0x4f, 0x43, discriminator};
  return zc::heapArray<uint8_t>(bytes);
}

zc::Array<uint8_t> documentOccurrence(uint8_t discriminator) {
  const uint8_t bytes[] = {0x4f, 0x43, 0x43, discriminator};
  return zc::heapArray<uint8_t>(bytes);
}

DiagnosticFact documentFact(uint8_t discriminator = 1) {
  auto document = documentIdentity(1);
  auto occurrenceBytes = documentOccurrence(discriminator);
  auto occurrence = ZC_REQUIRE_NONNULL(DiagnosticOccurrenceKey::document(
      zc::heapArray<uint8_t>(document.asPtr()), zc::heapArray<uint8_t>(occurrenceBytes.asPtr())));
  auto primary = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::documentSite(
      zc::mv(document), zc::mv(occurrenceBytes), DocumentDiagnosticSiteRole::Primary, 0));
  zc::Vector<zc::String> arguments;
  arguments.add(zc::str("toml-syntax"_zc));
  zc::Vector<DiagnosticSecondary> secondary;
  return ZC_REQUIRE_NONNULL(DiagnosticFact::from(zc::mv(occurrence), DiagID::PackageManifestInvalid,
                                                 zc::mv(arguments), zc::mv(primary),
                                                 zc::mv(secondary)));
}

SourceDiagnosticProvenanceEntry documentProvenance(uint8_t discriminator, uint64_t offset) {
  return SourceDiagnosticProvenanceEntry{ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::documentSite(
                                             documentIdentity(1), documentOccurrence(discriminator),
                                             DocumentDiagnosticSiteRole::Primary, 0)),
                                         DiagnosticSourceRange{offset, offset, false}};
}

}  // namespace

ZC_TEST("DiagnosticFactCollector canonicalizes root order and preserves occurrences") {
  zc::Vector<DiagnosticFact> first;
  first.add(fact(2));
  first.add(fact(0));
  zc::Vector<DiagnosticFact> second;
  second.add(fact(1));

  DiagnosticFactCollector collector;
  ZC_REQUIRE(collector.addRoot(first.asPtr()));
  ZC_REQUIRE(collector.addRoot(second.asPtr()));
  auto result = zc::mv(collector).seal();
  ZC_REQUIRE(result.is<CompilationDiagnosticFacts>());
  const auto facts = result.get<CompilationDiagnosticFacts>().facts();
  ZC_REQUIRE(facts.size() == 3);
  ZC_EXPECT(facts[0].occurrence().occurrence() == 0);
  ZC_EXPECT(facts[1].occurrence().occurrence() == 1);
  ZC_EXPECT(facts[2].occurrence().occurrence() == 2);
}

ZC_TEST("DiagnosticFactCollector collapses equal duplicate occurrences") {
  zc::Vector<DiagnosticFact> first;
  first.add(fact(7));
  zc::Vector<DiagnosticFact> second;
  second.add(first[0].clone());

  DiagnosticFactCollector collector;
  ZC_REQUIRE(collector.addRoot(first.asPtr()));
  ZC_REQUIRE(collector.addRoot(second.asPtr()));
  auto result = zc::mv(collector).seal();
  ZC_REQUIRE(result.is<CompilationDiagnosticFacts>());
  ZC_EXPECT(result.get<CompilationDiagnosticFacts>().facts().size() == 1);
}

ZC_TEST("DiagnosticFactCollector rejects conflicting payloads for one occurrence") {
  zc::Vector<DiagnosticFact> first;
  first.add(fact(11));
  zc::Vector<DiagnosticFact> second;
  second.add(fact(11, DiagID::ExpectedToken));

  DiagnosticFactCollector collector;
  ZC_REQUIRE(collector.addRoot(first.asPtr()));
  ZC_REQUIRE(collector.addRoot(second.asPtr()));
  auto result = zc::mv(collector).seal();
  ZC_REQUIRE(result.is<DiagnosticCollectionFailure>());
  ZC_EXPECT(result.get<DiagnosticCollectionFailure>() ==
            DiagnosticCollectionFailure::ConflictingOccurrence);
}

ZC_TEST("CompilationDiagnosticCollector seals facts with source authorities") {
  auto source = tests::test_identity_detail::source();
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(1));
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(provenance(1, 3));

  CompilationDiagnosticCollector collector;
  ZC_REQUIRE(collector.addSourceAuthority(source, 8));
  ZC_REQUIRE(collector.addRoot(facts.asPtr(), entries.asPtr()));
  ZC_EXPECT(collector.hasErrors());
  auto result = zc::mv(collector).seal();
  ZC_REQUIRE(result.is<CompilationDiagnosticFacts>());
  const auto& sealed = result.get<CompilationDiagnosticFacts>();
  ZC_REQUIRE(sealed.facts().size() == 1);
  ZC_REQUIRE(sealed.sources().size() == 1);
  ZC_EXPECT(sealed.sources()[0].source.sameAs(source));
  ZC_EXPECT(sealed.sources()[0].provenance.sourceByteLength() == 8);
  ZC_EXPECT(sealed.sources()[0].provenance.entries().size() == 1);
}

ZC_TEST("CompilationDiagnosticCollector collapses equal provenance and rejects conflicts") {
  auto source = tests::test_identity_detail::source();
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(2));
  zc::Vector<SourceDiagnosticProvenanceEntry> first;
  first.add(provenance(2, 4));
  zc::Vector<SourceDiagnosticProvenanceEntry> equal;
  equal.add(first[0].clone());

  CompilationDiagnosticCollector accepted;
  ZC_REQUIRE(accepted.addSourceAuthority(source, 8));
  ZC_REQUIRE(accepted.addRoot(facts.asPtr(), first.asPtr()));
  ZC_REQUIRE(accepted.addRoot(facts.asPtr(), equal.asPtr()));
  auto acceptedResult = zc::mv(accepted).seal();
  ZC_REQUIRE(acceptedResult.is<CompilationDiagnosticFacts>());
  ZC_EXPECT(
      acceptedResult.get<CompilationDiagnosticFacts>().sources()[0].provenance.entries().size() ==
      1);

  zc::Vector<SourceDiagnosticProvenanceEntry> conflicting;
  conflicting.add(provenance(2, 5));
  CompilationDiagnosticCollector rejected;
  ZC_REQUIRE(rejected.addSourceAuthority(source, 8));
  ZC_REQUIRE(rejected.addRoot(facts.asPtr(), first.asPtr()));
  ZC_REQUIRE(rejected.addRoot(facts.asPtr(), conflicting.asPtr()));
  auto rejectedResult = zc::mv(rejected).seal();
  ZC_REQUIRE(rejectedResult.is<DiagnosticCollectionFailure>());
  ZC_EXPECT(rejectedResult.get<DiagnosticCollectionFailure>() ==
            DiagnosticCollectionFailure::ConflictingProvenance);
}

ZC_TEST("CompilationDiagnosticCollector rejects missing and conflicting source authorities") {
  auto source = tests::test_identity_detail::source();
  zc::Vector<DiagnosticFact> facts;
  facts.add(fact(3));
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(provenance(3, 2));

  CompilationDiagnosticCollector missing;
  ZC_EXPECT(!missing.addRoot(facts.asPtr(), entries.asPtr()));
  auto missingResult = zc::mv(missing).seal();
  ZC_REQUIRE(missingResult.is<DiagnosticCollectionFailure>());
  ZC_EXPECT(missingResult.get<DiagnosticCollectionFailure>() ==
            DiagnosticCollectionFailure::MissingAuthority);

  CompilationDiagnosticCollector conflicting;
  ZC_REQUIRE(conflicting.addSourceAuthority(source, 8));
  ZC_EXPECT(!conflicting.addSourceAuthority(source, 9));
  auto conflictingResult = zc::mv(conflicting).seal();
  ZC_REQUIRE(conflictingResult.is<DiagnosticCollectionFailure>());
  ZC_EXPECT(conflictingResult.get<DiagnosticCollectionFailure>() ==
            DiagnosticCollectionFailure::ConflictingAuthority);
}

ZC_TEST("CompilationDiagnosticCollector seals document facts with exact authority") {
  auto identity = documentIdentity(1);
  zc::Vector<DiagnosticFact> facts;
  facts.add(documentFact());
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(documentProvenance(1, 3));

  CompilationDiagnosticCollector collector;
  ZC_REQUIRE(collector.addDocumentAuthority(identity.asPtr(), 8));
  ZC_REQUIRE(collector.addRoot(facts.asPtr(), entries.asPtr()));
  auto result = zc::mv(collector).seal();
  ZC_REQUIRE(result.is<CompilationDiagnosticFacts>());
  const auto& sealed = result.get<CompilationDiagnosticFacts>();
  ZC_REQUIRE(sealed.documents().size() == 1);
  ZC_EXPECT(sealed.documents()[0].identity.asPtr() == identity.asPtr());
  ZC_EXPECT(sealed.documents()[0].byteLength == 8);
  ZC_EXPECT(sealed.documents()[0].provenance.size() == 1);
}

ZC_TEST(
    "CompilationDiagnosticCollector rejects missing conflicting and out-of-range document "
    "authority") {
  auto identity = documentIdentity(1);
  zc::Vector<DiagnosticFact> facts;
  facts.add(documentFact());
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(documentProvenance(1, 3));

  CompilationDiagnosticCollector missing;
  ZC_EXPECT(!missing.addRoot(facts.asPtr(), entries.asPtr()));
  auto missingResult = zc::mv(missing).seal();
  ZC_REQUIRE(missingResult.is<DiagnosticCollectionFailure>());
  ZC_EXPECT(missingResult.get<DiagnosticCollectionFailure>() ==
            DiagnosticCollectionFailure::MissingAuthority);

  CompilationDiagnosticCollector conflicting;
  ZC_REQUIRE(conflicting.addDocumentAuthority(identity.asPtr(), 8));
  ZC_EXPECT(!conflicting.addDocumentAuthority(identity.asPtr(), 9));
  auto conflictingResult = zc::mv(conflicting).seal();
  ZC_REQUIRE(conflictingResult.is<DiagnosticCollectionFailure>());
  ZC_EXPECT(conflictingResult.get<DiagnosticCollectionFailure>() ==
            DiagnosticCollectionFailure::ConflictingAuthority);

  CompilationDiagnosticCollector outOfRange;
  ZC_REQUIRE(outOfRange.addDocumentAuthority(identity.asPtr(), 2));
  ZC_EXPECT(!outOfRange.addRoot(facts.asPtr(), entries.asPtr()));
  auto outOfRangeResult = zc::mv(outOfRange).seal();
  ZC_REQUIRE(outOfRangeResult.is<DiagnosticCollectionFailure>());
  ZC_EXPECT(outOfRangeResult.get<DiagnosticCollectionFailure>() ==
            DiagnosticCollectionFailure::MissingAuthority);
}

}  // namespace zomlang::compiler::diagnostics
