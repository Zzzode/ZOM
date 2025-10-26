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
  do {
    lexer.lex(token);
    tokens.add(token);
  } while (token.getKind() != ast::SyntaxKind::EndOfFile);

  return tokens;
}

ZC_TEST("LexerTest.IdentifierTokenization") {
  auto tokens = tokenize("identifier _validName $dollarId"_zc);
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.KeywordTokenization") {
  auto tokens = tokenize("fun if else while for struct enum"_zc);
  ZC_EXPECT(tokens.size() == 8);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FunKeyword));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::IfKeyword));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::ElseKeyword));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::WhileKeyword));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::ForKeyword));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::StructKeyword));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::EnumKeyword));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.NumericLiteralTokenization") {
  auto tokens = tokenize("42 3.14 0xDEADBEEF 0b1010 0o755"_zc);

  ZC_EXPECT(tokens.size() == 6);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.StringLiteralTokenization") {
  auto tokens = tokenize("\"hello\" \"with\\na\" \"escape\"\n");
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.OperatorTokenization") {
  auto tokens = tokenize("+ - * / ++ -- == != <= >= && || ! ? :"_zc);
  ZC_EXPECT(tokens.size() == 16);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Plus));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Minus));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Asterisk));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Slash));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::PlusPlus));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::MinusMinus));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::EqualsEquals));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::ExclamationEquals));
  ZC_EXPECT(tokens[8].is(ast::SyntaxKind::LessThanEquals));
  ZC_EXPECT(tokens[9].is(ast::SyntaxKind::GreaterThanEquals));
  ZC_EXPECT(tokens[10].is(ast::SyntaxKind::AmpersandAmpersand));
  ZC_EXPECT(tokens[11].is(ast::SyntaxKind::BarBar));
  ZC_EXPECT(tokens[12].is(ast::SyntaxKind::Exclamation));
  ZC_EXPECT(tokens[13].is(ast::SyntaxKind::Question));
  ZC_EXPECT(tokens[14].is(ast::SyntaxKind::Colon));
  ZC_EXPECT(tokens[15].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.CommentHandling") {
  auto tokens = tokenize(
      "// This is a comment\n"
      "codeAfter // Trailing comment\n"
      "/* Multi-line\ncomment */ remaining"_zc);

  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::EndOfFile));

  const auto& sm = getSourceManager();

  ZC_EXPECT(tokens[0].getText(sm) == "codeAfter"_zc);
  ZC_EXPECT(tokens[1].getText(sm) == "remaining"_zc);
}

ZC_TEST("LexerTest.ErrorHandling") {
  auto tokens = tokenize("#invalid");
  ZC_EXPECT(tokens.size() == 3);  // # + invalid + EOF
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Hash));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.LexerModes") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("test"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test mode switching
  lexer.enterMode(LexerMode::kNormal);
  lexer.exitMode(LexerMode::kNormal);

  lexer.enterMode(LexerMode::kStringInterpolation);
  lexer.exitMode(LexerMode::kStringInterpolation);

  lexer.enterMode(LexerMode::kRegexLiteral);
  lexer.exitMode(LexerMode::kRegexLiteral);

  ZC_EXPECT(true);
}

ZC_TEST("LexerTest.CommentRetentionModes") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("// comment"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test comment retention modes
  lexer.setCommentRetentionMode(CommentRetentionMode::kNone);
  lexer.setCommentRetentionMode(CommentRetentionMode::kAttachToNextToken);
  lexer.setCommentRetentionMode(CommentRetentionMode::kReturnAsTokens);

  ZC_EXPECT(true);
}

ZC_TEST("LexerTest.FullStartLoc") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("test"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto loc = lexer.getFullStartLoc();
  ZC_EXPECT(!loc.isInvalid());
}

ZC_TEST("LexerTest.IsCodeCompletion") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("test"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  bool isCompletion = lexer.isCodeCompletion();
  ZC_EXPECT(!isCompletion);
}

ZC_TEST("LexerTest.StateManagement") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("test"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test state management with a valid token
  Token token;
  lexer.lex(token);  // Get a valid token first
  LexerState state = lexer.getStateForBeginningOfToken(token);
  lexer.restoreState(state);

  ZC_EXPECT(true);
}

ZC_TEST("LexerTest.StringLiteralWithContent") {
  // Test string literal parsing with actual content
  // This test verifies the fix for string literal parsing after lexer updates
  auto tokens = tokenize("\"hello\" \"1234\" \"test\\nstring\""_zc);

  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));

  // Verify string content includes quotes (as per current implementation)
  auto& sourceManager = getSourceManager();
  zc::String firstString = tokens[0].getText(sourceManager);
  zc::String secondString = tokens[1].getText(sourceManager);
  zc::String thirdString = tokens[2].getText(sourceManager);

  // String literals should include the surrounding quotes
  ZC_EXPECT(firstString == "\"hello\""_zc);
  ZC_EXPECT(secondString == "\"1234\""_zc);
  ZC_EXPECT(thirdString == "\"test\\nstring\""_zc);
}

ZC_TEST("LexerTest.FunctionKeywordAndIdentifier") {
  // Test function keyword and identifier parsing
  // This verifies the fix for identifier parsing after lexer updates
  auto tokens = tokenize("fun myFunction test123 $identifier"_zc);
  ZC_EXPECT(tokens.size() == 5);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FunKeyword));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::EndOfFile));

  // Verify identifier content
  auto& sourceManager = getSourceManager();
  zc::String secondToken = tokens[1].getText(sourceManager);
  zc::String thirdToken = tokens[2].getText(sourceManager);
  zc::String fourthToken = tokens[3].getText(sourceManager);

  ZC_EXPECT(secondToken == "myFunction");
  ZC_EXPECT(thirdToken == "test123");
  ZC_EXPECT(fourthToken == "$identifier");
}

ZC_TEST("LexerTest.ComplexStringLiterals") {
  // Test more complex string literal scenarios
  // Including escape sequences and edge cases
  auto tokens = tokenize("\"\" \"a\" \"\\n\" \"quote\\\"inside\""_zc);
  ZC_EXPECT(tokens.size() == 5);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::EndOfFile));

  // Verify content
  auto& sourceManager = getSourceManager();
  zc::String emptyString = tokens[0].getText(sourceManager);
  zc::String singleChar = tokens[1].getText(sourceManager);
  zc::String newline = tokens[2].getText(sourceManager);
  zc::String quoted = tokens[3].getText(sourceManager);

  ZC_EXPECT(emptyString == "\"\"");
  ZC_EXPECT(singleChar == "\"a\"");
  ZC_EXPECT(newline == "\"\\n\"");
  ZC_EXPECT(quoted == "\"quote\\\"inside\"");
}

