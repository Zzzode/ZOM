// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/binder/metadata/binding-metadata.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::binder {

/// \brief Exhaustive projection from Binder invariants to internal incidents.
class BinderDiagnosticProjector final {
public:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      const BinderInvariantFact& invariant) noexcept;
  ZC_NODISCARD static zc::Maybe<basic::BoundedIncidentSet> projectAll(
      zc::ArrayPtr<const BinderInvariantFact> invariants) noexcept;
};

}  // namespace zomlang::compiler::binder
