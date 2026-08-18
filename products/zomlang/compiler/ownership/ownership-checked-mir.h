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
#include "zomlang/compiler/driver/interface/borrow-evidence.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/facts/ownership-facts-revision.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::type {
class SemanticTypeStore;
}  // namespace zomlang::compiler::type

namespace zomlang::compiler::ownership {

/// \brief Immutable RFC 0007 ownership-checked MIR capability.
///
/// OwnershipCheckedMir is the sole committed successor of Built MIR, the
/// verified ownership event overlay, and the verified ownership facts. The
/// finalizer consumes all three move-only products, rechecks every revision,
/// lease, and identity without dereferencing unvalidated handles, and either
/// publishes one wrapper or publishes no value. A rejected operation destroys
/// its consumed local input and returns no predecessor or partial successor.
///
/// The wrapper stores the Built MIR, verified event overlay, facts, and the
/// exact revision triple. It does not retain a repository pointer or
/// capability in the encoded or runtime wrapper; every successor operation
/// receives a live capability again and resolves the embedded lease before
/// inspecting or moving the predecessor payload.
class OwnershipCheckedMir final {
public:
  ~OwnershipCheckedMir() noexcept(false);
  OwnershipCheckedMir(OwnershipCheckedMir&&) noexcept;
  OwnershipCheckedMir& operator=(OwnershipCheckedMir&&) noexcept;
  ZC_DISALLOW_COPY(OwnershipCheckedMir);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  /// \brief Returns the owned immutable Built MIR payload.
  ZC_NODISCARD const mir::VerifiedBuiltMir& builtMir() const noexcept;
  /// \brief Returns the owned immutable verified ownership event overlay.
  ZC_NODISCARD const VerifiedOwnershipEventOverlay& eventOverlay() const noexcept;
  /// \brief Returns the owned immutable verified ownership facts bundle.
  ZC_NODISCARD const facts::VerifiedOwnershipInputs& facts() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& eventOverlayRevision() const noexcept;
  ZC_NODISCARD const facts::OwnershipFactsRevision& factsRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;

private:
  struct Impl;
  explicit OwnershipCheckedMir(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class OwnershipFinalizer;
};

/// \brief Sole publisher of immutable ownership-checked MIR wrappers.
///
/// The finalizer consumes Built MIR, the verified event overlay, and the
/// verified ownership facts and rechecks, without dereferencing unvalidated
/// handles: semantic context brand and fingerprint; module identity; the
/// canonical Built MIR artifact and exact MirRevisionId; the exact event
/// overlay revision; the facts revision triple; the resolved lease
/// BorrowEvidenceRevision; and byte equality between the analyzed lease and
/// VerifiedBuiltMir.borrowEvidenceLease. A foreign, missing, stale, swapped,
/// or post-teardown lease or capability selects RFC 0010 InputRevisionMismatch
/// before candidate construction.
class OwnershipFinalizer final {
public:
  ZC_NODISCARD static ir::IrOperationResult<OwnershipCheckedMir> finalizeOwnership(
      mir::VerifiedBuiltMir&& builtMir, VerifiedOwnershipEventOverlay&& eventOverlay,
      facts::VerifiedOwnershipInputs&& facts,
      const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository,
      const type::SemanticTypeStore& semanticTypes);
};

}  // namespace zomlang::compiler::ownership
