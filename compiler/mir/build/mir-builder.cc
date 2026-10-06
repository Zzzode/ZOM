// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/mir-builder.h"

#include <cstddef>

#include "compiler/checker/body/marker-proof.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/mir/build/mir-fn-builder.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::mir {
namespace {

/// \brief Unique pool lookup returning none on zero or duplicate matches.
///
/// Replicates the legacy HIR accessor helper semantics so the recursive arms and
/// the shape rail resolve a node identically; the duplicate-match case stays a
/// delegation back to the legacy rejection path.
template <typename Record>
zc::Maybe<const Record&> uniqueRecordFor(zc::ArrayPtr<const Record> records, hir::HirNodeId node) {
  zc::Maybe<const Record&> result;
  for (const auto& record : records) {
    if (record.node != node) continue;
    if (result != zc::none) return zc::none;
    result = record;
  }
  return result;
}

zc::Maybe<const hir::HirScalarLiteralExpression&> expressionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.expressions(), node);
}

zc::Maybe<const hir::HirDirectCallExpression&> callFor(const hir::VerifiedHirModule& module,
                                                       hir::HirNodeId node) {
  return uniqueRecordFor(module.calls(), node);
}

zc::Maybe<const hir::HirParameterReferenceExpression&> parameterReferenceFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.parameterReferences(), node);
}

zc::Maybe<const hir::HirConditionalExpression&> conditionalFor(const hir::VerifiedHirModule& module,
                                                               hir::HirNodeId node) {
  return uniqueRecordFor(module.conditionals(), node);
}

zc::Maybe<const hir::HirReceiverCallExpression&> receiverCallFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.receiverCalls(), node);
}

MirBlockId blockId(uint32_t ordinal) {
  auto value = MirBlockId::fromOrdinal(ordinal);
  ZC_IF_SOME(id, value) { return id; }
  ZC_UNREACHABLE
}

zc::Maybe<const hir::HirParameterReborrowExpression&> parameterReborrowFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.parameterReborrows(), node);
}

zc::Maybe<const hir::HirLocalBinding&> localFor(const hir::VerifiedHirModule& module,
                                                hir::HirNodeId node) {
  return uniqueRecordFor(module.locals(), node);
}

zc::Maybe<const hir::HirLocalWriteStatement&> localWriteFor(const hir::VerifiedHirModule& module,
                                                            hir::HirNodeId node) {
  return uniqueRecordFor(module.localWrites(), node);
}

zc::Maybe<const hir::HirLocalReferenceExpression&> localReferenceFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.localReferences(), node);
}

zc::Maybe<const hir::HirLocalFieldProjectionExpression&> localFieldProjectionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.localFieldProjections(), node);
}

zc::Maybe<const hir::HirParameterFieldProjectionExpression&> parameterFieldProjectionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.parameterFieldProjections(), node);
}

zc::Maybe<const hir::HirParameterFieldWriteStatement&> parameterFieldWriteFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.parameterFieldWrites(), node);
}

zc::Maybe<const hir::HirNominalAggregateExpression&> aggregateFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.aggregates(), node);
}

zc::Maybe<const hir::HirPrimitiveBinaryExpression&> primitiveBinaryFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.primitiveBinaryOperations(), node);
}

zc::Maybe<const hir::HirReturnStatement&> returnFor(const hir::VerifiedHirModule& module,
                                                    hir::HirNodeId node) {
  return uniqueRecordFor(module.returns(), node);
}

/// \brief Resolves whether a semantic type id names the canonical Unit primitive.
bool isUnitSemanticType(const type::SemanticTypeStore& semanticTypes,
                        identity::SemanticTypeId type) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) return false;
  auto primitive = lookup.get<type::SemanticTypeLookup>().data().primitiveKind();
  return primitive != zc::none &&
         ZC_ASSERT_NONNULL(primitive) == type::semantic::PrimitiveKind::Unit;
}

/// \brief Resolves a place use to a copy or move operand from the Copy marker
/// proof, replicating the legacy placeUse helper.
zc::Maybe<MirOperand> placeUse(checker::marker::MarkerProofEngine& proofs, identity::DefId copy,
                               MirPlace&& place) {
  const identity::SemanticTypeId type = place.resultType();
  const auto proof = proofs.prove(copy, type);
  if (proof.is<checker::marker::MarkerProofPositive>()) { return MirOperand::copy(zc::mv(place)); }
  if (proof.is<checker::marker::MarkerProofNegative>() ||
      proof.is<checker::marker::MarkerProofUnsatisfied>()) {
    return MirOperand::move(zc::mv(place));
  }
  return zc::none;
}

/// \brief Finds the source index of the parameter matching a parameter-reference
/// key, replicating the legacy linear key match.
zc::Maybe<size_t> parameterIndexFor(const hir::HirFunctionDeclaration& declaration,
                                    const identity::CallableParameterKey& parameter) {
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == parameter) return i;
  }
  return zc::none;
}

/// \brief Maps a primitive operation to its MIR relational operator.
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

/// \brief Maps a primitive operation to its MIR arithmetic operator.
zc::Maybe<MirArithmeticOperator> arithmeticOperatorFor(
    checker::PrimitiveOperation operation) noexcept {
  switch (operation) {
    case checker::PrimitiveOperation::Add:
      return MirArithmeticOperator::Add;
    case checker::PrimitiveOperation::Sub:
      return MirArithmeticOperator::Sub;
    case checker::PrimitiveOperation::Mul:
      return MirArithmeticOperator::Mul;
    case checker::PrimitiveOperation::Div:
      return MirArithmeticOperator::Div;
    case checker::PrimitiveOperation::Rem:
      return MirArithmeticOperator::Rem;
    case checker::PrimitiveOperation::Pow:
      return MirArithmeticOperator::Pow;
    case checker::PrimitiveOperation::Shl:
      return MirArithmeticOperator::Shl;
    case checker::PrimitiveOperation::Shr:
      return MirArithmeticOperator::Shr;
    case checker::PrimitiveOperation::UShr:
      return MirArithmeticOperator::UShr;
    case checker::PrimitiveOperation::BitAnd:
      return MirArithmeticOperator::BitAnd;
    case checker::PrimitiveOperation::BitOr:
      return MirArithmeticOperator::BitOr;
    case checker::PrimitiveOperation::BitXor:
      return MirArithmeticOperator::BitXor;
    // Logical short-circuit operators lower to bitwise And/Or: the admitted
    // HIR slice only accepts side-effect-free operands (literals, parameters,
    // earlier locals), so short-circuit and bitwise evaluation coincide.
    case checker::PrimitiveOperation::LogicalAnd:
      return MirArithmeticOperator::BitAnd;
    case checker::PrimitiveOperation::LogicalOr:
      return MirArithmeticOperator::BitOr;
    default:
      return zc::none;
  }
}

/// \brief Builds one MIR leaf operand for a primitive-binary initializer: a
/// scalar constant, a parameter place-use, or an earlier user-local
/// place-use. Projected and later-local operands return none.
zc::Maybe<MirOperand> binaryLeafOperand(
    const hir::VerifiedHirModule& hirModule, const hir::HirFunctionDeclaration& declaration,
    hir::HirNodeId operandNode, zc::ArrayPtr<MirLocalId> parameterLocals,
    zc::ArrayPtr<MirLocalId> userLocals, size_t bindingIndex, identity::SemanticTypeId operandType,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (auto literal = expressionFor(hirModule, operandNode); literal != zc::none) {
    if (ZC_ASSERT_NONNULL(literal).type != operandType) return zc::none;
    return MirOperand::constant(operandType, ZC_ASSERT_NONNULL(literal).value.clone());
  }
  if (auto parameter = parameterReferenceFor(hirModule, operandNode); parameter != zc::none) {
    const auto& value = ZC_ASSERT_NONNULL(parameter);
    auto index = parameterIndexFor(declaration, value.parameter);
    if (index == zc::none || value.type != operandType ||
        value.category != hir::HirValueCategory::Place) {
      return zc::none;
    }
    zc::Vector<MirProjection> projections;
    return placeUse(proofs, copyMarker,
                    MirPlace(parameterLocals[ZC_ASSERT_NONNULL(index)], operandType,
                             zc::mv(projections), operandType));
  }
  if (auto reference = localReferenceFor(hirModule, operandNode); reference != zc::none) {
    const auto& value = ZC_ASSERT_NONNULL(reference);
    if (value.type != operandType || value.category != hir::HirValueCategory::Place ||
        value.local.ordinal() == 0 || value.local.ordinal() > static_cast<uint32_t>(bindingIndex)) {
      return zc::none;
    }
    zc::Vector<MirProjection> projections;
    return placeUse(proofs, copyMarker,
                    MirPlace(userLocals[value.local.ordinal() - 1], operandType,
                             zc::mv(projections), operandType));
  }
  return zc::none;
}

