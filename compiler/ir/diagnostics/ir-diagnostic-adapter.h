// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "compiler/ir/diagnostics/ir-failure.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

/// \brief One registered capability-diagnostic group retaining every contributing IR fact.
class IrCapabilityDiagnosticGroup final {
public:
  IrCapabilityDiagnosticGroup(IrCapabilityDiagnosticGroup&&) noexcept;
  IrCapabilityDiagnosticGroup& operator=(IrCapabilityDiagnosticGroup&&) noexcept;
  ~IrCapabilityDiagnosticGroup() noexcept(false);
  ZC_DISALLOW_COPY(IrCapabilityDiagnosticGroup);

  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> diagnosticSpan() const;
  ZC_NODISCARD uint64_t occurrenceCount() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const IrFailureFact> facts() const noexcept;

private:
  struct Impl;
  explicit IrCapabilityDiagnosticGroup(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;

  friend zc::Vector<IrCapabilityDiagnosticGroup> groupIrCapabilityFailures(
      const SortedCapabilityFailureFacts& failures);
};

/// \brief Deduplicates adjacent capability failures by code, location, and canonical root.
ZC_NODISCARD zc::Vector<IrCapabilityDiagnosticGroup> groupIrCapabilityFailures(
    const SortedCapabilityFailureFacts& failures);

/// \brief Emits registered capability groups without fabricating a location.
void emitIrCapabilityDiagnosticGroups(diagnostics::DiagnosticEngine& engine,
                                      zc::ArrayPtr<const IrCapabilityDiagnosticGroup> groups);

}  // namespace zomlang::compiler::ir
