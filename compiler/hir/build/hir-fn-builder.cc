// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/hir/build/hir-fn-builder.h"

#include "compiler/hir/hir-internal.h"

namespace zomlang::compiler::hir {
namespace detail {

HirFnCtx::HirFnCtx(uint32_t& nextNode, zc::Vector<HirFunctionDeclaration>& functions,
                   zc::Vector<HirBlockStatement>& blocks, zc::Vector<HirReturnStatement>& returns,
                   zc::Vector<HirScalarLiteralExpression>& expressions,
                   zc::Vector<HirParameterReferenceExpression>& parameterReferences,
                   zc::Vector<HirLocalBinding>& locals,
                   zc::Vector<HirLocalReferenceExpression>& localReferences) noexcept
    : nextNode(&nextNode),
      functions(&functions),
      blocks(&blocks),
      returns(&returns),
      expressions(&expressions),
      parameterReferences(&parameterReferences),
      locals(&locals),
      localReferences(&localReferences) {}

HirNodeId HirFnCtx::allocNode() {
  HirNodeId id = hirId(*nextNode);
  ++(*nextNode);
  return id;
}

void HirFnCtx::addFunction(HirFunctionDeclaration declaration) {
  functions->add(zc::mv(declaration));
}

void HirFnCtx::addBlock(HirBlockStatement block) { blocks->add(zc::mv(block)); }

void HirFnCtx::addReturn(HirReturnStatement statement) { returns->add(zc::mv(statement)); }

void HirFnCtx::addExpression(HirScalarLiteralExpression expression) {
  expressions->add(zc::mv(expression));
}

void HirFnCtx::addParameterReference(HirParameterReferenceExpression reference) {
  parameterReferences->add(zc::mv(reference));
}

void HirFnCtx::addLocal(HirLocalBinding local) { locals->add(zc::mv(local)); }

void HirFnCtx::addLocalReference(HirLocalReferenceExpression reference) {
  localReferences->add(zc::mv(reference));
}

namespace {

/// \brief Lowers one scalar leaf into the supplied destination node id.
///
/// Destination-driven arm dispatch over the admitted scalar leaf family
/// (RFC 0048 family 1): a scalar literal constant or a function-parameter place
/// reference. The destination id is allocated by the enclosing arm, so the leaf
/// lowers "into" that destination like a rustc `intoDest` lowering. The literal
/// branch uses the caller-supplied type and span (the initializer's), while the
/// parameter branch carries its own resolved type and span in the pending
/// reference. Exactly one payload is populated; the builder validated the tag.
void lowerScalarLeaf(HirNodeId destination, identity::SemanticTypeId literalType,
                     const identity::SourceSpan& literalSpan,
                     const zc::Maybe<checker::checked::CanonicalConstValue>& literal,
                     const zc::Maybe<HirParameterReferenceExpression>& parameter, HirFnCtx& ctx) {
  ZC_IF_SOME(value, literal) {
    ctx.addExpression(HirScalarLiteralExpression{destination, literalType, value.clone(),
                                                 HirValueCategory::Value, literalSpan.clone()});
    return;
  }
  ZC_IF_SOME(reference, parameter) {
    ctx.addParameterReference(
        HirParameterReferenceExpression{destination, reference.parameter.clone(), reference.type,
                                        reference.category, reference.sourceSpan.clone()});
  }
}

/// \brief Emits the shared function header record, consuming its parameters.
HirFunctionDeclaration lowerFunctionHeader(HirNodeId functionId, HirNodeId bodyId,
                                           PendingFunctionDeclaration& function) {
  return HirFunctionDeclaration{functionId,
                                function.definition,
                                function.resultType,
                                zc::mv(function.parameters),
                                function.visibility.clone(),
                                function.linkage,
                                function.declarationSpan.clone(),
                                bodyId,
                                zc::none};
}

}  // namespace

void lowerScalarReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();

  ctx.addFunction(lowerFunctionHeader(functionId, bodyId, function));
  zc::Vector<HirNodeId> statements;
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});
  lowerScalarLeaf(valueId, function.resultType, function.valueSpan, function.literal,
                  function.parameterReference, ctx);
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
}

void lowerLocalReturnFunction(PendingFunctionDeclaration&& function, HirFnCtx& ctx) {
  const identity::SemanticTypeId localType = ZC_ASSERT_NONNULL(function.local).type;
  const identity::SourceSpan localSpan = ZC_ASSERT_NONNULL(function.local).sourceSpan.clone();
  const identity::SourceSpan initializerSpan =
      ZC_ASSERT_NONNULL(ZC_ASSERT_NONNULL(function.local).initializerSpan).clone();
  const identity::SemanticTypeId placeType = ZC_ASSERT_NONNULL(function.localReference).type;
  const HirValueCategory placeCategory = ZC_ASSERT_NONNULL(function.localReference).category;
  const identity::SourceSpan placeSpan =
      ZC_ASSERT_NONNULL(function.localReference).sourceSpan.clone();

  // Fixed source-preorder stride for the one-binding shape, matching the
  // generic materializer: function F, body F+1, local F+2, initializer F+3,
  // return F+4, value F+5. The block lists [local, return].
  const HirNodeId functionId = ctx.allocNode();
  const HirNodeId bodyId = ctx.allocNode();
  const HirNodeId localId = ctx.allocNode();
  const HirNodeId initializerId = ctx.allocNode();
  const HirNodeId returnId = ctx.allocNode();
  const HirNodeId valueId = ctx.allocNode();

  ctx.addFunction(lowerFunctionHeader(functionId, bodyId, function));
  zc::Vector<HirNodeId> statements;
  statements.add(localId);
  statements.add(returnId);
  ctx.addBlock(HirBlockStatement{bodyId, zc::mv(statements), function.bodySpan.clone()});

  ctx.addLocal(HirLocalBinding{localId, hirLocalId(1), localType, initializerId, localSpan.clone(),
                               initializerSpan.clone()});
  lowerScalarLeaf(initializerId, localType, initializerSpan, function.literal,
                  function.parameterReference, ctx);
  ctx.addLocalReference(HirLocalReferenceExpression{valueId, hirLocalId(1), placeType,
                                                    placeCategory, placeSpan.clone()});
  ctx.addReturn(
      HirReturnStatement{returnId, function.resultType, valueId, function.returnSpan.clone()});
}

}  // namespace detail
}  // namespace zomlang::compiler::hir
