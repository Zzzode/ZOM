// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/diagnostics/consumer/diagnostic-policy.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::diagnostics {

struct DiagnosticPolicyResult::Impl final {
  Impl(ResolvedDiagnosticBatch&& authoritative, zc::Vector<size_t>&& displayedIndices,
       size_t errorCount, size_t omittedCount)
      : authoritative(zc::mv(authoritative)),
        displayedIndices(zc::mv(displayedIndices)),
        errorCount(errorCount),
        omittedCount(omittedCount) {}

  ResolvedDiagnosticBatch authoritative;
  zc::Vector<size_t> displayedIndices;
  size_t errorCount;
  size_t omittedCount;
};

DiagnosticPolicyResult::DiagnosticPolicyResult(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
DiagnosticPolicyResult::~DiagnosticPolicyResult() noexcept(false) = default;
DiagnosticPolicyResult::DiagnosticPolicyResult(DiagnosticPolicyResult&&) noexcept = default;
DiagnosticPolicyResult& DiagnosticPolicyResult::operator=(DiagnosticPolicyResult&&) noexcept =
    default;
const ResolvedDiagnosticBatch& DiagnosticPolicyResult::authoritative() const noexcept {
  return impl->authoritative;
}
size_t DiagnosticPolicyResult::displayedCount() const noexcept {
  return impl->displayedIndices.size();
}
const ResolvedDiagnostic& DiagnosticPolicyResult::displayed(size_t index) const {
  ZC_IREQUIRE(index < impl->displayedIndices.size(), "diagnostic policy index is out of bounds");
  return impl->authoritative.diagnostics()[impl->displayedIndices[index]];
}
size_t DiagnosticPolicyResult::errorCount() const noexcept { return impl->errorCount; }
size_t DiagnosticPolicyResult::omittedCount() const noexcept { return impl->omittedCount; }

DiagnosticPolicyResult applyDiagnosticPolicy(ResolvedDiagnosticBatch&& batch,
                                             const DiagnosticPolicy& policy) {
  zc::Vector<size_t> displayedIndices(batch.size());
  size_t errorCount = 0;
  size_t omittedCount = 0;
  const auto diagnostics = batch.diagnostics();
  for (size_t index = 0; index < diagnostics.size(); ++index) {
    const bool isError = diagnostics[index].severity() >= DiagSeverity::kError;
    if (!isError || errorCount < policy.maximumErrors()) {
      displayedIndices.add(index);
    } else {
      ++omittedCount;
    }
    if (isError) { ++errorCount; }
  }
  return DiagnosticPolicyResult(zc::heap<DiagnosticPolicyResult::Impl>(
      zc::mv(batch), zc::mv(displayedIndices), errorCount, omittedCount));
}

}  // namespace zomlang::compiler::diagnostics
