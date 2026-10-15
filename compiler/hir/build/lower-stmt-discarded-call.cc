// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-stmt-discarded-call.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerVoidReceiverFieldWriteFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const auto& shape = function.shape.get<VoidMutatingMethod>();
  const auto& write = shape.parameterFieldWrite;
  const auto& parameter = shape.parameterFieldWriteParameter;

  // Fixed source-preorder stride: function F, body F+1, write statement F+2,
  // write value parameter reference F+3. The block lists only the write; no
  // return is materialized for the Unit result.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId writeId = ctx.allocNode(ast::NodeId());
  const HirNodeId valueId = ctx.allocNode(ast::NodeId());

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

void lowerEmptyBodyVoidFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  // Fixed source-preorder stride: function F, body F+1. The block lists no
  // statements; no return is materialized for the Unit result.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  zc::Vector<HirNodeId> statements;
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
}

void lowerVoidLetBodyFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const auto& bindings = function.shape.get<VoidLetBody>().voidLetBindings;
  const size_t count = bindings.size();

  // Fixed source-preorder stride: function F, body F+1, then N x (local,
  // literal). The block lists the locals; no return is materialized for the
  // Unit result. The dead locals are folded away by the LIR builder.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  zc::Vector<HirNodeId> statementIds;
  zc::Vector<HirNodeId> literalIds;
  for (size_t i = 0; i < count; ++i) {
    statementIds.add(ctx.allocNode(ast::NodeId()));
    literalIds.add(ctx.allocNode(ast::NodeId()));
  }

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), zc::mv(function.receiver),
                                         function.visibility.clone(), function.linkage,
                                         function.declarationSpan.clone(), bodyId, zc::none});
  for (size_t i = 0; i < count; ++i) {
    const auto& binding = bindings[i];
    const identity::SourceSpan& initializerSpan = ZC_ASSERT_NONNULL(binding.local.initializerSpan);
    ctx.addLocal(HirLocalBinding{statementIds[i], hirLocalId(static_cast<uint32_t>(i + 1)),
                                 binding.local.type, literalIds[i],
                                 binding.local.sourceSpan.clone(), initializerSpan.clone()});
    ctx.addExpression(HirScalarLiteralExpression{literalIds[i], binding.local.type,
                                                 binding.literal.clone(), HirValueCategory::Value,
                                                 initializerSpan.clone()});
  }
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statementIds), function.bodySpan.clone()});
}

void lowerDiscardedReceiverCallFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  auto& shape = function.shape.get<DiscardedReceiverCall>();
  HirReceiverCallExpression setCall = zc::mv(shape.statementReceiverCall);
  HirLocalReferenceExpression setReceiver = zc::mv(shape.statementReceiverReference);
  HirReceiverCallExpression getCall = zc::mv(shape.receiverCall);
  HirNominalAggregateExpression aggregate = zc::mv(shape.aggregate);
  const identity::SemanticTypeId localType = shape.local.type;
  const identity::SourceSpan localSpan = shape.local.sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(shape.local.initializerSpan).clone();
  const identity::SemanticTypeId receiverType = shape.localReference.type;
  const HirValueCategory receiverCategory = shape.localReference.category;
  const identity::SourceSpan receiverSpan = shape.localReference.sourceSpan.clone();

  // Fixed source-preorder stride over the three statements: function F, body
  // F+1, local F+2, aggregate initializer F+3, set receiver reference F+4, set
  // call F+5, get receiver reference F+6, get call F+7, return F+8. The block
  // lists the local, the set call node (statement position), and the return.
  const HirNodeId functionId = ctx.allocNode(function.sourceNode);
  const HirNodeId bodyId = ctx.allocNode(function.bodyNode);
  const HirNodeId localId = ctx.allocNode(ast::NodeId());
  const HirNodeId initializerId = ctx.allocNode(ast::NodeId());
  const HirNodeId setReceiverId = ctx.allocNode(ast::NodeId());
  const HirNodeId setCallId = ctx.allocNode(ast::NodeId());
  const HirNodeId getReceiverId = ctx.allocNode(ast::NodeId());
  const HirNodeId getCallId = ctx.allocNode(function.returnValueNode);
  const HirNodeId returnId = ctx.allocNode(function.returnNode);

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
      setCall.resultType, zc::mv(setCall.arguments), setCall.sourceSpan.clone(),
      zc::mv(setCall.interface)});
  ctx.addLocalReference(HirLocalReferenceExpression{getReceiverId, hirLocalId(1), receiverType,
                                                    receiverCategory, receiverSpan.clone()});
  ctx.addReceiverCall(HirReceiverCallExpression{
      getCallId, getReceiverId, getCall.callee, getCall.calleeType, getCall.receiverSourceType,
      getCall.receiverType, getCall.receiverMode, zc::mv(getCall.receiverAdjustments),
      getCall.resultType, zc::mv(getCall.arguments), getCall.sourceSpan.clone(),
      zc::mv(getCall.interface)});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, getCallId, function.returnSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
