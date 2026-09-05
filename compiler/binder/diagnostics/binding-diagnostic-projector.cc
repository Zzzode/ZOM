// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/binder/diagnostics/binding-diagnostic-projector.h"

namespace zomlang::compiler::binder {
namespace {

uint32_t kindTag(BinderInvariantKind kind) noexcept {
  switch (kind) {
    case BinderInvariantKind::MalformedScopeGraph:
    case BinderInvariantKind::MissingRequiredResolution:
    case BinderInvariantKind::AliasCycle:
    case BinderInvariantKind::InvalidBindingFact:
    case BinderInvariantKind::InvalidEmitterOrdinal:
      return static_cast<uint32_t>(kind);
  }
  ZC_UNREACHABLE
}

uint32_t producerTag(BinderEmitterSite producer) noexcept {
  switch (producer) {
    case BinderEmitterSite::ModuleSkeleton:
    case BinderEmitterSite::ImportBinding:
    case BinderEmitterSite::LabelAndClosure:
      return static_cast<uint32_t>(producer);
  }
  ZC_UNREACHABLE
}

}  // namespace

basic::CompilerIncidentDescriptor BinderDiagnosticProjector::project(
    const BinderInvariantFact& invariant) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Binder, basic::CompilerIncidentPhaseKey(0x00000001),
      basic::CompilerIncidentKindKey(kindTag(invariant.kind)),
      basic::CompilerIncidentProducerKey(producerTag(invariant.emitterSite)), 1);
}

zc::Maybe<basic::BoundedIncidentSet> BinderDiagnosticProjector::projectAll(
    zc::ArrayPtr<const BinderInvariantFact> invariants) noexcept {
  if (invariants.size() == 0) { return zc::none; }
  basic::BoundedIncidentSet incidents;
  for (const auto& invariant : invariants) {
    if (!incidents.add(project(invariant))) { return zc::none; }
  }
  return incidents;
}

}  // namespace zomlang::compiler::binder
