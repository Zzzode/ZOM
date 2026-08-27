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

#include "compiler/parser/token-cursor.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace parser {

namespace {

lexer::Token makeToken(ast::SyntaxKind kind, zc::StringPtr text = ""_zc) {
  return lexer::Token(kind, source::SourceRange(), text);
}

zc::Vector<lexer::Token> makeTokenStream() {
  zc::Vector<lexer::Token> tokens;
  tokens.add(makeToken(ast::SyntaxKind::LetKeyword, "let"_zc));
  tokens.add(makeToken(ast::SyntaxKind::Identifier, "value"_zc));
  tokens.add(makeToken(ast::SyntaxKind::Equals, "="_zc));
  tokens.add(makeToken(ast::SyntaxKind::EndOfFile));
  return tokens;
}

zc::Vector<lexer::Token> makeRightShiftTokenStream(ast::SyntaxKind shiftKind, zc::StringPtr text) {
  zc::Vector<lexer::Token> tokens;
  tokens.add(makeToken(shiftKind, text));
  tokens.add(makeToken(ast::SyntaxKind::Identifier, "after"_zc));
  tokens.add(makeToken(ast::SyntaxKind::EndOfFile));
  return tokens;
}

}  // namespace

ZC_TEST("TokenCursorTest.PeekAndTokenClampToEof") {
  auto tokens = makeTokenStream();
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);

  ZC_EXPECT(stream.bufferedTokenLimit() == 3);
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::LetKeyword);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::Identifier);
  ZC_EXPECT(cursor.peek(99) == ast::SyntaxKind::EndOfFile);
  ZC_EXPECT(cursor.tokenAt(99).is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("TokenStreamTest.LexesOnDemand") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto bufferId = sourceManager->addMemBufferCopy("let value = 1;"_zc.asBytes(), "lazy-stream.zom");
  diagnostics::DiagnosticEngine diagnosticEngine(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;
  TokenStream stream(*sourceManager, diagnosticEngine, langOpts, stringPool, bufferId);
  TokenCursor cursor(stream);

  ZC_EXPECT(stream.bufferedSize() == 0);
  ZC_EXPECT(!stream.hasBufferedEof());

  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::LetKeyword);
  ZC_EXPECT(stream.bufferedSize() == 1);
  ZC_EXPECT(!stream.hasBufferedEof());

  ZC_EXPECT(cursor.peek(2) == ast::SyntaxKind::Equals);
  ZC_EXPECT(stream.bufferedSize() == 3);
  ZC_EXPECT(!stream.hasBufferedEof());

  cursor.moveTo(99);
  ZC_EXPECT(cursor.isAtEnd());
  ZC_EXPECT(stream.hasBufferedEof());
}

