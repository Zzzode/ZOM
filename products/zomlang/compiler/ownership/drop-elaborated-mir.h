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
#include "zomlang/compiler/driver/interface/borrow-evidence.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/ownership/facts/resources.h"
#include "zomlang/compiler/ownership/ownership-checked-mir.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership {

/// \brief Closed kind algebra for one drop discharge in the linear subset.
enum class DropDischargeKind : uint8_t {
  LogicalDrop = 0x01,
  OverwriteDrop = 0x02,
  ReturnTransfer = 0x03,
  ConsumingCallTransfer = 0x04,
  CastFailureDrop = 0x05,
};

/// \brief One ordered component step in a drop discharge.
///
/// Each component records the exact place, resource subject, value type,
/// optional logical-drop action, and declaration ordinal that the elaborator
/// validated against the verified ownership resource facts.
struct DropDischargeComponent final {
  facts::MovePathKey place;
  facts::DropResourceSubject subject;
  identity::SemanticTypeId valueType;
  zc::Maybe<LogicalDropAction> action;
  uint32_t declarationOrdinal;
};

/// \brief One recorded drop discharge for one pending drop obligation.
///
/// The event is the MIR event at which the discharge executes, the place is
/// the root move path of the discharged resource, and the components are the
/// execution-order sequence validated by the elaborator. A Positive Linear
/// discharge records the linked LinearConsume that performs the obligation's
/// linear consumption at the discharge cutpoint; a purely Logical discharge
/// carries none.
struct DropDischargeRecord final {
  MirEventKey event;
  facts::MovePathKey place;
  DropDischargeKind kind;
  facts::DropPlanMode mode;
  zc::Vector<DropDischargeComponent> components;
  zc::Maybe<facts::LinearConsumption> linearConsume;
};

/// \brief Immutable RFC 0007 drop-elaborated MIR capability.
///
/// DropElaboratedMir is the sole committed successor of OwnershipCheckedMir.
/// The elaborator consumes the checked wrapper, rechecks every revision, lease,
/// and identity without dereferencing unvalidated handles, validates that every
/// pending drop obligation has a complete discharge path through the linear
/// CFG, classifies each discharge kind from the verified linear-consumption
/// inventory, binds each discharge event to its exact cutpoint, and links the
/// LinearConsume consumed by every Positive Linear discharge. It either
/// publishes one wrapper or publishes no value. A rejected operation destroys
/// its consumed local input and returns no predecessor or partial successor.
///
/// The wrapper stores the OwnershipCheckedMir and the recorded drop-discharge
/// inventory. It does not retain a repository pointer or capability in the
/// encoded or runtime wrapper; every successor operation receives a live
/// capability again and resolves the embedded lease before inspecting or
/// moving the predecessor payload.
class DropElaboratedMir final {
public:
  ~DropElaboratedMir() noexcept(false);
  DropElaboratedMir(DropElaboratedMir&&) noexcept;
  DropElaboratedMir& operator=(DropElaboratedMir&&) noexcept;
  ZC_DISALLOW_COPY(DropElaboratedMir);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  /// \brief Returns the owned immutable ownership-checked MIR payload.
  ///
  /// Valid only before takeCheckedMir is called. The session extracts the
  /// checked payload for separate publication; after that call the wrapper
  /// retains only the discharge inventory.
  ZC_NODISCARD const OwnershipCheckedMir& checkedMir() const noexcept;
  /// \brief Returns the recorded drop-discharge inventory in execution order.
  ZC_NODISCARD zc::ArrayPtr<const DropDischargeRecord> discharges() const noexcept;
  /// \brief Moves the owned ownership-checked MIR payload out of this wrapper.
  ///
  /// The session calls this after elaboration to publish the checked payload
  /// alongside the discharge inventory. The wrapper retains the discharge
  /// inventory; checkedMir is no longer valid after this call.
  ZC_NODISCARD OwnershipCheckedMir takeCheckedMir() && noexcept;

private:
  struct Impl;
  explicit DropElaboratedMir(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class DropElaborator;
  friend class CoroutineElaborator;
};

/// \brief Sole publisher of immutable drop-elaborated MIR wrappers.
///
/// The elaborator consumes OwnershipCheckedMir and rechecks, without
/// dereferencing unvalidated handles: semantic context brand and fingerprint;
/// module identity; the canonical Built MIR artifact and exact MirRevisionId;
/// the exact event overlay revision; the facts revision triple; the resolved
/// lease BorrowEvidenceRevision; and byte equality between the analyzed lease
/// and VerifiedBuiltMir.borrowEvidenceLease. A foreign, missing, stale,
/// swapped, or post-teardown lease or capability selects RFC 0010
/// InputRevisionMismatch before candidate construction.
///
/// For the current linear MIR subset (single block, no branches), the
/// elaboration then validates that every pending drop obligation in the
/// verified resource facts has a complete discharge path through the linear
/// CFG. A resource moved between places legitimately appears in multiple
/// closed drop plans, one per move site, so a fact may be covered by more
/// than one plan. The elaborator rejects a missing discharge (a fact with no
/// plan at all) or a component order violation with IrInvariantRejected at
/// OwnershipProofValidation.
///
/// Each discharge is classified from the verified linear-consumption
/// inventory: a return operand selects ReturnTransfer, a consuming-call
/// operand selects ConsumingCallTransfer, and an obligation with no terminator
/// consumption is dropped at the function exit with LogicalDrop. The discharge
/// event is bound to the exact consumption cutpoint (BeforeTerminator for a
/// return or call, the return exit for a drop). Every Positive Linear
/// discharge records the linked LinearConsume; every linear obligation must be
/// consumed by exactly one emitted discharge, and an unconsumed or
/// doubly-consumed obligation selects InvalidCleanup.
class DropElaborator final {
public:
  ZC_NODISCARD static ir::IrOperationResult<DropElaboratedMir> elaborateDrops(
      OwnershipCheckedMir&& checked,
      const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository);

