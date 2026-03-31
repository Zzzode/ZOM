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

#include "zomlang/compiler/binder/binder.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace binder {

class BinderTest {
public:
  BinderTest() : diagEngine(sourceManager) {
    // Initialize infrastructure
  }

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diagEngine;
  symbol::SymbolTable symbolTable;
};

// Friend class to access protected members of Binder
class TestBinder : public Binder {
public:
  TestBinder(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagEng)
      : Binder(symbolTable, diagEng) {}

  using Binder::bindBlockStatement;
  using Binder::bindClassDeclaration;
  using Binder::bindFunctionDeclaration;
  using Binder::checkContextualIdentifier;
  using Binder::isBlockScopedContainer;
  using Binder::isContainer;
};

ZC_TEST("BinderTest.BindSourceFile_ManyNodes") {
  BinderTest test;
  Binder binder(test.symbolTable, test.diagEngine);

  zc::Vector<zc::Own<ast::Statement>> statements;

  {
    auto modulePath =
        ast::factory::createModulePath(ast::factory::createStringLiteral("std/io"_zc));
    statements.add(ast::factory::createImportDeclaration(zc::mv(modulePath),
                                                         ast::factory::createIdentifier("io"_zc)));
  }

  {
    auto exportPath = ast::factory::createStringLiteral("std/fmt"_zc);
    statements.add(ast::factory::createExportDeclaration(zc::mv(exportPath),
                                                         ast::factory::createIdentifier("fmt"_zc)));
  }

  {
    zc::Vector<zc::Own<ast::VariableDeclaration>> bindings;

    auto xName = ast::factory::createIdentifier("x"_zc);
    zc::Vector<zc::Own<ast::TypeNode>> unionTypes;
    unionTypes.add(ast::factory::createPredefinedType("i32"_zc));
    unionTypes.add(ast::factory::createPredefinedType("null"_zc));
    auto xType = ast::factory::createOptionalType(
        ast::factory::createParenthesizedType(ast::factory::createUnionType(zc::mv(unionTypes))));
    auto xInit = ast::factory::createConditionalExpression(
        ast::factory::createBooleanLiteral(true), zc::none, ast::factory::createIntegerLiteral(1),
        zc::none, ast::factory::createIntegerLiteral(0));
    bindings.add(
        ast::factory::createVariableDeclaration(zc::mv(xName), zc::mv(xType), zc::mv(xInit)));

    auto yName = ast::factory::createIdentifier("y"_zc);
    zc::Vector<zc::Own<ast::TemplateSpan>> spans;
    spans.add(ast::factory::createTemplateSpan(ast::factory::createIdentifier("x"_zc),
                                               ast::factory::createStringLiteral("!"_zc)));
    auto yInit = ast::factory::createTemplateLiteralExpression(
        ast::factory::createStringLiteral("hello "_zc), zc::mv(spans));
    bindings.add(ast::factory::createVariableDeclaration(zc::mv(yName), zc::none, zc::mv(yInit)));

    auto varDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
    statements.add(ast::factory::createVariableStatement(zc::mv(varDecl)));
  }

  {
    zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
    params.add(ast::factory::createParameterDeclaration(
        {}, zc::none, ast::factory::createIdentifier("a"_zc), zc::none));
    params.add(ast::factory::createParameterDeclaration(
        {}, zc::none, ast::factory::createIdentifier("b"_zc), zc::none));

    zc::Vector<zc::Own<ast::Statement>> bodyStatements;

    {
      auto callee = ast::factory::createIdentifier("foo"_zc);
      zc::Vector<zc::Own<ast::Expression>> args;
      args.add(ast::factory::createIntegerLiteral(42));
      args.add(ast::factory::createStringLiteral("arg"_zc));
      auto callExpr =
          ast::factory::createCallExpression(zc::mv(callee), zc::none, zc::none, zc::mv(args));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(callExpr)));
    }

    {
      auto obj = ast::factory::createIdentifier("obj"_zc);
      auto prop = ast::factory::createIdentifier("prop"_zc);
      auto propAccess =
          ast::factory::createPropertyAccessExpression(zc::mv(obj), zc::mv(prop), false);
      auto index = ast::factory::createIntegerLiteral(0);
      auto elementAccess =
          ast::factory::createElementAccessExpression(zc::mv(propAccess), zc::mv(index), false);
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(elementAccess)));
    }

    {
      auto operand = ast::factory::createIdentifier("a"_zc);
      auto unary =
          ast::factory::createPrefixUnaryExpression(ast::SyntaxKind::Exclamation, zc::mv(operand));
      auto post = ast::factory::createPostfixUnaryExpression(
          ast::SyntaxKind::PlusPlus, ast::factory::createIdentifier("b"_zc));
      auto bin = ast::factory::createBinaryExpression(
          zc::mv(unary), ast::factory::createTokenNode(ast::SyntaxKind::Plus), zc::mv(post));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(bin)));
    }

    {
      auto expr = ast::factory::createIdentifier("x"_zc);
      auto asExpr = ast::factory::createAsExpression(zc::mv(expr),
                                                     ast::factory::createPredefinedType("i32"_zc));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(asExpr)));
    }

    {
      auto expr = ast::factory::createIdentifier("x"_zc);
      auto forced = ast::factory::createForcedAsExpression(
          zc::mv(expr), ast::factory::createPredefinedType("i32"_zc));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(forced)));
    }

    {
      auto expr = ast::factory::createIdentifier("x"_zc);
      auto condAs = ast::factory::createConditionalAsExpression(
          zc::mv(expr), ast::factory::createPredefinedType("i32"_zc));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(condAs)));
    }

    {
      auto expr = ast::factory::createIdentifier("x"_zc);
      auto nn = ast::factory::createNonNullExpression(zc::mv(expr));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(nn)));
    }

    {
      auto expr = ast::factory::createIdentifier("x"_zc);
      auto voidExpr = ast::factory::createVoidExpression(zc::mv(expr));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(voidExpr)));
    }

    {
      auto expr = ast::factory::createIdentifier("x"_zc);
      auto typeofExpr = ast::factory::createTypeOfExpression(zc::mv(expr));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(typeofExpr)));
    }

    {
      auto expr = ast::factory::createIdentifier("x"_zc);
      auto awaitExpr = ast::factory::createAwaitExpression(zc::mv(expr));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(awaitExpr)));
    }

    {
      zc::Vector<zc::Own<ast::CaptureElement>> captures;
      captures.add(ast::factory::createCaptureElement(
          false, ast::factory::createIdentifier("capt"_zc), false));
      captures.add(ast::factory::createCaptureElement(false, zc::none, true));

      zc::Vector<zc::Own<ast::ParameterDeclaration>> fnParams;
      fnParams.add(ast::factory::createParameterDeclaration(
          {}, zc::none, ast::factory::createIdentifier("p"_zc), zc::none));

      zc::Vector<zc::Own<ast::Statement>> fnBodyStatements;
      fnBodyStatements.add(
          ast::factory::createReturnStatement(ast::factory::createIdentifier("p"_zc)));
      auto fnBody = ast::factory::createBlockStatement(zc::mv(fnBodyStatements));

      zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
      auto fnExpr = ast::factory::createFunctionExpression(
          zc::mv(typeParams), zc::mv(fnParams), zc::mv(captures),
          ast::factory::createPredefinedType("i32"_zc), zc::mv(fnBody));
      bodyStatements.add(ast::factory::createExpressionStatement(zc::mv(fnExpr)));
    }

    {
      auto testExpr = ast::factory::createBooleanLiteral(true);
      auto thenStmt = ast::factory::createEmptyStatement();
      auto elseStmt = ast::factory::createEmptyStatement();
      bodyStatements.add(
          ast::factory::createIfStatement(zc::mv(testExpr), zc::mv(thenStmt), zc::mv(elseStmt)));
    }

    {
      auto testExpr = ast::factory::createBooleanLiteral(true);
      zc::Vector<zc::Own<ast::Statement>> loopBodyStatements;
      loopBodyStatements.add(ast::factory::createBreakStatement());
      auto loopBody = ast::factory::createBlockStatement(zc::mv(loopBodyStatements));
      bodyStatements.add(ast::factory::createWhileStatement(zc::mv(testExpr), zc::mv(loopBody)));
    }

    {
      auto initStmt = ast::factory::createEmptyStatement();
      auto cond = ast::factory::createBooleanLiteral(true);
      auto update = ast::factory::createIdentifier("x"_zc);
      zc::Vector<zc::Own<ast::Statement>> loopBodyStatements;
      loopBodyStatements.add(ast::factory::createContinueStatement());
      auto loopBody = ast::factory::createBlockStatement(zc::mv(loopBodyStatements));
      bodyStatements.add(ast::factory::createForStatement(zc::mv(initStmt), zc::mv(cond),
                                                          zc::mv(update), zc::mv(loopBody)));
    }

    {
      zc::Vector<zc::Own<ast::Statement>> clauses;

      {
        auto pattern =
            ast::factory::createIdentifierPattern(ast::factory::createIdentifier("p"_zc));
        auto guard = ast::factory::createBooleanLiteral(true);
        zc::Vector<zc::Own<ast::Statement>> clauseBodyStatements;
        clauseBodyStatements.add(ast::factory::createEmptyStatement());
        auto clauseBody = ast::factory::createBlockStatement(zc::mv(clauseBodyStatements));
        clauses.add(
            ast::factory::createMatchClause(zc::mv(pattern), zc::mv(guard), zc::mv(clauseBody)));
      }

      {
        zc::Vector<zc::Own<ast::Statement>> defaultStatements;
        defaultStatements.add(ast::factory::createDebuggerStatement());
        clauses.add(ast::factory::createDefaultClause(zc::mv(defaultStatements)));
      }

      auto match = ast::factory::createMatchStatement(ast::factory::createIdentifier("x"_zc),
                                                      zc::mv(clauses));
      bodyStatements.add(zc::mv(match));
    }

    bodyStatements.add(ast::factory::createReturnStatement(ast::factory::createIdentifier("x"_zc)));

    auto body = ast::factory::createBlockStatement(zc::mv(bodyStatements));

    zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
    auto returnType =
        ast::factory::createReturnType(ast::factory::createPredefinedType("i32"_zc), zc::none);
    auto func = ast::factory::createFunctionDeclaration(ast::factory::createIdentifier("main"_zc),
                                                        zc::mv(typeParams), zc::mv(params),
                                                        zc::mv(returnType), zc::mv(body));
    statements.add(zc::mv(func));
  }

  {
    zc::Vector<zc::Own<ast::ClassElement>> members;
    members.add(ast::factory::createPropertyDeclaration(
        {}, ast::factory::createIdentifier("field"_zc),
        ast::factory::createPredefinedType("i32"_zc), ast::factory::createIntegerLiteral(1)));
    members.add(ast::factory::createSemicolonClassElement());

    zc::Vector<zc::Own<ast::ExpressionWithTypeArguments>> heritageTypes;
    {
      zc::Vector<zc::Own<ast::TypeNode>> typeArgs;
      zc::Vector<zc::Own<ast::TypeNode>> tupleElements;
      tupleElements.add(ast::factory::createPredefinedType("i32"_zc));
      tupleElements.add(ast::factory::createPredefinedType("str"_zc));
      typeArgs.add(ast::factory::createTupleType(zc::mv(tupleElements)));
      auto exprWithArgs = ast::factory::createExpressionWithTypeArguments(
          ast::factory::createIdentifier("Base"_zc), zc::mv(typeArgs));
      heritageTypes.add(zc::mv(exprWithArgs));
    }
    zc::Vector<zc::Own<ast::HeritageClause>> heritageClauses;
    heritageClauses.add(
        ast::factory::createHeritageClause(ast::SyntaxKind::ExtendsKeyword, zc::mv(heritageTypes)));

    auto cls = ast::factory::createClassDeclaration(ast::factory::createIdentifier("C"_zc), {},
                                                    zc::mv(heritageClauses), zc::mv(members));
    statements.add(zc::mv(cls));
  }

  {
    zc::Vector<zc::Own<ast::InterfaceElement>> members;

    zc::Vector<zc::Own<ast::ParameterDeclaration>> methodParams;
    methodParams.add(ast::factory::createParameterDeclaration(
        {}, zc::none, ast::factory::createIdentifier("n"_zc), zc::none,
        ast::factory::createPredefinedType("i32"_zc)));
    members.add(ast::factory::createMethodSignature(
        {}, ast::factory::createIdentifier("m"_zc), zc::none, {}, zc::mv(methodParams),
        ast::factory::createReturnType(ast::factory::createPredefinedType("i32"_zc), zc::none)));

    members.add(
        ast::factory::createPropertySignature({}, ast::factory::createIdentifier("p"_zc), zc::none,
                                              ast::factory::createPredefinedType("str"_zc)));

    auto iface = ast::factory::createInterfaceDeclaration(ast::factory::createIdentifier("I"_zc),
                                                          {}, zc::none, zc::mv(members));
    statements.add(zc::mv(iface));
  }

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("binder_many_nodes.zom"), zc::mv(statements));
  binder.bindSourceFile(*sourceFile);

  ZC_EXPECT(!test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.VariableDeclaration") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create a variable declaration: var x = 42;
  auto name = ast::factory::createIdentifier("x"_zc);
  auto init = ast::factory::createIntegerLiteral(42);
  auto binding = ast::factory::createVariableDeclaration(zc::mv(name), zc::none, zc::mv(init));
  auto& bindingRef = *binding;

  zc::Vector<zc::Own<ast::VariableDeclaration>> bindings;
  bindings.add(zc::mv(binding));

  auto varDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));

  // Wrap in variable declaration statement
  auto declStmt = ast::factory::createVariableStatement(zc::mv(varDecl));

  // Bind the declaration
  // Note: In a real scenario, this would be part of a larger AST traversal
  // For unit testing, we might need to expose some internal methods or wrap in a block

  // Create a block to serve as container
  zc::Vector<zc::Own<ast::Statement>> stmts;
  stmts.add(zc::mv(declStmt));
  auto block = ast::factory::createBlockStatement(zc::mv(stmts));
  auto& blockRef = *block;

  binder.bindBlockStatement(*block);

  ZC_EXPECT(!test.diagEngine.hasErrors());
  ZC_EXPECT(bindingRef.getSymbol() != zc::none);
  ZC_EXPECT(blockRef.getLocals() != zc::none);
}

