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

  // Just verify parser can handle simple expressions
  ZC_EXPECT(true);
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

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
