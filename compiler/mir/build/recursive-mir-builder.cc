// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/recursive-mir-builder.h"

#include "compiler/checker/body/marker-proof.h"
#include "compiler/hir/hir-internal.h"
#include "compiler/mir/build/hir-node-resolver.h"
#include "compiler/mir/build/mir-builder.h"

namespace zomlang::compiler::mir {

using hir::HirNodeId;
using hir::VerifiedHirModule;

namespace {

/// Finds the HIR record for a node by kind, returning a const pointer.
template <typename Record>
const Record* findRecord(zc::ArrayPtr<const Record> records, HirNodeId node) {
  for (const auto& record : records) {
    if (record.node == node) return &record;
  }
  return nullptr;
}

/// Resolves a place use to a copy or move operand from the Copy marker proof.
/// Duplicated from mir-builder.cc (anonymous namespace) for the recursive builder.
zc::Maybe<MirOperand> placeUse(checker::marker::MarkerProofEngine& proofs,
                                identity::DefId copyMarker, MirPlace&& place) {
  auto result = proofs.prove(copyMarker, place.resultType());
  if (result.is<checker::marker::MarkerProofPositive>()) {
    return MirOperand::copy(zc::mv(place));
  }
  if (result.is<checker::marker::MarkerProofNegative>() ||
      result.is<checker::marker::MarkerProofUnsatisfied>()) {
    return MirOperand::move(zc::mv(place));
  }
  return zc::none;
}

/// Finds the source index of the parameter matching a parameter-reference key.
/// Duplicated from mir-builder.cc (anonymous namespace) for the recursive builder.
zc::Maybe<size_t> parameterIndexFor(const hir::HirFunctionDeclaration& declaration,
                                     const identity::CallableParameterKey& parameter) {
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == parameter) return i;
  }
  return zc::none;
}

/// Lowers a scalar literal expression to a constant operand.
zc::Maybe<MirOperand> lowerScalarLiteral(const hir::HirScalarLiteralExpression& literal,
                                          identity::SemanticTypeId resultType) {
  return MirOperand::constant(resultType, literal.value.clone());
}

/// Lowers a parameter reference to a copy/move place-use operand.
zc::Maybe<MirOperand> lowerParameterReference(const hir::HirParameterReferenceExpression& paramRef,
                                               const hir::HirFunctionDeclaration& declaration,
                                               bool isMethod,
                                               checker::marker::MarkerProofEngine& proofs,
                                               identity::DefId copyMarker) {
  auto paramIndex = parameterIndexFor(declaration, paramRef.parameter);
  if (paramIndex == zc::none) return zc::none;
  const uint32_t localOrdinal =
      static_cast<uint32_t>(ZC_ASSERT_NONNULL(paramIndex) + 1 + (isMethod ? 1 : 0));
  auto localId = MirLocalId::fromOrdinal(localOrdinal);
  if (localId == zc::none) return zc::none;
  zc::Vector<MirProjection> projections;
  return placeUse(proofs, copyMarker,
                  MirPlace(ZC_ASSERT_NONNULL(localId), paramRef.type, zc::mv(projections),
                           paramRef.type));
}

/// Lowers a local reference to a copy/move place-use operand.
zc::Maybe<MirOperand> lowerLocalReference(const hir::HirLocalReferenceExpression& localRef,
                                           MirLocalId localId,
                                           checker::marker::MarkerProofEngine& proofs,
                                           identity::DefId copyMarker) {
  zc::Vector<MirProjection> projections;
  return placeUse(proofs, copyMarker,
                  MirPlace(localId, localRef.type, zc::mv(projections), localRef.type));
}

}  // namespace

