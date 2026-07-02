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
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace parser {

static zc::ArrayPtr<const ast::NodeId> topLevelStatements(const ast::Tree& tree) {
  const ast::Node& rootNode = tree.node(tree.root());
  ZC_EXPECT(rootNode.kind == ast::SyntaxKind::SourceFile);

  ast::NodeList list;
  list.first = rootNode.payload.words[ast::kSourceFileStatementsFirstWord];
  list.size = rootNode.payload.words[ast::kSourceFileStatementsSizeWord];
  return tree.list(list);
}

static ast::NodeId statementItem(const ast::Tree& tree, ast::NodeId wrapper) {
  const ast::Node& wrapperNode = tree.node(wrapper);
  if (wrapperNode.kind != ast::SyntaxKind::StatementListItem) { return wrapper; }
  return ast::NodeId(wrapperNode.payload.words[ast::kStatementListItemItemWord]);
}

static ast::SyntaxKind topLevelStatementKind(const ast::Tree& tree, size_t index) {
  return tree.node(statementItem(tree, topLevelStatements(tree)[index])).kind;
}

static const ast::Node& topLevelStatement(const ast::Tree& tree, size_t index) {
  return tree.node(statementItem(tree, topLevelStatements(tree)[index]));
}

static const ast::Node& firstLetDeclarator(const ast::Tree& tree, const ast::Node& letNode) {
  ZC_EXPECT(letNode.kind == ast::SyntaxKind::LetStmt);
  const ast::Node& declarations =
      tree.node(ast::NodeId(letNode.payload.words[ast::kLetStmtDeclarationsWord]));
  ZC_EXPECT(declarations.kind == ast::SyntaxKind::VariableDeclaratorList);

  ast::NodeList list;
  list.first = declarations.payload.words[ast::kVariableDeclaratorListDeclsFirstWord];
  list.size = declarations.payload.words[ast::kVariableDeclaratorListDeclsSizeWord];
  const auto declarators = tree.list(list);
  ZC_EXPECT(declarators.size() > 0);
  return tree.node(declarators[0]);
}

static const ast::Node& letInitializer(const ast::Tree& tree, size_t index) {
  const ast::Node& letNode = topLevelStatement(tree, index);
  ZC_EXPECT(letNode.kind == ast::SyntaxKind::LetStmt);
  const ast::Node& declarator = firstLetDeclarator(tree, letNode);
  return tree.node(ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorInitWord]));
}

static const ast::Node& expressionStatementExpression(const ast::Tree& tree, size_t index) {
  const ast::Node& statement = topLevelStatement(tree, index);
  ZC_EXPECT(statement.kind == ast::SyntaxKind::ExpressionStatement);
  return tree.node(ast::NodeId(statement.payload.words[ast::kExpressionStatementExpressionWord]));
}

static bool hasModuleDeclaration(const ast::Tree& tree) {
  const ast::Node& rootNode = tree.node(tree.root());
  return rootNode.payload.words[ast::kSourceFileModuleWord] != 0;
}

static ast::NodeId moduleDeclaration(const ast::Tree& tree) {
  const ast::Node& rootNode = tree.node(tree.root());
  return ast::NodeId(rootNode.payload.words[ast::kSourceFileModuleWord]);
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
  ZC_IF_SOME(root, result) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1);

    const ast::Node& letNode = root.node(statementItem(root, statements[0]));
    ZC_EXPECT(letNode.kind == ast::SyntaxKind::LetStmt);

    const ast::Node& declarator = firstLetDeclarator(root, letNode);
    const ast::Node& pattern =
        root.node(ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorPatternWord]));
    ZC_EXPECT(pattern.kind == ast::SyntaxKind::IdentifierPattern);
    ZC_EXPECT(root.ident(ast::IdentId(pattern.payload.words[ast::kIdentifierPatternNameWord])) ==
              "x");

    const ast::Node& init =
        root.node(ast::NodeId(declarator.payload.words[ast::kVariableDeclaratorInitWord]));
    ZC_EXPECT(init.kind == ast::SyntaxKind::IntLiteral);
    ZC_EXPECT(init.payload.words[ast::kIntLiteralBaseWord] == 10);
    ZC_EXPECT(root.bigInt(ast::BigIntId(init.payload.words[ast::kIntLiteralValueWord])) == "42");
  }
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
  ZC_IF_SOME(root, result) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1);

    const ast::Node& functionNode = root.node(statementItem(root, statements[0]));
    ZC_EXPECT(functionNode.kind == ast::SyntaxKind::FunctionDecl);
    ZC_EXPECT(root.ident(ast::IdentId(functionNode.payload.words[ast::kFunctionDeclNameWord])) ==
              "add");

    const ast::Node& params =
        root.node(ast::NodeId(functionNode.payload.words[ast::kFunctionDeclParamsIdWord]));
    ZC_EXPECT(params.kind == ast::SyntaxKind::FunctionParameterList);
    ZC_EXPECT(params.payload.words[ast::kFunctionParameterListNparamsWord] == 2);

    ast::NodeList paramList;
    paramList.first = params.payload.words[ast::kFunctionParameterListParamsFirstWord];
    paramList.size = params.payload.words[ast::kFunctionParameterListParamsSizeWord];
    const auto paramIds = root.list(paramList);
    ZC_EXPECT(paramIds.size() == 2);
    const ast::Node& firstParam = root.node(paramIds[0]);
    ZC_EXPECT(firstParam.kind == ast::SyntaxKind::FunctionParameterDecl);
    ZC_EXPECT(root.ident(ast::IdentId(
                  firstParam.payload.words[ast::kFunctionParameterDeclNameWord])) == "a");

    const ast::Node& returnType =
        root.node(ast::NodeId(functionNode.payload.words[ast::kFunctionDeclRetTyWord]));
    ZC_EXPECT(returnType.kind == ast::SyntaxKind::PredefinedTypeExpr);
    ZC_EXPECT(returnType.payload.words[ast::kPredefinedTypeExprKindWord] == 2);

    const ast::Node& body =
        root.node(ast::NodeId(functionNode.payload.words[ast::kFunctionDeclBodyWord]));
    ZC_EXPECT(body.kind == ast::SyntaxKind::BlockStmt);
  }
}

