// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/ir/diagnostics/ir-capability-failure-projector.h"

#include "zc/core/debug.h"

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

bool appendProjection(diagnostics::SemanticDiagnosticFactBatch& batch,
                      diagnostics::SemanticDiagnosticProjectionResult&& result) {
  if (!result.is<diagnostics::SemanticDiagnosticFactProjection>()) return false;
  auto projected = zc::mv(result).get<diagnostics::SemanticDiagnosticFactProjection>();
  batch.facts.add(zc::mv(projected.fact));
  for (auto& entry : projected.provenance) { batch.provenance.add(zc::mv(entry)); }
  return true;
}

zc::Maybe<const checker::CheckerIdentityAuthority::BoundModuleView&> moduleForSource(
    const checker::CheckerIdentityAuthority& identities, const identity::SourceFileKey& source) {
  auto sourceEntry = identities.sourceFile(source);
  if (sourceEntry == zc::none) return zc::none;
  zc::Maybe<const checker::CheckerIdentityAuthority::BoundModuleView&> result;
  ZC_IF_SOME(entry, sourceEntry) {
    for (const auto& module : identities.modules()) {
      if (module.sourceFile() != entry.handle()) continue;
      if (result != zc::none) return zc::none;
      result = module;
    }
  }
  return result;
}

bool groupHasConsistentSpan(const IrCapabilityFailureGroup& group) {
  const auto expected = group.diagnosticSpan();
  for (const auto& fact : group.facts()) {
    if (!sameSpan(expected, fact.sourceSpan())) return false;
  }
  return true;
}

zc::Vector<uint32_t> diagnosticEventPath(const IrFailureFact& root) {
  zc::Vector<uint32_t> path;
  path.add(static_cast<uint32_t>(root.phase()));
  path.add(static_cast<uint32_t>(root.kind()));
  return path;
}

}  // namespace

struct IrCapabilityFailureGroup::Impl final {
  Impl(zc::Maybe<identity::SourceSpan>&& diagnosticSpan, IrFailureFact&& firstFact)
      : spanValue(zc::mv(diagnosticSpan)) {
    factValues.add(zc::mv(firstFact));
  }

  zc::Maybe<identity::SourceSpan> spanValue;
  zc::Vector<IrFailureFact> factValues;
};

IrCapabilityFailureGroup::IrCapabilityFailureGroup(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
IrCapabilityFailureGroup::IrCapabilityFailureGroup(IrCapabilityFailureGroup&&) noexcept = default;
IrCapabilityFailureGroup& IrCapabilityFailureGroup::operator=(IrCapabilityFailureGroup&&) noexcept =
    default;
IrCapabilityFailureGroup::~IrCapabilityFailureGroup() noexcept(false) = default;

IrFailureKind IrCapabilityFailureGroup::kind() const noexcept { return impl->factValues[0].kind(); }
zc::Maybe<const identity::SourceSpan&> IrCapabilityFailureGroup::diagnosticSpan() const {
  ZC_IF_SOME(span, impl->spanValue) { return span; }
  return zc::none;
}
uint64_t IrCapabilityFailureGroup::occurrenceCount() const noexcept {
  return impl->factValues.size();
}
zc::ArrayPtr<const IrFailureFact> IrCapabilityFailureGroup::facts() const noexcept {
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

zc::StringPtr irOperationalFailureDisplay(IrFailureKind kind) noexcept {
  switch (kind) {
    case IrFailureKind::UnsupportedTargetCapability:
      return "unsupported-target-capability"_zc;
    case IrFailureKind::OutputCreationFailed:
      return "output-creation-failed"_zc;
    case IrFailureKind::RecursiveInstantiation:
      return "recursive-instantiation"_zc;
    case IrFailureKind::InstantiationBudgetExceeded:
      return "instantiation-budget-exceeded"_zc;
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
      ZC_UNREACHABLE;
  }
  ZC_UNREACHABLE;
}

zc::Vector<IrCapabilityFailureGroup> groupIrCapabilityFailures(
    const SortedCapabilityFailureFacts& failures) {
  zc::Vector<IrCapabilityFailureGroup> groups;
  for (const auto& fact : failures.facts()) {
    if (!groups.empty() && groups.back().kind() == fact.kind() &&
        sameSpan(groups.back().diagnosticSpan(), fact.sourceSpan()) &&
        sameCapabilityRoot(groups.back().facts()[0], fact)) {
      groups.back().impl->factValues.add(fact.clone());
      continue;
    }
    groups.add(IrCapabilityFailureGroup(
        zc::heap<IrCapabilityFailureGroup::Impl>(cloneSpan(fact), fact.clone())));
  }
  return groups;
}

IrCapabilityFailureProjectionResult projectIrCapabilityFailures(
    const SortedCapabilityFailureFacts& failures,
    const checker::CheckerIdentityAuthority& identities) {
  IrCapabilityFailureProjection result;
  auto groups = groupIrCapabilityFailures(failures);
  for (auto& group : groups) {
    if (!groupHasConsistentSpan(group) || group.facts().size() == 0) {
      return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
    }
    if (group.kind() == IrFailureKind::OutputCreationFailed || group.diagnosticSpan() == zc::none) {
      result.operationalFailures.add(zc::mv(group));
      continue;
    }
    const auto& root = group.facts()[0];
    ZC_IF_SOME(span, group.diagnosticSpan()) {
      auto boundModule = moduleForSource(identities, span.source());
      if (boundModule == zc::none) {
        return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
      }
      ZC_IF_SOME(moduleView, boundModule) {
        if (span.byteEnd() > moduleView.parsedModule().byteLength()) {
          return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
        }
        auto module = identities.module(moduleView.module());
        if (module == zc::none) {
          return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidModuleIdentity;
        }
        zc::Vector<zc::String> arguments;
        zc::Vector<diagnostics::SemanticDiagnosticRelatedSite> related;
        related.add(diagnostics::SemanticDiagnosticRelatedSite{
            diagnostics::DiagnosticSecondaryRole::Highlight, zc::none, span.clone(),
            zc::Vector<zc::String>()});
        auto path = diagnosticEventPath(root);
        ZC_IF_SOME(moduleEntry, module) {
          if (!appendProjection(
                  result.sourceDiagnostics,
                  diagnostics::projectSemanticDiagnosticFact(
                      moduleEntry.key(), diagnostics::SemanticDiagnosticDomain::IrCapability,
                      root.canonicalSortKey(), path.asPtr(), capabilityDiagnosticId(group.kind()),
                      zc::mv(arguments), span, zc::mv(related)))) {
            return diagnostics::SemanticDiagnosticBatchProjectionFailure::InvalidFactProjection;
          }
        }
      }
    }
  }
  return result;
}

}  // namespace zomlang::compiler::ir
