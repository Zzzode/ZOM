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

  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);

  ZC_IF_SOME(equality, conditional.condition.equality) {
    // Comparison-condition stride (9 + 2K nodes, where K is the count of binary
    // arms): function, body, left operand, right operand, equality comparison,
    // then arm value(s), else arm value(s), conditional, return. A binary arm
    // materializes three nodes (left operand, right operand, binary); a leaf arm
    // materializes one. The conditional takes the equality node as its
    // condition and each arm's value node (the binary node for a binary arm).
    const HirNodeId leftId = ctx.allocNode(equality.left.sourceNode);
    const HirNodeId rightId = ctx.allocNode(equality.right.sourceNode);
    const HirNodeId equalityId = ctx.allocNode(equality.sourceNode);
    const HirNodeId thenValueId = ctx.lowerArmValue(conditional.thenArm);
    const HirNodeId elseValueId = ctx.lowerArmValue(conditional.elseArm);
    const HirNodeId conditionalId = ctx.allocNode(conditional.conditionalNode);
    const HirNodeId returnId = ctx.allocNode(function.returnNode);

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
    ctx.addConditional(HirConditionalExpression{conditionalId, equalityId, thenValueId, elseValueId,
                                                function.resultType, HirValueCategory::Value,
                                                conditional.conditionalSpan.clone()});
    return;
  }

  ZC_IF_SOME(conjunctive, conditional.condition.conjunctive) {
    // Conjunctive-condition stride (11 + 2K nodes, where K is the count of
    // binary arms): function, body, scrutinee parameter reference, guard left
    // operand, guard right operand, guard comparison, conjunction (BitAnd),
    // then arm value(s), else arm value(s), conditional, return. The
    // conjunction is the conditional's condition; the guard comparison is the
    // conjunction's right operand.
    const HirNodeId conditionId = ctx.allocNode(ast::NodeId());
    const HirNodeId guardLeftId = ctx.allocNode(conjunctive.guard.left.sourceNode);
    const HirNodeId guardRightId = ctx.allocNode(conjunctive.guard.right.sourceNode);
    const HirNodeId guardComparisonId = ctx.allocNode(conjunctive.guard.sourceNode);
    const HirNodeId conjunctionId = ctx.allocNode(ast::NodeId());
    const HirNodeId thenValueId = ctx.lowerArmValue(conditional.thenArm);
    const HirNodeId elseValueId = ctx.lowerArmValue(conditional.elseArm);
    const HirNodeId conditionalId = ctx.allocNode(conditional.conditionalNode);
    const HirNodeId returnId = ctx.allocNode(function.returnNode);

    ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                           zc::mv(function.parameters), zc::mv(function.receiver),
                                           function.visibility.clone(), function.linkage,
                                           function.declarationSpan.clone(), bodyId, zc::none});
    zc::Vector<HirNodeId> statements;
    statements.add(returnId);
    ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
    ctx.addReturn(HirReturnStatement{returnId, function.resultType, conditionalId,
                                     function.returnSpan.clone()});
    ctx.addParameterReference(HirParameterReferenceExpression{
        conditionId, conjunctive.parameter.parameter.clone(), conjunctive.parameter.type,
        conjunctive.parameter.category, conjunctive.parameter.sourceSpan.clone()});
    ctx.lowerArmLeaf(guardLeftId, conjunctive.guard.left);
    ctx.lowerArmLeaf(guardRightId, conjunctive.guard.right);
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        guardComparisonId, guardLeftId, guardRightId, conjunctive.guard.operandType,
        conjunctive.guard.type, HirValueCategory::Value, conjunctive.guard.operation,
        conjunctive.guard.sourceSpan.clone(), conjunctive.guard.isUnaryDesugar});
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        conjunctionId, conditionId, guardComparisonId, conjunctive.parameter.type,
        conjunctive.parameter.type, HirValueCategory::Value,
        checker::PrimitiveOperation::LogicalAnd, conjunctive.sourceSpan.clone(), false});
    ctx.addConditional(HirConditionalExpression{
        conditionalId, conjunctionId, thenValueId, elseValueId, function.resultType,
        HirValueCategory::Value, conditional.conditionalSpan.clone()});
    return;
  }

  // Parameter-condition stride (7 + 2K nodes, where K is the count of binary
  // arms): function, body, condition parameter reference, then arm value(s),
  // else arm value(s), conditional, return.
  const HirNodeId conditionId = ctx.allocNode(ast::NodeId());
  const HirNodeId thenValueId = ctx.lowerArmValue(conditional.thenArm);
  const HirNodeId elseValueId = ctx.lowerArmValue(conditional.elseArm);
  const HirNodeId conditionalId = ctx.allocNode(conditional.conditionalNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

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
  ctx.addConditional(HirConditionalExpression{conditionalId, conditionId, thenValueId, elseValueId,
                                              function.resultType, HirValueCategory::Value,
                                              conditional.conditionalSpan.clone()});
}

void lowerChainedConditionalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingChainedConditionalReturn chained =
      zc::mv(ZC_ASSERT_NONNULL(function.chainedConditionalReturn));

  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const size_t armCount = chained.entries.size();

  // Per-arm node IDs: left operand, right operand, equality, then value.
  zc::Vector<HirNodeId> leftIds;
  zc::Vector<HirNodeId> rightIds;
  zc::Vector<HirNodeId> equalityIds;
  zc::Vector<HirNodeId> thenIds;
  for (size_t i = 0; i < armCount; ++i) {
    leftIds.add(ctx.allocNode(chained.entries[i].condition.left.sourceNode));
    rightIds.add(ctx.allocNode(chained.entries[i].condition.right.sourceNode));
    equalityIds.add(ctx.allocNode(chained.entries[i].condition.sourceNode));
    thenIds.add(ctx.allocNode(chained.entries[i].thenArm.sourceNode));
  }
  const HirNodeId elseId = ctx.allocNode(chained.elseArm.sourceNode);
  // Conditional IDs: innermost first, outermost last.
  zc::Vector<HirNodeId> conditionalIds;
  for (size_t i = armCount; i-- > 0;) { conditionalIds.add(ctx.allocNode(chained.matchNode)); }
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, conditionalIds[armCount - 1],
                                   function.returnSpan.clone()});

  for (size_t i = 0; i < armCount; ++i) {
    const auto& entry = chained.entries[i];
    ctx.lowerArmLeaf(leftIds[i], entry.condition.left);
    ctx.lowerArmLeaf(rightIds[i], entry.condition.right);
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        equalityIds[i], leftIds[i], rightIds[i], entry.condition.operandType, entry.condition.type,
        HirValueCategory::Value, entry.condition.operation, entry.condition.sourceSpan.clone(),
        entry.condition.isUnaryDesugar});
    ctx.lowerArmLeaf(thenIds[i], entry.thenArm);
  }
  ctx.lowerArmLeaf(elseId, chained.elseArm);

  // Build conditionals from inner to outer. conditionalIds[0] is the
  // innermost (last arm); conditionalIds[armCount-1] is the outermost.
  for (size_t i = 0; i < armCount; ++i) {
    const size_t armIndex = armCount - 1 - i;
    const HirNodeId elseTarget = (i == 0) ? elseId : conditionalIds[i - 1];
    ctx.addConditional(HirConditionalExpression{
        conditionalIds[i], equalityIds[armIndex], thenIds[armIndex], elseTarget,
        function.resultType, HirValueCategory::Value, chained.matchSpan.clone()});
  }
}

