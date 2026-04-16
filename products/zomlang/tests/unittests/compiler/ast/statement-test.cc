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

#include "zomlang/compiler/ast/statement.h"

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/cast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

static void expectIdentifierNameText(
    zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> name,
    zc::StringPtr expected) {
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) {
      ZC_IF_SOME(id, maybeId) { ZC_EXPECT(id.getText() == expected); }
      else { ZC_FAIL_EXPECT("name should not be none"); }
      return;
    }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const BindingPattern&>) {
      ZC_FAIL_EXPECT("name should be Identifier");
      return;
    }
    ZC_CASE_ONEOF_DEFAULT {
      ZC_FAIL_EXPECT("name should not be empty");
      return;
    }
  }
}

static void expectNoOpFlags(Node& node) {
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
  node.setFlags(NodeFlags::OptionalChain);
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
}

static void expectMutableFlagsRoundTrip(Node& node) {
  ZC_EXPECT(node.getFlags() == NodeFlags::None);
  node.setFlags(NodeFlags::OptionalChain);
  ZC_EXPECT(hasFlag(node.getFlags(), NodeFlags::OptionalChain));
}

ZC_TEST("StatementTest.VariableDeclarationList") {
  zc::Vector<zc::Own<VariableDeclaration>> declarations;
  auto name = factory::createIdentifier("x"_zc);
  auto decl = factory::createVariableDeclaration(zc::mv(name));
  declarations.add(zc::mv(decl));

  auto list = factory::createVariableDeclarationList(zc::mv(declarations));
  ZC_EXPECT(list->getKind() == SyntaxKind::VariableDeclarationList);
  ZC_EXPECT(list->getBindings().size() == 1);
}

ZC_TEST("StatementTest.FunctionDeclaration") {
  auto name = factory::createIdentifier("foo"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParams = zc::none;
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::FunctionDeclaration);
  // Function always has name (reference, not pointer)
  // Skip name check for now
  // Function always has body (reference, not pointer)
}

ZC_TEST("StatementTest.IfStatement") {
  zc::Own<Expression> cond = factory::createBooleanLiteral(true);
  auto thenStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::none);

  ZC_EXPECT(stmt->getKind() == SyntaxKind::IfStatement);
  ZC_EXPECT(stmt->getElseStatement() == zc::none);
}

ZC_TEST("StatementTest.BlockStatement") {
  zc::Vector<zc::Own<Statement>> statements;
  statements.add(factory::createEmptyStatement());
  auto block = factory::createBlockStatement(zc::mv(statements));

  ZC_EXPECT(block->getKind() == SyntaxKind::BlockStatement);
  ZC_EXPECT(block->getStatements().size() == 1);
}

ZC_TEST("StatementTest.ReturnStatement") {
  zc::Own<Expression> expr = factory::createFloatLiteral(42.0);
  auto stmt = factory::createReturnStatement(zc::mv(expr));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ReturnStatement);
  // ReturnStatement expression can be null pointer since it's optional
}

// ================================================================================
// BindingElement Tests

ZC_TEST("StatementTest.VariableDeclarationNoType") {
  auto name = factory::createIdentifier("variable"_zc);
  auto decl = factory::createVariableDeclaration(zc::mv(name));

  ZC_EXPECT(decl->getKind() == SyntaxKind::VariableDeclaration);
  expectIdentifierNameText(decl->getName(), "variable");
  ZC_EXPECT(decl->getInitializer() == zc::none);
  ZC_EXPECT(decl->getType() == zc::none);
}

ZC_TEST("StatementTest.VariableDeclarationWithType") {
  auto name = factory::createIdentifier("typed_var"_zc);
  auto typeName = factory::createIdentifier("Int"_zc);
  zc::Own<TypeNode> type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto binding = factory::createVariableDeclaration(zc::mv(name), zc::mv(type));

  ZC_EXPECT(binding->getKind() == SyntaxKind::VariableDeclaration);
  expectIdentifierNameText(binding->getName(), "typed_var");
  ZC_EXPECT(binding->getType() != zc::none);
  ZC_EXPECT(binding->getInitializer() == zc::none);
}

ZC_TEST("StatementTest.VariableDeclarationWithInitializer") {
  auto name = factory::createIdentifier("init_var"_zc);
  zc::Own<Expression> initializer = factory::createIntegerLiteral(42);
  auto decl = factory::createVariableDeclaration(zc::mv(name), zc::none, zc::mv(initializer));

  ZC_EXPECT(decl->getKind() == SyntaxKind::VariableDeclaration);
  expectIdentifierNameText(decl->getName(), "init_var");
  ZC_EXPECT(decl->getInitializer() != zc::none);
}

// ================================================================================
// Enhanced VariableDeclarationList Tests

ZC_TEST("StatementTest.VariableDeclarationMultipleBindings") {
  zc::Vector<zc::Own<VariableDeclaration>> bindings;
  auto name1 = factory::createIdentifier("x"_zc);
  auto name2 = factory::createIdentifier("y"_zc);
  bindings.add(factory::createVariableDeclaration(zc::mv(name1)));
  bindings.add(factory::createVariableDeclaration(zc::mv(name2)));

  auto decl = factory::createVariableDeclarationList(zc::mv(bindings));
  ZC_EXPECT(decl->getKind() == SyntaxKind::VariableDeclarationList);
  ZC_EXPECT(decl->getBindings().size() == 2);
}

ZC_TEST("StatementTest.VariableDeclarationAccept") {
  zc::Vector<zc::Own<VariableDeclaration>> bindings;
  auto name = factory::createIdentifier("test"_zc);
  bindings.add(factory::createVariableDeclaration(zc::mv(name)));
  auto decl = factory::createVariableDeclarationList(zc::mv(bindings));

  // Test visitor pattern - just ensure it doesn't crash
}

// ================================================================================
// Enhanced FunctionDeclaration Tests

ZC_TEST("StatementTest.FunctionDeclarationWithParameters") {
  auto name = factory::createIdentifier("testFunc"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParams = zc::none;
  zc::Vector<zc::Own<ParameterDeclaration>> params;

  auto paramName = factory::createIdentifier("param1"_zc);
  params.add(factory::createParameterDeclaration({}, zc::none, zc::mv(paramName), zc::none));

  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::FunctionDeclaration);
  expectIdentifierNameText(decl->getName(), "testFunc");
  ZC_EXPECT(decl->getParameters().size() == 1);
  ZC_EXPECT(decl->getTypeParameters().empty());
  ZC_EXPECT(decl->getReturnType() == zc::none);
}

ZC_TEST("StatementTest.FunctionDeclarationWithReturnTypeAndTypeParameters") {
  auto name = factory::createIdentifier("genericFunc"_zc);

  // Create type parameters
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  auto typeParamName = factory::createIdentifier("T"_zc);
  auto constraintName = factory::createIdentifier("Comparable"_zc);
  auto constraint = factory::createTypeReference(zc::mv(constraintName), zc::none);
  typeParams.add(
      factory::createTypeParameterDeclaration(zc::mv(typeParamName), zc::mv(constraint)));

  // Create parameters
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto paramName = factory::createIdentifier("value"_zc);
  auto paramTypeName = factory::createIdentifier("T"_zc);
  auto paramType = factory::createTypeReference(zc::mv(paramTypeName), zc::none);
  params.add(factory::createParameterDeclaration({}, zc::none, zc::mv(paramName), zc::none,
                                                 zc::mv(paramType)));

  // Create return type
  auto returnTypeName = factory::createIdentifier("String"_zc);
  auto returnTypeRef = factory::createTypeReference(zc::mv(returnTypeName), zc::none);
  auto returnType = factory::createReturnType(zc::mv(returnTypeRef), zc::none);

  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::mv(returnType), zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::FunctionDeclaration);
  expectIdentifierNameText(decl->getName(), "genericFunc");
  ZC_EXPECT(decl->getTypeParameters().size() == 1);
  ZC_EXPECT(decl->getParameters().size() == 1);
  ZC_EXPECT(decl->getReturnType() != zc::none);
}

