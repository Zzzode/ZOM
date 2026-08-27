// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "compiler/diagnostics/consumer/diagnostic-emitter.h"
#include "compiler/diagnostics/fact/diagnostic-fact.h"

namespace zomlang::compiler {
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace diagnostics {

/// \brief Complete canonical source facts and their revision-local provenance authority.
class PublishedSourceDiagnostics final {
public:
  ~PublishedSourceDiagnostics() noexcept(false);
  PublishedSourceDiagnostics(PublishedSourceDiagnostics&&) noexcept;
  PublishedSourceDiagnostics& operator=(PublishedSourceDiagnostics&&) noexcept;
  ZC_DISALLOW_COPY(PublishedSourceDiagnostics);

  ZC_NODISCARD zc::ArrayPtr<const DiagnosticFact> facts() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const SourceDiagnosticProvenanceMap& provenance() const noexcept;
  ZC_NODISCARD zc::Vector<DiagnosticFact> takeFacts();
  ZC_NODISCARD SourceDiagnosticProvenanceMap takeProvenance();

private:
  struct Impl;
  explicit PublishedSourceDiagnostics(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class SourceDiagnosticDraftBuffer;
};

/// \brief Non-encodable query-local lex and parse diagnostic draft authority.
class SourceDiagnosticDraftBuffer final {
public:
  struct Checkpoint final {
    uint64_t id = 0;
  };

  SourceDiagnosticDraftBuffer(const source::SourceManager& sources, const source::BufferId& buffer);
  ~SourceDiagnosticDraftBuffer() noexcept(false);
  SourceDiagnosticDraftBuffer(SourceDiagnosticDraftBuffer&&) noexcept;
  SourceDiagnosticDraftBuffer& operator=(SourceDiagnosticDraftBuffer&&) noexcept;
  ZC_DISALLOW_COPY(SourceDiagnosticDraftBuffer);

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

  /// \brief Publishes deterministic facts and complete provenance for one stable source.
  ZC_NODISCARD zc::Maybe<PublishedSourceDiagnostics> publish(const identity::SourceFileKey& source,
                                                             uint64_t sourceByteLength);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace diagnostics
}  // namespace zomlang::compiler
