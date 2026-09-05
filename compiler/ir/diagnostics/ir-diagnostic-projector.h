// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/basic/incident/compiler-incident.h"
#include "compiler/ir/diagnostics/ir-failure.h"
#include "compiler/ir/target/target-registry.h"
#include "zc/core/common.h"

namespace zomlang::compiler::ir {

enum class IrIncidentProducer : uint8_t {
  PhaseBoundary = 0x01,
  FrontendHandoff = 0x02,
  Hir = 0x03,
  Mir = 0x04,
  Lir = 0x05,
  Backend = 0x06,
};

/// \brief Projects IR and backend invariant failures to the internal incident rail.
class IrDiagnosticProjector final {
public:
  ZC_NODISCARD static zc::Maybe<basic::BoundedIncidentSet> projectAll(
      const SortedIrInvariantFailureFacts& failures) noexcept;
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      TargetSelectionVerificationIssue issue) noexcept;

private:
  ZC_NODISCARD static basic::CompilerIncidentDescriptor project(
      const IrFailureFact& failure) noexcept;
};

}  // namespace zomlang::compiler::ir