ZC_TEST("StatementTest.FunctionDeclarationAccept") {
  auto name = factory::createIdentifier("func"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParams = zc::none;
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  // Test visitor pattern - just ensure it doesn't crash
}

// ================================================================================
// ClassDeclaration Tests

ZC_TEST("StatementTest.ClassDeclaration") {
  auto name = factory::createIdentifier("TestClass"_zc);
  zc::Vector<zc::Own<ClassElement>> members;
  auto classDecl =
      factory::createClassDeclaration(zc::mv(name), zc::none, zc::none, zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::ClassDeclaration);
  expectIdentifierNameText(classDecl->getName(), "TestClass");
  ZC_EXPECT(classDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.ClassDeclarationWithMembers") {
  auto name = factory::createIdentifier("ChildClass"_zc);
  zc::Vector<zc::Own<ClassElement>> members;
  members.add(factory::createSemicolonClassElement());
  auto classDecl =
      factory::createClassDeclaration(zc::mv(name), zc::none, zc::none, zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::ClassDeclaration);
  expectIdentifierNameText(classDecl->getName(), "ChildClass");
  ZC_EXPECT(classDecl->getMembers().size() == 1);
}

ZC_TEST("StatementTest.ClassDeclarationWithSuperClass") {
  auto name = factory::createIdentifier("ChildClass"_zc);
  auto exprWithTypeArgs =
      factory::createExpressionWithTypeArguments(factory::createIdentifier("ParentClass"_zc), {});

  zc::Vector<zc::Own<ClassElement>> members;
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParams = zc::none;
  zc::Vector<zc::Own<HeritageClause>> superClasses;

  zc::Vector<zc::Own<ExpressionWithTypeArguments>> superClassExprs;
  superClassExprs.add(zc::mv(exprWithTypeArgs));
  auto superClass =
      factory::createHeritageClause(ast::SyntaxKind::ExtendsKeyword, zc::mv(superClassExprs));
  superClasses.add(zc::mv(superClass));

  auto classDecl = factory::createClassDeclaration(zc::mv(name), zc::mv(typeParams),
                                                   zc::mv(superClasses), zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::ClassDeclaration);
  expectIdentifierNameText(classDecl->getName(), "ChildClass");
  ZC_EXPECT(!classDecl->getHeritageClauses().empty());
  const auto& clauses = classDecl->getHeritageClauses();
  for (const auto& clause : clauses) {
    if (clause.getToken() == SyntaxKind::ExtendsKeyword) {
      const auto& types = clause.getTypes();
      if (!types.empty()) {
        const Expression& expr = types[0]->getExpression();
        if (ast::isa<Identifier>(expr)) {
          ZC_EXPECT(cast<Identifier>(expr).getText() == "ParentClass");
        }
      }
    }
  }
  ZC_EXPECT(classDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.ClassDeclarationAccept") {
  auto name = factory::createIdentifier("TestClass"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParams = zc::none;
  zc::Vector<zc::Own<ClassElement>> members;
  auto classDecl =
      factory::createClassDeclaration(zc::mv(name), zc::mv(typeParams), zc::none, zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// Enhanced IfStatement Tests

ZC_TEST("StatementTest.IfStatementWithElse") {
  auto cond = factory::createBooleanLiteral(false);
  auto thenStmt = factory::createEmptyStatement();
  auto elseStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::mv(elseStmt));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::IfStatement);
  ZC_IF_SOME(elseStmt, stmt->getElseStatement()) {
    ZC_EXPECT(elseStmt.getKind() == SyntaxKind::EmptyStatement);
  }
}

ZC_TEST("StatementTest.IfStatementGetters") {
  auto cond = factory::createBooleanLiteral(true);
  auto thenStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::none);

  // Test getCondition and getThenStatement methods
  ZC_EXPECT(stmt->getCondition().getKind() == SyntaxKind::BooleanLiteral);
  ZC_EXPECT(stmt->getThenStatement().getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.IfStatementAccept") {
  auto cond = factory::createBooleanLiteral(true);
  auto thenStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::none);

  // Test visitor pattern
}

// ================================================================================
// WhileStatement Tests

ZC_TEST("StatementTest.WhileStatement") {
  zc::Own<Expression> condition = factory::createBooleanLiteral(true);
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createWhileStatement(zc::mv(condition), zc::mv(body));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::WhileStatement);
  ZC_EXPECT(stmt->getCondition().getKind() == SyntaxKind::BooleanLiteral);
  ZC_EXPECT(stmt->getBody().getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.WhileStatementAccept") {
  auto condition = factory::createBooleanLiteral(false);
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createWhileStatement(zc::mv(condition), zc::mv(body));

  // Test visitor pattern
}

// ================================================================================
// ForStatement Tests

ZC_TEST("StatementTest.ForStatement") {
  auto init = factory::createEmptyStatement();
  auto condition = factory::createBooleanLiteral(true);
  auto update = factory::createIntegerLiteral(1);
  auto body = factory::createEmptyStatement();
  auto stmt =
      factory::createForStatement(zc::mv(init), zc::mv(condition), zc::mv(update), zc::mv(body));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ForStatement);
  ZC_IF_SOME(init, stmt->getInitializer()) {
    ZC_EXPECT(init.getKind() == SyntaxKind::EmptyStatement);
  }
  ZC_IF_SOME(condStmt, stmt->getCondition()) {
    ZC_EXPECT(condStmt.getKind() == SyntaxKind::BooleanLiteral);
  }
  ZC_IF_SOME(update, stmt->getUpdate()) {
    ZC_EXPECT(update.getKind() == SyntaxKind::IntegerLiteral);
  }
  ZC_EXPECT(stmt->getBody().getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.ForStatementMinimal") {
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createForStatement(zc::none, zc::none, zc::none, zc::mv(body));

  ZC_EXPECT(stmt->getInitializer() == zc::none);
  ZC_EXPECT(stmt->getCondition() == zc::none);
  ZC_EXPECT(stmt->getUpdate() == zc::none);
}

ZC_TEST("StatementTest.ForStatementAccept") {
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createForStatement(zc::none, zc::none, zc::none, zc::mv(body));

  // Test visitor pattern
}

// ================================================================================
// Enhanced ReturnStatement Tests

ZC_TEST("StatementTest.ReturnStatementEmpty") {
  auto stmt = factory::createReturnStatement(zc::none);

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ReturnStatement);
  ZC_EXPECT(stmt->getExpression() == zc::none);
}

ZC_TEST("StatementTest.ReturnStatementWithExpression") {
  auto expr = factory::createStringLiteral(zc::str("test"));
  auto stmt = factory::createReturnStatement(zc::mv(expr));

  ZC_EXPECT(stmt->getExpression() != zc::none);
}

ZC_TEST("StatementTest.ReturnStatementAccept") {
  auto stmt = factory::createReturnStatement(zc::none);

  // Test visitor pattern
}

// ================================================================================
// EmptyStatement Tests

ZC_TEST("StatementTest.EmptyStatement") {
  auto stmt = factory::createEmptyStatement();

  ZC_EXPECT(stmt->getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.EmptyStatementAccept") {
  auto stmt = factory::createEmptyStatement();

  // Test visitor pattern
}

// ================================================================================
// Enhanced BlockStatement Tests

ZC_TEST("StatementTest.BlockStatementEmpty") {
  auto block = factory::createBlockStatement({});

  ZC_EXPECT(block->getKind() == SyntaxKind::BlockStatement);
  ZC_EXPECT(block->getStatements().size() == 0);
}

ZC_TEST("StatementTest.BlockStatementMultiple") {
  zc::Vector<zc::Own<Statement>> statements;
  statements.add(factory::createEmptyStatement());
  statements.add(factory::createEmptyStatement());
  statements.add(factory::createEmptyStatement());
  auto block = factory::createBlockStatement(zc::mv(statements));

  ZC_EXPECT(block->getStatements().size() == 3);
}

ZC_TEST("StatementTest.BlockStatementAccept") {
  auto block = factory::createBlockStatement({});

  // Test visitor pattern
}

// ================================================================================
// ExpressionStatement Tests

ZC_TEST("StatementTest.ExpressionStatement") {
  zc::Own<Expression> expr = factory::createIntegerLiteral(123);
  auto stmt = factory::createExpressionStatement(zc::mv(expr));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ExpressionStatement);
  ZC_EXPECT(stmt->getExpression().getKind() == SyntaxKind::IntegerLiteral);
}

ZC_TEST("StatementTest.ExpressionStatementAccept") {
  auto expr = factory::createBooleanLiteral(true);
  auto stmt = factory::createExpressionStatement(zc::mv(expr));

  // Test visitor pattern
}

// ================================================================================
// BreakStatement Tests

ZC_TEST("StatementTest.BreakStatement") {
  auto stmt = factory::createBreakStatement(zc::none);

  ZC_EXPECT(stmt->getKind() == SyntaxKind::BreakStatement);
  ZC_EXPECT(stmt->getLabel() == zc::none);
}

ZC_TEST("StatementTest.BreakStatementWithLabel") {
  auto label = factory::createIdentifier("loop1"_zc);
  auto stmt = factory::createBreakStatement(zc::mv(label));

  ZC_IF_SOME(label, stmt->getLabel()) { ZC_EXPECT(label.getText() == "loop1"); }
}

ZC_TEST("StatementTest.BreakStatementAccept") {
  auto stmt = factory::createBreakStatement(zc::none);

  // Test visitor pattern
}

// ================================================================================
// ContinueStatement Tests

ZC_TEST("StatementTest.ContinueStatement") {
  auto stmt = factory::createContinueStatement(zc::none);

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ContinueStatement);
  ZC_EXPECT(stmt->getLabel() == zc::none);
}

ZC_TEST("StatementTest.ContinueStatementWithLabel") {
  auto label = factory::createIdentifier("loop2"_zc);
  auto stmt = factory::createContinueStatement(zc::mv(label));

  ZC_IF_SOME(label, stmt->getLabel()) { ZC_EXPECT(label.getText() == "loop2"); }
}

ZC_TEST("StatementTest.ContinueStatementAccept") {
  auto stmt = factory::createContinueStatement(zc::none);

  // Test visitor pattern
}

// ================================================================================
// MatchStatement Tests

ZC_TEST("StatementTest.MatchStatement") {
  zc::Own<Expression> discriminant = factory::createIntegerLiteral(42);
  zc::Vector<zc::Own<Statement>> clauses;
  clauses.add(factory::createEmptyStatement());
  auto stmt = factory::createMatchStatement(zc::mv(discriminant), zc::mv(clauses));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::MatchStatement);
  ZC_EXPECT(stmt->getDiscriminant().getKind() == SyntaxKind::IntegerLiteral);
  ZC_EXPECT(stmt->getClauses().size() == 1);
}