ZC_TEST("ParserTest.BinaryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("1 + 2 * 3;").asBytes(), "test.zom");
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

  ZC_IF_SOME(root, result) {
    const ast::Node& ifNode = topLevelStatement(root, 0);
    ZC_EXPECT(ifNode.kind == ast::SyntaxKind::IfStmt);

    const ast::Node& cond = root.node(ast::NodeId(ifNode.payload.words[ast::kIfStmtCondWord]));
    ZC_EXPECT(cond.kind == ast::SyntaxKind::BinaryExpr);

    const ast::Node& thenStmt =
        root.node(ast::NodeId(ifNode.payload.words[ast::kIfStmtThenStmtWord]));
    ZC_EXPECT(thenStmt.kind == ast::SyntaxKind::BlockStmt);

    const ast::Node& elseStmt =
        root.node(ast::NodeId(ifNode.payload.words[ast::kIfStmtElseStmtWord]));
    ZC_EXPECT(elseStmt.kind == ast::SyntaxKind::BlockStmt);
  }
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

  ZC_IF_SOME(root, result) {
    const ast::Node& whileNode = topLevelStatement(root, 0);
    ZC_EXPECT(whileNode.kind == ast::SyntaxKind::WhileStmt);

    const ast::Node& cond =
        root.node(ast::NodeId(whileNode.payload.words[ast::kWhileStmtCondWord]));
    ZC_EXPECT(cond.kind == ast::SyntaxKind::BinaryExpr);

    const ast::Node& body =
        root.node(ast::NodeId(whileNode.payload.words[ast::kWhileStmtBodyWord]));
    ZC_EXPECT(body.kind == ast::SyntaxKind::BlockStmt);
  }
}

ZC_TEST("ParserTest.ArrayLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("[1, 2, 3];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse array literal");
}

ZC_TEST("ParserTest.ObjectLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let obj = {x: 1, y: 2};").asBytes(), "test.zom");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for invalid syntax");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Parser should report invalid syntax");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unterminated strings");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Parser should report unterminated strings");
}

// ================================================================================
// Complex Expression Tests
ZC_TEST("ParserTest.NestedBinaryExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("(1 + 2) * (3 - 4) / 5;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse nested binary expression");
}

ZC_TEST("ParserTest.ConditionalExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x > 0;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse comparison expression");
}

ZC_TEST("ParserTest.FunctionCall") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("foo(1, 2, 3);").asBytes(), "test.zom");
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

ZC_TEST("ParserTest.NestedTypeArgumentsSplitRightShiftToken") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("alias Nested = Vec<Vec<i32>>;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse nested type arguments with a >> token");
  ZC_IF_SOME(tree, result) {
    const ast::Node& alias = topLevelStatement(tree, 0);
    ZC_EXPECT(alias.kind == ast::SyntaxKind::AliasDecl);

    const ast::Node& target =
        tree.node(ast::NodeId(alias.payload.words[ast::kAliasDeclTargetWord]));
    ZC_EXPECT(target.kind == ast::SyntaxKind::NamedTypeExpr);

    ast::NodeList outerArgs;
    outerArgs.first = target.payload.words[ast::kNamedTypeExprArgsFirstWord];
    outerArgs.size = target.payload.words[ast::kNamedTypeExprArgsSizeWord];
    const auto outerArgNodes = tree.list(outerArgs);
    ZC_EXPECT(outerArgNodes.size() == 1);

    const ast::Node& inner = tree.node(outerArgNodes[0]);
    ZC_EXPECT(inner.kind == ast::SyntaxKind::NamedTypeExpr);

    ast::NodeList innerArgs;
    innerArgs.first = inner.payload.words[ast::kNamedTypeExprArgsFirstWord];
    innerArgs.size = inner.payload.words[ast::kNamedTypeExprArgsSizeWord];
    ZC_EXPECT(tree.list(innerArgs).size() == 1);
  }
}

