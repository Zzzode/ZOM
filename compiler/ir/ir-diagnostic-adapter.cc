// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/ir/ir-diagnostic-adapter.h"

#include "zc/core/string.h"
#include "compiler/diagnostics/core/diagnostic.h"

namespace zomlang::compiler::ir {
namespace {

bool sameSpan(zc::Maybe<const identity::SourceSpan&> left,
              zc::Maybe<const identity::SourceSpan&> right) {
  if (left == zc::none) { return right == zc::none; }
  if (right == zc::none) { return false; }
  ZC_IF_SOME(leftValue, left) {
    ZC_IF_SOME(rightValue, right) {
      return leftValue.source().sameAs(rightValue.source()) &&
             leftValue.byteStart() == rightValue.byteStart() &&
             leftValue.byteEnd() == rightValue.byteEnd();
    }
  }
  ZC_UNREACHABLE
}

bool sameOwner(const IrFailureOwner& left, const IrFailureOwner& right) {
  if (left.kind() != right.kind()) { return false; }
  switch (left.kind()) {
    case IrFailureOwnerKind::Session: {
      ZC_IF_SOME(leftContext, left.sessionContext()) {
        ZC_IF_SOME(rightContext, right.sessionContext()) {
          return leftContext.digest() == rightContext.digest();
        }
      }
      ZC_UNREACHABLE
    }
    case IrFailureOwnerKind::Module: {
      ZC_IF_SOME(leftModule, left.moduleId()) {
        ZC_IF_SOME(rightModule, right.moduleId()) { return leftModule == rightModule; }
      }
      ZC_UNREACHABLE
    }
    case IrFailureOwnerKind::Definition: {
      ZC_IF_SOME(leftDefinition, left.definitionId()) {
        ZC_IF_SOME(rightDefinition, right.definitionId()) {
          return leftDefinition == rightDefinition;
        }
      }
      ZC_UNREACHABLE
    }
    case IrFailureOwnerKind::Instance: {
      ZC_IF_SOME(leftInstance, left.instanceId()) {
        ZC_IF_SOME(rightInstance, right.instanceId()) { return leftInstance == rightInstance; }
      }
      ZC_UNREACHABLE
    }
  }
  ZC_UNREACHABLE
}

bool sameCapabilityRoot(const IrFailureFact& left, const IrFailureFact& right) {
  if (left.kind() != right.kind()) { return false; }
  switch (left.kind()) {
    case IrFailureKind::RecursiveInstantiation:
      return left.detail().cycleValue().root == right.detail().cycleValue().root;
    case IrFailureKind::InstantiationBudgetExceeded:
      return left.detail().budgetValue().root == right.detail().budgetValue().root;
    case IrFailureKind::UnsupportedTargetCapability:
    case IrFailureKind::OutputCreationFailed:
      return sameOwner(left.owner(), right.owner());
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
    case IrFailureKind::BackendTranslationRejected:
    case IrFailureKind::CanonicalCodecMismatch:
      ZC_UNREACHABLE
  }
  ZC_UNREACHABLE
}

zc::Maybe<identity::SourceSpan> cloneSpan(const IrFailureFact& fact) {
  ZC_IF_SOME(span, fact.sourceSpan()) { return span.clone(); }
  return zc::none;
}

}  // namespace

struct IrDiagnosticGroup::Impl final {
  Impl(diagnostics::DiagID diagnosticId, bool invariant,
       zc::Maybe<identity::SourceSpan>&& diagnosticSpan, IrFailureFact&& firstFact)
      : idValue(diagnosticId), invariantValue(invariant), spanValue(zc::mv(diagnosticSpan)) {
    factValues.add(zc::mv(firstFact));
  }