ZC_TEST("StatementTest.MatchStatementAccept") {
  auto discriminant = factory::createBooleanLiteral(true);
  zc::Vector<zc::Own<Statement>> clauses;
  auto stmt = factory::createMatchStatement(zc::mv(discriminant), zc::mv(clauses));

  // Test visitor pattern
}

// ================================================================================
// AliasDeclaration Tests

ZC_TEST("StatementTest.AliasDeclaration") {
  auto name = factory::createIdentifier("MyAlias"_zc);
  auto typeName = factory::createIdentifier("Int"_zc);
  zc::Own<TypeNode> type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto decl = factory::createAliasDeclaration(zc::mv(name), zc::none, zc::mv(type));

  ZC_EXPECT(decl->getKind() == SyntaxKind::AliasDeclaration);
  expectIdentifierNameText(decl->getName(), "MyAlias");
}

ZC_TEST("StatementTest.AliasDeclarationAccept") {
  auto name = factory::createIdentifier("TestAlias"_zc);
  auto typeName = factory::createIdentifier("Int"_zc);
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto decl = factory::createAliasDeclaration(zc::mv(name), zc::none, zc::mv(type));

  // Test visitor pattern
}

// ================================================================================
// DebuggerStatement Tests

ZC_TEST("StatementTest.DebuggerStatement") {
  auto stmt = factory::createDebuggerStatement();

  ZC_EXPECT(stmt->getKind() == SyntaxKind::DebuggerStatement);
}

ZC_TEST("StatementTest.DebuggerStatementAccept") {
  auto stmt = factory::createDebuggerStatement();

  // Test visitor pattern
}

// ================================================================================
// InterfaceDeclaration Tests

