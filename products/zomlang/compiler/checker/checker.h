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
#include "zomlang/compiler/type/type-env.h"

namespace zomlang {
namespace compiler {

namespace diagnostics {
class DiagnosticEngine;
}

namespace symbol {
class SymbolTable;
}

namespace checker {

/// \brief Checker - Performs type checking and semantic validation on a bound AST.
///
/// The Checker runs after the Binder has completed name resolution. It performs
/// a two-phase type checking process as specified in RFC 0005:
///
///   Phase A: Signature Computation (DeclSignatureComputer)
///            - Computes type signatures for all declarations
///   Phase B: Body Checking (BodyChecker)
///            - Checks function bodies, expressions, statements
///            - Infers types for all expressions
///
/// After body checking, the TraitResolver validates interface coherence.
class Checker final {
public:
  Checker(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
          const ast::Tree& tree, const ast::BindingMetadata& metadata,
          type::TypeEnv& typeEnv) noexcept;
  ~Checker() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Checker);

  /// \brief Run the full type checking pipeline.
  /// \return true if no fatal errors were produced.
  bool check();

  /// \brief Get the type environment populated during checking.
  type::TypeEnv& getTypeEnv();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
