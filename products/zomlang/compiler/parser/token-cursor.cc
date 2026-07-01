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
  splitMode_ = false;
  splitRemaining_ = 0;
  splitOriginalKind_ = ast::SyntaxKind::Unknown;
}

size_t TokenCursor::size() const { return tokens.size(); }

size_t TokenCursor::position() const { return current; }

int TokenCursor::rightAngleCount(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::GreaterThan:
      return 1;
    case ast::SyntaxKind::GreaterThanGreaterThan:
      return 2;
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThan:
      return 3;
    default:
      return 0;
  }
}

void TokenCursor::primeSplitState() const {
  if (!splitMode_ || splitRemaining_ > 0) { return; }

  const lexer::Token& real = tokenAt(relativeIndex(0));
  const int count = rightAngleCount(real.getKind());
  if (count > 1) {
    splitRemaining_ = count;
    splitOriginalKind_ = real.getKind();
    // Create a virtual > token using the original range for diagnostics.
    // The value is ">" to reflect the virtual single character.
    splitVirtualToken_ = lexer::Token(ast::SyntaxKind::GreaterThan, real.getRange(), ">"_zc,
                                      real.getFlags());
  }
}

ast::SyntaxKind TokenCursor::peek(size_t offset) const {
  if (offset == 0) {
    primeSplitState();
    if (splitRemaining_ > 0) { return ast::SyntaxKind::GreaterThan; }
  }
  return token(offset).getKind();
}

const lexer::Token& TokenCursor::token(size_t offset) const {
  if (offset == 0) {
    primeSplitState();
    if (splitRemaining_ > 0) { return splitVirtualToken_; }
  }
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
  if (isAtEnd()) { return; }

  primeSplitState();
  if (splitRemaining_ > 0) {
    --splitRemaining_;
    if (splitRemaining_ <= 0) {
      // Exhausted the virtual tokens; move to next real token.
      splitRemaining_ = 0;
      splitOriginalKind_ = ast::SyntaxKind::Unknown;
      ++current;
    }
    return;
  }

  ++current;
}

void TokenCursor::moveTo(size_t index) {
  ZC_IREQUIRE(tokens.size() != 0, "token cursor requires a token stream with EOF");
  ZC_IREQUIRE(index < tokens.size(), "token cursor target outside token stream");
  current = index;
  // Moving to a new position aborts any in-progress split.
  splitRemaining_ = 0;
  splitOriginalKind_ = ast::SyntaxKind::Unknown;
}

bool TokenCursor::expect(ast::SyntaxKind kind, diagnostics::DiagnosticEngine& diagnosticEngine,
                         zc::StringPtr expected) {
  if (eat(kind)) { return true; }
  diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(token().getLocation(), expected);
  return false;
}

TokenCursor::Mark TokenCursor::mark() const { return current; }

void TokenCursor::rewind(Mark mark) {
  moveTo(mark);
  // Rewind aborts any in-progress split; the caller re-primes as needed.
}

bool TokenCursor::isAtEnd() const { return peek() == ast::SyntaxKind::EndOfFile; }

// ---- Split mode API ----

void TokenCursor::enableSplitMode() {
  splitMode_ = true;
  // Prime immediately so the first peek()/token() call is consistent.
  primeSplitState();
}

void TokenCursor::disableSplitMode() {
  splitMode_ = false;
  splitRemaining_ = 0;
  splitOriginalKind_ = ast::SyntaxKind::Unknown;
}

bool TokenCursor::isSplitModeActive() const { return splitMode_; }

// ---- Internals ----

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
