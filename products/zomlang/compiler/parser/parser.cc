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

#include "zomlang/compiler/parser/parser.h"

#include "zomlang/compiler/ast/ast.h"

namespace zomlang {
namespace compiler {
namespace parser {

// ================================================================================
// Parser::Impl
struct Parser::Impl {
  Impl(diagnostics::DiagnosticEngine& diagnosticEngine, uint64_t bufferId) noexcept
      : bufferId(bufferId), diagnosticEngine(diagnosticEngine) {}
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  uint64_t bufferId;
  diagnostics::DiagnosticEngine& diagnosticEngine;
};

// ================================================================================
// Parser

Parser::Parser(diagnostics::DiagnosticEngine& diagnosticEngine, uint64_t bufferId) noexcept
    : impl(zc::heap<Impl>(diagnosticEngine, bufferId)) {}

Parser::~Parser() noexcept(false) = default;

zc::Maybe<zc::Own<ast::AST>> Parser::parse(zc::ArrayPtr<const lexer::Token> tokens) {
  return zc::none;
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang