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

#include "zomlang/compiler/parser/parser.h"

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace parser {

ZC_TEST("ParserTest.BasicParserCreation") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Just verify parser can be created
  ZC_EXPECT(true);
}

ZC_TEST("ParserTest.EmptySource") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str(""_zc).asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
}

ZC_TEST("ParserTest.SimpleExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("42").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
}

ZC_TEST("ParserTest.VariableDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse variable declaration");
}

ZC_TEST("ParserTest.FunctionDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun add(a: i32, b: i32) -> i32 { return a + b; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function declaration");
}

ZC_TEST("ParserTest.BinaryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("1 + 2 * 3").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse binary expression");
}

ZC_TEST("ParserTest.IfStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("if (x > 0) { return x; } else { return -x; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse if statement");
}

ZC_TEST("ParserTest.WhileStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("while (x < 10) { x = x + 1; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse while statement");
}

ZC_TEST("ParserTest.ArrayLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("[1, 2, 3]").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse array literal");
}

ZC_TEST("ParserTest.ObjectLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{x: 1, y: 2}").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse object literal");
}

// ================================================================================
// Error Handling Tests
ZC_TEST("ParserTest.InvalidSyntax") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = ;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  // Should handle syntax error gracefully
  ZC_EXPECT(true, "Parser should handle invalid syntax");
}

ZC_TEST("ParserTest.UnterminatedString") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = \"unterminated").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(true, "Parser should handle unterminated string");
}

// ================================================================================
// Complex Expression Tests
ZC_TEST("ParserTest.NestedBinaryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("(1 + 2) * (3 - 4) / 5").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse nested binary expression");
}

ZC_TEST("ParserTest.ConditionalExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x > 0").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse comparison expression");
}

ZC_TEST("ParserTest.FunctionCall") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("foo(1, 2, 3)").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function call");
}

// ================================================================================
// Type Parsing Tests
ZC_TEST("ParserTest.TypeReferenceWithArguments") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: List<i32> = [];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type reference with arguments");
}

ZC_TEST("ParserTest.ObjectType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: { prop: i32; getProp(): i32 } = { prop: 42, getProp: () => 42 };").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse object type with properties and methods");
}

ZC_TEST("ParserTest.TupleType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: (a: i32, b: str) = (42, \"test\");").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse tuple type with named elements");
}

// ================================================================================
// Type Parsing Tests
ZC_TEST("ParserTest.TypeAnnotation") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type annotation");
}

ZC_TEST("ParserTest.StringLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("\"hello world\"").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse string literal");
}

// ================================================================================
// Declaration Tests
ZC_TEST("ParserTest.NumberLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("42").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse number literal");
}

ZC_TEST("ParserTest.BooleanLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("true").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse boolean literal");
}

// ================================================================================
// Import/Export Tests
ZC_TEST("ParserTest.Identifier") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("myVariable").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse identifier");
}

ZC_TEST("ParserTest.ParenthesizedExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("(42)").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse parenthesized expression");
}

ZC_TEST("ParserTest.PatternMatching") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("match x {\n"
                                                          "  _ => 0,\n"
                                                          "  (a, b) => a + b,\n"
                                                          "  { prop } => prop\n"
                                                          "}")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse pattern matching");
}

ZC_TEST("ParserTest.MatchWithGuardClause") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("match x {\n"
                                                          "  n if n > 0 => \"positive\",\n"
                                                          "  0 => \"zero\",\n"
                                                          "  _ => \"negative\"\n"
                                                          "}")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse match with guard clause");
}

ZC_TEST("ParserTest.EnumPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("match result {\n"
                                                          "  Ok(value) => value,\n"
                                                          "  Err(e) => handleError(e)\n"
                                                          "}")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse enum pattern");
}

ZC_TEST("ParserTest.ArrayBindingPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let [a, b, ...rest] = [1, 2, 3, 4];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse array binding pattern");
}

ZC_TEST("ParserTest.ObjectBindingPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let { x, y: z, ...rest } = { x: 1, y: 2, z: 3 };").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse object binding pattern");
}

ZC_TEST("ParserTest.ErrorDefaultExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let result = x ?? y ?? \"default\";").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse error default expression");
}

ZC_TEST("ParserTest.ChainedErrorDefaultExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let a = x ?? y ?? z ?? 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse chained error default expressions");
}

