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
}

namespace binder {

class DefinitionIdentityMap;

/// \brief Binds an immutable AST tree into side-table metadata.
class Binder final {
public:
  Binder(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
         const ast::Tree& tree, const DefinitionIdentityMap& identities,
         ast::BindingMetadata& metadata) noexcept;
  ~Binder() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Binder);

  /// \brief Bind the full tree and populate metadata keyed by NodeId.
  bool bind();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