zc::Maybe<MirFunction> buildRecursiveFunction(const VerifiedHirModule& hirModule,
                                                const hir::HirFunctionDeclaration& declaration,
                                                const checker::CheckerIdentityAuthority& identities,
                                                checker::marker::MarkerProofEngine& proofs,
                                                zc::Maybe<identity::DefId> copyMarker) {
  // The recursive builder walks the HIR tree structurally: function -> block
  // -> statements -> expressions. This first slice handles:
  //   - Scalar literal return: fn f() -> T { return <literal>; }
  //   - Parameter reference return: fn f(p: T) -> T { return p; }
  //   - Local reference return: fn f() -> T { let x = <literal>; return x; }
  // More families are added incrementally, each gated on corpus parity.
  //
  // Find the body block.
  const hir::HirBlockStatement* block = findRecord(hirModule.blocks(), declaration.body);
  if (block == nullptr) return zc::none;
  if (block->statements.size() == 0) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());

  // Declare receiver first (if method), then parameter locals. This matches
  // the shape strategy table's allocation order for byte-parity.
  const bool isMethod = declaration.receiver != zc::none;
  ZC_IF_SOME(receiver, declaration.receiver) {
    ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());
  }
  for (const auto& param : declaration.parameters) {
    ctx.declareLocal(MirLocalKind::Parameter, param.type, scope, param.sourceSpan.clone());
  }

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;

  // Track declared user locals by HIR local id (for local reference resolution).
  // The map is built as we walk LocalBinding statements. Linear scan is
  // acceptable: functions have few locals.
  struct UserLocalEntry {
    hir::HirLocalId hirId;
    MirLocalId mirId;
  };
  zc::Vector<UserLocalEntry> userLocals;
  const auto findUserLocal = [&](hir::HirLocalId id) -> zc::Maybe<MirLocalId> {
    for (const auto& entry : userLocals) {
      if (entry.hirId == id) return entry.mirId;
    }
    return zc::none;
  };

  // Walk statements in source order. The last statement must be a Return.
  for (size_t i = 0; i < block->statements.size(); ++i) {
    const HirNodeId stmtNode = block->statements[i];
    zc::Maybe<HirNodeKind> stmtKind = lookupHirNodeKind(hirModule, stmtNode);
    if (stmtKind == zc::none) return zc::none;

    if (ZC_ASSERT_NONNULL(stmtKind) == HirNodeKind::LocalBinding) {
      // Local binding: declare a UserLocal, emit StorageLive, lower the
      // initializer into the local.
      const auto* binding = findRecord(hirModule.locals(), stmtNode);
      if (binding == nullptr) return zc::none;
      if (binding->initializer == zc::none) return zc::none;  // Uninitialized locals not yet handled.

      const MirLocalId userLocal =
          ctx.declareLocal(MirLocalKind::UserLocal, binding->type, scope,
                           binding->sourceSpan.clone());
      userLocals.add(UserLocalEntry{binding->local, userLocal});

      ctx.appendStatement(MirStatement::storageLive(userLocal, binding->sourceSpan.clone()));

      // Lower the initializer expression.
      const HirNodeId initNode = ZC_ASSERT_NONNULL(binding->initializer);
      zc::Maybe<HirNodeKind> initKind = lookupHirNodeKind(hirModule, initNode);
      if (initKind == zc::none) return zc::none;

      if (ZC_ASSERT_NONNULL(initKind) != HirNodeKind::ScalarLiteral) return zc::none;
      const auto* literal = findRecord(hirModule.expressions(), initNode);
      if (literal == nullptr) return zc::none;
      auto initOperand = lowerScalarLiteral(*literal, binding->type);
      if (initOperand == zc::none) return zc::none;

      zc::Vector<MirProjection> destProjections;
      ctx.appendStatement(MirStatement::assign(
          MirPlace(userLocal, binding->type, zc::mv(destProjections), binding->type),
          MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(initOperand))),
          MirInitializationKind::Initialize, literal->sourceSpan.clone()));
    } else if (ZC_ASSERT_NONNULL(stmtKind) == HirNodeKind::Return) {
      // Return statement: lower the return value expression and emit a Return
      // terminator. This must be the last statement.
      if (i != block->statements.size() - 1) return zc::none;
      const auto* returnStmt = findRecord(hirModule.returns(), stmtNode);
      if (returnStmt == nullptr) return zc::none;

      const HirNodeId valueNode = returnStmt->value;
      zc::Maybe<HirNodeKind> valueKind = lookupHirNodeKind(hirModule, valueNode);
      if (valueKind == zc::none) return zc::none;

      MirOperand returnOperand = MirOperand::constant(
          declaration.resultType, checker::checked::CanonicalConstValue::unit());
      bool handled = false;

      switch (ZC_ASSERT_NONNULL(valueKind)) {
        case HirNodeKind::ScalarLiteral: {
          const auto* literal = findRecord(hirModule.expressions(), valueNode);
          if (literal == nullptr) return zc::none;
          auto operand = lowerScalarLiteral(*literal, declaration.resultType);
          if (operand == zc::none) return zc::none;
          returnOperand = zc::mv(ZC_ASSERT_NONNULL(operand));
          handled = true;
          break;
        }
        case HirNodeKind::ParameterReference: {
          if (copyMarker == zc::none) return zc::none;
          const auto* paramRef = findRecord(hirModule.parameterReferences(), valueNode);
          if (paramRef == nullptr) return zc::none;
          auto operand = lowerParameterReference(*paramRef, declaration, isMethod,
                                                  proofs,
                                                  ZC_ASSERT_NONNULL(copyMarker));
          if (operand == zc::none) return zc::none;
          returnOperand = zc::mv(ZC_ASSERT_NONNULL(operand));
          handled = true;
          break;
        }
        case HirNodeKind::LocalReference: {
          if (copyMarker == zc::none) return zc::none;
          const auto* localRef = findRecord(hirModule.localReferences(), valueNode);
          if (localRef == nullptr) return zc::none;
          // Find the user local for this reference.
          auto mirLocal = findUserLocal(localRef->local);
          if (mirLocal == zc::none) return zc::none;
          auto operand = lowerLocalReference(*localRef, ZC_ASSERT_NONNULL(mirLocal),
                                              proofs,
                                              ZC_ASSERT_NONNULL(copyMarker));
          if (operand == zc::none) return zc::none;
          returnOperand = zc::mv(ZC_ASSERT_NONNULL(operand));
          handled = true;
          break;
        }
        default:
          break;
      }

      if (!handled) return zc::none;

      ctx.terminateBlock(
          MirTerminator::returnValue(zc::mv(returnOperand), returnStmt->sourceSpan.clone()));
    } else {
      // Unsupported statement kind.
      return zc::none;
    }
  }

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  return ctx.finish(declaration.definition, MirFunctionKind::Function,
                    isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function,
                    declaration.resultType, declaration.sourceSpan.clone());
}

}  // namespace zomlang::compiler::mir