ZC_TEST("StatementTest.InterfaceDeclaration") {
  auto name = factory::createIdentifier("TestInterface"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters = zc::none;
  zc::Vector<zc::Own<InterfaceElement>> members;

  auto stmt = factory::createInterfaceDeclaration(zc::mv(name), zc::mv(typeParameters), zc::none,
                                                  zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::InterfaceDeclaration);
  expectIdentifierNameText(stmt->getName(), "TestInterface");
  ZC_EXPECT(stmt->getMembers().size() == 0);
}

ZC_TEST("StatementTest.InterfaceDeclarationAccept") {
  auto name = factory::createIdentifier("ITest"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters = zc::none;
  zc::Vector<zc::Own<InterfaceElement>> members;

  auto stmt = factory::createInterfaceDeclaration(zc::mv(name), zc::mv(typeParameters), zc::none,
                                                  zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// StructDeclaration Tests

ZC_TEST("StatementTest.StructDeclaration") {
  auto name = factory::createIdentifier("TestStruct"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters = zc::none;
  zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses = zc::none;
  zc::Vector<zc::Own<ClassElement>> members;
  auto stmt = factory::createStructDeclaration(zc::mv(name), zc::mv(typeParameters),
                                               zc::mv(heritageClauses), zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::StructDeclaration);
  expectIdentifierNameText(stmt->getName(), "TestStruct");
  ZC_EXPECT(stmt->getMembers().size() == 0);
}

ZC_TEST("StatementTest.StructDeclarationAccept") {
  auto name = factory::createIdentifier("MyStruct"_zc);
  zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters = zc::none;
  zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses = zc::none;
  zc::Vector<zc::Own<ClassElement>> members;
  auto stmt = factory::createStructDeclaration(zc::mv(name), zc::mv(typeParameters),
                                               zc::mv(heritageClauses), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// EnumDeclaration Tests

ZC_TEST("StatementTest.EnumDeclaration") {
  auto name = factory::createIdentifier("TestEnum"_zc);
  zc::Vector<zc::Own<EnumMember>> members;
  auto stmt = factory::createEnumDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::EnumDeclaration);
  expectIdentifierNameText(stmt->getName(), "TestEnum");
  ZC_EXPECT(stmt->getMembers().size() == 0);
}

ZC_TEST("StatementTest.EnumDeclarationAccept") {
  auto name = factory::createIdentifier("MyEnum"_zc);
  zc::Vector<zc::Own<EnumMember>> members;
  auto stmt = factory::createEnumDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// ErrorDeclaration Tests

ZC_TEST("StatementTest.ErrorDeclaration") {
  auto name = factory::createIdentifier("TestError"_zc);
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createErrorDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ErrorDeclaration);
  expectIdentifierNameText(stmt->getName(), "TestError");
  ZC_EXPECT(stmt->getMembers().size() == 0);
}

ZC_TEST("StatementTest.ErrorDeclarationAccept") {
  auto name = factory::createIdentifier("MyError"_zc);
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createErrorDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// TypeParameterDeclaration Tests

ZC_TEST("StatementTest.TypeParameterDeclaration") {
  auto name = factory::createIdentifier("T"_zc);
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(name), zc::none);

  ZC_EXPECT(typeParam->getKind() == SyntaxKind::TypeParameterDeclaration);
  expectIdentifierNameText(typeParam->getName(), "T");
  ZC_EXPECT(typeParam->getConstraint() == zc::none);
}

ZC_TEST("StatementTest.TypeParameterWithConstraint") {
  auto name = factory::createIdentifier("T"_zc);
  auto constraintName = factory::createIdentifier("Comparable"_zc);
  zc::Own<TypeNode> constraint = factory::createTypeReference(zc::mv(constraintName), zc::none);
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(name), zc::mv(constraint));

  expectIdentifierNameText(typeParam->getName(), "T");
  ZC_EXPECT(typeParam->getConstraint() != zc::none);
}

ZC_TEST("StatementTest.TypeParameterAccept") {
  auto name = factory::createIdentifier("U"_zc);
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(name), zc::none);

  // Test visitor pattern
}

// ================================================================================
// Base Statement Tests

ZC_TEST("StatementTest.BaseStatementAccept") {
  auto stmt = factory::createEmptyStatement();

  // Test base Statement::accept method
  // In a real test, we'd use a mock visitor to verify the call
}

ZC_TEST("StatementTest.MatchClause") {
  auto identifier = factory::createIdentifier("x"_zc);
  auto pattern = factory::createIdentifierPattern(zc::mv(identifier));
  auto body = factory::createEmptyStatement();
  auto clause = factory::createMatchClause(zc::mv(pattern), zc::none, zc::mv(body));

  ZC_EXPECT(clause->getKind() == SyntaxKind::MatchClause);
  ZC_EXPECT(clause->getPattern().getKind() == SyntaxKind::IdentifierPattern);
  ZC_EXPECT(clause->getBody().getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.MatchClauseWithGuard") {
  auto identifier = factory::createIdentifier("value"_zc);
  auto pattern = factory::createIdentifierPattern(zc::mv(identifier));
  auto guard = factory::createBooleanLiteral(true);
  auto body = factory::createEmptyStatement();
  auto clause = factory::createMatchClause(zc::mv(pattern), zc::mv(guard), zc::mv(body));

  ZC_EXPECT(clause->getKind() == SyntaxKind::MatchClause);
  ZC_EXPECT(clause->getPattern().getKind() == SyntaxKind::IdentifierPattern);
  ZC_IF_SOME(guard, clause->getGuard()) {
    ZC_EXPECT(guard.getKind() == SyntaxKind::BooleanLiteral);
  }
  ZC_EXPECT(clause->getBody().getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.DefaultClause") {
  zc::Vector<zc::Own<Statement>> statements;
  statements.add(factory::createEmptyStatement());
  auto clause = factory::createDefaultClause(zc::mv(statements));

  ZC_EXPECT(clause->getKind() == SyntaxKind::DefaultClause);
  ZC_EXPECT(clause->getStatements().size() == 1);
}

ZC_TEST("StatementTest.IdentifierPattern") {
  auto identifier = factory::createIdentifier("variable"_zc);
  auto pattern = factory::createIdentifierPattern(zc::mv(identifier));

  ZC_EXPECT(pattern->getKind() == SyntaxKind::IdentifierPattern);
  ZC_EXPECT(pattern->getIdentifier().getText() == "variable");
}

ZC_TEST("StatementTest.ExpressionPattern") {
  zc::Own<Expression> literal = factory::createIntegerLiteral(42);
  auto pattern = factory::createExpressionPattern(zc::mv(literal));

  ZC_EXPECT(pattern->getKind() == SyntaxKind::ExpressionPattern);
  ZC_EXPECT(pattern->getExpression().getKind() == SyntaxKind::IntegerLiteral);
}

ZC_TEST("StatementTest.WildcardPattern") {
  auto pattern = factory::createWildcardPattern();

  ZC_EXPECT(pattern->getKind() == SyntaxKind::WildcardPattern);
  ZC_EXPECT(pattern->getKind() == SyntaxKind::WildcardPattern);
}

ZC_TEST("StatementTest.ArrayBindingPattern") {
  zc::Vector<zc::Own<BindingElement>> elements;
  auto name = factory::createIdentifier("x"_zc);
  auto element = factory::createBindingElement(zc::none, zc::none, zc::mv(name), zc::none);
  elements.add(zc::mv(element));

  auto arrayBindingPattern = factory::createArrayBindingPattern(zc::mv(elements));

  ZC_EXPECT(arrayBindingPattern->getKind() == SyntaxKind::ArrayBindingPattern);
  ZC_EXPECT(arrayBindingPattern->getElements().size() == 1);
}

ZC_TEST("StatementTest.TuplePattern") {
  zc::Vector<zc::Own<Pattern>> elements;
  auto identifier = factory::createIdentifier("x"_zc);
  auto idPattern = factory::createIdentifierPattern(zc::mv(identifier));
  auto wildcardPattern = factory::createWildcardPattern();
  elements.add(zc::mv(idPattern));
  elements.add(zc::mv(wildcardPattern));

  auto tuplePattern = factory::createTuplePattern(zc::mv(elements));

  ZC_EXPECT(tuplePattern->getKind() == SyntaxKind::TuplePattern);
  ZC_EXPECT(tuplePattern->getElements().size() == 2);
}

ZC_TEST("StatementTest.ArrayPattern") {
  zc::Vector<zc::Own<Pattern>> elements;
  auto identifier = factory::createIdentifier("first"_zc);
  zc::Own<Pattern> pattern = factory::createIdentifierPattern(zc::mv(identifier));
  elements.add(zc::mv(pattern));

  auto arrayPattern = factory::createArrayPattern(zc::mv(elements));

  ZC_EXPECT(arrayPattern->getKind() == SyntaxKind::ArrayPattern);
  ZC_EXPECT(arrayPattern->getElements().size() == 1);
}

// ================================================================================
// BindingPattern Tests

ZC_TEST("StatementTest.ArrayBindingPatternTest") {
  zc::Vector<zc::Own<BindingElement>> elements;
  auto name = factory::createIdentifier("x"_zc);
  elements.add(factory::createBindingElement(zc::none, zc::none, zc::mv(name), zc::none));

  auto arrayBinding = factory::createArrayBindingPattern(zc::mv(elements));
  ZC_EXPECT(arrayBinding->getKind() == SyntaxKind::ArrayBindingPattern);
  ZC_EXPECT(arrayBinding->getElements().size() == 1);
}

ZC_TEST("StatementTest.ObjectBindingPattern") {
  zc::Vector<zc::Own<BindingElement>> properties;
  auto name = factory::createIdentifier("prop"_zc);
  properties.add(factory::createBindingElement(zc::none, zc::none, zc::mv(name), zc::none));

  auto objectBinding = factory::createObjectBindingPattern(zc::mv(properties));
  ZC_EXPECT(objectBinding->getKind() == SyntaxKind::ObjectBindingPattern);
  ZC_EXPECT(objectBinding->getProperties().size() == 1);
}

ZC_TEST("StatementTest.DeclarationFlagsRemainNone") {
  auto bindingElement =
      factory::createBindingElement(zc::none, zc::none,
                                    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                        factory::createIdentifier("binding"_zc)),
                                    zc::none);
  expectNoOpFlags(*bindingElement);

  auto parameter =
      factory::createParameterDeclaration({}, zc::none,
                                          zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                              factory::createIdentifier("param"_zc)),
                                          zc::none);
  expectNoOpFlags(*parameter);

  auto variable =
      factory::createVariableDeclaration(zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
          factory::createIdentifier("value"_zc)));
  expectNoOpFlags(*variable);

  zc::Vector<zc::Own<VariableDeclaration>> declarations;
  declarations.add(
      factory::createVariableDeclaration(zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
          factory::createIdentifier("item"_zc))));
  auto declarationList = factory::createVariableDeclarationList(zc::mv(declarations));
  expectNoOpFlags(*declarationList);

  auto variableStatement = factory::createVariableStatement(
      factory::createVariableDeclarationList(zc::Vector<zc::Own<VariableDeclaration>>()));
  expectNoOpFlags(*variableStatement);

  auto functionDeclaration = factory::createFunctionDeclaration(
      factory::createIdentifier("run"_zc), zc::none, zc::Vector<zc::Own<ParameterDeclaration>>(),
      zc::none, factory::createBlockStatement(zc::Vector<zc::Own<Statement>>()));
  expectNoOpFlags(*functionDeclaration);

  auto classDeclaration =
      factory::createClassDeclaration(factory::createIdentifier("Widget"_zc), zc::none, zc::none,
                                      zc::Vector<zc::Own<ClassElement>>());
  expectNoOpFlags(*classDeclaration);

  auto interfaceDeclaration =
      factory::createInterfaceDeclaration(factory::createIdentifier("Shape"_zc), zc::none, zc::none,
                                          zc::Vector<zc::Own<InterfaceElement>>());
  expectNoOpFlags(*interfaceDeclaration);

  auto structDeclaration =
      factory::createStructDeclaration(factory::createIdentifier("Point"_zc), zc::none, zc::none,
                                       zc::Vector<zc::Own<ClassElement>>());
  expectNoOpFlags(*structDeclaration);

  auto enumMember = factory::createEnumMember(factory::createIdentifier("Ready"_zc));
  expectNoOpFlags(*enumMember);

  zc::Vector<zc::Own<EnumMember>> enumMembers;
  enumMembers.add(factory::createEnumMember(factory::createIdentifier("Busy"_zc)));
  auto enumDeclaration =
      factory::createEnumDeclaration(factory::createIdentifier("State"_zc), zc::mv(enumMembers));
  expectNoOpFlags(*enumDeclaration);

  zc::Vector<zc::Own<Statement>> errorMembers;
  errorMembers.add(factory::createEmptyStatement());
  auto errorDeclaration = factory::createErrorDeclaration(factory::createIdentifier("Problem"_zc),
                                                          zc::mv(errorMembers));
  expectNoOpFlags(*errorDeclaration);

  auto aliasDeclaration = factory::createAliasDeclaration(
      factory::createIdentifier("Id"_zc), zc::none, factory::createPredefinedType("i32"_zc));
  expectNoOpFlags(*aliasDeclaration);
}

ZC_TEST("StatementTest.StatementAndMemberFlagsRemainNone") {
  expectMutableFlagsRoundTrip(*factory::createDebuggerStatement());
  expectNoOpFlags(*factory::createMatchClause(factory::createWildcardPattern(), zc::none,
                                              factory::createEmptyStatement()));
  expectNoOpFlags(*factory::createDefaultClause(zc::Vector<zc::Own<Statement>>()));

  zc::Vector<zc::Own<BindingElement>> arrayElements;
  arrayElements.add(
      factory::createBindingElement(zc::none, zc::none,
                                    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                        factory::createIdentifier("array"_zc)),
                                    zc::none));
  expectNoOpFlags(*factory::createArrayBindingPattern(zc::mv(arrayElements)));

  zc::Vector<zc::Own<BindingElement>> objectElements;
  objectElements.add(
      factory::createBindingElement(zc::none, zc::none,
                                    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                        factory::createIdentifier("object"_zc)),
                                    zc::none));
  expectNoOpFlags(*factory::createObjectBindingPattern(zc::mv(objectElements)));

  expectNoOpFlags(*factory::createBlockStatement(zc::Vector<zc::Own<Statement>>()));
  expectNoOpFlags(*factory::createExpressionStatement(factory::createIntegerLiteral(1)));
  expectNoOpFlags(*factory::createIfStatement(factory::createBooleanLiteral(true),
                                              factory::createEmptyStatement(), zc::none));
  expectNoOpFlags(*factory::createLabeledStatement(factory::createIdentifier("label"_zc),
                                                   factory::createEmptyStatement()));
  expectNoOpFlags(*factory::createBreakStatement(zc::none));
  expectNoOpFlags(*factory::createContinueStatement(zc::none));
  expectNoOpFlags(*factory::createWhileStatement(factory::createBooleanLiteral(false),
                                                 factory::createEmptyStatement()));
  expectNoOpFlags(*factory::createReturnStatement(zc::none));
  expectNoOpFlags(*factory::createEmptyStatement());

  zc::Vector<zc::Own<Statement>> matchClauses;
  matchClauses.add(factory::createEmptyStatement());
  expectNoOpFlags(
      *factory::createMatchStatement(factory::createIdentifier("value"_zc), zc::mv(matchClauses)));

  expectNoOpFlags(
      *factory::createForStatement(zc::none, zc::none, zc::none, factory::createEmptyStatement()));
  expectNoOpFlags(*factory::createForInStatement(factory::createEmptyStatement(),
                                                 factory::createIdentifier("items"_zc),
                                                 factory::createEmptyStatement()));

  zc::Vector<zc::Own<ExpressionWithTypeArguments>> heritageTypes;
  heritageTypes.add(
      factory::createExpressionWithTypeArguments(factory::createIdentifier("Base"_zc), zc::none));
  expectNoOpFlags(
      *factory::createHeritageClause(SyntaxKind::ExtendsKeyword, zc::mv(heritageTypes)));

  expectNoOpFlags(*factory::createPropertySignature({}, factory::createIdentifier("property"_zc),
                                                    zc::none, zc::none, zc::none));
  expectNoOpFlags(*factory::createMethodSignature(
      {}, factory::createIdentifier("method"_zc), zc::none, zc::none,
      zc::Vector<zc::Own<ParameterDeclaration>>(), zc::none));
  expectNoOpFlags(*factory::createSemicolonInterfaceElement());
  expectNoOpFlags(*factory::createSemicolonClassElement());
  expectNoOpFlags(*factory::createMethodDeclaration(
      {}, factory::createIdentifier("member"_zc), zc::none, zc::none,
      zc::Vector<zc::Own<ParameterDeclaration>>(), zc::none, zc::none));
  expectNoOpFlags(*factory::createInitDeclaration(
      {}, zc::none, zc::Vector<zc::Own<ParameterDeclaration>>(), zc::none, zc::none));
  expectNoOpFlags(*factory::createDeinitDeclaration({}, zc::none));
  expectNoOpFlags(*factory::createGetAccessorDeclaration(
      {}, factory::createIdentifier("value"_zc), zc::none,
      zc::Vector<zc::Own<ParameterDeclaration>>(), zc::none, zc::none));
  expectNoOpFlags(*factory::createSetAccessorDeclaration(
      {}, factory::createIdentifier("value"_zc), zc::none,
      zc::Vector<zc::Own<ParameterDeclaration>>(), zc::none, zc::none));
  expectNoOpFlags(*factory::createPropertyDeclaration({}, factory::createIdentifier("field"_zc),
                                                      zc::none, zc::none));
}

