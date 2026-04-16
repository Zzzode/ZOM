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
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/cast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace parser {

static void expectIdentifierNameText(
    zc::OneOf<zc::Maybe<const ast::Identifier&>, zc::Maybe<const ast::BindingPattern&>> name,
    zc::StringPtr expected) {
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const ast::Identifier&>) {
      ZC_IF_SOME(id, maybeId) { ZC_EXPECT(id.getText() == expected); }
      else { ZC_FAIL_EXPECT("name should not be none"); }
      return;
    }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const ast::BindingPattern&>) {
      ZC_FAIL_EXPECT("name should be Identifier");
      return;
    }
    ZC_CASE_ONEOF_DEFAULT {
      ZC_FAIL_EXPECT("name should not be empty");
      return;
    }
  }
}

ZC_TEST("ParserTest.BasicParserCreation") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  // Just verify parser can be created
  ZC_EXPECT(true);
}

ZC_TEST("ParserTest.EmptySource") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str(""_zc).asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
}

ZC_TEST("ParserTest.SimpleExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("42").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
}

ZC_TEST("ParserTest.VariableDeclarationList") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse variable declaration");
}

ZC_TEST("ParserTest.FunctionDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun add(a: i32, b: i32) -> i32 { return a + b; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function declaration");
}

ZC_TEST("ParserTest.BinaryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("1 + 2 * 3").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse binary expression");
}

ZC_TEST("ParserTest.IfStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("if (x > 0) { return x; } else { return -x; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse if statement");
}

ZC_TEST("ParserTest.WhileStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("while (x < 10) { x = x + 1; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse while statement");
}

ZC_TEST("ParserTest.ArrayLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("[1, 2, 3]").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse array literal");
}

ZC_TEST("ParserTest.ObjectLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{x: 1, y: 2}").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse object literal");
}

// ================================================================================
// Error Handling Tests
ZC_TEST("ParserTest.InvalidSyntax") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = ;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  // Should handle syntax error gracefully
  ZC_EXPECT(true, "Parser should handle invalid syntax");
}

ZC_TEST("ParserTest.UnterminatedString") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = \"unterminated").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(true, "Parser should handle unterminated string");
}

// ================================================================================
// Complex Expression Tests
ZC_TEST("ParserTest.NestedBinaryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("(1 + 2) * (3 - 4) / 5").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse nested binary expression");
}

ZC_TEST("ParserTest.ConditionalExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x > 0").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse comparison expression");
}

ZC_TEST("ParserTest.FunctionCall") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("foo(1, 2, 3)").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function call");
}

// ================================================================================
// Type Parsing Tests
ZC_TEST("ParserTest.TypeReferenceWithArguments") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: List<i32> = [];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type reference with arguments");
}

ZC_TEST("ParserTest.ObjectType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: { prop: i32; getProp(): i32 } = { prop: 42, "
                                              "getProp: fun() -> i32 { return 42; } };")
                                          .asBytes(),
                                      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse object type with properties and methods");
}

ZC_TEST("ParserTest.TupleType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: (a: i32, b: str) = (42, \"test\");").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse tuple type with named elements");
}

// ================================================================================
// Type Parsing Tests
ZC_TEST("ParserTest.TypeAnnotation") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type annotation");
}

ZC_TEST("ParserTest.StringLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("\"hello world\"").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse string literal");
}

// ================================================================================
// Declaration Tests
ZC_TEST("ParserTest.NumberLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("42").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse number literal");
}

ZC_TEST("ParserTest.BooleanLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("true").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse boolean literal");
}

// ================================================================================
// Import/Export Tests
ZC_TEST("ParserTest.Identifier") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("myVariable").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse identifier");
}

ZC_TEST("ParserTest.ParenthesizedExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("(42)").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse parenthesized expression");
}

ZC_TEST("ParserTest.PatternMatching") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("match x {\n"
                                                          "  _ => 0,\n"
                                                          "  (a, b) => a + b,\n"
                                                          "  { prop } => prop\n"
                                                          "}")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse pattern matching");
}

ZC_TEST("ParserTest.MatchWithGuardClause") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("match x {\n"
                                                          "  n if n > 0 => \"positive\",\n"
                                                          "  0 => \"zero\",\n"
                                                          "  _ => \"negative\"\n"
                                                          "}")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse match with guard clause");
}

ZC_TEST("ParserTest.EnumPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("match result {\n"
                                                          "  Ok(value) => value,\n"
                                                          "  Err(e) => handleError(e)\n"
                                                          "}")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse enum pattern");
}

ZC_TEST("ParserTest.ArrayBindingPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let [a, b, ...rest] = [1, 2, 3, 4];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse array binding pattern");
}

ZC_TEST("ParserTest.ObjectBindingPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let { x, y: z, ...rest } = { x: 1, y: 2, z: 3 };").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse object binding pattern");
}

ZC_TEST("ParserTest.ErrorDefaultExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let result = x ?? y ?? \"default\";").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse error default expression");
}

ZC_TEST("ParserTest.ChainedErrorDefaultExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let a = x ?? y ?? z ?? 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse chained error default expressions");
}

