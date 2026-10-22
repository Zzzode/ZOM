// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/checker/operator-kind.h"
#include "compiler/hir/hir-module.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::hir {
namespace detail {

/// \brief The HIR pools the call-then-local-arithmetic predicate reads.
///
/// The HIR builder passes its in-construction vectors; every consumer that
/// already owns a verified module uses of() instead. One struct keeps the
/// predicate independent of how the records were produced.
struct CallArithmeticPools final {
  zc::ArrayPtr<const HirFunctionDeclaration> functions;
  zc::ArrayPtr<const HirBlockStatement> blocks;
  zc::ArrayPtr<const HirReturnStatement> returns;
  zc::ArrayPtr<const HirScalarLiteralExpression> expressions;
  zc::ArrayPtr<const HirLocalBinding> locals;
  zc::ArrayPtr<const HirLocalReferenceExpression> localReferences;
  zc::ArrayPtr<const HirDirectCallExpression> calls;
  zc::ArrayPtr<const HirPrimitiveBinaryExpression> primitiveBinaryOperations;

  /// \brief Returns the pools of one verified module.
  static CallArithmeticPools of(const VerifiedHirModule& module) noexcept;
};

/// \brief The records the admitted bindings resolve to, so a lowering never
/// re-derives the shape. Every reference points into the module the predicate
/// saw.
///
/// Two body shapes are admitted. The three-binding form declares a call local,
/// a constant local and a result local; the two-binding form drops the constant
/// local, so the call local is one of the arithmetic operands. `constantLocal`
/// and `constant` are set only for the three-binding form.
struct CallArithmeticParts final {
  const HirLocalBinding& callLocal;
  zc::Maybe<const HirLocalBinding&> constantLocal;
  const HirLocalBinding& resultLocal;
  const HirDirectCallExpression& call;
  zc::Maybe<const HirScalarLiteralExpression&> constant;
  const HirPrimitiveBinaryExpression& binary;
  const HirReturnStatement& returnStatement;
  /// \brief The binary's operation, already narrowed to the set both the MIR
  /// strategy and the LIR lowering map. Never Pow: LIR's arithmetic operator
  /// mapping has no arm for it.
  checker::PrimitiveOperation operation;
};

/// \brief True exactly when the declaration plus block are an admitted body:
///   fn f() -> T { let x = g(); let a: T = <const>; let y: T = a <op> b; return y; }
///   fn f() -> T { let x = g(); let y: T = x <op> b; return y; }
///
/// This is the single definition of both bodies. The HIR admission gate, the HIR
/// verifier, and the MIR strategy all call it, so the admitted set and the
/// lowered set are the same set by construction.
///
/// The predicate also owns the two bounds that keep the set a subset of what
/// LIR accepts: the result type's primitive kind must have a LIR integer width
/// (so Isize/Usize are excluded), and the operation must have a LIR arithmetic
/// arm (so Pow is excluded).
///
/// It also owns the module half. The body is emitted only by
/// `lowerCallThenLocalArithmeticModule`, which the driver dispatches for a
/// two-function module whose callee is the scalar constant-return shape. A body
/// accepted in any other module would pass HIR and then meet the driver's
/// module-level rejection, which reports an internal slice inventory instead of
/// a capability drain. So the module must be exactly the one that lowering
/// receives: two functions, one of them the called callee, that callee an
/// ordinary parameter-free function returning the caller's type from a single
/// integer literal.
///
/// \param pools The HIR record pools to resolve against.
/// \param semanticTypes Session-owned type store that owns the result type.
/// \param declaration The candidate function declaration.
/// \param block The candidate function body block.
/// \return The resolved parts, or none when the body is outside the shape.
///
/// Not `noexcept`: the primitive-kind allowlist consults the session type
/// store, whose lookup raises an internal invariant on a malformed handle.
ZC_NODISCARD zc::Maybe<CallArithmeticParts> matchCallThenLocalArithmetic(
    const CallArithmeticPools& pools, const type::SemanticTypeStore& semanticTypes,
    const HirFunctionDeclaration& declaration, const HirBlockStatement& block);

}  // namespace detail
}  // namespace zomlang::compiler::hir