  /// \brief Rechecks the full ownership-rail lineage without dereferencing
  /// unvalidated handles.
  ///
  /// Successor wrappers (CoroutineElaborator, ExecutableMirVerifier) call this
  /// because only DropElaborator is a friend of VerifiedBuiltMir, which owns
  /// the private lease and identity-authority methods.
  ZC_NODISCARD static bool recheckLineage(
      const OwnershipCheckedMir& checked,
      const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository);

  /// \brief Retains the checker identity authority for IR failure construction.
  ///
  /// Successor wrappers call this through the DropElaborator friend of
  /// VerifiedBuiltMir to construct the identity resolver for a rejection.
  ZC_NODISCARD static checker::CheckerIdentityAuthority retainIdentityAuthority(
      const mir::VerifiedBuiltMir& builtMir);
};

/// \brief Immutable RFC 0007 coroutine-elaborated MIR capability.
///
/// CoroutineElaboratedMir is the sole committed successor of DropElaboratedMir.
/// The elaborator consumes the drop-elaborated wrapper, rechecks every
/// revision, lease, and identity without dereferencing unvalidated handles,
/// and validates that the current MIR subset admits no coroutine suspension
/// point. The closed Built MIR terminator algebra (Return, Unreachable, Call)
/// cannot represent a suspension point, so the validation is structural; a
/// future coroutine subset must extend the algebra and reject any suspension
/// point here before publication.
///
/// The wrapper stores the DropElaboratedMir predecessor. It does not retain a
/// repository pointer or capability; every successor operation receives a live
/// capability again and resolves the embedded lease before inspecting or
/// moving the predecessor payload.
class CoroutineElaboratedMir final {
public:
  ~CoroutineElaboratedMir() noexcept(false);
  CoroutineElaboratedMir(CoroutineElaboratedMir&&) noexcept;
  CoroutineElaboratedMir& operator=(CoroutineElaboratedMir&&) noexcept;
  ZC_DISALLOW_COPY(CoroutineElaboratedMir);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  /// \brief Returns the recorded drop-discharge inventory in execution order.
  ZC_NODISCARD zc::ArrayPtr<const DropDischargeRecord> discharges() const noexcept;
  /// \brief Moves the owned drop-elaborated predecessor out of this wrapper.
  ZC_NODISCARD DropElaboratedMir takeDropElaboratedMir() && noexcept;
  /// \brief Moves the owned ownership-checked MIR payload out of the predecessor chain.
  ZC_NODISCARD OwnershipCheckedMir takeCheckedMir() && noexcept;

private:
  struct Impl;
  explicit CoroutineElaboratedMir(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class CoroutineElaborator;
  friend class ExecutableMirVerifier;
};

/// \brief Sole publisher of immutable coroutine-elaborated MIR wrappers.
///
/// The elaborator consumes DropElaboratedMir and rechecks the full lineage
/// (semantic context brand and fingerprint, module identity, Built MIR and
/// overlay revisions, facts revision triple, resolved lease revision, and
/// lease byte equality) without dereferencing unvalidated handles. A foreign,
/// missing, stale, swapped, or post-teardown lease or capability selects RFC
/// 0010 InputRevisionMismatch before candidate construction.
class CoroutineElaborator final {
public:
  ZC_NODISCARD static ir::IrOperationResult<CoroutineElaboratedMir> elaborateCoroutines(
      DropElaboratedMir&& elaborated,
      const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository);
};

/// \brief Immutable RFC 0007 verified executable MIR capability.
///
/// VerifiedExecutableMir is the terminal committed successor of the ownership
/// rail. The verifier consumes the coroutine-elaborated wrapper, rechecks
/// every revision, lease, and identity without dereferencing unvalidated
/// handles, and certifies that the emitted cleanup (the recorded drop-discharge
/// inventory) discharges every pending drop obligation and consumes every
/// Positive Linear obligation exactly once. A rejected operation destroys its
/// consumed local input and returns no predecessor or partial successor.
///
/// The wrapper stores the CoroutineElaboratedMir predecessor. It does not
/// retain a repository pointer or capability; the terminal wrapper is the
/// ownership-rail artifact handed to the RFC 0006 cleanup consumer and target
/// lowering.
class VerifiedExecutableMir final {
public:
  ~VerifiedExecutableMir() noexcept(false);
  VerifiedExecutableMir(VerifiedExecutableMir&&) noexcept;
  VerifiedExecutableMir& operator=(VerifiedExecutableMir&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedExecutableMir);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  /// \brief Returns the recorded drop-discharge inventory in execution order.
  ZC_NODISCARD zc::ArrayPtr<const DropDischargeRecord> discharges() const noexcept;
  /// \brief Moves the owned ownership-checked MIR payload out of the predecessor chain.
  ZC_NODISCARD OwnershipCheckedMir takeCheckedMir() && noexcept;

private:
  struct Impl;
  explicit VerifiedExecutableMir(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ExecutableMirVerifier;
};

/// \brief Sole publisher of immutable verified executable MIR wrappers.
///
/// The verifier consumes CoroutineElaboratedMir and rechecks the full lineage
/// without dereferencing unvalidated handles. It then certifies cleanup
/// consumption completeness: every Positive Linear obligation in the verified
/// resource facts is consumed by exactly one emitted discharge's linked
/// LinearConsume, and no discharge links an obligation absent from the facts.
/// A foreign, missing, stale, swapped, or post-teardown lease or capability
/// selects RFC 0010 InputRevisionMismatch; an incomplete cleanup selects
/// InvalidCleanup at OwnershipProofValidation.
class ExecutableMirVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedExecutableMir> verifyExecutableMir(
      CoroutineElaboratedMir&& elaborated,
      const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository);
};

}  // namespace zomlang::compiler::ownership