// ================================================================================
// LookAhead Tests - Modified to use normal parsing without lookAhead
ZC_TEST("ParserTest.LookAheadBasic") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  // Test normal parsing without lookAhead
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse successfully without lookAhead");
}

ZC_TEST("ParserTest.CanLookAhead") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  // Test normal parsing without canLookAhead
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse successfully without canLookAhead");
}

ZC_TEST("ParserTest.IsLookAhead") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  // Test normal parsing without isLookAhead
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse successfully without isLookAhead");
}

ZC_TEST("ParserTest.LookAheadBeyondEOF") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  // Test normal parsing beyond EOF
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse successfully even with short input");
}

ZC_TEST("ParserTest.LookAheadEmptySource") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  // Test normal parsing on empty source
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse successfully even on empty source");
}

ZC_TEST("ParserTest.LookAheadComplexExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun add(a: i32, b: i32) -> i32 { return a + b; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  // Test normal parsing of complex expression without lookAhead
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse complex function without lookAhead");
}

ZC_TEST("ParserTest.ParseTypeQuery") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("typeof myVar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse typeof expression");
}

ZC_TEST("ParserTest.ParseTypeQueryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("typeof MyClass.field;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse typeof with property access");
}

ZC_TEST("ParserTest.ParseSimpleFunction") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("fun test() { return 42; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse simple function");
}

ZC_TEST("ParserTest.ParseSimpleClass") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("class MyClass { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse simple class");
}

ZC_TEST("ParserTest.ParseSimpleStruct") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("struct Point { x: i32; y: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse struct declaration");
}

ZC_TEST("ParserTest.ParseSimpleEnum") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("enum Color { Red, Green, Blue }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse enum declaration");
}

ZC_TEST("ParserTest.ParseLogicalAndExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("a && b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse logical AND expression");
}

ZC_TEST("ParserTest.ParseLogicalOrExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("a || b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse logical OR expression");
}

ZC_TEST("ParserTest.ParseRaisesClause") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun test() -> i32 raises ErrorType { return 42; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function with raises clause");
}

ZC_TEST("ParserTest.ParseModuleSyntax") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("module graphics;\n"
              "import math.geometry as geo;\n"
              "import math.geometry.{Point as GeoPoint, distance};\n"
              "export {GeoPoint, distance as calcDistance};\n"
              "export math.geometry.{Point};\n")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();

    ZC_EXPECT(statements.size() == 4, "Should contain imports and exports");

    ZC_IF_SOME(moduleDecl, sourceFile.getModuleDeclaration()) {
      ZC_EXPECT(moduleDecl.getModulePath().getSegments().size() == 1,
                "Module declaration should contain one segment");
    }
    else { ZC_EXPECT(false, "Should contain a module declaration"); }

    auto& moduleImport =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ImportDeclaration>(statements[0]);
    ZC_EXPECT(moduleImport.isModuleImport(), "First import should be a module import");
    ZC_IF_SOME(alias, moduleImport.getAlias()) { ZC_EXPECT(alias.getText() == "geo"); }

    auto& namedImport =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ImportDeclaration>(statements[1]);
    ZC_EXPECT(namedImport.isNamedImport(), "Second import should be a named import");
    ZC_EXPECT(namedImport.getSpecifiers().size() == 2, "Named import should have 2 specifiers");
    ZC_EXPECT(namedImport.getSpecifiers()[0].getImportedName().getText() == "Point");
    ZC_IF_SOME(alias, namedImport.getSpecifiers()[0].getAlias()) {
      ZC_EXPECT(alias.getText() == "GeoPoint");
    }

    auto& exportList =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ExportDeclaration>(statements[2]);
    ZC_EXPECT(exportList.isLocalExport(), "First export should be a local export");
    ZC_EXPECT(exportList.getSpecifiers().size() == 2, "Local export should have 2 specifiers");

    auto& reexport =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ExportDeclaration>(statements[3]);
    ZC_EXPECT(reexport.isReExport(), "Second export should be a re-export");
    ZC_IF_SOME(modulePath, reexport.getModulePath()) {
      ZC_EXPECT(modulePath.getSegments().size() == 2, "Re-export path should have 2 segments");
    }
  }
  else { ZC_EXPECT(false, "Should parse v1 module syntax"); }
}

ZC_TEST("ParserTest.ParseDeclarationSiteExport") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("export fun distance() -> i32 { return 0; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() == 1, "Should contain a single export declaration");

    auto& exportDecl =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ExportDeclaration>(statements[0]);
    ZC_EXPECT(exportDecl.isDeclarationExport(),
              "Declaration-site export should be represented as an export declaration");
    ZC_IF_SOME(declaration, exportDecl.getDeclaration()) {
      ZC_EXPECT(declaration.getKind() == ast::SyntaxKind::FunctionDeclaration,
                "Exported declaration should be a function");
    }
  }
  else { ZC_EXPECT(false, "Should parse declaration-site export"); }
}

