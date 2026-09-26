// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-stmt-discarded-call.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerVoidReceiverFieldWriteFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const auto& write = ZC_ASSERT_NONNULL(function.parameterFieldWrite);
  const auto& parameter = ZC_ASSERT_NONNULL(function.parameterFieldWriteParameter);

  // Fixed source-preorder stride: function F, body F+1, write statement F+2,
  // write value parameter reference F+3. The block lists only the write; no
  // return is materialized for the Unit result.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId writeId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(writeId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addParameterFieldWrite(HirParameterFieldWriteStatement{
      writeId, write.parameter.clone(), write.receiverType, write.field, write.type, valueId,
      write.sourceSpan.clone(), write.valueSpan.clone()});
  ctx.addParameterReference(HirParameterReferenceExpression{valueId, parameter.parameter.clone(),
                                                            parameter.type, HirValueCategory::Place,
                                                            parameter.sourceSpan.clone()});
}

void lowerDiscardedReceiverCallFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  HirReceiverCallExpression setCall = zc::mv(ZC_ASSERT_NONNULL(function.statementReceiverCall));
  HirLocalReferenceExpression setReceiver =
      zc::mv(ZC_ASSERT_NONNULL(function.statementReceiverReference));
  HirReceiverCallExpression getCall = zc::mv(ZC_ASSERT_NONNULL(function.receiverCall));
  HirNominalAggregateExpression aggregate = zc::mv(ZC_ASSERT_NONNULL(function.aggregate));
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::SemanticTypeId receiverType = ZC_ASSERT_NONNULL(function.localReference).type;
  const HirValueCategory receiverCategory = ZC_ASSERT_NONNULL(function.localReference).category;
  const identity::SourceSpan receiverSpan =
      ZC_ASSERT_NONNULL(function.localReference).sourceSpan.clone();

  // Fixed source-preorder stride over the three statements: function F, body
  // F+1, local F+2, aggregate initializer F+3, set receiver reference F+4, set
  // call F+5, get receiver reference F+6, get call F+7, return F+8. The block
  // lists the local, the set call node (statement position), and the return.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId setReceiverId = ctx.allocNode();
  const HirNodeId setCallId = ctx.allocNode();
  const HirNodeId getReceiverId = ctx.allocNode();
  const HirNodeId getCallId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(setCallId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  ctx.addAggregate(HirNominalAggregateExpression{initializerId, aggregate.definition,
                                                 aggregate.type, zc::mv(aggregate.elements),
                                                 aggregate.category, aggregate.sourceSpan.clone()});
  ctx.addLocalReference(HirLocalReferenceExpression{setReceiverId, hirLocalId(1), localType,
                                                    HirValueCategory::Place,
                                                    setReceiver.sourceSpan.clone()});
  ctx.addReceiverCall(HirReceiverCallExpression{
      setCallId, setReceiverId, setCall.callee, setCall.calleeType, setCall.receiverSourceType,
      setCall.receiverType, setCall.receiverMode, zc::mv(setCall.receiverAdjustments),
      setCall.resultType, zc::mv(setCall.arguments), setCall.sourceSpan.clone()});
  ctx.addLocalReference(HirLocalReferenceExpression{getReceiverId, hirLocalId(1), receiverType,
                                                    receiverCategory, receiverSpan.clone()});
  ctx.addReceiverCall(HirReceiverCallExpression{
      getCallId, getReceiverId, getCall.callee, getCall.calleeType, getCall.receiverSourceType,
      getCall.receiverType, getCall.receiverMode, zc::mv(getCall.receiverAdjustments),
      getCall.resultType, zc::mv(getCall.arguments), getCall.sourceSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, getCallId, function.returnSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