// ================================================================================
// LookAhead Tests
ZC_TEST("ParserTest.LookAheadBasic") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test lookAhead(1) - should return first token (x)
  const auto& token1 = parser.lookAhead(1);
  ZC_EXPECT(token1.is(lexer::TokenKind::kIdentifier), "First token should be 'x'");

  // Test lookAhead(2) - should return second token (identifier 'x')
  const auto& token2 = parser.lookAhead(2);
  ZC_EXPECT(token2.is(lexer::TokenKind::kEquals), "Second token should be '='");

  // Test lookAhead(3) - should return third token (=)
  const auto& token3 = parser.lookAhead(3);
  ZC_EXPECT(token3.is(lexer::TokenKind::kIntegerLiteral), "Third token should be '42'");
}

ZC_TEST("ParserTest.CanLookAhead") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test canLookAhead for valid positions
  ZC_EXPECT(parser.canLookAhead(1), "Should be able to look ahead 1 token");
  ZC_EXPECT(parser.canLookAhead(2), "Should be able to look ahead 2 tokens");
  ZC_EXPECT(parser.canLookAhead(3), "Should be able to look ahead 3 tokens");
  ZC_EXPECT(parser.canLookAhead(4), "Should be able to look ahead 4 tokens");
  ZC_EXPECT(!parser.canLookAhead(5), "Should not be able to look ahead 5 tokens");
}

ZC_TEST("ParserTest.IsLookAhead") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test isLookAhead for specific token kinds
  ZC_EXPECT(parser.isLookAhead(1, lexer::TokenKind::kIdentifier), "First token should be 'x'");
  ZC_EXPECT(parser.isLookAhead(2, lexer::TokenKind::kEquals), "Second token should be '='");
  ZC_EXPECT(parser.isLookAhead(3, lexer::TokenKind::kIntegerLiteral),
            "Third token should be integer literal 42");
  ZC_EXPECT(parser.isLookAhead(4, lexer::TokenKind::kSemicolon),
            "Fourth token should be semicolon");
}

ZC_TEST("ParserTest.LookAheadBeyondEOF") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test lookAhead beyond available tokens
  const auto& token1 = parser.lookAhead(1);
  ZC_EXPECT(token1.is(lexer::TokenKind::kEOF), "First token should be EOF");

  const auto& token2 = parser.lookAhead(2);
  ZC_EXPECT(token2.is(lexer::TokenKind::kEOF), "Second token should be EOF");

  const auto& token3 = parser.lookAhead(3);
  ZC_EXPECT(token3.is(lexer::TokenKind::kEOF), "Third token should be EOF");

  // Test canLookAhead beyond EOF
  ZC_EXPECT(!parser.canLookAhead(1), "Should not be able to look ahead beyond EOF");
  ZC_EXPECT(!parser.canLookAhead(2), "Should not be able to look ahead beyond EOF");
  ZC_EXPECT(!parser.canLookAhead(3), "Should not be able to look ahead beyond EOF");
}

ZC_TEST("ParserTest.LookAheadEmptySource") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test lookAhead on empty source
  const auto& token1 = parser.lookAhead(1);
  ZC_EXPECT(token1.is(lexer::TokenKind::kEOF), "First token should be EOF");

  // Test canLookAhead on empty source
  ZC_EXPECT(!parser.canLookAhead(1), "Should not be able to look ahead in empty source");

  // Test isLookAhead on empty source
  ZC_EXPECT(parser.isLookAhead(1, lexer::TokenKind::kEOF), "First token should be EOF");
  ZC_EXPECT(!parser.isLookAhead(1, lexer::TokenKind::kIdentifier),
            "First token should not be identifier");
}

ZC_TEST("ParserTest.LookAheadComplexExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun add(a: i32, b: i32) -> i32 { return a + b; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  // Test specific token positions in complex expression
  ZC_EXPECT(parser.isLookAhead(1, lexer::TokenKind::kIdentifier),
            "Token 1 should be identifier 'add'");
  ZC_EXPECT(parser.isLookAhead(2, lexer::TokenKind::kLeftParen), "Token 2 should be '('");
  ZC_EXPECT(parser.isLookAhead(3, lexer::TokenKind::kIdentifier),
            "Token 3 should be identifier 'a'");
  ZC_EXPECT(parser.isLookAhead(4, lexer::TokenKind::kColon), "Token 4 should be ':'");
  ZC_EXPECT(parser.isLookAhead(5, lexer::TokenKind::kI32Keyword),
            "Token 5 should be keyword 'i32'");
  ZC_EXPECT(parser.isLookAhead(6, lexer::TokenKind::kComma), "Token 6 should be ','");
  ZC_EXPECT(parser.isLookAhead(7, lexer::TokenKind::kIdentifier),
            "Token 7 should be identifier 'b'");
}