/// \brief Lowers a sequential N-local single-block body
/// (`let a = <lit/param/local>; ... let z = ...; return <local-or-param>;`,
/// N>=2): parameter locals occupy localId(1..P), user local i occupies
/// localId(P+i+1), and the single block holds StorageLive plus an Initialize
/// Assign per binding in source order followed by a Return of the selected
/// place. Each initializer is a scalar constant or a copy/move place-use of a
/// parameter or an earlier user local; literal-only bodies on parameterized
/// signatures still declare the parameter locals, unlike the dedicated
/// single-local rail. Aggregate, primitive-binary, nested-operand, and
/// unsafe-tail initializers return none so the legacy sequential rail keeps
/// them.
zc::Maybe<RecursiveFunctionProduct> buildSequentialLocalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.unsafeBlock != zc::none) return zc::none;
  if (block.statements.size() < 2) return zc::none;

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
  if (sourceReturn == zc::none) return zc::none;

  // Resolve every leading binding; layer-local ordinals must be dense 1..N and
  // every binding carries an initializer. An Increment binding contributes a
  // write statement immediately before its local binding node; the write
  // targets the incremented local (an earlier binding), and the local
  // binding's initializer is a local reference to that incremented local.
  zc::Vector<const hir::HirLocalBinding*> bindings;
  zc::Vector<const hir::HirLocalWriteStatement*> bindingWrites;
  {
    const hir::HirLocalWriteStatement* pendingWrite = nullptr;
    for (size_t i = 0; i + 1 < block.statements.size(); ++i) {
      auto write = localWriteFor(hirModule, block.statements[i]);
      if (write != zc::none) {
        if (pendingWrite != nullptr) return zc::none;
        pendingWrite = &ZC_ASSERT_NONNULL(write);
        continue;
      }
      auto sourceLocal = localFor(hirModule, block.statements[i]);
      if (sourceLocal == zc::none) return zc::none;
      const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
      if (binding.local.ordinal() != static_cast<uint32_t>(bindings.size() + 1) ||
          binding.initializer == zc::none) {
        return zc::none;
      }
      bindings.add(&binding);
      bindingWrites.add(pendingWrite);
      pendingWrite = nullptr;
    }
    if (pendingWrite != nullptr) return zc::none;
  }
  const size_t bindingCount = bindings.size();

  const hir::HirNodeId returnNode = ZC_ASSERT_NONNULL(sourceReturn).value;
  auto returnLocalReference = localReferenceFor(hirModule, returnNode);
  auto returnParameterReference = parameterReferenceFor(hirModule, returnNode);
  auto returnFieldProjection = localFieldProjectionFor(hirModule, returnNode);
  if (returnFieldProjection != zc::none) return zc::none;
  if (returnLocalReference == zc::none && returnParameterReference == zc::none) return zc::none;

  // Ternary-initialized local: the last binding's initializer is a
  // HirConditionalExpression whose condition is a bool parameter or local
  // reference and whose arms are scalar literals. The return names the ternary
  // local. Lower to a four-block diamond: entry initializes the leading locals
  // and switches on the condition, the two arm blocks assign the branch
  // literals, and the join block returns the ternary local.
  {
    const size_t ternaryIndex = bindingCount - 1;
    hir::HirNodeId ternaryInitializerNode;
    ZC_IF_SOME(initializer, bindings[ternaryIndex]->initializer) {
      ternaryInitializerNode = initializer;
    }
    auto conditional = conditionalFor(hirModule, ternaryInitializerNode);
    if (conditional != zc::none && returnLocalReference != zc::none &&
        ZC_ASSERT_NONNULL(returnLocalReference).local.ordinal() ==
            static_cast<uint32_t>(ternaryIndex + 1)) {
      // The ternary path does not lower increment writes; reject if any binding
      // has an associated write.
      for (size_t i = 0; i < bindingCount; ++i) {
        if (bindingWrites[i] != nullptr) return zc::none;
      }
      const auto& cond = ZC_ASSERT_NONNULL(conditional);
      if (cond.type == declaration.resultType && cond.category == hir::HirValueCategory::Value) {
        auto conditionParameter = parameterReferenceFor(hirModule, cond.condition);
        auto conditionLocal = localReferenceFor(hirModule, cond.condition);
        auto conditionLiteral = expressionFor(hirModule, cond.condition);
        if (conditionParameter != zc::none || conditionLocal != zc::none ||
            conditionLiteral != zc::none) {
          auto thenLiteral = expressionFor(hirModule, cond.thenReturnValue);
          auto elseLiteral = expressionFor(hirModule, cond.elseReturnValue);
          if (thenLiteral != zc::none && elseLiteral != zc::none &&
              ZC_ASSERT_NONNULL(thenLiteral).type == declaration.resultType &&
              ZC_ASSERT_NONNULL(elseLiteral).type == declaration.resultType) {
            detail::MirFnCtx ctx;
            const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
            zc::Vector<MirLocalId> parameterLocals;
            for (size_t p = 0; p < declaration.parameters.size(); ++p) {
              parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter,
                                                   declaration.parameters[p].type, scope,
                                                   declaration.parameters[p].sourceSpan.clone()));
            }
            zc::Vector<MirLocalId> userLocals;
            for (size_t i = 0; i < bindingCount; ++i) {
              userLocals.add(ctx.declareLocal(MirLocalKind::UserLocal, bindings[i]->type, scope,
                                              bindings[i]->sourceSpan.clone()));
            }
            // A bool-literal condition needs a temporary to hold the value
            // for the SwitchInt discriminant.
            const bool conditionIsLiteral = conditionLiteral != zc::none;
            zc::Maybe<MirLocalId> conditionTemp;
            if (conditionIsLiteral) {
              conditionTemp = ctx.declareLocal(
                  MirLocalKind::Temporary, ZC_ASSERT_NONNULL(conditionLiteral).type, scope,
                  ZC_ASSERT_NONNULL(conditionLiteral).sourceSpan.clone());
            }
            const MirBlockId entry = ctx.beginBlock(scope);
            (void)entry;
            // Initialize the leading (non-ternary) locals.
            for (size_t i = 0; i < ternaryIndex; ++i) {
              const auto& binding = *bindings[i];
              hir::HirNodeId initializerNode;
              ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
              auto literal = expressionFor(hirModule, initializerNode);
              auto localReference = localReferenceFor(hirModule, initializerNode);
              auto parameterReference = parameterReferenceFor(hirModule, initializerNode);
              if (aggregateFor(hirModule, initializerNode) != zc::none ||
                  primitiveBinaryFor(hirModule, initializerNode) != zc::none ||
                  conditionalFor(hirModule, initializerNode) != zc::none) {
                return zc::none;
              }
              const int present = (literal != zc::none ? 1 : 0) +
                                  (localReference != zc::none ? 1 : 0) +
                                  (parameterReference != zc::none ? 1 : 0);
              if (present != 1) return zc::none;
              zc::Maybe<MirRvalue> rvalue;
              identity::SourceSpan assignSpan = binding.sourceSpan.clone();
              ZC_IF_SOME(value, literal) {
                if (value.type != binding.type || value.category != hir::HirValueCategory::Value) {
                  return zc::none;
                }
                rvalue = MirRvalue::use(MirOperand::constant(binding.type, value.value.clone()));
                assignSpan = value.sourceSpan.clone();
              }
              ZC_IF_SOME(value, localReference) {
                if (value.type != binding.type || value.category != hir::HirValueCategory::Place ||
                    value.local.ordinal() == 0 ||
                    value.local.ordinal() > static_cast<uint32_t>(i)) {
                  return zc::none;
                }
                zc::Vector<MirProjection> projections;
                auto operand = placeUse(proofs, copyMarker,
                                        MirPlace(userLocals[value.local.ordinal() - 1],
                                                 binding.type, zc::mv(projections), binding.type));
                if (operand == zc::none) return zc::none;
                rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
                assignSpan = value.sourceSpan.clone();
              }
              ZC_IF_SOME(value, parameterReference) {
                auto parameterIndex = parameterIndexFor(declaration, value.parameter);
                if (parameterIndex == zc::none || value.type != binding.type ||
                    value.category != hir::HirValueCategory::Place) {
                  return zc::none;
                }
                zc::Vector<MirProjection> projections;
                auto operand = placeUse(proofs, copyMarker,
                                        MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                                                 binding.type, zc::mv(projections), binding.type));
                if (operand == zc::none) return zc::none;
                rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
                assignSpan = value.sourceSpan.clone();
              }
              if (rvalue == zc::none) return zc::none;
              ctx.appendStatement(
                  MirStatement::storageLive(userLocals[i], binding.sourceSpan.clone()));
              zc::Vector<MirProjection> destinationProjections;
              ctx.appendStatement(
                  MirStatement::assign(MirPlace(userLocals[i], binding.type,
                                                zc::mv(destinationProjections), binding.type),
                                       zc::mv(ZC_ASSERT_NONNULL(rvalue)),
                                       MirInitializationKind::Initialize, zc::mv(assignSpan)));
            }
            // StorageLive the ternary local, then switch on the condition.
            ctx.appendStatement(MirStatement::storageLive(
                userLocals[ternaryIndex], bindings[ternaryIndex]->sourceSpan.clone()));
            zc::Maybe<MirOperand> discriminant;
            ZC_IF_SOME(literal, conditionLiteral) {
              if (literal.category != hir::HirValueCategory::Value) { return zc::none; }
              const MirLocalId temp = ZC_ASSERT_NONNULL(conditionTemp);
              ctx.appendStatement(MirStatement::storageLive(temp, literal.sourceSpan.clone()));
              zc::Vector<MirProjection> tempProjections;
              ctx.appendStatement(MirStatement::assign(
                  MirPlace(temp, literal.type, zc::mv(tempProjections), literal.type),
                  MirRvalue::use(MirOperand::constant(literal.type, literal.value.clone())),
                  MirInitializationKind::Initialize, literal.sourceSpan.clone()));
              zc::Vector<MirProjection> projections;
              discriminant =
                  placeUse(proofs, copyMarker,
                           MirPlace(temp, literal.type, zc::mv(projections), literal.type));
            }
            ZC_IF_SOME(param, conditionParameter) {
              auto parameterIndex = parameterIndexFor(declaration, param.parameter);
              if (parameterIndex == zc::none || param.category != hir::HirValueCategory::Place) {
                return zc::none;
              }
              zc::Vector<MirProjection> projections;
              discriminant = placeUse(proofs, copyMarker,
                                      MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                                               param.type, zc::mv(projections), param.type));
            }
            ZC_IF_SOME(local, conditionLocal) {
              if (local.category != hir::HirValueCategory::Place || local.local.ordinal() == 0 ||
                  local.local.ordinal() > static_cast<uint32_t>(ternaryIndex)) {
                return zc::none;
              }
              zc::Vector<MirProjection> projections;
              discriminant = placeUse(proofs, copyMarker,
                                      MirPlace(userLocals[local.local.ordinal() - 1], local.type,
                                               zc::mv(projections), local.type));
            }
            if (discriminant == zc::none) return zc::none;
            zc::Vector<MirSwitchIntArm> arms;
            arms.add(
                MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
            arms.add(
                MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
            ctx.terminateBlock(MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                        zc::mv(arms), blockId(3),
                                                        cond.sourceSpan.clone()));
            // Arm blocks: assign the branch literal then goto the join.
            const auto armBlock = [&](const hir::HirScalarLiteralExpression& literal) {
              (void)ctx.beginBlock(scope);
              zc::Vector<MirProjection> projections;
              ctx.appendStatement(MirStatement::assign(
                  MirPlace(userLocals[ternaryIndex], declaration.resultType, zc::mv(projections),
                           declaration.resultType),
                  MirRvalue::use(MirOperand::constant(literal.type, literal.value.clone())),
                  MirInitializationKind::Initialize, literal.sourceSpan.clone()));
              ctx.terminateBlock(MirTerminator::gotoTarget(blockId(4), literal.sourceSpan.clone()));
            };
            armBlock(ZC_ASSERT_NONNULL(thenLiteral));
            armBlock(ZC_ASSERT_NONNULL(elseLiteral));
            // Join block: return the ternary local.
            (void)ctx.beginBlock(scope);
            zc::Vector<MirProjection> returnProjections;
            auto returnOperand =
                placeUse(proofs, copyMarker,
                         MirPlace(userLocals[ternaryIndex], declaration.resultType,
                                  zc::mv(returnProjections), declaration.resultType));
            if (returnOperand == zc::none) return zc::none;
            ctx.terminateBlock(
                MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                           ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));
            MirFunction function =
                ctx.finish(declaration.definition, MirFunctionKind::Function,
                           identity::DefinitionKind::Function, declaration.resultType,
                           declaration.sourceSpan.clone());
            zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
            return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
          }
        }
      }
    }
  }

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (size_t p = 0; p < declaration.parameters.size(); ++p) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[p].type,
                                         scope, declaration.parameters[p].sourceSpan.clone()));
  }
  zc::Vector<MirLocalId> userLocals;
  for (size_t i = 0; i < bindingCount; ++i) {
    userLocals.add(ctx.declareLocal(MirLocalKind::UserLocal, bindings[i]->type, scope,
                                    bindings[i]->sourceSpan.clone()));
  }

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  for (size_t i = 0; i < bindingCount; ++i) {
    const auto& binding = *bindings[i];
    // Lower the increment write if this binding has one. The write
    // overwrites the incremented local (an earlier binding) with an
    // arithmetic rvalue; the binding's initializer then reads the updated
    // value through a local reference.
    if (bindingWrites[i] != nullptr) {
      const auto& write = *bindingWrites[i];
      auto writeBinary = primitiveBinaryFor(hirModule, write.value);
      if (writeBinary == zc::none) return zc::none;
      const auto& binaryValue = ZC_ASSERT_NONNULL(writeBinary);
      if (!binaryValue.isIncrementDesugar) return zc::none;
      auto arithmetic = arithmeticOperatorFor(binaryValue.operation);
      if (arithmetic == zc::none) return zc::none;
      if (binaryValue.type != binding.type || binaryValue.operandType != binding.type) {
        return zc::none;
      }
      auto left =
          binaryLeafOperand(hirModule, declaration, binaryValue.left, parameterLocals,
                            userLocals.asPtr(), i, binaryValue.operandType, proofs, copyMarker);
      auto right =
          binaryLeafOperand(hirModule, declaration, binaryValue.right, parameterLocals,
                            userLocals.asPtr(), i, binaryValue.operandType, proofs, copyMarker);
      if (left == zc::none || right == zc::none) return zc::none;
      auto rvalue =
          MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(ZC_ASSERT_NONNULL(left)),
                                zc::mv(ZC_ASSERT_NONNULL(right)), binaryValue.type);
      if (write.local.ordinal() == 0 || write.local.ordinal() > static_cast<uint32_t>(i)) {
        return zc::none;
      }
      zc::Vector<MirProjection> writeProjections;
      ctx.appendStatement(MirStatement::assign(
          MirPlace(userLocals[write.local.ordinal() - 1], write.type, zc::mv(writeProjections),
                   write.type),
          zc::mv(rvalue), MirInitializationKind::Overwrite, write.sourceSpan.clone()));
    }
    hir::HirNodeId initializerNode;
    ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
    auto literal = expressionFor(hirModule, initializerNode);
    auto aggregate = aggregateFor(hirModule, initializerNode);
    auto localReference = localReferenceFor(hirModule, initializerNode);
    auto parameterReference = parameterReferenceFor(hirModule, initializerNode);
    auto binary = primitiveBinaryFor(hirModule, initializerNode);
    // Aggregate and nested-operand initializers stay on the legacy rail.
    if (aggregate != zc::none) return zc::none;
    // A primitive-binary initializer (no nested binary operand) lowers to an
    // Arithmetic or Comparison rvalue over literal/parameter/local leaves.
    if (binary != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(binary);
      if (value.category != hir::HirValueCategory::Value || value.type != binding.type) {
        return zc::none;
      }
      // Both operands must themselves be leaves (a nested binary operand is
      // the Temporary family, which stays on the legacy rail).
      if (primitiveBinaryFor(hirModule, value.left) != zc::none ||
          primitiveBinaryFor(hirModule, value.right) != zc::none) {
        return zc::none;
      }
      auto comparison = comparisonOperatorFor(value.operation);
      auto arithmetic = arithmeticOperatorFor(value.operation);
      if (comparison == zc::none && arithmetic == zc::none) return zc::none;
      // An arithmetic rvalue has result type equal to its operand type; a
      // comparison rvalue yields bool with a distinct operand type.
      if (arithmetic != zc::none && value.type != value.operandType) return zc::none;
      auto left = binaryLeafOperand(hirModule, declaration, value.left, parameterLocals,
                                    userLocals.asPtr(), i, value.operandType, proofs, copyMarker);
      auto right = binaryLeafOperand(hirModule, declaration, value.right, parameterLocals,
                                     userLocals.asPtr(), i, value.operandType, proofs, copyMarker);
      if (left == zc::none || right == zc::none) return zc::none;
      zc::Maybe<MirRvalue> binaryRvalue;
      if (arithmetic != zc::none) {
        binaryRvalue =
            MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(ZC_ASSERT_NONNULL(left)),
                                  zc::mv(ZC_ASSERT_NONNULL(right)), value.type);
      } else {
        binaryRvalue =
            MirRvalue::comparison(ZC_ASSERT_NONNULL(comparison), zc::mv(ZC_ASSERT_NONNULL(left)),
                                  zc::mv(ZC_ASSERT_NONNULL(right)), value.type);
      }
      ctx.appendStatement(MirStatement::storageLive(userLocals[i], binding.sourceSpan.clone()));
      zc::Vector<MirProjection> destinationProjections;
      ctx.appendStatement(MirStatement::assign(
          MirPlace(userLocals[i], binding.type, zc::mv(destinationProjections), binding.type),
          zc::mv(ZC_ASSERT_NONNULL(binaryRvalue)), MirInitializationKind::Initialize,
          value.sourceSpan.clone()));
      continue;
    }
    const int present = (literal != zc::none ? 1 : 0) + (localReference != zc::none ? 1 : 0) +
                        (parameterReference != zc::none ? 1 : 0);
    if (present != 1) { return zc::none; }

    zc::Maybe<MirRvalue> rvalue;
    identity::SourceSpan assignSpan = binding.sourceSpan.clone();
    ZC_IF_SOME(value, literal) {
      if (value.type != binding.type || value.category != hir::HirValueCategory::Value) {
        return zc::none;
      }
      rvalue = MirRvalue::use(MirOperand::constant(binding.type, value.value.clone()));
      assignSpan = value.sourceSpan.clone();
    }
    ZC_IF_SOME(value, localReference) {
      if (value.type != binding.type || value.category != hir::HirValueCategory::Place ||
          value.local.ordinal() == 0 || value.local.ordinal() > static_cast<uint32_t>(i)) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(userLocals[value.local.ordinal() - 1], binding.type,
                                       zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      assignSpan = value.sourceSpan.clone();
    }
    ZC_IF_SOME(value, parameterReference) {
      auto parameterIndex = parameterIndexFor(declaration, value.parameter);
      if (parameterIndex == zc::none || value.type != binding.type ||
          value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                                       binding.type, zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      assignSpan = value.sourceSpan.clone();
    }
    if (rvalue == zc::none) return zc::none;

    ctx.appendStatement(MirStatement::storageLive(userLocals[i], binding.sourceSpan.clone()));
    zc::Vector<MirProjection> destinationProjections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(userLocals[i], binding.type, zc::mv(destinationProjections), binding.type),
        zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize, zc::mv(assignSpan)));
  }

  zc::Maybe<MirOperand> returnOperand;
  ZC_IF_SOME(reference, returnLocalReference) {
    if (reference.type != declaration.resultType ||
        reference.category != hir::HirValueCategory::Place || reference.local.ordinal() == 0 ||
        reference.local.ordinal() > static_cast<uint32_t>(bindingCount)) {
      return zc::none;
    }
    zc::Vector<MirProjection> projections;
    returnOperand = placeUse(proofs, copyMarker,
                             MirPlace(userLocals[reference.local.ordinal() - 1], reference.type,
                                      zc::mv(projections), reference.type));
  }
  ZC_IF_SOME(reference, returnParameterReference) {
    auto parameterIndex = parameterIndexFor(declaration, reference.parameter);
    if (parameterIndex == zc::none || reference.type != declaration.resultType ||
        reference.category != hir::HirValueCategory::Place) {
      return zc::none;
    }
    zc::Vector<MirProjection> projections;
    returnOperand = placeUse(proofs, copyMarker,
                             MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                                      reference.type, zc::mv(projections), reference.type));
  }
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn f(...) -> T { return <literal>; }`: one root scope, no
/// locals, one empty entry block returning the scalar constant. Byte-identical
/// to the legacy fallthrough scalar construction. An inherent method
/// (`fn m(this) -> T { return <literal>; }`) additionally declares its
/// implicit `this` receiver as the leading parameter local, which the admitted
/// scalar body never reads.
zc::Maybe<RecursiveFunctionProduct> buildScalarReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirScalarLiteralExpression& literal,
    const checker::CheckerIdentityAuthority& identities) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  const bool isMethod = declaration.receiver != zc::none;
  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  ZC_IF_SOME(receiver, declaration.receiver) {
    ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());
  }
  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.terminateBlock(MirTerminator::returnValue(
      MirOperand::constant(declaration.resultType, literal.value.clone()),
      sourceReturn.sourceSpan.clone()));

  MirFunction function =
      ctx.finish(declaration.definition, MirFunctionKind::Function,
                 isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function,
                 declaration.resultType, declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn m(this) -> T { return this.field; }` on a shared or
