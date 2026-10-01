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
        equality.operation, equality.sourceSpan.clone(), equality.isUnaryDesugar});
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

void lowerLeadingLocalConditionalReturnFunction(PendingFunctionDeclaration&& function,
                                                HirFnCtx& ctx) {
  PendingLeadingLocalConditionalReturn leading =
      zc::mv(ZC_ASSERT_NONNULL(function.leadingLocalConditionalReturn));
  const size_t bindingCount = leading.bindings.size();

  // Source-preorder layout: function, body, per binding (local, initializer),
  // then left operand, right operand, comparison, then arm, else arm,
  // conditional, return.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  zc::Vector<HirNodeId> localIds;
  zc::Vector<HirNodeId> initializerIds;
  zc::Vector<HirNodeId> arithLeftIds;
  zc::Vector<HirNodeId> arithRightIds;
  for (size_t index = 0; index < bindingCount; ++index) {
    localIds.add(ctx.allocNode());
    initializerIds.add(ctx.allocNode());
    if (leading.bindings[index].kind == SequentialInitializerKind::PrimitiveBinary) {
      arithLeftIds.add(ctx.allocNode());
      arithRightIds.add(ctx.allocNode());
    }
  }
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
  for (const auto localId : localIds) { statements.add(localId); }
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, conditionalId,
                                   function.returnSpan.clone()});

  for (size_t index = 0; index < bindingCount; ++index) {
    const auto& binding = leading.bindings[index];
    size_t arithIndex = 0;
    switch (binding.kind) {
      case SequentialInitializerKind::Literal:
        ZC_IF_SOME(literal, binding.literal) {
          ctx.addExpression(HirScalarLiteralExpression{initializerIds[index], binding.type,
                                                       literal.clone(), HirValueCategory::Value,
                                                       binding.initializerSpan.clone()});
        }
        break;
      case SequentialInitializerKind::ParameterReference:
        ZC_IF_SOME(parameter, binding.parameter) {
          ctx.addParameterReference(HirParameterReferenceExpression{
              initializerIds[index], parameter.clone(), binding.type, HirValueCategory::Place,
              binding.initializerSpan.clone()});
        }
        break;
      case SequentialInitializerKind::LocalReference:
        ctx.addLocalReference(HirLocalReferenceExpression{
            initializerIds[index], hirLocalId(static_cast<uint32_t>(binding.referencedLocal + 1)),
            binding.type, HirValueCategory::Place, binding.initializerSpan.clone()});
        break;
      case SequentialInitializerKind::PrimitiveBinary: {
        // Count how many arithmetic bindings preceded this one to index the
        // pre-allocated leaf operand nodes.
        for (size_t earlier = 0; earlier < index; ++earlier) {
          if (leading.bindings[earlier].kind == SequentialInitializerKind::PrimitiveBinary) {
            ++arithIndex;
          }
        }
        const auto& leftLeaf = ZC_ASSERT_NONNULL(binding.arithmeticLeft);
        const auto& rightLeaf = ZC_ASSERT_NONNULL(binding.arithmeticRight);
        ZC_IF_SOME(literal, leftLeaf.literal) {
          ctx.addExpression(HirScalarLiteralExpression{arithLeftIds[arithIndex], binding.type,
                                                       literal.clone(), HirValueCategory::Value,
                                                       binding.initializerSpan.clone()});
        } else ZC_IF_SOME(parameter, leftLeaf.parameter) {
          ctx.addParameterReference(HirParameterReferenceExpression{
              arithLeftIds[arithIndex], parameter.clone(), binding.type, HirValueCategory::Place,
              binding.initializerSpan.clone()});
        } else if (leftLeaf.isLocal) {
          ctx.addLocalReference(HirLocalReferenceExpression{
              arithLeftIds[arithIndex],
              hirLocalId(static_cast<uint32_t>(leftLeaf.referencedLocal + 1)), binding.type,
              HirValueCategory::Place, binding.initializerSpan.clone()});
        }
        ZC_IF_SOME(literal, rightLeaf.literal) {
          ctx.addExpression(HirScalarLiteralExpression{arithRightIds[arithIndex], binding.type,
                                                       literal.clone(), HirValueCategory::Value,
                                                       binding.initializerSpan.clone()});
        } else ZC_IF_SOME(parameter, rightLeaf.parameter) {
          ctx.addParameterReference(HirParameterReferenceExpression{
              arithRightIds[arithIndex], parameter.clone(), binding.type, HirValueCategory::Place,
              binding.initializerSpan.clone()});
        } else if (rightLeaf.isLocal) {
          ctx.addLocalReference(HirLocalReferenceExpression{
              arithRightIds[arithIndex],
              hirLocalId(static_cast<uint32_t>(rightLeaf.referencedLocal + 1)), binding.type,
              HirValueCategory::Place, binding.initializerSpan.clone()});
        }
        ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
            initializerIds[index], arithLeftIds[arithIndex], arithRightIds[arithIndex],
            binding.type, binding.type, HirValueCategory::Value,
            ZC_ASSERT_NONNULL(binding.arithmeticOperation), binding.initializerSpan.clone()});
        break;
      }
      case SequentialInitializerKind::Aggregate:
      case SequentialInitializerKind::PrimitiveUnary:
      case SequentialInitializerKind::Cast:
      case SequentialInitializerKind::Ternary:
        break;
    }
    ctx.addLocal(HirLocalBinding{localIds[index], hirLocalId(static_cast<uint32_t>(index + 1)),
                                 binding.type, initializerIds[index], binding.patternSpan.clone(),
                                 binding.initializerSpan.clone()});
  }

  auto lowerConditionOperand = [&](HirNodeId destination,
                                   const PendingLeadingConditionOperand& operand) {
    ZC_IF_SOME(literal, operand.literal) {
      ctx.addExpression(HirScalarLiteralExpression{destination, operand.type, literal.clone(),
                                                   HirValueCategory::Value,
                                                   operand.sourceSpan.clone()});
      return;
    }
    if (operand.isLocal) {
      ctx.addLocalReference(HirLocalReferenceExpression{
          destination, hirLocalId(static_cast<uint32_t>(operand.referencedLocal + 1)), operand.type,
          HirValueCategory::Place, operand.sourceSpan.clone()});
      return;
    }
    ZC_IF_SOME(parameter, operand.parameter) {
      ctx.addParameterReference(
          HirParameterReferenceExpression{destination, parameter.clone(), operand.type,
                                          HirValueCategory::Place, operand.sourceSpan.clone()});
    }
  };
  lowerConditionOperand(leftId, leading.left);
  lowerConditionOperand(rightId, leading.right);
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      equalityId, leftId, rightId, leading.operandType, leading.conditionType,
      HirValueCategory::Value, leading.operation, leading.conditionSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{thenValueId, leading.resultType,
                                               leading.thenLiteral.clone(), HirValueCategory::Value,
                                               leading.thenSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{elseValueId, leading.resultType,
                                               leading.elseLiteral.clone(), HirValueCategory::Value,
                                               leading.elseSpan.clone()});
  ctx.addConditional(HirConditionalExpression{conditionalId, equalityId, thenValueId, elseValueId,
                                              leading.resultType, HirValueCategory::Value,
                                              leading.returnSpan.clone()});
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
                               HirValueCategory::Place, loop.loopSpan.clone(), zc::none, zc::none});
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

  // Fixed-id layout: function, body, local, initializer, per-write
  // (write node, write value node), return, value, condition, loop, then
  // per-binary left+right. The body block lists [local, loop, return]; the
  // loop statement carries the write node ids as its body.
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
  const HirNodeId loopConditionId = ctx.allocNode();
  const HirNodeId loopId = ctx.allocNode();
  // Binary operand nodes trail after the loop node, matching the verifier's
  // fixed-id layout (function, body, local, initializer, per-write write+value,
  // return, value, condition, loop, then per-binary left+right).
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
                               zc::mv(loopBody.loopSpan), zc::mv(loopBody.breakSpan),
                               zc::mv(loopBody.continueSpan)});
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

void lowerForLoopReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingForLoopReturn forLoop = zc::mv(ZC_ASSERT_NONNULL(function.forLoopReturn));

  // Fixed-id layout: function, body, local, initializer, comparison left,
  // comparison right, comparison condition, arithmetic left, arithmetic right,
  // arithmetic write value, write, loop, return, return value. The body block
  // lists [local, loop, return]; the loop statement carries the write node id
  // as its body.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId comparisonLeftId = ctx.allocNode();
  const HirNodeId comparisonRightId = ctx.allocNode();
  const HirNodeId conditionId = ctx.allocNode();
  const HirNodeId writeValueLeftId = ctx.allocNode();
  const HirNodeId writeValueRightId = ctx.allocNode();
  const HirNodeId writeValueId = ctx.allocNode();
  const HirNodeId writeId = ctx.allocNode();
  const HirNodeId loopId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId returnValueId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(loopId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, returnValueId,
                                   function.returnSpan.clone()});

  // Init local binding: `let i = 0` with its scalar literal initializer.
  ctx.addExpression(HirScalarLiteralExpression{
      initializerId, forLoop.initLiteral.type, zc::mv(forLoop.initLiteral.value),
      forLoop.initLiteral.category, forLoop.initLiteral.sourceSpan.clone()});
  ctx.addLocal(HirLocalBinding{localId, forLoop.local.local, forLoop.local.type, initializerId,
                               forLoop.local.sourceSpan.clone(),
                               ZC_ASSERT_NONNULL(forLoop.local.initializerSpan).clone()});

  // Loop condition: `i < 10`, a comparison of the init local against a scalar
  // literal. The bool result drives the loop header's SwitchInt terminator.
  ctx.addLocalReference(HirLocalReferenceExpression{
      comparisonLeftId, forLoop.conditionLeft.local, forLoop.conditionLeft.type,
      forLoop.conditionLeft.category, forLoop.conditionLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{
      comparisonRightId, forLoop.conditionRight.type, zc::mv(forLoop.conditionRight.value),
      forLoop.conditionRight.category, forLoop.conditionRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      conditionId, comparisonLeftId, comparisonRightId, forLoop.condition.operandType,
      forLoop.condition.type, forLoop.condition.category, forLoop.condition.operation,
      forLoop.condition.sourceSpan.clone()});

  // Update write: `i = i + 1`, an arithmetic binary over the init local and a
  // scalar literal, materialized as the loop body's sole statement.
  ctx.addLocalReference(HirLocalReferenceExpression{
      writeValueLeftId, forLoop.writeValueLeft.local, forLoop.writeValueLeft.type,
      forLoop.writeValueLeft.category, forLoop.writeValueLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{
      writeValueRightId, forLoop.writeValueRight.type, zc::mv(forLoop.writeValueRight.value),
      forLoop.writeValueRight.category, forLoop.writeValueRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      writeValueId, writeValueLeftId, writeValueRightId, forLoop.writeValue.operandType,
      forLoop.writeValue.type, forLoop.writeValue.category, forLoop.writeValue.operation,
      forLoop.writeValue.sourceSpan.clone()});
  ctx.addLocalWrite(HirLocalWriteStatement{
      writeId, forLoop.write.local, forLoop.write.field, forLoop.write.type, writeValueId,
      forLoop.write.kind, forLoop.write.sourceSpan.clone(), forLoop.write.valueSpan.clone()});

  zc::Vector<HirNodeId> loopStatements;
  loopStatements.add(writeId);
  ctx.addLoop(HirLoopStatement{loopId, conditionId, zc::mv(loopStatements), forLoop.condition.type,
                               HirValueCategory::Place, zc::mv(forLoop.loopSpan), zc::none,
                               zc::none});
  ctx.addExpression(HirScalarLiteralExpression{
      returnValueId, function.resultType, ZC_ASSERT_NONNULL(function.literal).clone(),
      HirValueCategory::Value, function.valueSpan.clone()});
}

void lowerForLoopAccumulatorReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingForLoopAccumulatorReturn forLoop =
      zc::mv(ZC_ASSERT_NONNULL(function.forLoopAccumulatorReturn));

  // Fixed-id layout: function, body, accumulator local, accumulator init
  // literal, loop-init local, loop-init literal, comparison left, comparison
  // right, comparison condition, body-write left, body-write right, body-write
  // binary, body write, update left, update right, update binary, update write,
  // loop, return, return value reference. The body block lists [accumulator
  // local, loop-init local, loop, return]; the loop statement carries the
  // body-write and update-write node ids as its body.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId accumulatorLocalId = ctx.allocNode();
  const HirNodeId accumulatorInitId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId comparisonLeftId = ctx.allocNode();
  const HirNodeId comparisonRightId = ctx.allocNode();
  const HirNodeId conditionId = ctx.allocNode();
  const HirNodeId bodyWriteLeftId = ctx.allocNode();
  const HirNodeId bodyWriteRightId = ctx.allocNode();
  const HirNodeId bodyWriteValueId = ctx.allocNode();
  const HirNodeId bodyWriteId = ctx.allocNode();
  const HirNodeId writeValueLeftId = ctx.allocNode();
  const HirNodeId writeValueRightId = ctx.allocNode();
  const HirNodeId writeValueId = ctx.allocNode();
  const HirNodeId writeId = ctx.allocNode();
  const HirNodeId loopId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId returnValueId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(accumulatorLocalId);
  statements.add(localId);
  statements.add(loopId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, returnValueId,
                                   function.returnSpan.clone()});

  // Accumulator local binding: `let sum = 0` with its scalar literal
  // initializer.
  ctx.addExpression(HirScalarLiteralExpression{
      accumulatorInitId, forLoop.accumulatorInitLiteral.type,
      zc::mv(forLoop.accumulatorInitLiteral.value), forLoop.accumulatorInitLiteral.category,
      forLoop.accumulatorInitLiteral.sourceSpan.clone()});
  ctx.addLocal(HirLocalBinding{
      accumulatorLocalId, forLoop.accumulatorLocal.local, forLoop.accumulatorLocal.type,
      accumulatorInitId, forLoop.accumulatorLocal.sourceSpan.clone(),
      ZC_ASSERT_NONNULL(forLoop.accumulatorLocal.initializerSpan).clone()});

  // Loop-init local binding: `let i = 0` with its scalar literal initializer.
  ctx.addExpression(HirScalarLiteralExpression{
      initializerId, forLoop.initLiteral.type, zc::mv(forLoop.initLiteral.value),
      forLoop.initLiteral.category, forLoop.initLiteral.sourceSpan.clone()});
  ctx.addLocal(HirLocalBinding{localId, forLoop.local.local, forLoop.local.type, initializerId,
                               forLoop.local.sourceSpan.clone(),
                               ZC_ASSERT_NONNULL(forLoop.local.initializerSpan).clone()});

  // Loop condition: `i < 10`, a comparison of the init local against a scalar
  // literal. The bool result drives the loop header's SwitchInt terminator.
  ctx.addLocalReference(HirLocalReferenceExpression{
      comparisonLeftId, forLoop.conditionLeft.local, forLoop.conditionLeft.type,
      forLoop.conditionLeft.category, forLoop.conditionLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{
      comparisonRightId, forLoop.conditionRight.type, zc::mv(forLoop.conditionRight.value),
      forLoop.conditionRight.category, forLoop.conditionRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      conditionId, comparisonLeftId, comparisonRightId, forLoop.condition.operandType,
      forLoop.condition.type, forLoop.condition.category, forLoop.condition.operation,
      forLoop.condition.sourceSpan.clone()});

  // Body write: `sum = sum + i`, an arithmetic binary over the accumulator
  // and the loop init local, materialized as the loop body's first statement.
  ctx.addLocalReference(HirLocalReferenceExpression{
      bodyWriteLeftId, forLoop.bodyWriteLeft.local, forLoop.bodyWriteLeft.type,
      forLoop.bodyWriteLeft.category, forLoop.bodyWriteLeft.sourceSpan.clone()});
  ctx.addLocalReference(HirLocalReferenceExpression{
      bodyWriteRightId, forLoop.bodyWriteRight.local, forLoop.bodyWriteRight.type,
      forLoop.bodyWriteRight.category, forLoop.bodyWriteRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      bodyWriteValueId, bodyWriteLeftId, bodyWriteRightId, forLoop.bodyWriteValue.operandType,
      forLoop.bodyWriteValue.type, forLoop.bodyWriteValue.category,
      forLoop.bodyWriteValue.operation, forLoop.bodyWriteValue.sourceSpan.clone()});
  ctx.addLocalWrite(HirLocalWriteStatement{
      bodyWriteId, forLoop.bodyWrite.local, forLoop.bodyWrite.field, forLoop.bodyWrite.type,
      bodyWriteValueId, forLoop.bodyWrite.kind, forLoop.bodyWrite.sourceSpan.clone(),
      forLoop.bodyWrite.valueSpan.clone()});

  // Update write: `i = i + 1`, an arithmetic binary over the init local and a
  // scalar literal, materialized as the loop body's second statement.
  ctx.addLocalReference(HirLocalReferenceExpression{
      writeValueLeftId, forLoop.writeValueLeft.local, forLoop.writeValueLeft.type,
      forLoop.writeValueLeft.category, forLoop.writeValueLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{
      writeValueRightId, forLoop.writeValueRight.type, zc::mv(forLoop.writeValueRight.value),
      forLoop.writeValueRight.category, forLoop.writeValueRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      writeValueId, writeValueLeftId, writeValueRightId, forLoop.writeValue.operandType,
      forLoop.writeValue.type, forLoop.writeValue.category, forLoop.writeValue.operation,
      forLoop.writeValue.sourceSpan.clone()});
  ctx.addLocalWrite(HirLocalWriteStatement{
      writeId, forLoop.write.local, forLoop.write.field, forLoop.write.type, writeValueId,
      forLoop.write.kind, forLoop.write.sourceSpan.clone(), forLoop.write.valueSpan.clone()});

  zc::Vector<HirNodeId> loopStatements;
  loopStatements.add(bodyWriteId);
  loopStatements.add(writeId);
  ctx.addLoop(HirLoopStatement{loopId, conditionId, zc::mv(loopStatements), forLoop.condition.type,
                               HirValueCategory::Place, zc::mv(forLoop.loopSpan), zc::none,
                               zc::none});
  // The return value is a place reference to the accumulator local.
  ctx.addLocalReference(HirLocalReferenceExpression{
      returnValueId, forLoop.returnReference.local, forLoop.returnReference.type,
      forLoop.returnReference.category, forLoop.returnReference.sourceSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