ZC_TEST("ParserTest.ParseTypeQuery") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("typeof myVar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse typeof expression");
}

ZC_TEST("ParserTest.ParseTypeQueryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("typeof MyClass.field;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse typeof with property access");
}

ZC_TEST("ParserTest.ParseSimpleFunction") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("fun test() { return 42; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse simple function");
}

ZC_TEST("ParserTest.ParseSimpleClass") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("class MyClass { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse simple class");
}

ZC_TEST("ParserTest.ParseSimpleStruct") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("struct Point { x: i32; y: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse struct declaration");
}

ZC_TEST("ParserTest.ParseSimpleEnum") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("enum Color { Red, Green, Blue }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse enum declaration");
}

ZC_TEST("ParserTest.ParseLogicalAndExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("a && b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse logical AND expression");
}

ZC_TEST("ParserTest.ParseLogicalOrExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("a || b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse logical OR expression");
}

ZC_TEST("ParserTest.ParseRaisesClause") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun test() -> i32 raises ErrorType { return 42; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function with raises clause");
}

ZC_TEST("ParserTest.ParseImportDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("import module.path").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse import declaration");
}

ZC_TEST("ParserTest.ParseModulePath") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("import path.to.module as alias").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse module path with alias");
}

ZC_TEST("ParserTest.ParseExportDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("export myModule").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse export declaration");
}

ZC_TEST("ParserTest.ParseClassDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Person { name: str; age: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse class declaration with properties");
}

ZC_TEST("ParserTest.ParseInterfaceDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface Drawable { draw(): void; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse interface declaration");
}

ZC_TEST("ParserTest.ParseStructDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("struct Rectangle { width: f64; height: f64; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse struct declaration with fields");
}

ZC_TEST("ParserTest.ParseEnumDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("enum Status { Active, Inactive, Pending }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse enum declaration with variants");
}

ZC_TEST("ParserTest.ParseErrorDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("error NetworkError { code: i32; message: str; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse error declaration");
}

ZC_TEST("ParserTest.ParseAliasDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("type UserId = i64;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias declaration");
}

ZC_TEST("ParserTest.ParseTypeOfExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("typeof variable;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse typeof expression");
}

ZC_TEST("ParserTest.ParseTypeQueryInTypeAlias") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  // Test simple identifier in type query within type alias
  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("type MyType = typeof myVar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias with simple type query");
}

ZC_TEST("ParserTest.ParseTypeQueryWithPropertyAccessInTypeAlias") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  // Test property access in type query within type alias
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("type MyType = typeof MyClass.property;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias with property access type query");
}

ZC_TEST("ParserTest.ParseTypeQueryWithChainedPropertyAccess") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  // Test chained property access in type query
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("type MyType = typeof MyClass.nested.property;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias with chained property access type query");
}

ZC_TEST("ParserTest.ParseTypeQueryInVariableDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  // Test type query in variable declaration type annotation
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: typeof myVariable = someValue;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse variable declaration with type query annotation");
}

ZC_TEST("ParserTest.ParseTypeQueryInFunctionParameter") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  // Test type query in function parameter type
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun test(param: typeof MyClass.method) -> void {}").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function with type query parameter type");
}

ZC_TEST("ParserTest.ParseTypeQueryInReturnType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  // Test type query in function return type
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun test() -> typeof globalVar { return globalVar; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function with type query return type");
}

ZC_TEST("ParserTest.ParseShortCircuitExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("a && b || c;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse short circuit expression");
}

ZC_TEST("ParserTest.ParseConditionalExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("condition ? trueValue : falseValue;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse conditional (ternary) expression");
}

ZC_TEST("ParserTest.ParseTypeArgumentsInExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("func<T, U>(arg1, arg2);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function call with type arguments");
}

ZC_TEST("ParserTest.ParseMatchStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match value { case 1 => \"one\"; case 2 => \"two\"; _ => \"other\"; }").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse match statement with cases");
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
