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
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

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
  return zc::mv(ast);
}

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
