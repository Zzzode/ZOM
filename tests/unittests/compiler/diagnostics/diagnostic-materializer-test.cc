// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/diagnostic-materializer.h"

#include "compiler/binder/stable/stable-binding-diagnostic-fact.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::diagnostics {
namespace {

zc::Vector<uint32_t> path(uint32_t slot, zc::Maybe<uint32_t> ordinal = zc::none) {
  zc::Vector<uint32_t> result;
  result.add(0);
  result.add(slot);
  ZC_IF_SOME(value, ordinal) { result.add(value); }
  return result;
}

DiagnosticProvenanceKey key(uint32_t slot, zc::Maybe<uint32_t> ordinal = zc::none) {
  return ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, path(slot, ordinal)));
}

DiagnosticFact populatedFact() {
  auto occurrence = ZC_REQUIRE_NONNULL(
      DiagnosticOccurrenceKey::from(tests::test_identity_detail::source(),
                                    SourceDiagnosticPhase::Lex, SourceDiagnosticEmitter::Lexer, 0));
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  secondary.add(ZC_REQUIRE_NONNULL(DiagnosticSecondary::highlight(key(1, uint32_t{0}))));
  zc::Vector<zc::String> noteArguments;
  noteArguments.add(zc::str("identifier"_zc));
  secondary.add(ZC_REQUIRE_NONNULL(DiagnosticSecondary::note(
      DiagID::ExpectedToken, key(2, uint32_t{0}), zc::mv(noteArguments))));
  return ZC_REQUIRE_NONNULL(DiagnosticFact::from(zc::mv(occurrence), DiagID::InvalidCharacter,
                                                 zc::mv(arguments), key(0), zc::mv(secondary)));
}

SourceDiagnosticProvenanceMap map() {
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(SourceDiagnosticProvenanceEntry{key(0), DiagnosticSourceRange{1, 1, false}});
  entries.add(
      SourceDiagnosticProvenanceEntry{key(1, uint32_t{0}), DiagnosticSourceRange{1, 3, true}});
  entries.add(
      SourceDiagnosticProvenanceEntry{key(2, uint32_t{0}), DiagnosticSourceRange{4, 4, false}});
  return ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(entries), 6));
}

identity::SourceFileKey foreignSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(
      tests::test_identity_detail::scalar<identity::CanonicalPathSegment>("foreign.zom"_zc));
  return identity::SourceFileKey::from(
      tests::test_identity_detail::coreCrate(),
      identity::SourceOriginKey::localFile(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

identity::SourceFileKey siblingSource(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(tests::test_identity_detail::scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      tests::test_identity_detail::crate(),
      identity::SourceOriginKey::localFile(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))));
}

binder::IdentitySyntaxSiteKey identitySite(const identity::SourceFileKey& source,
                                           uint32_t component) {
  zc::Vector<uint32_t> syntaxPath;
  syntaxPath.add(component);
  return ZC_REQUIRE_NONNULL(binder::IdentitySyntaxSiteKey::from(
      tests::test_identity_detail::module(), source.clone(), zc::mv(syntaxPath)));
}

zc::Array<uint8_t> documentIdentity(uint8_t discriminator) {
  const uint8_t bytes[] = {0x44, 0x4f, 0x43, discriminator};
  return zc::heapArray<uint8_t>(bytes);
}

zc::Array<uint8_t> documentOccurrence() {
  const uint8_t bytes[] = {0x4f, 0x43, 0x43, 0x01};
  return zc::heapArray<uint8_t>(bytes);
}

DiagnosticFact documentFact() {
  auto primaryDocument = documentIdentity(1);
  auto occurrenceBytes = documentOccurrence();
  auto occurrence = ZC_REQUIRE_NONNULL(
      DiagnosticOccurrenceKey::document(zc::heapArray<uint8_t>(primaryDocument.asPtr()),
                                        zc::heapArray<uint8_t>(occurrenceBytes.asPtr())));
  auto primary = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::documentSite(
      zc::heapArray<uint8_t>(primaryDocument.asPtr()),
      zc::heapArray<uint8_t>(occurrenceBytes.asPtr()), DocumentDiagnosticSiteRole::Primary, 0));
  auto highlight = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::documentSite(
      zc::heapArray<uint8_t>(primaryDocument.asPtr()),
      zc::heapArray<uint8_t>(occurrenceBytes.asPtr()), DocumentDiagnosticSiteRole::Highlight, 0));
  auto note = ZC_REQUIRE_NONNULL(DiagnosticProvenanceKey::documentSite(
      documentIdentity(2), zc::mv(occurrenceBytes), DocumentDiagnosticSiteRole::Note, 0));
  zc::Vector<zc::String> arguments;
  arguments.add(zc::str("toml-syntax"_zc));
  zc::Vector<DiagnosticSecondary> secondary;
  secondary.add(ZC_REQUIRE_NONNULL(DiagnosticSecondary::highlight(zc::mv(highlight))));
  secondary.add(ZC_REQUIRE_NONNULL(
      DiagnosticSecondary::note(DiagID::PreviousWorkspacePackageHere, zc::mv(note), {})));
  return ZC_REQUIRE_NONNULL(DiagnosticFact::from(zc::mv(occurrence), DiagID::PackageManifestInvalid,
                                                 zc::mv(arguments), zc::mv(primary),
                                                 zc::mv(secondary)));
}

