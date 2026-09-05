// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/identity/diagnostics/identity-diagnostic-projector.h"

namespace zomlang::compiler::identity {
namespace {

uint32_t phaseTag(IdentityAllocationPhase phase) noexcept {
  switch (phase) {
    case IdentityAllocationPhase::Context:
    case IdentityAllocationPhase::Registry:
    case IdentityAllocationPhase::Encoding:
    case IdentityAllocationPhase::CompilationUnit:
    case IdentityAllocationPhase::Crate:
    case IdentityAllocationPhase::Source:
    case IdentityAllocationPhase::Module:
    case IdentityAllocationPhase::Definition:
    case IdentityAllocationPhase::Impl:
    case IdentityAllocationPhase::GenericParameter:
    case IdentityAllocationPhase::CallableParameter:
    case IdentityAllocationPhase::SemanticType:
      return static_cast<uint32_t>(phase);
  }
  ZC_UNREACHABLE
}

uint32_t kindTag(IdentityInvariantKind kind) noexcept {
  switch (kind) {
    case IdentityInvariantKind::InvalidHandle:
    case IdentityInvariantKind::ForeignContext:
    case IdentityInvariantKind::ForeignRegistry:
    case IdentityInvariantKind::SlotOutOfRange:
    case IdentityInvariantKind::AncestorMismatch:
    case IdentityInvariantKind::InvalidSourceRange:
    case IdentityInvariantKind::DuplicateCanonicalKey:
    case IdentityInvariantKind::InvalidClosedValue:
    case IdentityInvariantKind::PostFreezeMutation:
    case IdentityInvariantKind::BrandExhausted:
    case IdentityInvariantKind::DuplicateSingletonStore:
    case IdentityInvariantKind::NonCanonicalEncoding:
    case IdentityInvariantKind::DigestCollision:
      return static_cast<uint32_t>(kind);
  }
  ZC_UNREACHABLE
}

uint32_t producerTag(IdentityApiSite producer) noexcept {
  switch (producer) {
    case IdentityApiSite::ContextBrandIssue:
    case IdentityApiSite::RegistryBrandIssue:
    case IdentityApiSite::CanonicalEncode:
    case IdentityApiSite::CompilationUnitFreeze:
    case IdentityApiSite::CrateFreeze:
    case IdentityApiSite::SourceFreeze:
    case IdentityApiSite::ModuleFreeze:
    case IdentityApiSite::DefinitionFreeze:
    case IdentityApiSite::ImplFreeze:
    case IdentityApiSite::SemanticTypeStoreCreate:
    case IdentityApiSite::HandleLookup:
    case IdentityApiSite::RegistryMutation:
    case IdentityApiSite::GenericParameterFreeze:
    case IdentityApiSite::CallableParameterFreeze:
      return static_cast<uint32_t>(producer);
  }
  ZC_UNREACHABLE
}

}  // namespace

basic::CompilerIncidentDescriptor IdentityDiagnosticProjector::project(
    const IdentityInvariant& invariant) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Identity,
      basic::CompilerIncidentPhaseKey(phaseTag(invariant.phase())),
      basic::CompilerIncidentKindKey(kindTag(invariant.kind())),
      basic::CompilerIncidentProducerKey(producerTag(invariant.apiSite())), 1);
}

zc::Maybe<basic::BoundedIncidentSet> IdentityDiagnosticProjector::projectAll(
    zc::ArrayPtr<const IdentityInvariant> invariants) noexcept {
  if (invariants.size() == 0) { return zc::none; }
  basic::BoundedIncidentSet incidents;
  for (const auto& invariant : invariants) {
    if (!incidents.add(project(invariant))) { return zc::none; }
  }
  return incidents;
}

}  // namespace zomlang::compiler::identity
