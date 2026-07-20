// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/identity/identity-diagnostic-adapter.h"
#include "zomlang/compiler/ir/ir-failure.h"

namespace zomlang::compiler::ir {

/// \brief Resolves one validated canonical IR source span into a live source buffer.
class IrDiagnosticLocationResolver {
public:
  virtual ~IrDiagnosticLocationResolver() noexcept(false) = default;
  ZC_DISALLOW_COPY(IrDiagnosticLocationResolver);

  ZC_NODISCARD virtual zc::Maybe<source::SourceLoc> resolve(
      const identity::SourceSpan& span) const = 0;

protected:
  IrDiagnosticLocationResolver() noexcept = default;
  IrDiagnosticLocationResolver(IrDiagnosticLocationResolver&&) noexcept = default;
  IrDiagnosticLocationResolver& operator=(IrDiagnosticLocationResolver&&) noexcept = default;
};

/// \brief One registered diagnostic group retaining every complete contributing IR fact.
class IrDiagnosticGroup final {
public:
  IrDiagnosticGroup(IrDiagnosticGroup&&) noexcept;
  IrDiagnosticGroup& operator=(IrDiagnosticGroup&&) noexcept;
  ~IrDiagnosticGroup() noexcept(false);
  ZC_DISALLOW_COPY(IrDiagnosticGroup);

  ZC_NODISCARD diagnostics::DiagID diagnosticId() const noexcept;
  ZC_NODISCARD bool isInvariant() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::SourceSpan&> diagnosticSpan() const;
  ZC_NODISCARD uint64_t occurrenceCount() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const IrFailureFact> facts() const noexcept;

private:
  struct Impl;
  explicit IrDiagnosticGroup(zc::Own<Impl>&& impl) noexcept;

  zc::Own<Impl> impl;

  friend zc::Vector<IrDiagnosticGroup> groupIrCapabilityFailures(
      const SortedCapabilityFailureFacts& failures);
  friend zc::Vector<IrDiagnosticGroup> groupIrInvariantFailures(
      const SortedIrInvariantFailureFacts& failures);
};

/// \brief Exhaustively maps one closed RFC 0010 kind and phase to a registered diagnostic.
ZC_NODISCARD diagnostics::DiagID irDiagnosticId(IrFailureKind kind, IrFailurePhase phase) noexcept;

/// \brief Deduplicates adjacent capability failures by code, location, and canonical root.
ZC_NODISCARD zc::Vector<IrDiagnosticGroup> groupIrCapabilityFailures(
    const SortedCapabilityFailureFacts& failures);

/// \brief Groups adjacent invariant failures only by mapped diagnostic and validated location.
ZC_NODISCARD zc::Vector<IrDiagnosticGroup> groupIrInvariantFailures(
    const SortedIrInvariantFailureFacts& failures);

/// \brief Emits registered capability or invariant groups without fabricating a location.
void emitIrDiagnosticGroups(
    diagnostics::DiagnosticEngine& engine, zc::ArrayPtr<const IrDiagnosticGroup> groups,
    zc::Maybe<const IrDiagnosticLocationResolver&> locationResolver = zc::none);

/// \brief Routes RFC 0010 identity rejection through the canonical RFC 0011 adapter.
void emitIrIdentityInvariantFailures(
    diagnostics::DiagnosticEngine& engine, const SortedIdentityInvariantFacts& failures,
    zc::Maybe<const identity::IdentityDiagnosticLocationResolver&> locationResolver = zc::none);

}  // namespace zomlang::compiler::ir
