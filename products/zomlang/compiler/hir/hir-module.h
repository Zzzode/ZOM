// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/hir/checked-module.h"
#include "zomlang/compiler/hir/hir-node-id.h"
#include "zomlang/compiler/type/semantic-type-data.h"

namespace zomlang::compiler::mir {
class BuiltMirBuilder;
class BuiltMirVerifier;
}  // namespace zomlang::compiler::mir

namespace zomlang::compiler::ownership {
class OwnershipAdmittedBoundModule;
}  // namespace zomlang::compiler::ownership

namespace zomlang::compiler::hir {

struct ModuleHirVisibility final {
  identity::ModuleId module;
};
struct ExternalHirVisibility final {};

enum class HirVisibilityKind : uint8_t { Module = 0x01, External = 0x02 };

/// \brief Closed source visibility retained without binder-owned lookup state.
class HirVisibility final {
public:
  ZC_NODISCARD static HirVisibility module(identity::ModuleId module) noexcept;
  ZC_NODISCARD static HirVisibility external() noexcept;
  HirVisibility(HirVisibility&&) noexcept = default;
  HirVisibility& operator=(HirVisibility&&) noexcept = default;
  ZC_DISALLOW_COPY(HirVisibility);

  ZC_NODISCARD HirVisibility clone() const noexcept;
  ZC_NODISCARD HirVisibilityKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<identity::ModuleId> visibleModule() const noexcept;

private:
  explicit HirVisibility(ModuleHirVisibility value) noexcept : value(value) {}
  explicit HirVisibility(ExternalHirVisibility value) noexcept : value(value) {}
  zc::OneOf<ModuleHirVisibility, ExternalHirVisibility> value;
};

enum class HirLinkage : uint8_t {
  Internal = 0x01,
  ExternalCdecl = 0x02,
  ExternalStdcall = 0x03,
  ExternalZomNative = 0x04
};

enum class HirValueCategory : uint8_t { Value = 0x01, Place = 0x02 };

/// \brief One module-owned scalar binding pattern in immutable semantic HIR.
struct HirBindingPattern final {
  HirNodeId node;
  identity::DefId binding;
  identity::SemanticTypeId type;
  identity::SemanticTypeId scrutineeType;
  bool reachable;
  identity::SourceSpan sourceSpan;
};

/// \brief One exact scalar literal expression copied from verified checked facts.
struct HirScalarLiteralExpression final {
  HirNodeId node;
  identity::SemanticTypeId type;
  checker::checked::CanonicalConstValue value;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

/// \brief One field value admitted into a closed nominal aggregate expression.
struct HirNominalAggregateElement final {
  identity::DefId field;
  identity::SemanticTypeId type;
  checker::checked::CanonicalConstValue value;
  identity::SourceSpan sourceSpan;
};

/// \brief One verified closed nominal aggregate expression.
struct HirNominalAggregateExpression final {
  HirNodeId node;
  identity::DefId definition;
  identity::SemanticTypeId type;
  zc::Vector<HirNominalAggregateElement> elements;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

/// \brief One function-owned local binding with a layer-local identity.
struct HirLocalBinding final {
  HirNodeId node;
  HirLocalId local;
  identity::SemanticTypeId type;
  zc::Maybe<HirNodeId> initializer;
  identity::SourceSpan sourceSpan;
  zc::Maybe<identity::SourceSpan> initializerSpan;
};

/// \brief One checked use of a function-local binding as a place expression.
struct HirLocalReferenceExpression final {
  HirNodeId node;
  HirLocalId local;
  identity::SemanticTypeId type;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

/// \brief One checked field projection rooted at a function-local binding.
struct HirLocalFieldProjectionExpression final {
  HirNodeId node;
  HirLocalId local;
  identity::DefId field;
  identity::SemanticTypeId receiverType;
  identity::SemanticTypeId type;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

enum class HirLocalWriteKind : uint8_t { Initialize = 0x01, Overwrite = 0x02 };

/// \brief One verified scalar write to a mutable function-local place.
struct HirLocalWriteStatement final {
  HirNodeId node;
  HirLocalId local;
  zc::Maybe<identity::DefId> field;
  identity::SemanticTypeId type;
  HirNodeId value;
  HirLocalWriteKind kind;
  identity::SourceSpan sourceSpan;
  identity::SourceSpan valueSpan;
};

/// \brief One immutable function parameter retained from its checked signature.
struct HirParameter final {
  identity::CallableParameterKey key;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
};

/// \brief One checked use of a function parameter as a place expression.
struct HirParameterReferenceExpression final {
  HirNodeId node;
  identity::CallableParameterKey parameter;
  identity::SemanticTypeId type;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

/// \brief One checked read-only constant index projection rooted at a function parameter.
struct HirParameterIndexExpression final {
  HirNodeId node;
  identity::CallableParameterKey parameter;
  identity::SemanticTypeId receiverType;
  identity::SemanticTypeId indexType;
  checker::checked::CanonicalConstValue index;
  identity::SemanticTypeId type;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
  identity::SourceSpan indexSpan;
};

/// \brief One checked reborrow rooted in a reference parameter.
struct HirParameterReborrowExpression final {
  HirNodeId node;
  identity::CallableParameterKey parameter;
  zc::Maybe<HirLocalId> sourceAlias;
  identity::SemanticTypeId sourceType;
  identity::SemanticTypeId type;
  type::semantic::Mutability mutability;
  identity::SourceSpan sourceSpan;
};

/// \brief One checked borrow of a function-local binding without a dereference projection.
///
/// Unlike a parameter reborrow, the borrow source is the local itself, so the MIR
/// BorrowCreation source place carries zero projections.
struct HirLocalBorrowExpression final {
  HirNodeId node;
  HirLocalId local;
  identity::SemanticTypeId sourceType;
  identity::SemanticTypeId type;
  type::semantic::Mutability mutability;
  identity::SourceSpan sourceSpan;
};

/// \brief One verified scalar constant argument retained by a direct call expression.
struct HirDirectCallArgument final {
  identity::SemanticTypeId type;
  checker::checked::CanonicalConstValue value;
  identity::SourceSpan sourceSpan;
};

/// \brief One verified direct call expression in semantic HIR.
struct HirDirectCallExpression final {
  HirNodeId node;
  identity::DefId callee;
  identity::SemanticTypeId calleeType;
  identity::SemanticTypeId resultType;
  zc::Vector<HirDirectCallArgument> arguments;
  identity::SourceSpan sourceSpan;
};

/// \brief One checked receiver-call expression retained before ownership lowering.
///
/// The receiver node is evaluated before every explicit argument. Mutable receiver
/// calls retain their checker-selected adjustment sequence so MIR can create the
/// temporary borrow before recording its normal-edge activation at the call.
struct HirReceiverCallExpression final {
  HirNodeId node;
  HirNodeId receiver;
  identity::DefId callee;
  identity::SemanticTypeId calleeType;
  identity::SemanticTypeId receiverSourceType;
  identity::SemanticTypeId receiverType;
  checker::checked::ReceiverMode receiverMode;
  zc::Vector<checker::checked::ReceiverAdjustmentStep> receiverAdjustments;
  identity::SemanticTypeId resultType;
  zc::Vector<HirDirectCallArgument> arguments;
  identity::SourceSpan sourceSpan;
};

/// \brief One checked unsafe block expression retained for ownership boundary lowering.
///
/// The body is the scalar expression produced by the block tail; MIR lowering wraps it in
/// Enter/Exit unsafe-scope boundary statements so the ownership overlay can acknowledge the
/// unsafe region.
struct HirUnsafeBlockExpression final {
  HirNodeId node;
  HirNodeId body;
  identity::SemanticTypeId type;
  identity::SourceSpan sourceSpan;
};

/// \brief One checked if/else conditional expression retained for multi-block MIR lowering.
///
/// The condition is a bool expression; each branch yields a scalar return value. MIR lowering
/// emits a SwitchInt terminator on the entry block, one block per branch, and a Return
/// terminator in each branch block.
struct HirConditionalExpression final {
  HirNodeId node;
  HirNodeId condition;
  HirNodeId thenReturnValue;
  HirNodeId elseReturnValue;
  identity::SemanticTypeId type;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

/// \brief One admitted `while` loop retained for reducible multi-block MIR lowering.
///
/// The condition is a bool parameter reference and the loop body is empty. MIR lowering emits a
/// header block whose SwitchInt terminator branches to the body on a true discriminant and to the
/// exit otherwise; the body block jumps back to the header, forming a reducible back-edge.
struct HirLoopStatement final {
  HirNodeId node;
  HirNodeId condition;
  identity::SemanticTypeId type;
  HirValueCategory category;
  identity::SourceSpan sourceSpan;
};

/// \brief One scalar return statement in immutable semantic HIR.
struct HirReturnStatement final {
  HirNodeId node;
  identity::SemanticTypeId resultType;
  HirNodeId value;
  identity::SourceSpan sourceSpan;
};

/// \brief One closed lexical block in immutable semantic HIR.
struct HirBlockStatement final {
  HirNodeId node;
  zc::Vector<HirNodeId> statements;
  identity::SourceSpan sourceSpan;
};

/// \brief One module-scope function with a verified scalar return body.
struct HirFunctionDeclaration final {
  HirNodeId node;
  identity::DefId definition;
  identity::SemanticTypeId resultType;
  zc::Vector<HirParameter> parameters;
  HirVisibility visibility;
  HirLinkage linkage;
  identity::SourceSpan sourceSpan;
  HirNodeId body;
  zc::Maybe<HirNodeId> unsafeBlock;
};

/// \brief One module-scope let, mut, or const declaration in semantic HIR.
struct HirValueDeclaration final {
  HirNodeId node;
  identity::DefId definition;
  identity::DefinitionKind definitionKind;
  identity::SemanticTypeId declaredType;
  identity::SemanticTypeId inferredType;
  type::semantic::Mutability mutability;
  HirVisibility visibility;
  HirLinkage linkage;
  identity::SourceSpan sourceSpan;
  HirNodeId pattern;
  HirNodeId initializer;
  zc::Maybe<checker::checked::CanonicalConstValue> constantValue;
};

/// \brief Private mutable build product admitted only by the HIR verifier.
class HirModuleCandidate final {
public:
  ~HirModuleCandidate() noexcept(false);
  HirModuleCandidate(HirModuleCandidate&&) noexcept;
  HirModuleCandidate& operator=(HirModuleCandidate&&) noexcept;
  ZC_DISALLOW_COPY(HirModuleCandidate);

private:
  struct Impl;
  explicit HirModuleCandidate(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class HirBuilder;
  friend class HirVerifier;
};

/// \brief NodeId-free immutable semantic HIR module capability.
class VerifiedHirModule final {
public:
  ~VerifiedHirModule() noexcept(false);
  VerifiedHirModule(VerifiedHirModule&&) noexcept;
  VerifiedHirModule& operator=(VerifiedHirModule&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedHirModule);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceContentDigest() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& parsedModuleReceiptDigest() const noexcept;
  ZC_NODISCARD const checker::checked::CheckedFactsRevision& checkedFactsRevision() const noexcept;
  ZC_NODISCARD const checker::dispatch::DispatchFactsRevision& dispatchFactsRevision()
      const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD const ModuleInterfaceLineage& ownInterface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleInterfaceLineage> visibleImportedInterfaces()
      const noexcept;
  ZC_NODISCARD const checker::checked::CheckedEvidenceLease& checkedEvidenceLease() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::VerifiedBorrowEvidenceLease& borrowEvidenceLease()
      const noexcept;
  /// \brief Returns the admitted checked-module capability retained by this HIR module.
  ZC_NODISCARD const VerifiedCheckedModule& admittedCheckedModule() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirValueDeclaration> declarations() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirFunctionDeclaration> functions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirBlockStatement> blocks() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirReturnStatement> returns() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirBindingPattern> patterns() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirScalarLiteralExpression> expressions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirNominalAggregateExpression> aggregates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirLocalBinding> locals() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirLocalWriteStatement> localWrites() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirLocalReferenceExpression> localReferences() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirLocalFieldProjectionExpression> localFieldProjections()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirParameterReferenceExpression> parameterReferences()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirParameterIndexExpression> parameterIndexes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirParameterReborrowExpression> parameterReborrows()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirLocalBorrowExpression> localBorrows() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirDirectCallExpression> calls() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirReceiverCallExpression> receiverCalls() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirUnsafeBlockExpression> unsafeBlocks() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirConditionalExpression> conditionals() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const HirLoopStatement> loops() const noexcept;
  ZC_NODISCARD zc::Maybe<zc::String> dump() const;

private:
  ZC_NODISCARD ownership::OwnershipAdmittedBoundModule retainAdmittedBoundModule() const;
  ZC_NODISCARD checker::CheckerIdentityAuthority retainIdentityAuthority() const;
  ZC_NODISCARD driver::borrow_evidence::BorrowEvidenceRepositoryCapability
  borrowEvidenceCapability() const noexcept;
  ZC_NODISCARD const type::SemanticTypeStore& semanticTypes() const noexcept;

  struct Impl;
  explicit VerifiedHirModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class HirVerifier;
  friend class mir::BuiltMirBuilder;
  friend class mir::BuiltMirVerifier;
};

/// \brief Lowers only complete verified frontend handoffs into HIR candidates.
class HirBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<HirModuleCandidate> build(
      VerifiedCheckedModule&& checkedModule);
};

/// \brief Sole publisher of immutable NodeId-free HIR modules.
class HirVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedHirModule> verify(
      HirModuleCandidate&& candidate);
};

}  // namespace zomlang::compiler::hir
