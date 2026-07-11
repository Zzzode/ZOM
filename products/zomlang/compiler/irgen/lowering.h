// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include "zc/core/one-of.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/irgen/ir.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/type/type-env.h"

namespace zomlang {
namespace compiler {
namespace irgen {

enum class LoweringPhase : uint8_t {
  CheckedInput,
  FunctionSignature,
  FunctionBody,
  Expression,
  ErrorPropagation,
  ForcedUnwrap,
  TargetLayout,
  ModuleVerification,
};

/// \brief Closed internal failure facts returned by mixed-prototype lowering.
///
/// These values are not diagnostic identifiers. The compiler driver maps every
/// value exhaustively to a registered capability or invariant diagnostic.
enum class LoweringFailureKind : uint8_t {
  UnsupportedSourceShape,
  UnsupportedExpression,
  UnknownTargetLayout,
  UnsupportedCrossSourceTarget,
  DispatchNotFrozen,
  InvalidSourceRoot,
  InvalidStatementList,
  InvalidBindingMetadata,
  MissingBindingSymbol,
  MissingTypeFact,
  InvalidTypeFact,
  MissingDispatchFact,
  InvalidDispatchFact,
  ErrorUnionLayoutMismatch,
  MissingErrorAlternative,
  MissingSourceContext,
  InvalidSourceRange,
  DuplicateFunctionSymbol,
};

struct LoweringFailure final {
  LoweringFailureKind kind = LoweringFailureKind::UnsupportedSourceShape;
  LoweringPhase phase = LoweringPhase::CheckedInput;
  ast::NodeId node;
};

struct LoweringSourceContext final {
  LoweringSourceContext(const source::SourceManager& manager, source::BufferId buffer)
      : manager(manager), buffer(buffer) {}

  const source::SourceManager& manager;
  source::BufferId buffer;
};

using LoweringResult = zc::OneOf<Module, LoweringFailure>;

/// \brief Lower the checked subset supported by the typed IR foundation.
/// \param tree The immutable checked AST.
/// \param metadata Bound symbols and scopes for the AST.
/// \param typeEnv The checker type side table for the AST.
/// \param target The explicit target data layout.
/// \param sourceContext Source mapping required for panic metadata.
/// \return A typed module or a precise unsupported-lowering error.
LoweringResult lowerCheckedTree(const ast::Tree& tree, const ast::BindingMetadata& metadata,
                                const type::TypeEnv& typeEnv, TargetDataLayout target,
                                zc::Maybe<const LoweringSourceContext&> sourceContext = zc::none);

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
