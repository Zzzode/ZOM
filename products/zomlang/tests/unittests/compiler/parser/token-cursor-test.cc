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

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

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

}  // namespace

ZC_TEST("TokenCursorTest.PeekAndTokenClampToEof") {
  auto tokens = makeTokenStream();
  TokenCursor cursor(tokens.asPtr());

  ZC_EXPECT(cursor.size() == 4);
  ZC_EXPECT(cursor.position() == 0);
  ZC_EXPECT(cursor.peek() == ast::SyntaxKind::LetKeyword);
  ZC_EXPECT(cursor.peek(1) == ast::SyntaxKind::Identifier);
  ZC_EXPECT(cursor.peek(99) == ast::SyntaxKind::EndOfFile);
  ZC_EXPECT(cursor.tokenAt(99).is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("TokenCursorTest.EatMarkAndRewind") {
  auto tokens = makeTokenStream();
  TokenCursor cursor(tokens.asPtr());

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
  TokenCursor cursor(tokens.asPtr());

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
  TokenCursor cursor(tokens.asPtr());
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
  TokenCursor cursor(tokens.asPtr());

  ZC_EXPECT(cursor.eat(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(cursor.eat(ast::SyntaxKind::Identifier));
  ZC_EXPECT(cursor.eat(ast::SyntaxKind::Equals));
  ZC_EXPECT(cursor.isAtEnd());
  ZC_EXPECT(cursor.position() == 3);
  ZC_EXPECT(cursor.eat(ast::SyntaxKind::EndOfFile));
  ZC_EXPECT(cursor.position() == 3);
  ZC_EXPECT(cursor.isAtEnd());
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
