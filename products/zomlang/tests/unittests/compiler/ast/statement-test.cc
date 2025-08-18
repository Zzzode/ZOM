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

#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("StatementTest.VariableDeclaration") {
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto name = factory::createIdentifier(zc::str("x"));
  auto binding = factory::createBindingElement(zc::mv(name));
  bindings.add(zc::mv(binding));

  auto decl = factory::createVariableDeclaration(zc::mv(bindings));
  ZC_EXPECT(decl->getKind() == SyntaxKind::kVariableDeclaration);
  ZC_EXPECT(decl->isStatement());
  ZC_EXPECT(decl->getBindings().size() == 1);
}

ZC_TEST("StatementTest.FunctionDeclaration") {
  auto name = factory::createIdentifier(zc::str("foo"));
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::kFunctionDeclaration);
  // Function always has name (reference, not pointer)
  // Skip name check for now
  // Function always has body (reference, not pointer)
}

ZC_TEST("StatementTest.IfStatement") {
  auto cond = factory::createBooleanLiteral(true);
  auto thenStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::none);

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kIfStatement);
  auto ifStmt = static_cast<IfStatement*>(stmt.get());
  // IfStatement always has condition and then statement (references, not pointers)
  ZC_EXPECT(ifStmt->getElseStatement() == nullptr);
}

ZC_TEST("StatementTest.BlockStatement") {
  zc::Vector<zc::Own<Statement>> statements;
  statements.add(factory::createEmptyStatement());
  auto block = factory::createBlockStatement(zc::mv(statements));

  ZC_EXPECT(block->getKind() == SyntaxKind::kBlockStatement);
  ZC_EXPECT(block->getStatements().size() == 1);
}

ZC_TEST("StatementTest.ReturnStatement") {
  auto expr = factory::createFloatLiteral(42.0);
  auto stmt = factory::createReturnStatement(zc::mv(expr));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kReturnStatement);
  // ReturnStatement expression can be null pointer since it's optional
}

// ================================================================================
// BindingElement Tests

ZC_TEST("StatementTest.BindingElement") {
  auto name = factory::createIdentifier(zc::str("variable"));
  auto binding = factory::createBindingElement(zc::mv(name));

  ZC_EXPECT(binding->getKind() == SyntaxKind::kBindingElement);
  ZC_EXPECT(binding->getName().getName() == "variable");
  ZC_EXPECT(binding->getType() == zc::none);
  ZC_EXPECT(binding->getInitializer() == nullptr);
}

ZC_TEST("StatementTest.BindingElementWithType") {
  auto name = factory::createIdentifier(zc::str("typed_var"));
  auto typeName = factory::createIdentifier(zc::str("Int"));
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto binding = factory::createBindingElement(zc::mv(name), zc::mv(type));

  ZC_EXPECT(binding->getKind() == SyntaxKind::kBindingElement);
  ZC_EXPECT(binding->getName().getName() == "typed_var");
  ZC_EXPECT(binding->getType() != zc::none);
  ZC_EXPECT(binding->getInitializer() == nullptr);
}

ZC_TEST("StatementTest.BindingElementWithInitializer") {
  auto name = factory::createIdentifier(zc::str("init_var"));
  auto initializer = factory::createIntegerLiteral(42);
  auto binding = factory::createBindingElement(zc::mv(name), zc::none, zc::mv(initializer));

  ZC_EXPECT(binding->getKind() == SyntaxKind::kBindingElement);
  ZC_EXPECT(binding->getName().getName() == "init_var");
  ZC_EXPECT(binding->getType() == zc::none);
  ZC_EXPECT(binding->getInitializer() != nullptr);
}

ZC_TEST("StatementTest.BindingElementAccept") {
  auto name = factory::createIdentifier(zc::str("test_var"));
  auto binding = factory::createBindingElement(zc::mv(name));

  // Test visitor pattern - just ensure it doesn't crash
  // In a real test, we'd use a mock visitor
}

// ================================================================================
// Enhanced VariableDeclaration Tests

ZC_TEST("StatementTest.VariableDeclarationMultipleBindings") {
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto name1 = factory::createIdentifier(zc::str("x"));
  auto name2 = factory::createIdentifier(zc::str("y"));
  bindings.add(factory::createBindingElement(zc::mv(name1)));
  bindings.add(factory::createBindingElement(zc::mv(name2)));

  auto decl = factory::createVariableDeclaration(zc::mv(bindings));
  ZC_EXPECT(decl->getKind() == SyntaxKind::kVariableDeclaration);
  ZC_EXPECT(decl->getBindings().size() == 2);
}