ZC_TEST("BinderTest.FunctionDeclaration") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create function: func foo() {}
  auto name = ast::factory::createIdentifier("foo"_zc);
  auto body = ast::factory::createBlockStatement({});

  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;

  auto funcDecl = ast::factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams),
                                                          zc::mv(params), zc::none, zc::mv(body));
  auto& funcRef = *funcDecl;

  binder.bindFunctionDeclaration(funcRef);

  ZC_EXPECT(!test.diagEngine.hasErrors());
  ZC_EXPECT(funcRef.getSymbol() != zc::none);
  ZC_EXPECT(funcRef.getLocals() != zc::none);
}

ZC_TEST("BinderTest.ClassDeclaration") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create class: class MyClass {}
  auto name = ast::factory::createIdentifier("MyClass"_zc);
  zc::Vector<zc::Own<ast::ClassElement>> members;

  auto classDecl =
      ast::factory::createClassDeclaration(zc::mv(name), {}, zc::none, zc::mv(members));
  auto& classRef = *classDecl;

  binder.bindClassDeclaration(classRef);

  ZC_EXPECT(!test.diagEngine.hasErrors());
  ZC_EXPECT(classRef.getSymbol() != zc::none);
}

ZC_TEST("BinderTest.ContextualKeywords") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Test await as identifier in normal context (should be allowed)
  auto name = ast::factory::createIdentifier("await"_zc);
  binder.checkContextualIdentifier(*name);
  ZC_EXPECT(!test.diagEngine.hasErrors());

  // Note: To test failure cases, we'd need to set up context flags which are internal
  // or construct a full async function AST
}