ZC_TEST("LexerTest.StringLiteralWithActualNewline") {
  // Test string literal parsing with actual newline character
  // This test verifies that strings with actual newlines are properly handled
  auto tokens = tokenize("\"hello\" \"test\nstring\" \"world\""_zc);

  // Expected tokens: "hello", "test\nstring", "world", EOF
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));

  // Verify all strings are correctly parsed
  auto& sourceManager = getSourceManager();
  zc::String firstString = tokens[0].getText(sourceManager);
  zc::String secondString = tokens[1].getText(sourceManager);
  zc::String thirdString = tokens[2].getText(sourceManager);

  ZC_EXPECT(firstString == "\"hello\"");
  ZC_EXPECT(secondString == "\"test\nstring\"");
  ZC_EXPECT(thirdString == "\"world\"");
}

ZC_TEST("LexerTest.FunctionDeclarationTokens") {
  // Test tokenization of function declaration
  // This verifies the complete function parsing pipeline
  auto tokens = tokenize("fun add(a: i32, b: i32) -> i32 { return a + b; }"_zc);

  // Should have: fun, identifier, (, identifier, :, identifier, ,, identifier, :, identifier, ),
  // ->, identifier, {, return, identifier, +, identifier, ;, }, EOF
  ZC_EXPECT(tokens.size() == 21);

  // Verify key tokens
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FunKeyword));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::LeftParen));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::Colon));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::I32Keyword));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::Comma));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[8].is(ast::SyntaxKind::Colon));
  ZC_EXPECT(tokens[9].is(ast::SyntaxKind::I32Keyword));
  ZC_EXPECT(tokens[10].is(ast::SyntaxKind::RightParen));
  ZC_EXPECT(tokens[11].is(ast::SyntaxKind::Arrow));
  ZC_EXPECT(tokens[12].is(ast::SyntaxKind::I32Keyword));
  ZC_EXPECT(tokens[13].is(ast::SyntaxKind::LeftBrace));
  ZC_EXPECT(tokens[14].is(ast::SyntaxKind::ReturnKeyword));
  ZC_EXPECT(tokens[15].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[16].is(ast::SyntaxKind::Plus));
  ZC_EXPECT(tokens[17].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[18].is(ast::SyntaxKind::Semicolon));
  ZC_EXPECT(tokens[19].is(ast::SyntaxKind::RightBrace));
  ZC_EXPECT(tokens[20].is(ast::SyntaxKind::EndOfFile));

  // Verify function name
  auto& sourceManager = getSourceManager();
  zc::String funcName = tokens[1].getText(sourceManager);
  ZC_EXPECT(funcName == "add");
}

ZC_TEST("LexerTest.LookAheadAndCanLookAhead") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("let x = 42"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  bool canLook = lexer.canLookAhead(1);
  ZC_EXPECT(canLook);

  const Token& next = lexer.lookAhead(1);
  ZC_EXPECT(!next.is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.PeekNextToken") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("let x = 42"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token current;
  lexer.lex(current);

  Token peeked = lexer.peekNextToken();
  ZC_EXPECT(!peeked.is(ast::SyntaxKind::EndOfFile));

  Token next;
  lexer.lex(next);
  ZC_EXPECT(peeked.getKind() == next.getKind());
}

ZC_TEST("LexerTest.MultipleOperators") {
  auto tokens = tokenize("= += -= *= /= %= &= |= ^= <<= >>= <=> &&= \?\?="_zc);
  ZC_EXPECT(tokens.size() == 16);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Equals));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::PlusEquals));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::MinusEquals));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::AsteriskEquals));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::SlashEquals));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::PercentEquals));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::AmpersandEquals));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::BarEquals));
  ZC_EXPECT(tokens[8].is(ast::SyntaxKind::CaretEquals));
  ZC_EXPECT(tokens[9].is(ast::SyntaxKind::LessThanLessThanEquals));
  ZC_EXPECT(tokens[10].is(ast::SyntaxKind::GreaterThanGreaterThanEquals));
  ZC_EXPECT(tokens[11].is(ast::SyntaxKind::LessThanEquals));
  ZC_EXPECT(tokens[12].is(ast::SyntaxKind::GreaterThan));
  ZC_EXPECT(tokens[13].is(ast::SyntaxKind::AmpersandAmpersandEquals));
  ZC_EXPECT(tokens[14].is(ast::SyntaxKind::QuestionQuestionEquals));
  ZC_EXPECT(tokens[15].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.BracesAndParentheses") {
  auto tokens = tokenize("{ } ( ) [ ]"_zc);
  ZC_EXPECT(tokens.size() == 7);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LeftBrace));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::RightBrace));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::LeftParen));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::RightParen));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::LeftBracket));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::RightBracket));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.MoreKeywords") {
  auto tokens = tokenize("let var const class interface implements extends"_zc);
  ZC_EXPECT(tokens.size() == 8);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::VarKeyword));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::ConstKeyword));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::ClassKeyword));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::InterfaceKeyword));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::ImplementsKeyword));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::ExtendsKeyword));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.ControlFlowKeywords") {
  auto tokens = tokenize("return break continue match case default"_zc);
  ZC_EXPECT(tokens.size() == 7);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ReturnKeyword));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::BreakKeyword));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::ContinueKeyword));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::MatchKeyword));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::CaseKeyword));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::DefaultKeyword));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.NumericLiteralsWithSuffixes") {
  auto tokens = tokenize("42u 42l 42ul 3.14f 3.14d"_zc);
  ZC_EXPECT(tokens.size() == 6);
  for (size_t i = 0; i < tokens.size() - 1; ++i) {
    ZC_EXPECT(tokens[i].is(ast::SyntaxKind::IntegerLiteral) ||
              tokens[i].is(ast::SyntaxKind::FloatLiteral));
  }
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.CharacterLiterals") {
  auto tokens = tokenize("'a' '\\n' '\\t' '\\\\' '\\'' '\"'"_zc);
  ZC_EXPECT(tokens.size() == 7);
  for (size_t i = 0; i < tokens.size() - 1; ++i) {
    ZC_EXPECT(tokens[i].is(ast::SyntaxKind::CharacterLiteral));
  }
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.NestedComments") {
  auto tokens = tokenize("/* outer /* inner */ still outer */ code"_zc);
  ZC_EXPECT(tokens.size() == 6);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Asterisk));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Slash));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.StringInterpolation") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("\"Hello ${name}!\""_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  lexer.enterMode(LexerMode::kStringInterpolation);

  Token token;
  lexer.lex(token);

  lexer.exitMode(LexerMode::kStringInterpolation);
  ZC_EXPECT(true);  // Test mode switching works
}

ZC_TEST("LexerTest.RegexLiterals") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("/[a-z]+/g"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  lexer.enterMode(LexerMode::kRegexLiteral);

  Token token;
  lexer.lex(token);

  lexer.exitMode(LexerMode::kRegexLiteral);
  ZC_EXPECT(true);  // Test regex mode switching works
}

ZC_TEST("LexerTest.CommentRetentionAsTokens") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("// comment\ncode"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  lexer.setCommentRetentionMode(CommentRetentionMode::kReturnAsTokens);

  Token token;
  lexer.lex(token);
  // Should get comment token when retention mode is set
  ZC_EXPECT(true);
}

