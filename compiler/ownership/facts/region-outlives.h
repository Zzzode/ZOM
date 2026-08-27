// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "compiler/ownership/facts/region-key.h"
#include "compiler/ownership/facts/region-membership.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One region outlives relation: `from` outlives `to`.
///
/// The orientation is exact: `from` outlives `to` when every ownership point
/// carrying `to` also carries `from`, i.e. the live-point set of `to` is a
/// subset of the live-point set of `from`.
struct RegionOutlivesFact final {
  RegionKey from;
  RegionKey to;
};

/// \brief Untrusted region-outlives inventory awaiting independent reconstruction.
class RegionOutlivesCandidate final {
public:
  RegionOutlivesCandidate(identity::SemanticContextBrand semanticContext,
                          identity::ContextFingerprint&& contextFingerprint,
                          identity::ModuleId module, mir::MirRevisionId builtRevision,
                          OwnershipEventOverlayRevision overlayRevision,
                          driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                          zc::Vector<RegionOutlivesFact>&& outlives) noexcept;
  RegionOutlivesCandidate(RegionOutlivesCandidate&&) noexcept = default;
  RegionOutlivesCandidate& operator=(RegionOutlivesCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(RegionOutlivesCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<RegionOutlivesFact> outlives;
};

/// \brief Immutable region-outlives inventory bound to one verified ownership input snapshot.
class VerifiedRegionOutlives final {
public:
  ~VerifiedRegionOutlives() noexcept(false);
  VerifiedRegionOutlives(VerifiedRegionOutlives&&) noexcept;
  VerifiedRegionOutlives& operator=(VerifiedRegionOutlives&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedRegionOutlives);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const RegionOutlivesFact> outlives() const noexcept;

private:
  struct Impl;
  explicit VerifiedRegionOutlives(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class RegionOutlivesVerifier;
};

/// \brief Derives the region outlives relation from verified region memberships.
///
/// The relation is the primitive subset relation over live-point sets: for
/// every distinct pair of regions (R1, R2) present in the membership inventory,
/// R1 outlives R2 when the points carrying R2 are a subset of the points
/// carrying R1. The admitted subset therefore produces Input-outlives-Loan
/// edges (inputs are live from entry, loans only from their borrow issue) and a
/// Static region live at every point outlives every other region.
class RegionOutlivesBuilder final {
public:
  /// \brief Derives the primitive outlives relation from membership rows.
  ///
  /// Pure function over the membership inventory: R1 outlives R2 when every
  /// point carrying R2 also carries R1. Exposed directly so the subset
  /// semantics can be unit-tested for region kinds the production membership
  /// dataflow does not seed yet (for example Static regions).
  /// \param memberships The region-liveness membership inventory.
  /// \return The canonically sorted outlives facts.
  ZC_NODISCARD static zc::Vector<RegionOutlivesFact> derive(
      zc::ArrayPtr<const RegionMembership> memberships);

  ZC_NODISCARD static ir::IrOperationResult<RegionOutlivesCandidate> build(
      const VerifiedRegionMemberships& memberships, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs the region outlives relation from verified memberships.
class RegionOutlivesVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedRegionOutlives> verify(
      RegionOutlivesCandidate&& candidate, const VerifiedRegionMemberships& memberships,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
