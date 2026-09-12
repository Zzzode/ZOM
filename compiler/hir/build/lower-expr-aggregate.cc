// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-expr-aggregate.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

void lowerAggregateFieldProjectionFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::DefId aggregateDefinition = ZC_ASSERT_NONNULL(function.aggregate).definition;
  const identity::SemanticTypeId aggregateType = ZC_ASSERT_NONNULL(function.aggregate).type;
  const HirValueCategory aggregateCategory = ZC_ASSERT_NONNULL(function.aggregate).category;
  const identity::SourceSpan aggregateSpan =
      ZC_ASSERT_NONNULL(function.aggregate).sourceSpan.clone();
  zc::Vector<HirNominalAggregateElement> aggregateElements =
      zc::mv(ZC_ASSERT_NONNULL(function.aggregate).elements);
  const identity::DefId projectionField = ZC_ASSERT_NONNULL(function.localFieldProjection).field;
  const identity::SemanticTypeId projectionReceiverType =
      ZC_ASSERT_NONNULL(function.localFieldProjection).receiverType;
  const identity::SemanticTypeId projectionType =
      ZC_ASSERT_NONNULL(function.localFieldProjection).type;
  const HirValueCategory projectionCategory =
      ZC_ASSERT_NONNULL(function.localFieldProjection).category;
  const identity::SourceSpan projectionSpan =
      ZC_ASSERT_NONNULL(function.localFieldProjection).sourceSpan.clone();

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, local F+2, aggregate initializer F+3, return F+4,
  // field projection F+5. The return value is the projection node.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();

  ctx.addFunction(HirFunctionDeclaration{functionId, function.definition, function.resultType,
                                         zc::mv(function.parameters), function.visibility.clone(),
                                         function.linkage, function.declarationSpan.clone(), bodyId,
                                         zc::none});
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

}  // namespace detail
}  // namespace zomlang::compiler::hir