void lowerLeadingLocalConditionalReturnFunction(PendingFunctionDeclaration&& function,
                                                HirFnCtx& ctx) {
  PendingLeadingLocalConditionalReturn leading =
      zc::mv(ZC_ASSERT_NONNULL(function.leadingLocalConditionalReturn));
  const size_t bindingCount = leading.bindings.size();

  // Source-preorder layout: function, body, per binding (local, initializer),
  // then left operand, right operand, comparison, then arm value(s), else arm
  // value(s), conditional, return. A binary arm materializes three nodes
  // (left operand, right operand, binary); a leaf arm materializes one.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  zc::Vector<HirNodeId> localIds;
  zc::Vector<HirNodeId> initializerIds;
  zc::Vector<HirNodeId> arithLeftIds;
  zc::Vector<HirNodeId> arithRightIds;
  for (size_t index = 0; index < bindingCount; ++index) {
    localIds.add(ctx.allocNode(leading.bindings[index].declaratorNode));
    initializerIds.add(ctx.allocNode(leading.bindings[index].initializerNode));
    if (leading.bindings[index].kind == SequentialInitializerKind::PrimitiveBinary) {
      arithLeftIds.add(
          ctx.allocNode(ZC_ASSERT_NONNULL(leading.bindings[index].arithmeticLeft).sourceNode));
      arithRightIds.add(
          ctx.allocNode(ZC_ASSERT_NONNULL(leading.bindings[index].arithmeticRight).sourceNode));
    }
  }
  const HirNodeId leftId = ctx.allocNode(leading.left.sourceNode);
  const HirNodeId rightId = ctx.allocNode(leading.right.sourceNode);
  const HirNodeId equalityId = ctx.allocNode(ast::NodeId());
  // Arm nodes must be allocated before conditional and return to match the
  // verifier's fixed-id layout: equality, then arm(s), else arm(s),
  // conditional, return.
  const HirNodeId thenValueId = ctx.lowerArmValue(leading.thenArm);
  const HirNodeId elseValueId = ctx.lowerArmValue(leading.elseArm);
  const HirNodeId conditionalId = ctx.allocNode(leading.conditionalNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

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
      case SequentialInitializerKind::EnumVariant:
      case SequentialInitializerKind::FoldedStringLength:
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
      case SequentialInitializerKind::Increment:
      case SequentialInitializerKind::PostfixIncrement:
      case SequentialInitializerKind::Cast:
      case SequentialInitializerKind::Ternary:
      case SequentialInitializerKind::EnumVariantConstruction:
      case SequentialInitializerKind::FoldedStringConcat:
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
  ctx.addConditional(HirConditionalExpression{conditionalId, equalityId, thenValueId, elseValueId,
                                              leading.resultType, HirValueCategory::Value,
                                              leading.returnSpan.clone()});
}

void lowerLoopReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingLoopReturn loop = zc::mv(ZC_ASSERT_NONNULL(function.loopReturn));

  // Empty-body loop stride (6 nodes): function, body, condition parameter
  // reference, return literal, loop statement, return. The body block lists the
  // loop statement followed by the return.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId conditionId = ctx.allocNode(ast::NodeId());
  const HirNodeId returnValueId = ctx.allocNode(function.returnValueNode);
  const HirNodeId loopId = ctx.allocNode(loop.loopNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

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
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId initializerId = ctx.allocNode(ast::NodeId());
  zc::Vector<HirNodeId> writeIds;
  zc::Vector<HirNodeId> writeValueIds;
  for (size_t index = 0; index < writeCount; ++index) {
    ast::NodeId writeSource;
    ZC_IF_SOME(binary, function.localWriteValues[index].binary) { writeSource = binary.sourceNode; }
    writeIds.add(ctx.allocNode(writeSource));
    writeValueIds.add(ctx.allocNode(writeSource));
  }
  const HirNodeId returnId = ctx.allocNode(function.returnNode);
  const HirNodeId valueId = ctx.allocNode(function.returnValueNode);
  const HirNodeId loopConditionId = ctx.allocNode(ast::NodeId());
  const HirNodeId loopId = ctx.allocNode(loopBody.loopNode);
  // Binary operand nodes trail after the loop node, matching the verifier's
  // fixed-id layout (function, body, local, initializer, per-write write+value,
  // return, value, condition, loop, then per-binary left+right).
  zc::Vector<zc::Maybe<HirNodeId>> binaryLeftIds;
  zc::Vector<zc::Maybe<HirNodeId>> binaryRightIds;
  for (const auto& writeValue : function.localWriteValues) {
    zc::Maybe<HirNodeId> leftId;
    zc::Maybe<HirNodeId> rightId;
    if (writeValue.binary != zc::none) {
      leftId = ctx.allocNode(ZC_ASSERT_NONNULL(writeValue.binary).left.sourceNode);
      rightId = ctx.allocNode(ZC_ASSERT_NONNULL(writeValue.binary).right.sourceNode);
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
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId initializerId = ctx.allocNode(ast::NodeId());
  const HirNodeId comparisonLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId comparisonRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId conditionId = ctx.allocNode(ast::NodeId());
  const HirNodeId writeValueLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId writeValueRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId writeValueId = ctx.allocNode(ast::NodeId());
  const HirNodeId writeId = ctx.allocNode(ast::NodeId());
  const HirNodeId loopId = ctx.allocNode(forLoop.loopNode);
  const HirNodeId returnId = ctx.allocNode(forLoop.returnNode);
  const HirNodeId returnValueId = ctx.allocNode(function.returnValueNode);

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
                               HirValueCategory::Place, zc::mv(forLoop.loopSpan),
                               zc::mv(forLoop.breakSpan), zc::mv(forLoop.continueSpan)});
  ctx.addExpression(HirScalarLiteralExpression{
      returnValueId, function.resultType, ZC_ASSERT_NONNULL(function.literal).clone(),
      HirValueCategory::Value, function.valueSpan.clone()});
}

void lowerForLoopAccumulatorReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingForLoopAccumulatorReturn forLoop =
      zc::mv(ZC_ASSERT_NONNULL(function.forLoopAccumulatorReturn));
  const size_t accumulatorCount = forLoop.accumulators.size();

  // Fixed-id layout: function, body, then per accumulator (local binding, init
  // literal), then loop-init (local binding, init literal), comparison (left,
  // right, condition), then per accumulator body write (left, right, value,
  // write), then update (left, right, value, write), loop, return, return
  // value reference. Total: 14 + 6N nodes. The body block lists [accumulator
  // locals..., loop-init local, loop, return]; the loop statement carries the
  // body-write and update-write node ids as its body.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});

  zc::Vector<HirNodeId> bodyStatements;
  zc::Vector<HirNodeId> loopStatements;

  // Accumulator local bindings: `mut x = 0` with scalar literal initializers.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto& acc = forLoop.accumulators[k];
    const HirNodeId accLocalId = ctx.allocNode(acc.bindingNode);
    const HirNodeId accInitId = ctx.allocNode(ast::NodeId());
    ctx.addExpression(
        HirScalarLiteralExpression{accInitId, acc.initLiteral.type, zc::mv(acc.initLiteral.value),
                                   acc.initLiteral.category, acc.initLiteral.sourceSpan.clone()});
    ctx.addLocal(HirLocalBinding{accLocalId, acc.local.local, acc.local.type, accInitId,
                                 acc.local.sourceSpan.clone(),
                                 ZC_ASSERT_NONNULL(acc.local.initializerSpan).clone()});
    bodyStatements.add(accLocalId);
  }

  // Loop-init local binding: `let i = 0` with its scalar literal initializer.
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId initializerId = ctx.allocNode(ast::NodeId());
  ctx.addExpression(HirScalarLiteralExpression{
      initializerId, forLoop.initLiteral.type, zc::mv(forLoop.initLiteral.value),
      forLoop.initLiteral.category, forLoop.initLiteral.sourceSpan.clone()});
  ctx.addLocal(HirLocalBinding{localId, forLoop.local.local, forLoop.local.type, initializerId,
                               forLoop.local.sourceSpan.clone(),
                               ZC_ASSERT_NONNULL(forLoop.local.initializerSpan).clone()});
  bodyStatements.add(localId);

  // Loop condition: `i < 10`, a comparison of the init local against a scalar
  // literal. The bool result drives the loop header's SwitchInt terminator.
  const HirNodeId comparisonLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId comparisonRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId conditionId = ctx.allocNode(ast::NodeId());
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

  // Body writes: `x = x + i` or `x = x + 1`, arithmetic binaries over each
  // accumulator and the loop init local or a scalar literal.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto& acc = forLoop.accumulators[k];
    const HirNodeId bodyWriteLeftId = ctx.allocNode(ast::NodeId());
    const HirNodeId bodyWriteRightId = ctx.allocNode(ast::NodeId());
    const HirNodeId bodyWriteValueId = ctx.allocNode(ast::NodeId());
    const HirNodeId bodyWriteId = ctx.allocNode(acc.writeNode);
    ctx.addLocalReference(HirLocalReferenceExpression{
        bodyWriteLeftId, acc.bodyWriteLeft.local, acc.bodyWriteLeft.type,
        acc.bodyWriteLeft.category, acc.bodyWriteLeft.sourceSpan.clone()});
    if (acc.bodyWriteRightIsLiteral) {
      const auto& lit = ZC_ASSERT_NONNULL(acc.bodyWriteRightLiteral);
      ctx.addExpression(HirScalarLiteralExpression{bodyWriteRightId, lit.type, lit.value.clone(),
                                                   lit.category, lit.sourceSpan.clone()});
    } else {
      const auto& ref = ZC_ASSERT_NONNULL(acc.bodyWriteRight);
      ctx.addLocalReference(HirLocalReferenceExpression{bodyWriteRightId, ref.local, ref.type,
                                                        ref.category, ref.sourceSpan.clone()});
    }
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        bodyWriteValueId, bodyWriteLeftId, bodyWriteRightId, acc.bodyWriteValue.operandType,
        acc.bodyWriteValue.type, acc.bodyWriteValue.category, acc.bodyWriteValue.operation,
        acc.bodyWriteValue.sourceSpan.clone()});
    ctx.addLocalWrite(HirLocalWriteStatement{
        bodyWriteId, acc.bodyWrite.local, acc.bodyWrite.field, acc.bodyWrite.type, bodyWriteValueId,
        acc.bodyWrite.kind, acc.bodyWrite.sourceSpan.clone(), acc.bodyWrite.valueSpan.clone()});
    loopStatements.add(bodyWriteId);
  }

  // Update write: `i = i + 1`, an arithmetic binary over the init local and a
  // scalar literal, materialized as the loop body's last statement.
  const HirNodeId writeValueLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId writeValueRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId writeValueId = ctx.allocNode(ast::NodeId());
  const HirNodeId writeId = ctx.allocNode(ast::NodeId());
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
  loopStatements.add(writeId);

  // If-guarded break condition: `if (i == <lit>) { break; }`. Materialize the
  // comparison (left local-ref, right literal, binary) and wire the binary node
  // id to the loop statement's breakCondition field. The MIR builder lowers a
  // guard block that evaluates this comparison before the accumulator writes.
  HirNodeId breakConditionId{};
  if (forLoop.breakCondition != zc::none) {
    const HirNodeId breakCondLeftId = ctx.allocNode(ast::NodeId());
    const HirNodeId breakCondRightId = ctx.allocNode(ast::NodeId());
    breakConditionId = ctx.allocNode(ast::NodeId());
    const auto& breakCondLeft = ZC_ASSERT_NONNULL(forLoop.breakConditionLeft);
    const auto& breakCondRight = ZC_ASSERT_NONNULL(forLoop.breakConditionRight);
    const auto& breakCond = ZC_ASSERT_NONNULL(forLoop.breakCondition);
    ctx.addLocalReference(HirLocalReferenceExpression{breakCondLeftId, breakCondLeft.local,
                                                      breakCondLeft.type, breakCondLeft.category,
                                                      breakCondLeft.sourceSpan.clone()});
    ctx.addExpression(HirScalarLiteralExpression{
        breakCondRightId, breakCondRight.type, breakCondRight.value.clone(),
        breakCondRight.category, breakCondRight.sourceSpan.clone()});
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        breakConditionId, breakCondLeftId, breakCondRightId, breakCond.operandType, breakCond.type,
        breakCond.category, breakCond.operation, breakCond.sourceSpan.clone()});
  }

  const HirNodeId loopId = ctx.allocNode(forLoop.loopNode);
  const HirNodeId returnId = ctx.allocNode(forLoop.returnNode);
  const HirNodeId returnValueId = ctx.allocNode(function.returnValueNode);
  bodyStatements.add(loopId);
  bodyStatements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(bodyStatements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, returnValueId,
                                   function.returnSpan.clone()});
  ctx.addLoop(HirLoopStatement{loopId, conditionId, zc::mv(loopStatements), forLoop.condition.type,
                               HirValueCategory::Place, zc::mv(forLoop.loopSpan),
                               zc::mv(forLoop.breakSpan), zc::mv(forLoop.continueSpan),
                               breakConditionId});
  // The return value is a place reference to the first accumulator local.
  ctx.addLocalReference(HirLocalReferenceExpression{
      returnValueId, forLoop.returnReference.local, forLoop.returnReference.type,
      forLoop.returnReference.category, forLoop.returnReference.sourceSpan.clone()});
}

void lowerNestedForLoopAccumulatorReturnFunction(PendingFunctionDeclaration&& function,
                                                 HirFnCtx& ctx) {
  PendingNestedForLoopAccumulatorReturn nested =
      zc::mv(ZC_ASSERT_NONNULL(function.nestedForLoopAccumulatorReturn));
  const size_t accumulatorCount = nested.accumulators.size();

  // Fixed-id layout: function, body, then per accumulator (local binding, init
  // literal), then outer-init (local binding, init literal), outer comparison
  // (left, right, condition), then inner-init (local binding, init literal),
  // inner comparison (left, right, condition), then per accumulator body write
  // (left, right, value, write), then inner update (left, right, value, write),
  // inner loop, then outer update (left, right, value, write), outer loop,
  // return, return value reference. Total: 24 + 6N nodes.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});

  zc::Vector<HirNodeId> bodyStatements;
  zc::Vector<HirNodeId> outerLoopStatements;
  zc::Vector<HirNodeId> innerLoopStatements;

  // Accumulator local bindings: `mut x = 0` with scalar literal initializers.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto& acc = nested.accumulators[k];
    const HirNodeId accLocalId = ctx.allocNode(acc.bindingNode);
    const HirNodeId accInitId = ctx.allocNode(ast::NodeId());
    ctx.addExpression(
        HirScalarLiteralExpression{accInitId, acc.initLiteral.type, zc::mv(acc.initLiteral.value),
                                   acc.initLiteral.category, acc.initLiteral.sourceSpan.clone()});
    ctx.addLocal(HirLocalBinding{accLocalId, acc.local.local, acc.local.type, accInitId,
                                 acc.local.sourceSpan.clone(),
                                 ZC_ASSERT_NONNULL(acc.local.initializerSpan).clone()});
    bodyStatements.add(accLocalId);
  }

  // Outer loop-init local binding: `let i = 0` with scalar literal initializer.
  const HirNodeId outerLocalId = ctx.allocNode(ast::NodeId());
  const HirNodeId outerInitializerId = ctx.allocNode(ast::NodeId());
  ctx.addExpression(HirScalarLiteralExpression{
      outerInitializerId, nested.outerInitLiteral.type, zc::mv(nested.outerInitLiteral.value),
      nested.outerInitLiteral.category, nested.outerInitLiteral.sourceSpan.clone()});
  ctx.addLocal(HirLocalBinding{outerLocalId, nested.outerLocal.local, nested.outerLocal.type,
                               outerInitializerId, nested.outerLocal.sourceSpan.clone(),
                               ZC_ASSERT_NONNULL(nested.outerLocal.initializerSpan).clone()});
  bodyStatements.add(outerLocalId);

  // Outer loop condition: `i < 3`, a comparison of the outer init local
  // against a scalar literal.
  const HirNodeId outerCmpLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId outerCmpRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId outerConditionId = ctx.allocNode(ast::NodeId());
  ctx.addLocalReference(HirLocalReferenceExpression{
      outerCmpLeftId, nested.outerConditionLeft.local, nested.outerConditionLeft.type,
      nested.outerConditionLeft.category, nested.outerConditionLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{
      outerCmpRightId, nested.outerConditionRight.type, zc::mv(nested.outerConditionRight.value),
      nested.outerConditionRight.category, nested.outerConditionRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      outerConditionId, outerCmpLeftId, outerCmpRightId, nested.outerCondition.operandType,
      nested.outerCondition.type, nested.outerCondition.category, nested.outerCondition.operation,
      nested.outerCondition.sourceSpan.clone()});

  // Inner loop-init local binding: `let j = 0` with scalar literal initializer.
  const HirNodeId innerLocalId = ctx.allocNode(ast::NodeId());
  const HirNodeId innerInitializerId = ctx.allocNode(ast::NodeId());
  ctx.addExpression(HirScalarLiteralExpression{
      innerInitializerId, nested.innerInitLiteral.type, zc::mv(nested.innerInitLiteral.value),
      nested.innerInitLiteral.category, nested.innerInitLiteral.sourceSpan.clone()});
  ctx.addLocal(HirLocalBinding{innerLocalId, nested.innerLocal.local, nested.innerLocal.type,
                               innerInitializerId, nested.innerLocal.sourceSpan.clone(),
                               ZC_ASSERT_NONNULL(nested.innerLocal.initializerSpan).clone()});

  // Inner loop condition: `j < 3`, a comparison of the inner init local
  // against a scalar literal.
  const HirNodeId innerCmpLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId innerCmpRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId innerConditionId = ctx.allocNode(ast::NodeId());
  ctx.addLocalReference(HirLocalReferenceExpression{
      innerCmpLeftId, nested.innerConditionLeft.local, nested.innerConditionLeft.type,
      nested.innerConditionLeft.category, nested.innerConditionLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{
      innerCmpRightId, nested.innerConditionRight.type, zc::mv(nested.innerConditionRight.value),
      nested.innerConditionRight.category, nested.innerConditionRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      innerConditionId, innerCmpLeftId, innerCmpRightId, nested.innerCondition.operandType,
      nested.innerCondition.type, nested.innerCondition.category, nested.innerCondition.operation,
      nested.innerCondition.sourceSpan.clone()});

  // Accumulator body writes: `sum = sum + 1`, arithmetic binaries over each
  // accumulator and the inner init local or a scalar literal. These live in
  // the inner loop body.
  for (size_t k = 0; k < accumulatorCount; ++k) {
    auto& acc = nested.accumulators[k];
    const HirNodeId bodyWriteLeftId = ctx.allocNode(ast::NodeId());
    const HirNodeId bodyWriteRightId = ctx.allocNode(ast::NodeId());
    const HirNodeId bodyWriteValueId = ctx.allocNode(ast::NodeId());
    const HirNodeId bodyWriteId = ctx.allocNode(acc.writeNode);
    ctx.addLocalReference(HirLocalReferenceExpression{
        bodyWriteLeftId, acc.bodyWriteLeft.local, acc.bodyWriteLeft.type,
        acc.bodyWriteLeft.category, acc.bodyWriteLeft.sourceSpan.clone()});
    if (acc.bodyWriteRightIsLiteral) {
      const auto& lit = ZC_ASSERT_NONNULL(acc.bodyWriteRightLiteral);
      ctx.addExpression(HirScalarLiteralExpression{bodyWriteRightId, lit.type, lit.value.clone(),
                                                   lit.category, lit.sourceSpan.clone()});
    } else {
      const auto& ref = ZC_ASSERT_NONNULL(acc.bodyWriteRight);
      ctx.addLocalReference(HirLocalReferenceExpression{bodyWriteRightId, ref.local, ref.type,
                                                        ref.category, ref.sourceSpan.clone()});
    }
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        bodyWriteValueId, bodyWriteLeftId, bodyWriteRightId, acc.bodyWriteValue.operandType,
        acc.bodyWriteValue.type, acc.bodyWriteValue.category, acc.bodyWriteValue.operation,
        acc.bodyWriteValue.sourceSpan.clone()});
    ctx.addLocalWrite(HirLocalWriteStatement{
        bodyWriteId, acc.bodyWrite.local, acc.bodyWrite.field, acc.bodyWrite.type, bodyWriteValueId,
        acc.bodyWrite.kind, acc.bodyWrite.sourceSpan.clone(), acc.bodyWrite.valueSpan.clone()});
    innerLoopStatements.add(bodyWriteId);
  }

  // Inner loop update write: `j = j + 1`, an arithmetic binary over the inner
  // init local and a scalar literal. This is the inner loop body's last
  // statement.
  const HirNodeId innerWriteLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId innerWriteRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId innerWriteValueId = ctx.allocNode(ast::NodeId());
  const HirNodeId innerWriteId = ctx.allocNode(ast::NodeId());
  ctx.addLocalReference(HirLocalReferenceExpression{
      innerWriteLeftId, nested.innerWriteValueLeft.local, nested.innerWriteValueLeft.type,
      nested.innerWriteValueLeft.category, nested.innerWriteValueLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{innerWriteRightId, nested.innerWriteValueRight.type,
                                               zc::mv(nested.innerWriteValueRight.value),
                                               nested.innerWriteValueRight.category,
                                               nested.innerWriteValueRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      innerWriteValueId, innerWriteLeftId, innerWriteRightId, nested.innerWriteValue.operandType,
      nested.innerWriteValue.type, nested.innerWriteValue.category,
      nested.innerWriteValue.operation, nested.innerWriteValue.sourceSpan.clone()});
  ctx.addLocalWrite(HirLocalWriteStatement{
      innerWriteId, nested.innerWrite.local, nested.innerWrite.field, nested.innerWrite.type,
      innerWriteValueId, nested.innerWrite.kind, nested.innerWrite.sourceSpan.clone(),
      nested.innerWrite.valueSpan.clone()});
  innerLoopStatements.add(innerWriteId);

  // Inner loop statement.
  const HirNodeId innerLoopId = ctx.allocNode(nested.innerLoopNode);
  ctx.addLoop(HirLoopStatement{innerLoopId, innerConditionId, zc::mv(innerLoopStatements),
                               nested.innerCondition.type, HirValueCategory::Place,
                               zc::mv(nested.innerLoopSpan), zc::none, zc::none});
  outerLoopStatements.add(innerLocalId);
  outerLoopStatements.add(innerLoopId);

  // Outer loop update write: `i = i + 1`, an arithmetic binary over the outer
  // init local and a scalar literal. This is the outer loop body's last
  // statement, after the inner loop.
  const HirNodeId outerWriteLeftId = ctx.allocNode(ast::NodeId());
  const HirNodeId outerWriteRightId = ctx.allocNode(ast::NodeId());
  const HirNodeId outerWriteValueId = ctx.allocNode(ast::NodeId());
  const HirNodeId outerWriteId = ctx.allocNode(ast::NodeId());
  ctx.addLocalReference(HirLocalReferenceExpression{
      outerWriteLeftId, nested.outerWriteValueLeft.local, nested.outerWriteValueLeft.type,
      nested.outerWriteValueLeft.category, nested.outerWriteValueLeft.sourceSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{outerWriteRightId, nested.outerWriteValueRight.type,
                                               zc::mv(nested.outerWriteValueRight.value),
                                               nested.outerWriteValueRight.category,
                                               nested.outerWriteValueRight.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      outerWriteValueId, outerWriteLeftId, outerWriteRightId, nested.outerWriteValue.operandType,
      nested.outerWriteValue.type, nested.outerWriteValue.category,
      nested.outerWriteValue.operation, nested.outerWriteValue.sourceSpan.clone()});
  ctx.addLocalWrite(HirLocalWriteStatement{
      outerWriteId, nested.outerWrite.local, nested.outerWrite.field, nested.outerWrite.type,
      outerWriteValueId, nested.outerWrite.kind, nested.outerWrite.sourceSpan.clone(),
      nested.outerWrite.valueSpan.clone()});
  outerLoopStatements.add(outerWriteId);

  // Outer loop statement.
  const HirNodeId outerLoopId = ctx.allocNode(nested.outerLoopNode);
  const HirNodeId returnId = ctx.allocNode(nested.returnNode);
  const HirNodeId returnValueId = ctx.allocNode(function.returnValueNode);
  bodyStatements.add(outerLoopId);
  bodyStatements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(bodyStatements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, returnValueId,
                                   function.returnSpan.clone()});
  ctx.addLoop(HirLoopStatement{outerLoopId, outerConditionId, zc::mv(outerLoopStatements),
                               nested.outerCondition.type, HirValueCategory::Place,
                               zc::mv(nested.outerLoopSpan), zc::none, zc::none});
  // The return value is a place reference to the first accumulator local.
  ctx.addLocalReference(HirLocalReferenceExpression{
      returnValueId, nested.returnReference.local, nested.returnReference.type,
      nested.returnReference.category, nested.returnReference.sourceSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
