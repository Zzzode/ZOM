// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>

#include "compiler/diagnostics/fact/diagnostic-materializer.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"

namespace zomlang::compiler::diagnostics {

/// \brief Immutable presentation policy applied after complete materialization.
class DiagnosticPolicy final {
public:
  static constexpr size_t defaultMaximumErrors = 100;

  constexpr explicit DiagnosticPolicy(size_t maximumErrors = defaultMaximumErrors) noexcept
      : maximumErrorCount(maximumErrors) {}

  ZC_NODISCARD constexpr size_t maximumErrors() const noexcept { return maximumErrorCount; }

private:
  size_t maximumErrorCount;
};

/// \brief Immutable policy view that retains the complete authoritative batch.
class DiagnosticPolicyResult final {
public:
  ~DiagnosticPolicyResult() noexcept(false);
  DiagnosticPolicyResult(DiagnosticPolicyResult&&) noexcept;
  DiagnosticPolicyResult& operator=(DiagnosticPolicyResult&&) noexcept;
  ZC_DISALLOW_COPY(DiagnosticPolicyResult);

  ZC_NODISCARD const ResolvedDiagnosticBatch& authoritative() const noexcept;
  ZC_NODISCARD size_t displayedCount() const noexcept;
  ZC_NODISCARD const ResolvedDiagnostic& displayed(size_t index) const;
  ZC_NODISCARD size_t errorCount() const noexcept;
  ZC_NODISCARD size_t omittedCount() const noexcept;
  ZC_NODISCARD bool hasErrors() const noexcept { return errorCount() != 0; }

private:
  struct Impl;
  explicit DiagnosticPolicyResult(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend DiagnosticPolicyResult applyDiagnosticPolicy(ResolvedDiagnosticBatch&&,
                                                      const DiagnosticPolicy&);
};

/// \brief Applies display budgeting without modifying the authoritative batch.
ZC_NODISCARD DiagnosticPolicyResult applyDiagnosticPolicy(ResolvedDiagnosticBatch&& batch,
                                                          const DiagnosticPolicy& policy);

}  // namespace zomlang::compiler::diagnostics
