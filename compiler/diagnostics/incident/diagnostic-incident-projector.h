// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/diagnostics/fact/compilation-diagnostic-facts.h"
#include "zc/core/common.h"

namespace zomlang::compiler::diagnostics {

enum class DiagnosticIncidentPhase : uint8_t {
  Projection = 0x01,
  Collection = 0x02,
  Materialization = 0x03,
  Publication = 0x04,
};

enum class DiagnosticIncidentKind : uint8_t {
  ProjectionRejected = 0x01,
  CapacityExceeded = 0x02,
  ConflictingOccurrence = 0x03,
  ConflictingAuthority = 0x04,
  MissingAuthority = 0x05,
  ConflictingProvenance = 0x06,
  InvalidProvenance = 0x07,
  MaterializationFailed = 0x08,
};

enum class DiagnosticIncidentProducer : uint8_t {
  Source = 0x01,
  Binder = 0x02,
  Checker = 0x03,
  Ownership = 0x04,
  Ir = 0x05,
  Package = 0x06,
  Driver = 0x07,
};

/// \brief Projects diagnostics-pipeline contract failures to the internal incident rail.
class DiagnosticIncidentProjector final {
public:
  /// \brief Maps every collector failure to its exact incident kind.
  ZC_NODISCARD static DiagnosticIncidentKind collectionFailureKind(
      DiagnosticCollectionFailure failure) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(DiagnosticIncidentPhase phase,
                                                                DiagnosticIncidentKind kind,
                                                                DiagnosticIncidentProducer producer,
                                                                uint64_t occurrences = 1) noexcept;
};

}  // namespace zomlang::compiler::diagnostics