ZC_TEST("BinderTest.ReservedWords") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Test reserved word usage
  auto name = ast::factory::createIdentifier("class"_zc);
  binder.checkContextualIdentifier(*name);
  // Should not error here as checkContextualIdentifier only checks await/yield/etc.
  // Real reserved word checking happens in parser or specific binder methods

  ZC_EXPECT(!test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.ContextualIdentifier_ReservedWordError") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  auto name = ast::factory::createIdentifier("interface"_zc);
  binder.checkContextualIdentifier(*name);
  ZC_EXPECT(test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.ContextualIdentifier_SkipsWhenAlreadyHasErrors") {
  BinderTest test;

  class CaptureConsumer final : public diagnostics::DiagnosticConsumer {
  public:
    zc::Vector<diagnostics::DiagID> diagnosticIds;

    void handleDiagnostic(const source::SourceManager& sm,
                          const diagnostics::Diagnostic& diagnostic) override {
      (void)sm;
      diagnosticIds.add(diagnostic.getId());
    }
  };

  auto consumer = zc::heap<CaptureConsumer>();
  auto& consumerRef = *consumer;
  test.diagEngine.addConsumer(zc::mv(consumer));

  TestBinder binder(test.symbolTable, test.diagEngine);

  test.diagEngine.diagnose<diagnostics::DiagID::InvalidCharacter>(source::SourceLoc{});
  ZC_EXPECT(test.diagEngine.hasErrors());

  binder.checkContextualIdentifier(*ast::factory::createIdentifier("interface"_zc));

  ZC_EXPECT(consumerRef.diagnosticIds.size() == 1);
  ZC_EXPECT(consumerRef.diagnosticIds[0] == diagnostics::DiagID::InvalidCharacter);
}

ZC_TEST("BinderTest.ContainerFlags") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  auto sourceFile = ast::factory::createSourceFile(zc::str("binder_container_flags.zom"),
                                                   zc::Vector<zc::Own<ast::Statement>>{});
  ZC_EXPECT(binder.isContainer(*sourceFile));

  zc::Vector<zc::Own<ast::Statement>> blockStatements;
  auto block = ast::factory::createBlockStatement(zc::mv(blockStatements));
  ZC_EXPECT(!binder.isContainer(*block));
  ZC_EXPECT(binder.isBlockScopedContainer(*block));

  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
  zc::Vector<zc::Own<ast::Statement>> fnBodyStatements;
  auto fnBody = ast::factory::createBlockStatement(zc::mv(fnBodyStatements));
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
  auto funcDecl = ast::factory::createFunctionDeclaration(ast::factory::createIdentifier("f"_zc),
                                                          zc::mv(typeParams), zc::mv(params),
                                                          zc::none, zc::mv(fnBody));
  ZC_EXPECT(binder.isContainer(*funcDecl));

  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> fnExprTypeParams;
  zc::Vector<zc::Own<ast::ParameterDeclaration>> fnExprParams;
  zc::Vector<zc::Own<ast::CaptureElement>> fnExprCaptures;
  zc::Vector<zc::Own<ast::Statement>> fnExprBodyStatements;
  auto fnExprBody = ast::factory::createBlockStatement(zc::mv(fnExprBodyStatements));
  auto fnExpr =
      ast::factory::createFunctionExpression(zc::mv(fnExprTypeParams), zc::mv(fnExprParams),
                                             zc::mv(fnExprCaptures), zc::none, zc::mv(fnExprBody));
  ZC_EXPECT(binder.isContainer(*fnExpr));

  zc::Vector<zc::Own<ast::ClassElement>> classMembers;
  auto classDecl = ast::factory::createClassDeclaration(ast::factory::createIdentifier("C"_zc), {},
                                                        zc::none, zc::mv(classMembers));
  ZC_EXPECT(binder.isContainer(*classDecl));

  zc::Vector<zc::Own<ast::InterfaceElement>> interfaceMembers;
  auto interfaceDecl = ast::factory::createInterfaceDeclaration(
      ast::factory::createIdentifier("I"_zc), {}, zc::none, zc::mv(interfaceMembers));
  ZC_EXPECT(binder.isContainer(*interfaceDecl));

  ZC_EXPECT(!binder.isContainer(*ast::factory::createIdentifier("x"_zc)));
}

