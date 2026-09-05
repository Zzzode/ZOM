// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/identity/diagnostics/identity-invariant.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::identity {

/// \brief Exhaustive projection from Identity invariants to internal incidents.
class IdentityDiagnosticProjector final {
public:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      const IdentityInvariant& invariant) noexcept;
  ZC_NODISCARD static zc::Maybe<basic::BoundedIncidentSet> projectAll(
      zc::ArrayPtr<const IdentityInvariant> invariants) noexcept;
};

}  // namespace zomlang::compiler::identity
