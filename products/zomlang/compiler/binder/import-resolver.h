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
#include "zomlang/compiler/ast/tree.h"

namespace zomlang {
namespace compiler {

namespace diagnostics {
class DiagnosticEngine;
}

namespace symbol {
class SymbolTable;
class ScopeManager;
class Scope;
}  // namespace symbol

namespace binder {

class DefinitionIdentityMap;

/// \brief Phase 1.5 Import Resolution.
///
/// ImportResolver is responsible for:
/// 1. Resolving import declaration module paths
/// 2. Binding imported symbols into the current scope
/// 3. Handling re-export declarations
/// 4. Detecting circular import dependencies
///
/// This phase runs after the initial Binder pass (Phase 1) which establishes
/// parent/child relationships and basic scope structure.
class ImportResolver final {
public:
  ImportResolver(symbol::SymbolTable& symbols, symbol::ScopeManager& scopes, const ast::Tree& tree,
                 const DefinitionIdentityMap& identities, ast::BindingMetadata& metadata,
                 diagnostics::DiagnosticEngine& diags) noexcept;
  ~ImportResolver() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ImportResolver);

  /// \brief Resolve all import and export declarations in the tree.
  /// \return true if no errors were encountered.
  bool resolveImports();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