ZC_TEST("ParserTest.ObjectType") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x: { prop: i32; getProp: () -> i32 } = { prop: 42, "
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

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("\"hello world\";").asBytes(), "test.zom");
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

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse number literal");
}

ZC_TEST("ParserTest.BooleanLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("true;").asBytes(), "test.zom");
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

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("myVariable;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse identifier");
}

ZC_TEST("ParserTest.ParenthesizedExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("(42);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse parenthesized expression");
}

ZC_TEST("ParserTest.PatternMatching") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("match (x) {\n"
                                                          "  when _ => { return 0; }\n"
                                                          "  when (a, b) => { return a + b; }\n"
                                                          "  when { prop } => { return prop; }\n"
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

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("match (x) {\n"
                                              "  when n if n > 0 => { return \"positive\"; }\n"
                                              "  when 0 => { return \"zero\"; }\n"
                                              "  when _ => { return \"negative\"; }\n"
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

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("match (result) {\n"
                                              "  when Ok(value) => { return value; }\n"
                                              "  when Err(e) => { return handleError(e); }\n"
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
// Parser facade smoke tests.
ZC_TEST("ParserTest.ParseBasicLetStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse let statement");
}

ZC_TEST("ParserTest.ParseBasicLetStatementWithInitializer") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse initialized let statement");
}

ZC_TEST("ParserTest.ParseBasicLetStatementWithSemicolon") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse terminated let statement");
}

ZC_TEST("ParserTest.ParseShortExpressionStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse successfully even with short input");
}

ZC_TEST("ParserTest.ParseEmptySource") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse successfully even on empty source");
}

ZC_TEST("ParserTest.ParseComplexFunction") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun add(a: i32, b: i32) -> i32 { return a + b; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse complex function");
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
              "import math::geometry as geo;\n"
              "import math::geometry::{Point as GeoPoint, distance};\n"
              "export {GeoPoint, distance as calcDistance};\n"
              "export math::geometry::{Point};\n")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 4, "Should contain imports and exports");
    ZC_EXPECT(hasModuleDeclaration(root), "Should contain a module declaration");
    const ast::Node& module = root.node(moduleDeclaration(root));
    ZC_EXPECT(module.kind == ast::SyntaxKind::ModuleDeclaration);
    const ast::Node& modulePath =
        root.node(ast::NodeId(module.payload.words[ast::kModuleDeclarationPathWord]));
    ZC_EXPECT(modulePath.kind == ast::SyntaxKind::ModulePath);
    ast::IdentList segments;
    segments.first = modulePath.payload.words[ast::kModulePathSegmentsFirstWord];
    segments.size = modulePath.payload.words[ast::kModulePathSegmentsSizeWord];
    const auto moduleSegments = root.identList(segments);
    ZC_EXPECT(moduleSegments.size() == 1);
    ZC_EXPECT(root.ident(moduleSegments[0]) == "graphics");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ImportDeclaration);
    ZC_EXPECT(topLevelStatementKind(root, 1) == ast::SyntaxKind::ImportDeclaration);
    ZC_EXPECT(topLevelStatementKind(root, 2) == ast::SyntaxKind::ExportDeclaration);
    ZC_EXPECT(topLevelStatementKind(root, 3) == ast::SyntaxKind::ExportDeclaration);
  }
  else { ZC_EXPECT(false, "Should parse module syntax"); }
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
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain a single export declaration");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ExportDeclaration,
              "Declaration-site export should be represented as an export declaration");
  }
  else { ZC_EXPECT(false, "Should parse declaration-site export"); }
}

ZC_TEST("ParserTest.ModuleDeclarationMustBeFirst") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("import math::geometry;\n"
                                                          "module graphics;\n"
                                                          "let x = 1;\n")
                                                      .asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for misplaced module declarations");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Late module declarations should diagnose");
}

