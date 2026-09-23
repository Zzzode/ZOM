// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/lower-expr-borrow.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {
namespace {

/// \brief Allocates the trailing unsafe-block destination (when the return is
/// wrapped in one admitted unsafe block) and appends its record wrapping the
/// inner return value. Returns the unsafe node id, or none.
zc::Maybe<HirNodeId> lowerTrailingUnsafeBlock(PendingFunctionDeclaration& function,
                                              HirNodeId valueId, HirFnCtx& ctx) {
  if (function.unsafeBlockSpan == zc::none) { return zc::none; }
  const HirNodeId unsafeId = ctx.allocNode();
  ctx.addUnsafeBlock(HirUnsafeBlockExpression{unsafeId, valueId, function.resultType,
                                              ZC_ASSERT_NONNULL(function.unsafeBlockSpan).clone()});
  return unsafeId;
}

}  // namespace

void lowerParameterReborrowReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  HirParameterReborrowExpression reborrow = zc::mv(ZC_ASSERT_NONNULL(function.parameterReborrow));
  // The pending record carries a default local id for an alias source; the
  // materializer maps it to the sole user local ordinal.
  zc::Maybe<HirLocalId> sourceAlias;
  if (reborrow.sourceAlias != zc::none) { sourceAlias = hirLocalId(1); }

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, return F+2, reborrow value F+3, and a trailing
  // unsafe block F+4 when the return is wrapped in `unsafe { ... }`.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();
  zc::Maybe<HirNodeId> unsafeBlockId = lowerTrailingUnsafeBlock(function, valueId, ctx);

  ctx.addFunction(HirFunctionDeclaration{
      functionId, function.definition, function.resultType, zc::mv(function.parameters), zc::none,
      function.visibility.clone(), function.linkage, function.declarationSpan.clone(), bodyId,
      zc::mv(unsafeBlockId)});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
  ctx.addParameterReborrow(HirParameterReborrowExpression{
      valueId, zc::mv(reborrow.parameter), zc::mv(sourceAlias), reborrow.sourceType, reborrow.type,
      reborrow.mutability, reborrow.sourceSpan.clone()});
}

void lowerUnsafeScalarReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  checker::checked::CanonicalConstValue literal = zc::mv(ZC_ASSERT_NONNULL(function.literal));

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, return F+2, scalar value F+3, trailing unsafe block
  // F+4 whose body is the scalar value.
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();
  zc::Maybe<HirNodeId> unsafeBlockId = lowerTrailingUnsafeBlock(function, valueId, ctx);

  ctx.addFunction(HirFunctionDeclaration{
      functionId, function.definition, function.resultType, zc::mv(function.parameters), zc::none,
      function.visibility.clone(), function.linkage, function.declarationSpan.clone(), bodyId,
      zc::mv(unsafeBlockId)});
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
  ctx.addExpression(HirScalarLiteralExpression{valueId, function.resultType, zc::mv(literal),
                                               HirValueCategory::Value,
                                               function.valueSpan.clone()});
}

void lowerLocalAliasReborrowReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  HirParameterReferenceExpression initializer =
      zc::mv(ZC_ASSERT_NONNULL(function.parameterReference));
  HirParameterReborrowExpression reborrow = zc::mv(ZC_ASSERT_NONNULL(function.parameterReborrow));
  // The local-alias reborrow always carries the sole user local; the pending
  // default id maps to local ordinal 1.
  zc::Maybe<HirLocalId> sourceAlias;
  if (reborrow.sourceAlias != zc::none) { sourceAlias = hirLocalId(1); }

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, local F+2, parameter-reference initializer F+3,
  // return F+4, reborrow value F+5, and a trailing unsafe block F+6 when
  // wrapped. The block lists [local, return].
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();
  zc::Maybe<HirNodeId> unsafeBlockId = lowerTrailingUnsafeBlock(function, valueId, ctx);

  ctx.addFunction(HirFunctionDeclaration{
      functionId, function.definition, function.resultType, zc::mv(function.parameters), zc::none,
      function.visibility.clone(), function.linkage, function.declarationSpan.clone(), bodyId,
      zc::mv(unsafeBlockId)});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  ctx.addParameterReference(HirParameterReferenceExpression{
      initializerId, zc::mv(initializer.parameter), initializer.type, initializer.category,
      initializer.sourceSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
  ctx.addParameterReborrow(HirParameterReborrowExpression{
      valueId, zc::mv(reborrow.parameter), zc::mv(sourceAlias), reborrow.sourceType, reborrow.type,
      reborrow.mutability, reborrow.sourceSpan.clone()});
}

void lowerLocalBorrowReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  HirLocalBorrowExpression borrow = zc::mv(ZC_ASSERT_NONNULL(function.localBorrow));

  // Fixed source-preorder stride matching the generic materializer:
  // function F, body F+1, local F+2, initializer F+3, return F+4, local-borrow
  // value F+5, and a trailing unsafe block F+6 when wrapped. The initializer is
  // a scalar literal or a parameter reference; the block lists [local, return].
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();
  zc::Maybe<HirNodeId> unsafeBlockId = lowerTrailingUnsafeBlock(function, valueId, ctx);

  ctx.addFunction(HirFunctionDeclaration{
      functionId, function.definition, function.resultType, zc::mv(function.parameters), zc::none,
      function.visibility.clone(), function.linkage, function.declarationSpan.clone(), bodyId,
      zc::mv(unsafeBlockId)});
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  if (function.literal != zc::none) {
    ctx.addExpression(HirScalarLiteralExpression{initializerId, localType,
                                                 zc::mv(ZC_ASSERT_NONNULL(function.literal)),
                                                 HirValueCategory::Value, initializerSpan.clone()});
  } else {
    HirParameterReferenceExpression initializer =
        zc::mv(ZC_ASSERT_NONNULL(function.parameterReference));
    ctx.addParameterReference(HirParameterReferenceExpression{
        initializerId, zc::mv(initializer.parameter), initializer.type, initializer.category,
        initializer.sourceSpan.clone()});
  }
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
  ctx.addLocalBorrow(HirLocalBorrowExpression{valueId, hirLocalId(1), borrow.sourceType,
                                              borrow.type, borrow.mutability,
                                              borrow.sourceSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