ZC_TEST("TokenCursorTest.EatMarkAndRewind") {
  auto tokens = makeTokenStream();
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);

  const TokenCursor::Mark start = cursor.mark();
  ZC_EXPECT(cursor.at(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(cursor.eat(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(cursor.position() == 1);
  ZC_EXPECT(cursor.at(ast::SyntaxKind::Identifier));

  cursor.rewind(start);
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.at(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(!cursor.eat(ast::SyntaxKind::Semicolon));
  ZC_EXPECT(cursor.position() == 0);
}

ZC_TEST("TokenCursorTest.AdvanceAndMoveTo") {
  auto tokens = makeTokenStream();
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);

  cursor.advance();
  ZC_EXPECT(cursor.position() == 1);
  ZC_EXPECT(cursor.at(ast::SyntaxKind::Identifier));

  cursor.moveTo(2);
  ZC_EXPECT(cursor.position() == 2);
  ZC_EXPECT(cursor.at(ast::SyntaxKind::Equals));

  cursor.moveTo(3);
  ZC_EXPECT(cursor.isAtEnd());
  cursor.advance();
  ZC_EXPECT(cursor.position() == 3);
}

ZC_TEST("TokenCursorTest.ExpectConsumesOrDiagnoses") {
  auto tokens = makeTokenStream();
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);
  auto sourceManager = zc::heap<source::SourceManager>();
  diagnostics::DiagnosticEngine diagnosticEngine(*sourceManager);

  ZC_EXPECT(cursor.expect(ast::SyntaxKind::LetKeyword, diagnosticEngine, "let"_zc));
  ZC_EXPECT(cursor.position() == 1);
  ZC_EXPECT(!diagnosticEngine.hasErrors());

  ZC_EXPECT(!cursor.expect(ast::SyntaxKind::Semicolon, diagnosticEngine, ";"_zc));
  ZC_EXPECT(cursor.position() == 1);
  ZC_EXPECT(diagnosticEngine.hasErrors());
}

ZC_TEST("TokenCursorTest.EofDoesNotAdvancePastEnd") {
  auto tokens = makeTokenStream();
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);

  ZC_EXPECT(cursor.eat(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(cursor.eat(ast::SyntaxKind::Identifier));
  ZC_EXPECT(cursor.eat(ast::SyntaxKind::Equals));
  ZC_EXPECT(cursor.isAtEnd());
  ZC_EXPECT(cursor.position() == 3);
  ZC_EXPECT(cursor.eat(ast::SyntaxKind::EndOfFile));
  ZC_EXPECT(cursor.position() == 3);
  ZC_EXPECT(cursor.isAtEnd());
}

ZC_TEST("TokenCursorTest.SplitModeExposesVirtualLookahead") {
  auto tokens =
      makeRightShiftTokenStream(ast::SyntaxKind::GreaterThanGreaterThanGreaterThan, ">>>"_zc);
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);
  cursor.enableSplitMode();

  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(2) == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(3) == ast::SyntaxKind::Identifier);

  cursor.advance();
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(2) == ast::SyntaxKind::Identifier);

  cursor.advance();
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::Identifier);

  cursor.advance();
  ZC_EXPECT(cursor.position() == 1);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::Identifier);
}

ZC_TEST("TokenCursorTest.MarkAndRewindRestoreSplitState") {
  auto tokens =
      makeRightShiftTokenStream(ast::SyntaxKind::GreaterThanGreaterThanGreaterThan, ">>>"_zc);
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);
  cursor.enableSplitMode();

  cursor.advance();
  const TokenCursor::Mark afterFirstVirtualGreater = cursor.mark();

  cursor.advance();
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::Identifier);

  cursor.rewind(afterFirstVirtualGreater);
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(2) == ast::SyntaxKind::Identifier);

  cursor.advance();
  cursor.advance();
  ZC_EXPECT(cursor.position() == 1);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::Identifier);
}

ZC_TEST("TokenCursorTest.ScopedSplitModeRestoresPreviousMode") {
  auto tokens = makeRightShiftTokenStream(ast::SyntaxKind::GreaterThanGreaterThan, ">>"_zc);
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);

  {
    TokenCursor::ScopedSplitMode splitMode(cursor);
    ZC_EXPECT(cursor.isSplitModeActive());
    ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThan);
    ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::GreaterThan);
  }

  ZC_EXPECT(!cursor.isSplitModeActive());
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThanGreaterThan);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::Identifier);
}

ZC_TEST("TokenCursorTest.NestedScopedSplitModeKeepsConsumedVirtualClosers") {
  auto tokens =
      makeRightShiftTokenStream(ast::SyntaxKind::GreaterThanGreaterThanGreaterThan, ">>>"_zc);
  TokenStream stream;
  stream.reset(tokens.asPtr());
  TokenCursor cursor(stream);
  cursor.enableSplitMode();

  {
    TokenCursor::ScopedSplitMode nested(cursor);
    cursor.advance();
  }

  ZC_EXPECT(cursor.isSplitModeActive());
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::GreaterThan);
  ZC_EXPECT(cursor.peek(2) == ast::SyntaxKind::Identifier);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
