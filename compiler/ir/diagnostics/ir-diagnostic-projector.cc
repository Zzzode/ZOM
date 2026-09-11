// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/ir/diagnostics/ir-diagnostic-projector.h"

namespace zomlang::compiler::ir {
namespace {

basic::CompilerIncidentDomain domain(IrFailurePhase phase) noexcept {
  switch (phase) {
    case IrFailurePhase::LlvmTranslation:
    case IrFailurePhase::ObjectEmission:
    case IrFailurePhase::LinkPlanConstruction:
    case IrFailurePhase::LinkerInvocation:
    case IrFailurePhase::ExecutablePublication:
      return basic::CompilerIncidentDomain::Backend;
    case IrFailurePhase::CheckedModuleAssembly:
    case IrFailurePhase::HirConstruction:
    case IrFailurePhase::HirVerification:
    case IrFailurePhase::MirConstruction:
    case IrFailurePhase::BuiltMirVerification:
    case IrFailurePhase::ProofValidation:
    case IrFailurePhase::CleanupElaboration:
    case IrFailurePhase::CoroutineElaboration:
    case IrFailurePhase::ExecutableMirVerification:
    case IrFailurePhase::Monomorphization:
    case IrFailurePhase::TargetSelection:
    case IrFailurePhase::LirLowering:
    case IrFailurePhase::LirVerification:
    case IrFailurePhase::FeatureBoundaryVerification:
      return basic::CompilerIncidentDomain::Ir;
  }
  ZC_UNREACHABLE
}

uint32_t kindTag(IrFailureKind kind) noexcept {
  switch (kind) {
    case IrFailureKind::InputRevisionMismatch:
    case IrFailureKind::MissingRequiredFact:
    case IrFailureKind::AdditionalFact:
    case IrFailureKind::InvalidFact:
    case IrFailureKind::InvalidControlFlow:
    case IrFailureKind::InvalidPlace:
    case IrFailureKind::InvalidOwnershipProof:
    case IrFailureKind::InvalidCleanup:
    case IrFailureKind::InvalidCoroutineState:
    case IrFailureKind::InvalidSsa:
    case IrFailureKind::MissingTargetLayout:
    case IrFailureKind::InvalidAbi:
    case IrFailureKind::UnresolvedDispatch:
    case IrFailureKind::UnsupportedTargetCapability:
    case IrFailureKind::UnsupportedSourceConstruct:
    case IrFailureKind::BackendTranslationRejected:
    case IrFailureKind::RecursiveInstantiation:
    case IrFailureKind::InstantiationBudgetExceeded:
    case IrFailureKind::OutputCreationFailed:
    case IrFailureKind::CanonicalCodecMismatch:
      return static_cast<uint32_t>(kind);
  }
  ZC_UNREACHABLE
}

IrIncidentProducer producer(const IrFailureFact& failure) noexcept {
  ZC_IF_SOME(site, failure.site()) {
    switch (site.kind()) {
      case IrFailureSiteKind::FrontendHandoff:
        return IrIncidentProducer::FrontendHandoff;
      case IrFailureSiteKind::Hir:
        return IrIncidentProducer::Hir;
      case IrFailureSiteKind::Mir:
        return IrIncidentProducer::Mir;
      case IrFailureSiteKind::Lir:
        return IrIncidentProducer::Lir;
      case IrFailureSiteKind::Backend:
        return IrIncidentProducer::Backend;
    }
  }
  return IrIncidentProducer::PhaseBoundary;
}

}  // namespace

basic::CompilerIncidentDescriptor IrDiagnosticProjector::project(
    const IrFailureFact& failure) noexcept {
  return basic::CompilerIncidentDescriptor(
      domain(failure.phase()),
      basic::CompilerIncidentPhaseKey(static_cast<uint32_t>(failure.phase())),
      basic::CompilerIncidentKindKey(kindTag(failure.kind())),
      basic::CompilerIncidentProducerKey(static_cast<uint32_t>(producer(failure))), 1);
}

zc::Maybe<basic::BoundedIncidentSet> IrDiagnosticProjector::projectAll(
    const SortedIrInvariantFailureFacts& failures) noexcept {
  basic::BoundedIncidentSet incidents;
  for (const auto& failure : failures.facts()) {
    if (!incidents.add(IrDiagnosticProjector::project(failure))) { return zc::none; }
  }
  if (incidents.empty()) { return zc::none; }
  return incidents;
}

basic::CompilerIncidentDescriptor IrDiagnosticProjector::project(
    TargetSelectionVerificationIssue issue) noexcept {
  IrFailureKind kind = IrFailureKind::InvalidFact;
  switch (issue) {
    case TargetSelectionVerificationIssue::RegistryRevisionMismatch:
      kind = IrFailureKind::InputRevisionMismatch;
      break;
    case TargetSelectionVerificationIssue::ProjectionMismatch:
    case TargetSelectionVerificationIssue::InvalidFact:
      kind = IrFailureKind::InvalidFact;
      break;
    case TargetSelectionVerificationIssue::CapabilityUnavailable:
      ZC_UNREACHABLE
  }
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Ir,
      basic::CompilerIncidentPhaseKey(static_cast<uint32_t>(IrFailurePhase::TargetSelection)),
      basic::CompilerIncidentKindKey(static_cast<uint32_t>(kind)),
      basic::CompilerIncidentProducerKey(static_cast<uint32_t>(IrIncidentProducer::PhaseBoundary)),
      1);
}

}  // namespace zomlang::compiler::ir
