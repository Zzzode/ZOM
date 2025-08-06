// Copyright (c) 2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/lexer/lexer.h"

#include <cctype>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace lexer {

source::SourceManager& getSourceManager() {
  static zc::Own<source::SourceManager> sourceManager = zc::heap<source::SourceManager>();
  return *sourceManager;
}

zc::Vector<Token> tokenize(zc::StringPtr source) {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  zc::Vector<Token> tokens;
  auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token token;
  // skip unknown
  lexer.lex(token);
  do {
    lexer.lex(token);
    tokens.add(token);
  } while (token.getKind() != TokenKind::kEOF);

  return tokens;
}

ZC_TEST("LexerTest.IdentifierTokenization") {
  auto tokens = tokenize("identifier _validName $dollarId"_zc);
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(TokenKind::kIdentifier));
  ZC_EXPECT(tokens[1].is(TokenKind::kIdentifier));
  ZC_EXPECT(tokens[2].is(TokenKind::kIdentifier));
  ZC_EXPECT(tokens[3].is(TokenKind::kEOF));
}

ZC_TEST("LexerTest.KeywordTokenization") {
  auto tokens = tokenize("fun if else while for struct enum"_zc);
  ZC_EXPECT(tokens.size() == 8);
  ZC_EXPECT(tokens[0].is(TokenKind::kFunKeyword));
  ZC_EXPECT(tokens[1].is(TokenKind::kIfKeyword));
  ZC_EXPECT(tokens[2].is(TokenKind::kElseKeyword));
  ZC_EXPECT(tokens[3].is(TokenKind::kWhileKeyword));
  ZC_EXPECT(tokens[4].is(TokenKind::kForKeyword));
  ZC_EXPECT(tokens[5].is(TokenKind::kStructKeyword));
  ZC_EXPECT(tokens[6].is(TokenKind::kEnumKeyword));
  ZC_EXPECT(tokens[7].is(TokenKind::kEOF));
}

ZC_TEST("LexerTest.NumericLiteralTokenization") {
  auto tokens = tokenize("42 3.14 0xDEADBEEF 0b1010 0o755"_zc);
  ZC_EXPECT(tokens.size() == 6);
  ZC_EXPECT(tokens[0].is(TokenKind::kIntegerLiteral));
  ZC_EXPECT(tokens[1].is(TokenKind::kFloatLiteral));
  ZC_EXPECT(tokens[2].is(TokenKind::kIntegerLiteral));
  ZC_EXPECT(tokens[3].is(TokenKind::kIntegerLiteral));
  ZC_EXPECT(tokens[4].is(TokenKind::kIntegerLiteral));
  ZC_EXPECT(tokens[5].is(TokenKind::kEOF));
}

ZC_TEST("LexerTest.StringLiteralTokenization") {
  auto tokens = tokenize("\"hello\" \"with\\na\" \"escape\"\n");
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(TokenKind::kStringLiteral));
  ZC_EXPECT(tokens[1].is(TokenKind::kStringLiteral));
  ZC_EXPECT(tokens[2].is(TokenKind::kStringLiteral));
  ZC_EXPECT(tokens[3].is(TokenKind::kEOF));
}

ZC_TEST("LexerTest.OperatorTokenization") {
  auto tokens = tokenize("+ - * / ++ -- == != <= >= && || ! ? :"_zc);
  ZC_EXPECT(tokens.size() == 16);
  ZC_EXPECT(tokens[0].is(TokenKind::kPlus));
  ZC_EXPECT(tokens[1].is(TokenKind::kMinus));
  ZC_EXPECT(tokens[2].is(TokenKind::kAsterisk));
  ZC_EXPECT(tokens[3].is(TokenKind::kSlash));
  ZC_EXPECT(tokens[4].is(TokenKind::kPlusPlus));
  ZC_EXPECT(tokens[5].is(TokenKind::kMinusMinus));
  ZC_EXPECT(tokens[6].is(TokenKind::kEqualsEquals));
  ZC_EXPECT(tokens[7].is(TokenKind::kExclamationEquals));
  ZC_EXPECT(tokens[8].is(TokenKind::kLessThanEquals));
  ZC_EXPECT(tokens[9].is(TokenKind::kGreaterThanEquals));
  ZC_EXPECT(tokens[10].is(TokenKind::kAmpersandAmpersand));
  ZC_EXPECT(tokens[11].is(TokenKind::kBarBar));
  ZC_EXPECT(tokens[12].is(TokenKind::kExclamation));
  ZC_EXPECT(tokens[13].is(TokenKind::kQuestion));
  ZC_EXPECT(tokens[14].is(TokenKind::kColon));
  ZC_EXPECT(tokens[15].is(TokenKind::kEOF));
}

ZC_TEST("LexerTest.CommentHandling") {
  auto tokens = tokenize(
      "// This is a comment\n"
      "codeAfter // Trailing comment\n"
      "/* Multi-line\ncomment */ remaining"_zc);
  ZC_EXPECT(tokens.size() >= 1);
  ZC_EXPECT(tokens[tokens.size() - 1].is(TokenKind::kEOF));
}

ZC_TEST("LexerTest.ErrorHandling") {
  auto tokens = tokenize("#invalid");
  ZC_EXPECT(tokens.size() >= 1);
  ZC_EXPECT(tokens[tokens.size() - 1].is(TokenKind::kEOF));
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang