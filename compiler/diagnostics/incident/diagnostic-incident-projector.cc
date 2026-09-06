// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/incident/diagnostic-incident-projector.h"

namespace zomlang::compiler::diagnostics {

DiagnosticIncidentKind DiagnosticIncidentProjector::collectionFailureKind(
    DiagnosticCollectionFailure failure) noexcept {
  switch (failure) {
    case DiagnosticCollectionFailure::CapacityExceeded:
      return DiagnosticIncidentKind::CapacityExceeded;
    case DiagnosticCollectionFailure::ConflictingOccurrence:
      return DiagnosticIncidentKind::ConflictingOccurrence;
    case DiagnosticCollectionFailure::ConflictingAuthority:
      return DiagnosticIncidentKind::ConflictingAuthority;
    case DiagnosticCollectionFailure::MissingAuthority:
      return DiagnosticIncidentKind::MissingAuthority;
    case DiagnosticCollectionFailure::ConflictingProvenance:
      return DiagnosticIncidentKind::ConflictingProvenance;
    case DiagnosticCollectionFailure::InvalidProvenance:
      return DiagnosticIncidentKind::InvalidProvenance;
  }
  ZC_UNREACHABLE;
}

basic::CompilerIncidentDescriptor DiagnosticIncidentProjector::project(
    DiagnosticIncidentPhase phase, DiagnosticIncidentKind kind, DiagnosticIncidentProducer producer,
    uint64_t occurrences) noexcept {
  return basic::CompilerIncidentDescriptor(
      basic::CompilerIncidentDomain::Diagnostics,
      basic::CompilerIncidentPhaseKey(static_cast<uint32_t>(phase)),
      basic::CompilerIncidentKindKey(static_cast<uint32_t>(kind)),
      basic::CompilerIncidentProducerKey(static_cast<uint32_t>(producer)), occurrences);
}

}  // namespace zomlang::compiler::diagnostics