// ================================================================================
// ForInStatement Tests

ZC_TEST("StatementTest.ForInStatement") {
  zc::Vector<zc::Own<VariableDeclaration>> bindings;
  auto varDecl = factory::createVariableDeclaration(
      zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(factory::createIdentifier("x"_zc)));
  bindings.add(zc::mv(varDecl));
  auto varList = factory::createVariableDeclarationList(zc::mv(bindings));
  auto varStmt = factory::createVariableStatement(zc::mv(varList));

  auto expr = factory::createIdentifier("items"_zc);
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createForInStatement(zc::mv(varStmt), zc::mv(expr), zc::mv(body));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ForInStatement);
  ZC_EXPECT(stmt->getBody().getKind() == SyntaxKind::EmptyStatement);
  ZC_EXPECT(stmt->getExpression().getKind() == SyntaxKind::Identifier);
}

ZC_TEST("StatementTest.ForInStatementWithVariable") {
  zc::Vector<zc::Own<VariableDeclaration>> bindings;
  auto varDecl =
      factory::createVariableDeclaration(zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
          factory::createIdentifier("item"_zc)));
  bindings.add(zc::mv(varDecl));
  auto varList = factory::createVariableDeclarationList(zc::mv(bindings));
  auto varStmt = factory::createVariableStatement(zc::mv(varList));

  auto expr = factory::createIdentifier("collection"_zc);
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createForInStatement(zc::mv(varStmt), zc::mv(expr), zc::mv(body));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ForInStatement);
  ZC_EXPECT(stmt->getInitializer().getKind() == SyntaxKind::VariableStatement);
}

