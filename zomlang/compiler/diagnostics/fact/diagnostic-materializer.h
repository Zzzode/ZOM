// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"

namespace zomlang::compiler {
namespace identity {
class SourceFileKey;
}
namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace diagnostics {

class Diagnostic;
class DiagnosticEngine;

enum class DiagnosticMaterializationFailure : uint8_t {
  MissingProvenance = 0x01,
  ForeignSource = 0x02,
  OutOfRange = 0x03,
  RoleMismatch = 0x04,
  ArgumentMismatch = 0x05,
};

/// \brief Resolves revision-local provenance without exposing provider state.
class DiagnosticProvenanceResolver {
public:
  virtual ~DiagnosticProvenanceResolver() noexcept(false) = default;
  ZC_DISALLOW_COPY_AND_MOVE(DiagnosticProvenanceResolver);

  ZC_NODISCARD virtual bool owns(const identity::SourceFileKey& source) const noexcept = 0;
  ZC_NODISCARD virtual zc::Maybe<const DiagnosticSourceRange&> resolve(
      const DiagnosticProvenanceKey& key) const noexcept = 0;

protected:
  DiagnosticProvenanceResolver() = default;
};

/// \brief Source-specific resolver over one retained provenance map.
class SourceDiagnosticProvenanceResolver final : public DiagnosticProvenanceResolver {
public:
  SourceDiagnosticProvenanceResolver(const identity::SourceFileKey& source,
                                     const SourceDiagnosticProvenanceMap& provenance) noexcept;
  ~SourceDiagnosticProvenanceResolver() noexcept(false) override;

  ZC_NODISCARD bool owns(const identity::SourceFileKey& candidate) const noexcept override;
  ZC_NODISCARD zc::Maybe<const DiagnosticSourceRange&> resolve(
      const DiagnosticProvenanceKey& key) const noexcept override;

private:
  const identity::SourceFileKey& source;
  const SourceDiagnosticProvenanceMap& provenance;
};

/// \brief Complete materialized batch that cannot be partially published.
class ResolvedDiagnosticBatch final {
public:
  ~ResolvedDiagnosticBatch() noexcept(false);
  ResolvedDiagnosticBatch(ResolvedDiagnosticBatch&&) noexcept;
  ResolvedDiagnosticBatch& operator=(ResolvedDiagnosticBatch&&) noexcept;
  ZC_DISALLOW_COPY(ResolvedDiagnosticBatch);

  ZC_NODISCARD size_t size() const noexcept;

private:
  struct Impl;
  explicit ResolvedDiagnosticBatch(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend zc::OneOf<ResolvedDiagnosticBatch, DiagnosticMaterializationFailure>
  materializeDiagnosticFacts(zc::ArrayPtr<const DiagnosticFact>,
                             const DiagnosticProvenanceResolver&, source::SourceManager&,
                             const source::BufferId&);
  friend void publishResolvedDiagnosticBatch(ResolvedDiagnosticBatch&&, DiagnosticEngine&);
};

using DiagnosticMaterializationResult =
    zc::OneOf<ResolvedDiagnosticBatch, DiagnosticMaterializationFailure>;

/// \brief Resolves and validates the entire batch without touching a diagnostic engine.
ZC_NODISCARD DiagnosticMaterializationResult materializeDiagnosticFacts(
    zc::ArrayPtr<const DiagnosticFact> facts, const DiagnosticProvenanceResolver& resolver,
    source::SourceManager& sources, const source::BufferId& buffer);

/// \brief Publishes one completely resolved batch exactly once.
void publishResolvedDiagnosticBatch(ResolvedDiagnosticBatch&& batch, DiagnosticEngine& engine);

}  // namespace diagnostics
}  // namespace zomlang::compiler
