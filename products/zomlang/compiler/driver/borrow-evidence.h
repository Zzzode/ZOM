// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <atomic>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/refcount.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/borrow-interface.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/driver/interface-source.h"
#include "zomlang/compiler/driver/module-interface.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ir/ir-failure.h"

namespace zomlang::compiler::hir {
class VerifiedHirModule;
}

namespace zomlang::compiler::mir {
class BuiltMirVerifier;
class VerifiedBuiltMir;
}  // namespace zomlang::compiler::mir

namespace zomlang::compiler::driver::borrow_evidence {

namespace detail {}  // namespace detail

/// \brief One imported-surface tuple in the RFC 0013 revision stream.
struct ImportedBorrowRevisionFrame final {
  zc::ArrayPtr<const uint8_t> expandedModuleKey;
  identity::Sha256Digest interfaceRevision;
  identity::Sha256Digest borrowRevision;
};

/// \brief One local-summary tuple whose key controls canonical map order.
struct LocalBorrowSummaryRevisionFrame final {
  zc::ArrayPtr<const uint8_t> expandedCallableKey;
  zc::ArrayPtr<const uint8_t> encodedSummary;
};

/// \brief Domain-separated revision of one complete verified borrow-evidence value.
class BorrowEvidenceRevision final {
public:
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  ZC_NODISCARD static zc::Maybe<BorrowEvidenceRevision> computeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& signatureFactsRevision,
      zc::ArrayPtr<const LocalBorrowSummaryRevisionFrame> localSummaries,
      const identity::Sha256Digest& ownInterfaceRevision,
      const identity::Sha256Digest& ownBorrowRevision,
      zc::ArrayPtr<const ImportedBorrowRevisionFrame> importedSurfaces);

private:
  explicit BorrowEvidenceRevision(const identity::Sha256Digest& value) noexcept;
  identity::Sha256Digest value;
  friend class BorrowEvidenceBuilder;
};

/// \brief Canonical RFC 0013 borrow-evidence framing codec.
class BorrowEvidenceCanonicalCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& signatureFactsRevision,
      zc::ArrayPtr<const LocalBorrowSummaryRevisionFrame> localSummaries,
      const identity::Sha256Digest& ownInterfaceRevision,
      const identity::Sha256Digest& ownBorrowRevision,
      zc::ArrayPtr<const ImportedBorrowRevisionFrame> importedSurfaces);
};

