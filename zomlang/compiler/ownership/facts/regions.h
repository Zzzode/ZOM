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
#include "zomlang/compiler/ownership/facts/refs.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One loan region derived for an admitted parameter reborrow or local borrow.
struct ReborrowRegion final {
  identity::DefId owner;
  MirEventKey entry;
  MirEventKey loan;
  zc::OneOf<ParameterReferenceOrigin, LocalReferenceOrigin> origin;
  zc::Vector<OwnershipPoint> members;
};

/// \brief Untrusted loan-region inventory awaiting reconstruction.
class ReborrowRegionCandidate final {
public:
  ReborrowRegionCandidate(identity::SemanticContextBrand semanticContext,
                          identity::ContextFingerprint&& contextFingerprint,
                          identity::ModuleId module, mir::MirRevisionId builtRevision,
                          OwnershipEventOverlayRevision overlayRevision,
                          driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                          zc::Vector<ReborrowRegion>&& regions) noexcept;
  ReborrowRegionCandidate(ReborrowRegionCandidate&&) noexcept = default;
  ReborrowRegionCandidate& operator=(ReborrowRegionCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(ReborrowRegionCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<ReborrowRegion> regions;
};

/// \brief Immutable bounded loan-region inventory for admitted reference origins.
class VerifiedReborrowRegions final {
public:
  ~VerifiedReborrowRegions() noexcept(false);
  VerifiedReborrowRegions(VerifiedReborrowRegions&&) noexcept;
  VerifiedReborrowRegions& operator=(VerifiedReborrowRegions&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedReborrowRegions);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ReborrowRegion> regions() const noexcept;

private:
  struct Impl;
  explicit VerifiedReborrowRegions(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ReborrowRegionVerifier;
};

/// \brief Derives bounded loan membership for the admitted reference-origin shape.
class ReborrowRegionBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<ReborrowRegionCandidate> build(
      const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
      const VerifiedReferenceDefinitions& references, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs bounded loan-region membership.
class ReborrowRegionVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedReborrowRegions> verify(
      ReborrowRegionCandidate&& candidate, const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
      const VerifiedReferenceDefinitions& references, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