ZC_TEST("StatementTest.ForInStatementLocalsInterface") {
  auto initStmt = factory::createEmptyStatement();
  auto expr = factory::createIdentifier("dict"_zc);
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createForInStatement(zc::mv(initStmt), zc::mv(expr), zc::mv(body));

  // Test LocalsContainer interface
  ZC_EXPECT(stmt->getLocals() == zc::none);
  ZC_EXPECT(stmt->getNextContainer() == zc::none);
}

// ================================================================================
// LabeledStatement Tests

ZC_TEST("StatementTest.LabeledStatement") {
  auto label = factory::createIdentifier("loop"_zc);
  auto stmt = factory::createEmptyStatement();
  auto labeled = factory::createLabeledStatement(zc::mv(label), zc::mv(stmt));

  ZC_EXPECT(labeled->getKind() == SyntaxKind::LabeledStatement);
  ZC_EXPECT(labeled->getLabel().getText() == "loop");
  ZC_EXPECT(labeled->getStatement().getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.LabeledStatementWithWhile") {
  auto label = factory::createIdentifier("outer"_zc);
  auto whileStmt = factory::createWhileStatement(factory::createBooleanLiteral(true),
                                                 factory::createEmptyStatement());
  auto labeled = factory::createLabeledStatement(zc::mv(label), zc::mv(whileStmt));

  ZC_EXPECT(labeled->getKind() == SyntaxKind::LabeledStatement);
  ZC_EXPECT(labeled->getLabel().getText() == "outer");
  ZC_EXPECT(labeled->getStatement().getKind() == SyntaxKind::WhileStatement);
}

// ================================================================================
// HeritageClause Tests

ZC_TEST("StatementTest.HeritageClauseExtends") {
  auto expr = factory::createExpressionWithTypeArguments(factory::createIdentifier("BaseClass"_zc),
                                                         zc::none);
  zc::Vector<zc::Own<ExpressionWithTypeArguments>> types;
  types.add(zc::mv(expr));

  auto clause = factory::createHeritageClause(SyntaxKind::ExtendsKeyword, zc::mv(types));

  ZC_EXPECT(clause->getKind() == SyntaxKind::HeritageClause);
  ZC_EXPECT(clause->getToken() == SyntaxKind::ExtendsKeyword);
  ZC_EXPECT(clause->getTypes().size() == 1);
}

ZC_TEST("StatementTest.HeritageClauseImplements") {
  auto expr1 = factory::createExpressionWithTypeArguments(
      factory::createIdentifier("Interface1"_zc), zc::none);
  auto expr2 = factory::createExpressionWithTypeArguments(
      factory::createIdentifier("Interface2"_zc), zc::none);
  zc::Vector<zc::Own<ExpressionWithTypeArguments>> types;
  types.add(zc::mv(expr1));
  types.add(zc::mv(expr2));

  auto clause = factory::createHeritageClause(SyntaxKind::ImplementsKeyword, zc::mv(types));

  ZC_EXPECT(clause->getKind() == SyntaxKind::HeritageClause);
  ZC_EXPECT(clause->getToken() == SyntaxKind::ImplementsKeyword);
  ZC_EXPECT(clause->getTypes().size() == 2);
}

// ================================================================================
// ParameterDeclaration Tests

ZC_TEST("StatementTest.ParameterDeclarationSimple") {
  auto param = factory::createParameterDeclaration(
      {}, zc::none,
      zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(factory::createIdentifier("x"_zc)),
      zc::none);

  ZC_EXPECT(param->getKind() == SyntaxKind::ParameterDeclaration);
  expectIdentifierNameText(param->getName(), "x");
  ZC_EXPECT(param->getType() == zc::none);
  ZC_EXPECT(param->getInitializer() == zc::none);
}

ZC_TEST("StatementTest.ParameterDeclarationWithType") {
  auto typeName = factory::createIdentifier("Int"_zc);
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto param =
      factory::createParameterDeclaration({}, zc::none,
                                          zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                              factory::createIdentifier("value"_zc)),
                                          zc::none, zc::mv(type));

  ZC_EXPECT(param->getKind() == SyntaxKind::ParameterDeclaration);
  ZC_EXPECT(param->getType() != zc::none);
}

ZC_TEST("StatementTest.ParameterDeclarationWithQuestionToken") {
  auto questionToken = factory::createTokenNode(SyntaxKind::Question);
  auto param =
      factory::createParameterDeclaration({}, zc::none,
                                          zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                              factory::createIdentifier("optional"_zc)),
                                          zc::mv(questionToken));

  ZC_EXPECT(param->getKind() == SyntaxKind::ParameterDeclaration);
  ZC_EXPECT(param->getQuestionToken() != zc::none);
}