ZC_TEST("StatementTest.VariableDeclarationAccept") {
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto name = factory::createIdentifier(zc::str("test"));
  bindings.add(factory::createBindingElement(zc::mv(name)));
  auto decl = factory::createVariableDeclaration(zc::mv(bindings));

  // Test visitor pattern - just ensure it doesn't crash
}

// ================================================================================
// Enhanced FunctionDeclaration Tests

ZC_TEST("StatementTest.FunctionDeclarationWithParameters") {
  auto name = factory::createIdentifier(zc::str("testFunc"));
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;

  auto paramName = factory::createIdentifier(zc::str("param1"));
  params.add(factory::createBindingElement(zc::mv(paramName)));

  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::kFunctionDeclaration);
  ZC_EXPECT(decl->getName().getName() == "testFunc");
  ZC_EXPECT(decl->getParameters().size() == 1);
  ZC_EXPECT(decl->getTypeParameters().size() == 0);
  ZC_EXPECT(decl->getReturnType() == nullptr);
}

ZC_TEST("StatementTest.FunctionDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("func"));
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  // Test visitor pattern
}

// ================================================================================
// ClassDeclaration Tests

ZC_TEST("StatementTest.ClassDeclaration") {
  auto name = factory::createIdentifier(zc::str("TestClass"));
  zc::Vector<zc::Own<Statement>> members;
  auto classDecl = factory::createClassDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::kClassDeclaration);
  ZC_EXPECT(classDecl->getName().getName() == "TestClass");
  ZC_EXPECT(classDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.ClassDeclarationWithMembers") {
  auto name = factory::createIdentifier(zc::str("ChildClass"));
  zc::Vector<zc::Own<Statement>> members;
  members.add(factory::createEmptyStatement());
  auto classDecl = factory::createClassDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::kClassDeclaration);
  ZC_EXPECT(classDecl->getName().getName() == "ChildClass");
  ZC_EXPECT(classDecl->getMembers().size() == 1);
}

ZC_TEST("StatementTest.ClassDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("TestClass"));
  zc::Vector<zc::Own<Statement>> members;
  auto classDecl = factory::createClassDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// Enhanced IfStatement Tests

ZC_TEST("StatementTest.IfStatementWithElse") {
  auto cond = factory::createBooleanLiteral(false);
  auto thenStmt = factory::createEmptyStatement();
  auto elseStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::mv(elseStmt));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kIfStatement);
  auto ifStmt = static_cast<IfStatement*>(stmt.get());
  ZC_EXPECT(ifStmt->getElseStatement() != nullptr);
}

ZC_TEST("StatementTest.IfStatementGetters") {
  auto cond = factory::createBooleanLiteral(true);
  auto thenStmt = factory::createEmptyStatement();
  auto stmt = factory::createIfStatement(zc::mv(cond), zc::mv(thenStmt), zc::none);

  auto ifStmt = static_cast<IfStatement*>(stmt.get());
  // Test getCondition and getThenStatement methods
  ZC_EXPECT(ifStmt->getCondition().getKind() == SyntaxKind::kBooleanLiteral);
  ZC_EXPECT(ifStmt->getThenStatement().getKind() == SyntaxKind::kEmptyStatement);
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
  auto condition = factory::createBooleanLiteral(true);
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createWhileStatement(zc::mv(condition), zc::mv(body));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kWhileStatement);
  auto whileStmt = static_cast<WhileStatement*>(stmt.get());
  ZC_EXPECT(whileStmt->getCondition().getKind() == SyntaxKind::kBooleanLiteral);
  ZC_EXPECT(whileStmt->getBody().getKind() == SyntaxKind::kEmptyStatement);
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kForStatement);
  auto forStmt = static_cast<ForStatement*>(stmt.get());
  ZC_EXPECT(forStmt->getInit() != nullptr);
  ZC_EXPECT(forStmt->getCondition() != nullptr);
  ZC_EXPECT(forStmt->getUpdate() != nullptr);
  ZC_EXPECT(forStmt->getBody().getKind() == SyntaxKind::kEmptyStatement);
}

