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
#include "zomlang/compiler/diagnostics/diagnostic-emitter.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/lexer/lexer.h"

namespace zomlang {
namespace compiler {
namespace parser {

TokenStream::TokenStream() = default;

TokenStream::TokenStream(const source::SourceManager& sourceMgr,
                         diagnostics::DiagnosticEmitter& diagnosticEngine,
                         const basic::LangOptions& langOpts, basic::StringPool& stringPool,
                         const source::BufferId& bufferId)
    : lexer(zc::heap<lexer::Lexer>(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId)) {}

TokenStream::~TokenStream() noexcept(false) = default;

ParsedTokenSnapshot::ParsedTokenSnapshot(const source::SourceManager& sources,
                                         const source::BufferId& buffer,
                                         zc::Array<ParsedTokenRange>&& tokens) noexcept
    : sourceManager(&sources), buffer(buffer), tokenValues(zc::mv(tokens)) {}

void TokenStream::reset(zc::ArrayPtr<const lexer::Token> newTokens) {
  lexer = nullptr;
  tokens.clear();
  tokens.addAll(newTokens);
  reachedEof = tokens.size() != 0 && tokens.back().is(ast::SyntaxKind::EndOfFile);
}

size_t TokenStream::bufferedSize() const { return tokens.size(); }

size_t TokenStream::bufferedTokenLimit() const {
  if (reachedEof && tokens.size() != 0) { return tokens.size() - 1; }
  return tokens.size();
}

bool TokenStream::hasBufferedEof() const { return reachedEof; }

const lexer::Token& TokenStream::tokenAt(size_t index) const {
  ensure(index);
  ZC_IREQUIRE(tokens.size() != 0, "token stream requires at least EOF");
  if (index >= tokens.size()) { return tokens[eofIndex()]; }
  return tokens[index];
}

ast::SyntaxKind TokenStream::kindAt(size_t index) const { return tokenAt(index).getKind(); }

size_t TokenStream::clampIndex(size_t index) const {
  ensure(index);
  if (reachedEof && index >= tokens.size()) { return eofIndex(); }
  return index;
}

zc::Array<ParsedTokenRange> TokenStream::copyBufferedTokenRanges() const {
  ZC_IREQUIRE(reachedEof && tokens.size() != 0 && tokens.back().is(ast::SyntaxKind::EndOfFile),
              "successful parse must retain its final EOF token");
  zc::Vector<ParsedTokenRange> ranges;
  for (const auto& token : tokens) {
    ranges.add(ParsedTokenRange(token.getKind(), token.getRange(), zc::str(token.getValue())));
  }
  return ranges.releaseAsArray();
}

void TokenStream::ensure(size_t index) const {
  while (!reachedEof && index >= tokens.size()) { lexNext(); }
}

void TokenStream::lexNext() const {
  ZC_IREQUIRE(lexer.get() != nullptr, "token stream cannot extend a fixed token buffer");
  lexer::Token token;
  lexer->lex(token);
  reachedEof = token.is(ast::SyntaxKind::EndOfFile);
  tokens.add(token);
}

size_t TokenStream::eofIndex() const {
  ZC_IREQUIRE(tokens.size() != 0, "token stream requires at least EOF");
  return tokens.size() - 1;
}

TokenCursor::TokenCursor(TokenStream& stream) { reset(stream); }

TokenCursor::ScopedSplitMode::ScopedSplitMode(TokenCursor& cursor)
    : cursor(cursor), previousSplitMode(cursor.splitMode_) {
  cursor.enableSplitMode();
}

TokenCursor::ScopedSplitMode::~ScopedSplitMode() {
  cursor.restoreScopedSplitMode(previousSplitMode);
}

void TokenCursor::reset(TokenStream& newStream) {
  stream = &newStream;
  current = 0;
  splitMode_ = false;
  splitRemaining_ = 0;
  splitOriginalKind_ = ast::SyntaxKind::Unknown;
}

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
    splitVirtualToken_ =
        lexer::Token(ast::SyntaxKind::GreaterThan, real.getRange(), ">"_zc, real.getFlags());
  }
}

ast::SyntaxKind TokenCursor::peek(size_t offset) const {
  primeSplitState();
  if (splitRemaining_ > 0) {
    if (offset < static_cast<size_t>(splitRemaining_)) { return ast::SyntaxKind::GreaterThan; }
    return tokenAt(relativeIndex(offset - static_cast<size_t>(splitRemaining_) + 1)).getKind();
  }
  return token(offset).getKind();
}

const lexer::Token& TokenCursor::token(size_t offset) const {
  primeSplitState();
  if (splitRemaining_ > 0) {
    if (offset < static_cast<size_t>(splitRemaining_)) { return splitVirtualToken_; }
    return tokenAt(relativeIndex(offset - static_cast<size_t>(splitRemaining_) + 1));
  }
  return tokenAt(relativeIndex(offset));
}

const lexer::Token& TokenCursor::tokenAt(size_t index) const {
  ZC_IREQUIRE(stream != nullptr, "token cursor requires a token stream");
  return stream->tokenAt(index);
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
  ZC_IREQUIRE(stream != nullptr, "token cursor requires a token stream");
  current = stream->clampIndex(index);
  // Moving to a new position aborts any in-progress split.
  splitRemaining_ = 0;
  splitOriginalKind_ = ast::SyntaxKind::Unknown;
}

bool TokenCursor::expect(ast::SyntaxKind kind, diagnostics::DiagnosticEmitter& diagnosticEngine,
                         zc::StringPtr expected) {
  if (eat(kind)) { return true; }
  diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(token().getLocation(), expected);
  return false;
}

TokenCursor::Mark TokenCursor::mark() const {
  return Mark{current, splitMode_, splitRemaining_, splitOriginalKind_, splitVirtualToken_};
}

void TokenCursor::rewind(Mark mark) {
  ZC_IREQUIRE(stream != nullptr, "token cursor requires a token stream");
  current = mark.current;
  splitMode_ = mark.splitMode;
  splitRemaining_ = mark.splitRemaining;
  splitOriginalKind_ = mark.splitOriginalKind;
  splitVirtualToken_ = mark.splitVirtualToken;
}

bool TokenCursor::isAtEnd() const { return peek() == ast::SyntaxKind::EndOfFile; }

// ---- Split mode API ----

void TokenCursor::enableSplitMode() {
  splitMode_ = true;
  // Prime immediately so the first peek()/token() call is consistent.
  primeSplitState();
}

TokenCursor::ScopedSplitMode TokenCursor::scopedSplitMode() { return ScopedSplitMode(*this); }

void TokenCursor::disableSplitMode() {
  splitMode_ = false;
  splitRemaining_ = 0;
  splitOriginalKind_ = ast::SyntaxKind::Unknown;
}

bool TokenCursor::isSplitModeActive() const { return splitMode_; }

void TokenCursor::restoreScopedSplitMode(bool wasActive) {
  splitMode_ = wasActive;
  if (!splitMode_) {
    splitRemaining_ = 0;
    splitOriginalKind_ = ast::SyntaxKind::Unknown;
  }
}

// ---- Internals ----

size_t TokenCursor::relativeIndex(size_t offset) const {
  ZC_IREQUIRE(stream != nullptr, "token cursor requires a token stream");
  if (offset > static_cast<size_t>(-1) - current) {
    return stream->clampIndex(static_cast<size_t>(-1));
  }
  return stream->clampIndex(current + offset);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
