// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/checker/checker-identity-authority.h"
#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "compiler/diagnostics/fact/semantic-diagnostic-fact.h"
#include "compiler/ir/diagnostics/ir-failure.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

/// \brief One canonical capability-failure group retaining every contributing IR fact.
class IrCapabilityFailureGroup final {
public:
  IrCapabilityFailureGroup(IrCapabilityFailureGroup&&) noexcept;
  IrCapabilityFailureGroup& operator=(IrCapabilityFailureGroup&&) noexcept;
  ~IrCapabilityFailureGroup() noexcept(false);
  ZC_DISALLOW_COPY(IrCapabilityFailureGroup);

  ZC_NODISCARD IrFailureKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> diagnosticSpan() const;
  ZC_NODISCARD uint64_t occurrenceCount() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const IrFailureFact> facts() const noexcept;

private:
  struct Impl;
  explicit IrCapabilityFailureGroup(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;

  friend zc::Vector<IrCapabilityFailureGroup> groupIrCapabilityFailures(
      const SortedCapabilityFailureFacts& failures);
};

/// \brief Source-backed diagnostics and source-less operational failures from one rejection.
struct IrCapabilityFailureProjection final {
  diagnostics::SemanticDiagnosticFactBatch sourceDiagnostics;
  zc::Vector<IrCapabilityFailureGroup> operationalFailures;
};

using IrCapabilityFailureProjectionResult =
    zc::OneOf<IrCapabilityFailureProjection, diagnostics::SemanticDiagnosticBatchProjectionFailure>;

/// \brief Deduplicates adjacent capability failures by kind, location, and canonical root.
ZC_NODISCARD zc::Vector<IrCapabilityFailureGroup> groupIrCapabilityFailures(
    const SortedCapabilityFailureFacts& failures);

/// \brief Projects source-backed groups and preserves source-less groups as operational failures.
ZC_NODISCARD IrCapabilityFailureProjectionResult
projectIrCapabilityFailures(const SortedCapabilityFailureFacts& failures,
                            const checker::CheckerIdentityAuthority& identities);

/// \brief Returns the closed display token for a source-less operational failure.
ZC_NODISCARD zc::StringPtr irOperationalFailureDisplay(IrFailureKind kind) noexcept;

}  // namespace zomlang::compiler::ir
