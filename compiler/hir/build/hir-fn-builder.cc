// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/hir-fn-builder.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

HirFnCtx::HirFnCtx(uint32_t& nextNode, zc::Vector<HirFunctionDeclaration>& functions,
                   zc::Vector<HirBlockStatement>& blocks, zc::Vector<HirReturnStatement>& returns,
                   zc::Vector<HirScalarLiteralExpression>& expressions,
                   zc::Vector<HirParameterReferenceExpression>& parameterReferences,
                   zc::Vector<HirLocalBinding>& locals,
                   zc::Vector<HirLocalWriteStatement>& localWrites,
                   zc::Vector<HirLocalReferenceExpression>& localReferences,
                   zc::Vector<HirPrimitiveBinaryExpression>& primitiveBinaryOperations,
                   zc::Vector<HirNominalAggregateExpression>& aggregates,
                   zc::Vector<HirLocalFieldProjectionExpression>& localFieldProjections,
                   zc::Vector<HirUnsafeBlockExpression>& unsafeBlocks) noexcept
    : nextNode(&nextNode),
      functions(&functions),
      blocks(&blocks),
      returns(&returns),
      expressions(&expressions),
      parameterReferences(&parameterReferences),
      locals(&locals),
      localWrites(&localWrites),
      localReferences(&localReferences),
      primitiveBinaryOperations(&primitiveBinaryOperations),
      aggregates(&aggregates),
      localFieldProjections(&localFieldProjections),
      unsafeBlocks(&unsafeBlocks) {}

HirNodeId HirFnCtx::allocNode() {
  HirNodeId id = hirId(*nextNode);
  ++(*nextNode);
  return id;
}

void HirFnCtx::addFunction(HirFunctionDeclaration declaration) {
  functions->add(zc::mv(declaration));
}

void HirFnCtx::addBlock(HirBlockStatement block) { blocks->add(zc::mv(block)); }

void HirFnCtx::addReturn(HirReturnStatement statement) { returns->add(zc::mv(statement)); }

void HirFnCtx::addExpression(HirScalarLiteralExpression expression) {
  expressions->add(zc::mv(expression));
}

void HirFnCtx::addParameterReference(HirParameterReferenceExpression reference) {
  parameterReferences->add(zc::mv(reference));
}

void HirFnCtx::addLocal(HirLocalBinding local) { locals->add(zc::mv(local)); }

void HirFnCtx::addLocalWrite(HirLocalWriteStatement write) { localWrites->add(zc::mv(write)); }

void HirFnCtx::addLocalReference(HirLocalReferenceExpression reference) {
  localReferences->add(zc::mv(reference));
}

void HirFnCtx::addPrimitiveBinary(HirPrimitiveBinaryExpression operation) {
  primitiveBinaryOperations->add(zc::mv(operation));
}

void HirFnCtx::addAggregate(HirNominalAggregateExpression aggregate) {
  aggregates->add(zc::mv(aggregate));
}

void HirFnCtx::addLocalFieldProjection(HirLocalFieldProjectionExpression projection) {
  localFieldProjections->add(zc::mv(projection));
}

void HirFnCtx::addUnsafeBlock(HirUnsafeBlockExpression block) { unsafeBlocks->add(zc::mv(block)); }

void HirFnCtx::lowerArmLeaf(HirNodeId destination, const PendingConditionalArm& leaf) {
  ZC_IF_SOME(reference, leaf.parameter) {
    addParameterReference(HirParameterReferenceExpression{destination, reference.parameter.clone(),
                                                          leaf.type, HirValueCategory::Place,
                                                          leaf.sourceSpan.clone()});
    return;
  }
  ZC_IF_SOME(literal, leaf.literal) {
    addExpression(HirScalarLiteralExpression{destination, leaf.type, literal.clone(),
                                             HirValueCategory::Value, leaf.sourceSpan.clone()});
  }
}