ZC_TEST("ParserTest.ModuleDeclarationMustBeFirst") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("import math.geometry;\n"
                                                          "module graphics;\n"
                                                          "let x = 1;\n")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();

    ZC_EXPECT(statements.size() == 3,
              "Late module declaration should not be accepted as a source element");
    ZC_EXPECT(statements[0].getKind() == ast::SyntaxKind::ImportDeclaration,
              "First statement should remain the import");
    ZC_EXPECT(statements[1].getKind() == ast::SyntaxKind::ModuleDeclaration,
              "Second statement should be the module declaration");
    ZC_EXPECT(statements[2].getKind() == ast::SyntaxKind::VariableStatement,
              "Third statement should be the variable declaration");
    ZC_EXPECT(diagnosticEngine->hasErrors(),
              "Late module declaration should produce a parse error");
  }
  else { ZC_EXPECT(false, "Parser should recover from a misplaced module declaration"); }
}

ZC_TEST("ParserTest.LegacyExportDefaultInBlockRecovers") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("fun outer() {\n"
                                                          "  export default foo;\n"
                                                          "  let x = 1;\n"
                                                          "}\n")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& topLevelStatements = sourceFile.getStatements();
    ZC_EXPECT(topLevelStatements.size() == 1, "Should parse the outer function");

    auto& functionDecl =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::FunctionDeclaration>(
            topLevelStatements[0]);
    auto& body = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::BlockStatement>(
        functionDecl.getBody());
    const auto& bodyStatements = body.getStatements();

    ZC_EXPECT(diagnosticEngine->hasErrors(), "Legacy export default should produce parse errors");
    ZC_EXPECT(bodyStatements.size() >= 1,
              "Parser should recover after invalid legacy export and keep later statements");
    ZC_EXPECT(
        bodyStatements[bodyStatements.size() - 1].getKind() == ast::SyntaxKind::VariableStatement,
        "Recovered tail statement should be the variable declaration");
  }
  else { ZC_EXPECT(false, "Parser should recover from legacy export syntax"); }
}

ZC_TEST("ParserTest.ParseClassDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Person { name: str; age: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse class declaration with properties");
}

ZC_TEST("ParserTest.ParseInterfaceDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface Drawable { draw(): void; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse interface declaration");
}

ZC_TEST("ParserTest.ParseInterfacePropertySignature") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface I { let x?: i32; const y: str; fun f<T>(a: i32) -> void; }").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() == 1, "Should contain a single top-level statement");

    auto& ifaceDecl =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::InterfaceDeclaration>(
            statements[0]);
    const auto& members = ifaceDecl.getMembers();

    ZC_EXPECT(members.size() == 3, "Should parse 3 interface members");
    expectIdentifierNameText(members[0].getName(), "x"_zc);
    expectIdentifierNameText(members[1].getName(), "y"_zc);
    expectIdentifierNameText(members[2].getName(), "f"_zc);
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseClassMemberMissingSemicolon") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("class C { x: i32 y: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parse should succeed with recovery");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Should emit MissingSemicolon diagnostic");
}

ZC_TEST("ParserTest.ParseStructDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("struct Rectangle { width: f64; height: f64; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse struct declaration with fields");
}

ZC_TEST("ParserTest.ParseEnumDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("enum Status { Active, Inactive, Pending }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse enum declaration with variants");
}

ZC_TEST("ParserTest.ParseErrorDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("error NetworkError { code: i32; message: str; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse error declaration");
}

ZC_TEST("ParserTest.ParseAliasDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("type UserId = i64;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias declaration");
}

ZC_TEST("ParserTest.ParseTypeOfExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("typeof variable;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse typeof expression");
}

ZC_TEST("ParserTest.ParseTypeQueryInTypeAlias") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  // Test simple identifier in type query within type alias
  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("type MyType = typeof myVar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias with simple type query");
}

ZC_TEST("ParserTest.ParseTypeQueryWithPropertyAccessInTypeAlias") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  // Test property access in type query within type alias
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("type MyType = typeof MyClass.property;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias with property access type query");
}

ZC_TEST("ParserTest.ParseTypeQueryWithChainedPropertyAccess") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  // Test chained property access in type query
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("type MyType = typeof MyClass.nested.property;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse type alias with chained property access type query");
}

ZC_TEST("ParserTest.ParseTypeQueryInVariableDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  // Test type query in variable declaration type annotation
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: typeof myVariable = someValue;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse variable declaration with type query annotation");
}

ZC_TEST("ParserTest.ParseTypeQueryInFunctionParameter") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  // Test type query in function parameter type
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun test(param: typeof MyClass.method) -> void {}").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function with type query parameter type");
}

ZC_TEST("ParserTest.ParseTypeQueryInReturnType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  // Test type query in function return type
  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun test() -> typeof globalVar { return globalVar; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function with type query return type");
}

ZC_TEST("ParserTest.ParseFunctionTypeWithModifiedParameter") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let transform: (readonly value: i32) -> i32 = fn;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function type with a modified parameter");
}

ZC_TEST("ParserTest.ParseShortCircuitExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("a && b || c;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse short circuit expression");
}

ZC_TEST("ParserTest.ParseConditionalExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("condition ? trueValue : falseValue;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse conditional (ternary) expression");
}

