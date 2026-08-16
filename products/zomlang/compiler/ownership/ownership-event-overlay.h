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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/checker/body-checker.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/hir/hir-module.h"
#include "zomlang/compiler/identity/brand.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ir/ir-failure.h"
#include "zomlang/compiler/ir/ir-identity.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/surface-admission.h"

namespace zomlang::compiler::ownership {

/// \brief Revision-bound digest of one complete ownership event overlay.
class OwnershipEventOverlayRevision final {
public:
  constexpr OwnershipEventOverlayRevision() noexcept = default;
  ZC_NODISCARD static OwnershipEventOverlayRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept {
    return OwnershipEventOverlayRevision(digest);
  }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }
  constexpr bool operator==(OwnershipEventOverlayRevision other) const noexcept {
    return value == other.value;
  }
  constexpr bool operator!=(OwnershipEventOverlayRevision other) const noexcept {
    return !(*this == other);
  }

private:
  explicit OwnershipEventOverlayRevision(const identity::Sha256Digest& digest) noexcept
      : value(digest) {}
  identity::Sha256Digest value;
};

/// \brief Closed kind algebra for canonical MIR control-flow points.
enum class MirPointKind : uint8_t {
  Entry = 0x01,
  BeforeStatement = 0x02,
  AfterStatement = 0x03,
  BeforeTerminator = 0x04,
  Edge = 0x05,
  Exit = 0x06,
};

/// \brief Closed kind algebra for canonical MIR function exits.
enum class MirExitKind : uint8_t {
  Return = 0x01,
  ResidualReturn = 0x02,
  Break = 0x03,
  Continue = 0x04,
  Panic = 0x05,
  Unwind = 0x06,
  Cancellation = 0x07,
  Unreachable = 0x08,
};

struct MirEntryPoint final {};
struct MirBeforeStatementPoint final {
  mir::MirBlockId block;
  uint32_t ordinal;
};
struct MirAfterStatementPoint final {
  mir::MirBlockId block;
  uint32_t ordinal;
};
struct MirBeforeTerminatorPoint final {
  mir::MirBlockId block;
};
struct MirEdgePoint final {
  mir::MirBlockId from;
  uint32_t edgeOrdinal;
  mir::MirBlockId to;
};
struct MirExitPoint final {
  mir::MirBlockId block;
  MirExitKind kind;
};

/// \brief Closed discriminated union of canonical MIR control-flow points.
class MirPoint final {
public:
  MirPoint(MirPoint&&) noexcept = default;
  MirPoint& operator=(MirPoint&&) noexcept = default;
  MirPoint(const MirPoint&) = default;
  MirPoint& operator=(const MirPoint&) = default;

  ZC_NODISCARD static MirPoint entry() noexcept;
  ZC_NODISCARD static MirPoint beforeStatement(mir::MirBlockId block, uint32_t ordinal) noexcept;
  ZC_NODISCARD static MirPoint afterStatement(mir::MirBlockId block, uint32_t ordinal) noexcept;
  ZC_NODISCARD static MirPoint beforeTerminator(mir::MirBlockId block) noexcept;
  ZC_NODISCARD static MirPoint edge(mir::MirBlockId from, uint32_t edgeOrdinal,
                                    mir::MirBlockId to) noexcept;
  ZC_NODISCARD static MirPoint exit(mir::MirBlockId block, MirExitKind kind) noexcept;
  ZC_NODISCARD MirPointKind kind() const noexcept;
  ZC_NODISCARD const MirBeforeStatementPoint& beforeStatementValue() const;
  ZC_NODISCARD const MirAfterStatementPoint& afterStatementValue() const;
  ZC_NODISCARD const MirBeforeTerminatorPoint& beforeTerminatorValue() const;
  ZC_NODISCARD const MirEdgePoint& edgeValue() const;
  ZC_NODISCARD const MirExitPoint& exitValue() const;
  bool operator==(const MirPoint& other) const noexcept;
  bool operator!=(const MirPoint& other) const noexcept { return !(*this == other); }
  bool operator<(const MirPoint& other) const noexcept;

private:
  explicit MirPoint(MirEntryPoint value) noexcept;
  explicit MirPoint(MirBeforeStatementPoint value) noexcept;
  explicit MirPoint(MirAfterStatementPoint value) noexcept;
  explicit MirPoint(MirBeforeTerminatorPoint value) noexcept;
  explicit MirPoint(MirEdgePoint value) noexcept;
  explicit MirPoint(MirExitPoint value) noexcept;
  zc::OneOf<MirEntryPoint, MirBeforeStatementPoint, MirAfterStatementPoint,
            MirBeforeTerminatorPoint, MirEdgePoint, MirExitPoint>
      value;
};