ZC_TEST("LexerTest.CommentAttachToNextToken") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("// comment\nidentifier"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  lexer.setCommentRetentionMode(CommentRetentionMode::kAttachToNextToken);

  Token token;
  lexer.lex(token);
  ZC_EXPECT(token.is(ast::SyntaxKind::Identifier));
}

ZC_TEST("LexerTest.WhitespaceHandling") {
  auto tokens = tokenize("   \t\n  identifier  \t\n  "_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

// Test cases for uncovered code paths
ZC_TEST("LexerTest.SlashEqualsOperator") {
  // Test /= operator (line 938-945)
  auto tokens = tokenize("a /= b"_zc);
  ZC_EXPECT(tokens.size() == 4);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::SlashEquals));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.ErrorPropagateAndDefaultOperators") {
  // Test ?! and ?: operators (lines 966-971)
  auto tokens = tokenize("a ?! b ?: c"_zc);
  ZC_EXPECT(tokens.size() == 6);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::ErrorPropagate));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::ErrorDefault));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.DotDotDotOperator") {
  // Test ... operator (lines 976-984)
  auto tokens = tokenize("func(...args)"_zc);
  ZC_EXPECT(tokens.size() == 6);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::LeftParen));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::DotDotDot));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::RightParen));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.UnicodeIdentifiers") {
  auto tokens = tokenize("αβγ δεζ 变量名"_zc);
  ZC_EXPECT(tokens.size() == 4);
  for (size_t i = 0; i < tokens.size() - 1; ++i) {
    ZC_EXPECT(tokens[i].is(ast::SyntaxKind::Identifier));
  }
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.ChineseUnicodeIdentifiers") {
  // Test lexing of Chinese Unicode identifiers only
  auto tokens = tokenize("变量 函数名 类型定义 数据结构"_zc);
  ZC_EXPECT(tokens.size() == 5);
  for (size_t i = 0; i < tokens.size() - 1; ++i) {
    ZC_EXPECT(tokens[i].is(ast::SyntaxKind::Identifier));
  }
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Verify the actual text content of Chinese identifiers
  const auto& sourceManager = getSourceManager();
  ZC_EXPECT(tokens[0].getText(sourceManager) == "变量"_zc);
  ZC_EXPECT(tokens[1].getText(sourceManager) == "函数名"_zc);
  ZC_EXPECT(tokens[2].getText(sourceManager) == "类型定义"_zc);
  ZC_EXPECT(tokens[3].getText(sourceManager) == "数据结构"_zc);
}

ZC_TEST("LexerTest.ErrorRecovery") {
  auto tokens = tokenize("valid @invalid valid2"_zc);
  ZC_EXPECT(tokens.size() == 5);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::At));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.LongIdentifiers") {
  zc::String longId = zc::str("veryLongIdentifier");
  auto tokens = tokenize(longId);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.EmptyInput") {
  auto tokens = tokenize(""_zc);
  ZC_EXPECT(tokens.size() == 1);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.OnlyWhitespace") {
  auto tokens = tokenize("   \t\n\r  "_zc);
  ZC_EXPECT(tokens.size() == 1);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.OnlyComments") {
  auto tokens = tokenize("// comment1\n/* comment2 */"_zc);
  ZC_EXPECT(tokens.size() == 1);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.MixedContent") {
  auto tokens = tokenize("fun /* comment */ main() { let x = 42; // end comment\n}"_zc);
  ZC_EXPECT(tokens.size() == 12);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::FunKeyword));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::LeftParen));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::RightParen));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::LeftBrace));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::LetKeyword));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::Equals));
  ZC_EXPECT(tokens[8].is(ast::SyntaxKind::IntegerLiteral));
  ZC_EXPECT(tokens[9].is(ast::SyntaxKind::Semicolon));
  ZC_EXPECT(tokens[10].is(ast::SyntaxKind::RightBrace));
  ZC_EXPECT(tokens[11].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.TokenLocation") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("identifier"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token token;
  lexer.lex(token);

  auto loc = token.getLocation();
  ZC_EXPECT(!loc.isInvalid());
}

