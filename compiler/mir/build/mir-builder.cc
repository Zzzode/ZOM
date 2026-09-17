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

zc::Maybe<const hir::HirLocalBinding&> localFor(const hir::VerifiedHirModule& module,
                                                hir::HirNodeId node) {
  return uniqueRecordFor(module.locals(), node);
}

zc::Maybe<const hir::HirLocalWriteStatement&> localWriteFor(const hir::VerifiedHirModule& module,
                                                            hir::HirNodeId node) {
  return uniqueRecordFor(module.localWrites(), node);
}

zc::Maybe<const hir::HirLocalReferenceExpression&> localReferenceFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.localReferences(), node);
}

zc::Maybe<const hir::HirLocalFieldProjectionExpression&> localFieldProjectionFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.localFieldProjections(), node);
}

zc::Maybe<const hir::HirNominalAggregateExpression&> aggregateFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.aggregates(), node);
}

zc::Maybe<const hir::HirPrimitiveBinaryExpression&> primitiveBinaryFor(
    const hir::VerifiedHirModule& module, hir::HirNodeId node) {
  return uniqueRecordFor(module.primitiveBinaryOperations(), node);
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

/// \brief Finds the source index of the parameter matching a parameter-reference
/// key, replicating the legacy linear key match.
zc::Maybe<size_t> parameterIndexFor(const hir::HirFunctionDeclaration& declaration,
                                    const identity::CallableParameterKey& parameter) {
  for (size_t i = 0; i < declaration.parameters.size(); ++i) {
    if (declaration.parameters[i].key == parameter) return i;
  }
  return zc::none;
}

/// \brief Lowers a sequential N-local single-block body
/// (`let a = <lit/param/local>; ... let z = ...; return <local-or-param>;`,
/// N>=2): parameter locals occupy localId(1..P), user local i occupies
/// localId(P+i+1), and the single block holds StorageLive plus an Initialize
/// Assign per binding in source order followed by a Return of the selected
/// place. Each initializer is a scalar constant or a copy/move place-use of a
/// parameter or an earlier user local; literal-only bodies on parameterized
/// signatures still declare the parameter locals, unlike the dedicated
/// single-local rail. Aggregate, primitive-binary, nested-operand, and
/// unsafe-tail initializers return none so the legacy sequential rail keeps
/// them.
zc::Maybe<RecursiveFunctionProduct> buildSequentialLocalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirBlockStatement& block,
    const hir::VerifiedHirModule& hirModule, const checker::CheckerIdentityAuthority& identities,
    checker::marker::MarkerProofEngine& proofs, identity::DefId copyMarker) {
  if (declaration.unsafeBlock != zc::none) return zc::none;
  if (block.statements.size() < 3) return zc::none;
  const size_t bindingCount = block.statements.size() - 1;

  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;
  auto sourceReturn = returnFor(hirModule, block.statements[bindingCount]);
  if (sourceReturn == zc::none) return zc::none;

  // Resolve every leading binding; layer-local ordinals must be dense 1..N and
  // every binding carries an initializer.
  zc::Vector<const hir::HirLocalBinding*> bindings;
  for (size_t i = 0; i < bindingCount; ++i) {
    auto sourceLocal = localFor(hirModule, block.statements[i]);
    if (sourceLocal == zc::none) return zc::none;
    const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
    if (binding.local.ordinal() != static_cast<uint32_t>(i + 1) ||
        binding.initializer == zc::none) {
      return zc::none;
    }
    bindings.add(&binding);
  }

  const hir::HirNodeId returnNode = ZC_ASSERT_NONNULL(sourceReturn).value;
  auto returnLocalReference = localReferenceFor(hirModule, returnNode);
  auto returnParameterReference = parameterReferenceFor(hirModule, returnNode);
  auto returnFieldProjection = localFieldProjectionFor(hirModule, returnNode);
  if (returnFieldProjection != zc::none) return zc::none;
  if (returnLocalReference == zc::none && returnParameterReference == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  zc::Vector<MirLocalId> parameterLocals;
  for (size_t p = 0; p < declaration.parameters.size(); ++p) {
    parameterLocals.add(ctx.declareLocal(MirLocalKind::Parameter, declaration.parameters[p].type,
                                         scope, declaration.parameters[p].sourceSpan.clone()));
  }
  zc::Vector<MirLocalId> userLocals;
  for (size_t i = 0; i < bindingCount; ++i) {
    userLocals.add(ctx.declareLocal(MirLocalKind::UserLocal, bindings[i]->type, scope,
                                    bindings[i]->sourceSpan.clone()));
  }

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  for (size_t i = 0; i < bindingCount; ++i) {
    const auto& binding = *bindings[i];
    hir::HirNodeId initializerNode;
    ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
    auto literal = expressionFor(hirModule, initializerNode);
    auto aggregate = aggregateFor(hirModule, initializerNode);
    auto localReference = localReferenceFor(hirModule, initializerNode);
    auto parameterReference = parameterReferenceFor(hirModule, initializerNode);
    auto binary = primitiveBinaryFor(hirModule, initializerNode);
    // Aggregate, binary, and nested-operand initializers stay on the legacy rail.
    if (aggregate != zc::none || binary != zc::none) return zc::none;
    const int present = (literal != zc::none ? 1 : 0) + (localReference != zc::none ? 1 : 0) +
                        (parameterReference != zc::none ? 1 : 0);
    if (present != 1) return zc::none;

    zc::Maybe<MirRvalue> rvalue;
    identity::SourceSpan assignSpan = binding.sourceSpan.clone();
    ZC_IF_SOME(value, literal) {
      if (value.type != binding.type || value.category != hir::HirValueCategory::Value) {
        return zc::none;
      }
      rvalue = MirRvalue::use(MirOperand::constant(binding.type, value.value.clone()));
      assignSpan = value.sourceSpan.clone();
    }
    ZC_IF_SOME(value, localReference) {
      if (value.type != binding.type || value.category != hir::HirValueCategory::Place ||
          value.local.ordinal() == 0 || value.local.ordinal() > static_cast<uint32_t>(i)) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(userLocals[value.local.ordinal() - 1], binding.type,
                                       zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      assignSpan = value.sourceSpan.clone();
    }
    ZC_IF_SOME(value, parameterReference) {
      auto parameterIndex = parameterIndexFor(declaration, value.parameter);
      if (parameterIndex == zc::none || value.type != binding.type ||
          value.category != hir::HirValueCategory::Place) {
        return zc::none;
      }
      zc::Vector<MirProjection> projections;
      auto operand = placeUse(proofs, copyMarker,
                              MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                                       binding.type, zc::mv(projections), binding.type));
      if (operand == zc::none) return zc::none;
      rvalue = MirRvalue::use(zc::mv(ZC_ASSERT_NONNULL(operand)));
      assignSpan = value.sourceSpan.clone();
    }
    if (rvalue == zc::none) return zc::none;

    ctx.appendStatement(MirStatement::storageLive(userLocals[i], binding.sourceSpan.clone()));
    zc::Vector<MirProjection> destinationProjections;
    ctx.appendStatement(MirStatement::assign(
        MirPlace(userLocals[i], binding.type, zc::mv(destinationProjections), binding.type),
        zc::mv(ZC_ASSERT_NONNULL(rvalue)), MirInitializationKind::Initialize, zc::mv(assignSpan)));
  }

  zc::Maybe<MirOperand> returnOperand;
  ZC_IF_SOME(reference, returnLocalReference) {
    if (reference.type != declaration.resultType ||
        reference.category != hir::HirValueCategory::Place || reference.local.ordinal() == 0 ||
        reference.local.ordinal() > static_cast<uint32_t>(bindingCount)) {
      return zc::none;
    }
    zc::Vector<MirProjection> projections;
    returnOperand = placeUse(proofs, copyMarker,
                             MirPlace(userLocals[reference.local.ordinal() - 1], reference.type,
                                      zc::mv(projections), reference.type));
  }
  ZC_IF_SOME(reference, returnParameterReference) {
    auto parameterIndex = parameterIndexFor(declaration, reference.parameter);
    if (parameterIndex == zc::none || reference.type != declaration.resultType ||
        reference.category != hir::HirValueCategory::Place) {
      return zc::none;
    }
    zc::Vector<MirProjection> projections;
    returnOperand = placeUse(proofs, copyMarker,
                             MirPlace(parameterLocals[ZC_ASSERT_NONNULL(parameterIndex)],
                                      reference.type, zc::mv(projections), reference.type));
  }
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(
      MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                 ZC_ASSERT_NONNULL(sourceReturn).sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
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

/// \brief Lowers `fun f(...) -> T { let x = <literal>; return x; }`: one root
/// scope, one UserLocal at localId(1), StorageLive followed by an Initialize
/// Assign of the scalar constant, and a Return of the copy/move place-use of
/// the local. The legacy dedicated single-local rail declares no parameter
/// locals, allocates no function-result local, and emits no StorageDead, so
/// this arm reproduces exactly that layout byte for byte.
zc::Maybe<RecursiveFunctionProduct> buildScalarLocalReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer, const hir::HirReturnStatement& sourceReturn,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId userLocal = ctx.declareLocal(MirLocalKind::UserLocal, sourceLocal.type, scope,
                                                sourceLocal.sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(userLocal, sourceLocal.sourceSpan.clone()));
  zc::Vector<MirProjection> destinationProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, sourceLocal.type, zc::mv(destinationProjections), sourceLocal.type),
      MirRvalue::use(MirOperand::constant(sourceLocal.type, initializer.value.clone())),
      MirInitializationKind::Initialize, initializer.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, sourceLocal.type, zc::mv(returnProjections), sourceLocal.type));
  if (returnOperand == zc::none) return zc::none;
  ctx.terminateBlock(MirTerminator::returnValue(zc::mv(ZC_ASSERT_NONNULL(returnOperand)),
                                                sourceReturn.sourceSpan.clone()));

  MirFunction function = ctx.finish(declaration.definition, MirFunctionKind::Function,
                                    identity::DefinitionKind::Function, declaration.resultType,
                                    declaration.sourceSpan.clone());
  zc::Array<uint8_t> ownerKey = ZC_ASSERT_NONNULL(definition).key().encode();
  return RecursiveFunctionProduct{zc::mv(function), zc::mv(ownerKey)};
}

/// \brief Lowers `fun f() -> T { mut x = <lit>; x = <lit>; return x; }`: one
/// root scope, one UserLocal at localId(1), StorageLive, the Initialize Assign
/// of the initializer constant, one Overwrite Assign of the write constant, and
/// a Return of the copy/move place-use of the local. Both values are scalar
/// literals, so the legacy rail declares no parameter locals; the statement and
/// span order is reproduced byte for byte.
zc::Maybe<RecursiveFunctionProduct> buildScalarLocalOverwriteReturn(
    const hir::HirFunctionDeclaration& declaration, const hir::HirLocalBinding& sourceLocal,
    const hir::HirScalarLiteralExpression& initializer,
    const hir::HirLocalWriteStatement& overwrite,
    const hir::HirScalarLiteralExpression& overwriteValue,
    const hir::HirReturnStatement& sourceReturn,
    const checker::CheckerIdentityAuthority& identities, checker::marker::MarkerProofEngine& proofs,
    identity::DefId copyMarker) {
  auto definition = identities.definition(declaration.definition);
  if (definition == zc::none) return zc::none;

  detail::MirFnCtx ctx;
  const MirSourceScopeId scope = ctx.pushRootScope(declaration.sourceSpan.clone());
  const MirLocalId userLocal = ctx.declareLocal(MirLocalKind::UserLocal, sourceLocal.type, scope,
                                                sourceLocal.sourceSpan.clone());

  const MirBlockId entry = ctx.beginBlock(scope);
  (void)entry;
  ctx.appendStatement(MirStatement::storageLive(userLocal, sourceLocal.sourceSpan.clone()));
  zc::Vector<MirProjection> initializeProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, sourceLocal.type, zc::mv(initializeProjections), sourceLocal.type),
      MirRvalue::use(MirOperand::constant(sourceLocal.type, initializer.value.clone())),
      MirInitializationKind::Initialize, initializer.sourceSpan.clone()));
  zc::Vector<MirProjection> overwriteProjections;
  ctx.appendStatement(MirStatement::assign(
      MirPlace(userLocal, sourceLocal.type, zc::mv(overwriteProjections), sourceLocal.type),
      MirRvalue::use(MirOperand::constant(sourceLocal.type, overwriteValue.value.clone())),
      MirInitializationKind::Overwrite, overwrite.sourceSpan.clone()));
  zc::Vector<MirProjection> returnProjections;
  auto returnOperand =
      placeUse(proofs, copyMarker,
               MirPlace(userLocal, sourceLocal.type, zc::mv(returnProjections), sourceLocal.type));
  if (returnOperand == zc::none) return zc::none;
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
  if (block.statements.empty()) return zc::none;
  // Every owned shape ends in its trailing return; a missing return delegates.
  auto sourceReturn = returnFor(hirModule, block.statements[block.statements.size() - 1]);
  if (sourceReturn == zc::none) return zc::none;
  const hir::HirNodeId valueNode = ZC_ASSERT_NONNULL(sourceReturn).value;

  // The bare literal and parameter returns are the one-statement shapes.
  if (block.statements.size() == 1) {
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
  }

  // Single scalar-initialized user local: `let x = <literal>; return x;`. The
  // strict gate mirrors validLocalReturnFunction and the dedicated two-statement
  // construction: one binding whose initializer is a scalar literal and a bare
  // place reference of that same local. No parameter locals are declared on this
  // legacy layout, regardless of the function signature. Aggregate, binary,
  // parameter, call, borrow, and uninitialized initializers keep their own
  // legacy shapes.
  if (block.statements.size() == 2 && declaration.unsafeBlock == zc::none) {
    auto sourceLocal = localFor(hirModule, block.statements[0]);
    if (sourceLocal != zc::none) {
      const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
      hir::HirNodeId initializerNode;
      ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
      auto literal = expressionFor(hirModule, initializerNode);
      auto reference = localReferenceFor(hirModule, valueNode);
      auto fieldProjection = localFieldProjectionFor(hirModule, valueNode);
      auto aggregate = aggregateFor(hirModule, initializerNode);
      auto binary = primitiveBinaryFor(hirModule, initializerNode);
      if (literal != zc::none && aggregate == zc::none && binary == zc::none &&
          reference != zc::none && fieldProjection == zc::none &&
          binding.initializer == ZC_ASSERT_NONNULL(literal).node &&
          binding.local == ZC_ASSERT_NONNULL(reference).local &&
          binding.type == declaration.resultType &&
          ZC_ASSERT_NONNULL(literal).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).category == hir::HirValueCategory::Place) {
        return buildScalarLocalReturn(declaration, binding, ZC_ASSERT_NONNULL(literal),
                                      ZC_ASSERT_NONNULL(sourceReturn), identities, proofs,
                                      copyMarker);
      }
    }
  }

  // Single scalar local with one repeated literal write:
  // `mut x = <lit>; x = <lit>; return x;`. Mirrors
  // validLocalOverwriteReturnFunction: the middle statement is an Overwrite
  // local write to the binding with no field projection, and both values are
  // scalar literals. Parameter or binary write values keep their own legacy
  // shapes.
  if (block.statements.size() == 3 && declaration.unsafeBlock == zc::none) {
    auto sourceLocal = localFor(hirModule, block.statements[0]);
    auto sourceOverwrite = localWriteFor(hirModule, block.statements[1]);
    if (sourceLocal != zc::none && sourceOverwrite != zc::none) {
      const auto& binding = ZC_ASSERT_NONNULL(sourceLocal);
      const auto& write = ZC_ASSERT_NONNULL(sourceOverwrite);
      hir::HirNodeId initializerNode;
      ZC_IF_SOME(initializer, binding.initializer) { initializerNode = initializer; }
      auto literal = expressionFor(hirModule, initializerNode);
      auto overwriteLiteral = expressionFor(hirModule, write.value);
      auto reference = localReferenceFor(hirModule, valueNode);
      auto fieldProjection = localFieldProjectionFor(hirModule, valueNode);
      auto overwriteBinary = primitiveBinaryFor(hirModule, write.value);
      auto overwriteParameter = parameterReferenceFor(hirModule, write.value);
      if (literal != zc::none && overwriteLiteral != zc::none && reference != zc::none &&
          fieldProjection == zc::none && overwriteBinary == zc::none &&
          overwriteParameter == zc::none &&
          binding.initializer == ZC_ASSERT_NONNULL(literal).node &&
          write.kind == hir::HirLocalWriteKind::Overwrite && write.field == zc::none &&
          write.local == binding.local && write.local == ZC_ASSERT_NONNULL(reference).local &&
          write.type == binding.type && binding.type == declaration.resultType &&
          ZC_ASSERT_NONNULL(literal).type == binding.type &&
          ZC_ASSERT_NONNULL(overwriteLiteral).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).type == binding.type &&
          ZC_ASSERT_NONNULL(reference).category == hir::HirValueCategory::Place) {
        return buildScalarLocalOverwriteReturn(declaration, binding, ZC_ASSERT_NONNULL(literal),
                                               write, ZC_ASSERT_NONNULL(overwriteLiteral),
                                               ZC_ASSERT_NONNULL(sourceReturn), identities, proofs,
                                               copyMarker);
      }
    }
  }

  // Sequential N-local body with N>=2 plain initializers:
  // `let a = <lit/param/local>; ... return <local-or-param>;`. The arm performs
  // its own strict gate; aggregate, binary, nested-operand, unsafe-tail,
  // field-projection, and forward-reference shapes return none and keep the
  // legacy sequential rail.
  if (block.statements.size() >= 3 && declaration.unsafeBlock == zc::none) {
    auto sequential =
        buildSequentialLocalReturn(declaration, block, hirModule, identities, proofs, copyMarker);
    if (sequential != zc::none) return sequential;
  }

  return zc::none;
}

}  // namespace zomlang::compiler::mir
