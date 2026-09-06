// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "compiler/diagnostics/fact/compilation-diagnostic-facts.h"
#include "compiler/identity/key/source-key.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler {
namespace identity {
class SourceFileKey;
}
namespace diagnostics {
class ResolvedDiagnosticBatch;

enum class DiagnosticMaterializationFailure : uint8_t {
  MissingProvenance = 0x01,
  ForeignSource = 0x02,
  OutOfRange = 0x03,
  RoleMismatch = 0x04,
  ArgumentMismatch = 0x05,
};

class DiagnosticProvenanceResolver {
public:
  virtual ~DiagnosticProvenanceResolver() noexcept(false) = default;
  ZC_DISALLOW_COPY(DiagnosticProvenanceResolver);

  ZC_NODISCARD virtual bool owns(const DiagnosticProvenanceKey& key) const noexcept = 0;
  ZC_NODISCARD virtual zc::Maybe<uint64_t> byteLength(
      const DiagnosticProvenanceKey& key) const noexcept = 0;
  ZC_NODISCARD virtual zc::Maybe<const DiagnosticSourceRange&> resolve(
      const DiagnosticProvenanceKey& key) const noexcept = 0;

protected:
  DiagnosticProvenanceResolver() = default;
  DiagnosticProvenanceResolver(DiagnosticProvenanceResolver&&) noexcept = default;
  DiagnosticProvenanceResolver& operator=(DiagnosticProvenanceResolver&&) noexcept = default;
};

class SourceDiagnosticProvenanceResolver final : public DiagnosticProvenanceResolver {
public:
  SourceDiagnosticProvenanceResolver(const identity::SourceFileKey& source,
                                     const SourceDiagnosticProvenanceMap& provenance) noexcept;
  ~SourceDiagnosticProvenanceResolver() noexcept(false) override;

  ZC_NODISCARD bool owns(const DiagnosticProvenanceKey& key) const noexcept override;
  ZC_NODISCARD zc::Maybe<uint64_t> byteLength(
      const DiagnosticProvenanceKey& key) const noexcept override;
  ZC_NODISCARD zc::Maybe<const DiagnosticSourceRange&> resolve(
      const DiagnosticProvenanceKey& key) const noexcept override;

private:
  const identity::SourceFileKey& source;
  const SourceDiagnosticProvenanceMap& provenance;
};

/// \brief One owned source provenance authority admitted to request-wide materialization.
struct DiagnosticSourceProvenance final {
  identity::SourceFileKey source;
  SourceDiagnosticProvenanceMap provenance;
};

/// \brief Request-wide resolver over complete provenance authorities for distinct sources.
class CompilationDiagnosticProvenanceResolver final : public DiagnosticProvenanceResolver {
public:
  ZC_NODISCARD static zc::Maybe<CompilationDiagnosticProvenanceResolver> from(
      zc::Vector<DiagnosticSourceProvenance>&& sources);
  ZC_NODISCARD static zc::Maybe<CompilationDiagnosticProvenanceResolver> from(
      const CompilationDiagnosticFacts& facts);
  ~CompilationDiagnosticProvenanceResolver() noexcept(false) override;
  CompilationDiagnosticProvenanceResolver(CompilationDiagnosticProvenanceResolver&&) noexcept;
  CompilationDiagnosticProvenanceResolver& operator=(
      CompilationDiagnosticProvenanceResolver&&) noexcept;
  ZC_DISALLOW_COPY(CompilationDiagnosticProvenanceResolver);

  ZC_NODISCARD bool owns(const DiagnosticProvenanceKey& key) const noexcept override;
  ZC_NODISCARD zc::Maybe<uint64_t> byteLength(
      const DiagnosticProvenanceKey& key) const noexcept override;
  ZC_NODISCARD zc::Maybe<const DiagnosticSourceRange&> resolve(
      const DiagnosticProvenanceKey& key) const noexcept override;

private:
  explicit CompilationDiagnosticProvenanceResolver(
      zc::Vector<DiagnosticSourceProvenance>&& sources) noexcept;

  zc::Vector<DiagnosticSourceProvenance> sources;
  zc::Vector<CompilationDiagnosticDocument> documents;
};

/// \brief One fully resolved source-qualified diagnostic location.
class ResolvedDiagnosticLocation final {
public:
  ResolvedDiagnosticLocation(ResolvedDiagnosticLocation&&) noexcept = default;
  ResolvedDiagnosticLocation& operator=(ResolvedDiagnosticLocation&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedDiagnosticLocation);
  ~ResolvedDiagnosticLocation() noexcept = default;

