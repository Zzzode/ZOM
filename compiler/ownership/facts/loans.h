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

#include <cstdint>

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "compiler/ownership/facts/paths.h"
#include "compiler/ownership/facts/points.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One issued reference loan derived from a verified MIR borrow creation.
struct LoanFact final {
  identity::DefId owner;
  MirEventKey issue;
  MirEventKey commit;
  mir::MirBorrowKind kind;
  OwnershipPoint activeFrom;
  MovePathKey source;
  MovePathKey destination;
};

/// \brief Untrusted current-subset loan inventory awaiting independent reconstruction.
class LoanCandidate final {
public:
  LoanCandidate(identity::SemanticContextBrand semanticContext,
                identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
                mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
                driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                zc::Vector<LoanFact>&& loans) noexcept;
  LoanCandidate(LoanCandidate&&) noexcept = default;
  LoanCandidate& operator=(LoanCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(LoanCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<LoanFact> loans;
};

/// \brief Immutable loan inventory bound to exact MIR, overlay, and borrow evidence revisions.
class VerifiedLoanFacts final {
public:
  ~VerifiedLoanFacts() noexcept(false);
  VerifiedLoanFacts(VerifiedLoanFacts&&) noexcept;
  VerifiedLoanFacts& operator=(VerifiedLoanFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedLoanFacts);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const LoanFact> loans() const noexcept;

private:
  struct Impl;
  explicit VerifiedLoanFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class LoanVerifier;
};

/// \brief Derives one loan fact for every currently admitted MIR borrow creation.
class LoanBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<LoanCandidate> build(
      const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs the loan inventory from MIR and ownership events.
class LoanVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedLoanFacts> verify(
      LoanCandidate&& candidate, const VerifiedMovePaths& movePaths,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