ZC_TEST("ParserTest.UnsupportedExportDefaultInBlockRecovers") {
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

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported export syntax");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Unsupported export default should produce parse errors");
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
      zc::str("interface Drawable { fun draw() -> unit; }").asBytes(), "test.zom");
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
      zc::str("interface I { let x?: i32; const y: str; fun f<T>(a: i32) -> unit; }").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain a single top-level statement");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::InterfaceDecl,
              "Should parse an interface declaration");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for malformed class members");
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
      sourceManager->addMemBufferCopy(zc::str("alias UserId = i64;").asBytes(), "test.zom");
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
  auto bufferId = sourceManager->addMemBufferCopy(zc::str("alias MyType = typeof myVar;").asBytes(),
                                                  "test.zom");
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
      zc::str("alias MyType = typeof MyClass.property;").asBytes(), "test.zom");
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
      zc::str("alias MyType = typeof MyClass.nested.property;").asBytes(), "test.zom");
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
      zc::str("fun test(param: typeof MyClass.method) -> unit {}").asBytes(), "test.zom");
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

ZC_TEST("ParserTest.ParseNestedTypeArgumentsInGenericCall") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("func<Vec<i32>>(arg);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse nested type arguments in a generic call");
  ZC_IF_SOME(tree, result) {
    const ast::Node& call = expressionStatementExpression(tree, 0);
    ZC_EXPECT(call.kind == ast::SyntaxKind::CallExpression);

    ast::NodeList typeArgs;
    typeArgs.first = call.payload.words[ast::kCallExpressionTypeArgsFirstWord];
    typeArgs.size = call.payload.words[ast::kCallExpressionTypeArgsSizeWord];
    const auto typeArgNodes = tree.list(typeArgs);
    ZC_EXPECT(typeArgNodes.size() == 1);

    const ast::Node& vectorType = tree.node(typeArgNodes[0]);
    ZC_EXPECT(vectorType.kind == ast::SyntaxKind::NamedTypeExpr);

    ast::NodeList vectorArgs;
    vectorArgs.first = vectorType.payload.words[ast::kNamedTypeExprArgsFirstWord];
    vectorArgs.size = vectorType.payload.words[ast::kNamedTypeExprArgsSizeWord];
    ZC_EXPECT(tree.list(vectorArgs).size() == 1);
  }
}

ZC_TEST("ParserTest.ParseMatchStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match (value) { when 1 => { return \"one\"; } "
              "when 2 => { return \"two\"; } default => { return \"other\"; } }")
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
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain a single top-level statement");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ClassDecl,
              "Should parse a class declaration");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for malformed heritage lists");
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
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain a class declaration");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ClassDecl,
              "Should parse a class declaration");
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
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain a class declaration");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ClassDecl,
              "Should parse a class declaration");
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseHeritageObjectLiteral") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("class C extends {} {}").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain a class declaration");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ClassDecl,
              "Should parse a class declaration");
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseHeritageObjectLiteralBeforeNextDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("class C extends {} {}\nlet value = 1;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 2, "Should keep the following declaration separate");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ClassDecl,
              "Should parse the class declaration first");
    ZC_EXPECT(topLevelStatementKind(root, 1) == ast::SyntaxKind::LetStmt,
              "Should parse the following variable declaration second");
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