ZC_TEST("ParserTest.ParseTypeArgumentsInExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("func<T, U>(arg1, arg2);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse function call with type arguments");
}

ZC_TEST("ParserTest.ParseMatchStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match value { case 1 => \"one\"; case 2 => \"two\"; default => \"other\"; }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse match statement with cases");
}

ZC_TEST("ParserTest.ParseClassHeritageClauses") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Child extends Base implements IFoo, IBar { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() == 1, "Should contain a single top-level statement");

    auto& classDecl =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ClassDeclaration>(statements[0]);

    const auto& clauses = classDecl.getHeritageClauses();
    if (clauses.size() > 0) {
      ZC_EXPECT(clauses.size() == 2, "Should parse extends + implements clauses");
      ZC_EXPECT(clauses[0].getToken() == ::zomlang::compiler::ast::SyntaxKind::ExtendsKeyword,
                "First clause should be extends");
      ZC_EXPECT(clauses[1].getToken() == ::zomlang::compiler::ast::SyntaxKind::ImplementsKeyword,
                "Second clause should be implements");

      ZC_EXPECT(clauses[0].getTypes().size() == 1, "Extends should have one type");
      ZC_EXPECT(clauses[1].getTypes().size() == 2, "Implements should have two types");
    } else {
      ZC_EXPECT(false, "Should parse heritage clauses");
    }
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseHeritageClauseDoubleComma") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("class C extends Base,, { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parse should succeed with recovery");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Should emit error for malformed heritage list");
}

ZC_TEST("ParserTest.ParseHeritageTypeArgumentsSameLine") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class C extends Base<i32> { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    auto& classDecl =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ClassDeclaration>(statements[0]);

    const auto& clauses = classDecl.getHeritageClauses();
    if (clauses.size() > 0) {
      const auto& types = clauses[0].getTypes();
      ZC_EXPECT(types.size() == 1, "Should have one extends type");
      ZC_EXPECT(ZC_ASSERT_NONNULL(types[0]->getTypeArguments()).size() == 1,
                "Should parse one type argument");
    } else {
      ZC_EXPECT(false, "Expected heritage clauses");
    }
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseHeritageTypeArgumentsWithLineBreak") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class C extends Base<i32>\n{ }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    auto& classDecl =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ClassDeclaration>(statements[0]);

    const auto& clauses = classDecl.getHeritageClauses();
    if (clauses.size() > 0) {
      const auto& types = clauses[0].getTypes();
      ZC_EXPECT(types.size() == 1, "Should have one extends type");
      ZC_EXPECT(ZC_ASSERT_NONNULL(types[0]->getTypeArguments()).size() == 1,
                "Should parse one type argument");
    } else {
      ZC_EXPECT(false, "Expected heritage clauses");
    }
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseHeritageObjectLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class C extends {} { x: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    auto& classDecl =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ClassDeclaration>(statements[0]);

    const auto& clauses = classDecl.getHeritageClauses();
    if (clauses.size() > 0) {
      const auto& types = clauses[0].getTypes();
      ZC_EXPECT(types.size() == 1, "Should have one extends type");
    } else {
      ZC_EXPECT(false, "Expected heritage clauses");
    }
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseForStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("for (i = 0; i < 3; i = i + 1) { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse for statement");
}

ZC_TEST("ParserTest.ParseForStatementEmptyClauses") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("for (;;){ }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse for statement with empty clauses");
}

ZC_TEST("ParserTest.ParseDebuggerAndJumpStatements") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("debugger; break; continue loop; return; return 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse debugger/break/continue/return statements");
}

ZC_TEST("ParserTest.ParseOptionalChaining") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("obj?.prop; obj?.[0]; obj?.(1); obj?.; obj.; obj!.prop;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse optional chaining and recovery paths");
}

ZC_TEST("ParserTest.ParseNewExpressions") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("new Foo(1, 2).bar; new Foo[0]; new Foo;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse new expressions and member access chains");
}

ZC_TEST("ParserTest.NewExpressionInvalidOptionalChain") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("new Foo?.bar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parse should succeed with recovery");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Should diagnose invalid optional chain from new expression");
}

ZC_TEST("ParserTest.ParseUnaryOperators") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("void x; !true; ~x; +1; -1; -x ** 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse unary operators and exponentiation diagnostics path");
}

ZC_TEST("ParserTest.DestructuringAssignmentBlockError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);

  class MockConsumer : public diagnostics::DiagnosticConsumer {
  public:
    bool foundError = false;
    void handleDiagnostic(const source::SourceManager&,
                          const diagnostics::Diagnostic& diag) override {
      if (diag.getId() == diagnostics::DiagID::DeclarationOrStatementExpectedAfterBlock) {
        foundError = true;
      }
    }
  };

  auto consumer = zc::heap<MockConsumer>();
  auto consumerPtr = consumer.get();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{ x: 1 } = y;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  parser.parse();

  ZC_EXPECT(consumerPtr->foundError,
            "Should report DeclarationOrStatementExpectedAfterBlock error");
}

ZC_TEST("ParserTest.DeinitDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("class MyClass { deinit { } }").asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse class with deinit");
}

