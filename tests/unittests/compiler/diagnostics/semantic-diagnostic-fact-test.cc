// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"

#include "compiler/identity/source/source-snapshot.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::diagnostics {
namespace {

identity::ImmutableSourceSnapshot snapshot() {
  return ZC_REQUIRE_NONNULL(identity::ImmutableSourceSnapshot::from(
      tests::test_identity_detail::source(), zc::heapArray<uint8_t>(16, uint8_t{0})));
}

zc::Array<uint8_t> owner() {
  const uint8_t bytes[] = {0x12, 0x34};
  return zc::heapArray<uint8_t>(zc::arrayPtr(bytes));
}

zc::Vector<uint32_t> eventPath() {
  zc::Vector<uint32_t> result;
  result.add(7);
  result.add(2);
  return result;
}

}  // namespace

ZC_TEST("SemanticDiagnosticFact projects stable source-qualified facts") {
  auto source = snapshot();
  auto primary = ZC_REQUIRE_NONNULL(source.span(2, 5));
  auto noteSpan = ZC_REQUIRE_NONNULL(source.span(8, 11));
  zc::Vector<zc::String> arguments;
  arguments.add(zc::str("i32"));
  zc::Vector<SemanticDiagnosticRelatedSite> related;
  zc::Vector<zc::String> noteArguments;
  related.add(SemanticDiagnosticRelatedSite{DiagnosticSecondaryRole::Note, DiagID::ValueMovedHere,
                                            zc::mv(noteSpan), zc::mv(noteArguments)});

  auto projected = projectSemanticDiagnosticFact(tests::test_identity_detail::module(),
                                                 SemanticDiagnosticDomain::Checker, owner().asPtr(),
                                                 eventPath().asPtr(), DiagID::CannotCallNonFunction,
                                                 zc::mv(arguments), primary, zc::mv(related));

  ZC_REQUIRE(projected.is<SemanticDiagnosticFactProjection>());
  const auto& value = projected.get<SemanticDiagnosticFactProjection>();
  ZC_EXPECT(value.fact.code() == DiagID::CannotCallNonFunction);
  ZC_EXPECT(value.fact.occurrence().isSemantic());
  ZC_EXPECT(value.fact.occurrence().semanticDomain() == SemanticDiagnosticDomain::Checker);
  ZC_EXPECT(value.fact.occurrence().semanticOwnerBytes() == owner().asPtr());
  ZC_EXPECT(value.fact.occurrence().semanticEventPath() == eventPath().asPtr());
  ZC_EXPECT(value.fact.primary().semanticRole() == SemanticDiagnosticSiteRole::Primary);
  ZC_REQUIRE(value.fact.secondary().size() == 1);
  ZC_EXPECT(value.fact.secondary()[0].provenance().semanticRole() ==
            SemanticDiagnosticSiteRole::Note);
  ZC_REQUIRE(value.provenance.size() == 2);
  const DiagnosticSourceRange expectedPrimary{2, 2, false};
  const DiagnosticSourceRange expectedNote{8, 8, false};
  ZC_EXPECT(value.provenance[0].range == expectedPrimary);
  ZC_EXPECT(value.provenance[1].range == expectedNote);
}

ZC_TEST("SemanticDiagnosticFact rejects incomplete owners") {
  auto source = snapshot();
  auto primary = ZC_REQUIRE_NONNULL(source.span(0, 1));
  zc::Vector<zc::String> arguments;
  zc::Vector<SemanticDiagnosticRelatedSite> related;
  const zc::ArrayPtr<const uint8_t> emptyOwner;

  auto invalidOwner = projectSemanticDiagnosticFact(
      tests::test_identity_detail::module(), SemanticDiagnosticDomain::Checker, emptyOwner,
      eventPath().asPtr(), DiagID::CannotInferNullInitializer, zc::mv(arguments), primary,
      zc::mv(related));
  ZC_REQUIRE(invalidOwner.is<SemanticDiagnosticProjectionFailure>());
  ZC_EXPECT(invalidOwner.get<SemanticDiagnosticProjectionFailure>() ==
            SemanticDiagnosticProjectionFailure::InvalidOwner);
}

ZC_TEST("SemanticDiagnosticFact codec preserves semantic topology") {
  auto source = snapshot();
  auto primary = ZC_REQUIRE_NONNULL(source.span(1, 3));
  zc::Vector<zc::String> arguments;
  zc::Vector<SemanticDiagnosticRelatedSite> related;
  auto projected = projectSemanticDiagnosticFact(
      tests::test_identity_detail::module(), SemanticDiagnosticDomain::BorrowInterface,
      owner().asPtr(), eventPath().asPtr(), DiagID::BorrowOutputRegionAmbiguous, zc::mv(arguments),
      primary, zc::mv(related));
  ZC_REQUIRE(projected.is<SemanticDiagnosticFactProjection>());
  zc::Vector<DiagnosticFact> facts;
  facts.add(zc::mv(projected.get<SemanticDiagnosticFactProjection>().fact));
  constexpr DiagnosticFactCodecLimits limits{1, 4096, 8, 4096, 8};
  auto encoded = ZC_REQUIRE_NONNULL(encodeDiagnosticFacts(zc::none, facts.asPtr(), limits));
  auto decoded = ZC_REQUIRE_NONNULL(decodeDiagnosticFacts(zc::none, encoded.asPtr(), limits));
  ZC_REQUIRE(decoded.size() == 1);
  ZC_EXPECT(decoded[0] == facts[0]);
}

}  // namespace zomlang::compiler::diagnostics
