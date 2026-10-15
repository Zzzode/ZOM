// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-expr-binary.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerComparisonReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const auto& comparison = function.shape.get<ComparisonReturn>().comparisonReturn;
  const identity::SemanticTypeId operandType = comparison.operandType;
  const identity::SemanticTypeId resultType = comparison.type;
  const checker::PrimitiveOperation operation = comparison.operation;
  const identity::SourceSpan binarySpan = comparison.sourceSpan.clone();

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, left F+2, right F+3, binary F+4, return F+5. The
  // return takes the binary node directly (no result temporary).
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId leftId = ctx.allocNode(comparison.left.sourceNode);
  const HirNodeId rightId = ctx.allocNode(comparison.right.sourceNode);
  const HirNodeId binaryId = ctx.allocNode(comparison.sourceNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.lowerArmLeaf(leftId, comparison.left);
  ctx.lowerArmLeaf(rightId, comparison.right);
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      binaryId, leftId, rightId, operandType, resultType, HirValueCategory::Value, operation,
      binarySpan.clone(), comparison.isUnaryDesugar});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, binaryId, function.returnSpan.clone()});
}

void lowerPrimitiveUnaryReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  auto& shape = function.shape.get<PrimitiveUnaryReturn>();
  const auto& comparison = shape.comparisonReturn;
  const identity::SemanticTypeId operandType = comparison.operandType;
  const identity::SemanticTypeId resultType = comparison.type;
  const checker::PrimitiveOperation operation = comparison.operation;
  const identity::SourceSpan binarySpan = comparison.sourceSpan.clone();
  const identity::SemanticTypeId localType = shape.local.type;
  const identity::SourceSpan localSpan = shape.local.sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(shape.local.initializerSpan).clone();

  // Fixed source-preorder stride: function F, body F+1, local F+2,
  // initializer F+3, left F+4, right F+5, binary F+6, return F+7. The block
  // lists [local, return]; the return takes the binary node directly.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId initializerId = ctx.allocNode(ast::NodeId());
  const HirNodeId leftId = ctx.allocNode(comparison.left.sourceNode);
  const HirNodeId rightId = ctx.allocNode(comparison.right.sourceNode);
  const HirNodeId binaryId = ctx.allocNode(comparison.sourceNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  PendingConditionalArm initializerArm{zc::mv(shape.literal),   zc::none, zc::none,     localType,
                                       initializerSpan.clone(), zc::none, ast::NodeId()};
  ctx.lowerArmLeaf(initializerId, initializerArm);
  ctx.lowerArmLeaf(leftId, comparison.left);
  ctx.lowerArmLeaf(rightId, comparison.right);
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{
      binaryId, leftId, rightId, operandType, resultType, HirValueCategory::Value, operation,
      binarySpan.clone(), comparison.isUnaryDesugar});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, binaryId, function.returnSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