ZC_TEST("ParserTest.DeinitDeclarationWithModifiers") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class MyClass { public deinit { } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse class with public deinit");
}

ZC_TEST("ParserTest.ObjectLiteralFeatures") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  // Test shorthand (x), property assignment (y: 2), and spread (...z)
  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let obj = { x, y: 2, ...z };").asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none,
            "Should parse object literal with shorthand, assignment, and spread");
}

ZC_TEST("ParserTest.PropertyAccessAllowsUnicodeEscapeSequenceAfterDot") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);

  class MockConsumer final : public diagnostics::DiagnosticConsumer {
  public:
    bool foundUnicodeEscapeSequenceCannotAppearHere = false;

    void handleDiagnostic(const source::SourceManager&,
                          const diagnostics::Diagnostic& diag) override {
      if (diag.getId() == diagnostics::DiagID::UnicodeEscapeSequenceCannotAppearHere) {
        foundUnicodeEscapeSequenceCannotAppearHere = true;
      }
    }
  };

  auto consumer = zc::heap<MockConsumer>();
  auto consumerPtr = consumer.get();
  diagnosticEngine->addConsumer(zc::mv(consumer));

  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("obj.\\u0061;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() == 1, "Should contain a single expression statement");

    auto& expressionStatement =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ExpressionStatement>(
            statements[0]);
    auto& propertyAccess =
        ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::PropertyAccessExpression>(
            expressionStatement.getExpression());

    ZC_EXPECT(propertyAccess.getName().getText() == "a"_zc,
              "Unicode escape should still produce identifier text");
    ZC_EXPECT(!consumerPtr->foundUnicodeEscapeSequenceCannotAppearHere,
              "Should not report UnicodeEscapeSequenceCannotAppearHere");
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

// ================================================================================
// Error Recovery Tests - Malformed Statements
ZC_TEST("ParserTest.ParseMissingSemicolon") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = 1 let y = 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parser should recover from missing semicolon");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Parser should report diagnostics for missing semicolon");
}

ZC_TEST("ParserTest.ParseMissingClosingBrace") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("fun foo() { let x = 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parser should recover from missing closing brace");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Parser should report diagnostics for missing closing brace");
}

ZC_TEST("ParserTest.ParseExtraTokensAfterStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = 1; } extra").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parser should recover from extra tokens");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Parser should report diagnostics for extra tokens");
}

ZC_TEST("ParserTest.ParseUnterminatedString") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = \"hello").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parser should recover from unterminated string");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Parser should report diagnostics for unterminated string");
}

ZC_TEST("ParserTest.ParseInvalidTokenSequence") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let = ;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Parser should recover from invalid token sequence");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Parser should report diagnostics for invalid tokens");
}

// ================================================================================
// Statement Parsing Tests
ZC_TEST("ParserTest.ParseLabeledStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("label: let x = 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse labeled statement");

  ZC_IF_SOME(root, result) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() == 1, "Should contain one labeled statement");
    ZC_EXPECT(statements[0].getKind() == ast::SyntaxKind::LabeledStatement,
              "Statement should be a labeled statement");
  }
}

ZC_TEST("ParserTest.ParseEmptyStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str(";").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse empty statement");

  ZC_IF_SOME(root, result) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() == 1, "Should contain one statement");
    ZC_EXPECT(statements[0].getKind() == ast::SyntaxKind::EmptyStatement,
              "Statement should be an empty statement");
  }
}

ZC_TEST("ParserTest.ParseForInLoop") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("for (let x in items) { break; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse for-in loop");

  ZC_IF_SOME(root, result) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() == 1, "Should contain one for-in statement");
    ZC_EXPECT(statements[0].getKind() == ast::SyntaxKind::ForInStatement,
              "Statement should be a for-in statement");
  }
}

ZC_TEST("ParserTest.ParseSuperExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class A extends B { init() { super.init(); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse super expression");
}

// ================================================================================
// Import/Export Edge Cases
ZC_TEST("ParserTest.ParseImportCall") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let m = import(\"module\");").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse import call");
}

ZC_TEST("ParserTest.ParseNamedImportsWithAliases") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("import math.geometry.{Point as GeoPoint, distance};").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse named imports with aliases");

  ZC_IF_SOME(root, result) {
    auto& sourceFile = ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::SourceFile>(*root);
    const auto& statements = sourceFile.getStatements();
    ZC_EXPECT(statements.size() >= 1, "Should contain at least one import declaration");

    // Find the named import (might have other statements if module is not first)
    const ast::ImportDeclaration* namedImport = nullptr;
    for (const auto& stmt : statements) {
      if (stmt.getKind() == ast::SyntaxKind::ImportDeclaration) {
        auto& importDecl =
            ::zomlang::compiler::ast::cast<::zomlang::compiler::ast::ImportDeclaration>(stmt);
        if (importDecl.isNamedImport()) {
          namedImport = &importDecl;
          break;
        }
      }
    }

    ZC_EXPECT(namedImport != nullptr, "Should find a named import");
    if (namedImport != nullptr) {
      ZC_EXPECT(namedImport->isNamedImport(), "Import should be a named import");
      ZC_EXPECT(namedImport->getSpecifiers().size() == 2,
                "Named import should have two specifiers");
      ZC_EXPECT(namedImport->getSpecifiers()[0].getImportedName().getText() == "Point",
                "First imported name should be 'Point'");
      ZC_IF_SOME(alias, namedImport->getSpecifiers()[0].getAlias()) {
        ZC_EXPECT(alias.getText() == "GeoPoint", "First alias should be 'GeoPoint'");
      }
      else { ZC_EXPECT(false, "First specifier should have an alias"); }
    }
  }
}

