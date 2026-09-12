// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-stmt-write.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerLocalWriteFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::SemanticTypeId placeType = ZC_ASSERT_NONNULL(function.localReference).type;
  const HirValueCategory placeCategory = ZC_ASSERT_NONNULL(function.localReference).category;
  const identity::SourceSpan placeSpan =
      ZC_ASSERT_NONNULL(function.localReference).sourceSpan.clone();
  const size_t writeCount = function.localWrites.size();

  // Source-preorder stride matching the generic materializer: function, body,
  // local, initializer leaf, then per write (write node, write value node),
  // then return and returned value. A binary write reserves two trailing
  // operand node ids after the return value (in write order); the write's value
  // node is the binary node. No receiver/unsafe/loop ids in this family.
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

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), function.visibility.clone(),
                                         function.linkage, function.declarationSpan.clone(), bodyId,
                                         zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  for (const auto writeId : writeIds) { statements.add(writeId); }
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});

  // Initializer leaf (literal or parameter) at the initializer node.
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
