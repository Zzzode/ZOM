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
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership::facts {

/// \brief One canonical move-path key owned by a MIR function.
struct MovePathKey final {
  identity::DefId owner;
  mir::MirPlace place;
};

/// \brief One move path and its immediate projection prefix.
struct MovePathFact final {
  MovePathKey key;
  zc::Maybe<MovePathKey> parent;
};

/// \brief One distinct conflicting pair of move paths.
struct MovePathPair final {
  MovePathKey first;
  MovePathKey second;
};

/// \brief Complete current-subset move-path inventory for one MIR function.
struct MovePathFunction final {
  identity::DefId owner;
  zc::Vector<MovePathFact> facts;
  zc::Vector<MovePathPair> conflicts;
};

/// \brief Returns whether two move-path places potentially overlap.
///
/// Places rooted at different locals never conflict. Along the shared
/// projection prefix, identical projections extend the prefix; distinct
/// sibling fields and downcasts to distinct variants are disjoint; subslices
/// are disjoint exactly when their half-open ranges are disjoint; every other
/// projection pair (indices, dereferences, and all mixed-kind pairs) may
/// alias. A place that is a projection prefix of another conflicts with it.
ZC_NODISCARD bool placesConflict(const mir::MirPlace& first, const mir::MirPlace& second) noexcept;

/// \brief Untrusted move-path inventory awaiting independent reconstruction.
class MovePathCandidate final {
public:
  MovePathCandidate(identity::SemanticContextBrand semanticContext,
                    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
                    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
                    zc::Vector<MovePathFunction>&& functions) noexcept;
  MovePathCandidate(MovePathCandidate&&) noexcept = default;
  MovePathCandidate& operator=(MovePathCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(MovePathCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  zc::Vector<MovePathFunction> functions;
};

/// \brief Immutable move-path inventory bound to one Built MIR and event overlay.
class VerifiedMovePaths final {
public:
  ~VerifiedMovePaths() noexcept(false);
  VerifiedMovePaths(VerifiedMovePaths&&) noexcept;
  VerifiedMovePaths& operator=(VerifiedMovePaths&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedMovePaths);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MovePathFunction> functions() const noexcept;
  ZC_NODISCARD bool conflicts(const MovePathKey& first, const MovePathKey& second) const noexcept;

private:
  struct Impl;
  explicit VerifiedMovePaths(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class MovePathVerifier;
};

/// \brief Derives exact root and projection-prefix move paths from admitted MIR.
class MovePathBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<MovePathCandidate> build(
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently validates and publishes one move-path inventory.
class MovePathVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedMovePaths> verify(
      MovePathCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
