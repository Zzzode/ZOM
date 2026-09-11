// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-expr-binary.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerComparisonReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const auto& comparison = ZC_ASSERT_NONNULL(function.comparisonReturn);
  const identity::SemanticTypeId operandType = comparison.operandType;
  const identity::SemanticTypeId resultType = comparison.type;
  const checker::PrimitiveOperation operation = comparison.operation;
  const identity::SourceSpan binarySpan = comparison.sourceSpan.clone();

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, left F+2, right F+3, binary F+4, return F+5. The
  // return takes the binary node directly (no result temporary).
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId leftId = ctx.allocNode();
  const HirNodeId rightId = ctx.allocNode();
  const HirNodeId binaryId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), function.visibility.clone(),
                                         function.linkage, function.declarationSpan.clone(), bodyId,
                                         zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.lowerArmLeaf(leftId, comparison.left);
  ctx.lowerArmLeaf(rightId, comparison.right);
  ctx.addPrimitiveBinary(HirPrimitiveBinaryExpression{binaryId, leftId, rightId, operandType,
                                                      resultType, HirValueCategory::Value,
                                                      operation, binarySpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, binaryId, function.returnSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
