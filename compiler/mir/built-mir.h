// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/checker/body/body-checker.h"
#include "compiler/checker/inference/checked-facts.h"
#include "compiler/hir/hir-module.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/ir-identity.h"

namespace zomlang::compiler::ownership {
class OwnershipAdmittedBoundModule;
class OwnershipEventOverlayBuilder;
class OwnershipEventOverlayVerifier;
class OwnershipFinalizer;
class DropElaborator;
namespace facts {
class FlowBuilder;
class FlowVerifier;
class InitializationBuilder;
class InitializationSourceVerifier;
class InitializationVerifier;
class BorrowSourceVerifier;
class LoanBuilder;
class LoanVerifier;
class MovePathBuilder;
class MovePathVerifier;
class OwnershipInputVerifier;
class OwnershipResourceBuilder;
class OwnershipResourceVerifier;
class ReborrowRegionBuilder;
class ReborrowRegionVerifier;
class ReborrowStateBuilder;
class ReborrowStateVerifier;
class ReferenceDefinitionBuilder;
class ReferenceDefinitionVerifier;
class RegionMembershipBuilder;
class RegionMembershipVerifier;
class EscapeBuilder;
class EscapeVerifier;
class CaptureBuilder;
class CaptureVerifier;
class RegionOutlivesBuilder;
class RegionOutlivesVerifier;
}  // namespace facts
}  // namespace zomlang::compiler::ownership

