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
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/ownership/facts/loans.h"
#include "compiler/ownership/facts/paths.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Parameter-reborrow origin detail carrying the root parameter ordinal.
struct ParameterReferenceOrigin final {
  uint32_t rootParameter;

  bool operator==(const ParameterReferenceOrigin&) const = default;
};

/// \brief Local-borrow origin detail for a borrow of a function-local binding.
struct LocalReferenceOrigin final {
  bool operator==(const LocalReferenceOrigin&) const = default;
};

/// \brief The origin (parameter entry or local introduction) behind one reference definition.
struct ReferenceInputOrigin final {
  MirEventKey entry;
  OwnershipPoint activation;
  zc::OneOf<ParameterReferenceOrigin, LocalReferenceOrigin> detail;
  MovePathKey referent;

  ZC_NODISCARD ReferenceInputOrigin clone() const {
    return ReferenceInputOrigin{entry, activation, detail,
                                MovePathKey{referent.owner, referent.place.clone()}};
  }
};

/// \brief Exact current-subset ownership points for one reference value's liveness.
struct ReferenceLivePoints final {
  OwnershipPoint afterCommit;
  OwnershipPoint afterCommitCfg;
  OwnershipPoint beforeReturnCfg;
  OwnershipPoint beforeReturn;
  OwnershipPoint afterReturn;
};

/// \brief One current-subset reference value installed at a verified move path.
struct ReferenceDefinition final {
  identity::DefId owner;
  MirEventKey introduction;
  MirEventKey loan;
  ReferenceInputOrigin origin;
  MirEventKey returned;
  MovePathKey destination;
  ReferenceLivePoints livePoints;
};

/// \brief Untrusted reference-definition inventory awaiting independent reconstruction.
class ReferenceDefinitionCandidate final {
public:
  ReferenceDefinitionCandidate(
      identity::SemanticContextBrand semanticContext,
      identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
      mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
      driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
      zc::Vector<ReferenceDefinition>&& definitions) noexcept;
  ReferenceDefinitionCandidate(ReferenceDefinitionCandidate&&) noexcept = default;
  ReferenceDefinitionCandidate& operator=(ReferenceDefinitionCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(ReferenceDefinitionCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<ReferenceDefinition> definitions;
};

/// \brief Immutable reference definitions bound to one verified ownership input snapshot.
class VerifiedReferenceDefinitions final {
public:
  ~VerifiedReferenceDefinitions() noexcept(false);
  VerifiedReferenceDefinitions(VerifiedReferenceDefinitions&&) noexcept;
  VerifiedReferenceDefinitions& operator=(VerifiedReferenceDefinitions&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedReferenceDefinitions);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ReferenceDefinition> definitions() const noexcept;

private:
  struct Impl;
  explicit VerifiedReferenceDefinitions(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ReferenceDefinitionVerifier;
};

/// \brief Derives reference definitions from verified immediate loan commits.
class ReferenceDefinitionBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<ReferenceDefinitionCandidate> build(
      const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently validates and publishes the current reference-definition inventory.
class ReferenceDefinitionVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedReferenceDefinitions> verify(
      ReferenceDefinitionCandidate&& candidate, const VerifiedMovePaths& movePaths,
      const VerifiedLoanFacts& loans, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
