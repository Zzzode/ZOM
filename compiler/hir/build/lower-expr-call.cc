// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-expr-call.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerDirectCallReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  HirDirectCallExpression call = zc::mv(ZC_ASSERT_NONNULL(function.call));

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, return F+2, call F+3. The call node is the return
  // value destination.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId callId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::none,
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addDirectCall(HirDirectCallExpression{callId, call.callee, call.calleeType, call.resultType,
                                            zc::mv(call.arguments), call.sourceSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, callId, function.returnSpan.clone()});
}

void lowerDirectCallInitializerFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  HirDirectCallExpression call = zc::mv(ZC_ASSERT_NONNULL(function.call));
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::SemanticTypeId placeType = ZC_ASSERT_NONNULL(function.localReference).type;
  const HirValueCategory placeCategory = ZC_ASSERT_NONNULL(function.localReference).category;
  const identity::SourceSpan placeSpan =
      ZC_ASSERT_NONNULL(function.localReference).sourceSpan.clone();

  // Fixed source-preorder stride matching the generic materializer: function F,
  // body F+1, local F+2, call initializer F+3, return F+4, local reference F+5.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();

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
  ctx.addDirectCall(HirDirectCallExpression{initializerId, call.callee, call.calleeType,
                                            call.resultType, zc::mv(call.arguments),
                                            call.sourceSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
  ctx.addLocalReference(HirLocalReferenceExpression{valueId, hirLocalId(1), placeType,
                                                    placeCategory, placeSpan.clone()});
}

void lowerReceiverCallFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  HirReceiverCallExpression call = zc::mv(ZC_ASSERT_NONNULL(function.receiverCall));
  HirNominalAggregateExpression aggregate = zc::mv(ZC_ASSERT_NONNULL(function.aggregate));
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::SemanticTypeId receiverType = ZC_ASSERT_NONNULL(function.localReference).type;
  const HirValueCategory receiverCategory = ZC_ASSERT_NONNULL(function.localReference).category;
  const identity::SourceSpan receiverSpan =
      ZC_ASSERT_NONNULL(function.localReference).sourceSpan.clone();

  // Fixed source-preorder stride matching the generic materializer: function F,
  // body F+1, local F+2, aggregate initializer F+3, return F+4, receiver
  // reference F+5, receiver call F+6. The receiver call is the return value.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId receiverId = ctx.allocNode();
  const HirNodeId callId = ctx.allocNode();

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
  ctx.addAggregate(HirNominalAggregateExpression{initializerId, aggregate.definition,
                                                 aggregate.type, zc::mv(aggregate.elements),
                                                 aggregate.category, aggregate.sourceSpan.clone()});
  ctx.addLocalReference(HirLocalReferenceExpression{receiverId, hirLocalId(1), receiverType,
                                                    receiverCategory, receiverSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, callId, function.returnSpan.clone()});
  ctx.addReceiverCall(HirReceiverCallExpression{
      callId, receiverId, call.callee, call.calleeType, call.receiverSourceType, call.receiverType,
      call.receiverMode, zc::mv(call.receiverAdjustments), call.resultType, zc::mv(call.arguments),
      call.sourceSpan.clone()});
}

void lowerReceiverSelfCallFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  HirReceiverCallExpression call = zc::mv(ZC_ASSERT_NONNULL(function.receiverSelfCall));

  // Fixed source-preorder stride matching the generic materializer: function F,
  // body F+1, return F+2, self call F+3. The implicit receiver is a function
  // header parameter, so it allocates no body node: the call's receiver slot is
  // unset and MIR forwards the leading receiver parameter local directly.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId callId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, callId, function.returnSpan.clone()});
  ctx.addReceiverCall(HirReceiverCallExpression{
      callId, HirNodeId(), call.callee, call.calleeType, call.receiverSourceType, call.receiverType,
      call.receiverMode, zc::mv(call.receiverAdjustments), call.resultType, zc::mv(call.arguments),
      call.sourceSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
