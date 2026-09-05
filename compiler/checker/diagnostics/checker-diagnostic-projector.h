// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/checker/facts/dispatch-facts.h"
#include "compiler/checker/facts/signature-facts.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::checker {

/// \brief Exhaustive projection from Checker and Dispatch invariants to internal incidents.
class CheckerDiagnosticProjector final {
public:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      const signature::CheckerInvariantFact& invariant) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      const dispatch::DispatchInvariantFact& invariant) noexcept;
  ZC_NODISCARD static zc::Maybe<basic::BoundedIncidentSet> projectAll(
      zc::ArrayPtr<const signature::CheckerVerificationFailure> failures) noexcept;
  ZC_NODISCARD static zc::Maybe<basic::BoundedIncidentSet> projectAll(
      zc::ArrayPtr<const dispatch::DispatchVerificationFailure> failures) noexcept;
};

}  // namespace zomlang::compiler::checker