  diagnostics::DiagID idValue;
  bool invariantValue;
  zc::Maybe<identity::SourceSpan> spanValue;
  zc::Vector<IrFailureFact> factValues;
};

IrDiagnosticGroup::IrDiagnosticGroup(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
IrDiagnosticGroup::IrDiagnosticGroup(IrDiagnosticGroup&&) noexcept = default;
IrDiagnosticGroup& IrDiagnosticGroup::operator=(IrDiagnosticGroup&&) noexcept = default;
IrDiagnosticGroup::~IrDiagnosticGroup() noexcept(false) = default;

diagnostics::DiagID IrDiagnosticGroup::diagnosticId() const noexcept { return impl->idValue; }
bool IrDiagnosticGroup::isInvariant() const noexcept { return impl->invariantValue; }
zc::Maybe<const identity::SourceSpan&> IrDiagnosticGroup::diagnosticSpan() const {
  ZC_IF_SOME(span, impl->spanValue) { return span; }
  return zc::none;
}
uint64_t IrDiagnosticGroup::occurrenceCount() const noexcept { return impl->factValues.size(); }
zc::ArrayPtr<const IrFailureFact> IrDiagnosticGroup::facts() const noexcept {
  return impl->factValues.asPtr();
}

diagnostics::DiagID irDiagnosticId(IrFailureKind kind, IrFailurePhase phase) noexcept {
  using diagnostics::DiagID;
  switch (kind) {
    case IrFailureKind::UnsupportedTargetCapability:
      return DiagID::TargetCapabilityUnavailable;
    case IrFailureKind::RecursiveInstantiation:
      return DiagID::RecursiveInstantiation;
    case IrFailureKind::InstantiationBudgetExceeded:
      return DiagID::InstantiationBudgetExceeded;
    case IrFailureKind::OutputCreationFailed:
      return DiagID::IrOutputCreationFailed;
    case IrFailureKind::CanonicalCodecMismatch:
      return DiagID::IrCanonicalCodecMismatch;
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
    case IrFailureKind::BackendTranslationRejected:
      break;
  }

  switch (phase) {
    case IrFailurePhase::CheckedModuleAssembly:
      return DiagID::CheckedModuleInvariant;
    case IrFailurePhase::HirConstruction:
    case IrFailurePhase::HirVerification:
      return DiagID::HirInvariant;
    case IrFailurePhase::MirConstruction:
    case IrFailurePhase::BuiltMirVerification:
      return DiagID::BuiltMirInvariant;
    case IrFailurePhase::OwnershipProofValidation:
      return DiagID::OwnershipProofInvariant;
    case IrFailurePhase::CleanupElaboration:
    case IrFailurePhase::CoroutineElaboration:
    case IrFailurePhase::ExecutableMirVerification:
      return DiagID::ExecutableMirInvariant;
    case IrFailurePhase::Monomorphization:
    case IrFailurePhase::TargetSelection:
    case IrFailurePhase::LirLowering:
    case IrFailurePhase::LirVerification:
      return DiagID::LirInvariant;
    case IrFailurePhase::LlvmTranslation:
    case IrFailurePhase::ObjectEmission:
      return DiagID::BackendInvariant;
    case IrFailurePhase::FeatureBoundaryVerification:
      return DiagID::FeatureBoundaryInvariant;
    // RFC 0043 link/publication phases reuse the backend invariant family; the
    // RFC adds no new diagnostic family.
    case IrFailurePhase::LinkPlanConstruction:
    case IrFailurePhase::LinkerInvocation:
    case IrFailurePhase::ExecutablePublication:
      return DiagID::BackendInvariant;
  }
  ZC_UNREACHABLE
}

zc::Vector<IrDiagnosticGroup> groupIrCapabilityFailures(
    const SortedCapabilityFailureFacts& failures) {
  zc::Vector<IrDiagnosticGroup> groups;
  for (const auto& fact : failures.facts()) {
    const auto id = irDiagnosticId(fact.kind(), fact.phase());
    if (!groups.empty() && groups.back().diagnosticId() == id &&
        sameSpan(groups.back().diagnosticSpan(), fact.sourceSpan()) &&
        sameCapabilityRoot(groups.back().facts()[0], fact)) {
      groups.back().impl->factValues.add(fact.clone());
      continue;
    }
    groups.add(IrDiagnosticGroup(
        zc::heap<IrDiagnosticGroup::Impl>(id, false, cloneSpan(fact), fact.clone())));
  }
  return groups;
}

zc::Vector<IrDiagnosticGroup> groupIrInvariantFailures(
    const SortedIrInvariantFailureFacts& failures) {
  zc::Vector<IrDiagnosticGroup> groups;
  for (const auto& fact : failures.facts()) {
    const auto id = irDiagnosticId(fact.kind(), fact.phase());
    if (!groups.empty() && groups.back().diagnosticId() == id &&
        sameSpan(groups.back().diagnosticSpan(), fact.sourceSpan())) {
      groups.back().impl->factValues.add(fact.clone());
      continue;
    }
    groups.add(IrDiagnosticGroup(
        zc::heap<IrDiagnosticGroup::Impl>(id, true, cloneSpan(fact), fact.clone())));
  }
  return groups;
}

void emitIrDiagnosticGroups(diagnostics::DiagnosticEngine& engine,
                            zc::ArrayPtr<const IrDiagnosticGroup> groups,
                            zc::Maybe<const IrDiagnosticLocationResolver&> locationResolver) {
  for (const auto& group : groups) {
    source::SourceLoc location;
    ZC_IF_SOME(span, group.diagnosticSpan()) {
      ZC_IF_SOME(resolver, locationResolver) {
        ZC_IF_SOME(resolved, resolver.resolve(span)) { location = resolved; }
      }
    }
    if (group.isInvariant()) {
      engine.emit(diagnostics::Diagnostic(group.diagnosticId(), location,
                                          zc::str(group.occurrenceCount())));
    } else {
      engine.emit(diagnostics::Diagnostic(group.diagnosticId(), location));
    }
  }
}

void emitIrIdentityInvariantFailures(
    diagnostics::DiagnosticEngine& engine, const SortedIdentityInvariantFacts& failures,
    zc::Maybe<const identity::IdentityDiagnosticLocationResolver&> locationResolver) {
  auto groups = identity::groupIdentityInvariants(failures.facts());
  identity::emitIdentityDiagnosticGroups(engine, groups.asPtr(), locationResolver);
}

}  // namespace zomlang::compiler::ir