/// mutating receiver: one root scope, one receiver parameter local carrying
/// the receiver reference, and a Return of a copy/move place-use rooted at that
/// local through [Dereference(&Owner -> Owner), Field(Owner -> T)].
zc::Maybe<RecursiveFunctionProduct> buildReceiverFieldReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirParameterFieldProjectionExpression& projection,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (projection.type != declaration.resultType ||
      projection.category != hir::HirValueCategory::Place) {
    return zc::none;
  }
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());

  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::dereference(receiver.type, projection.receiverType));
  projections.add(MirProjection::field(projection.field, projection.receiverType, projection.type));
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(ZC_ASSERT_NONNULL(MirLocalId::fromOrdinal(1)),
                                         receiver.type, zc::mv(projections), projection.type));
  if (returnOperand == zc::none) return zc::none;

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn f(p: Owner) -> T { return p.field; }` for an ordinary
/// by-value struct parameter: one root scope, one Parameter local carrying the
/// nominal owner, and a Return of a copy/move place-use rooted at that local
/// through a single [Field(Owner -> T)] projection (no dereference, unlike the
/// receiver twin).
zc::Maybe<RecursiveFunctionProduct> buildByValueParameterFieldReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirParameterFieldProjectionExpression& projection,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver != zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  if (projection.type != declaration.resultType ||
      projection.category != hir::HirValueCategory::Place) {
    return zc::none;
  }
  if (declaration.parameters.size() != 1 || declaration.parameters[0].key != projection.parameter ||
      declaration.parameters[0].type != projection.receiverType) {
    return zc::none;
  }
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId parameterLocal =
      ctx.declareLocal(MirLocalKind::Parameter, projection.receiverType, scope,
                       declaration.parameters[0].sourceSpan.clone());
  if (parameterLocal.ordinal() != 1) return zc::none;

  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::field(projection.field, projection.receiverType, projection.type));
  auto returnOperand = placeUse(
      proofs, copyMarker,
      MirPlace(parameterLocal, projection.receiverType, zc::mv(projections), projection.type));
  if (returnOperand == zc::none) return zc::none;

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers the method parameter-binary return
/// `fn m(this, p0..pN-1) -> T { return <parameter|literal> OP <parameter|literal>; }`:
/// the receiver leads the parameter locals, one FunctionResult local holds the
/// arithmetic/comparison rvalue, and the entry block stores it live, assigns
/// the result, and returns a place-use of that local.
zc::Maybe<RecursiveFunctionProduct> buildMethodBinaryReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirPrimitiveBinaryExpression& binary, const hir::VerifiedHirModule& hirModule,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  auto arithmetic = arithmeticOperatorFor(binary.operation);
  auto comparison = comparisonOperatorFor(binary.operation);
  if (arithmetic == zc::none && comparison == zc::none) return zc::none;
  if (arithmetic != zc::none && binary.type != binary.operandType) return zc::none;

  // Each operand is a scalar literal or a place-use of an ordinary parameter.
  // Receiver-field and nested-binary operands keep their own lowering shapes.
  auto operandFor = [&](hir::HirNodeId operandNode,
                        zc::ArrayPtr<MirLocalId> parameterLocals) -> zc::Maybe<MirOperand> {
    if (auto literal = expressionFor(hirModule, operandNode); literal != zc::none) {
      if (ZC_ASSERT_NONNULL(literal).type != binary.operandType) return zc::none;
      return MirOperand::constant(binary.operandType, ZC_ASSERT_NONNULL(literal).value.clone());
    }
    if (auto parameter = parameterReferenceFor(hirModule, operandNode); parameter != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(parameter);
      auto index = parameterIndexFor(declaration, value.parameter);
      if (index == zc::none || value.type != binary.operandType ||
          value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      return placeUse(proofs, copyMarker,
                      MirPlace(parameterLocals[ZC_ASSERT_NONNULL(index)], binary.operandType,
                               zc::mv(projections), binary.operandType));
    }
    return zc::none;
  };

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (const auto& parameter : declaration.parameters) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, parameter.type, scope,
                                         parameter.sourceSpan.clone()));
  }
  const MirLocalId result = ctx.declareLocal(MirLocalKind::FunctionResult, declaration.resultType,
                                             scope, sourceReturn.sourceSpan.clone());

  auto left = operandFor(binary.left, parameterLocals.asPtr());
  auto right = operandFor(binary.right, parameterLocals.asPtr());
  if (left == zc::none || right == zc::none) return zc::none;

  zc::Maybe<MirRvalue> rvalue;
  if (arithmetic != zc::none) {
    rvalue = MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(ZC_ASSERT_NONNULL(left)),
                                   zc::mv(ZC_ASSERT_NONNULL(right)), binary.type);
  } else {
    rvalue = MirRvalue::comparison(ZC_ASSERT_NONNULL(comparison), zc::mv(ZC_ASSERT_NONNULL(left)),
                                   zc::mv(ZC_ASSERT_NONNULL(right)), binary.type);
  }

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(result, sourceReturn.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(
      MirStatement::assign(MirPlace(result, declaration.resultType, zc::mv(destinationProjections),
                                    declaration.resultType),
                           zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize,
                           binary.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(
      proofs, copyMarker,
      MirPlace(result, declaration.resultType, zc::mv(returnProjections), declaration.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers the free-function parameter-binary return
/// `fn f(p0..pN-1) -> T { return <parameter|literal> OP <parameter|literal>; }`:
/// one FunctionResult local holds the arithmetic/comparison rvalue, and the
/// entry block stores it live, assigns it, and returns a place-use of that
/// local.
zc::Maybe<RecursiveFunctionProduct> buildFreeFunctionBinaryReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirPrimitiveBinaryExpression& binary, const hir::VerifiedHirModule& hirModule,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver != zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  auto arithmetic = arithmeticOperatorFor(binary.operation);
  auto comparison = comparisonOperatorFor(binary.operation);
  if (arithmetic == zc::none && comparison == zc::none) return zc::none;
  if (arithmetic != zc::none && binary.type != binary.operandType) return zc::none;

  // Each operand is a scalar literal or a place-use of an ordinary parameter.
  auto operandFor = [&](hir::HirNodeId operandNode,
                        zc::ArrayPtr<MirLocalId> parameterLocals) -> zc::Maybe<MirOperand> {
    if (auto literal = expressionFor(hirModule, operandNode); literal != zc::none) {
      if (ZC_ASSERT_NONNULL(literal).type != binary.operandType) return zc::none;
      return MirOperand::constant(binary.operandType, ZC_ASSERT_NONNULL(literal).value.clone());
    }
    if (auto parameter = parameterReferenceFor(hirModule, operandNode); parameter != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(parameter);
      auto index = parameterIndexFor(declaration, value.parameter);
      if (index == zc::none || value.type != binary.operandType ||
          value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      return placeUse(proofs, copyMarker,
                      MirPlace(parameterLocals[ZC_ASSERT_NONNULL(index)], binary.operandType,
                               zc::mv(projections), binary.operandType));
    }
    return zc::none;
  };

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (const auto& parameter : declaration.parameters) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, parameter.type, scope,
                                         parameter.sourceSpan.clone()));
  }
  const MirLocalId result = ctx.declareLocal(MirLocalKind::FunctionResult, declaration.resultType,
                                             scope, sourceReturn.sourceSpan.clone());

  auto left = operandFor(binary.left, parameterLocals.asPtr());
  auto right = operandFor(binary.right, parameterLocals.asPtr());
  if (left == zc::none || right == zc::none) return zc::none;

  zc::Maybe<MirRvalue> rvalue;
  if (arithmetic != zc::none) {
    rvalue = MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(ZC_ASSERT_NONNULL(left)),
                                   zc::mv(ZC_ASSERT_NONNULL(right)), binary.type);
  } else {
    rvalue = MirRvalue::comparison(ZC_ASSERT_NONNULL(comparison), zc::mv(ZC_ASSERT_NONNULL(left)),
                                   zc::mv(ZC_ASSERT_NONNULL(right)), binary.type);
  }

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(result, sourceReturn.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(
      MirStatement::assign(MirPlace(result, declaration.resultType, zc::mv(destinationProjections),
                                    declaration.resultType),
                           zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize,
                           binary.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(
      proofs, copyMarker,
      MirPlace(result, declaration.resultType, zc::mv(returnProjections), declaration.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers the receiver-field arithmetic return
/// `fn m(this) -> T { return this.<field> OP <literal>; }` (and the mirrored
/// operand order): the receiver leads the single parameter local, one
/// FunctionResult local holds the arithmetic rvalue whose field operand is a
/// [Dereference, Field] place-use of the receiver and whose other operand is a
/// scalar constant, and the entry block stores the result live, assigns it, and
/// returns a place-use of that local.
zc::Maybe<RecursiveFunctionProduct> buildReceiverFieldBinaryReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirPrimitiveBinaryExpression& binary,
    const hir::HirParameterFieldProjectionExpression& projection,
    const checker::checked::CanonicalConstValue& literalValue, bool fieldIsLeft,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  (void)hirModule;
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  auto arithmetic = arithmeticOperatorFor(binary.operation);
  if (arithmetic == zc::none) return zc::none;
  if (binary.type != binary.operandType || projection.type != binary.operandType) {
    return zc::none;
  }

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());
  const MirLocalId result = ctx.declareLocal(MirLocalKind::FunctionResult, declaration.resultType,
                                             scope, sourceReturn.sourceSpan.clone());

  zc::Vector<MirProjection> fieldProjections;
  fieldProjections.add(MirProjection::dereference(receiver.type, projection.receiverType));
  fieldProjections.add(
      MirProjection::field(projection.field, projection.receiverType, projection.type));
  zc::Maybe<MirOperand> fieldOperand =
      placeUse(proofs, copyMarker,
               MirPlace(ZC_ASSERT_NONNULL(MirLocalId::fromOrdinal(1)), receiver.type,
                        zc::mv(fieldProjections), projection.type));
  if (fieldOperand == zc::none) return zc::none;
  auto literalOperand = MirOperand::constant(projection.type, literalValue.clone());

  zc::Maybe<MirRvalue> rvalue;
  if (fieldIsLeft) {
    rvalue = MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic),
                                   zc::mv(ZC_ASSERT_NONNULL(fieldOperand)), zc::mv(literalOperand),
                                   binary.type);
  } else {
    rvalue = MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(literalOperand),
                                   zc::mv(ZC_ASSERT_NONNULL(fieldOperand)), binary.type);
  }

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(result, sourceReturn.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(
      MirStatement::assign(MirPlace(result, declaration.resultType, zc::mv(destinationProjections),
                                    declaration.resultType),
                           zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize,
                           binary.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(
      proofs, copyMarker,
      MirPlace(result, declaration.resultType, zc::mv(returnProjections), declaration.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn f(p0..pN-1) -> R { return pK; }`, or the method twin
/// `fn m(this, p0..pN-1) -> R { return pK; }`: one root scope, parameter
/// locals in source order with the implicit receiver leading for a method,
/// one empty entry block returning a copy/move place-use of the referenced
/// parameter local. Byte-identical to the legacy parameter-return construction.
zc::Maybe<RecursiveFunctionProduct> buildParameterReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirParameterReferenceExpression& reference,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  size_t referencedIndex = 0;
  bool referencedFound = false;
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == reference.parameter &&
        declaration.parameters[i].type == reference.type) {
      referencedIndex = i;
      referencedFound = true;
      break;
    }
  }
  if (!referencedFound) return zc::none;
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  const bool isMethod = declaration.receiver != zc::none;
  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  ZC_IF_SOME(receiver, declaration.receiver) {
    ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());
  }
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[i].type, scope,
                     declaration.parameters[i].sourceSpan.clone());
  }
  const auto referencedLocal = ZC_ASSERT_NONNULL(
      MirLocalId::fromOrdinal(static_cast<uint32_t>(referencedIndex + 1 + (isMethod ? 1 : 0))));
  zc::Vector<MirProjection> projections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(referencedLocal, reference.type, zc::mv(projections), reference.type));
  if (returnOperand == zc::none) return zc::none;

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function =
      ctx.finish(declaration.definition, MirFunctionKind::Function,
                 isMethod ? identity::DefinitionKind::Method : identity::DefinitionKind::Function,
                 declaration.resultType, declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn f(...) -> T { let x = <literal>; return x; }`: one root
/// scope, one UserLocal at localId(1), StorageLive followed by an Initialize
/// Assign of the scalar constant, and a Return of the copy/move place-use of
/// the local. The legacy dedicated single-local rail declares no parameter
/// locals, allocates no function-result local, and emits no StorageDead, so
/// this arm reproduces exactly that layout byte for byte.
zc::Maybe<RecursiveFunctionProduct> buildScalarLocalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer, const hir::HirReturnStatement& sourceReturn,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId userLocal = ctx.declareLocal(MirLocalKind::UserLocal, sourceLocal.type, scope,
                                                sourceLocal.sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(userLocal, sourceLocal.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, sourceLocal.type, zc::mv(destinationProjections), sourceLocal.type),
      MirRvalue::use(MirOperand::constant(sourceLocal.type, initializer.value.clone())),
      MirInitializationKind::Initialize, initializer.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, sourceLocal.type, zc::mv(returnProjections), sourceLocal.type));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn f() -> T { mut x = <lit>; x = <lit>; return x; }`: one
/// root scope, one UserLocal at localId(1), StorageLive, the Initialize Assign
/// of the initializer constant, one Overwrite Assign of the write constant, and
/// a Return of the copy/move place-use of the local. Both values are scalar
/// literals, so the legacy rail declares no parameter locals; the statement and
/// span order is reproduced byte for byte.
zc::Maybe<RecursiveFunctionProduct> buildScalarLocalOverwriteReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer,
    const hir::HirLocalWriteStatement& overwrite,
    const hir::HirScalarLiteralExpression& overwriteValue,
    const hir::HirReturnStatement& sourceReturn,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId userLocal = ctx.declareLocal(MirLocalKind::UserLocal, sourceLocal.type, scope,
                                                sourceLocal.sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(userLocal, sourceLocal.sourceSpan.clone()));
  zc::Vector<MirProjection> initializeProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, sourceLocal.type, zc::mv(initializeProjections), sourceLocal.type),
      MirRvalue::use(MirOperand::constant(sourceLocal.type, initializer.value.clone())),
      MirInitializationKind::Initialize, initializer.sourceSpan.clone()));
  zc::Vector<MirProjection> overwriteProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, sourceLocal.type, zc::mv(overwriteProjections), sourceLocal.type),
      MirRvalue::use(MirOperand::constant(sourceLocal.type, overwriteValue.value.clone())),
      MirInitializationKind::Overwrite, overwrite.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, sourceLocal.type, zc::mv(returnProjections), sourceLocal.type));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers a single user-local body with one or more overwrite writes.
///
/// `mut x = <lit/param>; x = <lit/param/binary>; ...; return x;`. The first
/// write may be an Initialize when the local has no initializer; every
/// subsequent write is an Overwrite. A binary write value lowers to an
/// arithmetic rvalue whose left operand is a place-use of the user local and
/// whose right operand is a scalar literal, parameter, or earlier local.
zc::Maybe<RecursiveFunctionProduct> buildLocalWriteReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.unsafeBlock != zc::none) return zc::none;
  if (block.statements.size() < 3) return zc::none;

  auto sourceLocal = localFor(hirModule, block.statements[0]);
  if (sourceLocal == zc::none) { return zc::none; }
  const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
  if (binding.local.ordinal() != 1) { return zc::none; }
  if (binding.type != declaration.resultType) { return zc::none; }

  const size_t writeCount = block.statements.size() - 2;
  zc::Vector<const hir::HirLocalWriteStatement*> writes;
  for (size_t i = 0; i < writeCount; ++i) {
    auto write = localWriteFor(hirModule, block.statements[1 + i]);
    if (write == zc::none) { return zc::none; }
    const auto& w = ZC_ASSERT_NONNULL(write);
    if (w.local != binding.local || w.field != zc::none || w.type != binding.type) {
      return zc::none;
    }
    writes.add(&w);
  }

  auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
  if (sourceReturn == zc::none) { return zc::none; }
  auto reference = localReferenceFor(hirModule, ZC_ASSERT_NONNULL(sourceReturn).value);
  if (reference == zc::none) { return zc::none; }
  if (ZC_ASSERT_NONNULL(reference).local != binding.local ||
      ZC_ASSERT_NONNULL(reference).type != binding.type ||
      ZC_ASSERT_NONNULL(reference).category != hir::HirValueCategory::Place) {
    return zc::none;
  }

  // Resolve the initializer (literal or parameter).
  hir::HirNodeId initializerNode;
  ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
  auto initializerLiteral = expressionFor(hirModule, initializerNode);
  auto initializerParameter = parameterReferenceFor(hirModule, initializerNode);
  if (initializerLiteral == zc::none && initializerParameter == zc::none) { return zc::none; }

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (const auto& parameter : declaration.parameters) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, parameter.type, scope,
                                         parameter.sourceSpan.clone()));
  }
  const MirLocalId userLocal =
      ctx.declareLocal(MirLocalKind::UserLocal, binding.type, scope, binding.sourceSpan.clone());

  // Operand resolver for binary write values: literal, parameter, the user
  // local itself (compound assignment desugar), or a nested binary operation
  // lowered to a temporary local.
  auto operandForImpl = [&](auto&& self, hir::HirNodeId operandNode,
                            identity::SemanticTypeId operandType) -> zc::Maybe<MirOperand> {
    if (auto literal = expressionFor(hirModule, operandNode); literal != zc::none) {
      if (ZC_ASSERT_NONNULL(literal).type != operandType) return zc::none;
      return MirOperand::constant(operandType, ZC_ASSERT_NONNULL(literal).value.clone());
    }
    if (auto parameter = parameterReferenceFor(hirModule, operandNode); parameter != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(parameter);
      auto index = parameterIndexFor(declaration, value.parameter);
      if (index == zc::none || value.type != operandType ||
          value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      return placeUse(proofs, copyMarker,
                      MirPlace(parameterLocals[ZC_ASSERT_NONNULL(index)], operandType,
                               zc::mv(projections), operandType));
    }
    if (auto localRef = localReferenceFor(hirModule, operandNode); localRef != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(localRef);
      if (value.local != binding.local || value.type != operandType ||
          value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      return placeUse(proofs, copyMarker,
                      MirPlace(userLocal, operandType, zc::mv(projections), operandType));
    }
    // A nested binary operand: lower it to a temporary local assigned an
    // arithmetic rvalue, then return a place-use of that temporary.
    if (auto nestedBinary = primitiveBinaryFor(hirModule, operandNode); nestedBinary != zc::none) {
      const auto& binary = ZC_ASSERT_NONNULL(nestedBinary);
      if (binary.type != operandType) return zc::none;
      auto arithmetic = arithmeticOperatorFor(binary.operation);
      if (arithmetic == zc::none) return zc::none;
      auto left = self(self, binary.left, binary.operandType);
      auto right = self(self, binary.right, binary.operandType);
      if (left == zc::none || right == zc::none) return zc::none;
      const MirLocalId temp =
          ctx.declareLocal(MirLocalKind::Temporary, binary.type, scope, binary.sourceSpan.clone());
      ctx.appendStatement(MirStatement::storageLive(temp, binary.sourceSpan.clone()));
      zc::Vector<MirProjection> projections;
      ctx.appendStatement(MirStatement::assign(
          MirPlace(temp, binary.type, zc::mv(projections), binary.type),
          MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(ZC_ASSERT_NONNULL(left)),
                                zc::mv(ZC_ASSERT_NONNULL(right)), binary.type),
          MirInitializationKind::Initialize, binary.sourceSpan.clone()));
      zc::Vector<MirProjection> tempProjections;
      return placeUse(proofs, copyMarker,
                      MirPlace(temp, operandType, zc::mv(tempProjections), operandType));
    }
    return zc::none;
  };
  auto operandFor = [&](hir::HirNodeId node, identity::SemanticTypeId type) {
    return operandForImpl(operandForImpl, node, type);
  };

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(userLocal, binding.sourceSpan.clone()));

  // Initialize the user local.
  {
    zc::Maybe<MirRvalue> rvalue;
    identity::SourceSpan initializerSpan = binding.sourceSpan.clone();
    ZC_IF_SOME(literal, initializerLiteral) {
      rvalue = MirRvalue::use(MirOperand::constant(binding.type, literal.value.clone()));
      initializerSpan = literal.sourceSpan.clone();
    }
    ZC_IF_SOME(parameter, initializerParameter) {
      auto index = parameterIndexFor(declaration, parameter.parameter);
      if (index == zc::none || parameter.type != binding.type ||
          parameter.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(parameterLocals[ZC_ASSERT_NONNULL(index)], binding.type,
                                       zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      initializerSpan = parameter.sourceSpan.clone();
    }
    zc::Vector<MirProjection> projections;
    ctx.appendStatement(
        MirStatement::assign(MirPlace(userLocal, binding.type, zc::mv(projections), binding.type),
                             zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize,
                             zc::mv(initializerSpan)));
  }

  // Apply each write.
  for (size_t i = 0; i < writeCount; ++i) {
    const auto& write = *writes[i];
    auto writeLiteral = expressionFor(hirModule, write.value);
    auto writeParameter = parameterReferenceFor(hirModule, write.value);
    auto writeBinary = primitiveBinaryFor(hirModule, write.value);

    zc::Maybe<MirRvalue> rvalue;
    if (writeLiteral != zc::none) {
      if (ZC_ASSERT_NONNULL(writeLiteral).type != binding.type) return zc::none;
      rvalue = MirRvalue::use(
          MirOperand::constant(binding.type, ZC_ASSERT_NONNULL(writeLiteral).value.clone()));
    } else if (writeParameter != zc::none) {
      const auto& parameter = ZC_ASSERT_NONNULL(writeParameter);
      auto index = parameterIndexFor(declaration, parameter.parameter);
      if (index == zc::none || parameter.type != binding.type ||
          parameter.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(parameterLocals[ZC_ASSERT_NONNULL(index)], binding.type,
                                       zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
    } else if (writeBinary != zc::none) {
      const auto& binary = ZC_ASSERT_NONNULL(writeBinary);
      auto arithmetic = arithmeticOperatorFor(binary.operation);
      if (arithmetic == zc::none) { return zc::none; }
      if (binary.type != binding.type || binary.operandType != binding.type) { return zc::none; }
      auto left = operandFor(binary.left, binary.operandType);
      auto right = operandFor(binary.right, binary.operandType);
      if (left == zc::none || right == zc::none) { return zc::none; }
      rvalue = MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(ZC_ASSERT_NONNULL(left)),
                                     zc::mv(ZC_ASSERT_NONNULL(right)), binary.type);
    } else {
      return zc::none;
    }

    zc::Vector<MirProjection> projections;
    ctx.appendStatement(
        MirStatement::assign(MirPlace(userLocal, binding.type, zc::mv(projections), binding.type),
                             zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Overwrite,
                             write.sourceSpan.clone()));
  }

  zc::Vector<MirProjection> returnProjections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, binding.type, zc::mv(returnProjections), binding.type));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers one aggregate-initializer single user-local return body.
///
/// Byte-identical to the legacy dedicated single-local aggregate rail: one
/// root scope, one UserLocal at localId(1) (no parameter locals, even when the
/// signature has parameters), StorageLive plus an Initialize Assign of a
/// nominal aggregate with constant elements, and a Return selecting either
/// that local (whole struct) or one of its fields.
///
/// - N==2 leading `let` bindings where the single binding is an aggregate
///   initializer with constant elements; a literal/parameter/local/binary
///   initializer keeps the dedicated legacy rail.
/// - The return is a bare user-local reference (whole struct) or a single
///   field projection of that local; aggregate composites and field writes
///   stay on the legacy rail.
/// - Aggregate elements must all be scalar constants of their element type.
/// - No unsafe-tail variant joins this arm.
zc::Maybe<RecursiveFunctionProduct> buildAggregateLocalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
  if (sourceReturn == zc::none) return zc::none;

  auto sourceLocal = localFor(hirModule, block.statements[0]);
  if (sourceLocal == zc::none) return zc::none;
  const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);

  hir::HirNodeId initializerNode;
  ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
  auto aggregate = aggregateFor(hirModule, initializerNode);
  if (aggregate == zc::none) return zc::none;
  auto returnLocalReference = localReferenceFor(hirModule, ZC_ASSERT_NONNULL(sourceReturn).value);
  auto returnFieldProjection =
      localFieldProjectionFor(hirModule, ZC_ASSERT_NONNULL(sourceReturn).value);
  if (returnLocalReference == zc::none && returnFieldProjection == zc::none) return zc::none;

  // Strict gate, self-contained.
  if (declaration.unsafeBlock != zc::none) return zc::none;
  if (block.statements.size() != 2) return zc::none;
  if (binding.local.ordinal() != 1) return zc::none;
  if (binding.type != declaration.resultType && returnLocalReference != zc::none) return zc::none;

  const auto& sourceAggregate = ZC_ASSERT_NONNULL(aggregate);
  if (sourceAggregate.type != binding.type) return zc::none;

  // The returned place must read the single aggregate user local.
  if (returnLocalReference != zc::none &&
      ZC_ASSERT_NONNULL(returnLocalReference).local != binding.local) {
    return zc::none;
  }
  if (returnFieldProjection != zc::none) {
    const auto& projection = ZC_ASSERT_NONNULL(returnFieldProjection);
    if (projection.type != declaration.resultType) return zc::none;
  }

  // Every aggregate element is a constant of its declared element type.
  zc::Vector<MirNominalAggregateElement> elements;
  for (const auto& element : sourceAggregate.elements) {
    elements.add(MirNominalAggregateElement{
        element.field, MirOperand::constant(element.type, element.value.clone())});
  }

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId userLocal =
      ctx.declareLocal(MirLocalKind::UserLocal, binding.type, scope, binding.sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(userLocal, binding.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, binding.type, zc::mv(destinationProjections), binding.type),
      MirRvalue::nominalAggregate(sourceAggregate.definition, sourceAggregate.type,
                                  zc::mv(elements)),
      MirInitializationKind::Initialize, sourceAggregate.sourceSpan.clone()));

  zc::Vector<MirProjection> returnProjections;
  identity::SemanticTypeId returnType = binding.type;
  if (returnFieldProjection != zc::none) {
    const auto& projection = ZC_ASSERT_NONNULL(returnFieldProjection);
    returnType = projection.type;
    returnProjections.add(MirProjection::field(projection.field, binding.type, projection.type));
  }
  auto returnOperand = placeUse(
      proofs, copyMarker, MirPlace(userLocal, binding.type, zc::mv(returnProjections), returnType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers a by-value aggregate caller:
/// `fn entry() -> T { let p: P = P { ..constants.. }; return f(p); }`. One
/// UserLocal of nominal type P at localId(1) is initialized from constant
/// elements, one Temporary of the call result type at localId(2) receives the
/// direct call, and the whole user local is copied as the call's sole by-value
/// argument. Block 1 calls and continues to block 2, which returns the result
/// temporary.
zc::Maybe<RecursiveFunctionProduct> buildByValueAggregateCallReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.receiver != zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  if (block.statements.size() != 2 || declaration.parameters.size() != 0) { return zc::none; }
  auto sourceReturn = returnFor(hirModule, block.statements[1]);
  auto sourceLocal = localFor(hirModule, block.statements[0]);
  if (sourceReturn == zc::none || sourceLocal == zc::none) { return zc::none; }
  const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
  if (binding.local.ordinal() != 1 || binding.initializer == zc::none) { return zc::none; }
  hir::HirNodeId initializerNode;
  ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
  auto aggregate = aggregateFor(hirModule, initializerNode);
  auto call = callFor(hirModule, ZC_ASSERT_NONNULL(sourceReturn).value);
  if (aggregate == zc::none || call == zc::none) { return zc::none; }
  const auto& sourceAggregate = ZC_ASSERT_NONNULL(aggregate);
  const auto& sourceCall = ZC_ASSERT_NONNULL(call);
  if (sourceAggregate.type != binding.type || sourceCall.resultType != declaration.resultType ||
      sourceCall.arguments.size() != 1 || sourceCall.arguments[0].value != zc::none ||
      sourceCall.arguments[0].parameter != zc::none ||
      sourceCall.arguments[0].local != binding.local ||
      sourceCall.arguments[0].type != binding.type) {
    return zc::none;
  }
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) { return zc::none; }

  zc::Vector<MirNominalAggregateElement> elements;
  for (const auto& element : sourceAggregate.elements) {
    elements.add(MirNominalAggregateElement{
        element.field, MirOperand::constant(element.type, element.value.clone())});
  }

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId userLocal =
      ctx.declareLocal(MirLocalKind::UserLocal, binding.type, scope, binding.sourceSpan.clone());
  if (userLocal.ordinal() != 1) { return zc::none; }
  const MirLocalId resultLocal = ctx.declareLocal(MirLocalKind::Temporary, sourceCall.resultType,
                                                  scope, sourceCall.sourceSpan.clone());
  if (resultLocal.ordinal() != 2) { return zc::none; }

  // Block 1: initialize the aggregate local, copy it as the by-value call
  // argument, and call into block 2.
  (void)ctx.beginBlock(scope);
  ctx.appendStatement(MirStatement::storageLive(userLocal, binding.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, binding.type, zc::mv(destinationProjections), binding.type),
      MirRvalue::nominalAggregate(sourceAggregate.definition, sourceAggregate.type,
                                  zc::mv(elements)),
      MirInitializationKind::Initialize, sourceAggregate.sourceSpan.clone()));
  ctx.appendStatement(MirStatement::storageLive(resultLocal, sourceCall.sourceSpan.clone()));
  zc::Vector<MirProjection> argumentProjections;
  auto argumentOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, binding.type, zc::mv(argumentProjections), binding.type));
  if (argumentOperand == zc::none) { return zc::none; }
  zc::Vector<MirOperand> arguments;
  arguments.add(zc::mv(ZC_ASSERT_NONNULL(argumentOperand)));
  zc::Maybe<MirBlockId> noUnwind;
  zc::Vector<MirProjection> resultProjections;
  ctx.terminateBlock(
      MirTerminator::call(sourceCall.callee, zc::mv(arguments), MirCallEffect::noActivation(),
                          MirPlace(resultLocal, sourceCall.resultType, zc::mv(resultProjections),
                                   sourceCall.resultType),
                          blockId(2), zc::mv(noUnwind), sourceCall.sourceSpan.clone()));

  // Block 2: return the result temporary.
  (void)ctx.beginBlock(scope);
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(resultLocal, sourceCall.resultType,
                                         zc::mv(returnProjections), sourceCall.resultType));
  if (returnOperand == zc::none) { return zc::none; }
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers a scalar-local direct-call caller:
/// `fn entry() -> i32 { let a: i32 = <literal>; return f(a); }`. One UserLocal
/// of scalar type at localId(1) is initialized from a constant, one Temporary
/// of the call result type at localId(2) receives the direct call, and the
/// scalar user local is copied as the call's sole by-value argument. Block 1
/// calls and continues to block 2, which returns the result temporary.
zc::Maybe<RecursiveFunctionProduct> buildScalarLocalCallReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.receiver != zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  if (block.statements.size() != 2 || declaration.parameters.size() != 0) { return zc::none; }
  auto sourceReturn = returnFor(hirModule, block.statements[1]);
  auto sourceLocal = localFor(hirModule, block.statements[0]);
  if (sourceReturn == zc::none || sourceLocal == zc::none) { return zc::none; }
  const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
  if (binding.local.ordinal() != 1 || binding.initializer == zc::none) { return zc::none; }
  hir::HirNodeId initializerNode;
  ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
  auto literal = expressionFor(hirModule, initializerNode);
  auto call = callFor(hirModule, ZC_ASSERT_NONNULL(sourceReturn).value);
  if (literal == zc::none || call == zc::none) { return zc::none; }
  const auto& sourceLiteral = ZC_ASSERT_NONNULL(literal);
  const auto& sourceCall = ZC_ASSERT_NONNULL(call);
  if (sourceLiteral.type != binding.type || sourceCall.resultType != declaration.resultType ||
      sourceCall.arguments.size() != 1 || sourceCall.arguments[0].value != zc::none ||
      sourceCall.arguments[0].parameter != zc::none ||
      sourceCall.arguments[0].local != binding.local ||
      sourceCall.arguments[0].type != binding.type) {
    return zc::none;
  }
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) { return zc::none; }

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId userLocal =
      ctx.declareLocal(MirLocalKind::UserLocal, binding.type, scope, binding.sourceSpan.clone());
  if (userLocal.ordinal() != 1) { return zc::none; }
  const MirLocalId resultLocal = ctx.declareLocal(MirLocalKind::Temporary, sourceCall.resultType,
                                                  scope, sourceCall.sourceSpan.clone());
  if (resultLocal.ordinal() != 2) { return zc::none; }

  // Block 1: initialize the scalar local from a constant, copy it as the
  // by-value call argument, and call into block 2.
  (void)ctx.beginBlock(scope);
  ctx.appendStatement(MirStatement::storageLive(userLocal, binding.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, binding.type, zc::mv(destinationProjections), binding.type),
      MirRvalue::use(MirOperand::constant(binding.type, sourceLiteral.value.clone())),
      MirInitializationKind::Initialize, sourceLiteral.sourceSpan.clone()));
  ctx.appendStatement(MirStatement::storageLive(resultLocal, sourceCall.sourceSpan.clone()));
  zc::Vector<MirProjection> argumentProjections;
  auto argumentOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, binding.type, zc::mv(argumentProjections), binding.type));
  if (argumentOperand == zc::none) { return zc::none; }
  zc::Vector<MirOperand> arguments;
  arguments.add(zc::mv(ZC_ASSERT_NONNULL(argumentOperand)));
  zc::Maybe<MirBlockId> noUnwind;
  zc::Vector<MirProjection> resultProjections;
  ctx.terminateBlock(
      MirTerminator::call(sourceCall.callee, zc::mv(arguments), MirCallEffect::noActivation(),
                          MirPlace(resultLocal, sourceCall.resultType, zc::mv(resultProjections),
                                   sourceCall.resultType),
                          blockId(2), zc::mv(noUnwind), sourceCall.sourceSpan.clone()));

  // Block 2: return the result temporary.
  (void)ctx.beginBlock(scope);
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(resultLocal, sourceCall.resultType,
                                         zc::mv(returnProjections), sourceCall.resultType));
  if (returnOperand == zc::none) { return zc::none; }
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn m(this) -> T { let x = <literal>; return x; }` on a shared
/// receiver: one root scope, one receiver parameter local at localId(1), one
/// UserLocal at localId(2), StorageLive followed by an Initialize Assign of the
/// scalar constant, and a Return of the copy/move place-use of the user local.
/// The receiver is unused by the body and folds away at LIR.
zc::Maybe<RecursiveFunctionProduct> buildMethodScalarLocalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer, const hir::HirReturnStatement& sourceReturn,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver == zc::none) return zc::none;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  (void)ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope,
                         receiver.sourceSpan.clone());
  const MirLocalId userLocal = ctx.declareLocal(MirLocalKind::UserLocal, sourceLocal.type, scope,
                                                sourceLocal.sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(userLocal, sourceLocal.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, sourceLocal.type, zc::mv(destinationProjections), sourceLocal.type),
      MirRvalue::use(MirOperand::constant(sourceLocal.type, initializer.value.clone())),
      MirInitializationKind::Initialize, initializer.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, sourceLocal.type, zc::mv(returnProjections), sourceLocal.type));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn m(this) -> T { this.field = <constant>; return this.field; }`
/// on a mutable receiver: one root scope, one receiver parameter local carrying
/// the mutable reference, one Overwrite Assign to the [Dereference, Field] place
/// with a constant-use rvalue, and a Return of a copy/move place-use of that same
/// projected place.
zc::Maybe<RecursiveFunctionProduct> buildReceiverFieldWriteReturn(
    const hir::HirFunctionDeclaration& declaration,
    const hir::HirParameterFieldWriteStatement& sourceWrite,
    const hir::HirScalarLiteralExpression& writeLiteral,
    const hir::HirParameterFieldProjectionExpression& projection,
    const hir::HirReturnStatement& sourceReturn,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (projection.type != declaration.resultType || sourceWrite.type != declaration.resultType ||
      writeLiteral.type != declaration.resultType || sourceWrite.field != projection.field ||
      sourceWrite.parameter != receiver.key) {
    return zc::none;
  }
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId receiverLocal =
      ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  zc::Vector<MirProjection> writeProjections;
  writeProjections.add(MirProjection::dereference(receiver.type, projection.receiverType));
  writeProjections.add(
      MirProjection::field(projection.field, projection.receiverType, projection.type));
  ctx.appendStatement(MirStatement::assign(
      MirPlace(receiverLocal, receiver.type, zc::mv(writeProjections), projection.type),
      MirRvalue::use(MirOperand::constant(projection.type, writeLiteral.value.clone())),
      MirInitializationKind::Overwrite, sourceWrite.sourceSpan.clone()));

  zc::Vector<MirProjection> returnProjections;
  returnProjections.add(MirProjection::dereference(receiver.type, projection.receiverType));
  returnProjections.add(
      MirProjection::field(projection.field, projection.receiverType, projection.type));
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(receiverLocal, receiver.type, zc::mv(returnProjections), projection.type));
  if (returnOperand == zc::none) return zc::none;

  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `mutating fn m(this, x: T) { this.field = x; }` with a Unit
