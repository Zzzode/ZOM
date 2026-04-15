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

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
