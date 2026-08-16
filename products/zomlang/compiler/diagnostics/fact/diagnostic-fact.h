// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/diagnostics/core/diagnostic-ids.h"

namespace zomlang::compiler::identity {
class ModuleKey;
class SourceFileKey;
}  // namespace zomlang::compiler::identity

namespace zomlang::compiler::binder {
class StableBindingDiagnosticFactFactory;
class StableBindingDiagnosticFactCodecAccess;
}  // namespace zomlang::compiler::binder

namespace zomlang::compiler::diagnostics {

class DiagnosticFactCodecAccess;

enum class SourceDiagnosticPhase : uint8_t { Lex = 0x01, Parse = 0x02 };
enum class SourceDiagnosticEmitter : uint8_t { Lexer = 0x01, Parser = 0x02 };
enum class IdentityDiagnosticPhase : uint8_t { IdentityAdmission = 0x01 };
enum class IdentityDiagnosticEmitter : uint8_t {
  DuplicateBound = 0x01,
  DefinitionIdentityCollision = 0x02,
  ConstantExpressionNotAllowed = 0x03,
  DuplicateGenericParameter = 0x04
};
enum class BinderDiagnosticProducer : uint8_t { BindModuleSkeleton = 0x01, BindOwnerBody = 0x02 };
enum class BinderDiagnosticEmitter : uint8_t {
  Declaration = 0x01,
  Lookup = 0x02,
  ControlTransfer = 0x03,
  ContextualSelf = 0x04
};
enum class DiagnosticFactOrigin : uint8_t { Source = 0x01, Module = 0x02 };
enum class DiagnosticSecondaryRole : uint8_t {
  Highlight = 0x01,
  PreviousDeclaration = 0x01,
  Note = 0x02,
};

/// \brief Stable identity of one deterministic source diagnostic occurrence.
class DiagnosticOccurrenceKey final {
public:
  ~DiagnosticOccurrenceKey() noexcept(false);
  DiagnosticOccurrenceKey(DiagnosticOccurrenceKey&&) noexcept;
  DiagnosticOccurrenceKey& operator=(DiagnosticOccurrenceKey&&) noexcept;
  ZC_DISALLOW_COPY(DiagnosticOccurrenceKey);

  ZC_NODISCARD static zc::Maybe<DiagnosticOccurrenceKey> from(identity::SourceFileKey&& source,
                                                              SourceDiagnosticPhase phase,
                                                              SourceDiagnosticEmitter emitter,
                                                              uint32_t occurrence);
  /// \brief Admits one module-owned identity-admission occurrence.
  ZC_NODISCARD static zc::Maybe<DiagnosticOccurrenceKey> identityAdmission(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      zc::Vector<uint32_t>&& syntaxPath, IdentityDiagnosticEmitter emitter);
  ZC_NODISCARD DiagnosticOccurrenceKey clone() const;
  ZC_NODISCARD DiagnosticFactOrigin origin() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD SourceDiagnosticPhase phase() const noexcept;
  ZC_NODISCARD SourceDiagnosticEmitter emitter() const noexcept;
  ZC_NODISCARD uint32_t occurrence() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD bool isIdentityAdmission() const noexcept;
  ZC_NODISCARD IdentityDiagnosticPhase identityPhase() const noexcept;
  ZC_NODISCARD IdentityDiagnosticEmitter identityEmitter() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> identitySyntaxPath() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD bool isBinder() const noexcept;
  ZC_NODISCARD BinderDiagnosticProducer binderProducer() const noexcept;
  ZC_NODISCARD BinderDiagnosticEmitter binderEmitter() const noexcept;
  ZC_NODISCARD bool hasBinderSemanticOwner() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> binderSemanticOwnerBytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> binderSyntaxPath() const ZC_LIFETIMEBOUND;
  bool operator==(const DiagnosticOccurrenceKey& other) const noexcept;

private:
  ZC_NODISCARD static zc::Maybe<DiagnosticOccurrenceKey> binder(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      BinderDiagnosticProducer producer, zc::Maybe<zc::Array<uint8_t>>&& semanticOwner,
      BinderDiagnosticEmitter emitter, zc::Vector<uint32_t>&& syntaxPath);
  struct Impl;
  explicit DiagnosticOccurrenceKey(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class binder::StableBindingDiagnosticFactFactory;
  friend class binder::StableBindingDiagnosticFactCodecAccess;
  friend class DiagnosticFactCodecAccess;
};

/// \brief Stable source provenance key resolved only against its retained source map.
class DiagnosticProvenanceKey final {
public:
  ~DiagnosticProvenanceKey() noexcept(false);
  DiagnosticProvenanceKey(DiagnosticProvenanceKey&&) noexcept;
  DiagnosticProvenanceKey& operator=(DiagnosticProvenanceKey&&) noexcept;
  ZC_DISALLOW_COPY(DiagnosticProvenanceKey);

