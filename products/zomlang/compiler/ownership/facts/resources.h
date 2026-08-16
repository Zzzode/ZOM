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
#include "zomlang/compiler/ownership/facts/paths.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Canonical identity of one current-subset ownership-resource generation.
struct DropResourceSubject final {
  MirEventKey introduction;
  MovePathKey origin;
  identity::SemanticTypeId originType;
};

/// \brief One checker-authorized logical resource component rooted at an initialization event.
enum class DropRequirement : uint8_t {
  Logical = 0x01,
  Linear = 0x02,
  LinearLogical = 0x03,
};

struct OwnershipResourceFact final {
  DropResourceSubject subject;
  DropRequirement requirement;
  zc::Maybe<LogicalDropAction> dropAction;
  uint32_t declarationOrdinal;
};

/// \brief One ownership-preserving move of a current-subset resource component.
struct DropTransfer final {
  MovePathKey from;
  MovePathKey to;
  MirEventKey event;
};

/// \brief Complete logical resource inventory for one current-subset MIR function.
struct OwnershipResourceFunction final {
  identity::DefId owner;
  zc::Vector<OwnershipResourceFact> facts;
  zc::Vector<DropTransfer> transfers;
};

/// \brief Untrusted logical resource inventory awaiting independent reconstruction.
class OwnershipResourceCandidate final {
public:
  OwnershipResourceCandidate(identity::SemanticContextBrand semanticContext,
                             identity::SemanticContextFingerprint&& contextFingerprint,
                             identity::ModuleId module, mir::MirRevisionId builtRevision,
                             OwnershipEventOverlayRevision overlayRevision,
                             zc::Vector<OwnershipResourceFunction>&& functions) noexcept;
  OwnershipResourceCandidate(OwnershipResourceCandidate&&) noexcept = default;
  OwnershipResourceCandidate& operator=(OwnershipResourceCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(OwnershipResourceCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::SemanticContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  zc::Vector<OwnershipResourceFunction> functions;
};

/// \brief Immutable logical resource facts bound to one Built MIR and event overlay.
class VerifiedOwnershipResourceFacts final {
public:
  ~VerifiedOwnershipResourceFacts() noexcept(false);
  VerifiedOwnershipResourceFacts(VerifiedOwnershipResourceFacts&&) noexcept;
  VerifiedOwnershipResourceFacts& operator=(VerifiedOwnershipResourceFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedOwnershipResourceFacts);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const OwnershipResourceFunction> functions() const noexcept;

private:
  struct Impl;
  explicit VerifiedOwnershipResourceFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class OwnershipResourceVerifier;
};

/// \brief Projects checker-authorized logical resources into immutable ownership inputs.
class OwnershipResourceBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<OwnershipResourceCandidate> build(
      const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs logical resources from verified overlay and MIR inputs.
class OwnershipResourceVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedOwnershipResourceFacts> verify(
      OwnershipResourceCandidate&& candidate, const VerifiedMovePaths& movePaths,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