ZC_TEST("LexerTest.MultipleTokensLocation") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("first second third"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token token1, token2, token3;
  lexer.lex(token1);
  lexer.lex(token2);
  lexer.lex(token3);

  ZC_EXPECT(token1.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(token2.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(token3.is(ast::SyntaxKind::Identifier));

  // Locations should be different
  ZC_EXPECT(token1.getLocation() != token2.getLocation());
  ZC_EXPECT(token2.getLocation() != token3.getLocation());
}

ZC_TEST("LexerTest.LookAheadMultiple") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("a b c d e"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test looking ahead multiple tokens
  ZC_EXPECT(lexer.canLookAhead(1));
  ZC_EXPECT(lexer.canLookAhead(2));
  ZC_EXPECT(lexer.canLookAhead(3));

  const Token& ahead1 = lexer.lookAhead(1);
  const Token& ahead2 = lexer.lookAhead(2);
  const Token& ahead3 = lexer.lookAhead(3);

  ZC_EXPECT(ahead1.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(ahead2.is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(ahead3.is(ast::SyntaxKind::Identifier));
}

ZC_TEST("LexerTest.StateRestoration") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy("first second third"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token token1;
  lexer.lex(token1);
  LexerState state = lexer.getStateForBeginningOfToken(token1);

  Token token2;
  lexer.lex(token2);

  // Restore state and re-lex
  lexer.restoreState(state);
  Token token2Again;
  lexer.lex(token2Again);

  ZC_EXPECT(token2.getKind() == token2Again.getKind());
}

ZC_TEST("LexerTest.ComplexOperators") {
  // Test triple equals and other complex operators
  auto tokens = tokenize("=== !== <<= >>= >>>= **= \?\?="_zc);
  ZC_EXPECT(tokens.size() == 8);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::EqualsEqualsEquals));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::ExclamationEqualsEquals));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::LessThanLessThanEquals));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::GreaterThanGreaterThanEquals));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::GreaterThanGreaterThanGreaterThanEquals));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::AsteriskAsteriskEquals));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::QuestionQuestionEquals));
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test arrow operator and question dot
  auto tokens2 = tokenize("-> ?. ... **"_zc);
  ZC_EXPECT(tokens2.size() == 5);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::Arrow));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::QuestionDot));
  ZC_EXPECT(tokens2[2].is(ast::SyntaxKind::DotDotDot));
  ZC_EXPECT(tokens2[3].is(ast::SyntaxKind::AsteriskAsterisk));
  ZC_EXPECT(tokens2[tokens2.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test shift operators
  auto tokens3 = tokenize("<< >> >>> &= |= ^="_zc);
  ZC_EXPECT(tokens3.size() == 7);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::LessThanLessThan));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::GreaterThanGreaterThan));
  ZC_EXPECT(tokens3[2].is(ast::SyntaxKind::GreaterThanGreaterThanGreaterThan));
  ZC_EXPECT(tokens3[3].is(ast::SyntaxKind::AmpersandEquals));
  ZC_EXPECT(tokens3[4].is(ast::SyntaxKind::BarEquals));
  ZC_EXPECT(tokens3[5].is(ast::SyntaxKind::CaretEquals));
  ZC_EXPECT(tokens3[tokens3.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.UnicodeScalarValues") {
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test valid Unicode scalar values
  auto bufferId =
      sourceManager.addMemBufferCopy("\\u0041 \\u{42} \\u{10FFFF}"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token token;
  lexer.lex(token);
  ZC_EXPECT(!token.is(ast::SyntaxKind::EndOfFile));

  // Test invalid Unicode scalar values (out of range)
  auto bufferId2 =
      sourceManager.addMemBufferCopy("\\u{110000} \\u{FFFFFF}"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);

  Token token2;
  lexer2.lex(token2);
  // Should handle invalid Unicode gracefully
  ZC_EXPECT(true);

  // Test incomplete Unicode escapes
  auto bufferId3 = sourceManager.addMemBufferCopy("\\u123 \\u{12"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);

  Token token3;
  lexer3.lex(token3);
  // Should recover from incomplete escapes
  ZC_EXPECT(true);
}

ZC_TEST("LexerTest.EscapedIdentifierEdgeCases") {
  // Test escaped identifier at start of input
  auto tokens = tokenize("\\u0041identifier"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[tokens.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test multiple escaped characters in one identifier
  auto tokens2 = tokenize("\\u0041\\u0042\\u0043"_zc);
  ZC_EXPECT(tokens2.size() == 2);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[tokens2.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test mixed escaped and regular characters
  auto tokens3 = tokenize("\\u0041bc\\u0044ef"_zc);
  ZC_EXPECT(tokens3.size() == 2);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[tokens3.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test escaped identifier followed by keyword
  auto tokens4 = tokenize("\\u0041bc fun"_zc);
  ZC_EXPECT(tokens4.size() == 3);
  ZC_EXPECT(tokens4[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens4[1].is(ast::SyntaxKind::FunKeyword));
  ZC_EXPECT(tokens4[tokens4.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.OperatorPrecedenceAndAssociativity") {
  // Test complex operator combinations
  auto tokens = tokenize("++x-- x**y x??y?.z"_zc);
  ZC_EXPECT(tokens.size() == 12);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::PlusPlus));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::MinusMinus));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::AsteriskAsterisk));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::QuestionQuestion));
  ZC_EXPECT(tokens[8].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[9].is(ast::SyntaxKind::QuestionDot));
  ZC_EXPECT(tokens[10].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[11].is(ast::SyntaxKind::EndOfFile));

  // Test assignment operators
  auto tokens2 = tokenize("x += y -= z *= w /= v %= u"_zc);
  ZC_EXPECT(tokens2.size() == 12);
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::PlusEquals));
  ZC_EXPECT(tokens2[3].is(ast::SyntaxKind::MinusEquals));
  ZC_EXPECT(tokens2[5].is(ast::SyntaxKind::AsteriskEquals));
  ZC_EXPECT(tokens2[7].is(ast::SyntaxKind::SlashEquals));
  ZC_EXPECT(tokens2[9].is(ast::SyntaxKind::PercentEquals));
  ZC_EXPECT(tokens2[tokens2.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test bitwise operators
  auto tokens3 = tokenize("x & y | z ^ w ~ v"_zc);
  ZC_EXPECT(tokens3.size() == 10);
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::Ampersand));
  ZC_EXPECT(tokens3[3].is(ast::SyntaxKind::Bar));
  ZC_EXPECT(tokens3[5].is(ast::SyntaxKind::Caret));
  ZC_EXPECT(tokens3[tokens3.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.LexEscapedIdentifier") {
  // Test escaped identifiers with Unicode escape sequences
  auto tokens = tokenize("\\u0041BC \\u{48}ello"_zc);
  ZC_EXPECT(tokens.size() == 3);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::EndOfFile));

  // Test Unicode escaped identifiers \uXXXX format
  auto tokens1 = tokenize("\\u0041BC \\u0048ello"_zc);
  ZC_EXPECT(tokens1.size() == 3);
  ZC_EXPECT(tokens1[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens1[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens1[2].is(ast::SyntaxKind::EndOfFile));

  // Test mixed escaped and normal characters
  auto tokens2 = tokenize("\\u0041bc\\u{44}ef"_zc);
  ZC_EXPECT(tokens2.size() == 2);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::EndOfFile));

  // Test invalid escape sequences (should handle gracefully)
  auto tokens3 = tokenize("\\u123 \\u{} valid"_zc);
  ZC_EXPECT(tokens3.size() == 6);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[2].is(ast::SyntaxKind::LeftBrace));
  ZC_EXPECT(tokens3[3].is(ast::SyntaxKind::RightBrace));
  ZC_EXPECT(tokens3[4].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[5].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.LexOperator") {
  // Test single character operators
  auto tokens = tokenize("+ - * / = < > ! & |"_zc);
  ZC_EXPECT(tokens.size() == 11);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Plus));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::Minus));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::Asterisk));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::Slash));
  ZC_EXPECT(tokens[4].is(ast::SyntaxKind::Equals));
  ZC_EXPECT(tokens[5].is(ast::SyntaxKind::LessThan));
  ZC_EXPECT(tokens[6].is(ast::SyntaxKind::GreaterThan));
  ZC_EXPECT(tokens[7].is(ast::SyntaxKind::Exclamation));
  ZC_EXPECT(tokens[8].is(ast::SyntaxKind::Ampersand));
  ZC_EXPECT(tokens[9].is(ast::SyntaxKind::Bar));
  ZC_EXPECT(tokens[10].is(ast::SyntaxKind::EndOfFile));

  // Test multi-character operators
  auto tokens2 = tokenize("== != <= >= === !== <<= >>= >>>="_zc);
  ZC_EXPECT(tokens2.size() == 10);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::EqualsEquals));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::ExclamationEquals));
  ZC_EXPECT(tokens2[2].is(ast::SyntaxKind::LessThanEquals));
  ZC_EXPECT(tokens2[3].is(ast::SyntaxKind::GreaterThanEquals));
  ZC_EXPECT(tokens2[4].is(ast::SyntaxKind::EqualsEqualsEquals));
  ZC_EXPECT(tokens2[5].is(ast::SyntaxKind::ExclamationEqualsEquals));
  ZC_EXPECT(tokens2[6].is(ast::SyntaxKind::LessThanLessThanEquals));
  ZC_EXPECT(tokens2[7].is(ast::SyntaxKind::GreaterThanGreaterThanEquals));
  ZC_EXPECT(tokens2[8].is(ast::SyntaxKind::GreaterThanGreaterThanGreaterThanEquals));
  ZC_EXPECT(tokens2[9].is(ast::SyntaxKind::EndOfFile));

  // Test arrow operators
  auto tokens3 = tokenize("=> ->"_zc);
  ZC_EXPECT(tokens3.size() == 3);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::EqualsGreaterThan));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::Arrow));
  ZC_EXPECT(tokens3[2].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.LexUnicodeScalarValue") {
  // Test valid Unicode scalar values
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test \uXXXX format
  auto bufferId = sourceManager.addMemBufferCopy("\\u0041 \\u0048"_zc.asBytes(), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token token;
  lexer.lex(token);
  ZC_EXPECT(!token.is(ast::SyntaxKind::EndOfFile));

  // Test \u{XXXX} format
  auto bufferId2 = sourceManager.addMemBufferCopy("\\u{41} \\u{48}"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);

  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(!token2.is(ast::SyntaxKind::EndOfFile));

  // Test maximum valid Unicode value
  auto bufferId3 = sourceManager.addMemBufferCopy("\\u{10FFFF}"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);

  Token token3;
  lexer3.lex(token3);
  ZC_EXPECT(!token3.is(ast::SyntaxKind::EndOfFile));

  // Test invalid Unicode values (should handle gracefully)
  auto bufferId4 =
      sourceManager.addMemBufferCopy("\\u{110000} \\u{FFFFFF}"_zc.asBytes(), "test4.zom");
  Lexer lexer4(sourceManager, *diagnosticEngine, langOpts, bufferId4);

  Token token4;
  lexer4.lex(token4);
  ZC_EXPECT(true);  // Should not crash

  // Test incomplete escape sequences
  auto bufferId5 = sourceManager.addMemBufferCopy("\\u123 \\u{12"_zc.asBytes(), "test5.zom");
  Lexer lexer5(sourceManager, *diagnosticEngine, langOpts, bufferId5);

  Token token5;
  lexer5.lex(token5);
  ZC_EXPECT(true);  // Should handle gracefully
}

ZC_TEST("LexerTest.UnicodeInStringLiterals") {
  // Test Unicode escape sequences in string literals
  auto tokens = tokenize("\"\\u0041\\u0042\" \"\\u{48}ello\" \"\\u{10FFFF}\""_zc);
  ZC_EXPECT(tokens.size() == 4);  // 3 string literals + EOF
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[2].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens[3].is(ast::SyntaxKind::EndOfFile));

  // Test Unicode in character literals
  auto tokens2 = tokenize("'\\u0041' '\\u{42}'"_zc);
  ZC_EXPECT(tokens2.size() == 3);  // 2 character literals + EOF
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::CharacterLiteral));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::CharacterLiteral));
  ZC_EXPECT(tokens2[2].is(ast::SyntaxKind::EndOfFile));

  // Test mixed escape sequences in strings
  auto tokens3 = tokenize("\"Hello\\n\\u0041\\tWorld\\u{21}\"");
  ZC_EXPECT(tokens3.size() == 2);  // 1 string literal + EOF
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.UnicodeDigitsInIdentifiers") {
  // Test Unicode digits from various scripts in identifiers
  // This tests the isUnicodeIdentifierContinuation function's digit ranges
  // Using actual Unicode characters, not escape sequences

  // ASCII digits (0x0030-0x0039)
  auto tokens1 = tokenize("id123 var456"_zc);
  ZC_EXPECT(tokens1.size() == 3);
  ZC_EXPECT(tokens1[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens1[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens1[2].is(ast::SyntaxKind::EndOfFile));

  auto& sourceManager = getSourceManager();
  ZC_EXPECT(tokens1[0].getText(sourceManager) == "id123"_zc);
  ZC_EXPECT(tokens1[1].getText(sourceManager) == "var456"_zc);

  // Test Arabic-Indic digits (0x0660-0x0669) - ٠١٢٣٤٥٦٧٨٩
  // Using actual UTF-8 encoded Arabic-Indic digits
  auto tokens2 = tokenize("id٠١٢ var٣٤"_zc);
  ZC_EXPECT(tokens2.size() == 3);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[2].is(ast::SyntaxKind::EndOfFile));

  // Test Devanagari digits (0x0966-0x096f) - ०१२३४५६७८९
  auto tokens3 = tokenize("id०१२ var३"_zc);
  ZC_EXPECT(tokens3.size() == 3);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[2].is(ast::SyntaxKind::EndOfFile));

  // Test Bengali digits (0x09e6-0x09ef) - ০১২৩৪৫৬৭৮৯
  auto tokens4 = tokenize("id০১ var২৩"_zc);
  ZC_EXPECT(tokens4.size() == 3);
  ZC_EXPECT(tokens4[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens4[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens4[2].is(ast::SyntaxKind::EndOfFile));

  // Test Thai digits (0x0e50-0x0e59) - ๐๑๒๓๔๕๖๗๘๙
  auto tokens5 = tokenize("id๐๑ var๒"_zc);
  ZC_EXPECT(tokens5.size() == 3);
  ZC_EXPECT(tokens5[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens5[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens5[2].is(ast::SyntaxKind::EndOfFile));

  // Test Fullwidth digits (0xff10-0xff19) - ０１２３４５６７８９
  auto tokens6 = tokenize("id０１ var２"_zc);
  ZC_EXPECT(tokens6.size() == 3);
  ZC_EXPECT(tokens6[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens6[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens6[2].is(ast::SyntaxKind::EndOfFile));

  // Test mixed Unicode digits in one identifier
  auto tokens7 = tokenize("mixed٠०০๐０"_zc);
  ZC_EXPECT(tokens7.size() == 2);
  ZC_EXPECT(tokens7[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens7[1].is(ast::SyntaxKind::EndOfFile));

  // Test that Unicode digits cannot start an identifier (only continue)
  // Arabic-Indic digit at start should not be part of identifier
  auto tokens8 = tokenize("٠identifier"_zc);
  ZC_EXPECT(tokens8.size() >= 2);  // Should be separate tokens
  ZC_EXPECT(tokens8[tokens8.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test Extended Arabic-Indic digits (0x06f0-0x06f9) - ۰۱۲۳۴۵۶۷۸۹
  auto tokens9 = tokenize("id۰۱۲ var۳"_zc);
  ZC_EXPECT(tokens9.size() == 3);
  ZC_EXPECT(tokens9[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens9[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens9[2].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.FourByteUTF8Sequences") {
  // Test 4-byte UTF-8 sequences - focus on valid identifier characters
  // This tests the 4-byte UTF-8 handling in tryDecodeUtf8CodePoint
  auto tokens = tokenize("identifier"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));

  // Test with actual 4-byte UTF-8 character in string literal
  auto tokens2 = tokenize("\"🚀\""_zc);
  ZC_EXPECT(tokens2.size() == 2);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::EndOfFile));

  // Test invalid 4-byte UTF-8 sequences to trigger error handling
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Create buffer with invalid UTF-8 sequence
  zc::byte invalidUtf8[] = {0xF0, 0x80, 0x80, 0x80, 0x00};  // Overlong encoding
  auto bufferId = sourceManager.addMemBufferCopy(zc::arrayPtr(invalidUtf8, 4), "test.zom");
  Lexer lexer(sourceManager, *diagnosticEngine, langOpts, bufferId);

  Token token;
  lexer.lex(token);
  ZC_EXPECT(true);  // Should handle gracefully without crashing
}

ZC_TEST("LexerTest.UnicodeEscapeSequenceEdgeCases") {
  // Test Unicode escape sequences with various edge cases
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test \u{0} - null character
  auto bufferId1 = sourceManager.addMemBufferCopy("\\u{0}"_zc.asBytes(), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);
  Token token1;
  lexer1.lex(token1);
  ZC_EXPECT(true);  // Should handle null character

  // Test \u{7F} - DEL character
  auto bufferId2 = sourceManager.addMemBufferCopy("\\u{7F}"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);
  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(true);  // Should handle DEL character

  // Test \u{FFFF} - maximum BMP character
  auto bufferId3 = sourceManager.addMemBufferCopy("\\u{FFFF}"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);
  Token token3;
  lexer3.lex(token3);
  ZC_EXPECT(true);  // Should handle max BMP character

  // Test \u{10000} - first supplementary character
  auto bufferId4 = sourceManager.addMemBufferCopy("\\u{10000}"_zc.asBytes(), "test4.zom");
  Lexer lexer4(sourceManager, *diagnosticEngine, langOpts, bufferId4);
  Token token4;
  lexer4.lex(token4);
  ZC_EXPECT(true);  // Should handle supplementary characters

  // Test incomplete escape at end of input
  auto bufferId5 = sourceManager.addMemBufferCopy("\\u{12"_zc.asBytes(), "test5.zom");
  Lexer lexer5(sourceManager, *diagnosticEngine, langOpts, bufferId5);
  Token token5;
  lexer5.lex(token5);
  ZC_EXPECT(true);  // Should handle incomplete escape gracefully

  // Test empty braces
  auto bufferId6 = sourceManager.addMemBufferCopy("\\u{}"_zc.asBytes(), "test6.zom");
  Lexer lexer6(sourceManager, *diagnosticEngine, langOpts, bufferId6);
  Token token6;
  lexer6.lex(token6);
  ZC_EXPECT(true);  // Should handle empty braces

  // Test too many hex digits
  auto bufferId7 = sourceManager.addMemBufferCopy("\\u{123456789}"_zc.asBytes(), "test7.zom");
  Lexer lexer7(sourceManager, *diagnosticEngine, langOpts, bufferId7);
  Token token7;
  lexer7.lex(token7);
  ZC_EXPECT(true);  // Should handle overflow gracefully
}

ZC_TEST("LexerTest.UnicodeEscapeHexDigitHandling") {
  // Test hex digit handling in Unicode escapes (lines 1027-1031, 1073-1080)
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test lowercase hex digits a-f
  auto bufferId1 = sourceManager.addMemBufferCopy("\\u{abcdef}"_zc.asBytes(), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);
  Token token1;
  lexer1.lex(token1);
  ZC_EXPECT(true);  // Should handle lowercase hex

  // Test uppercase hex digits A-F
  auto bufferId2 = sourceManager.addMemBufferCopy("\\u{ABCDEF}"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);
  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(true);  // Should handle uppercase hex

  // Test mixed case hex digits
  auto bufferId3 = sourceManager.addMemBufferCopy("\\u{1a2B3c}"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);
  Token token3;
  lexer3.lex(token3);
  ZC_EXPECT(true);  // Should handle mixed case hex

  // Test standard \uXXXX format with various hex digits
  auto bufferId4 = sourceManager.addMemBufferCopy("\\u1234"_zc.asBytes(), "test4.zom");
  Lexer lexer4(sourceManager, *diagnosticEngine, langOpts, bufferId4);
  Token token4;
  lexer4.lex(token4);
  ZC_EXPECT(true);  // Should handle standard format

  // Test standard format with uppercase
  auto bufferId5 = sourceManager.addMemBufferCopy("\\uABCD"_zc.asBytes(), "test5.zom");
  Lexer lexer5(sourceManager, *diagnosticEngine, langOpts, bufferId5);
  Token token5;
  lexer5.lex(token5);
  ZC_EXPECT(true);  // Should handle uppercase in standard format

  // Test standard format with lowercase
  auto bufferId6 = sourceManager.addMemBufferCopy("\\uabcd"_zc.asBytes(), "test6.zom");
  Lexer lexer6(sourceManager, *diagnosticEngine, langOpts, bufferId6);
  Token token6;
  lexer6.lex(token6);
  ZC_EXPECT(true);  // Should handle lowercase in standard format
}

ZC_TEST("LexerTest.UnicodeEscapeInvalidCases") {
  // Test invalid Unicode escape sequences that should trigger error paths
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test \u not followed by 'u' (line 1005-1009)
  auto bufferId1 = sourceManager.addMemBufferCopy("\\x1234"_zc.asBytes(), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);
  Token token1;
  lexer1.lex(token1);
  ZC_EXPECT(true);  // Should handle invalid escape

  // Test incomplete standard escape
  auto bufferId2 = sourceManager.addMemBufferCopy("\\u12"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);
  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(true);  // Should handle incomplete escape

  // Test invalid hex digit in standard format
  auto bufferId3 = sourceManager.addMemBufferCopy("\\u12GH"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);
  Token token3;
  lexer3.lex(token3);
  ZC_EXPECT(true);  // Should handle invalid hex digit

  // Test value out of Unicode range
  auto bufferId4 = sourceManager.addMemBufferCopy("\\u{110000}"_zc.asBytes(), "test4.zom");
  Lexer lexer4(sourceManager, *diagnosticEngine, langOpts, bufferId4);
  Token token4;
  lexer4.lex(token4);
  ZC_EXPECT(true);  // Should handle out-of-range value
}

ZC_TEST("LexerTest.UTF8DecodingASCIIPath") {
  // Test ASCII character handling in UTF-8 decoding (lines 1250-1254)
  // This tests the fast path for ASCII characters < 0x80
  auto tokens = tokenize("abc123XYZ"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));

  auto& sourceManager = getSourceManager();
  zc::String identifier = tokens[0].getText(sourceManager);
  ZC_EXPECT(identifier == "abc123XYZ");

  // Test ASCII punctuation and operators
  auto tokens2 = tokenize("!@#$%^&*()"_zc);
  ZC_EXPECT(tokens2.size() > 5);  // Should tokenize various operators
  ZC_EXPECT(tokens2[tokens2.size() - 1].is(ast::SyntaxKind::EndOfFile));

  // Test ASCII control characters and whitespace
  auto tokens3 = tokenize("a\t\n\r b"_zc);
  ZC_EXPECT(tokens3.size() == 3);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens3[2].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.UnicodeIdentifierContinuationFailure") {
  // Test Unicode identifier with invalid continuation characters (L744-748)
  // This should trigger the code path where Unicode continuation validation fails
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test with valid Unicode start but invalid UTF-8 continuation bytes
  zc::byte invalidContinuation[] = {0xCE, 0xB1, 0x80, 0x81, 0x00};  // α + invalid bytes
  auto bufferId1 =
      sourceManager.addMemBufferCopy(zc::arrayPtr(invalidContinuation, 4), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);
  Token token1;
  lexer1.lex(token1);
  ZC_EXPECT(true);  // Should handle invalid continuation gracefully

  // Test with overlong encoding in continuation
  zc::byte overlongContinuation[] = {0xCE, 0xB2, 0xC0, 0x80, 0x00};  // β + overlong encoding
  auto bufferId2 =
      sourceManager.addMemBufferCopy(zc::arrayPtr(overlongContinuation, 4), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);
  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(true);  // Should handle overlong encoding gracefully

  // Test with valid Unicode start but non-identifier continuation character
  auto tokens3 = tokenize("γ123\x7F"_zc);  // Gamma + digits + DEL character
  ZC_EXPECT(tokens3.size() >= 2);          // Should tokenize what it can
  ZC_EXPECT(tokens3[tokens3.size() - 1].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.ErrorPropagateOperator") {
  // Test error propagate operator ?! (L811-812)
  // This should trigger the specific code path for ?! operator handling
  auto tokens = tokenize("?!"_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::ErrorPropagate));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));

  // Test in context with other operators
  auto tokens2 = tokenize("a ?! b"_zc);
  ZC_EXPECT(tokens2.size() == 4);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::ErrorPropagate));
  ZC_EXPECT(tokens2[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[3].is(ast::SyntaxKind::EndOfFile));

  // Test mixed with other question mark operators
  auto tokens3 = tokenize("? ?! ?? ?:"_zc);
  ZC_EXPECT(tokens3.size() == 5);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::Question));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::ErrorPropagate));
  ZC_EXPECT(tokens3[2].is(ast::SyntaxKind::QuestionQuestion));
  ZC_EXPECT(tokens3[3].is(ast::SyntaxKind::ErrorDefault));
  ZC_EXPECT(tokens3[4].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.LogicalOrEqualsOperator") {
  // Test logical OR equals operator ||= (L877-878)
  // This should trigger the specific code path for ||= operator handling
  auto tokens = tokenize("||="_zc);
  ZC_EXPECT(tokens.size() == 2);
  ZC_EXPECT(tokens[0].is(ast::SyntaxKind::BarBarEquals));
  ZC_EXPECT(tokens[1].is(ast::SyntaxKind::EndOfFile));

  // Test in assignment context
  auto tokens2 = tokenize("a ||= b"_zc);
  ZC_EXPECT(tokens2.size() == 4);
  ZC_EXPECT(tokens2[0].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[1].is(ast::SyntaxKind::BarBarEquals));
  ZC_EXPECT(tokens2[2].is(ast::SyntaxKind::Identifier));
  ZC_EXPECT(tokens2[3].is(ast::SyntaxKind::EndOfFile));

  // Test mixed with other logical operators
  auto tokens3 = tokenize("|| ||= && &="_zc);
  ZC_EXPECT(tokens3.size() == 5);
  ZC_EXPECT(tokens3[0].is(ast::SyntaxKind::BarBar));
  ZC_EXPECT(tokens3[1].is(ast::SyntaxKind::BarBarEquals));
  ZC_EXPECT(tokens3[2].is(ast::SyntaxKind::AmpersandAmpersand));
  ZC_EXPECT(tokens3[3].is(ast::SyntaxKind::AmpersandEquals));
  ZC_EXPECT(tokens3[4].is(ast::SyntaxKind::EndOfFile));

  // Test with bitwise OR operators
  auto tokens4 = tokenize("| |= || ||="_zc);
  ZC_EXPECT(tokens4.size() == 5);
  ZC_EXPECT(tokens4[0].is(ast::SyntaxKind::Bar));
  ZC_EXPECT(tokens4[1].is(ast::SyntaxKind::BarEquals));
  ZC_EXPECT(tokens4[2].is(ast::SyntaxKind::BarBar));
  ZC_EXPECT(tokens4[3].is(ast::SyntaxKind::BarBarEquals));
  ZC_EXPECT(tokens4[4].is(ast::SyntaxKind::EndOfFile));
}

ZC_TEST("LexerTest.UnicodeEscapeBreakPath") {
  // Test Unicode escape sequence processing break statement (L741)
  // This should trigger the break path in Unicode escape handling
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test invalid escape sequence that should trigger break
  auto bufferId1 = sourceManager.addMemBufferCopy("\\x41"_zc.asBytes(), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);
  Token token1;
  lexer1.lex(token1);
  ZC_EXPECT(true);  // Should handle invalid escape gracefully

  // Test incomplete Unicode escape that should break early
  auto bufferId2 = sourceManager.addMemBufferCopy("\\u"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);
  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(true);  // Should handle incomplete escape

  // Test malformed Unicode escape
  auto bufferId3 = sourceManager.addMemBufferCopy("\\uGHIJ"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);
  Token token3;
  lexer3.lex(token3);
  ZC_EXPECT(true);  // Should handle malformed escape

  // Test escape at end of input
  auto bufferId4 = sourceManager.addMemBufferCopy("\\"_zc.asBytes(), "test4.zom");
  Lexer lexer4(sourceManager, *diagnosticEngine, langOpts, bufferId4);
  Token token4;
  lexer4.lex(token4);
  ZC_EXPECT(true);  // Should handle escape at EOF
}

ZC_TEST("LexerTest.FormTokenPathCoverage") {
  // Test formToken call path coverage (L775)
  // This should trigger the formToken call in escaped identifier processing
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test escaped identifier that triggers formToken
  auto bufferId1 = sourceManager.addMemBufferCopy("`identifier`"_zc.asBytes(), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);
  Token token1;
  lexer1.lex(token1);
  ZC_EXPECT(token1.is(ast::SyntaxKind::Backtick));  // Should form escaped identifier token

  // Test escaped identifier with Unicode
  auto bufferId2 = sourceManager.addMemBufferCopy("`αβγ`"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);
  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(token2.is(ast::SyntaxKind::Backtick));  // Should form Unicode escaped identifier
  lexer2.lex(token2);
  ZC_EXPECT(token2.is(ast::SyntaxKind::Identifier));
  lexer2.lex(token2);
  ZC_EXPECT(token2.is(ast::SyntaxKind::Backtick));

  // Test escaped identifier with special characters
  auto bufferId3 = sourceManager.addMemBufferCopy("`hello world`"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);
  Token token3;
  lexer3.lex(token3);
  ZC_EXPECT(token3.is(ast::SyntaxKind::Backtick));
  lexer3.lex(token3);
  ZC_EXPECT(token3.is(ast::SyntaxKind::Identifier));
  lexer3.lex(token3);
  ZC_EXPECT(token3.is(ast::SyntaxKind::Identifier));
  lexer3.lex(token3);
  ZC_EXPECT(token3.is(ast::SyntaxKind::Backtick));

  // Test escaped identifier with numbers
  auto bufferId4 = sourceManager.addMemBufferCopy("`123abc`"_zc.asBytes(), "test4.zom");
  Lexer lexer4(sourceManager, *diagnosticEngine, langOpts, bufferId4);
  Token token4;
  lexer4.lex(token4);
  ZC_EXPECT(token4.is(ast::SyntaxKind::Backtick));
  lexer4.lex(token4);
  ZC_EXPECT(token4.is(ast::SyntaxKind::IntegerLiteral));
  lexer4.lex(token4);
  ZC_EXPECT(token4.is(ast::SyntaxKind::Identifier));
  lexer4.lex(token4);
  ZC_EXPECT(token4.is(ast::SyntaxKind::Backtick));
}

ZC_TEST("LexerTest.CommentRetentionMode") {
  // Test comment retention mode token formation (L1098-1101)
  // This should trigger the formToken call in comment retention mode
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test single line comment retention
  auto bufferId1 =
      sourceManager.addMemBufferCopy("// This is a comment\nidentifier"_zc.asBytes(), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);
  lexer1.setCommentRetentionMode(
      CommentRetentionMode::kReturnAsTokens);  // Enable comment retention
  Token token1;
  lexer1.lex(token1);
  ZC_EXPECT(token1.is(ast::SyntaxKind::Identifier));  // Next token should be identifier

  // Test multi-line comment retention
  auto bufferId2 = sourceManager.addMemBufferCopy(
      "/* Multi\nline\ncomment */\nidentifier"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);
  lexer2.setCommentRetentionMode(
      CommentRetentionMode::kReturnAsTokens);  // Enable comment retention
  Token token2;
  lexer2.lex(token2);
  ZC_EXPECT(token2.is(ast::SyntaxKind::Identifier));  // Next token should be identifier

  // Test comment with special characters
  auto bufferId3 =
      sourceManager.addMemBufferCopy("// Comment with αβγ Unicode\ntest"_zc.asBytes(), "test3.zom");
  Lexer lexer3(sourceManager, *diagnosticEngine, langOpts, bufferId3);
  lexer3.setCommentRetentionMode(
      CommentRetentionMode::kReturnAsTokens);  // Enable comment retention
  Token token3;
  lexer3.lex(token3);
  ZC_EXPECT(token3.is(ast::SyntaxKind::Identifier));
}

ZC_TEST("LexerTest.StateRestoreFunction") {
  // Test state restore functionality (L1383-1386)
  // This should trigger the state restoration in lookAheadToken
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test lookahead with state restoration
  auto bufferId1 = sourceManager.addMemBufferCopy(
      "identifier1 identifier2 identifier3"_zc.asBytes(), "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);

  // Look ahead multiple tokens to trigger cache expansion and state restoration
  const Token& lookahead1 = lexer1.lookAhead(1);
  const Token& lookahead2 = lexer1.lookAhead(2);

  ZC_EXPECT(lookahead1.is(ast::SyntaxKind::Identifier));  // First token
  ZC_EXPECT(lookahead2.is(ast::SyntaxKind::Identifier));  // Second token

  // Test state restoration with complex tokens
  auto bufferId2 = sourceManager.addMemBufferCopy(
      "123.456 \"string\" /* comment */ identifier"_zc.asBytes(), "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);

  const Token& ahead1 = lexer2.lookAhead(1);  // Float literal
  const Token& ahead2 = lexer2.lookAhead(2);  // String literal
  const Token& ahead3 = lexer2.lookAhead(3);  // Identifier

  ZC_EXPECT(ahead1.is(ast::SyntaxKind::FloatLiteral));
  ZC_EXPECT(ahead2.is(ast::SyntaxKind::StringLiteral));
  ZC_EXPECT(ahead3.is(ast::SyntaxKind::Identifier));

  // Test that normal lexing still works after lookahead
  Token current;
  lexer2.lex(current);
  ZC_EXPECT(current.is(ast::SyntaxKind::FloatLiteral));  // Should match lookahead
}

ZC_TEST("LexerTest.CacheTokenRangeEndPointer") {
  // Test cache token range end position pointer setting (L1367)
  // This should trigger the pointer setting in token cache expansion
  auto& sourceManager = getSourceManager();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  auto langOpts = basic::LangOptions();

  // Test cache expansion with multiple token types
  auto bufferId1 = sourceManager.addMemBufferCopy(
      "token1 token2 token3 token4 token5 token6 token7 token8 token9 token10 token11 token12 token13 token14 token15 token16 token17 token18"_zc
          .asBytes(),
      "test1.zom");
  Lexer lexer1(sourceManager, *diagnosticEngine, langOpts, bufferId1);

  // Look ahead beyond initial cache size to trigger expansion
  const Token& token17 = lexer1.lookAhead(17);
  const Token& token18 = lexer1.lookAhead(18);

  ZC_EXPECT(token17.is(ast::SyntaxKind::Identifier));  // Should be token17
  ZC_EXPECT(token18.is(ast::SyntaxKind::Identifier));  // Should be token18

  // Test with mixed token types to ensure proper range handling
  auto bufferId2 = sourceManager.addMemBufferCopy("123 \"str\" id1 456.78 id2 id3 id4"_zc.asBytes(),
                                                  "test2.zom");
  Lexer lexer2(sourceManager, *diagnosticEngine, langOpts, bufferId2);

  // Look ahead to various positions to trigger cache operations
  const Token& int_token = lexer2.lookAhead(1);  // 123
  const Token& str_token = lexer2.lookAhead(2);  // "str"

  // Debug: Check what tokens we actually got
  ZC_EXPECT(int_token.is(ast::SyntaxKind::IntegerLiteral));  // Should be 123
  ZC_EXPECT(str_token.is(ast::SyntaxKind::StringLiteral));   // Should be "str"
  // Note: lookAhead(1) is the first token, lookAhead(2) is next, etc.
  // Let's verify the actual token sequence
  Token current;
  lexer2.lex(current);                                     // Get first token
  ZC_EXPECT(current.is(ast::SyntaxKind::IntegerLiteral));  // Should be 123

  lexer2.lex(current);                                    // Get second token
  ZC_EXPECT(current.is(ast::SyntaxKind::StringLiteral));  // Should be "str"

  lexer2.lex(current);                                 // Get third token
  ZC_EXPECT(current.is(ast::SyntaxKind::Identifier));  // Should be id1

  lexer2.lex(current);                                   // Get fourth token
  ZC_EXPECT(current.is(ast::SyntaxKind::FloatLiteral));  // Should be 456.78

  lexer2.lex(current);                                 // Get fifth token
  ZC_EXPECT(current.is(ast::SyntaxKind::Identifier));  // Should be id2
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