ZC_TEST("BinderTest.VisitOptionals_Absent") {
  BinderTest test;
  Binder binder(test.symbolTable, test.diagEngine);

  zc::Vector<zc::Own<ast::Statement>> statements;

  {
    auto cond = ast::factory::createBooleanLiteral(true);
    auto thenStmt = ast::factory::createEmptyStatement();
    statements.add(ast::factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::none));
  }

  {
    auto initStmt = zc::none;
    auto cond = zc::none;
    auto update = zc::none;
    zc::Vector<zc::Own<ast::Statement>> loopBodyStatements;
    loopBodyStatements.add(ast::factory::createBreakStatement());
    auto loopBody = ast::factory::createBlockStatement(zc::mv(loopBodyStatements));
    statements.add(ast::factory::createForStatement(zc::mv(initStmt), zc::mv(cond), zc::mv(update),
                                                    zc::mv(loopBody)));
  }

  { statements.add(ast::factory::createReturnStatement(zc::none)); }

  {
    zc::Vector<zc::Own<ast::Statement>> clauses;
    auto pattern = ast::factory::createWildcardPattern();
    zc::Vector<zc::Own<ast::Statement>> clauseBodyStatements;
    clauseBodyStatements.add(ast::factory::createEmptyStatement());
    auto clauseBody = ast::factory::createBlockStatement(zc::mv(clauseBodyStatements));
    clauses.add(ast::factory::createMatchClause(zc::mv(pattern), zc::none, zc::mv(clauseBody)));
    auto match =
        ast::factory::createMatchStatement(ast::factory::createIdentifier("x"_zc), zc::mv(clauses));
    statements.add(zc::mv(match));
  }

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("binder_visit_optionals.zom"), zc::mv(statements));
  binder.bindSourceFile(*sourceFile);
}

