// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/ir/diagnostics/ir-diagnostic-adapter.h"

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

struct IrCapabilityDiagnosticGroup::Impl final {
  Impl(diagnostics::DiagID diagnosticId, zc::Maybe<identity::SourceSpan>&& diagnosticSpan,
       IrFailureFact&& firstFact)
      : idValue(diagnosticId), spanValue(zc::mv(diagnosticSpan)) {
    factValues.add(zc::mv(firstFact));
  }

  diagnostics::DiagID idValue;
  zc::Maybe<identity::SourceSpan> spanValue;
  zc::Vector<IrFailureFact> factValues;
};

IrCapabilityDiagnosticGroup::IrCapabilityDiagnosticGroup(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
IrCapabilityDiagnosticGroup::IrCapabilityDiagnosticGroup(IrCapabilityDiagnosticGroup&&) noexcept =
    default;
IrCapabilityDiagnosticGroup& IrCapabilityDiagnosticGroup::operator=(
    IrCapabilityDiagnosticGroup&&) noexcept = default;
IrCapabilityDiagnosticGroup::~IrCapabilityDiagnosticGroup() noexcept(false) = default;

diagnostics::DiagID IrCapabilityDiagnosticGroup::diagnosticId() const noexcept {
  return impl->idValue;
}
zc::Maybe<const identity::SourceSpan&> IrCapabilityDiagnosticGroup::diagnosticSpan() const {
  ZC_IF_SOME(span, impl->spanValue) { return span; }
  return zc::none;
}
uint64_t IrCapabilityDiagnosticGroup::occurrenceCount() const noexcept {
  return impl->factValues.size();
}
zc::ArrayPtr<const IrFailureFact> IrCapabilityDiagnosticGroup::facts() const noexcept {
  return impl->factValues.asPtr();
}

diagnostics::DiagID capabilityDiagnosticId(IrFailureKind kind) noexcept {
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

zc::Vector<IrCapabilityDiagnosticGroup> groupIrCapabilityFailures(
    const SortedCapabilityFailureFacts& failures) {
  zc::Vector<IrCapabilityDiagnosticGroup> groups;
  for (const auto& fact : failures.facts()) {
    const auto id = capabilityDiagnosticId(fact.kind());
    if (!groups.empty() && groups.back().diagnosticId() == id &&
        sameSpan(groups.back().diagnosticSpan(), fact.sourceSpan()) &&
        sameCapabilityRoot(groups.back().facts()[0], fact)) {
      groups.back().impl->factValues.add(fact.clone());
      continue;
    }
    groups.add(IrCapabilityDiagnosticGroup(
        zc::heap<IrCapabilityDiagnosticGroup::Impl>(id, cloneSpan(fact), fact.clone())));
  }
  return groups;
}

void emitIrCapabilityDiagnosticGroups(diagnostics::DiagnosticEngine& engine,
                                      zc::ArrayPtr<const IrCapabilityDiagnosticGroup> groups) {
  for (const auto& group : groups) {
    engine.emit(diagnostics::Diagnostic(group.diagnosticId(), source::SourceLoc()));
  }
}

}  // namespace zomlang::compiler::ir