ZC_TEST("ParserTest.ParseFunctionReturnObjectTypeBeforeNextDeclaration") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun make() -> { value: i32 } { return {}; }\nlet done = true;").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 2, "Should keep the following declaration separate");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::FunctionDecl,
              "Should parse the function declaration first");
    ZC_EXPECT(topLevelStatementKind(root, 1) == ast::SyntaxKind::LetStmt,
              "Should parse the following variable declaration second");
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

  ZC_IF_SOME(root, result) {
    const ast::Node& forNode = topLevelStatement(root, 0);
    ZC_EXPECT(forNode.kind == ast::SyntaxKind::ForStmt);

    const ast::Node& init = root.node(ast::NodeId(forNode.payload.words[ast::kForStmtInitWord]));
    ZC_EXPECT(init.kind == ast::SyntaxKind::ExpressionStatement);

    const ast::Node& cond = root.node(ast::NodeId(forNode.payload.words[ast::kForStmtCondWord]));
    ZC_EXPECT(cond.kind == ast::SyntaxKind::BinaryExpr);

    const ast::Node& update =
        root.node(ast::NodeId(forNode.payload.words[ast::kForStmtUpdateWord]));
    ZC_EXPECT(update.kind == ast::SyntaxKind::AssignmentExpr);

    const ast::Node& body = root.node(ast::NodeId(forNode.payload.words[ast::kForStmtBodyWord]));
    ZC_EXPECT(body.kind == ast::SyntaxKind::BlockStmt);
  }
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

  ZC_IF_SOME(root, result) {
    const ast::Node& forNode = topLevelStatement(root, 0);
    ZC_EXPECT(forNode.kind == ast::SyntaxKind::ForStmt);
    ZC_EXPECT(forNode.payload.words[ast::kForStmtInitWord] == 0);
    ZC_EXPECT(forNode.payload.words[ast::kForStmtCondWord] == 0);
    ZC_EXPECT(forNode.payload.words[ast::kForStmtUpdateWord] == 0);

    const ast::Node& body = root.node(ast::NodeId(forNode.payload.words[ast::kForStmtBodyWord]));
    ZC_EXPECT(body.kind == ast::SyntaxKind::BlockStmt);
  }
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

  ZC_IF_SOME(root, result) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 5);

    ZC_EXPECT(topLevelStatement(root, 0).kind == ast::SyntaxKind::DebuggerStatement);
    ZC_EXPECT(topLevelStatement(root, 1).kind == ast::SyntaxKind::BreakStmt);

    const ast::Node& continueNode = topLevelStatement(root, 2);
    ZC_EXPECT(continueNode.kind == ast::SyntaxKind::ContinueStatement);
    ZC_EXPECT(root.ident(ast::IdentId(
                  continueNode.payload.words[ast::kContinueStatementLabelWord])) == "loop");

    const ast::Node& emptyReturn = topLevelStatement(root, 3);
    ZC_EXPECT(emptyReturn.kind == ast::SyntaxKind::ReturnStmt);
    ZC_EXPECT(emptyReturn.payload.words[ast::kReturnStmtValueWord] == 0);

    const ast::Node& valueReturn = topLevelStatement(root, 4);
    ZC_EXPECT(valueReturn.kind == ast::SyntaxKind::ReturnStmt);
    const ast::Node& value =
        root.node(ast::NodeId(valueReturn.payload.words[ast::kReturnStmtValueWord]));
    ZC_EXPECT(value.kind == ast::SyntaxKind::IntLiteral);
  }
}

ZC_TEST("ParserTest.ParseOptionalChainingForms") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("obj?.prop; obj?.[0]; obj?.(1); obj?.prop?.[0]?.(1); obj!!.prop;").asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse optional chaining forms");
  ZC_EXPECT(!diagnosticEngine->hasErrors(), "Valid optional chaining should not diagnose");
}

ZC_TEST("ParserTest.ParseOptionalChainingRecovery") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("obj?.; obj?.[];").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for invalid optional chaining");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Invalid optional chaining should diagnose");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for invalid new optional chains");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Should diagnose invalid optional chain from new expression");
}

ZC_TEST("ParserTest.ParseUnaryOperators") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("!true; ~x; +1; -1; -x ** 2;").asBytes(), "test.zom");
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
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain a single expression statement");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ExpressionStatement);
    ZC_EXPECT(!consumerPtr->foundUnicodeEscapeSequenceCannotAppearHere,
              "Should not report UnicodeEscapeSequenceCannotAppearHere");
  }
  else { ZC_EXPECT(false, "Parse should succeed"); }
}

// ================================================================================
// Fail-closed error tests - malformed statements
ZC_TEST("ParserTest.ParseMissingSemicolon") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = 1 let y = 2;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for missing semicolons");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for missing closing braces");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for extra tokens");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unterminated strings");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for invalid token sequences");
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
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain one labeled statement");
    const ast::Node& labeledNode = topLevelStatement(root, 0);
    ZC_EXPECT(labeledNode.kind == ast::SyntaxKind::LabeledStatement,
              "Statement should be a labeled statement");
    ZC_EXPECT(root.ident(ast::IdentId(
                  labeledNode.payload.words[ast::kLabeledStatementLabelWord])) == "label");

    const ast::Node& inner =
        root.node(ast::NodeId(labeledNode.payload.words[ast::kLabeledStatementStatementWord]));
    ZC_EXPECT(inner.kind == ast::SyntaxKind::LetStmt);
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
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain one statement");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::EmptyStatement,
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
  ZC_EXPECT(!diagnosticEngine->hasErrors(), "Valid for-in loop should not diagnose");

  ZC_IF_SOME(root, result) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain one for-in statement");
    const ast::Node& forInNode = topLevelStatement(root, 0);
    ZC_EXPECT(forInNode.kind == ast::SyntaxKind::ForInStatement,
              "Statement should be a for-in statement");

    const ast::Node& binding =
        root.node(ast::NodeId(forInNode.payload.words[ast::kForInStatementBindingWord]));
    ZC_EXPECT(binding.kind == ast::SyntaxKind::IdentifierPattern);
    ZC_EXPECT(root.ident(ast::IdentId(binding.payload.words[ast::kIdentifierPatternNameWord])) ==
              "x");

    const ast::Node& expression =
        root.node(ast::NodeId(forInNode.payload.words[ast::kForInStatementExpressionWord]));
    ZC_EXPECT(expression.kind == ast::SyntaxKind::IdentExpr);
    ZC_EXPECT(root.ident(ast::IdentId(expression.payload.words[ast::kIdentExprNameWord])) ==
              "items");

    const ast::Node& body =
        root.node(ast::NodeId(forInNode.payload.words[ast::kForInStatementBodyWord]));
    ZC_EXPECT(body.kind == ast::SyntaxKind::BlockStmt);
  }
}

