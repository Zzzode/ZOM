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
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/loans.h"
#include "zomlang/compiler/ownership/facts/region-key.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One region-liveness membership: region R is live at point P.
struct RegionMembership final {
  RegionKey region;
  OwnershipPoint point;
};

/// \brief Untrusted region-membership inventory awaiting independent reconstruction.
class RegionMembershipCandidate final {
public:
  RegionMembershipCandidate(identity::SemanticContextBrand semanticContext,
                            identity::ContextFingerprint&& contextFingerprint,
                            identity::ModuleId module, mir::MirRevisionId builtRevision,
                            OwnershipEventOverlayRevision overlayRevision,
                            driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                            zc::Vector<RegionMembership>&& memberships) noexcept;
  RegionMembershipCandidate(RegionMembershipCandidate&&) noexcept = default;
  RegionMembershipCandidate& operator=(RegionMembershipCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(RegionMembershipCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<RegionMembership> memberships;
};

/// \brief Immutable region-membership inventory bound to one verified ownership input snapshot.
class VerifiedRegionMemberships final {
public:
  ~VerifiedRegionMemberships() noexcept(false);
  VerifiedRegionMemberships(VerifiedRegionMemberships&&) noexcept;
  VerifiedRegionMemberships& operator=(VerifiedRegionMemberships&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedRegionMemberships);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const RegionMembership> memberships() const noexcept;

private:
  struct Impl;
  explicit VerifiedRegionMemberships(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class RegionMembershipVerifier;
};

/// \brief Derives region liveness for the admitted ownership subset via forward dataflow.
///
/// The builder runs a forward dataflow over each verified FlowFunction. Regions
/// are seeded at their introduction events and propagated through flow edges
/// with union at joins. Loan regions are killed at the last use of their
/// destination. The admitted CFG subset is reducible and may carry loop back
/// edges, so the analysis converges by worklist fixpoint iteration rather than
/// a single topological pass.
class RegionMembershipBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<RegionMembershipCandidate> build(
      const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs region liveness from verified ownership inputs.
class RegionMembershipVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedRegionMemberships> verify(
      RegionMembershipCandidate&& candidate, const VerifiedFlow& flow,
      const VerifiedLoanFacts& loans, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