/// \brief Complete immutable verified borrow surface selected from one source interface.
class ImportedBorrowSurface final {
public:
  ImportedBorrowSurface(identity::ModuleId module,
                        module_interface::ModuleInterfaceRevision interfaceRevision,
                        checker::borrow::VerifiedBorrowInterfaceSurface&& surface);
  ~ImportedBorrowSurface() noexcept(false);
  ImportedBorrowSurface(ImportedBorrowSurface&&) noexcept;
  ImportedBorrowSurface& operator=(ImportedBorrowSurface&&) noexcept;
  ZC_DISALLOW_COPY(ImportedBorrowSurface);

  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const module_interface::ModuleInterfaceRevision& interfaceRevision() const noexcept;
  ZC_NODISCARD const checker::borrow::VerifiedBorrowInterfaceSurface& surface() const noexcept;
  ZC_NODISCARD ImportedBorrowSurface clone() const;

private:
  struct Impl;
  explicit ImportedBorrowSurface(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Untrusted candidate map entry with an independently checked canonical key.
struct ImportedBorrowSurfaceCandidate final {
  identity::ModuleId module;
  ImportedBorrowSurface surface;
};

/// \brief Untrusted mutable candidate consumed by the independent verifier.
struct BorrowEvidenceCandidate final {
  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  checker::signature::SignatureFactsRevision localSignatureFactsRevision;
  zc::Vector<checker::borrow::BorrowSignatureSummary> localSummaries;
  module_interface::ModuleInterfaceRevision ownInterfaceRevision;
  checker::borrow::BorrowInterfaceRevision ownBorrowRevision;
  zc::Vector<ImportedBorrowSurfaceCandidate> importedSurfaces;
  BorrowEvidenceRevision revision;
  zc::Array<uint8_t> canonicalRecord;
};

/// \brief Complete verified-only inputs for borrow-evidence construction and verification.
struct BorrowEvidenceBuildInput final {
  const checker::signature::VerifiedSignatureFacts& localSignatureFacts;
  const checker::cross_module::ImportedSignatureView& importedSignatures;
  const VerifiedModuleInterface& ownInterface;
  zc::ArrayPtr<const VerifiedInterfaceSource> availableInterfaces;
  const checker::CheckerIdentityAuthority& identities;
};

/// \brief One complete RFC 0010-aligned borrow-evidence invariant fact.
struct BorrowEvidenceInvariantFact final {
  ir::IrFailureKind kind;
  uint32_t traversalOrdinal;
  zc::Vector<uint32_t> structuralFieldPath;
};

/// \brief Deterministically ordered facts from the first rejected precedence stage.
struct BorrowEvidenceInvariantRejected final {
  zc::Vector<BorrowEvidenceInvariantFact> failures;
};

using BorrowEvidenceCandidateResult =
    zc::OneOf<BorrowEvidenceCandidate, BorrowEvidenceInvariantRejected>;

/// \brief Constructs an untrusted canonical candidate from verified frontend publications.
class BorrowEvidenceBuilder final {
public:
  ZC_NODISCARD static BorrowEvidenceCandidateResult build(const BorrowEvidenceBuildInput& input);
};

/// \brief Immutable complete RFC 0013 frontend borrow evidence.
class VerifiedBorrowEvidence final {
public:
  ~VerifiedBorrowEvidence() noexcept(false);
  VerifiedBorrowEvidence(VerifiedBorrowEvidence&&) noexcept;
  VerifiedBorrowEvidence& operator=(VerifiedBorrowEvidence&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBorrowEvidence);

  /// \brief Clones immutable verified evidence without cloning a repository lease.
  ZC_NODISCARD VerifiedBorrowEvidence clone() const;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const checker::signature::SignatureFactsRevision& localSignatureFactsRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const checker::borrow::BorrowSignatureSummary> localSummaries()
      const noexcept;
  ZC_NODISCARD const module_interface::ModuleInterfaceRevision& ownInterfaceRevision()
      const noexcept;
  ZC_NODISCARD const checker::borrow::BorrowInterfaceRevision& ownBorrowRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ImportedBorrowSurface> importedSurfaces() const noexcept;
  ZC_NODISCARD const BorrowEvidenceRevision& revision() const noexcept;

private:
  struct Impl;
  explicit VerifiedBorrowEvidence(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class BorrowEvidenceVerifier;
};

using BorrowEvidenceVerificationResult =
    zc::OneOf<VerifiedBorrowEvidence, BorrowEvidenceInvariantRejected>;

/// \brief Independently verifies an untrusted candidate against exact verified inputs.
class BorrowEvidenceVerifier final {
public:
  ZC_NODISCARD static BorrowEvidenceVerificationResult verify(
      BorrowEvidenceCandidate&& candidate, const BorrowEvidenceBuildInput& input);
};

struct BorrowEvidenceKey final {
  identity::ModuleId module;
  BorrowEvidenceRevision revision;
};

/// \brief Opaque capability for one exact repository-owned evidence value.
class VerifiedBorrowEvidenceLease final {
public:
  VerifiedBorrowEvidenceLease(VerifiedBorrowEvidenceLease&&) noexcept = default;
  VerifiedBorrowEvidenceLease& operator=(VerifiedBorrowEvidenceLease&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedBorrowEvidenceLease);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const BorrowEvidenceKey& key() const noexcept;

private:
  ZC_NODISCARD VerifiedBorrowEvidenceLease clone() const;
  ZC_NODISCARD bool matches(const VerifiedBorrowEvidenceLease& other) const noexcept;
  VerifiedBorrowEvidenceLease(identity::SemanticContextBrand semanticContext,
                              identity::RegistryBrand repository, BorrowEvidenceKey key) noexcept;

  identity::SemanticContextBrand context;
  identity::RegistryBrand repository;
  BorrowEvidenceKey evidenceKey;
  friend class BorrowEvidenceRepository;
  friend class mir::BuiltMirVerifier;
  friend class mir::VerifiedBuiltMir;
  friend class BorrowEvidenceRepositoryCapability;
};

struct BorrowEvidenceRepositoryRejected final {
  ir::IrFailureKind kind;
};

using BorrowEvidenceAdoptionResult =
    zc::OneOf<VerifiedBorrowEvidenceLease, BorrowEvidenceRepositoryRejected>;

namespace detail {

/// \brief Shared storage retained by a repository and every issued capability.
class BorrowEvidenceRepositoryState final : public zc::AtomicRefcounted {
public:
  struct Entry final {
    zc::Array<uint8_t> expandedModuleKey;
    VerifiedBorrowEvidence evidence;
  };

  BorrowEvidenceRepositoryState(identity::SemanticContextBrand context,
                                identity::RegistryBrand repository,
                                uint32_t expectedEntryCount) noexcept
      : context(context),
        repository(repository),
        expectedEntryCount(expectedEntryCount),
        entries(expectedEntryCount),
        sortedIndices(expectedEntryCount) {}
  ~BorrowEvidenceRepositoryState() noexcept(false) override = default;
  ZC_DISALLOW_COPY_AND_MOVE(BorrowEvidenceRepositoryState);

  ZC_NODISCARD bool isLive() const noexcept { return live.load(std::memory_order_acquire); }
  void invalidate() const noexcept { live.store(false, std::memory_order_release); }

  identity::SemanticContextBrand context;
  identity::RegistryBrand repository;
  uint32_t expectedEntryCount;
  mutable zc::Vector<Entry> entries;
  mutable zc::Vector<uint32_t> sortedIndices;

private:
  mutable std::atomic_bool live = true;
};

}  // namespace detail

class BorrowEvidenceRepository;

/// \brief Closed exact-lookup result that never exposes repository storage pointers.
class BorrowEvidenceLookupResult final {
public:
  ZC_NODISCARD bool isResolved() const noexcept;
  ZC_NODISCARD const VerifiedBorrowEvidence& evidence() const;
  ZC_NODISCARD ir::IrFailureKind rejectionKind() const noexcept;

private:
  explicit BorrowEvidenceLookupResult(const VerifiedBorrowEvidence& evidence) noexcept;
  explicit BorrowEvidenceLookupResult(ir::IrFailureKind rejection) noexcept;

  zc::Maybe<const VerifiedBorrowEvidence&> resolved;
  ir::IrFailureKind rejection = ir::IrFailureKind::InvalidFact;
  friend class BorrowEvidenceRepository;
  friend class BorrowEvidenceRepositoryCapability;
};

/// \brief Opaque authority required to resolve one repository-owned evidence lease.
class BorrowEvidenceRepositoryCapability final {
public:
  BorrowEvidenceRepositoryCapability(BorrowEvidenceRepositoryCapability&&) noexcept = default;
  BorrowEvidenceRepositoryCapability& operator=(BorrowEvidenceRepositoryCapability&&) = delete;
  ZC_DISALLOW_COPY(BorrowEvidenceRepositoryCapability);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD BorrowEvidenceLookupResult
  lookup(const VerifiedBorrowEvidenceLease& lease) const noexcept;

private:
  ZC_NODISCARD BorrowEvidenceRepositoryCapability clone() const noexcept;
  ZC_NODISCARD bool matches(const BorrowEvidenceRepositoryCapability& other) const noexcept;
  BorrowEvidenceRepositoryCapability(
      identity::SemanticContextBrand semanticContext, identity::RegistryBrand repository,
      zc::Arc<detail::BorrowEvidenceRepositoryState>&& state) noexcept;

  identity::SemanticContextBrand context;
  identity::RegistryBrand repository;
  zc::Arc<detail::BorrowEvidenceRepositoryState> state;
  friend class BorrowEvidenceRepository;
  friend class hir::VerifiedHirModule;
  friend class mir::VerifiedBuiltMir;
};

/// \brief Session-owned append-only authority for verified borrow evidence.
class BorrowEvidenceRepository final {
public:
  ~BorrowEvidenceRepository() noexcept(false);
  BorrowEvidenceRepository(BorrowEvidenceRepository&&) noexcept;
  BorrowEvidenceRepository& operator=(BorrowEvidenceRepository&&) noexcept;
  ZC_DISALLOW_COPY(BorrowEvidenceRepository);

  ZC_NODISCARD static zc::Maybe<BorrowEvidenceRepository> create(
      identity::SemanticContextBrand context, identity::RegistryBrand repositoryBrand,
      uint32_t expectedEntryCount);
  ZC_NODISCARD BorrowEvidenceAdoptionResult
  adopt(VerifiedBorrowEvidence&& evidence, const checker::CheckerIdentityAuthority& identities);
  ZC_NODISCARD BorrowEvidenceRepositoryCapability capability() const noexcept;
  ZC_NODISCARD zc::Maybe<VerifiedBorrowEvidenceLease> lease(
      identity::ModuleId module, const BorrowEvidenceRevision& revision) const noexcept;

private:
  explicit BorrowEvidenceRepository(
      zc::Arc<detail::BorrowEvidenceRepositoryState>&& state) noexcept;
  zc::Arc<detail::BorrowEvidenceRepositoryState> state;

  friend class BorrowEvidenceRepositoryCapability;
};

}  // namespace zomlang::compiler::driver::borrow_evidence