/// \brief Owner-bound deterministic location inside one MIR function body.
struct MirLocation final {
  identity::DefId owner;
  MirPoint point;

  bool operator==(const MirLocation& other) const noexcept {
    return owner == other.owner && point == other.point;
  }
  bool operator!=(const MirLocation& other) const noexcept { return !(*this == other); }
};

/// \brief Canonical key of one MIR event: its location plus operand ordinal.
struct MirEventKey final {
  MirLocation location;
  uint32_t operandOrdinal;

  bool operator==(const MirEventKey& other) const noexcept {
    return location == other.location && operandOrdinal == other.operandOrdinal;
  }
  bool operator!=(const MirEventKey& other) const noexcept { return !(*this == other); }
};

/// \brief Phase of one ownership event relative to its MIR source.
enum class OwnershipEventStage : uint8_t { Source = 0x01, Effect = 0x02, Commit = 0x03 };

/// \brief Closed role algebra for one MIR event slot.
enum class OwnershipEventRole : uint8_t {
  Operation = 0x01,
  EntryRoot = 0x02,
  OperandRead = 0x03,
  OperandCopy = 0x04,
  OperandMove = 0x05,
  ConstantOperand = 0x06,
  DestinationWrite = 0x07,
  BorrowIssue = 0x08,
  BorrowActivation = 0x09,
  StorageLive = 0x0a,
  StorageDead = 0x0b,
  SetDiscriminant = 0x0c,
  Deinitialize = 0x0d,
  LogicalDrop = 0x0e,
  LinearConsume = 0x0f,
  Capture = 0x10,
  Escape = 0x11,
  VariantSwitch = 0x12,
  PanicPayload = 0x13,
  UnsafeOperation = 0x14,
  UnsafeAcknowledgement = 0x15,
  StaticAddress = 0x16,
  CheckedCastCheck = 0x17,
  CheckedCastSuccess = 0x18,
  CheckedCastFailure = 0x19,
  CastCarrierInitialize = 0x1a,
  CastCarrierTransfer = 0x1b,
  CastCarrierDrop = 0x1c,
};

/// \brief One verified event slot in one function overlay.
struct MirEventSlot final {
  MirEventKey key;
  OwnershipEventStage stage;
  zc::Vector<OwnershipEventRole> roles;
};

/// \brief Presentation-only validated source association for one MIR event.
struct MirEventSource final {
  MirEventKey key;
  identity::SourceSpan span;
};

/// \brief Canonical identity of one MIR borrow issue.
struct LoanKey final {
  MirEventKey issue;

  bool operator==(const LoanKey& other) const noexcept { return issue == other.issue; }
  bool operator!=(const LoanKey& other) const noexcept { return !(*this == other); }
};

/// \brief Checker-authorized activation of one mutable receiver borrow on a call edge.
struct DeferredActivationFact final {
  LoanKey loan;
  MirEventKey receiverSource;
  MirEventKey activation;
  checker::checked::ReceiverMode receiverMode;
  identity::SemanticTypeId adjustmentSource;
  identity::SemanticTypeId adjustmentDestination;
  zc::Vector<checker::checked::ReceiverAdjustmentStep> adjustmentSteps;
};

/// \brief One persisted RFC 0015 marker-proof outcome for an ownership event.
struct OwnershipMarkerDecisionPositive final {
  checker::signature::MarkerFact proof;
};

/// \brief One persisted explicit negative RFC 0015 marker-proof outcome.
struct OwnershipMarkerDecisionExplicitNegative final {
  checker::signature::MarkerFact explicitFact;
};

/// \brief One persisted marker query without a satisfiable proof.
struct OwnershipMarkerDecisionUnsatisfied final {};

using OwnershipMarkerDecision =
    zc::OneOf<OwnershipMarkerDecisionPositive, OwnershipMarkerDecisionExplicitNegative,
              OwnershipMarkerDecisionUnsatisfied>;

/// \brief Exact revision-bound identity of one ownership marker query.
struct OwnershipMarkerUseKey final {
  MirEventKey event;
  identity::DefId marker;
  identity::SemanticTypeId subject;
  checker::signature::MarkerPolicyRegistryRevision markerPolicyRevision;
  checker::cross_module::CoherenceViewRevision coherenceRevision;
};

/// \brief Immutable ownership projection of one Copy or Linear marker query.
struct OwnershipMarkerUse final {
  OwnershipMarkerUseKey key;
  OwnershipMarkerDecision decision;
};

/// \brief Closed logical deinitialization action algebra for one resource component.
struct LogicalDropDeclaredAction final {
  identity::DefId deinitializer;
};
struct LogicalDropBuiltinAction final {
  identity::SemanticTypeId ownerType;
};
struct LogicalDropDynamicAction final {
  identity::SemanticTypeId existentialType;
};
using LogicalDropAction =
    zc::OneOf<LogicalDropDeclaredAction, LogicalDropBuiltinAction, LogicalDropDynamicAction>;

