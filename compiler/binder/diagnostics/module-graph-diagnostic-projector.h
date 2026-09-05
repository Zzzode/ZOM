// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/binder/graph/module-resolution.h"
#include "zc/core/common.h"

namespace zomlang::compiler::binder {

enum class ModuleGraphIncidentPhase : uint8_t {
  Initialization = 0x01,
  InputStaging = 0x02,
  SourceParsing = 0x03,
  GraphConstruction = 0x04,
  Binding = 0x05,
};

enum class ModuleGraphIncidentKind : uint8_t {
  RegistrationFailed = 0x01,
  MissingRequiredState = 0x02,
  DuplicateCanonicalIdentity = 0x03,
  QueryContractViolation = 0x04,
  MaterializationFailed = 0x05,
  VerificationFailed = 0x06,
  InvalidDependencyGraph = 0x07,
  InvalidBindingHandoff = 0x08,
};

enum class ModuleGraphIncidentProducer : uint8_t {
  Session = 0x01,
  QueryRuntime = 0x02,
  DiagnosticMaterializer = 0x03,
  StructuralResolver = 0x04,
  DependencyDeriver = 0x05,
};

/// \brief Projects module-graph contract failures to the internal incident rail.
class ModuleGraphDiagnosticProjector final {
public:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      ModuleGraphIncidentPhase phase, ModuleGraphIncidentKind kind,
      ModuleGraphIncidentProducer producer, uint64_t occurrences = 1) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      const ModuleResolutionInvariantFact& invariant,
      ModuleGraphIncidentProducer producer) noexcept;
};

}  // namespace zomlang::compiler::binder