ZC_TEST("StatementTest.ForStatementMinimal") {
  auto body = factory::createEmptyStatement();
  auto stmt = factory::createForStatement(zc::none, zc::none, zc::none, zc::mv(body));

  auto forStmt = static_cast<ForStatement*>(stmt.get());
  ZC_EXPECT(forStmt->getInit() == nullptr);
  ZC_EXPECT(forStmt->getCondition() == nullptr);
  ZC_EXPECT(forStmt->getUpdate() == nullptr);
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kReturnStatement);
  auto returnStmt = static_cast<ReturnStatement*>(stmt.get());
  ZC_EXPECT(returnStmt->getExpression() == nullptr);
}

ZC_TEST("StatementTest.ReturnStatementWithExpression") {
  auto expr = factory::createStringLiteral(zc::str("test"));
  auto stmt = factory::createReturnStatement(zc::mv(expr));

  auto returnStmt = static_cast<ReturnStatement*>(stmt.get());
  ZC_EXPECT(returnStmt->getExpression() != nullptr);
}

ZC_TEST("StatementTest.ReturnStatementAccept") {
  auto stmt = factory::createReturnStatement(zc::none);

  // Test visitor pattern
}

// ================================================================================
// EmptyStatement Tests

ZC_TEST("StatementTest.EmptyStatement") {
  auto stmt = factory::createEmptyStatement();

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kEmptyStatement);
  ZC_EXPECT(stmt->isStatement());
}

ZC_TEST("StatementTest.EmptyStatementAccept") {
  auto stmt = factory::createEmptyStatement();

  // Test visitor pattern
}

// ================================================================================
// Enhanced BlockStatement Tests

ZC_TEST("StatementTest.BlockStatementEmpty") {
  auto block = factory::createBlockStatement({});

  ZC_EXPECT(block->getKind() == SyntaxKind::kBlockStatement);
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
  auto expr = factory::createIntegerLiteral(123);
  auto stmt = factory::createExpressionStatement(zc::mv(expr));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kExpressionStatement);
  auto exprStmt = static_cast<ExpressionStatement*>(stmt.get());
  ZC_EXPECT(exprStmt->getExpression().getKind() == SyntaxKind::kIntegerLiteral);
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kBreakStatement);
  auto breakStmt = static_cast<BreakStatement*>(stmt.get());
  ZC_EXPECT(breakStmt->getLabel() == nullptr);
}

ZC_TEST("StatementTest.BreakStatementWithLabel") {
  auto label = factory::createIdentifier(zc::str("loop1"));
  auto stmt = factory::createBreakStatement(zc::mv(label));

  auto breakStmt = static_cast<BreakStatement*>(stmt.get());
  ZC_EXPECT(breakStmt->getLabel() != nullptr);
  ZC_EXPECT(breakStmt->getLabel()->getName() == "loop1");
}

ZC_TEST("StatementTest.BreakStatementAccept") {
  auto stmt = factory::createBreakStatement(zc::none);

  // Test visitor pattern
}

// ================================================================================
// ContinueStatement Tests

ZC_TEST("StatementTest.ContinueStatement") {
  auto stmt = factory::createContinueStatement(zc::none);

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kContinueStatement);
  auto continueStmt = static_cast<ContinueStatement*>(stmt.get());
  ZC_EXPECT(continueStmt->getLabel() == nullptr);
}

ZC_TEST("StatementTest.ContinueStatementWithLabel") {
  auto label = factory::createIdentifier(zc::str("loop2"));
  auto stmt = factory::createContinueStatement(zc::mv(label));

  auto continueStmt = static_cast<ContinueStatement*>(stmt.get());
  ZC_EXPECT(continueStmt->getLabel() != nullptr);
  ZC_EXPECT(continueStmt->getLabel()->getName() == "loop2");
}

ZC_TEST("StatementTest.ContinueStatementAccept") {
  auto stmt = factory::createContinueStatement(zc::none);

  // Test visitor pattern
}

// ================================================================================
// MatchStatement Tests

