// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/build/mir-builder.h"

#include <cstddef>

#include "compiler/checker/body/marker-proof.h"
#include "compiler/identity/key/definition-key.h"
#include "compiler/mir/build/mir-fn-builder.h"

namespace zomlang::compiler::mir {
namespace {

/// \brief Unique pool lookup returning none on zero or duplicate matches.
///
/// Replicates the legacy HIR accessor helper semantics so the recursive arms and
/// the shape rail resolve a node identically; the duplicate-match case stays a
/// delegation back to the legacy rejection path.
template <typename Record>
zc::Maybe<const Record&> uniqueRecordFor(zc::ArrayPtr<const Record> records, hir::HirNodeId node) {
  zc::Maybe<const Record&> result;
  for (const auto& record : records) {
    if (record.node != node) continue;
    if (result != zc::none) return zc::none;
    result = record;
  }
  return result;
}

zc::Maybe<const hir::HirScalarLiteralExpression&> expressionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.expressions(), node);
}

zc::Maybe<const hir::HirDirectCallExpression&> callFor(const hir::VerifiedHirModule& module,
                                                       hir::HirNodeId node) {
  return uniqueRecordFor(module.calls(), node);
}

zc::Maybe<const hir::HirParameterReferenceExpression&> parameterReferenceFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.parameterReferences(), node);
}

zc::Maybe<const hir::HirParameterReborrowExpression&> parameterReborrowFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.parameterReborrows(), node);
}

zc::Maybe<const hir::HirReturnStatement&> returnFor(const hir::VerifiedHirModule& module,
                                                    hir::HirNodeId node) {
  return uniqueRecordFor(module.returns(), node);
}

/// \brief Resolves a place use to a copy or move operand from the Copy marker
/// proof, replicating the legacy placeUse helper.
zc::Maybe<MirOperand> placeUse(checker::marker::MarkerProofEngine& proofs, identity::DefId copy,
                               MirPlace&& place) {
  const identity::SemanticTypeId type = place.resultType();
  const auto proof = proofs.prove(copy, type);
  if (proof.is<checker::marker::MarkerProofPositive>()) { return MirOperand::copy(zc::mv(place)); }
  if (proof.is<checker::marker::MarkerProofNegative>() ||
      proof.is<checker::marker::MarkerProofUnsatisfied>()) {
    return MirOperand::move(zc::mv(place));
  }
  return zc::none;
}

/// \brief Lowers `fun f(...) -> T { return <literal>; }`: one root scope, no
/// locals, one empty entry block returning the scalar constant. Byte-identical
/// to the legacy fallthrough scalar construction.
zc::Maybe<RecursiveFunctionProduct> buildScalarReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirScalarLiteralExpression& literal,
    const checker::CheckerIdentityAuthority& identities) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.terminateBlock(MirTerminator::returnValue(
      MirOperand::constant(declaration.resultType, literal.value.clone()),
      sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fun f(p0..pN-1) -> R { return pK; }`: one root scope, one
/// parameter local per declared parameter in source order, one empty entry
/// block returning a copy/move place-use of the referenced parameter local.
/// Byte-identical to the legacy parameter-return construction.
zc::Maybe<RecursiveFunctionProduct> buildParameterReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirReturnStatement& sourceReturn,
    const hir::HirParameterReferenceExpression& reference,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  size_t referencedIndex = 0;
  bool referencedFound = false;
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == reference.parameter &&
        declaration.parameters[i].type == reference.type) {
      referencedIndex = i;
      referencedFound = true;
      break;
    }
  }
  if (!referencedFound) return zc::none;
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[i].type, scope,
                     declaration.parameters[i].sourceSpan.clone());
  }
  const MirLocalId referencedLocal =
      ZC_ASSERT_NONNULL(MirLocalId::fromOrdinal(static_cast<uint32_t>(referencedIndex + 1)));
  zc::Vector<MirProjection> projections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(referencedLocal, reference.type, zc::mv(projections), reference.type));
  if (returnOperand == zc::none) return zc::none;

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

}  // namespace

zc::Maybe<RecursiveFunctionProduct> tryBuildRecursiveFunction(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (block.statements.size() != 1) return zc::none;
  auto sourceReturn = returnFor(hirModule, block.statements[0]);
  if (sourceReturn == zc::none) return zc::none;
  const hir::HirNodeId valueNode = ZC_ASSERT_NONNULL(sourceReturn).value;

  // Scalar literal return: exactly one literal expression and no direct call on
  // the return value, no unsafe tail (that shape stays on the legacy rail).
  auto literal = expressionFor(hirModule, valueNode);
  auto directCall = callFor(hirModule, valueNode);
  if (literal != zc::none && directCall == zc::none && declaration.unsafeBlock == zc::none &&
      ZC_ASSERT_NONNULL(literal).type == declaration.resultType) {
    return buildScalarReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                             ZC_ASSERT_NONNULL(literal), identities);
  }

  // Parameter return: a bare parameter reference (never a parameter reborrow,
  // which the legacy rail lowers with a borrow-creation temporary).
  auto parameterReference = parameterReferenceFor(hirModule, valueNode);
  auto parameterReborrow = parameterReborrowFor(hirModule, valueNode);
  if (parameterReference != zc::none && parameterReborrow == zc::none &&
      ZC_ASSERT_NONNULL(parameterReference).type == declaration.resultType &&
      ZC_ASSERT_NONNULL(parameterReference).category == hir::HirValueCategory::Place) {
    return buildParameterReturn(declaration, ZC_ASSERT_NONNULL(sourceReturn),
                                ZC_ASSERT_NONNULL(parameterReference), identities, proofs,
                                copyMarker);
  }

  return zc::none;
}

}  // namespace zomlang::compiler::mir