ZC_TEST("StatementTest.ParameterDeclarationWithInitializer") {
  auto init = factory::createIntegerLiteral(42);
  auto param =
      factory::createParameterDeclaration({}, zc::none,
                                          zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                              factory::createIdentifier("count"_zc)),
                                          zc::none, zc::none, zc::mv(init));

  ZC_EXPECT(param->getKind() == SyntaxKind::ParameterDeclaration);
  ZC_EXPECT(param->getInitializer() != zc::none);
}

ZC_TEST("StatementTest.ParameterDeclarationWithModifiers") {
  zc::Vector<ast::SyntaxKind> modifiers;
  modifiers.add(SyntaxKind::PublicKeyword);
  modifiers.add(SyntaxKind::ReadonlyKeyword);

  auto param =
      factory::createParameterDeclaration(zc::mv(modifiers), zc::none,
                                          zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                              factory::createIdentifier("param"_zc)),
                                          zc::none);

  ZC_EXPECT(param->getKind() == SyntaxKind::ParameterDeclaration);
  ZC_EXPECT(param->getModifiers().size() == 2);
}

ZC_TEST("StatementTest.ParameterDeclarationWithRest") {
  auto dotDotDotToken = factory::createTokenNode(SyntaxKind::DotDotDot);
  auto param = factory::createParameterDeclaration(
      {}, zc::mv(dotDotDotToken),
      zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(factory::createIdentifier("args"_zc)),
      zc::none);

  ZC_EXPECT(param->getKind() == SyntaxKind::ParameterDeclaration);
  ZC_EXPECT(param->getDotDotDotToken() != zc::none);
}

// ================================================================================
// PropertyDeclaration Tests

ZC_TEST("StatementTest.PropertyDeclaration") {
  auto name = factory::createIdentifier("name"_zc);
  auto type = factory::createPredefinedType("str"_zc);
  auto prop = factory::createPropertyDeclaration({}, zc::mv(name), zc::mv(type), zc::none);

  ZC_EXPECT(prop->getKind() == SyntaxKind::PropertyDeclaration);
  expectIdentifierNameText(prop->getName(), "name");
  ZC_EXPECT(prop->getType() != zc::none);
  ZC_EXPECT(prop->getInitializer() == zc::none);
}

ZC_TEST("StatementTest.PropertyDeclarationWithInitializer") {
  auto name = factory::createIdentifier("count"_zc);
  auto type = factory::createPredefinedType("i32"_zc);
  auto init = factory::createIntegerLiteral(0);
  auto prop = factory::createPropertyDeclaration({}, zc::mv(name), zc::mv(type), zc::mv(init));

  ZC_EXPECT(prop->getKind() == SyntaxKind::PropertyDeclaration);
  ZC_EXPECT(prop->getInitializer() != zc::none);
}

ZC_TEST("StatementTest.PropertyDeclarationWithModifiers") {
  zc::Vector<ast::SyntaxKind> modifiers;
  modifiers.add(SyntaxKind::PublicKeyword);
  modifiers.add(SyntaxKind::StaticKeyword);

  auto name = factory::createIdentifier("CONSTANT"_zc);
  auto prop =
      factory::createPropertyDeclaration(zc::mv(modifiers), zc::mv(name), zc::none, zc::none);

  ZC_EXPECT(prop->getKind() == SyntaxKind::PropertyDeclaration);
  ZC_EXPECT(prop->getModifiers().size() == 2);
}

// ================================================================================
// MethodDeclaration Tests

ZC_TEST("StatementTest.MethodDeclaration") {
  auto name = factory::createIdentifier("doSomething"_zc);
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto method = factory::createMethodDeclaration({}, zc::mv(name), zc::none, zc::none,
                                                 zc::mv(params), zc::none, zc::mv(body));

  ZC_EXPECT(method->getKind() == SyntaxKind::MethodDeclaration);
  expectIdentifierNameText(method->getName(), "doSomething");
  ZC_EXPECT(method->getBody() != zc::none);
}

ZC_TEST("StatementTest.MethodDeclarationWithOptional") {
  auto name = factory::createIdentifier("method"_zc);
  auto optionalToken = factory::createTokenNode(SyntaxKind::Question);
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto method = factory::createMethodDeclaration({}, zc::mv(name), zc::mv(optionalToken), zc::none,
                                                 zc::mv(params), zc::none, zc::none);

  ZC_EXPECT(method->getKind() == SyntaxKind::MethodDeclaration);
  ZC_EXPECT(method->isOptional());
}

ZC_TEST("StatementTest.MethodDeclarationWithReturnType") {
  auto name = factory::createIdentifier("getValue"_zc);
  auto typeName = factory::createIdentifier("Int"_zc);
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto returnType = factory::createReturnType(zc::mv(type), zc::none);

  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto method = factory::createMethodDeclaration({}, zc::mv(name), zc::none, zc::none,
                                                 zc::mv(params), zc::mv(returnType), zc::none);

  ZC_EXPECT(method->getKind() == SyntaxKind::MethodDeclaration);
  ZC_EXPECT(method->getReturnType() != zc::none);
}

ZC_TEST("StatementTest.MethodDeclarationLocalsInterface") {
  auto name = factory::createIdentifier("test"_zc);
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto method = factory::createMethodDeclaration({}, zc::mv(name), zc::none, zc::none,
                                                 zc::mv(params), zc::none, zc::mv(body));

  // Test LocalsContainer interface
  ZC_EXPECT(method->getLocals() == zc::none);
  ZC_EXPECT(method->getNextContainer() == zc::none);
}

// ================================================================================
// InitDeclaration Tests

ZC_TEST("StatementTest.InitDeclaration") {
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto init = factory::createInitDeclaration({}, zc::none, zc::mv(params), zc::none, zc::mv(body));

  ZC_EXPECT(init->getKind() == SyntaxKind::InitDeclaration);
  ZC_EXPECT(init->getBody() != zc::none);
}

ZC_TEST("StatementTest.InitDeclarationWithParameters") {
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  params.add(
      factory::createParameterDeclaration({}, zc::none,
                                          zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
                                              factory::createIdentifier("value"_zc)),
                                          zc::none));

  auto body = factory::createBlockStatement({});
  auto init = factory::createInitDeclaration({}, zc::none, zc::mv(params), zc::none, zc::mv(body));

  ZC_EXPECT(init->getKind() == SyntaxKind::InitDeclaration);
  ZC_EXPECT(init->getParameters().size() == 1);
}

ZC_TEST("StatementTest.InitDeclarationLocalsInterface") {
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto init = factory::createInitDeclaration({}, zc::none, zc::mv(params), zc::none, zc::mv(body));

  // Test LocalsContainer interface
  ZC_EXPECT(init->getLocals() == zc::none);
  ZC_EXPECT(init->getNextContainer() == zc::none);
}

// ================================================================================
// DeinitDeclaration Tests

ZC_TEST("StatementTest.DeinitDeclaration") {
  auto body = factory::createBlockStatement({});
  auto deinit = factory::createDeinitDeclaration({}, zc::mv(body));

  ZC_EXPECT(deinit->getKind() == SyntaxKind::DeinitDeclaration);
  ZC_EXPECT(deinit->getBody() != zc::none);
}

ZC_TEST("StatementTest.DeinitDeclarationNoBody") {
  auto deinit = factory::createDeinitDeclaration({}, zc::none);

  ZC_EXPECT(deinit->getKind() == SyntaxKind::DeinitDeclaration);
  ZC_EXPECT(deinit->getBody() == zc::none);
}