CompilationDiagnosticFacts documentFacts() {
  auto fact = documentFact();
  zc::Vector<DiagnosticFact> facts;
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(
      SourceDiagnosticProvenanceEntry{fact.primary().clone(), DiagnosticSourceRange{2, 2, false}});
  entries.add(SourceDiagnosticProvenanceEntry{fact.secondary()[0].provenance().clone(),
                                              DiagnosticSourceRange{2, 6, true}});
  entries.add(SourceDiagnosticProvenanceEntry{fact.secondary()[1].provenance().clone(),
                                              DiagnosticSourceRange{9, 9, false}});
  facts.add(zc::mv(fact));
  CompilationDiagnosticCollector collector;
  auto primaryDocument = documentIdentity(1);
  ZC_REQUIRE(collector.addDocumentAuthority(primaryDocument.asPtr(), 16));
  auto relatedDocument = documentIdentity(2);
  ZC_REQUIRE(collector.addDocumentAuthority(relatedDocument.asPtr(), 16));
  ZC_REQUIRE(collector.addRoot(facts.asPtr(), entries.asPtr()));
  auto sealed = zc::mv(collector).seal();
  ZC_REQUIRE(sealed.is<CompilationDiagnosticFacts>());
  return zc::mv(sealed).get<CompilationDiagnosticFacts>();
}

}  // namespace

ZC_TEST("DiagnosticMaterializer resolves the complete batch before publication") {
  auto provenance = map();
  auto sourceKey = tests::test_identity_detail::source();
  SourceDiagnosticProvenanceResolver resolver(sourceKey, provenance);
  zc::Vector<DiagnosticFact> facts;
  facts.add(populatedFact());
  auto result = materializeDiagnosticFacts(facts.asPtr(), resolver);
  ZC_REQUIRE(result.is<ResolvedDiagnosticBatch>());
  const auto resolved = result.get<ResolvedDiagnosticBatch>().diagnostics();
  ZC_REQUIRE(resolved.size() == 1);
  ZC_EXPECT(resolved[0].code() == DiagID::InvalidCharacter);
  ZC_EXPECT(resolved[0].related().size() == 2);
}

ZC_TEST("DiagnosticMaterializer reports missing and foreign provenance without emission") {
  zc::Vector<SourceDiagnosticProvenanceEntry> onlyPrimary;
  onlyPrimary.add(SourceDiagnosticProvenanceEntry{key(0), DiagnosticSourceRange{1, 1, false}});
  auto incomplete = ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(onlyPrimary), 6));
  auto sourceKey = tests::test_identity_detail::source();
  SourceDiagnosticProvenanceResolver resolver(sourceKey, incomplete);
  zc::Vector<DiagnosticFact> facts;
  facts.add(populatedFact());
  auto missing = materializeDiagnosticFacts(facts.asPtr(), resolver);
  ZC_REQUIRE(missing.is<DiagnosticMaterializationFailure>());
  ZC_EXPECT(missing.get<DiagnosticMaterializationFailure>() ==
            DiagnosticMaterializationFailure::MissingProvenance);

  auto provenance = map();
  auto foreign = foreignSource();
  SourceDiagnosticProvenanceResolver foreignResolver(foreign, provenance);
  auto rejected = materializeDiagnosticFacts(facts.asPtr(), foreignResolver);
  ZC_REQUIRE(rejected.is<DiagnosticMaterializationFailure>());
  ZC_EXPECT(rejected.get<DiagnosticMaterializationFailure>() ==
            DiagnosticMaterializationFailure::ForeignSource);
}

