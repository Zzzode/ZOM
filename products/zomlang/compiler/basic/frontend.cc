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

#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/binder.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {
namespace basic {

zc::Maybe<ast::Tree> performParse(const source::SourceManager& sm,
                                  diagnostics::DiagnosticEngine& diagnosticEngine,
                                  const LangOptions& langOpts, basic::StringPool& stringPool,
                                  const source::BufferId& bufferId) {
  // Create a Parser instance
  parser::Parser parser(sm, diagnosticEngine, langOpts, stringPool, bufferId);
  zc::Maybe<ast::Tree> tree = parser.parse();

  // Check for parsing errors
  if (diagnosticEngine.hasErrors()) {
    return zc::none;  // Return none if parsing reported errors
  }

  return zc::mv(tree);
}

bool performBind(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
                 const ast::Tree& tree, ast::BindingMetadata& metadata) {
  binder::Binder binder(symbolTable, diagnosticEngine, tree, metadata);
  return binder.bind();
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
