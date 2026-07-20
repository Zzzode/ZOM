// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/diagnostic-emitter.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact.h"

namespace zomlang::compiler {
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace diagnostics {

/// \brief Query-local lex/parse diagnostic authority with parser transactions.
class DiagnosticFactBuffer final {
public:
  struct Checkpoint final {
    uint64_t id = 0;
  };

  DiagnosticFactBuffer(const source::SourceManager& sources, const source::BufferId& buffer);
  ~DiagnosticFactBuffer() noexcept(false);
  DiagnosticFactBuffer(DiagnosticFactBuffer&&) noexcept;
  DiagnosticFactBuffer& operator=(DiagnosticFactBuffer&&) noexcept;
  ZC_DISALLOW_COPY(DiagnosticFactBuffer);

  ZC_NODISCARD DiagnosticEmitter& lexerEmitter();
  ZC_NODISCARD DiagnosticEmitter& parserEmitter();

  ZC_NODISCARD Checkpoint checkpoint();
  void commit(Checkpoint checkpoint);
  void rollback(Checkpoint checkpoint);

  ZC_NODISCARD bool hasErrors() const noexcept;
  ZC_NODISCARD size_t errorCount() const noexcept;
  ZC_NODISCARD bool hasInvariantViolation() const noexcept;
  ZC_NODISCARD zc::StringPtr invariantMessage() const ZC_LIFETIMEBOUND;
  void reportInvariant(zc::String&& message);

  /// \brief Transfers all facts in canonical occurrence order.
  ZC_NODISCARD zc::Vector<DiagnosticFact> takeFactsCanonical();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace diagnostics
}  // namespace zomlang::compiler
