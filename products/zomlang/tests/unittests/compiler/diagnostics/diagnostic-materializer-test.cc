// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-materializer.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

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
  auto occurrence = ZC_REQUIRE_NONNULL(DiagnosticOccurrenceKey::from(
      tests::test_identity_detail::source(), SourceDiagnosticPhase::Lex,
      SourceDiagnosticEmitter::Lexer, 0));
  zc::Vector<zc::String> arguments;
  zc::Vector<DiagnosticSecondary> secondary;
  secondary.add(ZC_REQUIRE_NONNULL(
      DiagnosticSecondary::highlight(key(1, uint32_t{0}))));
  zc::Vector<zc::String> noteArguments;
  noteArguments.add(zc::str("identifier"_zc));
  secondary.add(ZC_REQUIRE_NONNULL(DiagnosticSecondary::note(
      DiagID::ExpectedToken, key(2, uint32_t{0}), zc::mv(noteArguments))));
  return ZC_REQUIRE_NONNULL(DiagnosticFact::from(
      zc::mv(occurrence), DiagID::InvalidCharacter, zc::mv(arguments), key(0),
      zc::mv(secondary)));
}

SourceDiagnosticProvenanceMap map() {
  zc::Vector<SourceDiagnosticProvenanceEntry> entries;
  entries.add(SourceDiagnosticProvenanceEntry{
      key(0), DiagnosticSourceRange{1, 1, false}});
  entries.add(SourceDiagnosticProvenanceEntry{
      key(1, uint32_t{0}), DiagnosticSourceRange{1, 3, true}});
  entries.add(SourceDiagnosticProvenanceEntry{
      key(2, uint32_t{0}), DiagnosticSourceRange{4, 4, false}});
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

class CaptureConsumer final : public DiagnosticConsumer {
public:
  void handleDiagnostic(const source::SourceManager&, const Diagnostic& diagnostic) override {
    ids.add(diagnostic.getId());
    rangeCounts.add(diagnostic.getRanges().size());
    childCounts.add(diagnostic.getChildDiagnostics().size());
  }
  zc::Vector<DiagID> ids;
  zc::Vector<size_t> rangeCounts;
  zc::Vector<size_t> childCounts;
};

}  // namespace

ZC_TEST("DiagnosticMaterializer resolves the complete batch before publication") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("abcdef"_zc.asBytes(), "test.zom"_zc);
  auto provenance = map();
  auto sourceKey = tests::test_identity_detail::source();
  SourceDiagnosticProvenanceResolver resolver(sourceKey, provenance);
  zc::Vector<DiagnosticFact> facts;
  facts.add(populatedFact());
  auto result = materializeDiagnosticFacts(facts.asPtr(), resolver, sources, buffer);
  ZC_REQUIRE(result.is<ResolvedDiagnosticBatch>());
  ZC_EXPECT(result.get<ResolvedDiagnosticBatch>().size() == 1);

  DiagnosticEngine engine(sources);
  auto capture = zc::heap<CaptureConsumer>();
  auto& observed = *capture;
  engine.addConsumer(zc::mv(capture));
  publishResolvedDiagnosticBatch(zc::mv(result.get<ResolvedDiagnosticBatch>()), engine);
  ZC_REQUIRE(observed.ids.size() == 1);
  ZC_EXPECT(observed.ids[0] == DiagID::InvalidCharacter);
  ZC_EXPECT(observed.rangeCounts[0] == 1);
  ZC_EXPECT(observed.childCounts[0] == 1);
}

ZC_TEST("DiagnosticMaterializer reports missing and foreign provenance without emission") {
  source::SourceManager sources;
  const auto buffer = sources.addMemBufferCopy("abcdef"_zc.asBytes(), "test.zom"_zc);
  zc::Vector<SourceDiagnosticProvenanceEntry> onlyPrimary;
  onlyPrimary.add(SourceDiagnosticProvenanceEntry{
      key(0), DiagnosticSourceRange{1, 1, false}});
  auto incomplete =
      ZC_REQUIRE_NONNULL(SourceDiagnosticProvenanceMap::from(zc::mv(onlyPrimary), 6));
  auto sourceKey = tests::test_identity_detail::source();
  SourceDiagnosticProvenanceResolver resolver(sourceKey, incomplete);
  zc::Vector<DiagnosticFact> facts;
  facts.add(populatedFact());
  auto missing = materializeDiagnosticFacts(facts.asPtr(), resolver, sources, buffer);
  ZC_REQUIRE(missing.is<DiagnosticMaterializationFailure>());
  ZC_EXPECT(missing.get<DiagnosticMaterializationFailure>() ==
            DiagnosticMaterializationFailure::MissingProvenance);

  auto provenance = map();
  auto foreign = foreignSource();
  SourceDiagnosticProvenanceResolver foreignResolver(foreign, provenance);
  auto rejected = materializeDiagnosticFacts(facts.asPtr(), foreignResolver, sources, buffer);
  ZC_REQUIRE(rejected.is<DiagnosticMaterializationFailure>());
  ZC_EXPECT(rejected.get<DiagnosticMaterializationFailure>() ==
            DiagnosticMaterializationFailure::ForeignSource);
}

}  // namespace zomlang::compiler::diagnostics
