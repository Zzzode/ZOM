// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/diagnostics/module-graph-diagnostic-projector.h"

namespace zomlang::compiler::binder {
namespace {

uint32_t resolutionKindTag(ModuleResolutionInvariantKind kind) noexcept {
  switch (kind) {
    case ModuleResolutionInvariantKind::InputMismatch:
      return static_cast<uint32_t>(ModuleGraphIncidentKind::MissingRequiredState);
    case ModuleResolutionInvariantKind::InvalidEnvironment:
      return static_cast<uint32_t>(ModuleGraphIncidentKind::InvalidDependencyGraph);
    case ModuleResolutionInvariantKind::InvalidRequest:
      return static_cast<uint32_t>(ModuleGraphIncidentKind::QueryContractViolation);
  }
  ZC_UNREACHABLE
}

}  // namespace

basic::CompilerIncidentDescriptor ModuleGraphDiagnosticProjector::project(
    ModuleGraphIncidentPhase phase, ModuleGraphIncidentKind kind,
    ModuleGraphIncidentProducer producer, uint64_t occurrences) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Driver,
      basic::CompilerIncidentPhaseKey(static_cast<uint32_t>(phase)),
      basic::CompilerIncidentKindKey(static_cast<uint32_t>(kind)),
      basic::CompilerIncidentProducerKey(static_cast<uint32_t>(producer)), occurrences);
}

basic::CompilerIncidentDescriptor ModuleGraphDiagnosticProjector::project(
    const ModuleResolutionInvariantFact& invariant, ModuleGraphIncidentProducer producer) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Driver,
      basic::CompilerIncidentPhaseKey(
          static_cast<uint32_t>(ModuleGraphIncidentPhase::GraphConstruction)),
      basic::CompilerIncidentKindKey(resolutionKindTag(invariant.kind)),
      basic::CompilerIncidentProducerKey(static_cast<uint32_t>(producer)), invariant.occurrence);
}

}  // namespace zomlang::compiler::binder