/// result: one root scope with the receiver parameter local at localId(1) and
/// the ordinary parameter local at localId(2), one Overwrite Assign to the
/// [Dereference, Field] place whose rvalue is a copy/move place-use of the
/// parameter local, and a void Return carrying no value.
zc::Maybe<RecursiveFunctionProduct> buildReceiverFieldWriteVoidReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::HirParameterFieldWriteStatement& sourceWrite,
    const hir::HirParameterReferenceExpression& writeParameter,
    const type::SemanticTypeStore& semanticTypes,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none ||
      declaration.parameters.size() != 1) {
    return zc::none;
  }
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (!isUnitSemanticType(semanticTypes, declaration.resultType)) return zc::none;
  if (sourceWrite.parameter != receiver.key || sourceWrite.value != writeParameter.node ||
      writeParameter.parameter != declaration.parameters[0].key ||
      writeParameter.type != declaration.parameters[0].type ||
      writeParameter.type != sourceWrite.type ||
      writeParameter.category != hir::HirValueCategory::Place) {
    return zc::none;
  }
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId receiverLocal =
      ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());
  const MirLocalId parameterLocal =
      ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[0].type, scope,
                       declaration.parameters[0].sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  zc::Vector<MirProjection> writeProjections;
  writeProjections.add(MirProjection::dereference(receiver.type, sourceWrite.receiverType));
  writeProjections.add(
      MirProjection::field(sourceWrite.field, sourceWrite.receiverType, sourceWrite.type));
  zc::Vector<MirProjection> rhsProjections;
  auto parameterOperand = placeUse(
      proofs, copyMarker,
      MirPlace(parameterLocal, writeParameter.type, zc::mv(rhsProjections), writeParameter.type));
  if (parameterOperand == zc::none) return zc::none;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(receiverLocal, receiver.type, zc::mv(writeProjections), sourceWrite.type),
      MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(parameterOperand))), MirInitializationKind::Overwrite,
      sourceWrite.sourceSpan.clone()));

  // The void return has no HirReturnStatement to source a span from; the body
  // block span is the terminator span.
  ctx.terminateBlock(MirTerminator::returnVoid(block.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers the discarded-mutable-call then shared-call caller:
/// `fn f() -> T { let o = S{..}; o.set(c); return o.get(); }`. Five dense
/// locals (owner, the set-call mutable borrow, the unread Unit call result, the
/// get-call shared borrow, the get-call result) and three blocks: the first
/// initializes the owner, creates the mutable borrow, and calls the setter with
/// an ActivateMutableReceiver effect into the Unit temporary; the second
/// creates a distinct shared borrow of the same owner and calls the getter; the
/// third returns the getter result.
zc::Maybe<RecursiveFunctionProduct> buildVoidCallThenReceiverCallReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirLocalBinding& binding,
    const hir::HirNominalAggregateExpression& aggregate,
    const hir::HirReturnStatement& sourceReturn,
    const hir::HirLocalReferenceExpression& setReceiver,
    const hir::HirReceiverCallExpression& discardedCall,
    const hir::HirLocalReferenceExpression& getReceiver,
    const hir::HirReceiverCallExpression& trailingCall,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker, bool sharedDevirt) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId owner =
      ctx.declareLocal(MirLocalKind::UserLocal, binding.type, scope, binding.sourceSpan.clone());
  const MirLocalId borrowMutable = ctx.declareLocal(
      MirLocalKind::Temporary, discardedCall.receiverType, scope, setReceiver.sourceSpan.clone());
  const MirLocalId unitTemp = ctx.declareLocal(MirLocalKind::Temporary, discardedCall.resultType,
                                               scope, discardedCall.sourceSpan.clone());
  const MirLocalId borrowShared = ctx.declareLocal(
      MirLocalKind::Temporary, trailingCall.receiverType, scope, getReceiver.sourceSpan.clone());
  const MirLocalId getResult = ctx.declareLocal(MirLocalKind::Temporary, trailingCall.resultType,
                                                scope, trailingCall.sourceSpan.clone());

  // Block 1: initialize the owner, create the discarded-call borrow, then call
  // the discarded method with the borrow as its receiver argument. A mutable
  // one-argument Unit call activates the receiver borrow and passes one literal
  // argument; a shared zero-argument value call leaves the borrow unactivated
  // and passes no arguments, with the result discarded into a dead temporary.
  (void)ctx.beginBlock(scope);
  ctx.appendStatement(MirStatement::storageLive(owner, binding.sourceSpan.clone()));
  zc::Vector<MirNominalAggregateElement> elements;
  for (const auto& element : aggregate.elements) {
    elements.add(MirNominalAggregateElement{
        element.field, MirOperand::constant(element.type, element.value.clone())});
  }
  zc::Vector<MirProjection> ownerProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(owner, binding.type, zc::mv(ownerProjections), binding.type),
      MirRvalue::nominalAggregate(aggregate.definition, aggregate.type, zc::mv(elements)),
      MirInitializationKind::Initialize, aggregate.sourceSpan.clone()));
  ctx.appendStatement(MirStatement::storageLive(borrowMutable, setReceiver.sourceSpan.clone()));
  zc::Vector<MirProjection> borrowMutableDest;
  zc::Vector<MirProjection> borrowMutableSource;
  ctx.appendStatement(MirStatement::borrowCreation(
      MirPlace(borrowMutable, discardedCall.receiverType, zc::mv(borrowMutableDest),
               discardedCall.receiverType),
      sharedDevirt ? MirBorrowKind::Shared : MirBorrowKind::Mutable,
      MirPlace(owner, binding.type, zc::mv(borrowMutableSource), binding.type),
      setReceiver.sourceSpan.clone()));
  ctx.appendStatement(MirStatement::storageLive(unitTemp, discardedCall.sourceSpan.clone()));
  zc::Vector<MirProjection> receiverArgumentProjections;
  auto receiverArgument =
      placeUse(proofs, copyMarker,
               MirPlace(borrowMutable, discardedCall.receiverType,
                        zc::mv(receiverArgumentProjections), discardedCall.receiverType));
  if (receiverArgument == zc::none) return zc::none;
  zc::Vector<MirOperand> setArguments;
  setArguments.add(zc::mv(ZC_ASSERT_NONNULL(receiverArgument)));
  if (!sharedDevirt) {
    setArguments.add(
        MirOperand::constant(discardedCall.arguments[0].type,
                             ZC_ASSERT_NONNULL(discardedCall.arguments[0].value).clone()));
  }
  zc::Maybe<MirBlockId> noUnwindSet;
  zc::Vector<MirProjection> unitDestProjections;
  ctx.terminateBlock(
      MirTerminator::call(discardedCall.callee, zc::mv(setArguments),
                          sharedDevirt ? MirCallEffect::noActivation()
                                       : MirCallEffect::activateMutableReceiver(borrowMutable),
                          MirPlace(unitTemp, discardedCall.resultType, zc::mv(unitDestProjections),
                                   discardedCall.resultType),
                          blockId(2), zc::mv(noUnwindSet), discardedCall.sourceSpan.clone()));

  // Block 2: a distinct shared borrow of the same owner, then the getter call.
  (void)ctx.beginBlock(scope);
  ctx.appendStatement(MirStatement::storageLive(borrowShared, getReceiver.sourceSpan.clone()));
  zc::Vector<MirProjection> borrowSharedDest;
  zc::Vector<MirProjection> borrowSharedSource;
  ctx.appendStatement(MirStatement::borrowCreation(
      MirPlace(borrowShared, trailingCall.receiverType, zc::mv(borrowSharedDest),
               trailingCall.receiverType),
      MirBorrowKind::Shared,
      MirPlace(owner, binding.type, zc::mv(borrowSharedSource), binding.type),
      getReceiver.sourceSpan.clone()));
  ctx.appendStatement(MirStatement::storageLive(getResult, trailingCall.sourceSpan.clone()));
  zc::Vector<MirProjection> getReceiverArgumentProjections;
  auto getReceiverArgument =
      placeUse(proofs, copyMarker,
               MirPlace(borrowShared, trailingCall.receiverType,
                        zc::mv(getReceiverArgumentProjections), trailingCall.receiverType));
  if (getReceiverArgument == zc::none) return zc::none;
  zc::Vector<MirOperand> getArguments;
  getArguments.add(zc::mv(ZC_ASSERT_NONNULL(getReceiverArgument)));
  zc::Maybe<MirBlockId> noUnwindGet;
  zc::Vector<MirProjection> getResultProjections;
  ctx.terminateBlock(
      MirTerminator::call(trailingCall.callee, zc::mv(getArguments), MirCallEffect::noActivation(),
                          MirPlace(getResult, trailingCall.resultType, zc::mv(getResultProjections),
                                   trailingCall.resultType),
                          blockId(3), zc::mv(noUnwindGet), trailingCall.sourceSpan.clone()));

  // Block 3: return the getter result.
  (void)ctx.beginBlock(scope);
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(getResult, trailingCall.resultType,
                                         zc::mv(returnProjections), trailingCall.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn m(this, flag: bool) -> T { if (flag) { return a; } else {
/// return b; } }` on a shared receiver: the receiver is the leading parameter
/// local at localId(1), the ordinary parameters follow, and a FunctionResult
/// local dominates the four-block diamond (SwitchInt, two arm blocks with an
/// Initialize Assign and Goto, and an empty join returning the result).
zc::Maybe<RecursiveFunctionProduct> buildMethodConditionalReturn(
    const hir::HirFunctionDeclaration& declaration,
    const hir::HirConditionalExpression& conditional,
    const hir::HirParameterReferenceExpression& condition,
    const hir::HirReturnStatement& sourceReturn, const hir::VerifiedHirModule& hirModule,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) return zc::none;
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  if (conditional.type != declaration.resultType ||
      conditional.category != hir::HirValueCategory::Value) {
    return zc::none;
  }
  // The bare condition must resolve to one of the ordinary parameters.
  size_t conditionIndex = 0;
  bool conditionResolved = false;
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == condition.parameter) {
      conditionIndex = i;
      conditionResolved = true;
      break;
    }
  }
  if (!conditionResolved || condition.type != declaration.parameters[conditionIndex].type) {
    return zc::none;
  }
  // Each arm is a scalar literal in this slice.
  auto thenLiteral = expressionFor(hirModule, conditional.thenReturnValue);
  auto elseLiteral = expressionFor(hirModule, conditional.elseReturnValue);
  if (thenLiteral == zc::none || elseLiteral == zc::none) return zc::none;
  if (ZC_ASSERT_NONNULL(thenLiteral).type != declaration.resultType ||
      ZC_ASSERT_NONNULL(elseLiteral).type != declaration.resultType) {
    return zc::none;
  }

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  (void)ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope,
                         receiver.sourceSpan.clone());
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[i].type, scope,
                     declaration.parameters[i].sourceSpan.clone());
  }
  const MirLocalId resultLocal = ctx.declareLocal(
      MirLocalKind::FunctionResult, declaration.resultType, scope, sourceReturn.sourceSpan.clone());
  if (resultLocal.ordinal() != static_cast<uint32_t>(declaration.parameters.size() + 2)) {
    return zc::none;
  }

  const auto conditionLocalMaybe =
      MirLocalId::fromOrdinal(static_cast<uint32_t>(conditionIndex + 2));
  if (conditionLocalMaybe == zc::none) return zc::none;
  const MirLocalId conditionLocal = ZC_ASSERT_NONNULL(conditionLocalMaybe);
  zc::Vector<MirProjection> conditionProjections;
  auto discriminant = placeUse(
      proofs, copyMarker,
      MirPlace(conditionLocal, condition.type, zc::mv(conditionProjections), condition.type));
  if (discriminant == zc::none) return zc::none;
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(resultLocal, declaration.resultType,
                                         zc::mv(returnProjections), declaration.resultType));
  if (returnOperand == zc::none) return zc::none;

  // Block 1: StorageLive(result); SwitchInt(bool) { true -> 2, false -> 3 },
  // default 3.
  (void)ctx.beginBlock(scope);
  ctx.appendStatement(MirStatement::storageLive(resultLocal, sourceReturn.sourceSpan.clone()));
  zc::Vector<MirSwitchIntArm> arms;
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  ctx.terminateBlock(MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                              blockId(3), conditional.sourceSpan.clone()));
  // Blocks 2 and 3: Initialize Assign of the arm constant then Goto block 4.
  const auto armBlock = [&](const hir::HirScalarLiteralExpression& literal) {
    (void)ctx.beginBlock(scope);
    zc::Vector<MirProjection> projections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(resultLocal, declaration.resultType, zc::mv(projections), declaration.resultType),
        MirRvalue::use(MirOperand::constant(literal.type, literal.value.clone())),
        MirInitializationKind::Initialize, literal.sourceSpan.clone()));
    ctx.terminateBlock(MirTerminator::gotoTarget(blockId(4), literal.sourceSpan.clone()));
  };
  armBlock(ZC_ASSERT_NONNULL(thenLiteral));
  armBlock(ZC_ASSERT_NONNULL(elseLiteral));
  // Block 4: empty join returning the result.
  (void)ctx.beginBlock(scope);
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fn m(this) -> T { return this.n(); }` on a shared receiver:
/// the leading receiver parameter is forwarded directly as the call's receiver
/// argument (a shared reborrow, so no BorrowCreation temporary), one result
/// temporary receives the call, and the continuation returns it.
zc::Maybe<RecursiveFunctionProduct> buildReceiverSelfCallReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirReceiverCallExpression& call, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.receiver == zc::none || declaration.unsafeBlock != zc::none) { return zc::none; }
  const auto& receiver = ZC_ASSERT_NONNULL(declaration.receiver);
  if (call.receiver != hir::HirNodeId() ||
      call.receiverMode != checker::checked::ReceiverMode::Shared ||
      call.receiverAdjustments.size() != 1 ||
      call.receiverAdjustments[0] != checker::checked::ReceiverAdjustmentStep::ReborrowShared ||
      call.arguments.size() != 0 || call.resultType != declaration.resultType) {
    return zc::none;
  }
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId receiverLocal =
      ctx.declareLocal(MirLocalKind::Parameter, receiver.type, scope, receiver.sourceSpan.clone());
  const MirLocalId resultLocal =
      ctx.declareLocal(MirLocalKind::Temporary, call.resultType, scope, call.sourceSpan.clone());

  // Block 1: storage-live the result, then call forwarding the receiver
  // parameter directly as the sole receiver argument.
  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(resultLocal, call.sourceSpan.clone()));
  zc::Vector<MirProjection> receiverProjections;
  auto receiverArgument =
      placeUse(proofs, copyMarker,
               MirPlace(receiverLocal, receiver.type, zc::mv(receiverProjections), receiver.type));
  if (receiverArgument == zc::none) return zc::none;
  zc::Vector<MirOperand> arguments;
  arguments.add(zc::mv(ZC_ASSERT_NONNULL(receiverArgument)));
  zc::Maybe<MirBlockId> noUnwind;
  zc::Vector<MirProjection> resultProjections;
  auto callTerminator = MirTerminator::call(
      call.callee, zc::mv(arguments), MirCallEffect::noActivation(),
      MirPlace(resultLocal, call.resultType, zc::mv(resultProjections), call.resultType),
      blockId(2), zc::mv(noUnwind), call.sourceSpan.clone());
  ctx.terminateBlock(zc::mv(callTerminator));

  // Block 2: return the result temporary.
  (void)ctx.beginBlock(scope);
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(resultLocal, call.resultType, zc::mv(returnProjections), call.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Method, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers K leading scalar user locals followed by one comparison
/// conditional with two literal arms:
/// `let a: i32 = <lit/param/local>; ...; if (x CMP y) { return lt; } else {
/// return le; }`. Parameter locals occupy localId(1..P); user local i occupies
/// localId(P+i+1); the function result follows at P+K+1 and the bool comparison
/// temporary at P+K+2. The entry block initializes every leading local,
/// computes the comparison temporary, and switches on it; the two arm blocks
/// initialize the result and the join returns it.
zc::Maybe<RecursiveFunctionProduct> buildLeadingLocalConditionalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.unsafeBlock != zc::none || declaration.receiver != zc::none) return zc::none;
  if (block.statements.empty()) return zc::none;
  const size_t bindingCount = block.statements.size() - 1;

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  auto sourceReturn = returnFor(hirModule, block.statements[bindingCount]);
  if (sourceReturn == zc::none) return zc::none;

  zc::Vector<const hir::HirLocalBinding*> bindings;
  for (size_t i = 0; i < bindingCount; ++i) {
    auto sourceLocal = localFor(hirModule, block.statements[i]);
    if (sourceLocal == zc::none) return zc::none;
    const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
    if (binding.local.ordinal() != static_cast<uint32_t>(i + 1) ||
        binding.initializer == zc::none) {
      return zc::none;
    }
    bindings.add(&binding);
  }

  const hir::HirNodeId conditionalNode = ZC_ASSERT_NONNULL(sourceReturn).value;
  auto conditional = conditionalFor(hirModule, conditionalNode);
  if (conditional == zc::none) return zc::none;
  const auto& conditionalValue = ZC_ASSERT_NONNULL(conditional);
  if (conditionalValue.type != declaration.resultType ||
      conditionalValue.category != hir::HirValueCategory::Value) {
    return zc::none;
  }
  auto equality = primitiveBinaryFor(hirModule, conditionalValue.condition);
  if (equality == zc::none) return zc::none;
  const auto& comparisonValue = ZC_ASSERT_NONNULL(equality);
  auto comparison = comparisonOperatorFor(comparisonValue.operation);
  auto arithmetic = arithmeticOperatorFor(comparisonValue.operation);
  if (comparison == zc::none && arithmetic == zc::none) return zc::none;
  // Resolve one conditional arm: a scalar literal, a parameter place-use, a
  // leading-local place-use, or a primitive binary (arithmetic/comparison)
  // operation over two leaf operands.
  struct ArmValue {
    enum class Kind { Literal, Parameter, Local, Binary };
    Kind kind;
    const hir::HirScalarLiteralExpression* literal = nullptr;
    const hir::HirParameterReferenceExpression* parameter = nullptr;
    const hir::HirLocalReferenceExpression* local = nullptr;
    const hir::HirPrimitiveBinaryExpression* binary = nullptr;
  };
  auto resolveArm = [&](hir::HirNodeId armNode) -> zc::Maybe<ArmValue> {
    if (auto literal = expressionFor(hirModule, armNode); literal != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(literal);
      if (value.type != declaration.resultType) return zc::none;
      return ArmValue{ArmValue::Kind::Literal, &value, nullptr, nullptr, nullptr};
    }
    if (auto parameter = parameterReferenceFor(hirModule, armNode); parameter != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(parameter);
      if (value.type != declaration.resultType || value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      return ArmValue{ArmValue::Kind::Parameter, nullptr, &value, nullptr, nullptr};
    }
    if (auto local = localReferenceFor(hirModule, armNode); local != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(local);
      if (value.type != declaration.resultType || value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      return ArmValue{ArmValue::Kind::Local, nullptr, nullptr, &value, nullptr};
    }
    if (auto binary = primitiveBinaryFor(hirModule, armNode); binary != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(binary);
      if (value.type != declaration.resultType || value.category != hir::HirValueCategory::Value) {
        return zc::none;
      }
      return ArmValue{ArmValue::Kind::Binary, nullptr, nullptr, nullptr, &value};
    }
    return zc::none;
  };
  auto thenArm = resolveArm(conditionalValue.thenReturnValue);
  auto elseArm = resolveArm(conditionalValue.elseReturnValue);
  if (thenArm == zc::none || elseArm == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (size_t p = 0; p < declaration.parameters.size(); ++p) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[p].type,
                                         scope, declaration.parameters[p].sourceSpan.clone()));
  }
  zc::Vector<MirLocalId> userLocals;
  for (size_t i = 0; i < bindingCount; ++i) {
    userLocals.add(ctx.declareLocal(MirLocalKind::UserLocal, bindings[i]->type, scope,
                                    bindings[i]->sourceSpan.clone()));
  }
  const MirLocalId resultLocal =
      ctx.declareLocal(MirLocalKind::FunctionResult, declaration.resultType, scope,
                       ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone());
  if (resultLocal.ordinal() !=
      static_cast<uint32_t>(declaration.parameters.size() + bindingCount + 1)) {
    return zc::none;
  }
  const MirLocalId conditionTemp = ctx.declareLocal(MirLocalKind::Temporary, comparisonValue.type,
                                                    scope, comparisonValue.sourceSpan.clone());
  if (conditionTemp.ordinal() !=
      static_cast<uint32_t>(declaration.parameters.size() + bindingCount + 2)) {
    return zc::none;
  }

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  // Leading local preamble: StorageLive plus an Initialize Assign from a scalar
  // constant or a copy of a parameter or an earlier user local.
  for (size_t i = 0; i < bindingCount; ++i) {
    const auto& binding = *bindings[i];
    hir::HirNodeId initializerNode;
    ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
    auto literal = expressionFor(hirModule, initializerNode);
    auto localReference = localReferenceFor(hirModule, initializerNode);
    auto parameterReference = parameterReferenceFor(hirModule, initializerNode);
    if (aggregateFor(hirModule, initializerNode) != zc::none) { return zc::none; }
    if (auto binary = primitiveBinaryFor(hirModule, initializerNode); binary != zc::none) {
      const auto& value = ZC_ASSERT_NONNULL(binary);
      if (value.category != hir::HirValueCategory::Value || value.type != binding.type) {
        return zc::none;
      }
      if (primitiveBinaryFor(hirModule, value.left) != zc::none ||
          primitiveBinaryFor(hirModule, value.right) != zc::none) {
        return zc::none;
      }
      auto arithmetic = arithmeticOperatorFor(value.operation);
      if (arithmetic == zc::none) return zc::none;
      if (value.type != value.operandType) return zc::none;
      auto left = binaryLeafOperand(hirModule, declaration, value.left, parameterLocals,
                                    userLocals.asPtr(), i, value.operandType, proofs, copyMarker);
      auto right = binaryLeafOperand(hirModule, declaration, value.right, parameterLocals,
                                     userLocals.asPtr(), i, value.operandType, proofs, copyMarker);
      if (left == zc::none || right == zc::none) return zc::none;
      ctx.appendStatement(MirStatement::storageLive(userLocals[i], binding.sourceSpan.clone()));
      zc::Vector<MirProjection> destinationProjections;
      ctx.appendStatement(MirStatement::assign(
          MirPlace(userLocals[i], binding.type, zc::mv(destinationProjections), binding.type),
          MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic), zc::mv(ZC_ASSERT_NONNULL(left)),
                                zc::mv(ZC_ASSERT_NONNULL(right)), value.type),
          MirInitializationKind::Initialize, value.sourceSpan.clone()));
      continue;
    }
    const int present = (literal != zc::none ? 1 : 0) + (localReference != zc::none ? 1 : 0) +
                        (parameterReference != zc::none ? 1 : 0);
    if (present != 1) return zc::none;
    zc::Maybe<MirRvalue> rvalue;
    identity::SourceSpan assignSpan = binding.sourceSpan.clone();
    ZC_IF_SOME(value, literal) {
      if (value.type != binding.type || value.category != hir::HirValueCategory::Value) {
        return zc::none;
      }
      rvalue = MirRvalue::use(MirOperand::constant(binding.type, value.value.clone()));
      assignSpan = value.sourceSpan.clone();
    }
    ZC_IF_SOME(value, localReference) {
      if (value.type != binding.type || value.category != hir::HirValueCategory::Place ||
          value.local.ordinal() == 0 || value.local.ordinal() > static_cast<uint32_t>(i)) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(userLocals[value.local.ordinal() - 1], binding.type,
                                       zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      assignSpan = value.sourceSpan.clone();
    }
    ZC_IF_SOME(value, parameterReference) {
      auto parameterIndex = parameterIndexFor(declaration, value.parameter);
      if (parameterIndex == zc::none || value.type != binding.type ||
          value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                                       binding.type, zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      assignSpan = value.sourceSpan.clone();
    }
    if (rvalue == zc::none) return zc::none;
    ctx.appendStatement(MirStatement::storageLive(userLocals[i], binding.sourceSpan.clone()));
    zc::Vector<MirProjection> destinationProjections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(userLocals[i], binding.type, zc::mv(destinationProjections), binding.type),
        zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize, zc::mv(assignSpan)));
  }

  auto leftOperand = binaryLeafOperand(hirModule, declaration, comparisonValue.left,
                                       parameterLocals, userLocals.asPtr(), bindingCount,
                                       comparisonValue.operandType, proofs, copyMarker);
  auto rightOperand = binaryLeafOperand(hirModule, declaration, comparisonValue.right,
                                        parameterLocals, userLocals.asPtr(), bindingCount,
                                        comparisonValue.operandType, proofs, copyMarker);
  if (leftOperand == zc::none || rightOperand == zc::none) return zc::none;

  ctx.appendStatement(
      MirStatement::storageLive(resultLocal, ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));
  ctx.appendStatement(MirStatement::storageLive(conditionTemp, comparisonValue.sourceSpan.clone()));
  zc::Vector<MirProjection> tempProjections;
  // A relational comparison lowers to a comparison rvalue; a logical
  // short-circuit operator lowers to an arithmetic rvalue (BitAnd/BitOr)
  // because the admitted slice only accepts side-effect-free operands.
  MirRvalue conditionRvalue =
      comparison != zc::none
          ? MirRvalue::comparison(ZC_ASSERT_NONNULL(comparison),
                                  zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                  zc::mv(ZC_ASSERT_NONNULL(rightOperand)), comparisonValue.type)
          : MirRvalue::arithmetic(ZC_ASSERT_NONNULL(arithmetic),
                                  zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                                  zc::mv(ZC_ASSERT_NONNULL(rightOperand)), comparisonValue.type);
  ctx.appendStatement(MirStatement::assign(
      MirPlace(conditionTemp, comparisonValue.type, zc::mv(tempProjections), comparisonValue.type),
      zc::mv(conditionRvalue), MirInitializationKind::Initialize,
      comparisonValue.sourceSpan.clone()));
  zc::Vector<MirProjection> discriminantProjections;
  auto discriminant = placeUse(proofs, copyMarker,
                               MirPlace(conditionTemp, comparisonValue.type,
                                        zc::mv(discriminantProjections), comparisonValue.type));
  if (discriminant == zc::none) return zc::none;
  zc::Vector<MirSwitchIntArm> arms;
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  ctx.terminateBlock(MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                              blockId(3), conditionalValue.sourceSpan.clone()));
  // Resolve one arm to its MIR rvalue and source span. A literal arm lowers to
  // a constant use; a parameter arm lowers to a place-use of the parameter
  // local; a local arm lowers to a place-use of the leading user local; a
  // binary arm lowers to an arithmetic or comparison rvalue over two leaf
  // operands resolved via binaryLeafOperand.
  auto armRvalue = [&](const ArmValue& arm) -> zc::Maybe<MirRvalue> {
    switch (arm.kind) {
      case ArmValue::Kind::Literal: {
        const auto& literal = ZC_ASSERT_NONNULL(arm.literal);
        return MirRvalue::use(MirOperand::constant(literal.type, literal.value.clone()));
      }
      case ArmValue::Kind::Parameter: {
        const auto& parameter = ZC_ASSERT_NONNULL(arm.parameter);
        auto parameterIndex = parameterIndexFor(declaration, parameter.parameter);
        if (parameterIndex == zc::none) return zc::none;
        zc::Vector<MirProjection> projections;
        auto operand =
            placeUse(proofs, copyMarker,
                     MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                              declaration.resultType, zc::mv(projections), declaration.resultType));
        if (operand == zc::none) return zc::none;
        return MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      }
      case ArmValue::Kind::Local: {
        const auto& local = ZC_ASSERT_NONNULL(arm.local);
        if (local.local.ordinal() == 0 || local.local.ordinal() > bindingCount) return zc::none;
        zc::Vector<MirProjection> projections;
        auto operand =
            placeUse(proofs, copyMarker,
                     MirPlace(userLocals[local.local.ordinal() - 1], declaration.resultType,
                              zc::mv(projections), declaration.resultType));
        if (operand == zc::none) return zc::none;
        return MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      }
      case ArmValue::Kind::Binary: {
        const auto& binary = ZC_ASSERT_NONNULL(arm.binary);
        auto armArithmetic = arithmeticOperatorFor(binary.operation);
        auto armComparison = comparisonOperatorFor(binary.operation);
        if (armArithmetic == zc::none && armComparison == zc::none) return zc::none;
        auto left = binaryLeafOperand(hirModule, declaration, binary.left, parameterLocals,
                                      userLocals.asPtr(), bindingCount, binary.operandType, proofs,
                                      copyMarker);
        auto right = binaryLeafOperand(hirModule, declaration, binary.right, parameterLocals,
                                       userLocals.asPtr(), bindingCount, binary.operandType, proofs,
                                       copyMarker);
        if (left == zc::none || right == zc::none) return zc::none;
        if (armArithmetic != zc::none) {
          return MirRvalue::arithmetic(ZC_ASSERT_NONNULL(armArithmetic),
                                       zc::mv(ZC_ASSERT_NONNULL(left)),
                                       zc::mv(ZC_ASSERT_NONNULL(right)), binary.type);
        }
        return MirRvalue::comparison(ZC_ASSERT_NONNULL(armComparison),
                                     zc::mv(ZC_ASSERT_NONNULL(left)),
                                     zc::mv(ZC_ASSERT_NONNULL(right)), binary.type);
      }
    }
    return zc::none;
  };
  auto armSpan = [](const ArmValue& arm) -> const identity::SourceSpan& {
    switch (arm.kind) {
      case ArmValue::Kind::Literal:
        return ZC_ASSERT_NONNULL(arm.literal).sourceSpan;
      case ArmValue::Kind::Parameter:
        return ZC_ASSERT_NONNULL(arm.parameter).sourceSpan;
      case ArmValue::Kind::Local:
        return ZC_ASSERT_NONNULL(arm.local).sourceSpan;
      case ArmValue::Kind::Binary:
        return ZC_ASSERT_NONNULL(arm.binary).sourceSpan;
    }
    ZC_UNREACHABLE
  };
  auto thenRvalue = armRvalue(ZC_ASSERT_NONNULL(thenArm));
  auto elseRvalue = armRvalue(ZC_ASSERT_NONNULL(elseArm));
  if (thenRvalue == zc::none || elseRvalue == zc::none) return zc::none;
  const auto armBlock = [&](MirRvalue rvalue, const identity::SourceSpan& span) {
    (void)ctx.beginBlock(scope);
    zc::Vector<MirProjection> projections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(resultLocal, declaration.resultType, zc::mv(projections), declaration.resultType),
        zc::mv(rvalue), MirInitializationKind::Initialize, span.clone()));
    ctx.terminateBlock(MirTerminator::gotoTarget(blockId(4), span.clone()));
  };
  armBlock(zc::mv(ZC_ASSERT_NONNULL(thenRvalue)), armSpan(ZC_ASSERT_NONNULL(thenArm)));
  armBlock(zc::mv(ZC_ASSERT_NONNULL(elseRvalue)), armSpan(ZC_ASSERT_NONNULL(elseArm)));
  (void)ctx.beginBlock(scope);
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(resultLocal, declaration.resultType,
                                         zc::mv(returnProjections), declaration.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// Chained conditional return: a free function whose sole statement is a
/// return of a chain of nested conditionals. Each conditional has an equality
/// comparison (parameter == literal) as its condition, a scalar literal as its
/// then-value, and either another conditional (the inner chain) or a scalar
/// literal (the final else) as its else-value. Lowers to a 2N+2 block CFG:
/// N comparison/switch blocks, N then-arm blocks, one else-arm block, and one
/// join block that returns the result.
zc::Maybe<RecursiveFunctionProduct> buildChainedConditionalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.unsafeBlock != zc::none || declaration.receiver != zc::none) return zc::none;
  if (block.statements.size() != 1) return zc::none;

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  auto sourceReturn = returnFor(hirModule, block.statements[0]);
  if (sourceReturn == zc::none) return zc::none;

  // Walk the nested conditional chain to extract N entries (equality + then
  // literal) and one else literal.
  struct ChainedEntry {
    const hir::HirPrimitiveBinaryExpression* equality;
    const hir::HirScalarLiteralExpression* thenLiteral;
  };
  zc::Vector<ChainedEntry> entries;
  const hir::HirScalarLiteralExpression* elseLiteral = nullptr;
  hir::HirNodeId currentNode = ZC_ASSERT_NONNULL(sourceReturn).value;
  while (true) {
    auto conditional = conditionalFor(hirModule, currentNode);
    if (conditional == zc::none) return zc::none;
    const auto& conditionalValue = ZC_ASSERT_NONNULL(conditional);
    if (conditionalValue.type != declaration.resultType ||
        conditionalValue.category != hir::HirValueCategory::Value) {
      return zc::none;
    }
    auto equality = primitiveBinaryFor(hirModule, conditionalValue.condition);
    if (equality == zc::none) return zc::none;
    const auto& comparisonValue = ZC_ASSERT_NONNULL(equality);
    auto comparison = comparisonOperatorFor(comparisonValue.operation);
    if (comparison == zc::none || ZC_ASSERT_NONNULL(comparison) != MirComparisonOperator::Eq) {
      return zc::none;
    }
    auto thenLiteral = expressionFor(hirModule, conditionalValue.thenReturnValue);
    if (thenLiteral == zc::none) return zc::none;
    if (ZC_ASSERT_NONNULL(thenLiteral).type != declaration.resultType) return zc::none;
    entries.add(ChainedEntry{&comparisonValue, &ZC_ASSERT_NONNULL(thenLiteral)});
    // The else branch is either another conditional (inner chain) or a literal
    // (final else).
    auto elseExpr = expressionFor(hirModule, conditionalValue.elseReturnValue);
    if (elseExpr != zc::none) {
      if (ZC_ASSERT_NONNULL(elseExpr).type != declaration.resultType) return zc::none;
      elseLiteral = &ZC_ASSERT_NONNULL(elseExpr);
      break;
    }
    currentNode = conditionalValue.elseReturnValue;
  }
  if (entries.size() < 2 || elseLiteral == nullptr) return zc::none;

  const size_t armCount = entries.size();
  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (size_t p = 0; p < declaration.parameters.size(); ++p) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[p].type,
                                         scope, declaration.parameters[p].sourceSpan.clone()));
  }
  const MirLocalId resultLocal =
      ctx.declareLocal(MirLocalKind::FunctionResult, declaration.resultType, scope,
                       ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone());
  if (resultLocal.ordinal() != static_cast<uint32_t>(declaration.parameters.size() + 1)) {
    return zc::none;
  }
  zc::Vector<MirLocalId> conditionTemps;
  for (size_t i = 0; i < armCount; ++i) {
    conditionTemps.add(ctx.declareLocal(MirLocalKind::Temporary, entries[i].equality->type, scope,
                                        entries[i].equality->sourceSpan.clone()));
    if (conditionTemps[i].ordinal() !=
        static_cast<uint32_t>(declaration.parameters.size() + 2 + i)) {
      return zc::none;
    }
  }

  // Block layout (1-indexed ordinals):
  // Block 2k+1 (k=0..N-1): entry k — k=0 has StorageLive(result) plus
  //   StorageLive for every condition temp, then Assign(temp_k = Comparison);
  //   k>0 has just Assign(temp_k = Comparison). All entries end in SwitchInt.
  // Block 2k+2 (k=0..N-1): then arm k — Initialize resultLocal = literal_k,
  //   Goto 2N+2
  // Block 2N+1: else arm — Initialize resultLocal = else_literal, Goto 2N+2
  // Block 2N+2: join — Return resultLocal
  const uint32_t joinBlockOrdinal = static_cast<uint32_t>(2 * armCount + 2);

  for (size_t k = 0; k < armCount; ++k) {
    const auto& entry = entries[k];
    // Block 2k+1: entry k.
    (void)ctx.beginBlock(scope);
    if (k == 0) {
      ctx.appendStatement(MirStatement::storageLive(
          resultLocal, ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));
      // StorageLive every condition temp in the entry block so that all temps
      // are live on every CFG path. Without this, a temp declared in a later
      // entry block is dead on paths that branch to the join before reaching
      // that block, producing an invalid join state (dead but may-be-initialized).
      for (size_t t = 0; t < armCount; ++t) {
        ctx.appendStatement(
            MirStatement::storageLive(conditionTemps[t], entries[t].equality->sourceSpan.clone()));
      }
    }

    auto leftOperand = binaryLeafOperand(hirModule, declaration, entry.equality->left,
                                         parameterLocals, zc::ArrayPtr<MirLocalId>{}, 0,
                                         entry.equality->operandType, proofs, copyMarker);
    auto rightOperand = binaryLeafOperand(hirModule, declaration, entry.equality->right,
                                          parameterLocals, zc::ArrayPtr<MirLocalId>{}, 0,
                                          entry.equality->operandType, proofs, copyMarker);
    if (leftOperand == zc::none || rightOperand == zc::none) { return zc::none; }

    zc::Vector<MirProjection> tempProjections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(conditionTemps[k], entry.equality->type, zc::mv(tempProjections),
                 entry.equality->type),
        MirRvalue::comparison(MirComparisonOperator::Eq, zc::mv(ZC_ASSERT_NONNULL(leftOperand)),
                              zc::mv(ZC_ASSERT_NONNULL(rightOperand)), entry.equality->type),
        MirInitializationKind::Initialize, entry.equality->sourceSpan.clone()));

    zc::Vector<MirProjection> discriminantProjections;
    auto discriminant = placeUse(proofs, copyMarker,
                                 MirPlace(conditionTemps[k], entry.equality->type,
                                          zc::mv(discriminantProjections), entry.equality->type));
    if (discriminant == zc::none) return zc::none;

    const uint32_t thenBlockOrdinal = static_cast<uint32_t>(2 * k + 2);
    const uint32_t elseBlockOrdinal = (k + 1 < armCount) ? static_cast<uint32_t>(2 * (k + 1) + 1)
                                                         : static_cast<uint32_t>(2 * armCount + 1);
    zc::Vector<MirSwitchIntArm> arms;
    arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                             blockId(thenBlockOrdinal)});
    arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false),
                             blockId(elseBlockOrdinal)});
    ctx.terminateBlock(MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)),
                                                zc::mv(arms), blockId(elseBlockOrdinal),
                                                entry.equality->sourceSpan.clone()));

    // Block 2k+2: then arm k.
    (void)ctx.beginBlock(scope);
    zc::Vector<MirProjection> projections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(resultLocal, declaration.resultType, zc::mv(projections), declaration.resultType),
        MirRvalue::use(
            MirOperand::constant(entry.thenLiteral->type, entry.thenLiteral->value.clone())),
        MirInitializationKind::Initialize, entry.thenLiteral->sourceSpan.clone()));
    ctx.terminateBlock(MirTerminator::gotoTarget(blockId(joinBlockOrdinal),
                                                 entry.thenLiteral->sourceSpan.clone()));
  }

  // Block 2N+1: else arm.
  (void)ctx.beginBlock(scope);
  {
    zc::Vector<MirProjection> projections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(resultLocal, declaration.resultType, zc::mv(projections), declaration.resultType),
        MirRvalue::use(MirOperand::constant(elseLiteral->type, elseLiteral->value.clone())),
        MirInitializationKind::Initialize, elseLiteral->sourceSpan.clone()));
    ctx.terminateBlock(
        MirTerminator::gotoTarget(blockId(joinBlockOrdinal), elseLiteral->sourceSpan.clone()));
  }

  // Block 2N+2: join.
  (void)ctx.beginBlock(scope);
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(resultLocal, declaration.resultType,
                                         zc::mv(returnProjections), declaration.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// Match-guard conjunctive conditional return: a free function whose sole
/// statement is a return of a conditional whose condition is a conjunction
/// (LogicalAnd/BitAnd) of a bool parameter reference and a guard comparison.
/// The guard comparison has one parameter-reference operand and one
/// scalar-literal operand. The arms are scalar literals. Lowers to a
/// four-block diamond: the entry block evaluates the guard comparison into a
/// temporary, conjuncts it with the scrutinee into a second temporary, and
/// switches on the conjunction; the two arm blocks assign the branch literals
/// and the join block returns the result.
zc::Maybe<RecursiveFunctionProduct> buildConjunctiveConditionalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.receiver != zc::none || declaration.unsafeBlock != zc::none) return zc::none;
  if (block.statements.size() != 1) return zc::none;

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  auto sourceReturn = returnFor(hirModule, block.statements[0]);
  if (sourceReturn == zc::none) return zc::none;

  auto conditional = conditionalFor(hirModule, ZC_ASSERT_NONNULL(sourceReturn).value);
  if (conditional == zc::none) return zc::none;
  const auto& conditionalValue = ZC_ASSERT_NONNULL(conditional);
  if (conditionalValue.type != declaration.resultType ||
      conditionalValue.category != hir::HirValueCategory::Value) {
    return zc::none;
  }

  // The condition must be a conjunction (LogicalAnd lowered to BitAnd).
  auto conjunction = primitiveBinaryFor(hirModule, conditionalValue.condition);
  if (conjunction == zc::none) return zc::none;
  const auto& conjunctionValue = ZC_ASSERT_NONNULL(conjunction);
  if (conjunctionValue.operation != checker::PrimitiveOperation::LogicalAnd) return zc::none;

  // The left operand is the scrutinee bool parameter reference.
  auto scrutineeRef = parameterReferenceFor(hirModule, conjunctionValue.left);
  if (scrutineeRef == zc::none) return zc::none;
  const auto& scrutinee = ZC_ASSERT_NONNULL(scrutineeRef);
  auto scrutineeIndex = parameterIndexFor(declaration, scrutinee.parameter);
  if (scrutineeIndex == zc::none) return zc::none;
  if (scrutinee.type != declaration.parameters[ZC_ASSERT_NONNULL(scrutineeIndex)].type) {
    return zc::none;
  }

  // The right operand is the guard comparison: one parameter reference and one
  // scalar literal.
  auto guardComparison = primitiveBinaryFor(hirModule, conjunctionValue.right);
  if (guardComparison == zc::none) return zc::none;
  const auto& guardValue = ZC_ASSERT_NONNULL(guardComparison);
  auto guardComparisonOp = comparisonOperatorFor(guardValue.operation);
  if (guardComparisonOp == zc::none) return zc::none;
  if (guardValue.type != scrutinee.type) return zc::none;

  auto guardLeftParam = parameterReferenceFor(hirModule, guardValue.left);
  auto guardRightParam = parameterReferenceFor(hirModule, guardValue.right);
  auto guardLeftLiteral = expressionFor(hirModule, guardValue.left);
  auto guardRightLiteral = expressionFor(hirModule, guardValue.right);
  const bool guardLeftIsParam = guardLeftParam != zc::none;
  const bool guardRightIsParam = guardRightParam != zc::none;
  if (guardLeftIsParam == guardRightIsParam) return zc::none;
  const bool guardLeftIsLiteral = guardLeftLiteral != zc::none;
  const bool guardRightIsLiteral = guardRightLiteral != zc::none;
  if (guardLeftIsLiteral == guardRightIsLiteral) return zc::none;
  if (guardLeftIsParam != guardRightIsLiteral) return zc::none;

  const auto& guardParam =
      guardLeftIsParam ? ZC_ASSERT_NONNULL(guardLeftParam) : ZC_ASSERT_NONNULL(guardRightParam);
  const auto& guardLiteral = guardLeftIsLiteral ? ZC_ASSERT_NONNULL(guardLeftLiteral)
                                                : ZC_ASSERT_NONNULL(guardRightLiteral);
  auto guardParamIndex = parameterIndexFor(declaration, guardParam.parameter);
  if (guardParamIndex == zc::none) return zc::none;
  if (guardParam.type != guardValue.operandType) return zc::none;
  if (guardLiteral.type != guardValue.operandType) return zc::none;

  // The arms are scalar literals.
  auto thenLiteral = expressionFor(hirModule, conditionalValue.thenReturnValue);
  auto elseLiteral = expressionFor(hirModule, conditionalValue.elseReturnValue);
  if (thenLiteral == zc::none || elseLiteral == zc::none) return zc::none;
  if (ZC_ASSERT_NONNULL(thenLiteral).type != declaration.resultType ||
      ZC_ASSERT_NONNULL(elseLiteral).type != declaration.resultType) {
    return zc::none;
  }

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (size_t p = 0; p < declaration.parameters.size(); ++p) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[p].type,
                                         scope, declaration.parameters[p].sourceSpan.clone()));
  }
  const MirLocalId resultLocal =
      ctx.declareLocal(MirLocalKind::FunctionResult, declaration.resultType, scope,
                       ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone());
  if (resultLocal.ordinal() != static_cast<uint32_t>(declaration.parameters.size() + 1)) {
    return zc::none;
  }
  const MirLocalId guardTemp = ctx.declareLocal(MirLocalKind::Temporary, guardValue.type, scope,
                                                guardValue.sourceSpan.clone());
  if (guardTemp.ordinal() != static_cast<uint32_t>(declaration.parameters.size() + 2)) {
    return zc::none;
  }
  const MirLocalId conjTemp = ctx.declareLocal(MirLocalKind::Temporary, conjunctionValue.type,
                                               scope, conjunctionValue.sourceSpan.clone());
  if (conjTemp.ordinal() != static_cast<uint32_t>(declaration.parameters.size() + 3)) {
    return zc::none;
  }

  // Block 1: StorageLive(result); StorageLive(guardTemp);
  // Assign(guardTemp = guardCmp(param, lit)); StorageLive(conjTemp);
  // Assign(conjTemp = BitAnd(scrutinee, guardTemp));
  // SwitchInt(conjTemp) { true -> 2, false -> 3 }, default 3.
  (void)ctx.beginBlock(scope);
  ctx.appendStatement(
      MirStatement::storageLive(resultLocal, ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));
  ctx.appendStatement(MirStatement::storageLive(guardTemp, guardValue.sourceSpan.clone()));

  // Guard comparison operands: parameter place-use and scalar constant.
  zc::Vector<MirProjection> guardParamProjections;
  auto guardParamOperand =
      placeUse(proofs, copyMarker,
               MirPlace(parameterLocals[ZC_ASSERT_NONNULL(guardParamIndex)], guardParam.type,
                        zc::mv(guardParamProjections), guardParam.type));
  if (guardParamOperand == zc::none) return zc::none;
  auto guardLiteralOperand = MirOperand::constant(guardLiteral.type, guardLiteral.value.clone());
  MirOperand guardLeft =
      guardLeftIsParam ? zc::mv(ZC_ASSERT_NONNULL(guardParamOperand)) : zc::mv(guardLiteralOperand);
  MirOperand guardRight =
      guardLeftIsParam ? zc::mv(guardLiteralOperand) : zc::mv(ZC_ASSERT_NONNULL(guardParamOperand));
  zc::Vector<MirProjection> guardTempProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(guardTemp, guardValue.type, zc::mv(guardTempProjections), guardValue.type),
      MirRvalue::comparison(ZC_ASSERT_NONNULL(guardComparisonOp), zc::mv(guardLeft),
                            zc::mv(guardRight), guardValue.type),
      MirInitializationKind::Initialize, guardValue.sourceSpan.clone()));

  // Conjunction: BitAnd(scrutinee, guardTemp).
  ctx.appendStatement(MirStatement::storageLive(conjTemp, conjunctionValue.sourceSpan.clone()));
  zc::Vector<MirProjection> scrutineeProjections;
  auto scrutineeOperand =
      placeUse(proofs, copyMarker,
               MirPlace(parameterLocals[ZC_ASSERT_NONNULL(scrutineeIndex)], scrutinee.type,
                        zc::mv(scrutineeProjections), scrutinee.type));
  if (scrutineeOperand == zc::none) return zc::none;
  zc::Vector<MirProjection> guardTempUseProjections;
  auto guardTempUse = placeUse(
      proofs, copyMarker,
      MirPlace(guardTemp, guardValue.type, zc::mv(guardTempUseProjections), guardValue.type));
  if (guardTempUse == zc::none) return zc::none;
  zc::Vector<MirProjection> conjTempProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(conjTemp, conjunctionValue.type, zc::mv(conjTempProjections), conjunctionValue.type),
      MirRvalue::arithmetic(MirArithmeticOperator::BitAnd,
                            zc::mv(ZC_ASSERT_NONNULL(scrutineeOperand)),
                            zc::mv(ZC_ASSERT_NONNULL(guardTempUse)), conjunctionValue.type),
      MirInitializationKind::Initialize, conjunctionValue.sourceSpan.clone()));

  zc::Vector<MirProjection> discriminantProjections;
  auto discriminant = placeUse(proofs, copyMarker,
                               MirPlace(conjTemp, conjunctionValue.type,
                                        zc::mv(discriminantProjections), conjunctionValue.type));
  if (discriminant == zc::none) return zc::none;
  zc::Vector<MirSwitchIntArm> arms;
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  ctx.terminateBlock(MirTerminator::switchInt(zc::mv(ZC_ASSERT_NONNULL(discriminant)), zc::mv(arms),
                                              blockId(3), conditionalValue.sourceSpan.clone()));

  // Blocks 2 and 3: Initialize Assign of the arm constant then Goto block 4.
  const auto armBlock = [&](const hir::HirScalarLiteralExpression& literal) {
    (void)ctx.beginBlock(scope);
    zc::Vector<MirProjection> projections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(resultLocal, declaration.resultType, zc::mv(projections), declaration.resultType),
        MirRvalue::use(MirOperand::constant(literal.type, literal.value.clone())),
        MirInitializationKind::Initialize, literal.sourceSpan.clone()));
    ctx.terminateBlock(MirTerminator::gotoTarget(blockId(4), literal.sourceSpan.clone()));
  };
  armBlock(ZC_ASSERT_NONNULL(thenLiteral));
  armBlock(ZC_ASSERT_NONNULL(elseLiteral));

  // Block 4: Return(result).
  (void)ctx.beginBlock(scope);
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand = placeUse(proofs, copyMarker,
                                MirPlace(resultLocal, declaration.resultType,
                                         zc::mv(returnProjections), declaration.resultType));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

}  // namespace