  ZC_NODISCARD static zc::Maybe<DiagnosticProvenanceKey> from(
      identity::SourceFileKey&& source, SourceDiagnosticPhase phase,
      SourceDiagnosticEmitter emitter, zc::Vector<uint32_t>&& occurrencePath);
  /// \brief Admits one module-owned identity syntax provenance key.
  ZC_NODISCARD static zc::Maybe<DiagnosticProvenanceKey> identitySyntaxSite(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      zc::Vector<uint32_t>&& syntaxPath);
  ZC_NODISCARD DiagnosticProvenanceKey clone() const;
  ZC_NODISCARD DiagnosticFactOrigin origin() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD SourceDiagnosticPhase phase() const noexcept;
  ZC_NODISCARD SourceDiagnosticEmitter emitter() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> occurrencePath() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> identitySyntaxPath() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD bool isIdentitySyntaxSite() const noexcept;
  ZC_NODISCARD bool isBinderModuleSite() const noexcept;
  ZC_NODISCARD BinderDiagnosticEmitter binderEmitter() const noexcept;
  ZC_NODISCARD bool hasBinderSemanticOwner() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> binderSemanticOwnerBytes() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const uint32_t> binderSyntaxPath() const ZC_LIFETIMEBOUND;
  bool operator==(const DiagnosticProvenanceKey& other) const noexcept;

private:
  ZC_NODISCARD static zc::Maybe<DiagnosticProvenanceKey> binderModuleSite(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      zc::Maybe<zc::Array<uint8_t>>&& semanticOwner, BinderDiagnosticEmitter emitter,
      zc::Vector<uint32_t>&& syntaxPath);
  struct Impl;
  explicit DiagnosticProvenanceKey(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class binder::StableBindingDiagnosticFactFactory;
  friend class binder::StableBindingDiagnosticFactCodecAccess;
  friend class DiagnosticFactCodecAccess;
};

/// \brief One canonical source diagnostic secondary record.
class DiagnosticSecondary final {
public:
  ~DiagnosticSecondary() noexcept(false);
  DiagnosticSecondary(DiagnosticSecondary&&) noexcept;
  DiagnosticSecondary& operator=(DiagnosticSecondary&&) noexcept;
  ZC_DISALLOW_COPY(DiagnosticSecondary);

  ZC_NODISCARD static zc::Maybe<DiagnosticSecondary> highlight(
      DiagnosticProvenanceKey&& provenance);
  ZC_NODISCARD static zc::Maybe<DiagnosticSecondary> note(DiagID code,
                                                          DiagnosticProvenanceKey&& provenance,
                                                          zc::Vector<zc::String>&& arguments);
  ZC_NODISCARD static zc::Maybe<DiagnosticSecondary> previousDeclaration(
      DiagID code, DiagnosticProvenanceKey&& provenance);
  ZC_NODISCARD DiagnosticSecondary clone() const;
  ZC_NODISCARD DiagnosticSecondaryRole role() const noexcept;
  ZC_NODISCARD zc::Maybe<DiagID> code() const noexcept;
  ZC_NODISCARD const DiagnosticProvenanceKey& provenance() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const zc::String> arguments() const ZC_LIFETIMEBOUND;
  bool operator==(const DiagnosticSecondary& other) const noexcept;

private:
  struct Impl;
  explicit DiagnosticSecondary(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class binder::StableBindingDiagnosticFactFactory;
};

/// \brief One canonical query-safe source diagnostic fact.
struct DiagnosticFact final {
public:
  ~DiagnosticFact() noexcept(false);
  DiagnosticFact(DiagnosticFact&&) noexcept;
  DiagnosticFact& operator=(DiagnosticFact&&) noexcept;
  ZC_DISALLOW_COPY(DiagnosticFact);

