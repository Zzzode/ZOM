// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-generic.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerGenericFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  auto& generic = function.shape.get<GenericShape>();

  // Comparison-return path: 6 nodes in source preorder (function, body, left
  // operand, right operand, comparison, return). The return value is the
  // comparison node directly. Each operand is a scalar literal, parameter
  // reference, or local reference.
  ZC_IF_SOME(comparison, generic.comparisonReturn) {
    const HirNodeId functionId = ctx.allocNode(function.sourceNode);
    const HirNodeId bodyId = ctx.allocNode(ast::NodeId());
    const HirNodeId leftId = ctx.allocNode(ast::NodeId());
    const HirNodeId rightId = ctx.allocNode(ast::NodeId());
    const HirNodeId comparisonId = ctx.allocNode(ast::NodeId());
    const HirNodeId returnId = ctx.allocNode(ast::NodeId());

    ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                           zc::mv(function.parameters), zc::none,
                                           function.visibility.clone(), function.linkage,
                                           function.declarationSpan.clone(), bodyId, zc::none});
    zc::Vector<HirNodeId> statements;
    statements.add(returnId);
    ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
    ctx.addReturn(HirReturnStatement{returnId, function.resultType, comparisonId,
                                     function.returnSpan.clone()});

    auto materializeOperand = [&](HirNodeId operandId, const PendingConditionalArm& operand) {
      ZC_IF_SOME(reference, operand.parameter) {
        ctx.addParameterReference(
            HirParameterReferenceExpression{operandId, reference.parameter.clone(), operand.type,
                                            HirValueCategory::Place, operand.sourceSpan.clone()});
      }
      ZC_IF_SOME(literal, operand.literal) {
        ctx.addExpression(HirScalarLiteralExpression{operandId, operand.type, literal.clone(),
                                                     HirValueCategory::Value,
                                                     operand.sourceSpan.clone()});
      }
      ZC_IF_SOME(local, operand.local) {
        ctx.addLocalReference(HirLocalReferenceExpression{
            operandId, local.local, local.type, local.category, local.sourceSpan.clone()});
      }
    };
    materializeOperand(leftId, comparison.left);
    materializeOperand(rightId, comparison.right);
    ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
        comparisonId, leftId, rightId, comparison.operandType, comparison.type,
        HirValueCategory::Value, comparison.operation, comparison.sourceSpan.clone()});
    return;
  }

  // General path: optional local binding with optional initializer, N local
  // writes, return, and one of several return value forms. Node allocation
  // follows the same source-preorder stride as the former inline materializer.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(ast::NodeId());

  HirNodeId localId{};
  zc::Maybe<HirNodeId> initializerId;
  if (generic.local != zc::none) {
    localId = ctx.allocNode(ast::NodeId());
    ZC_IF_SOME(local, generic.local) {
      if (local.initializer != zc::none) { initializerId = ctx.allocNode(ast::NodeId()); }
    }
  }

  ZC_IF_SOME(aggregate, generic.aggregate) {
    ZC_IREQUIRE(initializerId != zc::none,
                "GenericShape aggregate requires a local initializer node");
    ZC_IF_SOME(identifier, initializerId) {
      ctx.addAggregate(HirNominalAggregateExpression{
          identifier, aggregate.definition, aggregate.type, zc::mv(aggregate.elements),
          aggregate.category, aggregate.sourceSpan.clone()});
    }
  }

  zc::Vector<HirNodeId> writeIds;
  zc::Vector<HirNodeId> writeValueIds;
  for (size_t index = 0; index < generic.localWrites.size(); ++index) {
    writeIds.add(ctx.allocNode(ast::NodeId()));
    writeValueIds.add(ctx.allocNode(ast::NodeId()));
  }

  const HirNodeId returnId = ctx.allocNode(ast::NodeId());
  const HirNodeId valueId = ctx.allocNode(ast::NodeId());

  zc::Maybe<HirNodeId> unsafeBlockId;
  if (function.unsafeBlockSpan != zc::none) {
    unsafeBlockId = ctx.allocNode(ast::NodeId());
    ctx.addUnsafeBlock(
        HirUnsafeBlockExpression{ZC_ASSERT_NONNULL(unsafeBlockId), valueId, function.resultType,
                                 ZC_ASSERT_NONNULL(function.unsafeBlockSpan).clone()});
  }

  // Binary write values reserve two trailing operand node ids each, in write
  // order, so a literal or parameter write's ids never shift.
  zc::Vector<zc::Maybe<HirNodeId>> writeBinaryLeftIds;
  zc::Vector<zc::Maybe<HirNodeId>> writeBinaryRightIds;
  for (size_t index = 0; index < generic.localWriteValues.size(); ++index) {
    zc::Maybe<HirNodeId> leftId;
    zc::Maybe<HirNodeId> rightId;
    if (generic.localWriteValues[index].binary != zc::none) {
      leftId = ctx.allocNode(ast::NodeId());
      rightId = ctx.allocNode(ast::NodeId());
    }
    writeBinaryLeftIds.add(zc::mv(leftId));
    writeBinaryRightIds.add(zc::mv(rightId));
  }

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId,
                                         zc::mv(unsafeBlockId), generic.returnsErrorUnwrapCall});

  zc::Vector<HirNodeId> statements;
  if (generic.local != zc::none) { statements.add(localId); }
  for (const auto writeId : writeIds) { statements.add(writeId); }
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});

  // A literal return value: when a local is present the literal initializes
  // the local (at the initializer node); otherwise it is the return value.
  ZC_IF_SOME(literal, generic.literal) {
    HirNodeId expressionId = valueId;
    identity::SemanticTypeId expressionType = function.resultType;
    identity::SourceSpan expressionSpan = function.valueSpan.clone();
    ZC_IF_SOME(local, generic.local) {
      ZC_IREQUIRE(initializerId != zc::none && local.initializerSpan != zc::none,
                  "GenericShape literal with local requires an initializer");
      ZC_IF_SOME(value, initializerId) { expressionId = value; }
      expressionType = local.type;
      ZC_IF_SOME(span, local.initializerSpan) { expressionSpan = span.clone(); }
    }
    ctx.addExpression(HirScalarLiteralExpression{expressionId, expressionType, literal.clone(),
                                                 HirValueCategory::Value, zc::mv(expressionSpan)});
  }

  ZC_IREQUIRE(generic.localWrites.size() == generic.localWriteValues.size() &&
                  writeIds.size() == generic.localWrites.size() &&
                  writeValueIds.size() == generic.localWrites.size(),
              "GenericShape local write/value size mismatch");

  for (size_t index = 0; index < generic.localWrites.size(); ++index) {
    const auto& write = generic.localWrites[index];
    const auto& writeValue = generic.localWriteValues[index];
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
      HirNodeId leftOperandId;
      HirNodeId rightOperandId;
      ZC_IF_SOME(id, writeBinaryLeftIds[index]) { leftOperandId = id; }
      ZC_IF_SOME(id, writeBinaryRightIds[index]) { rightOperandId = id; }
      auto materializeWriteArm = [&](HirNodeId armId, const PendingConditionalArm& arm) {
        ZC_IF_SOME(literal, arm.literal) {
          ctx.addExpression(HirScalarLiteralExpression{
              armId, arm.type, literal.clone(), HirValueCategory::Value, arm.sourceSpan.clone()});
        }
        ZC_IF_SOME(parameter, arm.parameter) {
          ctx.addParameterReference(
              HirParameterReferenceExpression{armId, parameter.parameter.clone(), arm.type,
                                              parameter.category, parameter.sourceSpan.clone()});
        }
        ZC_IF_SOME(local, arm.local) {
          ctx.addLocalReference(HirLocalReferenceExpression{
              armId, local.local, local.type, local.category, local.sourceSpan.clone()});
        }
      };
      materializeWriteArm(leftOperandId, binary.left);
      materializeWriteArm(rightOperandId, binary.right);
      ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
          writeValueIds[index], leftOperandId, rightOperandId, binary.operandType, binary.type,
          HirValueCategory::Value, binary.operation, binary.sourceSpan.clone()});
    }
  }

  ZC_IF_SOME(local, generic.local) {
    zc::Maybe<HirNodeId> initializer;
    zc::Maybe<identity::SourceSpan> initializerSpan;
    ZC_IF_SOME(value, initializerId) { initializer = value; }
    ZC_IF_SOME(span, local.initializerSpan) { initializerSpan = span.clone(); }
    ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), local.type, zc::mv(initializer),
                                 local.sourceSpan.clone(), zc::mv(initializerSpan),
                                 zc::mv(local.erasure)});
  }

  for (size_t index = 0; index < generic.localWrites.size(); ++index) {
    const auto& write = generic.localWrites[index];
    ctx.addLocalWrite(HirLocalWriteStatement{writeIds[index], hirLocalId(1), write.field,
                                             write.type, writeValueIds[index], write.kind,
                                             write.sourceSpan.clone(), write.valueSpan.clone()});
  }

  ZC_IF_SOME(reference, generic.localReference) {
    ctx.addLocalReference(HirLocalReferenceExpression{
        valueId, hirLocalId(1), reference.type, reference.category, reference.sourceSpan.clone()});
  }
  ZC_IF_SOME(projection, generic.localFieldProjection) {
    ctx.addLocalFieldProjection(HirLocalFieldProjectionExpression{
        valueId, hirLocalId(1), projection.field, projection.receiverType, projection.type,
        projection.category, projection.sourceSpan.clone()});
  }
  ZC_IF_SOME(reference, generic.parameterReference) {
    HirNodeId referenceId = valueId;
    ZC_IF_SOME(initializer, initializerId) { referenceId = initializer; }
    ctx.addParameterReference(
        HirParameterReferenceExpression{referenceId, reference.parameter.clone(), reference.type,
                                        reference.category, reference.sourceSpan.clone()});
  }
  ZC_IF_SOME(index, generic.parameterIndex) {
    ctx.addParameterIndex(HirParameterIndexExpression{
        valueId, index.parameter.clone(), index.receiverType, index.indexType, index.index.clone(),
        index.type, index.category, index.sourceSpan.clone(), index.indexSpan.clone()});
  }
  ZC_IF_SOME(reborrow, generic.parameterReborrow) {
    zc::Maybe<HirLocalId> sourceAlias;
    if (reborrow.sourceAlias != zc::none) { sourceAlias = hirLocalId(1); }
    ctx.addParameterReborrow(HirParameterReborrowExpression{
        valueId, reborrow.parameter.clone(), zc::mv(sourceAlias), reborrow.sourceType,
        reborrow.type, reborrow.mutability, reborrow.sourceSpan.clone()});
  }
  ZC_IF_SOME(borrow, generic.localBorrow) {
    ctx.addLocalBorrow(HirLocalBorrowExpression{valueId, hirLocalId(1), borrow.sourceType,
                                                borrow.type, borrow.mutability,
                                                borrow.sourceSpan.clone()});
  }
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