ZC_TEST("ParserTest.ParseForOfLoopReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("for (let x of items) { break; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported for-of syntax");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "for-of syntax should produce parse errors");
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
      zc::str("import math::geometry::{Point as GeoPoint, distance};").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse named imports with aliases");

  ZC_IF_SOME(root, result) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() >= 1, "Should contain at least one import declaration");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::ImportDeclaration,
              "Should find an import declaration");
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
  ZC_EXPECT(!diagnosticEngine->hasErrors(), "Strict equality should not produce parse errors");
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
  ZC_EXPECT(!diagnosticEngine->hasErrors(), "Strict inequality should not produce parse errors");
}

ZC_TEST("ParserTest.ParseErrorDefaultExpressionOperator") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a ?: b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse error default expression operator");
  ZC_EXPECT(!diagnosticEngine->hasErrors(), "Error default should not produce parse errors");
}

ZC_TEST("ParserTest.ParsePrefixUnaryExpressionShape") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = -value;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse prefix unary expression");
  ZC_IF_SOME(root, result) {
    const ast::Node& init = letInitializer(root, 0);
    ZC_EXPECT(init.kind == ast::SyntaxKind::UnaryExpression);
    ZC_EXPECT(init.payload.words[ast::kUnaryExpressionOpWord] ==
              static_cast<uint8_t>(ast::UnaryOperatorKind::Minus));
    const ast::Node& operand =
        root.node(ast::NodeId(init.payload.words[ast::kUnaryExpressionOperandWord]));
    ZC_EXPECT(operand.kind == ast::SyntaxKind::IdentExpr);
  }
}

ZC_TEST("ParserTest.ParseErrorPropagatePostfixExpressionShape") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = risky()?!;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse error propagation postfix expression");
  ZC_IF_SOME(root, result) {
    const ast::Node& init = letInitializer(root, 0);
    ZC_EXPECT(init.kind == ast::SyntaxKind::PostfixExpression);
    ZC_EXPECT(init.payload.words[ast::kPostfixExpressionOpWord] ==
              static_cast<uint8_t>(ast::PostfixOperatorKind::ErrorPropagate));
    const ast::Node& operand =
        root.node(ast::NodeId(init.payload.words[ast::kPostfixExpressionOperandWord]));
    ZC_EXPECT(operand.kind == ast::SyntaxKind::CallExpression);
  }
}

ZC_TEST("ParserTest.ParseForceUnwrapPostfixExpressionShape") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = maybe.value!!;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none, "Should parse force unwrap postfix expression");
  ZC_IF_SOME(root, result) {
    const ast::Node& init = letInitializer(root, 0);
    ZC_EXPECT(init.kind == ast::SyntaxKind::PostfixExpression);
    ZC_EXPECT(init.payload.words[ast::kPostfixExpressionOpWord] ==
              static_cast<uint8_t>(ast::PostfixOperatorKind::ErrorUnwrap));
    const ast::Node& operand =
        root.node(ast::NodeId(init.payload.words[ast::kPostfixExpressionOperandWord]));
    ZC_EXPECT(operand.kind == ast::SyntaxKind::MemberExpression);
  }
}

ZC_TEST("ParserTest.ParseSpacedQuestionColonAsInvalidConditionalExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = a ? : b;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for spaced question-colon");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Spaced question-colon should be an invalid conditional expression");
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
  ZC_IF_SOME(root, result) {
    const ast::Node& expression = expressionStatementExpression(root, 0);
    ZC_EXPECT(expression.kind == ast::SyntaxKind::UnaryExpression);
    ZC_EXPECT(expression.payload.words[ast::kUnaryExpressionOpWord] ==
              static_cast<uint8_t>(ast::UnaryOperatorKind::PreIncrement));
  }
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
  ZC_IF_SOME(root, result) {
    const ast::Node& expression = expressionStatementExpression(root, 0);
    ZC_EXPECT(expression.kind == ast::SyntaxKind::UnaryExpression);
    ZC_EXPECT(expression.payload.words[ast::kUnaryExpressionOpWord] ==
              static_cast<uint8_t>(ast::UnaryOperatorKind::PreDecrement));
  }
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
  ZC_IF_SOME(root, result) {
    const ast::Node& expression = expressionStatementExpression(root, 0);
    ZC_EXPECT(expression.kind == ast::SyntaxKind::PostfixExpression);
    ZC_EXPECT(expression.payload.words[ast::kPostfixExpressionOpWord] ==
              static_cast<uint8_t>(ast::PostfixOperatorKind::Increment));
  }
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
  ZC_IF_SOME(root, result) {
    const ast::Node& expression = expressionStatementExpression(root, 0);
    ZC_EXPECT(expression.kind == ast::SyntaxKind::PostfixExpression);
    ZC_EXPECT(expression.payload.words[ast::kPostfixExpressionOpWord] ==
              static_cast<uint8_t>(ast::PostfixOperatorKind::Decrement));
  }
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

  ZC_IF_SOME(root, parser.parse()) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain one statement");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::LetStmt,
              "Cast expression should stay inside a variable statement");
  }
  else { ZC_EXPECT(false, "Should parse cast expression"); }
}

