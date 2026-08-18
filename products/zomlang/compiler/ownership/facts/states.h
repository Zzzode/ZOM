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
#include "zomlang/compiler/ownership/facts/regions.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One reference value present at one exact point of an admitted reference origin.
struct ReborrowState final {
  identity::DefId owner;
  OwnershipPoint point;
  MirEventKey loan;
  zc::OneOf<ParameterReferenceOrigin, LocalReferenceOrigin> origin;
  MovePathKey destination;
};

/// \brief Untrusted reference-state inventory awaiting independent reconstruction.
class ReborrowStateCandidate final {
public:
  ReborrowStateCandidate(identity::SemanticContextBrand semanticContext,
                         identity::ContextFingerprint&& contextFingerprint,
                         identity::ModuleId module, mir::MirRevisionId builtRevision,
                         OwnershipEventOverlayRevision overlayRevision,
                         driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                         zc::Vector<ReborrowState>&& states) noexcept;
  ReborrowStateCandidate(ReborrowStateCandidate&&) noexcept = default;
  ReborrowStateCandidate& operator=(ReborrowStateCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(ReborrowStateCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<ReborrowState> states;
};

/// \brief Immutable bounded reference states for admitted reference origins.
class VerifiedReborrowStates final {
public:
  ~VerifiedReborrowStates() noexcept(false);
  VerifiedReborrowStates(VerifiedReborrowStates&&) noexcept;
  VerifiedReborrowStates& operator=(VerifiedReborrowStates&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedReborrowStates);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ReborrowState> states() const noexcept;

private:
  struct Impl;
  explicit VerifiedReborrowStates(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ReborrowStateVerifier;
};

/// \brief Derives point-exact reference values for the admitted reference-origin shape.
class ReborrowStateBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<ReborrowStateCandidate> build(
      const VerifiedReferenceDefinitions& references, const VerifiedReborrowRegions& regions,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs bounded reference states.
class ReborrowStateVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedReborrowStates> verify(
      ReborrowStateCandidate&& candidate, const VerifiedReferenceDefinitions& references,
      const VerifiedReborrowRegions& regions, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