  ZC_NODISCARD static zc::Maybe<DiagnosticFact> from(DiagnosticOccurrenceKey&& occurrence,
                                                     DiagID code,
                                                     zc::Vector<zc::String>&& arguments,
                                                     DiagnosticProvenanceKey&& primary,
                                                     zc::Vector<DiagnosticSecondary>&& secondary);
  ZC_NODISCARD DiagnosticFact clone() const;
  ZC_NODISCARD const DiagnosticOccurrenceKey& occurrence() const noexcept;
  ZC_NODISCARD DiagID code() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const zc::String> arguments() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const DiagnosticProvenanceKey& primary() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DiagnosticSecondary> secondary() const ZC_LIFETIMEBOUND;
  bool operator==(const DiagnosticFact& other) const noexcept;

private:
  struct Impl;
  explicit DiagnosticFact(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

struct DiagnosticSourceRange final {
  uint64_t byteStart = 0;
  uint64_t byteEnd = 0;
  bool isTokenRange = false;

  bool operator==(const DiagnosticSourceRange& other) const noexcept = default;
};

struct SourceDiagnosticProvenanceEntry final {
  DiagnosticProvenanceKey key;
  DiagnosticSourceRange range;

  ZC_NODISCARD SourceDiagnosticProvenanceEntry clone() const;
  bool operator==(const SourceDiagnosticProvenanceEntry& other) const noexcept;
};

/// \brief Complete revision-local source provenance authority for one fact sequence.
class SourceDiagnosticProvenanceMap final {
public:
  ~SourceDiagnosticProvenanceMap() noexcept(false);
  SourceDiagnosticProvenanceMap(SourceDiagnosticProvenanceMap&&) noexcept;
  SourceDiagnosticProvenanceMap& operator=(SourceDiagnosticProvenanceMap&&) noexcept;
  ZC_DISALLOW_COPY(SourceDiagnosticProvenanceMap);

  ZC_NODISCARD static zc::Maybe<SourceDiagnosticProvenanceMap> from(
      zc::Vector<SourceDiagnosticProvenanceEntry>&& entries, uint64_t sourceByteLength);
  ZC_NODISCARD SourceDiagnosticProvenanceMap clone() const;
  ZC_NODISCARD zc::ArrayPtr<const SourceDiagnosticProvenanceEntry> entries() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD uint64_t sourceByteLength() const noexcept;
  ZC_NODISCARD zc::Maybe<const DiagnosticSourceRange&> find(
      const DiagnosticProvenanceKey& key) const noexcept;
  bool operator==(const SourceDiagnosticProvenanceMap& other) const noexcept;

private:
  struct Impl;
  explicit SourceDiagnosticProvenanceMap(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Explicit fact-sequence limits shared by encoding and decoding.
struct DiagnosticFactCodecLimits final {
  uint64_t maximumFacts;
  uint64_t maximumEncodedBytes;
  uint64_t maximumProvenanceComponentsPerKey;
  uint64_t maximumArgumentBytesPerRecord;
  uint64_t maximumSecondaryPerFact;
};

/// \brief Explicit source-provenance limits shared by encoding and decoding.
struct DiagnosticProvenanceCodecLimits final {
  uint64_t maximumEntries;
  uint64_t maximumEncodedBytes;
  uint64_t maximumProvenanceComponentsPerKey;
  uint64_t maximumSourceByteOffset;
};

ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeDiagnosticFacts(
    zc::Maybe<zc::MemoryResource&> outputResource, zc::ArrayPtr<const DiagnosticFact> facts,
    DiagnosticFactCodecLimits limits);
ZC_NODISCARD zc::Maybe<zc::Vector<DiagnosticFact>> decodeDiagnosticFacts(
    zc::Maybe<zc::MemoryResource&> resultResource, zc::ArrayPtr<const uint8_t> encoded,
    DiagnosticFactCodecLimits limits);
ZC_NODISCARD zc::Maybe<zc::Array<uint8_t>> encodeSourceDiagnosticProvenance(
    zc::Maybe<zc::MemoryResource&> outputResource, const SourceDiagnosticProvenanceMap& provenance,
    DiagnosticProvenanceCodecLimits limits);
ZC_NODISCARD zc::Maybe<SourceDiagnosticProvenanceMap> decodeSourceDiagnosticProvenance(
    zc::Maybe<zc::MemoryResource&> resultResource, zc::ArrayPtr<const uint8_t> encoded,
    uint64_t sourceByteLength, DiagnosticProvenanceCodecLimits limits);
ZC_NODISCARD bool validateDiagnosticProvenance(
    zc::ArrayPtr<const DiagnosticFact> facts,
    const SourceDiagnosticProvenanceMap& provenance) noexcept;
ZC_NODISCARD bool isSourceSyntaxDiagnostic(DiagID code) noexcept;

}  // namespace zomlang::compiler::diagnostics
