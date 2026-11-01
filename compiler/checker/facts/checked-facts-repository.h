// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/checker/inference/checked-facts.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::checker::checked {

/// \brief Canonical append-only repository key for one checked module publication.
struct CheckedEvidenceKey final {
  identity::ModuleId module;
  CheckedFactsRevision revision;
};

/// \brief Opaque session-bound authority for later IR access to checked evidence.
class CheckedEvidenceLease final {
public:
  CheckedEvidenceLease(CheckedEvidenceLease&&) noexcept = default;
  CheckedEvidenceLease& operator=(CheckedEvidenceLease&&) noexcept = default;
  ZC_DISALLOW_COPY(CheckedEvidenceLease);

  ZC_NODISCARD identity::ModuleId module() const noexcept { return key.module; }
  ZC_NODISCARD const CheckedFactsRevision& revision() const noexcept { return key.revision; }

private:
  CheckedEvidenceLease(CheckedEvidenceKey&& key, identity::SemanticContextBrand session,
                       identity::RegistryBrand substitutionIssuer,
                       identity::RegistryBrand witnessIssuer) noexcept
      : key(zc::mv(key)),
        session(session),
        substitutionIssuer(substitutionIssuer),
        witnessIssuer(witnessIssuer) {}

  CheckedEvidenceKey key;
  identity::SemanticContextBrand session;
  identity::RegistryBrand substitutionIssuer;
  identity::RegistryBrand witnessIssuer;
  friend class CheckedFactsRepository;
};

enum class CheckedFactsRepositoryIssue : uint8_t {
  ForeignContext = 0x01,
  DuplicateEvidence = 0x02,
  InvalidLease = 0x03
};

using CheckedFactsAdoptionResult = zc::OneOf<CheckedEvidenceLease, CheckedFactsRepositoryIssue>;

/// \brief Session-owned append-only checked evidence and handle lifetime root.
class CheckedFactsRepository final {
public:
  explicit CheckedFactsRepository(identity::SemanticContextBrand semanticContext);
  ~CheckedFactsRepository() noexcept(false);
  CheckedFactsRepository(CheckedFactsRepository&&) noexcept;
  CheckedFactsRepository& operator=(CheckedFactsRepository&&) noexcept;
  ZC_DISALLOW_COPY(CheckedFactsRepository);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD size_t size() const noexcept;
  ZC_NODISCARD CheckedFactsAdoptionResult adopt(VerifiedCheckedFacts&& facts);
  ZC_NODISCARD zc::Maybe<CheckedEvidenceLease> lease(identity::ModuleId module,
                                                     const CheckedFactsRevision& revision) const;
  ZC_NODISCARD zc::Maybe<const VerifiedCheckedFacts&> lookup(
      const CheckedEvidenceLease& lease) const noexcept;
  ZC_NODISCARD zc::Maybe<const SubstitutionData&> substitution(
      const CheckedEvidenceLease& lease, CanonicalSubstitutionId id) const noexcept;
  ZC_NODISCARD zc::Maybe<const WitnessArgumentsData&> witnesses(
      const CheckedEvidenceLease& lease, WitnessArgumentsId id) const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> substitutionRecord(
      const CheckedEvidenceLease& lease, CanonicalSubstitutionId id) const noexcept;
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> witnessRecord(
      const CheckedEvidenceLease& lease, WitnessArgumentsId id) const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Retaining read-only view over one module's frozen substitution and
/// witness canonical records.
///
/// MIR construction and the monomorphization stage consume canonical record
/// bytes verbatim through this view; they never re-encode evidence. The view is
/// non-owning and valid only while the supplied repository and lease stay live
/// (one checked publication / one lowering call).
class CheckedFactsRecordView final {
public:
  CheckedFactsRecordView(const CheckedFactsRepository& repository,
                         const CheckedEvidenceLease& lease) noexcept
      : repository(repository), leaseValue(lease) {}
  CheckedFactsRecordView(const CheckedFactsRecordView&) = default;
  CheckedFactsRecordView& operator=(const CheckedFactsRecordView&) = delete;

  ZC_NODISCARD const CheckedEvidenceLease& lease() const noexcept { return leaseValue; }
  ZC_NODISCARD zc::Maybe<const SubstitutionData&> substitution(
      CanonicalSubstitutionId id) const noexcept {
    return repository.substitution(leaseValue, id);
  }
  ZC_NODISCARD zc::Maybe<const WitnessArgumentsData&> witnesses(
      WitnessArgumentsId id) const noexcept {
    return repository.witnesses(leaseValue, id);
  }
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> substitutionRecord(
      CanonicalSubstitutionId id) const noexcept {
    return repository.substitutionRecord(leaseValue, id);
  }
  ZC_NODISCARD zc::Maybe<zc::ArrayPtr<const uint8_t>> witnessRecord(
      WitnessArgumentsId id) const noexcept {
    return repository.witnessRecord(leaseValue, id);
  }

private:
  const CheckedFactsRepository& repository;
  const CheckedEvidenceLease& leaseValue;
};

}  // namespace zomlang::compiler::checker::checked