namespace zomlang::compiler::mir {

/// \brief Call-duration authority required to lower and validate value-transfer operands.
struct BuiltMirInput final {
  const hir::VerifiedHirModule& hir;
  const checker::body::BodyCheckingInput& body;
};

/// \brief Deterministic one-based identity of a local in one MIR body.
class MirLocalId final {
public:
  constexpr MirLocalId() noexcept = default;
  ZC_NODISCARD static zc::Maybe<MirLocalId> fromOrdinal(uint32_t ordinal) noexcept;
  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }
  constexpr bool operator==(MirLocalId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(MirLocalId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr MirLocalId(uint32_t ordinal) noexcept : value(ordinal) {}
  uint32_t value = 0;
};

/// \brief Deterministic one-based identity of a lexical source scope in one MIR body.
class MirSourceScopeId final {
public:
  constexpr MirSourceScopeId() noexcept = default;
  ZC_NODISCARD static zc::Maybe<MirSourceScopeId> fromOrdinal(uint32_t ordinal) noexcept;
  ZC_NODISCARD constexpr bool isValid() const noexcept { return value != 0; }
  ZC_NODISCARD constexpr uint32_t ordinal() const noexcept { return value; }
  constexpr bool operator==(MirSourceScopeId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(MirSourceScopeId other) const noexcept { return !(*this == other); }

private:
  explicit constexpr MirSourceScopeId(uint32_t ordinal) noexcept : value(ordinal) {}
  uint32_t value = 0;
};

enum class MirProjectionKind : uint8_t {
  Field = 0x01,
  Index = 0x02,
  Dereference = 0x03,
  Downcast = 0x04,
  Subslice = 0x05,
};

struct MirFieldProjection final {
  identity::DefId field;
  identity::SemanticTypeId inputType;
  identity::SemanticTypeId resultType;
};
struct MirIndexProjection final {
  MirLocalId index;
  identity::SemanticTypeId inputType;
  identity::SemanticTypeId resultType;
};
struct MirDereferenceProjection final {
  identity::SemanticTypeId inputType;
  identity::SemanticTypeId resultType;
};
struct MirDowncastProjection final {
  identity::DefId variant;
  identity::SemanticTypeId inputType;
  identity::SemanticTypeId resultType;
};
struct MirSubsliceProjection final {
  uint32_t first;
  uint32_t pastLast;
  identity::SemanticTypeId inputType;
  identity::SemanticTypeId resultType;
};

/// \brief Closed target-independent place projection algebra.
class MirProjection final {
public:
  MirProjection(MirProjection&&) noexcept = default;
  MirProjection& operator=(MirProjection&&) noexcept = default;
  ZC_DISALLOW_COPY(MirProjection);

  ZC_NODISCARD static MirProjection field(identity::DefId field, identity::SemanticTypeId inputType,
                                          identity::SemanticTypeId resultType) noexcept;
  ZC_NODISCARD static MirProjection index(MirLocalId index, identity::SemanticTypeId inputType,
                                          identity::SemanticTypeId resultType) noexcept;
  ZC_NODISCARD static MirProjection dereference(identity::SemanticTypeId inputType,
                                                identity::SemanticTypeId resultType) noexcept;
  ZC_NODISCARD static MirProjection downcast(identity::DefId variant,
                                             identity::SemanticTypeId inputType,
                                             identity::SemanticTypeId resultType) noexcept;
  ZC_NODISCARD static zc::Maybe<MirProjection> subslice(
      uint32_t first, uint32_t pastLast, identity::SemanticTypeId inputType,
      identity::SemanticTypeId resultType) noexcept;
  ZC_NODISCARD MirProjection clone() const noexcept;
  ZC_NODISCARD MirProjectionKind kind() const noexcept;
  ZC_NODISCARD bool isStructurallyValid() const noexcept;
  ZC_NODISCARD identity::SemanticTypeId inputType() const noexcept;
  ZC_NODISCARD identity::SemanticTypeId resultType() const noexcept;
  ZC_NODISCARD const MirFieldProjection& fieldValue() const;
  ZC_NODISCARD const MirIndexProjection& indexValue() const;
  ZC_NODISCARD const MirDowncastProjection& downcastValue() const;
  ZC_NODISCARD const MirSubsliceProjection& subsliceValue() const;

private:
  explicit MirProjection(MirFieldProjection value) noexcept;
  explicit MirProjection(MirIndexProjection value) noexcept;
  explicit MirProjection(MirDereferenceProjection value) noexcept;
  explicit MirProjection(MirDowncastProjection value) noexcept;
  explicit MirProjection(MirSubsliceProjection value) noexcept;
  zc::OneOf<MirFieldProjection, MirIndexProjection, MirDereferenceProjection, MirDowncastProjection,
            MirSubsliceProjection>
      value;
};

/// \brief One typed storage location and its logical projection path.
class MirPlace final {
public:
  MirPlace(MirLocalId local, identity::SemanticTypeId rootType,
           zc::Vector<MirProjection>&& projections, identity::SemanticTypeId resultType) noexcept;
  ~MirPlace() noexcept(false);
  MirPlace(MirPlace&&) noexcept;
  MirPlace& operator=(MirPlace&&) noexcept;
  ZC_DISALLOW_COPY(MirPlace);

  ZC_NODISCARD MirPlace clone() const;
  ZC_NODISCARD MirLocalId local() const noexcept;
  ZC_NODISCARD identity::SemanticTypeId rootType() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MirProjection> projections() const noexcept;
  ZC_NODISCARD identity::SemanticTypeId resultType() const noexcept;
  ZC_NODISCARD bool hasConsistentTypeChain() const noexcept;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

enum class MirOperandKind : uint8_t { Copy = 0x01, Move = 0x02, Constant = 0x03 };

struct MirCopyOperand final {
  MirPlace place;
};
struct MirMoveOperand final {
  MirPlace place;
};
struct MirConstantOperand final {
  identity::SemanticTypeId type;
  checker::checked::CanonicalConstValue value;
};

/// \brief Closed affine operand algebra; copy and move remain explicit uses.
class MirOperand final {
public:
  MirOperand(MirOperand&&) noexcept = default;
  MirOperand& operator=(MirOperand&&) noexcept = default;
  ZC_DISALLOW_COPY(MirOperand);

  ZC_NODISCARD static MirOperand copy(MirPlace&& place) noexcept;
  ZC_NODISCARD static MirOperand move(MirPlace&& place) noexcept;
  ZC_NODISCARD static MirOperand constant(identity::SemanticTypeId type,
                                          checker::checked::CanonicalConstValue&& value) noexcept;
  ZC_NODISCARD MirOperand clone() const;
  ZC_NODISCARD MirOperandKind kind() const noexcept;
  ZC_NODISCARD const MirPlace& place() const;
  ZC_NODISCARD const MirConstantOperand& constantValue() const;

private:
  explicit MirOperand(MirCopyOperand&& value) noexcept;
  explicit MirOperand(MirMoveOperand&& value) noexcept;
  explicit MirOperand(MirConstantOperand&& value) noexcept;
  zc::OneOf<MirCopyOperand, MirMoveOperand, MirConstantOperand> value;
};

enum class MirBorrowKind : uint8_t { Shared = 0x01, Mutable = 0x02 };
enum class MirRvalueKind : uint8_t {
  Use = 0x01,
  NominalAggregate = 0x02,
  Comparison = 0x03,
  Arithmetic = 0x04
};

/// \brief Closed comparison operator produced by primitive comparison lowering.
///
/// Covers the six relational comparisons of same-typed primitive scalars. The
/// field is modeled as an operator rather than a boolean so the encoded byte is
/// self-describing and each comparison kind is distinguishable in the canonical
/// stream. These bytes flow through `encodeRvalue`; changing a tag is a codec
/// change.
enum class MirComparisonOperator : uint8_t {
  Eq = 0x01,
  Ne = 0x02,
  Lt = 0x03,
  Le = 0x04,
  Gt = 0x05,
  Ge = 0x06
};

/// \brief Closed arithmetic and bitwise operator produced by primitive
/// non-comparison binary lowering.
///
/// Covers the twelve arithmetic and bitwise binary operators of same-typed
/// primitive scalars. Unlike a comparison, the result is the shared operand type
/// rather than bool. The operator is modeled as a self-describing byte so each
/// kind is distinguishable in the canonical stream. Logical `&&`/`||` are
/// excluded (their short-circuit semantics are not a primitive binary op). These
/// bytes flow through `encodeRvalue`; changing a tag is a codec change.
enum class MirArithmeticOperator : uint8_t {
  Add = 0x01,
  Sub = 0x02,
  Mul = 0x03,
  Div = 0x04,
  Rem = 0x05,
  Pow = 0x06,
  Shl = 0x07,
  Shr = 0x08,
  UShr = 0x09,
  BitAnd = 0x0a,
  BitOr = 0x0b,
  BitXor = 0x0c
};

struct MirUseRvalue final {
  MirOperand operand;
};
struct MirNominalAggregateElement final {
  identity::DefId field;
  MirOperand operand;
};
struct MirNominalAggregateRvalue final {
  identity::DefId definition;
  identity::SemanticTypeId type;
  zc::Vector<MirNominalAggregateElement> elements;
};
struct MirComparisonRvalue final {
  MirComparisonOperator op;
  MirOperand left;
  MirOperand right;
  identity::SemanticTypeId resultType;
};
struct MirArithmeticRvalue final {
  MirArithmeticOperator op;
  MirOperand left;
  MirOperand right;
  identity::SemanticTypeId resultType;
};

/// \brief Canonical target-independent assignment value for the Built MIR boundary.
class MirRvalue final {
public:
  MirRvalue(MirRvalue&&) noexcept = default;
  MirRvalue& operator=(MirRvalue&&) noexcept = default;
  ZC_DISALLOW_COPY(MirRvalue);

  ZC_NODISCARD static MirRvalue use(MirOperand&& operand) noexcept;
  ZC_NODISCARD static MirRvalue nominalAggregate(
      identity::DefId definition, identity::SemanticTypeId type,
      zc::Vector<MirNominalAggregateElement>&& elements) noexcept;
  ZC_NODISCARD static MirRvalue comparison(MirComparisonOperator op, MirOperand&& left,
                                           MirOperand&& right,
                                           identity::SemanticTypeId resultType) noexcept;
  ZC_NODISCARD static MirRvalue arithmetic(MirArithmeticOperator op, MirOperand&& left,
                                           MirOperand&& right,
                                           identity::SemanticTypeId resultType) noexcept;
  ZC_NODISCARD MirRvalue clone() const;
  ZC_NODISCARD MirRvalueKind kind() const noexcept;
  ZC_NODISCARD const MirUseRvalue& useValue() const;
  ZC_NODISCARD const MirNominalAggregateRvalue& nominalAggregateValue() const;
  ZC_NODISCARD const MirComparisonRvalue& comparisonValue() const;
  ZC_NODISCARD const MirArithmeticRvalue& arithmeticValue() const;

private:
  explicit MirRvalue(MirUseRvalue&& value) noexcept;
  explicit MirRvalue(MirNominalAggregateRvalue&& value) noexcept;
  explicit MirRvalue(MirComparisonRvalue&& value) noexcept;
  explicit MirRvalue(MirArithmeticRvalue&& value) noexcept;
  zc::OneOf<MirUseRvalue, MirNominalAggregateRvalue, MirComparisonRvalue, MirArithmeticRvalue>
      value;
};

enum class MirInitializationKind : uint8_t { Initialize = 0x01, Overwrite = 0x02 };
enum class MirStatementKind : uint8_t {
  Assign = 0x01,
  StorageLive = 0x02,
  StorageDead = 0x03,
  BorrowCreation = 0x04,
  SetDiscriminant = 0x05,
  Deinitialize = 0x06,
  UnsafeScopeBoundary = 0x07,
};

/// \brief Direction of one unsafe-scope boundary marker.
enum class MirUnsafeScopeBoundaryKind : uint8_t { Enter = 0x01, Exit = 0x02 };

struct MirAssignmentStatement final {
  MirPlace destination;
  MirRvalue value;
  MirInitializationKind initialization;
};
struct MirStorageLiveStatement final {
  MirLocalId local;
};
struct MirStorageDeadStatement final {
  MirLocalId local;
};
struct MirBorrowCreationStatement final {
  MirPlace destination;
  MirBorrowKind kind;
  MirPlace source;
};
struct MirSetDiscriminantStatement final {
  MirPlace destination;
  identity::DefId variant;
};
struct MirDeinitializeStatement final {
  MirPlace destination;
};
struct MirUnsafeScopeBoundaryStatement final {
  MirUnsafeScopeBoundaryKind kind;
  MirSourceScopeId scope;
};

/// \brief Closed statement algebra retaining explicit storage and initialization state.
class MirStatement final {
public:
  MirStatement(MirStatement&&) noexcept = default;
  MirStatement& operator=(MirStatement&&) noexcept = default;
  ZC_DISALLOW_COPY(MirStatement);

  ZC_NODISCARD static MirStatement assign(MirPlace&& destination, MirRvalue&& value,
                                          MirInitializationKind initialization,
                                          identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirStatement storageLive(MirLocalId local,
                                               identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirStatement storageDead(MirLocalId local,
                                               identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirStatement borrowCreation(MirPlace&& destination, MirBorrowKind kind,
                                                  MirPlace&& source,
                                                  identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirStatement setDiscriminant(MirPlace&& destination, identity::DefId variant,
                                                   identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirStatement deinitialize(MirPlace&& destination,
                                                identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirStatement unsafeScopeBoundary(MirUnsafeScopeBoundaryKind kind,
                                                       MirSourceScopeId scope,
                                                       identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD MirStatement clone() const;
  ZC_NODISCARD MirStatementKind kind() const noexcept;
  /// \brief Returns presentation-only source ownership for this MIR operation.
  ZC_NODISCARD const identity::SourceSpan& sourceSpan() const noexcept;
  ZC_NODISCARD const MirAssignmentStatement& assignmentValue() const;
  ZC_NODISCARD MirLocalId storageLocal() const;
  ZC_NODISCARD const MirBorrowCreationStatement& borrowCreationValue() const;
  ZC_NODISCARD const MirSetDiscriminantStatement& setDiscriminantValue() const;
  ZC_NODISCARD const MirDeinitializeStatement& deinitializeValue() const;
  ZC_NODISCARD const MirUnsafeScopeBoundaryStatement& unsafeScopeBoundaryValue() const;

private:
  MirStatement(MirAssignmentStatement&& value, identity::SourceSpan&& sourceSpan) noexcept;
  MirStatement(MirStorageLiveStatement value, identity::SourceSpan&& sourceSpan) noexcept;
  MirStatement(MirStorageDeadStatement value, identity::SourceSpan&& sourceSpan) noexcept;
  MirStatement(MirBorrowCreationStatement&& value, identity::SourceSpan&& sourceSpan) noexcept;
  MirStatement(MirSetDiscriminantStatement&& value, identity::SourceSpan&& sourceSpan) noexcept;
  MirStatement(MirDeinitializeStatement&& value, identity::SourceSpan&& sourceSpan) noexcept;
  MirStatement(MirUnsafeScopeBoundaryStatement value, identity::SourceSpan&& sourceSpan) noexcept;
  zc::OneOf<MirAssignmentStatement, MirStorageLiveStatement, MirStorageDeadStatement,
            MirBorrowCreationStatement, MirSetDiscriminantStatement, MirDeinitializeStatement,
            MirUnsafeScopeBoundaryStatement>
      value;
  identity::SourceSpan sourceSpanValue;
};

enum class MirTerminatorKind : uint8_t {
  Return = 0x01,
  Unreachable = 0x02,
  Call = 0x03,
  Goto = 0x04,
  SwitchInt = 0x05,
};

struct MirReturnTerminator final {
  zc::Maybe<MirOperand> value;
};
struct MirUnreachableTerminator final {};

enum class MirCallEffectKind : uint8_t {
  NoActivation = 0x01,
  ActivateMutableReceiver = 0x02,
};

struct MirNoActivationCallEffect final {};
struct MirActivateMutableReceiverCallEffect final {
  MirLocalId temporary;
};

/// \brief Ownership-relevant effect committed only when a call reaches its normal edge.
class MirCallEffect final {
public:
  MirCallEffect(MirCallEffect&&) noexcept = default;
  MirCallEffect& operator=(MirCallEffect&&) noexcept = default;
  ZC_DISALLOW_COPY(MirCallEffect);

  ZC_NODISCARD static MirCallEffect noActivation() noexcept;
  ZC_NODISCARD static MirCallEffect activateMutableReceiver(MirLocalId temporary) noexcept;
  ZC_NODISCARD MirCallEffect clone() const noexcept;
  ZC_NODISCARD MirCallEffectKind kind() const noexcept;
  ZC_NODISCARD bool commitsOnNormalEdge() const noexcept;
  ZC_NODISCARD zc::Maybe<MirLocalId> activatedMutableReceiver() const noexcept;

private:
  explicit MirCallEffect(MirNoActivationCallEffect value) noexcept;
  explicit MirCallEffect(MirActivateMutableReceiverCallEffect value) noexcept;
  zc::OneOf<MirNoActivationCallEffect, MirActivateMutableReceiverCallEffect> value;
};

struct MirCallTerminator final {
  identity::DefId callee;
  zc::Vector<MirOperand> arguments;
  MirCallEffect effect;
  MirPlace destination;
  MirBlockId normalTarget;
  zc::Maybe<MirBlockId> unwindTarget;
};
struct MirGotoTerminator final {
  MirBlockId target;
};
struct MirSwitchIntArm final {
  checker::checked::CanonicalConstValue value;
  MirBlockId target;
};
struct MirSwitchIntTerminator final {
  MirOperand discriminant;
  zc::Vector<MirSwitchIntArm> arms;
  MirBlockId defaultTarget;
};

/// \brief Closed terminator algebra for the currently supported Built MIR subset.
class MirTerminator final {
public:
  MirTerminator(MirTerminator&&) noexcept = default;
  MirTerminator& operator=(MirTerminator&&) noexcept = default;
  ZC_DISALLOW_COPY(MirTerminator);

  ZC_NODISCARD static MirTerminator returnValue(MirOperand&& value,
                                                identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirTerminator returnVoid(identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirTerminator unreachable(identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirTerminator call(identity::DefId callee, zc::Vector<MirOperand>&& arguments,
                                         MirCallEffect&& effect, MirPlace&& destination,
                                         MirBlockId normalTarget,
                                         zc::Maybe<MirBlockId>&& unwindTarget,
                                         identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirTerminator gotoTarget(MirBlockId target,
                                               identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD static MirTerminator switchInt(MirOperand&& discriminant,
                                              zc::Vector<MirSwitchIntArm>&& arms,
                                              MirBlockId defaultTarget,
                                              identity::SourceSpan&& sourceSpan) noexcept;
  ZC_NODISCARD MirTerminator clone() const;
  ZC_NODISCARD MirTerminatorKind kind() const noexcept;
  /// \brief Returns presentation-only source ownership for this MIR terminator.
  ZC_NODISCARD const identity::SourceSpan& sourceSpan() const noexcept;
  ZC_NODISCARD const MirReturnTerminator& returnValue() const;
  ZC_NODISCARD const MirCallTerminator& callValue() const;
  ZC_NODISCARD const MirGotoTerminator& gotoValue() const;
  ZC_NODISCARD const MirSwitchIntTerminator& switchIntValue() const;

private:
  MirTerminator(MirReturnTerminator&& value, identity::SourceSpan&& sourceSpan) noexcept;
  MirTerminator(MirUnreachableTerminator value, identity::SourceSpan&& sourceSpan) noexcept;
  MirTerminator(MirCallTerminator&& value, identity::SourceSpan&& sourceSpan) noexcept;
  MirTerminator(MirGotoTerminator&& value, identity::SourceSpan&& sourceSpan) noexcept;
  MirTerminator(MirSwitchIntTerminator&& value, identity::SourceSpan&& sourceSpan) noexcept;
  zc::OneOf<MirReturnTerminator, MirUnreachableTerminator, MirCallTerminator, MirGotoTerminator,
            MirSwitchIntTerminator>
      value;
  identity::SourceSpan sourceSpanValue;
};

enum class MirLocalKind : uint8_t {
  ModuleInitializerResult = 0x01,
  Temporary = 0x02,
  FunctionResult = 0x03,
  UserLocal = 0x04,
  Parameter = 0x05,
};
enum class MirFunctionKind : uint8_t { ModuleInitializer = 0x01, Function = 0x02 };

struct MirSourceScope final {
  MirSourceScopeId id;
  zc::Maybe<MirSourceScopeId> parent;
  identity::SourceSpan sourceSpan;
};

struct MirLocalDeclaration final {
  MirLocalId id;
  MirLocalKind kind;
  identity::SemanticTypeId type;
  MirSourceScopeId sourceScope;
  identity::SourceSpan sourceSpan;
};

struct MirBasicBlock final {
  MirBlockId id;
  MirSourceScopeId sourceScope;
  zc::Vector<MirStatement> statements;
  MirTerminator terminator;
};

/// \brief One verified source-owned body in Built MIR.
struct MirFunction final {
  identity::DefId owner;
  MirFunctionKind kind;
  identity::DefinitionKind sourceDefinitionKind;
  identity::SemanticTypeId resultType;
  identity::SourceSpan sourceSpan;
  zc::Vector<MirSourceScope> sourceScopes;
  zc::Vector<MirLocalDeclaration> locals;
  zc::Vector<MirBasicBlock> blocks;
};

/// \brief Domain-separated immutable revision of one complete MIR module.
class MirRevisionId final {
public:
  constexpr MirRevisionId() noexcept = default;

  ZC_NODISCARD static MirRevisionId fromDigest(const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return digestValue; }

private:
  explicit MirRevisionId(const identity::Sha256Digest& digest) noexcept : digestValue(digest) {}

  identity::Sha256Digest digestValue;
};

/// \brief Exact canonical MIR revision framing codec.
class MirRevisionCodec final {
public:
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeBuiltFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const identity::Sha256Digest& checkedFactsRevision,
      const identity::Sha256Digest& dispatchFactsRevision,
      const identity::Sha256Digest& borrowEvidenceRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeBuilt(
      const identity::ContextFingerprint& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const checker::checked::CheckedFactsRevision& checkedFactsRevision,
      const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
      const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
  ZC_NODISCARD static zc::Maybe<MirRevisionId> computeBuilt(
      const identity::ContextFingerprint& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      const checker::checked::CheckedFactsRevision& checkedFactsRevision,
      const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision,
      const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctions);
};

/// \brief Untrusted mutable Built MIR product admitted only by the independent verifier.
class BuiltMirCandidate final {
public:
  BuiltMirCandidate(const hir::VerifiedHirModule& sourceHir, zc::Vector<MirFunction>&& functions,
                    zc::Vector<zc::Array<uint8_t>>&& canonicalFunctions,
                    MirRevisionId revision) noexcept;
  BuiltMirCandidate(BuiltMirCandidate&&) noexcept = default;
  BuiltMirCandidate& operator=(BuiltMirCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(BuiltMirCandidate);

  const hir::VerifiedHirModule& sourceHir;
  zc::Vector<MirFunction> functions;
  zc::Vector<zc::Array<uint8_t>> canonicalFunctions;
  MirRevisionId revision;
};

/// \brief Immutable target-independent Built MIR capability with exact frontend lineage.
class VerifiedBuiltMir final {
public:
  ~VerifiedBuiltMir() noexcept(false);
  VerifiedBuiltMir(VerifiedBuiltMir&&) noexcept;
  VerifiedBuiltMir& operator=(VerifiedBuiltMir&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBuiltMir);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const checker::checked::CheckedFactsRevision& checkedFactsRevision() const noexcept;
  ZC_NODISCARD const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision()
      const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::VerifiedBorrowEvidenceLease& borrowEvidenceLease()
      const noexcept;
  ZC_NODISCARD const MirRevisionId& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MirFunction> functions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const zc::Array<uint8_t>> canonicalFunctionRecords() const noexcept;

private:
  ZC_NODISCARD ownership::OwnershipAdmittedBoundModule retainAdmittedBoundModule() const;
  ZC_NODISCARD checker::CheckerIdentityAuthority retainIdentityAuthority() const;
  ZC_NODISCARD driver::borrow_evidence::VerifiedBorrowEvidenceLease retainBorrowEvidenceLease()
      const;
  ZC_NODISCARD driver::borrow_evidence::BorrowEvidenceRepositoryCapability
  retainBorrowEvidenceCapability() const;
  ZC_NODISCARD bool matchesBorrowEvidenceInput(
      const driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease,
      const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability) const noexcept;
  ZC_NODISCARD driver::borrow_evidence::BorrowEvidenceLookupResult borrowEvidence() const noexcept;
  struct Impl;
  explicit VerifiedBuiltMir(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class BuiltMirVerifier;
  friend class ownership::OwnershipEventOverlayBuilder;
  friend class ownership::OwnershipEventOverlayVerifier;
  friend class ownership::facts::InitializationSourceVerifier;
  friend class ownership::facts::BorrowSourceVerifier;
  friend class ownership::facts::FlowBuilder;
  friend class ownership::facts::FlowVerifier;
  friend class ownership::facts::InitializationBuilder;
  friend class ownership::facts::InitializationVerifier;
  friend class ownership::facts::LoanBuilder;
  friend class ownership::facts::LoanVerifier;
  friend class ownership::facts::MovePathBuilder;
  friend class ownership::facts::MovePathVerifier;
  friend class ownership::facts::OwnershipInputVerifier;
  friend class ownership::facts::OwnershipResourceBuilder;
  friend class ownership::facts::OwnershipResourceVerifier;
  friend class ownership::facts::ReborrowRegionBuilder;
  friend class ownership::facts::ReborrowRegionVerifier;
  friend class ownership::facts::ReborrowStateBuilder;
  friend class ownership::facts::ReborrowStateVerifier;
  friend class ownership::facts::ReferenceDefinitionBuilder;
  friend class ownership::facts::ReferenceDefinitionVerifier;
  friend class ownership::facts::RegionMembershipBuilder;
  friend class ownership::facts::RegionMembershipVerifier;
  friend class ownership::facts::EscapeBuilder;
  friend class ownership::facts::EscapeVerifier;
  friend class ownership::facts::CaptureBuilder;
  friend class ownership::facts::CaptureVerifier;
  friend class ownership::facts::RegionOutlivesBuilder;
  friend class ownership::facts::RegionOutlivesVerifier;
  friend class ownership::DropElaborator;
  friend class ownership::OwnershipFinalizer;
};

/// \brief Lowers the complete currently supported HIR expression slice into Built MIR.
class BuiltMirBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<BuiltMirCandidate> build(const BuiltMirInput& input);
};

/// \brief Sole publisher of immutable revision-checked Built MIR modules.
class BuiltMirVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedBuiltMir> verify(BuiltMirCandidate&& candidate,
                                                                     const BuiltMirInput& input);
};

}  // namespace zomlang::compiler::mir
