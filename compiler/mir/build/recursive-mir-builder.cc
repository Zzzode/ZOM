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
  if (result.is<checker::marker::MarkerProofPositive>()) { return MirOperand::copy(zc::mv(place)); }
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
  return placeUse(
      proofs, copyMarker,
      MirPlace(ZC_ASSERT_NONNULL(localId), paramRef.type, zc::mv(projections), paramRef.type));
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

/// Maps a primitive operation to its MIR relational operator.
zc::Maybe<MirComparisonOperator> comparisonOperatorFor(
    checker::PrimitiveOperation operation) noexcept {
  switch (operation) {
    case checker::PrimitiveOperation::Eq:
      return MirComparisonOperator::Eq;
    case checker::PrimitiveOperation::Ne:
      return MirComparisonOperator::Ne;
    case checker::PrimitiveOperation::Lt:
      return MirComparisonOperator::Lt;
    case checker::PrimitiveOperation::Le:
      return MirComparisonOperator::Le;
    case checker::PrimitiveOperation::Gt:
      return MirComparisonOperator::Gt;
    case checker::PrimitiveOperation::Ge:
      return MirComparisonOperator::Ge;
    default:
      return zc::none;
  }
}

}  // namespace

zc::Maybe<MirFunction> buildRecursiveFunction(const VerifiedHirModule& hirModule,
                                              const hir::HirFunctionDeclaration& declaration,
                                              const checker::CheckerIdentityAuthority& identities,
                                              checker::marker::MarkerProofEngine& proofs,
                                              zc::Maybe<identity::DefId> copyMarker) {
  // The recursive builder walks the HIR tree structurally: function -> block
  // -> statements -> expressions. It handles:
  //   - Scalar literal return: fn f() -> T { return <literal>; }
  //   - Parameter reference return: fn f(p: T) -> T { return p; }
  //   - Local reference return: fn f() -> T { let x = <literal>; return x; }
  //   - Scalar local writes: a binding with or without an initializer followed
  //     by one or more plain writes whose value is a scalar literal or a
  //     parameter copy (`mut v: T; v = <literal/param>; ...; return v;`). The
  //     HIR write kind (Initialize/Overwrite) is transferred verbatim.
  // More families are added incrementally, each gated on corpus parity.
  //
  // Find the body block.
  const hir::HirBlockStatement* block = findRecord(hirModule.blocks(), declaration.body);
  if (block == nullptr) return zc::none;
  if (block->statements.size() == 0) return zc::none;
  // This builder walks statements structurally and does not model unsafe
  // scopes: it would lower `return unsafe { *ptr };` as a bare parameter
  // reference, returning the pointer where the pointee is required. Every
  // unsafe-wrapped body belongs to a shape strategy that emits the scope
  // boundaries and the dereference projection, so decline the function.
  if (declaration.unsafeBlock != zc::none) return zc::none;

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
    if (stmtKind == zc::none) { return zc::none; }

    if (ZC_ASSERT_NONNULL(stmtKind) == HirNodeKind::LocalBinding) {
      // Local binding: declare a UserLocal and emit StorageLive. When the
      // binding has an initializer, lower it into the local; an
      // initializer-less binding waits for its later Initialize write.
      const auto* binding = findRecord(hirModule.locals(), stmtNode);
      if (binding == nullptr) return zc::none;

      const MirLocalId userLocal = ctx.declareLocal(MirLocalKind::UserLocal, binding->type, scope,
                                                    binding->sourceSpan.clone());
      userLocals.add(UserLocalEntry{binding->local, userLocal});

      ctx.appendStatement(MirStatement::storageLive(userLocal, binding->sourceSpan.clone()));

      if (binding->initializer == zc::none) continue;

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
          MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(initOperand))), MirInitializationKind::Initialize,
          literal->sourceSpan.clone()));
    } else if (ZC_ASSERT_NONNULL(stmtKind) == HirNodeKind::LocalWrite) {
      // Plain scalar write to a user local: `v = <literal>` or `v = p`. The
      // HIR verifier already bounded the shape (no field projection, scalar
      // value, target is a declared local); the Initialize/Overwrite kind is
      // transferred verbatim.
      const auto* write = findRecord(hirModule.localWrites(), stmtNode);
      if (write == nullptr) return zc::none;
      if (write->field != zc::none) return zc::none;
      auto target = findUserLocal(write->local);
      if (target == zc::none) return zc::none;

      zc::Maybe<HirNodeKind> valueKind = lookupHirNodeKind(hirModule, write->value);
      if (valueKind == zc::none) return zc::none;

      zc::Maybe<MirOperand> writeOperand;
      if (ZC_ASSERT_NONNULL(valueKind) == HirNodeKind::ScalarLiteral) {
        const auto* literal = findRecord(hirModule.expressions(), write->value);
        if (literal == nullptr) return zc::none;
        writeOperand = lowerScalarLiteral(*literal, write->type);
      } else if (ZC_ASSERT_NONNULL(valueKind) == HirNodeKind::ParameterReference) {
        if (copyMarker == zc::none) return zc::none;
        const auto* paramRef = findRecord(hirModule.parameterReferences(), write->value);
        if (paramRef == nullptr) return zc::none;
        writeOperand = lowerParameterReference(*paramRef, declaration, isMethod, proofs,
                                               ZC_ASSERT_NONNULL(copyMarker));
      } else {
        return zc::none;
      }
      if (writeOperand == zc::none) return zc::none;

      const MirInitializationKind initialization = write->kind == hir::HirLocalWriteKind::Initialize
                                                       ? MirInitializationKind::Initialize
                                                       : MirInitializationKind::Overwrite;
      zc::Vector<MirProjection> writeProjections;
      ctx.appendStatement(MirStatement::assign(
          MirPlace(ZC_ASSERT_NONNULL(target), write->type, zc::mv(writeProjections), write->type),
          MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(writeOperand))), initialization,
          write->sourceSpan.clone()));
    } else if (ZC_ASSERT_NONNULL(stmtKind) == HirNodeKind::ConditionalLocalWrite) {
      // Definite-assignment diamond: `if (c) { v = a; } else { v = b; }`. The
      // StorageLive for the target was already emitted by the preceding
      // LocalBinding arm in the (open) entry block. Evaluate the condition into
      // a discriminant, terminate the entry with a SwitchInt, lower each arm to
      // a per-path Initialize of the same user local followed by a Goto, then
      // open the join block so the trailing Return statement terminates it.
      const auto* diamond = findRecord(hirModule.conditionalLocalWrites(), stmtNode);
      if (diamond == nullptr) return zc::none;
      const bool thenPresent = diamond->thenWrite != HirNodeId{};
      const bool elsePresent = diamond->elseWrite != HirNodeId{};
      if (!thenPresent && !elsePresent) return zc::none;
      const auto* thenWriteRecord =
          thenPresent ? findRecord(hirModule.localWrites(), diamond->thenWrite) : nullptr;
      const auto* elseWriteRecord =
          elsePresent ? findRecord(hirModule.localWrites(), diamond->elseWrite) : nullptr;
      if ((thenPresent && thenWriteRecord == nullptr) ||
          (elsePresent && elseWriteRecord == nullptr)) {
        return zc::none;
      }
      if ((thenPresent && thenWriteRecord->field != zc::none) ||
          (elsePresent && elseWriteRecord->field != zc::none) ||
          (thenPresent && thenWriteRecord->kind != hir::HirLocalWriteKind::Initialize) ||
          (elsePresent && elseWriteRecord->kind != hir::HirLocalWriteKind::Initialize)) {
        return zc::none;
      }
      // Both present arms target the same user local.
      if (thenPresent && elsePresent && thenWriteRecord->local != elseWriteRecord->local) {
        return zc::none;
      }
      const auto targetHirLocal = thenPresent ? thenWriteRecord->local : elseWriteRecord->local;
      const auto armType = thenPresent ? thenWriteRecord->type : elseWriteRecord->type;
      auto target = findUserLocal(targetHirLocal);
      if (target == zc::none) return zc::none;

      // Lower one scalar arm value (literal or parameter copy) to a use rvalue.
      auto armRvalue = [&](HirNodeId valueNode,
                           identity::SemanticTypeId valueType) -> zc::Maybe<MirRvalue> {
        auto valueKind = lookupHirNodeKind(hirModule, valueNode);
        if (valueKind == zc::none) return zc::none;
        zc::Maybe<MirOperand> operand;
        if (ZC_ASSERT_NONNULL(valueKind) == HirNodeKind::ScalarLiteral) {
          const auto* literal = findRecord(hirModule.expressions(), valueNode);
          if (literal == nullptr) return zc::none;
          operand = lowerScalarLiteral(*literal, valueType);
        } else if (ZC_ASSERT_NONNULL(valueKind) == HirNodeKind::ParameterReference) {
          if (copyMarker == zc::none) return zc::none;
          const auto* paramRef = findRecord(hirModule.parameterReferences(), valueNode);
          if (paramRef == nullptr) return zc::none;
          operand = lowerParameterReference(*paramRef, declaration, isMethod, proofs,
                                            ZC_ASSERT_NONNULL(copyMarker));
        } else {
          return zc::none;
        }
        if (operand == zc::none) return zc::none;
        return MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      };
      zc::Maybe<MirRvalue> thenRvalue;
      zc::Maybe<MirRvalue> elseRvalue;
      if (thenPresent) thenRvalue = armRvalue(thenWriteRecord->value, thenWriteRecord->type);
      if (elsePresent) elseRvalue = armRvalue(elseWriteRecord->value, elseWriteRecord->type);
      if ((thenPresent && thenRvalue == zc::none) || (elsePresent && elseRvalue == zc::none)) {
        return zc::none;
      }

      // Build the bool discriminant operand in the open entry block.
      zc::Maybe<HirNodeKind> conditionKind = lookupHirNodeKind(hirModule, diamond->condition);
      if (conditionKind == zc::none) return zc::none;
      zc::Maybe<MirOperand> discriminant;
      if (ZC_ASSERT_NONNULL(conditionKind) == HirNodeKind::ParameterReference) {
        if (copyMarker == zc::none) return zc::none;
        const auto* paramRef = findRecord(hirModule.parameterReferences(), diamond->condition);
        if (paramRef == nullptr) return zc::none;
        discriminant = lowerParameterReference(*paramRef, declaration, isMethod, proofs,
                                               ZC_ASSERT_NONNULL(copyMarker));
      } else if (ZC_ASSERT_NONNULL(conditionKind) == HirNodeKind::PrimitiveBinary) {
        const auto* comparison =
            findRecord(hirModule.primitiveBinaryOperations(), diamond->condition);
        if (comparison == nullptr) return zc::none;
        auto operation = comparisonOperatorFor(comparison->operation);
        if (operation == zc::none) return zc::none;
        auto leftOperand = [&] {
          const auto* leftLiteral = findRecord(hirModule.expressions(), comparison->left);
          if (leftLiteral != nullptr)
            return lowerScalarLiteral(*leftLiteral, comparison->operandType);
          const auto* leftParam = findRecord(hirModule.parameterReferences(), comparison->left);
          if (leftParam == nullptr || copyMarker == zc::none)
            return zc::Maybe<MirOperand>{zc::none};
          return lowerParameterReference(*leftParam, declaration, isMethod, proofs,
                                         ZC_ASSERT_NONNULL(copyMarker));
        }();
        auto rightOperand = [&] {
          const auto* rightLiteral = findRecord(hirModule.expressions(), comparison->right);
          if (rightLiteral != nullptr)
            return lowerScalarLiteral(*rightLiteral, comparison->operandType);
          const auto* rightParam = findRecord(hirModule.parameterReferences(), comparison->right);
          if (rightParam == nullptr || copyMarker == zc::none)
            return zc::Maybe<MirOperand>{zc::none};
          return lowerParameterReference(*rightParam, declaration, isMethod, proofs,
                                         ZC_ASSERT_NONNULL(copyMarker));
        }();
        if (leftOperand == zc::none || rightOperand == zc::none) return zc::none;
        const MirLocalId conditionTemp = ctx.declareLocal(MirLocalKind::Temporary, comparison->type,
                                                          scope, comparison->sourceSpan.clone());
        ctx.appendStatement(
            MirStatement::storageLive(conditionTemp, comparison->sourceSpan.clone()));
        zc::Vector<MirProjection> tempProjections;
        ctx.appendStatement(MirStatement::assign(
            MirPlace(conditionTemp, comparison->type, zc::mv(tempProjections), comparison->type),
            MirRvalue::comparison(ZC_ASSERT_NONNULL(operation),
                                  zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                  zc::mv(ZC_ASSERT_NONNULL(rightOperand)), comparison->type),
            MirInitializationKind::Initialize, comparison->sourceSpan.clone()));
        if (copyMarker == zc::none) return zc::none;
        zc::Vector<MirProjection> discriminantProjections;
        discriminant = placeUse(proofs, ZC_ASSERT_NONNULL(copyMarker),
                                MirPlace(conditionTemp, comparison->type,
                                         zc::mv(discriminantProjections), comparison->type));
      } else {
        return zc::none;
      }
      if (discriminant == zc::none) return zc::none;

      const MirBlockId thenBlock = ctx.peekBlockIdAt(0);
      const MirBlockId elseBlock = ctx.peekBlockIdAt(1);
      const MirBlockId joinBlock = ctx.peekBlockIdAt(2);
      zc::Vector<MirSwitchIntArm> switchArms;
      switchArms.add(
          MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), thenBlock});
      switchArms.add(
          MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), elseBlock});
      ctx.terminateBlock(MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                  zc::mv(switchArms), elseBlock,
                                                  diamond->sourceSpan.clone()));

      const auto emitArmBlock = [&](bool present, const hir::HirLocalWriteStatement* writeRecord,
                                    zc::Maybe<MirRvalue> rvalue) {
        (void)ctx.beginBlock(scope);
        if (present) {
          zc::Vector<MirProjection> projections;
          ctx.appendStatement(MirStatement::assign(
              MirPlace(ZC_ASSERT_NONNULL(target), armType, zc::mv(projections), armType),
              zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize,
              writeRecord->sourceSpan.clone()));
          ctx.terminateBlock(MirTerminator::gotoTarget(joinBlock, writeRecord->sourceSpan.clone()));
        } else {
          // Missing arm: the target stays uninitialized on this path. The
          // block only joins; the ownership rail reports ZOM4093 at the read.
          ctx.terminateBlock(MirTerminator::gotoTarget(joinBlock, diamond->sourceSpan.clone()));
        }
      };
      emitArmBlock(thenPresent, thenWriteRecord, zc::mv(thenRvalue));
      emitArmBlock(elsePresent, elseWriteRecord, zc::mv(elseRvalue));

      // Open the join block; the trailing Return statement terminates it.
      (void)ctx.beginBlock(scope);
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
          auto operand = lowerParameterReference(*paramRef, declaration, isMethod, proofs,
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
          auto operand = lowerLocalReference(*localRef, ZC_ASSERT_NONNULL(mirLocal), proofs,
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

  return ctx.finish(
      declaration.definition, MirFunctionKind::Function,
      isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function,
      declaration.resultType, declaration.sourceSpan.clone());
}

}  // namespace zomlang::compiler::mir
