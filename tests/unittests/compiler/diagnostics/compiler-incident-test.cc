// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/basic/incident/compiler-incident.h"

#include "compiler/diagnostics/incident/compiler-incident.h"
#include "compiler/identity/diagnostics/identity-diagnostic-projector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler {
namespace {

basic::CompilerIncidentDescriptor identityIncident(identity::IdentityInvariantKind kind,
                                                   identity::IdentityAllocationPhase phase,
                                                   identity::IdentityApiSite producer) {
  zc::Maybe<zc::Array<uint8_t>> noStructuralInput;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = ZC_REQUIRE_NONNULL(identity::IdentityInvariant::from(
      kind, phase, zc::mv(noStructuralInput), zc::mv(noRange), producer, 0));
  return identity::IdentityDiagnosticProjector::project(invariant);
}

}  // namespace

ZC_TEST("BoundedIncidentSet orders descriptors independently of insertion order") {
  const auto invalidHandle = identityIncident(identity::IdentityInvariantKind::InvalidHandle,
                                              identity::IdentityAllocationPhase::Registry,
                                              identity::IdentityApiSite::HandleLookup);
  const auto invalidRange = identityIncident(identity::IdentityInvariantKind::InvalidSourceRange,
                                             identity::IdentityAllocationPhase::Source,
                                             identity::IdentityApiSite::SourceFreeze);
  const auto invalidEncoding = identityIncident(
      identity::IdentityInvariantKind::NonCanonicalEncoding,
      identity::IdentityAllocationPhase::Encoding, identity::IdentityApiSite::CanonicalEncode);
  basic::BoundedIncidentSet first;
  ZC_EXPECT(first.add(invalidRange));
  ZC_EXPECT(first.add(invalidHandle));
  ZC_EXPECT(first.add(invalidEncoding));

  basic::BoundedIncidentSet second;
  ZC_EXPECT(second.add(invalidEncoding));
  ZC_EXPECT(second.add(invalidHandle));
  ZC_EXPECT(second.add(invalidRange));

  ZC_EXPECT(first.descriptors() == second.descriptors());
  ZC_REQUIRE(first.size() == 3);
  ZC_EXPECT(first.descriptors()[0].domain() == basic::CompilerIncidentDomain::Identity);
  ZC_EXPECT(first.descriptors()[0].phase().tag() ==
            static_cast<uint32_t>(identity::IdentityAllocationPhase::Registry));
}

ZC_TEST("BoundedIncidentSet merges equal shapes with a saturating count") {
  const auto descriptor = identityIncident(identity::IdentityInvariantKind::InvalidHandle,
                                           identity::IdentityAllocationPhase::Registry,
                                           identity::IdentityApiSite::HandleLookup);
  basic::BoundedIncidentSet incidents;
  ZC_EXPECT(incidents.add(descriptor.repeated(UINT64_MAX)));
  ZC_EXPECT(incidents.add(descriptor));

  ZC_REQUIRE(incidents.size() == 1);
  ZC_EXPECT(incidents.descriptors()[0].occurrences() == UINT64_MAX);
}

ZC_TEST("BoundedIncidentSet merges sets deterministically") {
  const auto invalidHandle = identityIncident(identity::IdentityInvariantKind::InvalidHandle,
                                              identity::IdentityAllocationPhase::Registry,
                                              identity::IdentityApiSite::HandleLookup);
  const auto invalidRange = identityIncident(identity::IdentityInvariantKind::InvalidSourceRange,
                                             identity::IdentityAllocationPhase::Source,
                                             identity::IdentityApiSite::SourceFreeze);
  basic::BoundedIncidentSet left;
  ZC_EXPECT(left.add(invalidRange));
  basic::BoundedIncidentSet right;
  ZC_EXPECT(right.add(invalidHandle));
  ZC_EXPECT(right.add(invalidRange));

  ZC_EXPECT(left.merge(right));
  ZC_REQUIRE(left.size() == 2);
  ZC_EXPECT(left.descriptors()[0] == invalidHandle);
  ZC_EXPECT(left.descriptors()[1].occurrences() == 2);
}

ZC_TEST("BoundedIncidentSet rejects an empty occurrence record") {
  const auto descriptor = identityIncident(identity::IdentityInvariantKind::InvalidHandle,
                                           identity::IdentityAllocationPhase::Registry,
                                           identity::IdentityApiSite::HandleLookup);
  basic::BoundedIncidentSet incidents;
  ZC_EXPECT(!incidents.add(descriptor.repeated(0)));
  ZC_EXPECT(incidents.empty());
}

ZC_TEST("IdentityDiagnosticProjector ignores user-controlled invariant evidence") {
  zc::Maybe<zc::Array<uint8_t>> firstStructural = zc::heapArray<uint8_t>({0x01});
  zc::Maybe<zc::Array<uint8_t>> secondStructural = zc::heapArray<uint8_t>({0xff});
  zc::Maybe<identity::UnbrandedSourceRange> firstRange;
  zc::Maybe<identity::UnbrandedSourceRange> secondRange;
  auto first = ZC_REQUIRE_NONNULL(identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Registry,
      zc::mv(firstStructural), zc::mv(firstRange), identity::IdentityApiSite::HandleLookup, 1));
  auto second = ZC_REQUIRE_NONNULL(identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, identity::IdentityAllocationPhase::Registry,
      zc::mv(secondStructural), zc::mv(secondRange), identity::IdentityApiSite::HandleLookup, 999));

  ZC_EXPECT(identity::IdentityDiagnosticProjector::project(first) ==
            identity::IdentityDiagnosticProjector::project(second));
}

ZC_TEST("Compiler incident rendering is stable and omits occurrence counts") {
  const auto descriptor = identityIncident(identity::IdentityInvariantKind::InvalidHandle,
                                           identity::IdentityAllocationPhase::Registry,
                                           identity::IdentityApiSite::HandleLookup);
  basic::BoundedIncidentSet one;
  basic::BoundedIncidentSet many;
  ZC_EXPECT(one.add(descriptor));
  ZC_EXPECT(many.add(descriptor.repeated(42)));

  auto first =
      ZC_REQUIRE_NONNULL(diagnostics::renderCompilerIncident("ZomLang Version test"_zc, one));
  auto second =
      ZC_REQUIRE_NONNULL(diagnostics::renderCompilerIncident("ZomLang Version test"_zc, many));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first.startsWith("error: internal compiler error\n"_zc));
  ZC_EXPECT(first.contains("note: phase: identity\n"_zc));
  ZC_EXPECT(first.contains("ZOM9910"_zc) == false);
  const auto marker = first.find("note: incident: "_zc);
  ZC_REQUIRE(marker != zc::none);
  ZC_IF_SOME(offset, marker) {
    const auto fingerprint = first.slice(offset + 16, offset + 48);
    ZC_EXPECT(fingerprint.size() == 32);
    for (const char value : fingerprint) {
      ZC_EXPECT((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'));
    }
  }
}

ZC_TEST("Compiler incident rendering rejects an empty set") {
  basic::BoundedIncidentSet incidents;
  ZC_EXPECT(diagnostics::renderCompilerIncident("ZomLang Version test"_zc, incidents) == zc::none);
}

}  // namespace zomlang::compiler