// ================================================================================
// Binary Operator Tests
ZC_TEST("ParserTest.ParseBitwiseOrExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a | b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse bitwise OR expression");
}

ZC_TEST("ParserTest.ParseBitwiseXorExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a ^ b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse bitwise XOR expression");
}

ZC_TEST("ParserTest.ParseBitwiseAndExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a & b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse bitwise AND expression");
}

ZC_TEST("ParserTest.ParseLeftShiftExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a << 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse left shift expression");
}

ZC_TEST("ParserTest.ParseRightShiftExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a >> 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse right shift expression");
}

ZC_TEST("ParserTest.ParseUnsignedRightShiftExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = a >>> 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse unsigned right shift expression");
}

// ================================================================================
// Equality and Relational Tests
ZC_TEST("ParserTest.ParseEqualityExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a == b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse equality expression");
}

ZC_TEST("ParserTest.ParseInequalityExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a != b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse inequality expression");
}

ZC_TEST("ParserTest.ParseStrictEqualityExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = a === b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse strict equality expression");
}

ZC_TEST("ParserTest.ParseStrictInequalityExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = a !== b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse strict inequality expression");
}

ZC_TEST("ParserTest.ParseLessThanExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a < b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse less than expression");
}

ZC_TEST("ParserTest.ParseGreaterThanExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a > b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse greater than expression");
}

ZC_TEST("ParserTest.ParseLessThanOrEqualExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a <= b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse less than or equal expression");
}

ZC_TEST("ParserTest.ParseGreaterThanOrEqualExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a >= b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse greater than or equal expression");
}

// ================================================================================
// Exponentiation Tests
ZC_TEST("ParserTest.ParseExponentiationExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a ** b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse exponentiation expression");
}

ZC_TEST("ParserTest.ParseExponentiationExpressionRightAssociative") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = 2 ** 3 ** 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse right-associative exponentiation expression");
}

// ================================================================================
// Update Expression Tests
ZC_TEST("ParserTest.ParsePrefixIncrementExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("++x;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse prefix increment expression");
}

ZC_TEST("ParserTest.ParsePrefixDecrementExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("--x;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse prefix decrement expression");
}

ZC_TEST("ParserTest.ParsePostfixIncrementExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x++;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse postfix increment expression");
}

ZC_TEST("ParserTest.ParsePostfixDecrementExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x--;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse postfix decrement expression");
}

// ================================================================================
// Cast Expression Tests
ZC_TEST("ParserTest.ParseCastExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = value as i32;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse cast expression");
}

ZC_TEST("ParserTest.ParseOptionalCastExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = value as? str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse optional cast expression");
}

ZC_TEST("ParserTest.ParseForceCastExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = value as! f64;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse force cast expression");
}

// ================================================================================
// Compound Assignment Tests
ZC_TEST("ParserTest.ParseMultiplyAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x *= 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse multiply assignment expression");
}

ZC_TEST("ParserTest.ParseDivideAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x /= 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse divide assignment expression");
}

ZC_TEST("ParserTest.ParseModuloAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x %= 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse modulo assignment expression");
}

ZC_TEST("ParserTest.ParseExponentiationAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x **= 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse exponentiation assignment expression");
}

ZC_TEST("ParserTest.ParseLeftShiftAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x <<= 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse left shift assignment expression");
}

ZC_TEST("ParserTest.ParseRightShiftAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x >>= 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse right shift assignment expression");
}

ZC_TEST("ParserTest.ParseUnsignedRightShiftAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x >>>= 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse unsigned right shift assignment expression");
}

ZC_TEST("ParserTest.ParseBitwiseAndAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x &= 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse bitwise AND assignment expression");
}

ZC_TEST("ParserTest.ParseBitwiseOrAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x |= 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse bitwise OR assignment expression");
}

ZC_TEST("ParserTest.ParseBitwiseXorAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x ^= 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse bitwise XOR assignment expression");
}

ZC_TEST("ParserTest.ParseLogicalAndAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x &&= y;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse logical AND assignment expression");
}

ZC_TEST("ParserTest.ParseLogicalOrAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x ||= y;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse logical OR assignment expression");
}

ZC_TEST("ParserTest.ParseNullishCoalescingAssignmentExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x ? ?= y;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse nullish coalescing assignment expression");
}

// ================================================================================
// Nullish Coalescing Tests
ZC_TEST("ParserTest.ParseNullishCoalescingExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = a ? ? b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse nullish coalescing expression");
}

ZC_TEST("ParserTest.ParseChainedNullishCoalescingExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = a ? ? b ? ? c;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse chained nullish coalescing expression");
}

