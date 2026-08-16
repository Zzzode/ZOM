// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/common.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/checker/inference/checked-facts.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/type/semantic-type-store.h"

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

/// \brief Produces canonical scalar-literal facts for signature and body checking.
class FactEmitter final {
public:
  ZC_NODISCARD static FactEmissionResult emit(const FactEmissionInput& input);
};

}  // namespace zomlang::compiler::checker::scalar_literal
