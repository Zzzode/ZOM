// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/diagnostics/checker-diagnostic-projector.h"

#include "compiler/identity/diagnostics/identity-diagnostic-projector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::checker {
namespace {

signature::CheckerInvariantFact checkerFact(signature::CheckerInvariantKind kind,
                                            signature::CheckerInvariantStage stage) {
  return signature::CheckerInvariantFact{kind,     stage,    identity::ModuleId(),   zc::none,
                                         zc::none, zc::none, zc::Vector<uint32_t>(), zc::none,
                                         zc::none, 0};
}

dispatch::DispatchInvariantFact dispatchFact(dispatch::DispatchInvariantKind kind,
                                             dispatch::DispatchInvariantStage stage) {
  return dispatch::DispatchInvariantFact{kind,     stage,    identity::ModuleId(),   zc::none,
                                         zc::none, zc::none, zc::Vector<uint32_t>(), zc::none,
                                         zc::none, 0};
}

}  // namespace

ZC_TEST("CheckerDiagnosticProjector preserves checker stage and kind") {
  const auto descriptor = CheckerDiagnosticProjector::project(
      checkerFact(signature::CheckerInvariantKind::MissingRequiredFact,
                  signature::CheckerInvariantStage::Verification));
  ZC_EXPECT(descriptor.domain() == basic::CompilerIncidentDomain::Checker);
  ZC_EXPECT(descriptor.phase().tag() ==
            static_cast<uint32_t>(signature::CheckerInvariantStage::Verification));
  ZC_EXPECT(descriptor.kind().tag() ==
            static_cast<uint32_t>(signature::CheckerInvariantKind::MissingRequiredFact));
}

ZC_TEST("CheckerDiagnosticProjector keeps dispatch in a disjoint phase family") {
  const auto descriptor = CheckerDiagnosticProjector::project(
      dispatchFact(dispatch::DispatchInvariantKind::InvalidFact,
                   dispatch::DispatchInvariantStage::Construction));
  ZC_EXPECT(descriptor.domain() == basic::CompilerIncidentDomain::Checker);
  ZC_EXPECT(descriptor.phase().tag() ==
            0x100U + static_cast<uint32_t>(dispatch::DispatchInvariantStage::Construction));
  ZC_EXPECT(descriptor.kind().tag() ==
            static_cast<uint32_t>(dispatch::DispatchInvariantKind::InvalidFact));
}

ZC_TEST("CheckerDiagnosticProjector aggregates mixed invariant families") {
  zc::Vector<signature::CheckerVerificationFailure> failures;
  failures.add(signature::CheckerVerificationFailure(checkerFact(
      signature::CheckerInvariantKind::InvalidFact, signature::CheckerInvariantStage::Body)));
  zc::Maybe<zc::Array<uint8_t>> noStructuralInput;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  failures.add(
      signature::CheckerVerificationFailure(ZC_REQUIRE_NONNULL(identity::IdentityInvariant::from(
          identity::IdentityInvariantKind::ForeignContext,
          identity::IdentityAllocationPhase::Context, zc::mv(noStructuralInput), zc::mv(noRange),
          identity::IdentityApiSite::HandleLookup, 0))));

  auto incidents = ZC_REQUIRE_NONNULL(CheckerDiagnosticProjector::projectAll(failures.asPtr()));
  ZC_REQUIRE(incidents.size() == 2);
  ZC_EXPECT(incidents.descriptors()[0].domain() == basic::CompilerIncidentDomain::Identity);
  ZC_EXPECT(incidents.descriptors()[1].domain() == basic::CompilerIncidentDomain::Checker);
}

ZC_TEST("CheckerDiagnosticProjector rejects an empty failure set") {
  zc::ArrayPtr<const signature::CheckerVerificationFailure> failures;
  ZC_EXPECT(CheckerDiagnosticProjector::projectAll(failures) == zc::none);
}

}  // namespace zomlang::compiler::checker