ZC_TEST("BinderTest.RedeclareVariableInSameScope") {
  BinderTest test;
  Binder binder(test.symbolTable, test.diagEngine);

  zc::Vector<zc::Own<ast::Statement>> stmts;

  zc::Vector<zc::Own<ast::VariableDeclaration>> bindings1;
  bindings1.add(ast::factory::createVariableDeclaration(ast::factory::createIdentifier("x"_zc)));
  stmts.add(ast::factory::createVariableStatement(
      ast::factory::createVariableDeclarationList(zc::mv(bindings1))));

  zc::Vector<zc::Own<ast::VariableDeclaration>> bindings2;
  bindings2.add(ast::factory::createVariableDeclaration(ast::factory::createIdentifier("x"_zc)));
  stmts.add(ast::factory::createVariableStatement(
      ast::factory::createVariableDeclarationList(zc::mv(bindings2))));

  auto block = ast::factory::createBlockStatement(zc::mv(stmts));
  binder.bind(*block);

  ZC_EXPECT(test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.RedeclareFunctionInSameScope") {
  BinderTest test;
  Binder binder(test.symbolTable, test.diagEngine);

  zc::Vector<zc::Own<ast::Statement>> statements;
  {
    auto body = ast::factory::createBlockStatement({});
    zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
    zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
    statements.add(ast::factory::createFunctionDeclaration(ast::factory::createIdentifier("f"_zc),
                                                           zc::mv(typeParams), zc::mv(params),
                                                           zc::none, zc::mv(body)));
  }
  {
    auto body = ast::factory::createBlockStatement({});
    zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
    zc::Vector<zc::Own<ast::ParameterDeclaration>> params;
    statements.add(ast::factory::createFunctionDeclaration(ast::factory::createIdentifier("f"_zc),
                                                           zc::mv(typeParams), zc::mv(params),
                                                           zc::none, zc::mv(body)));
  }

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("binder_redeclare_func.zom"), zc::mv(statements));
  binder.bindSourceFile(*sourceFile);

  ZC_EXPECT(test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.RedeclareClassInSameScope") {
  BinderTest test;
  Binder binder(test.symbolTable, test.diagEngine);

  zc::Vector<zc::Own<ast::Statement>> statements;
  {
    zc::Vector<zc::Own<ast::ClassElement>> members;
    statements.add(ast::factory::createClassDeclaration(ast::factory::createIdentifier("C"_zc), {},
                                                        zc::none, zc::mv(members)));
  }
  {
    zc::Vector<zc::Own<ast::ClassElement>> members;
    statements.add(ast::factory::createClassDeclaration(ast::factory::createIdentifier("C"_zc), {},
                                                        zc::none, zc::mv(members)));
  }

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("binder_redeclare_class.zom"), zc::mv(statements));
  binder.bindSourceFile(*sourceFile);

  ZC_EXPECT(test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.RedeclareInterfaceInSameScope") {
  BinderTest test;
  Binder binder(test.symbolTable, test.diagEngine);

  zc::Vector<zc::Own<ast::Statement>> statements;
  {
    zc::Vector<zc::Own<ast::InterfaceElement>> members;
    statements.add(ast::factory::createInterfaceDeclaration(ast::factory::createIdentifier("I"_zc),
                                                            {}, zc::none, zc::mv(members)));
  }
  {
    zc::Vector<zc::Own<ast::InterfaceElement>> members;
    statements.add(ast::factory::createInterfaceDeclaration(ast::factory::createIdentifier("I"_zc),
                                                            {}, zc::none, zc::mv(members)));
  }

  auto sourceFile =
      ast::factory::createSourceFile(zc::str("binder_redeclare_iface.zom"), zc::mv(statements));
  binder.bindSourceFile(*sourceFile);

  ZC_EXPECT(test.diagEngine.hasErrors());
}

ZC_TEST("BinderTest.ScopeResolution") {
  BinderTest test;
  TestBinder binder(test.symbolTable, test.diagEngine);

  // Create function: func foo() { var x = 1; }
  auto name = ast::factory::createIdentifier("foo"_zc);

  // Body: { var x = 1; }
  auto varName = ast::factory::createIdentifier("x"_zc);
  auto init = ast::factory::createIntegerLiteral(1);
  auto binding = ast::factory::createVariableDeclaration(zc::mv(varName), zc::none, zc::mv(init));
  auto& bindingRef = *binding;
  zc::Vector<zc::Own<ast::VariableDeclaration>> bindings;
  bindings.add(zc::mv(binding));
  auto varDecl = ast::factory::createVariableDeclarationList(zc::mv(bindings));
  auto declStmt = ast::factory::createVariableStatement(zc::mv(varDecl));

  zc::Vector<zc::Own<ast::Statement>> stmts;
  stmts.add(zc::mv(declStmt));
  auto body = ast::factory::createBlockStatement(zc::mv(stmts));
  auto& bodyRef = *body;

  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ast::ParameterDeclaration>> params;

  auto funcDecl = ast::factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams),
                                                          zc::mv(params), zc::none, zc::mv(body));
  auto& funcRef = *funcDecl;

  binder.bindFunctionDeclaration(funcRef);

  // Check that symbols were created
  ZC_EXPECT(!test.diagEngine.hasErrors());
  ZC_EXPECT(funcRef.getSymbol() != zc::none);
  ZC_EXPECT(funcRef.getLocals() != zc::none);
  ZC_EXPECT(bodyRef.getLocals() != zc::none);
  ZC_EXPECT(bindingRef.getSymbol() != zc::none);

  bool found = false;
  auto& scopeManager = test.symbolTable.getScopeManager();
  auto blockScopes = scopeManager.getScopesOfKind(symbol::Scope::Kind::Block);
  for (const auto& maybeScope : blockScopes) {
    ZC_IF_SOME(scope, maybeScope) {
      if (test.symbolTable.lookup("x"_zc, scope) != zc::none) {
        found = true;
        break;
      }
    }
  }
  ZC_EXPECT(found);

  // Verify scope structure
  // Note: Since we don't have direct access to internal scope stack or symbol table lookups easily
  // exposed we primarily rely on no errors during binding for this unit test level
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
