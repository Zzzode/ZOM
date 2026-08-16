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
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/init.h"
#include "zomlang/compiler/ownership/facts/resources.h"
#include "zomlang/compiler/ownership/facts/states.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One atomic, provenance-checked input bundle for ownership analysis.
///
/// This bundle is not an ownership proof. It collects independently verified
/// current-subset inventories that share one Built MIR, event overlay, and
/// borrow-evidence lineage.
class VerifiedOwnershipInputs final {
public:
  ~VerifiedOwnershipInputs() noexcept(false);
  VerifiedOwnershipInputs(VerifiedOwnershipInputs&&) noexcept;
  VerifiedOwnershipInputs& operator=(VerifiedOwnershipInputs&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedOwnershipInputs);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD bool hasLiveBorrowEvidence() const noexcept;
  ZC_NODISCARD const VerifiedMovePaths& movePaths() const noexcept;
  ZC_NODISCARD const VerifiedFlow& flow() const noexcept;
  ZC_NODISCARD const VerifiedInitializationFacts& initialization() const noexcept;
  ZC_NODISCARD const VerifiedLoanFacts& loans() const noexcept;
  ZC_NODISCARD const VerifiedReferenceDefinitions& references() const noexcept;
  ZC_NODISCARD const VerifiedReborrowRegions& regions() const noexcept;
  ZC_NODISCARD const VerifiedReborrowStates& states() const noexcept;
  ZC_NODISCARD const VerifiedOwnershipResourceFacts& resources() const noexcept;

private:
  struct Impl;
  explicit VerifiedOwnershipInputs(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class OwnershipInputVerifier;
};

/// \brief Checks that verified ownership inputs form one coherent analysis snapshot.
class OwnershipInputVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedOwnershipInputs> verify(
      VerifiedMovePaths&& movePaths, VerifiedFlow&& flow,
      VerifiedInitializationFacts&& initialization, VerifiedLoanFacts&& loans,
      VerifiedReferenceDefinitions&& references, VerifiedReborrowRegions&& regions,
      VerifiedReborrowStates&& states, VerifiedOwnershipResourceFacts&& resources,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
      const driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease,
      const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability);
};

}  // namespace zomlang::compiler::ownership::facts
