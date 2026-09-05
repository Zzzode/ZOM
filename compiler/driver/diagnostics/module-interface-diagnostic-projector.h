// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/driver/interface/module-interface.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::driver {

enum class ModuleInterfaceIncidentProducer : uint8_t {
  Verifier = 0x01,
};

/// \brief Projects module-interface invariant failures to the internal incident rail.
class ModuleInterfaceDiagnosticProjector final {
public:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      const ModuleInterfaceInvariantFact& invariant,
      ModuleInterfaceIncidentProducer producer =
          ModuleInterfaceIncidentProducer::Verifier) noexcept;
  ZC_NODISCARD static zc::Maybe<basic::BoundedIncidentSet> projectAll(
      zc::ArrayPtr<const ModuleInterfaceInvariantFact> invariants,
      ModuleInterfaceIncidentProducer producer =
          ModuleInterfaceIncidentProducer::Verifier) noexcept;
};

}  // namespace zomlang::compiler::driver