namespace {

/// \brief Lowers the family-1 scalar leaf (literal or resolved parameter) into
/// its destination. The scalar-return path carries the two alternatives in the
/// pending function's top-level fields rather than an arm struct, so this
/// adapter builds the same records as HirFnCtx::lowerArmLeaf.
void lowerScalarLeaf(HirNodeId destination, identity::SemanticTypeId literalType,
                     const identity::SourceSpan& literalSpan,
                     const zc::Maybe<checker::checked::CanonicalConstValue>& literal,
                     const zc::Maybe<HirParameterReferenceExpression>& parameter, HirFnCtx& ctx) {
  ZC_IF_SOME(reference, parameter) {
    ctx.addParameterReference(
        HirParameterReferenceExpression{destination, reference.parameter.clone(), reference.type,
                                        reference.category, reference.sourceSpan.clone()});
    return;
  }
  ZC_IF_SOME(value, literal) {
    ctx.addExpression(HirScalarLiteralExpression{destination, literalType, value.clone(),
                                                 HirValueCategory::Value, literalSpan.clone()});
  }
}

/// \brief Emits the shared function header record, consuming its parameters.
HirFunctionDeclaration lowerFunctionHeader(HirNodeId functionId, HirNodeId bodyId,
                                           PendingFunctionDeclaration& function) {
  return HirFunctionDeclaration{functionId,
                                function.definition,
                                function.resultType,
                                zc::mv(function.parameters),
                                function.visibility.clone(),
                                function.linkage,
                                function.declarationSpan.clone(),
                                bodyId,
                                zc::none};
}

}  // namespace

void lowerScalarReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();

  ctx.addFunction(lowerFunctionHeader(functionId, bodyId, function));
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  lowerScalarLeaf(valueId, function.resultType, function.valueSpan, function.literal,
                  function.parameterReference, ctx);
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
}

void lowerLocalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::SemanticTypeId placeType = ZC_ASSERT_NONNULL(function.localReference).type;
  const HirValueCategory placeCategory = ZC_ASSERT_NONNULL(function.localReference).category;
  const identity::SourceSpan placeSpan =
      ZC_ASSERT_NONNULL(function.localReference).sourceSpan.clone();

  // Fixed source-preorder stride for the one-binding shape, matching the
  // generic materializer: function F, body F+1, local F+2, initializer F+3,
  // return F+4, value F+5. The block lists [local, return].
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();

  ctx.addFunction(lowerFunctionHeader(functionId, bodyId, function));
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  lowerScalarLeaf(initializerId, localType, initializerSpan, function.literal,
                  function.parameterReference, ctx);
  ctx.addLocalReference(HirLocalReferenceExpression{valueId, hirLocalId(1), placeType,
                                                    placeCategory, placeSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
}

void lowerSequentialLocalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  PendingSequentialLocalReturn sequential =
      zc::mv(ZC_ASSERT_NONNULL(function.sequentialLocalReturn));
  const size_t bindingCount = sequential.bindings.size();

  // Reserve node ids in source preorder before emitting any record, matching
  // the generic materializer's two-pass allocation: function, body, then for
  // each binding its local and initializer plus a binary binding's two operand
  // nodes and each nested operand's two leaf nodes, then return and value.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  zc::Vector<HirNodeId> localNodeIds;
  zc::Vector<HirNodeId> initializerNodeIds;
  zc::Vector<zc::Maybe<HirNodeId>> leftOperandIds;
  zc::Vector<zc::Maybe<HirNodeId>> rightOperandIds;
  zc::Vector<zc::Maybe<HirNodeId>> leftNestedLeafLeftIds;
  zc::Vector<zc::Maybe<HirNodeId>> leftNestedLeafRightIds;
  zc::Vector<zc::Maybe<HirNodeId>> rightNestedLeafLeftIds;
  zc::Vector<zc::Maybe<HirNodeId>> rightNestedLeafRightIds;
  for (size_t index = 0; index < bindingCount; ++index) {
    localNodeIds.add(ctx.allocNode());
    initializerNodeIds.add(ctx.allocNode());
    zc::Maybe<HirNodeId> leftOperandId;
    zc::Maybe<HirNodeId> rightOperandId;
    zc::Maybe<HirNodeId> leftNestedLeafLeftId;
    zc::Maybe<HirNodeId> leftNestedLeafRightId;
    zc::Maybe<HirNodeId> rightNestedLeafLeftId;
    zc::Maybe<HirNodeId> rightNestedLeafRightId;
    if (sequential.bindings[index].kind == SequentialInitializerKind::PrimitiveBinary) {
      leftOperandId = ctx.allocNode();
      rightOperandId = ctx.allocNode();
      ZC_IF_SOME(left, sequential.bindings[index].leftOperand) {
        if (left.kind == SequentialBinaryOperandKind::NestedBinary) {
          leftNestedLeafLeftId = ctx.allocNode();
          leftNestedLeafRightId = ctx.allocNode();
        }
      }
      ZC_IF_SOME(right, sequential.bindings[index].rightOperand) {
        if (right.kind == SequentialBinaryOperandKind::NestedBinary) {
          rightNestedLeafLeftId = ctx.allocNode();
          rightNestedLeafRightId = ctx.allocNode();
        }
      }
    }
    leftOperandIds.add(zc::mv(leftOperandId));
    rightOperandIds.add(zc::mv(rightOperandId));
    leftNestedLeafLeftIds.add(zc::mv(leftNestedLeafLeftId));
    leftNestedLeafRightIds.add(zc::mv(leftNestedLeafRightId));
    rightNestedLeafLeftIds.add(zc::mv(rightNestedLeafLeftId));
    rightNestedLeafRightIds.add(zc::mv(rightNestedLeafRightId));
  }
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId returnValueId = ctx.allocNode();
  zc::Maybe<HirNodeId> unsafeBlockId;
  if (function.unsafeBlockSpan != zc::none) {
    unsafeBlockId = ctx.allocNode();
    ctx.addUnsafeBlock(HirUnsafeBlockExpression{
        ZC_ASSERT_NONNULL(unsafeBlockId), returnValueId, function.resultType,
        ZC_ASSERT_NONNULL(function.unsafeBlockSpan).clone()});
  }

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), function.visibility.clone(),
                                         function.linkage, function.declarationSpan.clone(), bodyId,
                                         zc::mv(unsafeBlockId)});
  zc::Vector<HirNodeId> statements;
  for (const auto localNodeId : localNodeIds) { statements.add(localNodeId); }
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(HirReturnStatement{returnId, function.resultType, returnValueId,
                                   function.returnSpan.clone()});

  // Lowers one binary leaf (never itself a nested binary) into its destination.
  auto lowerBinaryLeaf = [&](HirNodeId leafId, const PendingSequentialBinaryLeafOperand& leaf) {
    switch (leaf.kind) {
      case SequentialBinaryOperandKind::Literal:
        ZC_IF_SOME(literal, leaf.literal) {
          ctx.addExpression(HirScalarLiteralExpression{leafId, leaf.type, literal.clone(),
                                                       HirValueCategory::Value,
                                                       leaf.sourceSpan.clone()});
        }
        break;
      case SequentialBinaryOperandKind::ParameterReference:
        ZC_IF_SOME(parameter, leaf.parameter) {
          ctx.addParameterReference(
              HirParameterReferenceExpression{leafId, parameter.clone(), leaf.type,
                                              HirValueCategory::Place, leaf.sourceSpan.clone()});
        }
        break;
      case SequentialBinaryOperandKind::LocalReference:
        ctx.addLocalReference(HirLocalReferenceExpression{
            leafId, hirLocalId(static_cast<uint32_t>(leaf.referencedLocal + 1)), leaf.type,
            HirValueCategory::Place, leaf.sourceSpan.clone()});
        break;
      case SequentialBinaryOperandKind::NestedBinary:
        break;
    }
  };
  auto lowerBinaryOperand = [&](HirNodeId operandId, const PendingSequentialBinaryOperand& operand,
                                zc::Maybe<HirNodeId> nestedLeafLeftId,
                                zc::Maybe<HirNodeId> nestedLeafRightId) {
    switch (operand.kind) {
      case SequentialBinaryOperandKind::Literal:
        ZC_IF_SOME(literal, operand.literal) {
          ctx.addExpression(HirScalarLiteralExpression{operandId, operand.type, literal.clone(),
                                                       HirValueCategory::Value,
                                                       operand.sourceSpan.clone()});
        }
        break;
      case SequentialBinaryOperandKind::ParameterReference:
        ZC_IF_SOME(parameter, operand.parameter) {
          ctx.addParameterReference(
              HirParameterReferenceExpression{operandId, parameter.clone(), operand.type,
                                              HirValueCategory::Place, operand.sourceSpan.clone()});
        }
        break;
      case SequentialBinaryOperandKind::LocalReference:
        ctx.addLocalReference(HirLocalReferenceExpression{
            operandId, hirLocalId(static_cast<uint32_t>(operand.referencedLocal + 1)), operand.type,
            HirValueCategory::Place, operand.sourceSpan.clone()});
        break;
      case SequentialBinaryOperandKind::NestedBinary: {
        HirNodeId leafLeftId;
        HirNodeId leafRightId;
        ZC_IF_SOME(id, nestedLeafLeftId) { leafLeftId = id; }
        ZC_IF_SOME(id, nestedLeafRightId) { leafRightId = id; }
        ZC_IF_SOME(leaf, operand.nestedLeft) { lowerBinaryLeaf(leafLeftId, leaf); }
        ZC_IF_SOME(leaf, operand.nestedRight) { lowerBinaryLeaf(leafRightId, leaf); }
        ZC_IF_SOME(operation, operand.nestedOperation) {
          ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
              operandId, leafLeftId, leafRightId, operand.type, operand.type,
              HirValueCategory::Value, operation, operand.sourceSpan.clone()});
        }
        break;
      }
    }
  };

  for (size_t index = 0; index < bindingCount; ++index) {
    PendingSequentialBinding& binding = sequential.bindings[index];
    const HirNodeId localNodeId = localNodeIds[index];
    const HirNodeId initializerNodeId = initializerNodeIds[index];
    switch (binding.kind) {
      case SequentialInitializerKind::Literal:
        ZC_IF_SOME(literal, binding.literal) {
          ctx.addExpression(HirScalarLiteralExpression{initializerNodeId, binding.type,
                                                       literal.clone(), HirValueCategory::Value,
                                                       binding.initializerSpan.clone()});
        }
        break;
      case SequentialInitializerKind::Aggregate:
        ZC_IF_SOME(aggregate, binding.aggregate) {
          ctx.addAggregate(HirNominalAggregateExpression{
              initializerNodeId, aggregate.definition, aggregate.type, zc::mv(aggregate.elements),
              aggregate.category, aggregate.sourceSpan.clone()});
        }
        break;
      case SequentialInitializerKind::LocalReference:
        ctx.addLocalReference(HirLocalReferenceExpression{
            initializerNodeId, hirLocalId(static_cast<uint32_t>(binding.referencedLocal + 1)),
            binding.type, HirValueCategory::Place, binding.initializerSpan.clone()});
        break;
      case SequentialInitializerKind::ParameterReference:
        ZC_IF_SOME(parameter, binding.parameter) {
          ctx.addParameterReference(HirParameterReferenceExpression{
              initializerNodeId, parameter.clone(), binding.type, HirValueCategory::Place,
              binding.initializerSpan.clone()});
        }
        break;
      case SequentialInitializerKind::PrimitiveBinary: {
        HirNodeId leftOperandId;
        HirNodeId rightOperandId;
        ZC_IF_SOME(id, leftOperandIds[index]) { leftOperandId = id; }
        ZC_IF_SOME(id, rightOperandIds[index]) { rightOperandId = id; }
        ZC_IF_SOME(left, binding.leftOperand) {
          lowerBinaryOperand(leftOperandId, left, leftNestedLeafLeftIds[index],
                             leftNestedLeafRightIds[index]);
        }
        ZC_IF_SOME(right, binding.rightOperand) {
          lowerBinaryOperand(rightOperandId, right, rightNestedLeafLeftIds[index],
                             rightNestedLeafRightIds[index]);
        }
        ZC_IF_SOME(operation, binding.operation) {
          ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
              initializerNodeId, leftOperandId, rightOperandId, binding.operandType, binding.type,
              HirValueCategory::Value, operation, binding.initializerSpan.clone()});
        }
        break;
      }
    }
    ctx.addLocal(HirLocalBinding{localNodeId, hirLocalId(static_cast<uint32_t>(index + 1)),
                                 binding.type, initializerNodeId, binding.patternSpan.clone(),
                                 binding.initializerSpan.clone()});
  }

  ZC_IF_SOME(parameter, sequential.returnParameter) {
    ctx.addParameterReference(HirParameterReferenceExpression{
        returnValueId, parameter.clone(), sequential.type, HirValueCategory::Place,
        sequential.returnValueSpan.clone()});
  }
  if (sequential.returnParameter == zc::none) {
    ctx.addLocalReference(HirLocalReferenceExpression{
        returnValueId, hirLocalId(static_cast<uint32_t>(sequential.returnLocal + 1)),
        sequential.type, HirValueCategory::Place, sequential.returnValueSpan.clone()});
  }
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