zc::Maybe<RecursiveFunctionProduct> tryBuildRecursiveFunction(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    const type::SemanticTypeStore& semanticTypes, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  if (block.statements.empty()) return zc::none;
  // Void mutating-receiver method: a sole `this.<field> = <parameter>;`
  // statement with a Unit result. It deliberately has no trailing return, so it
  // must be claimed before the trailing-return resolution below bails.
  if (block.statements.size() == 1 && declaration.receiver != zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto write = parameterFieldWriteFor(hirModule, block.statements[0]);
    ZC_IF_SOME(sourceWrite, write) {
      auto writeParameter = parameterReferenceFor(hirModule, sourceWrite.value);
      ZC_IF_SOME(parameter, writeParameter) {
        auto product =
            buildReceiverFieldWriteVoidReturn(declaration, block, sourceWrite, parameter,
                                              semanticTypes, identities, proofs, copyMarker);
        if (product != zc::none) return product;
      }
    }
  }
  // Every owned shape ends in its trailing return; a missing return delegates.
  auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
  if (sourceReturn == zc::none) return zc::none;
  const hir::HirNodeId valueNode = ZC_ASSERT_NONNULL(sourceReturn).value;

  // K leading scalar user locals followed by one comparison conditional with
  // literal arms. The arm self-gates on the local + conditional/equality
  // structure and rejects every other K-local body, so it stays ahead of the
  // fixed-size single-local and sequential arms.
  if (block.statements.size() >= 1 && declaration.receiver == zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto product = buildLeadingLocalConditionalReturn(declaration, block, hirModule, identities,
                                                      proofs, copyMarker);
    if (product != zc::none) return product;
  }

  // Chained conditional return: a single-statement body whose return value is
  // a chain of nested equality conditionals with literal arms. The arm
  // self-gates on the HIR shape and rejects every other body.
  if (block.statements.size() == 1 && declaration.receiver == zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto product = buildChainedConditionalReturn(declaration, block, hirModule, identities, proofs,
                                                 copyMarker);
    if (product != zc::none) return product;
  }

  // Match-guard conjunctive conditional return: a single-statement body whose
  // return value is a conditional with a conjunction (LogicalAnd) condition.
  // The arm self-gates on the HIR shape and rejects every other body.
  if (block.statements.size() == 1 && declaration.receiver == zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto product = buildConjunctiveConditionalReturn(declaration, block, hirModule, identities,
                                                     proofs, copyMarker);
    if (product != zc::none) return product;
  }

  // The bare literal and parameter returns are the one-statement shapes.
  if (block.statements.size() == 1) {
    // Receiver method conditional return: one if/else whose branches return
    // scalar literals over a bare bool ordinary-parameter condition.
    auto methodConditional = conditionalFor(hirModule, valueNode);
    if (declaration.receiver != zc::none && declaration.unsafeBlock == zc::none &&
        methodConditional != zc::none) {
      const auto& conditional = ZC_ASSERT_NONNULL(methodConditional);
      auto conditionReference = parameterReferenceFor(hirModule, conditional.condition);
      if (conditionReference != zc::none &&
          expressionFor(hirModule, conditional.thenReturnValue) != zc::none &&
          expressionFor(hirModule, conditional.elseReturnValue) != zc::none) {
        auto product = buildMethodConditionalReturn(
            declaration, conditional, ZC_ASSERT_NONNULL(conditionReference),
            ZC_ASSERT_NONNULL(sourceReturn), hirModule, identities, proofs, copyMarker);
        if (product != zc::none) return product;
      }
    }

    // Scalar literal return: exactly one literal expression and no direct call on
    // the return value, no unsafe tail (that shape stays on the legacy rail).
    auto literal = expressionFor(hirModule, valueNode);
    auto directCall = callFor(hirModule, valueNode);
    if (literal != zc::none && directCall == zc::none && declaration.unsafeBlock == zc::none &&
        ZC_ASSERT_NONNULL(literal).type == declaration.resultType) {
      return buildScalarReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                               ZC_ASSERT_NONNULL(literal), identities);
    }

    // Receiver field read: a shared- or mutating-receiver method returning
    // this.field; the read is a copy through either kind of receiver reference.
    auto receiverFieldProjection = parameterFieldProjectionFor(hirModule, valueNode);
    if (declaration.receiver != zc::none && receiverFieldProjection != zc::none &&
        ZC_ASSERT_NONNULL(receiverFieldProjection).type == declaration.resultType &&
        ZC_ASSERT_NONNULL(receiverFieldProjection).category == hir::HirValueCategory::Place &&
        declaration.unsafeBlock == zc::none) {
      return buildReceiverFieldReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                      ZC_ASSERT_NONNULL(receiverFieldProjection), identities,
                                      proofs, copyMarker);
    }

    // By-value struct parameter field return: a free function returning
    // `parameter.field` where the parameter carries the nominal owner directly.
    if (declaration.receiver == zc::none && receiverFieldProjection != zc::none &&
        ZC_ASSERT_NONNULL(receiverFieldProjection).type == declaration.resultType &&
        ZC_ASSERT_NONNULL(receiverFieldProjection).category == hir::HirValueCategory::Place &&
        declaration.unsafeBlock == zc::none) {
      return buildByValueParameterFieldReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                              ZC_ASSERT_NONNULL(receiverFieldProjection),
                                              identities, proofs, copyMarker);
    }

    // Receiver self-call: a shared-receiver method forwarding `this` to a
    // zero-argument method (`return this.method();`). The receiver header
    // parameter is the direct call argument; no borrow temporary is created.
    if (declaration.receiver != zc::none && directCall == zc::none &&
        declaration.unsafeBlock == zc::none) {
      auto receiverCall = receiverCallFor(hirModule, valueNode);
      if (receiverCall != zc::none) {
        auto product = buildReceiverSelfCallReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                                   ZC_ASSERT_NONNULL(receiverCall), identities,
                                                   proofs, copyMarker);
        if (product != zc::none) return product;
      }
    }

    // Receiver parameter-binary return: a shared-receiver method whose return
    // value is a primitive arithmetic/comparison operation over ordinary
    // parameter references and scalar literals. A field-arithmetic return
    // instead combines a `this.<field>` projection with a scalar literal.
    if (declaration.receiver != zc::none && directCall == zc::none &&
        declaration.unsafeBlock == zc::none) {
      auto binary = primitiveBinaryFor(hirModule, valueNode);
      if (binary != zc::none) {
        auto fieldProjection =
            parameterFieldProjectionFor(hirModule, ZC_ASSERT_NONNULL(binary).left);
        hir::HirNodeId literalOperandNode = ZC_ASSERT_NONNULL(binary).right;
        bool fieldIsLeft = true;
        if (fieldProjection == zc::none) {
          fieldProjection = parameterFieldProjectionFor(hirModule, ZC_ASSERT_NONNULL(binary).right);
          literalOperandNode = ZC_ASSERT_NONNULL(binary).left;
          fieldIsLeft = false;
        }
        if (fieldProjection != zc::none) {
          auto literalExpression = expressionFor(hirModule, literalOperandNode);
          if (literalExpression != zc::none) {
            auto product = buildReceiverFieldBinaryReturn(
                declaration, ZC_ASSERT_NONNULL(sourceReturn), ZC_ASSERT_NONNULL(binary),
                ZC_ASSERT_NONNULL(fieldProjection), ZC_ASSERT_NONNULL(literalExpression).value,
                fieldIsLeft, hirModule, identities, proofs, copyMarker);
            if (product != zc::none) return product;
          }
        } else {
          auto product = buildMethodBinaryReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                                 ZC_ASSERT_NONNULL(binary), hirModule, identities,
                                                 proofs, copyMarker);
          if (product != zc::none) return product;
        }
      }
    }

    // Free-function parameter-binary return: a free function whose return
    // value is a primitive arithmetic/comparison operation over ordinary
    // parameter references and scalar literals.
    if (declaration.receiver == zc::none && directCall == zc::none &&
        declaration.unsafeBlock == zc::none) {
      auto binary = primitiveBinaryFor(hirModule, valueNode);
      if (binary != zc::none) {
        auto product = buildFreeFunctionBinaryReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                                     ZC_ASSERT_NONNULL(binary), hirModule,
                                                     identities, proofs, copyMarker);
        if (product != zc::none) return product;
      }
    }

    // Parameter return: a bare parameter reference (never a parameter reborrow,
    // which the legacy rail lowers with a borrow-creation temporary).
    auto parameterReference = parameterReferenceFor(hirModule, valueNode);
    auto parameterReborrow = parameterReborrowFor(hirModule, valueNode);
    if (parameterReference != zc::none && parameterReborrow == zc::none &&
        ZC_ASSERT_NONNULL(parameterReference).type == declaration.resultType &&
        ZC_ASSERT_NONNULL(parameterReference).category == hir::HirValueCategory::Place) {
      return buildParameterReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                  ZC_ASSERT_NONNULL(parameterReference), identities, proofs,
                                  copyMarker);
    }
  }

  // Receiver field write-read: a mutating-receiver method whose two-statement
  // body overwrites this.field with a literal and returns the field.
  if (block.statements.size() == 2 && declaration.receiver != zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto write = parameterFieldWriteFor(hirModule, block.statements[0]);
    auto projection = parameterFieldProjectionFor(hirModule, valueNode);
    if (write != zc::none && projection != zc::none) {
      auto writeLiteral = expressionFor(hirModule, ZC_ASSERT_NONNULL(write).value);
      if (writeLiteral != zc::none) {
        auto product = buildReceiverFieldWriteReturn(
            declaration, ZC_ASSERT_NONNULL(write), ZC_ASSERT_NONNULL(writeLiteral),
            ZC_ASSERT_NONNULL(projection), ZC_ASSERT_NONNULL(sourceReturn), identities, proofs,
            copyMarker);
        if (product != zc::none) return product;
      }
    }
  }

  // Discarded receiver call followed by a shared trailing receiver call:
  // `fn f() -> T { let o = S{..constants..}; o.set(c); return o.get(); }`. The
  // statement-position call targets an unread temporary; the trailing getter
  // returns the owner field. Two shapes are admitted: a mutable
  // one-literal-argument Unit call, or a shared zero-argument value-returning
  // devirtualized-erase call. This arm must precede the size-3 overwrite and
  // sequential-local arms below.
  if (block.statements.size() == 3 && declaration.receiver == zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto bindingRecord = localFor(hirModule, block.statements[0]);
    auto discardedRecord = receiverCallFor(hirModule, block.statements[1]);
    if (bindingRecord != zc::none && discardedRecord != zc::none) {
      const auto& binding = ZC_ASSERT_NONNULL(bindingRecord);
      const auto& discardedCall = ZC_ASSERT_NONNULL(discardedRecord);
      hir::HirNodeId initializerNode;
      ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
      auto aggregateRecord = aggregateFor(hirModule, initializerNode);
      auto trailingCallRecord = receiverCallFor(hirModule, valueNode);
      auto setReceiverRecord = localReferenceFor(hirModule, discardedCall.receiver);
      if (aggregateRecord != zc::none && trailingCallRecord != zc::none &&
          setReceiverRecord != zc::none) {
        const auto& aggregate = ZC_ASSERT_NONNULL(aggregateRecord);
        const auto& trailingCall = ZC_ASSERT_NONNULL(trailingCallRecord);
        const auto& setReceiver = ZC_ASSERT_NONNULL(setReceiverRecord);
        auto getReceiverRecord = localReferenceFor(hirModule, trailingCall.receiver);
        if (getReceiverRecord != zc::none) {
          const auto& getReceiver = ZC_ASSERT_NONNULL(getReceiverRecord);
          const bool discardedMutableUnit =
              discardedCall.receiverMode == checker::checked::ReceiverMode::Mutable &&
              discardedCall.receiverAdjustments.size() == 1 &&
              discardedCall.receiverAdjustments[0] ==
                  checker::checked::ReceiverAdjustmentStep::BorrowMutable &&
              isUnitSemanticType(semanticTypes, discardedCall.resultType) &&
              discardedCall.arguments.size() == 1 && discardedCall.arguments[0].value != zc::none;
          const bool discardedSharedValue =
              discardedCall.receiverMode == checker::checked::ReceiverMode::Shared &&
              discardedCall.receiverAdjustments.size() == 1 &&
              discardedCall.receiverAdjustments[0] ==
                  checker::checked::ReceiverAdjustmentStep::BorrowShared &&
              discardedCall.arguments.size() == 0;
          if (binding.local.ordinal() == 1 && binding.initializer == aggregate.node &&
              binding.type == aggregate.type && binding.type == discardedCall.receiverSourceType &&
              binding.type == trailingCall.receiverSourceType &&
              aggregate.category == hir::HirValueCategory::Value &&
              discardedCall.receiver == setReceiver.node &&
              trailingCall.receiver == getReceiver.node && setReceiver.local == binding.local &&
              getReceiver.local == binding.local &&
              setReceiver.category == hir::HirValueCategory::Place &&
              getReceiver.category == hir::HirValueCategory::Place &&
              (discardedMutableUnit || discardedSharedValue) &&
              trailingCall.receiverMode == checker::checked::ReceiverMode::Shared &&
              trailingCall.receiverAdjustments.size() == 1 &&
              trailingCall.receiverAdjustments[0] ==
                  checker::checked::ReceiverAdjustmentStep::BorrowShared &&
              trailingCall.arguments.size() == 0 &&
              trailingCall.resultType == declaration.resultType) {
            auto product = buildVoidCallThenReceiverCallReturn(
                declaration, binding, aggregate, ZC_ASSERT_NONNULL(sourceReturn), setReceiver,
                discardedCall, getReceiver, trailingCall, identities, proofs, copyMarker,
                discardedSharedValue);
            if (product != zc::none) return product;
          }
        }
      }
    }
  }

  // By-value aggregate caller:
  // `fn entry() -> T { let p: P = P { ..constants.. }; return f(p); }`. The
  // trailing return value is a direct call whose sole argument copies the
  // aggregate-initialized owner local. It must precede the scalar-local arm,
  // whose gate only admits a literal initializer and a place return.
  if (block.statements.size() == 2 && declaration.receiver == zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto product = buildByValueAggregateCallReturn(declaration, block, hirModule, identities,
                                                   proofs, copyMarker);
    if (product != zc::none) return product;
  }

  // Scalar-local direct-call caller:
  // `fn entry() -> i32 { let a: i32 = <literal>; return f(a); }`. The trailing
  // return value is a direct call whose sole argument copies the
  // literal-initialized scalar owner local. It must precede the scalar-local
  // arm, whose gate admits only a bare place return of the local.
  if (block.statements.size() == 2 && declaration.receiver == zc::none &&
      declaration.unsafeBlock == zc::none) {
    auto product =
        buildScalarLocalCallReturn(declaration, block, hirModule, identities, proofs, copyMarker);
    if (product != zc::none) return product;
  }

  // Single scalar-initialized user local: `let x = <literal>; return x;`. The
  // strict gate mirrors validLocalReturnFunction and the dedicated two-statement
  // construction: one binding whose initializer is a scalar literal and a bare
  // place reference of that same local. The plain-function layout declares no
  // parameter locals regardless of signature; a receiver method routes through
  // buildMethodScalarLocalReturn, which keeps the receiver parameter local.
  // Aggregate, binary, parameter, call, borrow, and uninitialized initializers
  // keep their own legacy shapes.
  if (block.statements.size() == 2 && declaration.unsafeBlock == zc::none) {
    auto sourceLocal = localFor(hirModule, block.statements[0]);
    if (sourceLocal != zc::none) {
      const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
      hir::HirNodeId initializerNode;
      ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
      auto literal = expressionFor(hirModule, initializerNode);
      auto reference = localReferenceFor(hirModule, valueNode);
      auto fieldProjection = localFieldProjectionFor(hirModule, valueNode);
      auto aggregate = aggregateFor(hirModule, initializerNode);
      auto binary = primitiveBinaryFor(hirModule, initializerNode);
      if (literal != zc::none && aggregate == zc::none && binary == zc::none &&
          reference != zc::none && fieldProjection == zc::none &&
          binding.initializer == ZC_ASSERT_NONNULL(literal).node &&
          binding.local == ZC_ASSERT_NONNULL(reference).local &&
          binding.type == declaration.resultType &&
          ZC_ASSERT_NONNULL(literal).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).category == hir::HirValueCategory::Place) {
        if (declaration.receiver == zc::none) {
          return buildScalarLocalReturn(declaration, binding, ZC_ASSERT_NONNULL(literal),
                                        ZC_ASSERT_NONNULL(sourceReturn), identities, proofs,
                                        copyMarker);
        }
        return buildMethodScalarLocalReturn(declaration, binding, ZC_ASSERT_NONNULL(literal),
                                            ZC_ASSERT_NONNULL(sourceReturn), identities, proofs,
                                            copyMarker);
      }
    }
  }

  // Single aggregate-initialized user local:
  // `let p = T{..constants..}; return p;` or `return p.f;`. One UserLocal at
  // localId(1), no parameter locals, a nominal aggregate rvalue of constant
  // elements, and a whole-local or one-field return. Aggregate composites and
  // field writes keep the legacy rail.
  if (block.statements.size() == 2 && declaration.unsafeBlock == zc::none) {
    auto aggregateProduct =
        buildAggregateLocalReturn(declaration, block, hirModule, identities, proofs, copyMarker);
    if (aggregateProduct != zc::none) return aggregateProduct;
  }

  // Single scalar local with one repeated literal write:
  // `mut x = <lit>; x = <lit>; return x;`. Mirrors
  // validLocalOverwriteReturnFunction: the middle statement is an Overwrite
  // local write to the binding with no field projection, and both values are
  // scalar literals. Parameter or binary write values keep their own legacy
  // shapes.
  if (block.statements.size() == 3 && declaration.unsafeBlock == zc::none) {
    auto sourceLocal = localFor(hirModule, block.statements[0]);
    auto sourceOverwrite = localWriteFor(hirModule, block.statements[1]);
    if (sourceLocal != zc::none && sourceOverwrite != zc::none) {
      const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
      const auto& write = ZC_ASSERT_NONNULL(sourceOverwrite);
      hir::HirNodeId initializerNode;
      ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
      auto literal = expressionFor(hirModule, initializerNode);
      auto overwriteLiteral = expressionFor(hirModule, write.value);
      auto reference = localReferenceFor(hirModule, valueNode);
      auto fieldProjection = localFieldProjectionFor(hirModule, valueNode);
      auto overwriteBinary = primitiveBinaryFor(hirModule, write.value);
      auto overwriteParameter = parameterReferenceFor(hirModule, write.value);
      if (literal != zc::none && overwriteLiteral != zc::none && reference != zc::none &&
          fieldProjection == zc::none && overwriteBinary == zc::none &&
          overwriteParameter == zc::none &&
          binding.initializer == ZC_ASSERT_NONNULL(literal).node &&
          write.kind == hir::HirLocalWriteKind::Overwrite && write.field == zc::none &&
          write.local == binding.local && write.local == ZC_ASSERT_NONNULL(reference).local &&
          write.type == binding.type && binding.type == declaration.resultType &&
          ZC_ASSERT_NONNULL(literal).type == binding.type &&
          ZC_ASSERT_NONNULL(overwriteLiteral).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).category == hir::HirValueCategory::Place) {
        return buildScalarLocalOverwriteReturn(declaration, binding, ZC_ASSERT_NONNULL(literal),
                                               write, ZC_ASSERT_NONNULL(overwriteLiteral),
                                               ZC_ASSERT_NONNULL(sourceReturn), identities, proofs,
                                               copyMarker);
      }
    }
  }

  // Single user-local body with one or more overwrite writes:
  // `mut x = <lit/param>; x = <lit/param/binary>; ...; return x;`. A binary
  // write value lowers to an arithmetic rvalue whose left operand is a
  // place-use of the user local (compound assignment desugar).
  if (block.statements.size() >= 3 && declaration.unsafeBlock == zc::none) {
    auto product =
        buildLocalWriteReturn(declaration, block, hirModule, identities, proofs, copyMarker);
    if (product != zc::none) return product;
  }

  // Sequential N-local body with N>=1 plain initializers:
  // `let a = <lit/param/local/ternary>; ... return <local-or-param>;`. The arm
  // performs its own strict gate; aggregate, nested-operand, unsafe-tail,
  // field-projection, and forward-reference shapes return none and keep the
  // legacy sequential rail.
  if (block.statements.size() >= 2 && declaration.unsafeBlock == zc::none) {
    auto sequential =
        buildSequentialLocalReturn(declaration, block, hirModule, identities, proofs, copyMarker);
    if (sequential != zc::none) return sequential;
  }

  return zc::none;
}

}  // namespace zomlang::compiler::mir
