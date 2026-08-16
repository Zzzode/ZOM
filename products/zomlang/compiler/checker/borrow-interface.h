// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/identity/key/definition-key.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::checker::borrow {

enum class BorrowShape : uint8_t {
  NoRegion = 0x01,
  DirectRootRegion = 0x02,
  NestedRegion = 0x03,
  ParametricRegion = 0x04,
  OpaqueRegion = 0x05
};

enum class BorrowInputRegionTag : uint8_t { Receiver = 0x01, Parameter = 0x02 };

/// \brief Canonical direct input region in one callable signature.
class BorrowInputRegion final {
public:
  ZC_NODISCARD static BorrowInputRegion receiver() noexcept;
  ZC_NODISCARD static BorrowInputRegion parameter(uint32_t index) noexcept;
  ZC_NODISCARD BorrowInputRegionTag tag() const noexcept;
  ZC_NODISCARD uint32_t parameterIndex() const noexcept;
  bool operator==(const BorrowInputRegion& other) const noexcept;

private:
  BorrowInputRegion(BorrowInputRegionTag tag, uint32_t parameterIndex) noexcept;
  BorrowInputRegionTag tagValue;
  uint32_t parameterIndexValue;
};

enum class BorrowReturnRelationTag : uint8_t { None = 0x01, DirectRoot = 0x02 };

/// \brief Canonical return-to-input region relation.
class BorrowReturnRelation final {
public:
  ZC_NODISCARD static BorrowReturnRelation none() noexcept;
  ZC_NODISCARD static BorrowReturnRelation directRoot(BorrowInputRegion source) noexcept;
  ZC_NODISCARD BorrowReturnRelationTag tag() const noexcept;
  ZC_NODISCARD const BorrowInputRegion& source() const noexcept;

private:
  BorrowReturnRelation(BorrowReturnRelationTag tag, BorrowInputRegion source) noexcept;
  BorrowReturnRelationTag tagValue;
  BorrowInputRegion sourceValue;
};

struct BorrowSignatureSummary final {
  identity::DefId callable;
  zc::Vector<BorrowInputRegion> directInputs;
  BorrowReturnRelation returnRelation;

  ZC_NODISCARD BorrowSignatureSummary clone() const;
};

/// \brief Canonical RFC 0013 borrow-summary codec.
class BorrowSignatureCanonicalCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeFramed(
      const BorrowSignatureSummary& summary, zc::ArrayPtr<const uint8_t> expandedCallableKey);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encode(
      const BorrowSignatureSummary& summary, const CheckerIdentityAuthority& identities);
};

/// \brief Domain-separated revision of one verified borrow-interface surface.
class BorrowInterfaceRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<BorrowInterfaceRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& signatureFactsRevision,
      const identity::Sha256Digest& importedSignatureViewRevision,
      zc::ArrayPtr<const zc::ArrayPtr<const uint8_t>> summaryRecords);

private:
  explicit BorrowInterfaceRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
};

enum class BorrowSignatureFailureKind : uint8_t {
  AmbiguousDirectResult = 0x01,
  UnexpressibleResult = 0x02,
  UnverifiedExternContract = 0x03
};

struct BorrowSignatureFailure final {
  BorrowSignatureFailureKind kind;
  identity::DefId callable;
  identity::SourceSpan primarySpan;
  identity::SourceSpan declarationSpan;
  uint32_t traversalOrdinal;

  ZC_NODISCARD BorrowSignatureFailure clone() const;
};

class VerifiedBorrowInterfaceSurface final {
public:
  ~VerifiedBorrowInterfaceSurface() noexcept(false);
  VerifiedBorrowInterfaceSurface(VerifiedBorrowInterfaceSurface&&) noexcept;
  VerifiedBorrowInterfaceSurface& operator=(VerifiedBorrowInterfaceSurface&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBorrowInterfaceSurface);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const signature::SignatureFactsRevision& signatureFactsRevision() const noexcept;
  ZC_NODISCARD const cross_module::ImportedSignatureViewRevision& importedSignatureViewRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const BorrowSignatureSummary> summaries() const noexcept;
  ZC_NODISCARD const BorrowInterfaceRevision& revision() const noexcept;
  /// \brief Produces a complete immutable copy for verified evidence retention.
  ZC_NODISCARD VerifiedBorrowInterfaceSurface clone() const;

private:
  struct Impl;
  explicit VerifiedBorrowInterfaceSurface(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class BorrowInterfaceBuilder;
};

struct BorrowInterfaceSourceRejected final {
  zc::Vector<BorrowSignatureFailure> failures;
};

struct BorrowInterfaceInvariantRejected final {
  zc::Vector<signature::CheckerVerificationFailure> failures;
};

struct BorrowInterfaceBuildInput final {
  identity::SemanticContextBrand semanticContext;
  const identity::ContextFingerprint& contextFingerprint;
  identity::ModuleId module;
  const signature::SignatureFactsRevision& signatureFactsRevision;
  const cross_module::ImportedSignatureViewRevision& importedSignatureViewRevision;
  zc::ArrayPtr<const signature::SemanticSignature> definitions;
  zc::ArrayPtr<const signature::SemanticSignature> supportDefinitions;
  const CheckerIdentityAuthority& identities;
  const type::SemanticTypeStore& semanticTypes;
};

using BorrowInterfaceBuildResult =
    zc::OneOf<VerifiedBorrowInterfaceSurface, BorrowInterfaceSourceRejected,
              BorrowInterfaceInvariantRejected>;

/// \brief Construct the complete canonical borrow surface for one authorized signature bundle.
class BorrowInterfaceBuilder final {
public:
  ZC_NODISCARD static BorrowInterfaceBuildResult build(const BorrowInterfaceBuildInput& input);
};

}  // namespace zomlang::compiler::checker::borrow
