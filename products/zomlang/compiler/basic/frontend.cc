// Copyright (c) 2024 Zode.Z. All rights reserved
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

#include "zomlang/compiler/basic/frontend.h"

#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/binder.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace basic {

/// Implementation of performParse
zc::Maybe<zc::Own<ast::Node>> performParse(const source::SourceManager& sm,
                                           diagnostics::DiagnosticEngine& diagnosticEngine,
                                           const LangOptions& langOpts,
                                           const source::BufferId& bufferId) {
  // Create a Parser instance
  parser::Parser parser(sm, diagnosticEngine, langOpts, bufferId);
  // Assuming Parser::parse now returns the AST or null on failure
  zc::Maybe<zc::Own<ast::Node>> ast = parser.parse();

  // Check for parsing errors
  if (diagnosticEngine.hasErrors()) {
    return zc::none;  // Return none if parsing reported errors
  }

  // Return the parsed AST (or none if parser returned none)
  return ast;  // NRVO optimization
}

/// Implementation of performBind
bool performBind(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
                 ast::Node& ast) {
  // Cast to SourceFile for binding
  if (auto* sourceFile = dynamic_cast<ast::SourceFile*>(&ast)) {
    // Create a binder instance
    binder::Binder binder(symbolTable, diagnosticEngine);

    // Perform binding for the source file
    binder.bindSourceFile(*sourceFile);

    // Return true if no errors occurred during binding
    return !diagnosticEngine.hasErrors();
  }

  // If the AST is not a SourceFile, we can't bind it
  return false;
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
