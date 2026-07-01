// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zomlang/compiler/parser/token-cursor.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"

namespace zomlang {
namespace compiler {
namespace parser {

TokenCursor::TokenCursor(zc::ArrayPtr<const lexer::Token> tokens) { reset(tokens); }

void TokenCursor::reset(zc::ArrayPtr<const lexer::Token> newTokens) {
  tokens = newTokens;
  current = 0;
}

size_t TokenCursor::size() const { return tokens.size(); }

size_t TokenCursor::position() const { return current; }

ast::SyntaxKind TokenCursor::peek(size_t offset) const { return token(offset).getKind(); }

const lexer::Token& TokenCursor::token(size_t offset) const {
  return tokenAt(relativeIndex(offset));
}

const lexer::Token& TokenCursor::tokenAt(size_t index) const {
  ZC_IREQUIRE(tokens.size() != 0, "token cursor requires a token stream with EOF");
  if (index >= tokens.size()) { index = eofIndex(); }
  return tokens[index];
}

bool TokenCursor::at(ast::SyntaxKind kind) const { return peek() == kind; }

bool TokenCursor::eat(ast::SyntaxKind kind) {
  if (!at(kind)) { return false; }
  advance();
  return true;
}

void TokenCursor::advance() {
  if (!isAtEnd()) { ++current; }
}

void TokenCursor::moveTo(size_t index) {
  ZC_IREQUIRE(tokens.size() != 0, "token cursor requires a token stream with EOF");
  ZC_IREQUIRE(index < tokens.size(), "token cursor target outside token stream");
  current = index;
}

bool TokenCursor::expect(ast::SyntaxKind kind, diagnostics::DiagnosticEngine& diagnosticEngine,
                         zc::StringPtr expected) {
  if (eat(kind)) { return true; }
  diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(token().getLocation(), expected);
  return false;
}

TokenCursor::Mark TokenCursor::mark() const { return current; }

void TokenCursor::rewind(Mark mark) { moveTo(mark); }

bool TokenCursor::isAtEnd() const { return peek() == ast::SyntaxKind::EndOfFile; }

size_t TokenCursor::eofIndex() const {
  ZC_IREQUIRE(tokens.size() != 0, "token cursor requires a token stream with EOF");
  return tokens.size() - 1;
}

size_t TokenCursor::relativeIndex(size_t offset) const {
  const size_t eof = eofIndex();
  if (current >= eof) { return eof; }
  if (offset > eof - current) { return eof; }
  return current + offset;
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