ZC_TEST("ParserTest.ParseOptionalCastExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = value as? str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  ZC_IF_SOME(root, parser.parse()) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain one statement");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::LetStmt,
              "Optional cast expression should stay inside a variable statement");
  }
  else { ZC_EXPECT(false, "Should parse optional cast expression"); }
}

ZC_TEST("ParserTest.ParseForceCastExpressionReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = value as! i32;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported force cast syntax");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Force cast syntax is not supported");
}

ZC_TEST("ParserTest.ParseAsKeywordAfterLineBreakReportsErrorAndRecovers") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = foo\nas(Bar);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for line-break-separated as-casts");
  ZC_EXPECT(diagnosticEngine->hasErrors(),
            "Line-break-separated as-cast should produce a parse error");
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

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("x ?"
                                                          "?= y;")
                                                      .asBytes(),
                                                  "test.zom");
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

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = a ?? b;").asBytes(), "test.zom");
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
      sourceManager->addMemBufferCopy(zc::str("let x = a ?? b ?? c;").asBytes(), "test.zom");
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for invalid super expressions");
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("ParserTest.ParseAwaitExpressionReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { let x = await bar(); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported await syntax");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Await syntax is not designed yet");
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
      zc::str("fun foo<T: i32>(x: T) -> T { return x; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseTypeParameterDefaultWithNestedGenericClose") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo<T = Vec<i32>>(x: T) -> T { return x; }").asBytes(), "test.zom");
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
      sourceManager->addMemBufferCopy(zc::str("alias Fn = (i32) -> str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseFunctionTypeWithNestedGenericParameter") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("alias Fn = (Vec<Vec<i32>>, str) -> bool;").asBytes(), "test.zom");
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
      zc::str("match (x) { when 1 => { return true; } when _ => { return false; } }").asBytes(),
      "test.zom");
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
      zc::str("match (x) { when is i32 => { return true; } when _ => { return false; } }")
          .asBytes(),
      "test.zom");
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

ZC_TEST("ParserTest.ParseTemplateLiteralWithSubstitution") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("let x = `hello ${name}, count ${count + 1}`;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_EXPECT(!diagnosticEngine->hasErrors());
}

ZC_TEST("ParserTest.ParseTaggedTemplateLiteralReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("tag<T>`hello`;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for tagged template literals");
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

ZC_TEST("ParserTest.ParseTemplateLiteralMissingCloseBrace") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = `hello ${name;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for malformed template literals");
  ZC_EXPECT(diagnosticEngine->hasErrors());
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

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = foo!!;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseOptionalPropertyAccess") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x = foo?.bar;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_EXPECT(!diagnosticEngine->hasErrors());
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

ZC_TEST("ParserTest.ParseThrowStatementReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { throw Error(\"fail\"); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported throw statements");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Throw statements are not supported");
}

ZC_TEST("ParserTest.ParseTryCatchStatementReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { try { bar(); } catch (e) { print(e); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported try-catch statements");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Try-catch statements are not supported");
}

ZC_TEST("ParserTest.ParseTryFinallyStatementReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { try { bar(); } finally { cleanup(); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);

  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported try-finally statements");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Try-finally statements are not supported");
}

ZC_TEST("ParserTest.ParseTryCatchFinallyStatementReportsError") {
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
  ZC_EXPECT(result == zc::none,
            "Parser must fail closed for unsupported try-catch-finally statements");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Try-catch-finally statements are not supported");
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
      zc::str("alias Callback = (i32) -> str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

/// Covers unsupported namespace declarations
ZC_TEST("ParserTest.ParseNamespaceDeclarationReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("namespace Utils { fun helper() -> i32 { return 42; } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported namespace declarations");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Namespace declarations are not supported");
}

/// Covers unsupported declare modifier
ZC_TEST("ParserTest.ParseDeclareStatementReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("declare fun externalFunc(x: i32) -> str;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported declare syntax");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Declare syntax is not supported");
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

/// Covers isStartOfStatement edge cases - do-while
ZC_TEST("ParserTest.ParseDoWhileStatement") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("do { bar(); } while (true);").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_EXPECT(!diagnosticEngine->hasErrors(), "Valid do-while statement should not diagnose");

  ZC_IF_SOME(root, result) {
    const auto statements = topLevelStatements(root);
    ZC_EXPECT(statements.size() == 1, "Should contain one do-while statement");
    ZC_EXPECT(topLevelStatementKind(root, 0) == ast::SyntaxKind::DoWhileStatement,
              "Statement should be a do-while statement");
  }
}

ZC_TEST("ParserTest.ParseDoWhileStatementWithoutSemicolon") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("do { bar(); } while (true)").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_EXPECT(!diagnosticEngine->hasErrors(),
            "Valid do-while statement without trailing semicolon should not diagnose");
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
      zc::str("match (x) { when Point { x: 1, y: 2 } => { return true; } "
              "when _ => { return false; } }")
          .asBytes(),
      "test.zom");
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

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("match (x) { when [1, 2] => { return true; } "
                                              "when _ => { return false; } }")
                                          .asBytes(),
                                      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseMatchWithEnumPatternVariants") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("match (result) { when Ok(value) => { return value; } "
              "when Result.Err() => { return 0; } when is i32 => { return 1; } "
              "when (value + 1) => { return 2; } }")
          .asBytes(),
      "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_EXPECT(!diagnosticEngine->hasErrors());
}

/// Covers parsePropertyName - identifier property names
ZC_TEST("ParserTest.ParseIdentifierPropertyName") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("interface I { fun key(); }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
}

ZC_TEST("ParserTest.ParseInvalidPropertyNames") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("interface I { [key]: i32; \"name\": str; 1: i32; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for invalid property names");
  ZC_EXPECT(diagnosticEngine->hasErrors());
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

ZC_TEST("ParserTest.ParseNewWithNestedTypeArgs") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x = new Box<Vec<i32>>();").asBytes(),
                                                  "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_IF_SOME(tree, result) {
    const ast::Node& newExpr = letInitializer(tree, 0);
    ZC_EXPECT(newExpr.kind == ast::SyntaxKind::NewExpression);

    ast::NodeList typeArgs;
    typeArgs.first = newExpr.payload.words[ast::kNewExpressionTypeArgsFirstWord];
    typeArgs.size = newExpr.payload.words[ast::kNewExpressionTypeArgsSizeWord];
    const auto typeArgNodes = tree.list(typeArgs);
    ZC_EXPECT(typeArgNodes.size() == 1);

    const ast::Node& vectorType = tree.node(typeArgNodes[0]);
    ZC_EXPECT(vectorType.kind == ast::SyntaxKind::NamedTypeExpr);

    ast::NodeList vectorArgs;
    vectorArgs.first = vectorType.payload.words[ast::kNamedTypeExprArgsFirstWord];
    vectorArgs.size = vectorType.payload.words[ast::kNamedTypeExprArgsSizeWord];
    ZC_EXPECT(tree.list(vectorArgs).size() == 1);
  }
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for misplaced const keywords");
  ZC_EXPECT(diagnosticEngine->hasErrors());
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for misplaced type keywords");
  ZC_EXPECT(diagnosticEngine->hasErrors());
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for misplaced module keywords");
  ZC_EXPECT(diagnosticEngine->hasErrors());
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for misplaced interface keywords");
  ZC_EXPECT(diagnosticEngine->hasErrors());
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
  ZC_EXPECT(result == zc::none, "Parser must fail closed for misplaced namespace keywords");
  ZC_EXPECT(diagnosticEngine->hasErrors());
}

/// Covers parsingContextErrors - bad tokens inside match clauses
ZC_TEST("ParserTest.ParseMatchWithBadToken") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("match (x) { when 1 => }").asBytes(), "test.zom");
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

/// Covers unsupported catch pattern syntax
ZC_TEST("ParserTest.ParseCatchPatternReportsError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() { try { bar(); } catch (e: Error) { print(e); } }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result == zc::none, "Parser must fail closed for unsupported catch syntax");
  ZC_EXPECT(diagnosticEngine->hasErrors(), "Catch syntax is not supported");
}

/// Covers parseRaisesClause
ZC_TEST("ParserTest.ParseFunctionWithRaises") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  basic::StringPool stringPool;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun foo() -> unit raises Error { return; }").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, stringPool, bufferId);
  auto result = parser.parse();
  ZC_EXPECT(result != zc::none);
  ZC_EXPECT(!diagnosticEngine->hasErrors(), "Raises clauses should parse without throw syntax");
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
