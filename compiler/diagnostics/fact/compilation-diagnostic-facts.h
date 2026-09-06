// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstddef>
#include <cstdint>

#include "compiler/diagnostics/fact/diagnostic-fact.h"
#include "compiler/identity/key/source-key.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::diagnostics {

enum class DiagnosticCollectionFailure : uint8_t {
  CapacityExceeded = 0x01,
  ConflictingOccurrence = 0x02,
  ConflictingAuthority = 0x03,
  MissingAuthority = 0x04,
  ConflictingProvenance = 0x05,
  InvalidProvenance = 0x06,
};

/// \brief One sealed revision-local source authority and its diagnostic provenance.
struct CompilationDiagnosticSource final {
  identity::SourceFileKey source;
  SourceDiagnosticProvenanceMap provenance;
};

/// \brief One exact non-source input document and its diagnostic provenance.
struct CompilationDiagnosticDocument final {
  zc::Array<uint8_t> identity;
  uint64_t byteLength;
  zc::Array<SourceDiagnosticProvenanceEntry> provenance;
};

/// \brief One exact non-source document admitted by a diagnostic producer.
struct DiagnosticDocumentAuthority final {
  zc::Array<uint8_t> identity;
  uint64_t byteLength;
};

/// \brief Immutable canonical diagnostics for one complete compilation request.
class CompilationDiagnosticFacts final {
public:
  ~CompilationDiagnosticFacts() noexcept(false);
  CompilationDiagnosticFacts(CompilationDiagnosticFacts&&) noexcept;
  CompilationDiagnosticFacts& operator=(CompilationDiagnosticFacts&&) noexcept;
  ZC_DISALLOW_COPY(CompilationDiagnosticFacts);

  ZC_NODISCARD zc::ArrayPtr<const DiagnosticFact> facts() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const CompilationDiagnosticSource> sources() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const CompilationDiagnosticDocument> documents() const ZC_LIFETIMEBOUND;

private:
  struct Impl;
  explicit CompilationDiagnosticFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class DiagnosticFactCollector;
  friend class CompilationDiagnosticCollector;
};

using DiagnosticCollectionResult =
    zc::OneOf<CompilationDiagnosticFacts, DiagnosticCollectionFailure>;

/// \brief Request-local owner of diagnostic fact roots and revision-local provenance.
class CompilationDiagnosticCollector final {
public:
  static constexpr size_t maximumProvenanceEntries = 1048576;

  CompilationDiagnosticCollector();
  ~CompilationDiagnosticCollector() noexcept(false);
  CompilationDiagnosticCollector(CompilationDiagnosticCollector&&) noexcept;
  CompilationDiagnosticCollector& operator=(CompilationDiagnosticCollector&&) noexcept;
  ZC_DISALLOW_COPY(CompilationDiagnosticCollector);

  /// \brief Registers one source identity and its exact revision byte length.
  ZC_NODISCARD bool addSourceAuthority(const identity::SourceFileKey& source,
                                       uint64_t sourceByteLength);
  /// \brief Registers one exact non-source document identity and byte length.
  ZC_NODISCARD bool addDocumentAuthority(zc::ArrayPtr<const uint8_t> identity,
                                         uint64_t documentByteLength);
  /// \brief Atomically admits one fact root and its complete in-order provenance entries.
  ZC_NODISCARD bool addRoot(zc::ArrayPtr<const DiagnosticFact> facts,
                            zc::ArrayPtr<const SourceDiagnosticProvenanceEntry> provenance);
  /// \brief Admits one complete source-owned query result.
  ZC_NODISCARD bool addSourceRoot(const identity::SourceFileKey& source,
                                  zc::ArrayPtr<const DiagnosticFact> facts,
                                  const SourceDiagnosticProvenanceMap& provenance);
  ZC_NODISCARD bool hasErrors() const noexcept;
  /// \brief Returns the first collection contract failure, if one has occurred.
  ZC_NODISCARD zc::Maybe<DiagnosticCollectionFailure> failure() const noexcept;
  ZC_NODISCARD DiagnosticCollectionResult seal() &&;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Request-local collector that seals roots into one deterministic fact set.
class DiagnosticFactCollector final {
public:
  static constexpr size_t maximumFacts = 65536;

  DiagnosticFactCollector();
  ~DiagnosticFactCollector() noexcept(false);
  DiagnosticFactCollector(DiagnosticFactCollector&&) noexcept;
  DiagnosticFactCollector& operator=(DiagnosticFactCollector&&) noexcept;
  ZC_DISALLOW_COPY(DiagnosticFactCollector);

  ZC_NODISCARD bool addRoot(zc::ArrayPtr<const DiagnosticFact> facts);
  ZC_NODISCARD DiagnosticCollectionResult seal() &&;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace zomlang::compiler::diagnostics