ZC_TEST("DiagnosticMaterializer preserves source identity across related locations") {
  auto primarySource = tests::test_identity_detail::source();
  auto relatedSource = siblingSource("related.zom"_zc);
  auto fact =
      ZC_REQUIRE_NONNULL(binder::StableBindingDiagnosticFactFactory::definitionRedeclaration(
          identitySite(primarySource, 2), identitySite(relatedSource, 1), DiagID::RedeclareClass,
          "Thing"_zc));

  zc::Vector<SourceDiagnosticProvenanceEntry> primaryEntries;
  primaryEntries.add(
      SourceDiagnosticProvenanceEntry{fact.primary().clone(), DiagnosticSourceRange{3, 3, false}});
  auto primaryMap =
      ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(primaryEntries), 8));
  zc::Vector<SourceDiagnosticProvenanceEntry> relatedEntries;
  relatedEntries.add(SourceDiagnosticProvenanceEntry{fact.secondary()[0].provenance().clone(),
                                                     DiagnosticSourceRange{12, 12, false}});
  auto relatedMap =
      ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(relatedEntries), 16));
  zc::Vector<DiagnosticSourceProvenance> sources;
  sources.add(DiagnosticSourceProvenance{primarySource.clone(), zc::mv(primaryMap)});
  sources.add(DiagnosticSourceProvenance{relatedSource.clone(), zc::mv(relatedMap)});
  auto resolver =
      ZC_REQUIRE_NONNULL(CompilationDiagnosticProvenanceResolver::from(zc::mv(sources)));
  zc::Vector<DiagnosticFact> facts;
  facts.add(zc::mv(fact));

  auto materialized = materializeDiagnosticFacts(facts.asPtr(), resolver);
  ZC_REQUIRE(materialized.is<ResolvedDiagnosticBatch>());
  const auto resolved = materialized.get<ResolvedDiagnosticBatch>().diagnostics();
  ZC_REQUIRE(resolved.size() == 1);
  ZC_EXPECT(resolved[0].primary().source().sameAs(primarySource));
  const DiagnosticSourceRange expectedPrimary{3, 3, false};
  ZC_EXPECT(resolved[0].primary().range() == expectedPrimary);
  ZC_REQUIRE(resolved[0].related().size() == 1);
  ZC_EXPECT(resolved[0].related()[0].location.source().sameAs(relatedSource));
  const DiagnosticSourceRange expectedRelated{12, 12, false};
  ZC_EXPECT(resolved[0].related()[0].location.range() == expectedRelated);
}

ZC_TEST("CompilationDiagnosticProvenanceResolver rejects duplicate source authorities") {
  auto source = tests::test_identity_detail::source();
  zc::Vector<SourceDiagnosticProvenanceEntry> firstEntries;
  firstEntries.add(SourceDiagnosticProvenanceEntry{key(0), DiagnosticSourceRange{1, 1, false}});
  auto first = ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(firstEntries), 6));
  zc::Vector<SourceDiagnosticProvenanceEntry> secondEntries;
  secondEntries.add(SourceDiagnosticProvenanceEntry{key(0), DiagnosticSourceRange{1, 1, false}});
  auto second = ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(secondEntries), 6));
  zc::Vector<DiagnosticSourceProvenance> sources;
  sources.add(DiagnosticSourceProvenance{source.clone(), zc::mv(first)});
  sources.add(DiagnosticSourceProvenance{source.clone(), zc::mv(second)});

  ZC_EXPECT(CompilationDiagnosticProvenanceResolver::from(zc::mv(sources)) == zc::none);
}

ZC_TEST("DiagnosticMaterializer preserves document identity across related locations") {
  auto sealed = documentFacts();
  auto resolver = ZC_REQUIRE_NONNULL(CompilationDiagnosticProvenanceResolver::from(sealed));
  auto materialized = materializeDiagnosticFacts(sealed, resolver);
  ZC_REQUIRE(materialized.is<ResolvedDiagnosticBatch>());
  const auto diagnostics = materialized.get<ResolvedDiagnosticBatch>().diagnostics();
  ZC_REQUIRE(diagnostics.size() == 1);
  ZC_EXPECT(diagnostics[0].primary().origin() == DiagnosticFactOrigin::Document);
  ZC_EXPECT(diagnostics[0].primary().documentIdentityBytes() == documentIdentity(1).asPtr());
  ZC_REQUIRE(diagnostics[0].related().size() == 2);
  ZC_EXPECT(diagnostics[0].related()[0].location.documentIdentityBytes() ==
            documentIdentity(1).asPtr());
  ZC_EXPECT(diagnostics[0].related()[1].location.documentIdentityBytes() ==
            documentIdentity(2).asPtr());
}

ZC_TEST("DiagnosticMaterializer rejects missing document provenance without publication") {
  auto fact = documentFact();
  zc::Vector<DiagnosticFact> facts;
  facts.add(zc::mv(fact));
  CompilationDiagnosticCollector collector;
  auto primaryDocument = documentIdentity(1);
  auto relatedDocument = documentIdentity(2);
  ZC_REQUIRE(collector.addDocumentAuthority(primaryDocument.asPtr(), 16));
  ZC_REQUIRE(collector.addDocumentAuthority(relatedDocument.asPtr(), 16));
  auto sealed = zc::mv(collector).seal();
  ZC_REQUIRE(sealed.is<CompilationDiagnosticFacts>());
  auto resolver = ZC_REQUIRE_NONNULL(
      CompilationDiagnosticProvenanceResolver::from(sealed.get<CompilationDiagnosticFacts>()));
  auto materialized = materializeDiagnosticFacts(facts.asPtr(), resolver);
  ZC_REQUIRE(materialized.is<DiagnosticMaterializationFailure>());
  ZC_EXPECT(materialized.get<DiagnosticMaterializationFailure>() ==
            DiagnosticMaterializationFailure::MissingProvenance);
}

}  // namespace zomlang::compiler::diagnostics
