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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/tree.h"

namespace zomlang {
namespace compiler {

namespace diagnostics {
class DiagnosticEngine;
}

namespace symbol {
class Symbol;
class SymbolTable;
class ScopeManager;
class Scope;
}  // namespace symbol

namespace binder {

/// \brief Phase 2: Name resolution pass.
///
/// NameResolver walks the syntax tree and resolves every identifier
/// reference to its corresponding symbol, populating BindingMetadata
/// with symbol bindings, unresolved markers, and shadowing information.
///
/// Resolution rules:
/// - IdentExpr: lookup in the current scope chain (lexical scoping)
/// - MemberExpression: resolve object, then look up the member in its type
/// - NamedTypeExpr: resolve a type name in the type namespace
/// - CallExpression: resolve the callee expression
/// - NewExpression: resolve the constructor / type being instantiated
///
/// Name lookup walks from the innermost active scope outward, respecting
/// value/type namespace separation and imported / inherited symbols.
class NameResolver final {
public:
  NameResolver(symbol::SymbolTable& symbols, symbol::ScopeManager& scopes, const ast::Tree& tree,
               ast::BindingMetadata& metadata, diagnostics::DiagnosticEngine& diags) noexcept;
  ~NameResolver() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NameResolver);

  /// \brief Resolve all name references in the tree.
  /// \return true if no errors were produced.
  bool resolve();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
