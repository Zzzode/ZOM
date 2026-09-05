// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/driver/diagnostics/module-interface-diagnostic-projector.h"

namespace zomlang::compiler::driver {
namespace {

uint32_t phaseTag(ModuleInterfaceInvariantStage stage) noexcept {
  switch (stage) {
    case ModuleInterfaceInvariantStage::Input:
    case ModuleInterfaceInvariantStage::Projection:
    case ModuleInterfaceInvariantStage::Verification:
    case ModuleInterfaceInvariantStage::Encoding:
      return static_cast<uint32_t>(stage);
  }
  ZC_UNREACHABLE
}

uint32_t kindTag(ModuleInterfaceInvariantKind kind) noexcept {
  switch (kind) {
    case ModuleInterfaceInvariantKind::InputMismatch:
    case ModuleInterfaceInvariantKind::MissingProjection:
    case ModuleInterfaceInvariantKind::AdditionalProjection:
    case ModuleInterfaceInvariantKind::InvalidProjection:
    case ModuleInterfaceInvariantKind::CanonicalCodecMismatch:
      return static_cast<uint32_t>(kind);
  }
  ZC_UNREACHABLE
}

}  // namespace

basic::CompilerIncidentDescriptor ModuleInterfaceDiagnosticProjector::project(
    const ModuleInterfaceInvariantFact& invariant,
    ModuleInterfaceIncidentProducer producer) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Driver,
      basic::CompilerIncidentPhaseKey(phaseTag(invariant.stage)),
      basic::CompilerIncidentKindKey(kindTag(invariant.kind)),
      basic::CompilerIncidentProducerKey(0x100U + static_cast<uint32_t>(producer)), 1);
}

zc::Maybe<basic::BoundedIncidentSet> ModuleInterfaceDiagnosticProjector::projectAll(
    zc::ArrayPtr<const ModuleInterfaceInvariantFact> invariants,
    ModuleInterfaceIncidentProducer producer) noexcept {
  if (invariants.size() == 0) { return zc::none; }
  basic::BoundedIncidentSet incidents;
  for (const auto& invariant : invariants) {
    if (!incidents.add(project(invariant, producer))) { return zc::none; }
  }
  return incidents;
}

}  // namespace zomlang::compiler::driver