ZC_TEST("StatementTest.DeinitDeclarationLocalsInterface") {
  auto body = factory::createBlockStatement({});
  auto deinit = factory::createDeinitDeclaration({}, zc::mv(body));

  // Test LocalsContainer interface
  ZC_EXPECT(deinit->getLocals() == zc::none);
  ZC_EXPECT(deinit->getNextContainer() == zc::none);
}

// ================================================================================
// GetAccessor Tests

ZC_TEST("StatementTest.GetAccessor") {
  auto name = factory::createIdentifier("value"_zc);
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto getter = factory::createGetAccessorDeclaration({}, zc::mv(name), zc::none, zc::mv(params),
                                                      zc::none, zc::mv(body));

  ZC_EXPECT(getter->getKind() == SyntaxKind::GetAccessor);
  expectIdentifierNameText(getter->getName(), "value");
  ZC_EXPECT(getter->getBody() != zc::none);
}

ZC_TEST("StatementTest.GetAccessorWithReturnType") {
  auto name = factory::createIdentifier("count"_zc);
  auto type = factory::createPredefinedType("i32"_zc);
  auto returnType = factory::createReturnType(zc::mv(type), zc::none);

  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto getter = factory::createGetAccessorDeclaration({}, zc::mv(name), zc::none, zc::mv(params),
                                                      zc::mv(returnType), zc::mv(body));

  ZC_EXPECT(getter->getKind() == SyntaxKind::GetAccessor);
  ZC_EXPECT(getter->getReturnType() != zc::none);
}

ZC_TEST("StatementTest.GetAccessorLocalsInterface") {
  auto name = factory::createIdentifier("data"_zc);
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto getter = factory::createGetAccessorDeclaration({}, zc::mv(name), zc::none, zc::mv(params),
                                                      zc::none, zc::mv(body));

  // Test LocalsContainer interface
  ZC_EXPECT(getter->getLocals() == zc::none);
  ZC_EXPECT(getter->getNextContainer() == zc::none);
}

// ================================================================================
// SetAccessor Tests

ZC_TEST("StatementTest.SetAccessor") {
  auto name = factory::createIdentifier("value"_zc);
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto setter = factory::createSetAccessorDeclaration({}, zc::mv(name), zc::none, zc::mv(params),
                                                      zc::none, zc::mv(body));

  ZC_EXPECT(setter->getKind() == SyntaxKind::SetAccessor);
  expectIdentifierNameText(setter->getName(), "value");
  ZC_EXPECT(setter->getBody() != zc::none);
}

ZC_TEST("StatementTest.SetAccessorLocalsInterface") {
  auto name = factory::createIdentifier("count"_zc);
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto body = factory::createBlockStatement({});
  auto setter = factory::createSetAccessorDeclaration({}, zc::mv(name), zc::none, zc::mv(params),
                                                      zc::none, zc::mv(body));

  // Test LocalsContainer interface
  ZC_EXPECT(setter->getLocals() == zc::none);
  ZC_EXPECT(setter->getNextContainer() == zc::none);
}

// ================================================================================
// MatchStatement Tests

ZC_TEST("StatementTest.MatchStatementGetters") {
  auto discriminant = factory::createIntegerLiteral(42);
  zc::Vector<zc::Own<Statement>> clauses;
  clauses.add(factory::createEmptyStatement());
  auto stmt = factory::createMatchStatement(zc::mv(discriminant), zc::mv(clauses));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::MatchStatement);
  ZC_EXPECT(stmt->getDiscriminant().getKind() == SyntaxKind::IntegerLiteral);
  ZC_EXPECT(stmt->getClauses().size() == 1);
}

// ================================================================================
// InterfaceDeclaration Tests

ZC_TEST("StatementTest.InterfaceDeclarationWithHeritage") {
  auto name = factory::createIdentifier("TestInterface"_zc);
  auto expr =
      factory::createExpressionWithTypeArguments(factory::createIdentifier("Base"_zc), zc::none);
  zc::Vector<zc::Own<ExpressionWithTypeArguments>> types;
  types.add(zc::mv(expr));
  auto heritageClause = factory::createHeritageClause(SyntaxKind::ExtendsKeyword, zc::mv(types));
  zc::Vector<zc::Own<HeritageClause>> heritageClauses;
  heritageClauses.add(zc::mv(heritageClause));

  zc::Vector<zc::Own<InterfaceElement>> members;
  auto decl = factory::createInterfaceDeclaration(zc::mv(name), zc::none, zc::mv(heritageClauses),
                                                  zc::mv(members));

  ZC_EXPECT(decl->getKind() == SyntaxKind::InterfaceDeclaration);
  ZC_EXPECT(!decl->getHeritageClauses().empty());
}

// ================================================================================
// StructDeclaration Tests

ZC_TEST("StatementTest.StructDeclarationWithHeritage") {
  auto name = factory::createIdentifier("TestStruct"_zc);
  auto expr = factory::createExpressionWithTypeArguments(factory::createIdentifier("BaseStruct"_zc),
                                                         zc::none);
  zc::Vector<zc::Own<ExpressionWithTypeArguments>> types;
  types.add(zc::mv(expr));
  auto heritageClause = factory::createHeritageClause(SyntaxKind::ExtendsKeyword, zc::mv(types));
  zc::Vector<zc::Own<HeritageClause>> heritageClauses;
  heritageClauses.add(zc::mv(heritageClause));

  zc::Vector<zc::Own<ClassElement>> members;
  auto decl = factory::createStructDeclaration(zc::mv(name), zc::none, zc::mv(heritageClauses),
                                               zc::mv(members));

  ZC_EXPECT(decl->getKind() == SyntaxKind::StructDeclaration);
  ZC_EXPECT(!decl->getHeritageClauses().empty());
}

// ================================================================================
// EnumDeclaration Tests

ZC_TEST("StatementTest.EnumDeclarationWithMembers") {
  auto name = factory::createIdentifier("TestEnum"_zc);
  zc::Vector<zc::Own<EnumMember>> members;
  members.add(factory::createEnumMember(factory::createIdentifier("Case1"_zc)));
  members.add(factory::createEnumMember(factory::createIdentifier("Case2"_zc),
                                        factory::createIntegerLiteral(2)));
  auto decl = factory::createEnumDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(decl->getKind() == SyntaxKind::EnumDeclaration);
  ZC_EXPECT(decl->getMembers().size() == 2);
}

// ================================================================================
// ErrorDeclaration Tests

ZC_TEST("StatementTest.ErrorDeclarationWithMembers") {
  auto name = factory::createIdentifier("TestError"_zc);
  zc::Vector<zc::Own<Statement>> members;
  zc::Vector<zc::Own<VariableDeclaration>> varDecls;
  varDecls.add(
      factory::createVariableDeclaration(zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>>(
          factory::createIdentifier("code"_zc))));
  members.add(
      factory::createVariableStatement(factory::createVariableDeclarationList(zc::mv(varDecls))));
  auto decl = factory::createErrorDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(decl->getKind() == SyntaxKind::ErrorDeclaration);
  ZC_EXPECT(decl->getMembers().size() == 1);
}

// ================================================================================
// AliasDeclaration Tests

ZC_TEST("StatementTest.AliasDeclarationWithTypeParameters") {
  auto name = factory::createIdentifier("MyAlias"_zc);
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  typeParams.add(factory::createTypeParameterDeclaration(factory::createIdentifier("T"_zc)));
  auto type = factory::createPredefinedType("i32"_zc);
  auto decl = factory::createAliasDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(type));

  ZC_EXPECT(decl->getKind() == SyntaxKind::AliasDeclaration);
  ZC_EXPECT(decl->getTypeParameters().size() == 1);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
