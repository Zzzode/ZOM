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

/// \brief The records the three bindings resolve to, so a lowering never
/// re-derives the shape. Every reference points into the module the predicate
/// saw.
struct CallArithmeticParts final {
  const HirLocalBinding& callLocal;
  const HirLocalBinding& constantLocal;
  const HirLocalBinding& resultLocal;
  const HirDirectCallExpression& call;
  const HirScalarLiteralExpression& constant;
  const HirPrimitiveBinaryExpression& binary;
  const HirLocalReferenceExpression& returnReference;
  const HirReturnStatement& returnStatement;
  /// \brief The binary's operation, already narrowed to the set both the MIR
  /// strategy and the LIR lowering map. Never Pow: LIR's arithmetic operator
  /// mapping has no arm for it.
  checker::PrimitiveOperation operation;
};

/// \brief True exactly when the declaration plus block are the admitted body:
///   fn f() -> T { let x = g(); let a: T = <const>; let y: T = a <op> b; return y; }
///
/// This is the single definition of that body. The HIR admission gate, the HIR
/// verifier, and the MIR strategy all call it, so the admitted set and the
/// lowered set are the same set by construction.
///
/// The predicate also owns the two bounds that keep the set a subset of what
/// LIR accepts: the result type's primitive kind must have a LIR integer width
/// (so Isize/Usize are excluded), and the operation must have a LIR arithmetic
/// arm (so Pow is excluded).
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
