// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "compiler/ast/tree.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/checker/inference/checked-facts.h"
#include "compiler/type/semantic-type-store.h"
#include "zc/core/common.h"
#include "zc/core/one-of.h"

namespace zomlang::compiler::checker::scalar_literal {

/// \brief Complete input for canonical scalar-literal fact production.
struct FactEmissionInput final {
  identity::SemanticContextBrand semanticContext;
  identity::ModuleId module;
  const ast::Tree& tree;
  ast::NodeId node;
  const checked::CheckedNodeKey& checkedNode;
  const identity::SourceFileKey& source;
  const CheckerIdentityAuthority& identities;
  type::SemanticTypeStore& semanticTypes;
};

/// \brief The orthogonal node-type and literal facts for one accepted scalar literal.
struct EmittedFacts final {
  checked::NodeTypeMap::Entry nodeType;
  checked::LiteralFactMap::Entry literal;
};

using FactEmissionResult = zc::OneOf<EmittedFacts, checked::CheckedFactsSourceRejected,
                                     checked::CheckedFactsInvariantRejected>;

/// \brief Whether `kind` is a scalar literal the emitter below can produce facts for.
///
/// Callers must gate on this before calling `FactEmitter::emit`: the emitter treats an
/// unaccepted kind as a compiler invariant, which is the wrong rail for source the
/// checker simply has no semantic contract for yet.
ZC_NODISCARD bool isEmittableScalarLiteral(ast::SyntaxKind kind) noexcept;

/// \brief The primitive kind a scalar literal contributes as a binary operand,
/// or none for literals that do not name a primitive operand type (string,
/// unit, null).
///
/// Integer literals default to `i32` when no annotation context narrows them;
/// that default is the sound choice for an ill-typed-operator classification,
/// since `true + 1` and friends are ill-typed regardless of the exact integer
/// width.
ZC_NODISCARD zc::Maybe<type::semantic::PrimitiveKind> scalarLiteralPrimitiveKind(
    ast::SyntaxKind kind) noexcept;

/// \brief Produces canonical scalar-literal facts for signature and body checking.
class FactEmitter final {
public:
  ZC_NODISCARD static FactEmissionResult emit(const FactEmissionInput& input);
};

}  // namespace zomlang::compiler::checker::scalar_literal