  ZC_NODISCARD DiagnosticFactOrigin origin() const noexcept;
  /// \pre `origin() != DiagnosticFactOrigin::Document`.
  ZC_NODISCARD const identity::SourceFileKey& source() const;
  /// \pre `origin() == DiagnosticFactOrigin::Document`.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> documentIdentityBytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const DiagnosticSourceRange& range() const noexcept { return rangeValue; }
  ZC_NODISCARD ResolvedDiagnosticLocation clone() const;

private:
  ResolvedDiagnosticLocation(identity::SourceFileKey&& source,
                             DiagnosticSourceRange range) noexcept;
  ResolvedDiagnosticLocation(zc::Array<uint8_t>&& document, DiagnosticSourceRange range) noexcept;

  zc::OneOf<identity::SourceFileKey, zc::Array<uint8_t>> identityValue;
  DiagnosticSourceRange rangeValue;

  friend struct ResolvedDiagnosticRelated;
  friend class ResolvedDiagnostic;
  friend zc::OneOf<ResolvedDiagnosticBatch, DiagnosticMaterializationFailure>
  materializeDiagnosticFacts(zc::ArrayPtr<const DiagnosticFact>,
                             const DiagnosticProvenanceResolver&);
};

struct ResolvedDiagnosticRelated final {
  DiagnosticSecondaryRole role;
  zc::Maybe<DiagID> code;
  ResolvedDiagnosticLocation location;
  zc::Array<zc::String> arguments;

  ZC_NODISCARD ResolvedDiagnosticRelated clone() const;
};

/// \brief Immutable diagnostic whose complete provenance has been resolved.
class ResolvedDiagnostic final {
public:
  ResolvedDiagnostic(ResolvedDiagnostic&&) noexcept = default;
  ResolvedDiagnostic& operator=(ResolvedDiagnostic&&) noexcept = default;
  ZC_DISALLOW_COPY(ResolvedDiagnostic);
  ~ResolvedDiagnostic() noexcept = default;

  ZC_NODISCARD DiagID code() const noexcept { return codeValue; }
  ZC_NODISCARD DiagSeverity severity() const noexcept;
  ZC_NODISCARD const ResolvedDiagnosticLocation& primary() const noexcept { return primaryValue; }
  ZC_NODISCARD zc::ArrayPtr<const zc::String> arguments() const ZC_LIFETIMEBOUND {
    return argumentValues.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const ResolvedDiagnosticRelated> related() const ZC_LIFETIMEBOUND {
    return relatedValues.asPtr();
  }
  ZC_NODISCARD ResolvedDiagnostic clone() const;

private:
  ResolvedDiagnostic(DiagID code, ResolvedDiagnosticLocation&& primary,
                     zc::Array<zc::String>&& arguments,
                     zc::Array<ResolvedDiagnosticRelated>&& related) noexcept;

  DiagID codeValue;
  ResolvedDiagnosticLocation primaryValue;
  zc::Array<zc::String> argumentValues;
  zc::Array<ResolvedDiagnosticRelated> relatedValues;

  friend class ResolvedDiagnosticBatch;
  friend zc::OneOf<ResolvedDiagnosticBatch, DiagnosticMaterializationFailure>
  materializeDiagnosticFacts(zc::ArrayPtr<const DiagnosticFact>,
                             const DiagnosticProvenanceResolver&);
};

/// \brief Complete immutable batch that cannot be partially published.
class ResolvedDiagnosticBatch final {
public:
  ~ResolvedDiagnosticBatch() noexcept(false);
  ResolvedDiagnosticBatch(ResolvedDiagnosticBatch&&) noexcept;
  ResolvedDiagnosticBatch& operator=(ResolvedDiagnosticBatch&&) noexcept;
  ZC_DISALLOW_COPY(ResolvedDiagnosticBatch);

  ZC_NODISCARD size_t size() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ResolvedDiagnostic> diagnostics() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD ResolvedDiagnosticBatch clone() const;

private:
  struct Impl;
  explicit ResolvedDiagnosticBatch(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend zc::OneOf<ResolvedDiagnosticBatch, DiagnosticMaterializationFailure>
  materializeDiagnosticFacts(zc::ArrayPtr<const DiagnosticFact>,
                             const DiagnosticProvenanceResolver&);
};

using DiagnosticMaterializationResult =
    zc::OneOf<ResolvedDiagnosticBatch, DiagnosticMaterializationFailure>;

ZC_NODISCARD DiagnosticMaterializationResult materializeDiagnosticFacts(
    zc::ArrayPtr<const DiagnosticFact> facts, const DiagnosticProvenanceResolver& resolver);

ZC_NODISCARD DiagnosticMaterializationResult materializeDiagnosticFacts(
    const CompilationDiagnosticFacts& facts, const DiagnosticProvenanceResolver& resolver);

}  // namespace diagnostics
}  // namespace zomlang::compiler