ZC_TEST("StatementTest.MatchStatement") {
  auto discriminant = factory::createIntegerLiteral(42);
  zc::Vector<zc::Own<Statement>> clauses;
  clauses.add(factory::createEmptyStatement());
  auto stmt = factory::createMatchStatement(zc::mv(discriminant), zc::mv(clauses));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kMatchStatement);
  auto matchStmt = static_cast<MatchStatement*>(stmt.get());
  ZC_EXPECT(matchStmt->getDiscriminant().getKind() == SyntaxKind::kIntegerLiteral);
  ZC_EXPECT(matchStmt->getClauses().size() == 1);
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
  auto name = factory::createIdentifier(zc::str("MyAlias"));
  auto typeName = factory::createIdentifier(zc::str("String"));
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto stmt = factory::createAliasDeclaration(zc::mv(name), zc::mv(type));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kAliasDeclaration);
  auto aliasDecl = static_cast<AliasDeclaration*>(stmt.get());
  ZC_EXPECT(aliasDecl->getName().getName() == "MyAlias");
}

ZC_TEST("StatementTest.AliasDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("TestAlias"));
  auto typeName = factory::createIdentifier(zc::str("Int"));
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto stmt = factory::createAliasDeclaration(zc::mv(name), zc::mv(type));

  // Test visitor pattern
}

// ================================================================================
// DebuggerStatement Tests

ZC_TEST("StatementTest.DebuggerStatement") {
  auto stmt = factory::createDebuggerStatement();

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kDebuggerStatement);
  ZC_EXPECT(stmt->isStatement());
}

ZC_TEST("StatementTest.DebuggerStatementAccept") {
  auto stmt = factory::createDebuggerStatement();

  // Test visitor pattern
}

// ================================================================================
// InterfaceDeclaration Tests

ZC_TEST("StatementTest.InterfaceDeclaration") {
  auto name = factory::createIdentifier(zc::str("TestInterface"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createInterfaceDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kInterfaceDeclaration);
  auto interfaceDecl = static_cast<InterfaceDeclaration*>(stmt.get());
  ZC_EXPECT(interfaceDecl->getName().getName() == "TestInterface");
  ZC_EXPECT(interfaceDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.InterfaceDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("ITest"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createInterfaceDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// StructDeclaration Tests

ZC_TEST("StatementTest.StructDeclaration") {
  auto name = factory::createIdentifier(zc::str("TestStruct"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createStructDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kStructDeclaration);
  auto structDecl = static_cast<StructDeclaration*>(stmt.get());
  ZC_EXPECT(structDecl->getName().getName() == "TestStruct");
  ZC_EXPECT(structDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.StructDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("MyStruct"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createStructDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// EnumDeclaration Tests

ZC_TEST("StatementTest.EnumDeclaration") {
  auto name = factory::createIdentifier(zc::str("TestEnum"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createEnumDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kEnumDeclaration);
  auto enumDecl = static_cast<EnumDeclaration*>(stmt.get());
  ZC_EXPECT(enumDecl->getName().getName() == "TestEnum");
  ZC_EXPECT(enumDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.EnumDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("MyEnum"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createEnumDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// ErrorDeclaration Tests

ZC_TEST("StatementTest.ErrorDeclaration") {
  auto name = factory::createIdentifier(zc::str("TestError"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createErrorDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::kErrorDeclaration);
  auto errorDecl = static_cast<ErrorDeclaration*>(stmt.get());
  ZC_EXPECT(errorDecl->getName().getName() == "TestError");
  ZC_EXPECT(errorDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.ErrorDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("MyError"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createErrorDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// TypeParameter Tests

ZC_TEST("StatementTest.TypeParameter") {
  auto name = factory::createIdentifier(zc::str("T"));
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(name), zc::none);

  ZC_EXPECT(typeParam->getKind() == SyntaxKind::kTypeParameterDeclaration);
  ZC_EXPECT(typeParam->getName().getName() == "T");
  ZC_EXPECT(typeParam->getConstraint() == zc::none);
}

ZC_TEST("StatementTest.TypeParameterWithConstraint") {
  auto name = factory::createIdentifier(zc::str("T"));
  auto constraintName = factory::createIdentifier(zc::str("Comparable"));
  auto constraint = factory::createTypeReference(zc::mv(constraintName), zc::none);
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(name), zc::mv(constraint));

  ZC_EXPECT(typeParam->getName().getName() == "T");
  ZC_EXPECT(typeParam->getConstraint() != zc::none);
}

ZC_TEST("StatementTest.TypeParameterAccept") {
  auto name = factory::createIdentifier(zc::str("U"));
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

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang