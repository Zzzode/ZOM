// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-expr-aggregate.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerAggregateFieldProjectionFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  auto& shape = function.shape.get<AggregateFieldProjectionReturn>();
  const identity::SemanticTypeId localType = shape.local.type;
  const identity::SourceSpan localSpan = shape.local.sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(shape.local.initializerSpan).clone();
  const identity::DefId aggregateDefinition = shape.aggregate.definition;
  const identity::SemanticTypeId aggregateType = shape.aggregate.type;
  const HirValueCategory aggregateCategory = shape.aggregate.category;
  const identity::SourceSpan aggregateSpan = shape.aggregate.sourceSpan.clone();
  zc::Vector<HirNominalAggregateElement> aggregateElements = zc::mv(shape.aggregate.elements);
  const identity::DefId projectionField = shape.localFieldProjection.field;
  const identity::SemanticTypeId projectionReceiverType = shape.localFieldProjection.receiverType;
  const identity::SemanticTypeId projectionType = shape.localFieldProjection.type;
  const HirValueCategory projectionCategory = shape.localFieldProjection.category;
  const identity::SourceSpan projectionSpan = shape.localFieldProjection.sourceSpan.clone();

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, local F+2, aggregate initializer F+3, return F+4,
  // field projection F+5. The return value is the projection node.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId initializerId = ctx.allocNode(ast::NodeId());
  const HirNodeId returnId = ctx.allocNode(function.returnNode);
  const HirNodeId valueId = ctx.allocNode(function.returnValueNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  ctx.addAggregate(HirNominalAggregateExpression{initializerId, aggregateDefinition, aggregateType,
                                                 zc::mv(aggregateElements), aggregateCategory,
                                                 aggregateSpan.clone()});
  ctx.addLocalFieldProjection(HirLocalFieldProjectionExpression{
      valueId, hirLocalId(1), projectionField, projectionReceiverType, projectionType,
      projectionCategory, projectionSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
}

void lowerLocalFieldBinaryFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  auto& shape = function.shape.get<LocalFieldBinaryReturn>();
  const identity::SemanticTypeId aggregateType = shape.local.type;
  const identity::SourceSpan localSpan = shape.local.sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(shape.local.initializerSpan).clone();
  const identity::DefId aggregateDefinition = shape.aggregate.definition;
  const identity::SourceSpan aggregateSpan = shape.aggregate.sourceSpan.clone();
  zc::Vector<HirNominalAggregateElement> aggregateElements = zc::mv(shape.aggregate.elements);
  const identity::DefId leftField = shape.leftProjection.field;
  const identity::DefId rightField = shape.rightProjection.field;
  const identity::SemanticTypeId operandType = shape.operandType;
  const identity::SemanticTypeId resultType = shape.resultType;
  const checker::PrimitiveOperation operation = shape.operation;
  const identity::SourceSpan binarySpan = shape.binarySpan.clone();
  const ast::NodeId leftNode = shape.leftNode;
  const ast::NodeId rightNode = shape.rightNode;
  const ast::NodeId binaryNode = shape.binaryNode;

  // Fixed source-preorder stride matching the verifier:
  // function F, body F+1, local F+2, aggregate initializer F+3,
  // left projection F+4, right projection F+5, binary F+6, return F+7.
  // The return takes the binary node directly (no result temporary).
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId initializerId = ctx.allocNode(ast::NodeId());
  const HirNodeId leftId = ctx.allocNode(leftNode);
  const HirNodeId rightId = ctx.allocNode(rightNode);
  const HirNodeId binaryId = ctx.allocNode(binaryNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), aggregateType, initializerId,
                               localSpan.clone(), initializerSpan.clone()});
  ctx.addAggregate(HirNominalAggregateExpression{initializerId, aggregateDefinition, aggregateType,
                                                 zc::mv(aggregateElements), HirValueCategory::Value,
                                                 aggregateSpan.clone()});
  ctx.addLocalFieldProjection(HirLocalFieldProjectionExpression{
      leftId, hirLocalId(1), leftField, aggregateType, operandType, HirValueCategory::Place,
      shape.leftProjection.sourceSpan.clone()});
  ctx.addLocalFieldProjection(HirLocalFieldProjectionExpression{
      rightId, hirLocalId(1), rightField, aggregateType, operandType, HirValueCategory::Place,
      shape.rightProjection.sourceSpan.clone()});
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{binaryId, leftId, rightId, operandType,
                                                      resultType, HirValueCategory::Value,
                                                      operation, binarySpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, binaryId, function.returnSpan.clone()});
}

void lowerUninitializedFieldProjectionReturnFunction(PendingFunctionDeclaration&& function,
                                                     HirFnCtx& ctx) {
  const auto& shape = function.shape.get<UninitializedFieldProjectionReturn>();
  // Source-preorder stride: function, body, local, return, value. No
  // initializer node — the binding is uninitialized.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId returnId = ctx.allocNode(function.returnNode);
  const HirNodeId valueId = ctx.allocNode(function.returnValueNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), shape.local.type, zc::none,
                               shape.local.sourceSpan.clone(), zc::none});
  ctx.addLocalFieldProjection(HirLocalFieldProjectionExpression{
      valueId, hirLocalId(1), shape.localFieldProjection.field,
      shape.localFieldProjection.receiverType, shape.localFieldProjection.type,
      shape.localFieldProjection.category, shape.localFieldProjection.sourceSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
}

void lowerEnumConstructionReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  // Both EnumConstructionReturn and AggregateReturn carry the aggregate field.
  HirNominalAggregateExpression aggregate = [&]() {
    if (function.shape.is<EnumConstructionReturn>()) {
      return zc::mv(function.shape.get<EnumConstructionReturn>().aggregate);
    }
    return zc::mv(function.shape.get<AggregateReturn>().aggregate);
  }();
  const identity::DefId aggregateDefinition = aggregate.definition;
  const identity::SemanticTypeId aggregateType = aggregate.type;
  const HirValueCategory aggregateCategory = aggregate.category;
  const identity::SourceSpan aggregateSpan = aggregate.sourceSpan.clone();
  zc::Vector<HirNominalAggregateElement> aggregateElements = zc::mv(aggregate.elements);

  // Fixed source-preorder stride matching the verifier's non-local return
  // layout: function F, body F+1, return F+2, value F+3. The aggregate is
  // the return value expression; there is no intermediate local.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);
  const HirNodeId aggregateId = ctx.allocNode(ast::NodeId());

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, aggregateId, function.returnSpan.clone()});
  ctx.addAggregate(HirNominalAggregateExpression{aggregateId, aggregateDefinition, aggregateType,
                                                 zc::mv(aggregateElements), aggregateCategory,
                                                 aggregateSpan.clone()});
}

void lowerExistentialReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  auto& shape = function.shape.get<ExistentialReturn>();
  const identity::DefId aggregateDefinition = shape.aggregate.definition;
  const identity::SemanticTypeId aggregateType = shape.aggregate.type;
  const HirValueCategory aggregateCategory = shape.aggregate.category;
  const identity::SourceSpan aggregateSpan = shape.aggregate.sourceSpan.clone();
  zc::Vector<HirNominalAggregateElement> aggregateElements = zc::mv(shape.aggregate.elements);
  zc::Maybe<HirErasureRecord> erasure = zc::mv(shape.returnErasure);

  // Fixed source-preorder stride matching the verifier's non-local return
  // layout: function F, body F+1, return F+2, value F+3. The aggregate is
  // the return value expression; there is no intermediate local.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);
  const HirNodeId aggregateId = ctx.allocNode(ast::NodeId());

  ctx.addFunction(HirFunctionDeclaration{
      functionId, function.definition, function.resultType, zc::mv(function.parameters), zc::none,
      function.visibility.clone(), function.linkage, function.declarationSpan.clone(), bodyId,
      zc::none, false, zc::none, zc::none, zc::mv(erasure)});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, aggregateId, function.returnSpan.clone()});
  ctx.addAggregate(HirNominalAggregateExpression{aggregateId, aggregateDefinition, aggregateType,
                                                 zc::mv(aggregateElements), aggregateCategory,
                                                 aggregateSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
