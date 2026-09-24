// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/mir-builder.h"

#include <cstddef>

#include "compiler/checker/body/marker-proof.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/mir/build/mir-fn-builder.h"

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
  if (block.statements.size() < 3) return zc::none;
  const size_t bindingCount = block.statements.size() - 1;

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  auto sourceReturn = returnFor(hirModule, block.statements[bindingCount]);
  if (sourceReturn == zc::none) return zc::none;

  // Resolve every leading binding; layer-local ordinals must be dense 1..N and
  // every binding carries an initializer.
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

  const hir::HirNodeId returnNode = ZC_ASSERT_NONNULL(sourceReturn).value;
  auto returnLocalReference = localReferenceFor(hirModule, returnNode);
  auto returnParameterReference = parameterReferenceFor(hirModule, returnNode);
  auto returnFieldProjection = localFieldProjectionFor(hirModule, returnNode);
  if (returnFieldProjection != zc::none) return zc::none;
  if (returnLocalReference == zc::none && returnParameterReference == zc::none) return zc::none;

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

/// \brief Lowers `fun f(...) -> T { return <literal>; }`: one root scope, no
/// locals, one empty entry block returning the scalar constant. Byte-identical
/// to the legacy fallthrough scalar construction. An inherent method
/// (`fun m(this) -> T { return <literal>; }`) additionally declares its
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

/// \brief Lowers `fun m(this) -> T { return this.field; }` on a shared
/// receiver: one root scope, one receiver parameter local carrying the shared
/// reference, and a Return of a copy/move place-use rooted at that local
/// through [Dereference(&Owner -> Owner), Field(Owner -> T)].
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

/// \brief Lowers `fun f(p0..pN-1) -> R { return pK; }`, or the method twin
/// `fun m(this, p0..pN-1) -> R { return pK; }`: one root scope, parameter
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

/// \brief Lowers `fun f(...) -> T { let x = <literal>; return x; }`: one root
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

/// \brief Lowers `fun f() -> T { mut x = <lit>; x = <lit>; return x; }`: one
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

/// \brief Lowers `fun m(this) -> T { this.field = <constant>; return this.field; }`
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

}  // namespace

zc::Maybe<RecursiveFunctionProduct> tryBuildRecursiveFunction(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (block.statements.empty()) return zc::none;
  // Every owned shape ends in its trailing return; a missing return delegates.
  auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
  if (sourceReturn == zc::none) return zc::none;
  const hir::HirNodeId valueNode = ZC_ASSERT_NONNULL(sourceReturn).value;

  // The bare literal and parameter returns are the one-statement shapes.
  if (block.statements.size() == 1) {
    // Scalar literal return: exactly one literal expression and no direct call on
    // the return value, no unsafe tail (that shape stays on the legacy rail).
    auto literal = expressionFor(hirModule, valueNode);
    auto directCall = callFor(hirModule, valueNode);
    if (literal != zc::none && directCall == zc::none && declaration.unsafeBlock == zc::none &&
        ZC_ASSERT_NONNULL(literal).type == declaration.resultType) {
      return buildScalarReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                               ZC_ASSERT_NONNULL(literal), identities);
    }

    // Receiver field read: a shared-receiver method returning this.field.
    auto receiverFieldProjection = parameterFieldProjectionFor(hirModule, valueNode);
    if (declaration.receiver != zc::none && receiverFieldProjection != zc::none &&
        ZC_ASSERT_NONNULL(receiverFieldProjection).type == declaration.resultType &&
        ZC_ASSERT_NONNULL(receiverFieldProjection).category == hir::HirValueCategory::Place &&
        declaration.unsafeBlock == zc::none) {
      return buildReceiverFieldReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                      ZC_ASSERT_NONNULL(receiverFieldProjection), identities,
                                      proofs, copyMarker);
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

  // Single scalar-initialized user local: `let x = <literal>; return x;`. The
  // strict gate mirrors validLocalReturnFunction and the dedicated two-statement
  // construction: one binding whose initializer is a scalar literal and a bare
  // place reference of that same local. No parameter locals are declared on this
  // legacy layout, regardless of the function signature. Aggregate, binary,
  // parameter, call, borrow, and uninitialized initializers keep their own
  // legacy shapes.
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
        return buildScalarLocalReturn(declaration, binding, ZC_ASSERT_NONNULL(literal),
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

  // Sequential N-local body with N>=2 plain initializers:
  // `let a = <lit/param/local>; ... return <local-or-param>;`. The arm performs
  // its own strict gate; aggregate, binary, nested-operand, unsafe-tail,
  // field-projection, and forward-reference shapes return none and keep the
  // legacy sequential rail.
  if (block.statements.size() >= 3 && declaration.unsafeBlock == zc::none) {
    auto sequential =
        buildSequentialLocalReturn(declaration, block, hirModule, identities, proofs, copyMarker);
    if (sequential != zc::none) return sequential;
  }

  return zc::none;
}

}  // namespace zomlang::compiler::mir
