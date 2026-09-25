// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-stmt-control.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerConditionalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingConditionalReturn conditional = zc::mv(ZC_ASSERT_NONNULL(function.conditionalReturn));

  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();

  ZC_IF_SOME(equality, conditional.condition.equality) {
    // Comparison-condition stride (9 nodes): function, body, left operand,
    // right operand, equality comparison, then value, else value, conditional,
    // return. The conditional takes the equality node as its condition.
    const HirNodeId leftId = ctx.allocNode();
    const HirNodeId rightId = ctx.allocNode();
    const HirNodeId equalityId = ctx.allocNode();
    const HirNodeId thenValueId = ctx.allocNode();
    const HirNodeId elseValueId = ctx.allocNode();
    const HirNodeId conditionalId = ctx.allocNode();
    const HirNodeId returnId = ctx.allocNode();

    ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                           zc::mv(function.parameters), zc::mv(function.receiver),
                                           function.visibility.clone(), function.linkage,
                                           function.declarationSpan.clone(), bodyId, zc::none});
    zc::Vector<HirNodeId> statements;
    statements.add(returnId);
    ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
    ctx.addReturn(HirReturnStatement{returnId, function.resultType, conditionalId,
                                     function.returnSpan.clone()});
    ctx.lowerArmLeaf(leftId, equality.left);
    ctx.lowerArmLeaf(rightId, equality.right);
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        equalityId, leftId, rightId, equality.operandType, equality.type, HirValueCategory::Value,
        equality.operation, equality.sourceSpan.clone()});
    ctx.lowerArmLeaf(thenValueId, conditional.thenArm);
    ctx.lowerArmLeaf(elseValueId, conditional.elseArm);
    ctx.addConditional(HirConditionalExpression{conditionalId, equalityId, thenValueId, elseValueId,
                                                function.resultType, HirValueCategory::Value,
                                                conditional.conditionalSpan.clone()});
    return;
  }

  // Parameter-condition stride (7 nodes): function, body, condition parameter
  // reference, then value, else value, conditional, return.
  const HirNodeId conditionId = ctx.allocNode();
  const HirNodeId thenValueId = ctx.allocNode();
  const HirNodeId elseValueId = ctx.allocNode();
  const HirNodeId conditionalId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, conditionalId,
                                   function.returnSpan.clone()});
  ZC_IF_SOME(parameter, conditional.condition.parameter) {
    ctx.addParameterReference(
        HirParameterReferenceExpression{conditionId, parameter.parameter.clone(), parameter.type,
                                        parameter.category, parameter.sourceSpan.clone()});
  }
  ctx.lowerArmLeaf(thenValueId, conditional.thenArm);
  ctx.lowerArmLeaf(elseValueId, conditional.elseArm);
  ctx.addConditional(HirConditionalExpression{conditionalId, conditionId, thenValueId, elseValueId,
                                              function.resultType, HirValueCategory::Value,
                                              conditional.conditionalSpan.clone()});
}

void lowerLoopReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingLoopReturn loop = zc::mv(ZC_ASSERT_NONNULL(function.loopReturn));

  // Empty-body loop stride (6 nodes): function, body, condition parameter
  // reference, return literal, loop statement, return. The body block lists the
  // loop statement followed by the return.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId conditionId = ctx.allocNode();
  const HirNodeId returnValueId = ctx.allocNode();
  const HirNodeId loopId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(loopId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, returnValueId,
                                   function.returnSpan.clone()});
  ctx.addParameterReference(HirParameterReferenceExpression{
      conditionId, loop.condition.parameter.clone(), loop.condition.type, loop.condition.category,
      loop.condition.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{returnValueId, loop.returnType,
                                               zc::mv(loop.returnLiteral), HirValueCategory::Value,
                                               loop.returnValueSpan.clone()});
  ctx.addLoop(HirLoopStatement{loopId, conditionId, zc::Vector<HirNodeId>{}, loop.condition.type,
                               HirValueCategory::Place, loop.loopSpan.clone()});
}

void lowerLoopBodyReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingLoopBodyReturn loopBody = zc::mv(ZC_ASSERT_NONNULL(function.loopBodyReturn));
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::SemanticTypeId placeType = ZC_ASSERT_NONNULL(function.localReference).type;
  const HirValueCategory placeCategory = ZC_ASSERT_NONNULL(function.localReference).category;
  const identity::SourceSpan placeSpan =
      ZC_ASSERT_NONNULL(function.localReference).sourceSpan.clone();
  const size_t writeCount = function.localWrites.size();

  // Fixed-id layout matching the generic materializer: function, body, local,
  // scalar-literal initializer, then per write (write node, write value node),
  // return, returned local reference, then per binary write two trailing
  // operand ids, and finally the loop condition parameter reference and the
  // loop statement. The body block lists [local, loop, return]; the loop
  // statement carries the write node ids as its body.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  zc::Vector<HirNodeId> writeIds;
  zc::Vector<HirNodeId> writeValueIds;
  for (size_t index = 0; index < writeCount; ++index) {
    writeIds.add(ctx.allocNode());
    writeValueIds.add(ctx.allocNode());
  }
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();
  zc::Vector<zc::Maybe<HirNodeId>> binaryLeftIds;
  zc::Vector<zc::Maybe<HirNodeId>> binaryRightIds;
  for (const auto& writeValue : function.localWriteValues) {
    zc::Maybe<HirNodeId> leftId;
    zc::Maybe<HirNodeId> rightId;
    if (writeValue.binary != zc::none) {
      leftId = ctx.allocNode();
      rightId = ctx.allocNode();
    }
    binaryLeftIds.add(zc::mv(leftId));
    binaryRightIds.add(zc::mv(rightId));
  }
  const HirNodeId loopConditionId = ctx.allocNode();
  const HirNodeId loopId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(loopId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addParameterReference(HirParameterReferenceExpression{
      loopConditionId, loopBody.condition.parameter.clone(), loopBody.condition.type,
      loopBody.condition.category, loopBody.condition.sourceSpan.clone()});
  zc::Vector<HirNodeId> loopStatements;
  for (const auto writeId : writeIds) { loopStatements.add(writeId); }
  ctx.addLoop(HirLoopStatement{loopId, loopConditionId, zc::mv(loopStatements),
                               loopBody.condition.type, HirValueCategory::Place,
                               zc::mv(loopBody.loopSpan)});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});

  // Initializer leaf (literal or parameter) at the initializer node, matching
  // the flat mut-local write arm.
  if (function.literal != zc::none) {
    ZC_IF_SOME(literal, function.literal) {
      ctx.addExpression(HirScalarLiteralExpression{initializerId, localType, literal.clone(),
                                                   HirValueCategory::Value,
                                                   initializerSpan.clone()});
    }
  } else {
    ZC_IF_SOME(reference, function.parameterReference) {
      ctx.addParameterReference(HirParameterReferenceExpression{
          initializerId, reference.parameter.clone(), reference.type, reference.category,
          reference.sourceSpan.clone()});
    }
  }

  for (size_t index = 0; index < writeCount; ++index) {
    const HirLocalWriteStatement& write = function.localWrites[index];
    const PendingLocalWriteValue& writeValue = function.localWriteValues[index];
    ZC_IF_SOME(literal, writeValue.literal) {
      ctx.addExpression(HirScalarLiteralExpression{writeValueIds[index], write.type,
                                                   literal.clone(), HirValueCategory::Value,
                                                   write.valueSpan.clone()});
    }
    ZC_IF_SOME(parameter, writeValue.parameter) {
      ctx.addParameterReference(HirParameterReferenceExpression{
          writeValueIds[index], parameter.parameter.clone(), write.type, parameter.category,
          parameter.sourceSpan.clone()});
    }
    ZC_IF_SOME(binary, writeValue.binary) {
      HirNodeId leftId;
      HirNodeId rightId;
      ZC_IF_SOME(id, binaryLeftIds[index]) { leftId = id; }
      ZC_IF_SOME(id, binaryRightIds[index]) { rightId = id; }
      ctx.lowerArmLeaf(leftId, binary.left);
      ctx.lowerArmLeaf(rightId, binary.right);
      ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
          writeValueIds[index], leftId, rightId, binary.operandType, binary.type,
          HirValueCategory::Value, binary.operation, binary.sourceSpan.clone()});
    }
    ctx.addLocalWrite(HirLocalWriteStatement{writeIds[index], hirLocalId(1), write.field,
                                             write.type, writeValueIds[index], write.kind,
                                             write.sourceSpan.clone(), write.valueSpan.clone()});
  }

  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  ctx.addLocalReference(HirLocalReferenceExpression{valueId, hirLocalId(1), placeType,
                                                    placeCategory, placeSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