/// \brief One postorder logical-drop component authorized by marker decisions.
struct LogicalDropPlanComponent final {
  mir::MirPlace place;
  identity::SemanticTypeId valueType;
  zc::Maybe<LogicalDropAction> dropAction;
  OwnershipMarkerUseKey copyDecision;
  OwnershipMarkerUseKey linearDecision;
  uint32_t declarationOrdinal;
};

/// \brief Complete logical-drop plan for one initialization event.
struct LogicalDropPlan final {
  MirEventKey initialization;
  mir::MirPlace root;
  zc::Vector<LogicalDropPlanComponent> components;
};

/// \brief Exact live checker and IR capabilities required to construct one ownership overlay.
struct OwnershipEventOverlayInput final {
  const OwnershipAdmittedBoundModule& admitted;
  const hir::VerifiedCheckedModule& checked;
  const hir::VerifiedHirModule& hir;
  const mir::VerifiedBuiltMir& built;
  checker::body::BodyCheckingInput body;
};

/// \brief One function-scoped slice of the ownership event overlay.
struct OwnershipFunctionEventOverlay final {
  identity::DefId owner;
  zc::Vector<MirEventSlot> slots;
  zc::Vector<MirEventSource> sourceMap;
  zc::Vector<DeferredActivationFact> deferredActivations;
  zc::Vector<OwnershipMarkerUse> markerUses;
  zc::Vector<LogicalDropPlan> logicalDropPlans;
};

/// \brief Untrusted mutable overlay product admitted only by the independent verifier.
class OwnershipEventOverlayCandidate final {
public:
  OwnershipEventOverlayCandidate(identity::SemanticContextBrand semanticContext,
                                 identity::ContextFingerprint&& contextFingerprint,
                                 identity::ModuleId module,
                                 checker::checked::CheckedFactsRevision checkedFactsRevision,
                                 mir::MirRevisionId builtRevision,
                                 zc::Vector<OwnershipFunctionEventOverlay>&& functions) noexcept
      : semanticContext(semanticContext),
        contextFingerprint(zc::mv(contextFingerprint)),
        module(module),
        checkedFactsRevision(checkedFactsRevision),
        builtRevision(builtRevision),
        functions(zc::mv(functions)) {}
  OwnershipEventOverlayCandidate(OwnershipEventOverlayCandidate&&) noexcept = default;
  OwnershipEventOverlayCandidate& operator=(OwnershipEventOverlayCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(OwnershipEventOverlayCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  checker::checked::CheckedFactsRevision checkedFactsRevision;
  mir::MirRevisionId builtRevision;
  zc::Vector<OwnershipFunctionEventOverlay> functions;
};

/// \brief Immutable, revision-checked ownership event overlay published by the verifier.
class VerifiedOwnershipEventOverlay final {
public:
  ~VerifiedOwnershipEventOverlay() noexcept(false);
  VerifiedOwnershipEventOverlay(VerifiedOwnershipEventOverlay&&) noexcept;
  VerifiedOwnershipEventOverlay& operator=(VerifiedOwnershipEventOverlay&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedOwnershipEventOverlay);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const checker::checked::CheckedFactsRevision& checkedFactsRevision() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const OwnershipFunctionEventOverlay> functions() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& revision() const noexcept;

private:
  struct Impl;
  explicit VerifiedOwnershipEventOverlay(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class OwnershipEventOverlayVerifier;
};

/// \brief Exact canonical ownership event overlay framing codec.
class OwnershipEventOverlayCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& checkedFactsRevision,
      const identity::Sha256Digest& builtRevisionDigest,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encode(
      const identity::ContextFingerprint& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const checker::checked::CheckedFactsRevision& checkedFactsRevision,
      const mir::MirRevisionId& builtRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
  ZC_NODISCARD static zc::Maybe<OwnershipEventOverlayRevision> compute(
      const identity::ContextFingerprint& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const checker::checked::CheckedFactsRevision& checkedFactsRevision,
      const mir::MirRevisionId& builtRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
};

/// \brief Builds the ownership event overlay from one exact live checker-to-MIR handoff.
class OwnershipEventOverlayBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<OwnershipEventOverlayCandidate> build(
      const OwnershipEventOverlayInput& input);
};

/// \brief Sole publisher of immutable revision-checked ownership event overlays.
class OwnershipEventOverlayVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedOwnershipEventOverlay> verify(
      OwnershipEventOverlayCandidate&& candidate, const OwnershipEventOverlayInput& input);
};

}  // namespace zomlang::compiler::ownership
