// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/checker/diagnostics/checker-diagnostic-projector.h"

#include "compiler/identity/diagnostics/identity-diagnostic-projector.h"

namespace zomlang::compiler::checker {
namespace {

uint32_t checkerPhaseTag(signature::CheckerInvariantStage stage) noexcept {
  switch (stage) {
    case signature::CheckerInvariantStage::Signature:
    case signature::CheckerInvariantStage::Coherence:
    case signature::CheckerInvariantStage::Body:
    case signature::CheckerInvariantStage::Verification:
      return static_cast<uint32_t>(stage);
  }
  ZC_UNREACHABLE
}

uint32_t checkerKindTag(signature::CheckerInvariantKind kind) noexcept {
  switch (kind) {
    case signature::CheckerInvariantKind::InputReceiptMismatch:
    case signature::CheckerInvariantKind::MissingRequiredFact:
    case signature::CheckerInvariantKind::AdditionalFact:
    case signature::CheckerInvariantKind::InvalidFact:
    case signature::CheckerInvariantKind::StaleRevision:
    case signature::CheckerInvariantKind::ViewMismatch:
    case signature::CheckerInvariantKind::InferenceLifecycle:
    case signature::CheckerInvariantKind::SolverStateInvalid:
    case signature::CheckerInvariantKind::InvalidEmitterOrdinal:
    case signature::CheckerInvariantKind::CanonicalCodecMismatch:
      return static_cast<uint32_t>(kind);
  }
  ZC_UNREACHABLE
}

uint32_t dispatchPhaseTag(dispatch::DispatchInvariantStage stage) noexcept {
  switch (stage) {
    case dispatch::DispatchInvariantStage::Input:
    case dispatch::DispatchInvariantStage::Construction:
    case dispatch::DispatchInvariantStage::Verification:
    case dispatch::DispatchInvariantStage::Encoding:
      return 0x100U + static_cast<uint32_t>(stage);
  }
  ZC_UNREACHABLE
}

uint32_t dispatchKindTag(dispatch::DispatchInvariantKind kind) noexcept {
  switch (kind) {
    case dispatch::DispatchInvariantKind::InputMismatch:
    case dispatch::DispatchInvariantKind::MissingFact:
    case dispatch::DispatchInvariantKind::AdditionalFact:
    case dispatch::DispatchInvariantKind::InvalidFact:
    case dispatch::DispatchInvariantKind::CanonicalCodecMismatch:
      return static_cast<uint32_t>(kind);
  }
  ZC_UNREACHABLE
}

}  // namespace

basic::CompilerIncidentDescriptor CheckerDiagnosticProjector::project(
    const signature::CheckerInvariantFact& invariant) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Checker,
      basic::CompilerIncidentPhaseKey(checkerPhaseTag(invariant.stage)),
      basic::CompilerIncidentKindKey(checkerKindTag(invariant.kind)),
      basic::CompilerIncidentProducerKey(0x00000001), 1);
}

basic::CompilerIncidentDescriptor CheckerDiagnosticProjector::project(
    const dispatch::DispatchInvariantFact& invariant) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Checker,
      basic::CompilerIncidentPhaseKey(dispatchPhaseTag(invariant.stage)),
      basic::CompilerIncidentKindKey(dispatchKindTag(invariant.kind)),
      basic::CompilerIncidentProducerKey(0x00000002), 1);
}

zc::Maybe<basic::BoundedIncidentSet> CheckerDiagnosticProjector::projectAll(
    zc::ArrayPtr<const signature::CheckerVerificationFailure> failures) noexcept {
  if (failures.size() == 0) { return zc::none; }
  basic::BoundedIncidentSet incidents;
  for (const auto& failure : failures) {
    const auto& value = failure.variant();
    const auto descriptor = value.is<identity::IdentityInvariant>()
                                ? identity::IdentityDiagnosticProjector::project(
                                      value.get<identity::IdentityInvariant>())
                                : project(value.get<signature::CheckerInvariantFact>());
    if (!incidents.add(descriptor)) { return zc::none; }
  }
  return incidents;
}

zc::Maybe<basic::BoundedIncidentSet> CheckerDiagnosticProjector::projectAll(
    zc::ArrayPtr<const dispatch::DispatchVerificationFailure> failures) noexcept {
  if (failures.size() == 0) { return zc::none; }
  basic::BoundedIncidentSet incidents;
  for (const auto& failure : failures) {
    const auto& value = failure.variant();
    const auto descriptor = value.is<identity::IdentityInvariant>()
                                ? identity::IdentityDiagnosticProjector::project(
                                      value.get<identity::IdentityInvariant>())
                                : project(value.get<dispatch::DispatchInvariantFact>());
    if (!incidents.add(descriptor)) { return zc::none; }
  }
  return incidents;
}

}  // namespace zomlang::compiler::checker