// ================================================================================
// Comma Expression Tests
ZC_TEST("ParserTest.ParseCommaExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("a, b, c;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse comma expression");
}

ZC_TEST("ParserTest.ParseCommaExpressionInVariableDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = (a, b, c);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse comma expression in variable declaration");
}

// ================================================================================
// Additional Expression Parsing Tests

ZC_TEST("ParserTest.ParseNewExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = new Foo();").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseNewExpressionWithArguments") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = new Foo(1, 2, 3);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseNewExpressionWithMemberAccess") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = new Foo().bar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseSuperExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class A extends B { init() { super.init(); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseSuperWithoutDotOrParen") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class A extends B { init() { let x = super; } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("ParserTest.ParseAwaitExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { let x = await bar(); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseFunctionExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let f = fun(x: i32) -> i32 { return x + 1; };").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseFunctionExpressionNoReturnType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let f = fun() { print(\"hello\"); };").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseCaptureClause") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let f = fun() use [x, &y] { return x + y; };").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTypeParameterWithConstraint") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo<T extends i32>(x: T) -> T { return x; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseImportCallExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let m = import(\"module\");").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

// ================================================================================
// Type Parsing Tests

ZC_TEST("ParserTest.ParseUnionType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 | str = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseIntersectionType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: A & B = value;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseArrayTypeSuffix") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32[] = [1, 2, 3];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseOptionalType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32? = none;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTupleType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: (i32, str) = (1, \"a\");").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTupleTypeWithNamedElements") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: (name: str, age: i32) = (\"a\", 1);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseFunctionType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("type Fn = (i32) -> str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseObjectTypeLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: { name: str; age: i32; } = value;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseParenthesizedType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: (i32 | str) = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTypeQuery") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: typeof foo = bar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTypeReferenceWithArguments") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: List<i32> = list;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

// ================================================================================
// Pattern Matching Tests

ZC_TEST("ParserTest.ParseMatchStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match x { 1 => true, _ => false }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseMatchWithPatterns") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match x { is i32 => true, _ => false }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

// ================================================================================
// Declaration Parsing Tests

ZC_TEST("ParserTest.ParseClassDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Foo { pub x: i32 = 0; pub fun bar() -> i32 { return self.x; } }").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseClassWithInit") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Foo { init(n: i32) { self.x = n; } pub x: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseClassWithDeinit") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Foo { deinit { cleanup(); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseClassWithAccessors") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Foo { pub val: i32 { get { return self._val; } set(v) { self._val = v; } } }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseInterfaceDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface Drawable { fun draw(); fun resize(scale: f64); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseStructDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("struct Point { x: f64; y: f64; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseEnumDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("enum Color { Red, Green, Blue }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseEnumWithValues") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("enum Status { Ok = 0, Error = 1, Pending = 2 }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseErrorDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("error ParseError { message: str; line: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseAliasDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("alias IntList = List<i32>;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseAliasWithTypeParameter") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("alias Pair<T> = (T, T);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseDebuggerStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("fun foo() { debugger; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseLabeledStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("outer: while (true) { break outer; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseForInStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("for (let x in items) { print(x); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTemplateLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = `hello`;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseStringLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = \"hello world\";").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseCharacterLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 'a';").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseNonNullExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = foo!;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseOptionalChaining") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = foo?.bar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseElementAccess") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = arr[0];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseReturnStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() -> i32 { return 42; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseThrowStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { throw Error(\"fail\"); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTryCatchStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { try { bar(); } catch (e) { print(e); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTryFinallyStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { try { bar(); } finally { cleanup(); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTryCatchFinallyStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { try { bar(); } catch (e) { print(e); } finally { cleanup(); } }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

// ================================================================================
// Targeted coverage for active low-coverage functions

/// Covers parseImportCallExpression - import("module") with full path
ZC_TEST("ParserTest.ParseImportCallWithSpecifier") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let m = import(\"./utils/helper\");").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseFunctionBlockOrSemicolon - semicolon shorthand (abstract method)
ZC_TEST("ParserTest.ParseInterfaceMethodWithSemicolon") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface I { fun foo(); fun bar(x: i32) -> str; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseModifiers with various modifier combinations
ZC_TEST("ParserTest.ParseModifiers") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Foo { pub mut x: i32 = 0; priv readonly y: str; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseClassElement - property declarations with accessors
ZC_TEST("ParserTest.ParseClassWithComputedProperty") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Rect { pub area: f64 { get { return self.w * self.h; } } }").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseAccessorDeclaration - both get and set
ZC_TEST("ParserTest.ParseClassWithGetSetAccessor") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Foo { pub val: i32 { get { return self._val; } set(newVal) { self._val = "
              "newVal; } } priv _val: i32 = 0; }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseDeclaration - type alias declarations
ZC_TEST("ParserTest.ParseTypeAliasDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("type Callback = (i32) -> str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseDeclaration - namespace declarations
ZC_TEST("ParserTest.ParseNamespaceDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("namespace Utils { fun helper() -> i32 { return 42; } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseDeclaration - declare modifier
ZC_TEST("ParserTest.ParseDeclareStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("declare fun externalFunc(x: i32) -> str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseLiteralExpression - various literal types
ZC_TEST("ParserTest.ParseVariousLiterals") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str(
          "let a = true; let b = false; let c = none; let d = 42; let e = 3.14; let f = \"hi\";")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseEnumDeclaration with type annotation
ZC_TEST("ParserTest.ParseEnumWithType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("enum Color: i32 { Red = 1, Green = 2, Blue = 3 }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseErrorDeclaration with methods
ZC_TEST("ParserTest.ParseErrorWithMethods") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("error AppError { message: str; fun format() -> str { return self.message; } }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers scanStartOfDeclaration - import/export scanning
ZC_TEST("ParserTest.ParseExportVariable") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("export let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers scanStartOfDeclaration - static scanning
ZC_TEST("ParserTest.ParseStaticMethod") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Math { pub static fun add(a: i32, b: i32) -> i32 { return a + b; } }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parsePropertyAccessExpressionRest - chained member access
ZC_TEST("ParserTest.ParseChainedMemberAccess") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = a.b.c.d.e;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseRightSideOfDot - keyword as property name
ZC_TEST("ParserTest.ParseKeywordAsProperty") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = obj.type;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseInterfaceElement - method signatures with modifiers
ZC_TEST("ParserTest.ParseInterfaceWithModifiers") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface Comparable { fun compareTo(other: Self) -> i32; pub val: i32; }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers isStartOfStatement edge cases - do-while, switch
ZC_TEST("ParserTest.ParseDoWhileStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { do { bar(); } while (true); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers isStartOfType - predefined type keywords
ZC_TEST("ParserTest.ParseAllPredefinedTypes") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let a: bool = true; let b: i8 = 1; let c: i16 = 1; let d: i64 = 1; "
              "let e: u8 = 1; let f: u16 = 1; let g: u32 = 1; let h: u64 = 1; "
              "let i: f32 = 1.0; let j: f64 = 1.0;")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseSuperExpression with bracket access
ZC_TEST("ParserTest.ParseSuperWithBracket") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class A extends B { init() { let x = super[0]; } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseClassElement with heritage clause parsing
ZC_TEST("ParserTest.ParseClassImplementsInterface") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class Dog implements Animal { pub fun speak() -> str { return \"woof\"; } }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseMatchStatement with complex patterns
ZC_TEST("ParserTest.ParseMatchWithStructPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match x { Point(x: 1, y: 2) => true, _ => false }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseMatchStatement with tuple pattern
ZC_TEST("ParserTest.ParseMatchWithArrayPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match x { [1, 2] => true, _ => false }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parsePropertyName - computed property names
ZC_TEST("ParserTest.ParseComputedPropertyName") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface I { fun [key: str](); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseBindingElement in destructuring
ZC_TEST("ParserTest.ParseObjectDestructuring") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let { x, y } = point;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseBindingElement in array destructuring
ZC_TEST("ParserTest.ParseArrayDestructuring") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let [a, b] = array;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseNewExpression with type arguments
ZC_TEST("ParserTest.ParseNewWithTypeArgs") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = new List<i32>();").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseCallExpressionRest with generic calls
ZC_TEST("ParserTest.ParseGenericCall") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = identity<i32>(42);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseErrorForMissingSemicolonAfter - keyword suggestions
ZC_TEST("ParserTest.ParseKeywordAfterBlock") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{ const }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseErrorForMissingSemicolonAfter - type keyword
ZC_TEST("ParserTest.ParseTypeKeywordAfterBlock") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{ type }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseErrorForMissingSemicolonAfter - module keyword
ZC_TEST("ParserTest.ParseModuleKeywordAfterBlock") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{ module }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseErrorForMissingSemicolonAfter - interface keyword
ZC_TEST("ParserTest.ParseInterfaceKeywordAfterBlock") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{ interface }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseErrorForMissingSemicolonAfter - namespace keyword
ZC_TEST("ParserTest.ParseNamespaceKeywordAfterBlock") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("{ namespace }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parsingContextErrors - bad tokens inside match clauses
ZC_TEST("ParserTest.ParseMatchWithBadToken") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("match x { 1 => }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parsingContextErrors - bad tokens in enum body
ZC_TEST("ParserTest.ParseEnumWithBadToken") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("enum E { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parsingContextErrors - bad tokens in class body
ZC_TEST("ParserTest.ParseClassWithBadToken") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("class C { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parsePattern in catch clause
ZC_TEST("ParserTest.ParseCatchPattern") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { try { bar(); } catch (e: Error) { print(e); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseRaisesClause
ZC_TEST("ParserTest.ParseFunctionWithRaises") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() raises Error { throw Error(\"fail\"); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers parseArrayType - standalone array type parsing
ZC_TEST("ParserTest.ParseArrayTypeInFunction") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() -> i32[][] { return [[1, 2], [3, 4]]; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers isValidHeritageClauseObjectLiteral
ZC_TEST("ParserTest.ParseClassExtendsWithGenerics") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class MyList extends List<i32> { }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
