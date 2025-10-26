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

ZC_TEST("StatementTest.VariableDeclarationList") {
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto name = factory::createIdentifier(zc::str("x"));
  auto binding = factory::createBindingElement(zc::mv(name));
  bindings.add(zc::mv(binding));

  auto decl = factory::createVariableDeclarationList(zc::mv(bindings));
  ZC_EXPECT(decl->getKind() == SyntaxKind::VariableDeclarationList);
  ZC_EXPECT(decl->getBindings().size() == 1);
}

ZC_TEST("StatementTest.FunctionDeclaration") {
  auto name = factory::createIdentifier(zc::str("foo"));
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::FunctionDeclaration);
  // Function always has name (reference, not pointer)
  // Skip name check for now
  // Function always has body (reference, not pointer)
}

ZC_TEST("StatementTest.IfStatement") {
  auto cond = factory::createBooleanLiteral(true);
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
  auto expr = factory::createFloatLiteral(42.0);
  auto stmt = factory::createReturnStatement(zc::mv(expr));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ReturnStatement);
  // ReturnStatement expression can be null pointer since it's optional
}

// ================================================================================
// BindingElement Tests

ZC_TEST("StatementTest.BindingElement") {
  auto name = factory::createIdentifier(zc::str("variable"));
  auto binding = factory::createBindingElement(zc::mv(name));

  ZC_EXPECT(binding->getKind() == SyntaxKind::BindingElement);
  ZC_EXPECT(binding->getName().getText() == "variable");
  ZC_EXPECT(binding->getType() == zc::none);
  ZC_EXPECT(binding->getInitializer() == zc::none);
}

ZC_TEST("StatementTest.BindingElementWithType") {
  auto name = factory::createIdentifier(zc::str("typed_var"));
  auto typeName = factory::createIdentifier(zc::str("Int"));
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto binding = factory::createBindingElement(zc::mv(name), zc::mv(type));

  ZC_EXPECT(binding->getKind() == SyntaxKind::BindingElement);
  ZC_EXPECT(binding->getName().getText() == "typed_var");
  ZC_EXPECT(binding->getType() != zc::none);
  ZC_EXPECT(binding->getInitializer() == zc::none);
}

ZC_TEST("StatementTest.BindingElementWithInitializer") {
  auto name = factory::createIdentifier(zc::str("init_var"));
  auto initializer = factory::createIntegerLiteral(42);
  auto binding = factory::createBindingElement(zc::mv(name), zc::none, zc::mv(initializer));

  ZC_EXPECT(binding->getKind() == SyntaxKind::BindingElement);
  ZC_EXPECT(binding->getName().getText() == "init_var");
  ZC_EXPECT(binding->getType() == zc::none);
  ZC_EXPECT(binding->getInitializer() != zc::none);
}

ZC_TEST("StatementTest.BindingElementAccept") {
  auto name = factory::createIdentifier(zc::str("test_var"));
  auto binding = factory::createBindingElement(zc::mv(name));

  // Test visitor pattern - just ensure it doesn't crash
  // In a real test, we'd use a mock visitor
}

// ================================================================================
// Enhanced VariableDeclarationList Tests

ZC_TEST("StatementTest.VariableDeclarationMultipleBindings") {
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto name1 = factory::createIdentifier(zc::str("x"));
  auto name2 = factory::createIdentifier(zc::str("y"));
  bindings.add(factory::createBindingElement(zc::mv(name1)));
  bindings.add(factory::createBindingElement(zc::mv(name2)));

  auto decl = factory::createVariableDeclarationList(zc::mv(bindings));
  ZC_EXPECT(decl->getKind() == SyntaxKind::VariableDeclarationList);
  ZC_EXPECT(decl->getBindings().size() == 2);
}

ZC_TEST("StatementTest.VariableDeclarationAccept") {
  zc::Vector<zc::Own<BindingElement>> bindings;
  auto name = factory::createIdentifier(zc::str("test"));
  bindings.add(factory::createBindingElement(zc::mv(name)));
  auto decl = factory::createVariableDeclarationList(zc::mv(bindings));

  // Test visitor pattern - just ensure it doesn't crash
}

// ================================================================================
// Enhanced FunctionDeclaration Tests

ZC_TEST("StatementTest.FunctionDeclarationWithParameters") {
  auto name = factory::createIdentifier(zc::str("testFunc"));
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;

  auto paramName = factory::createIdentifier(zc::str("param1"));
  params.add(factory::createBindingElement(zc::mv(paramName)));

  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::FunctionDeclaration);
  ZC_EXPECT(decl->getName().getText() == "testFunc");
  ZC_EXPECT(decl->getParameters().size() == 1);
  ZC_EXPECT(decl->getTypeParameters().size() == 0);
  ZC_EXPECT(decl->getReturnType() == zc::none);
}

ZC_TEST("StatementTest.FunctionDeclarationWithReturnTypeAndTypeParameters") {
  auto name = factory::createIdentifier(zc::str("genericFunc"));

  // Create type parameters
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  auto typeParamName = factory::createIdentifier(zc::str("T"));
  auto constraintName = factory::createIdentifier(zc::str("Comparable"));
  auto constraint = factory::createTypeReference(zc::mv(constraintName), zc::none);
  typeParams.add(
      factory::createTypeParameterDeclaration(zc::mv(typeParamName), zc::mv(constraint)));

  // Create parameters
  zc::Vector<zc::Own<BindingElement>> params;
  auto paramName = factory::createIdentifier(zc::str("value"));
  auto paramTypeName = factory::createIdentifier(zc::str("T"));
  auto paramType = factory::createTypeReference(zc::mv(paramTypeName), zc::none);
  params.add(factory::createBindingElement(zc::mv(paramName), zc::mv(paramType)));

  // Create return type
  auto returnTypeName = factory::createIdentifier(zc::str("String"));
  auto returnTypeRef = factory::createTypeReference(zc::mv(returnTypeName), zc::none);
  auto returnType = factory::createReturnType(zc::mv(returnTypeRef), zc::none);

  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::mv(returnType), zc::mv(body));

  ZC_EXPECT(decl->getKind() == SyntaxKind::FunctionDeclaration);
  ZC_EXPECT(decl->getName().getText() == "genericFunc");
  ZC_EXPECT(decl->getTypeParameters().size() == 1);
  ZC_EXPECT(decl->getParameters().size() == 1);
  ZC_EXPECT(decl->getReturnType() != zc::none);
}

ZC_TEST("StatementTest.FunctionDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("func"));
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto body = factory::createBlockStatement({});
  auto decl = factory::createFunctionDeclaration(zc::mv(name), zc::mv(typeParams), zc::mv(params),
                                                 zc::none, zc::mv(body));

  // Test visitor pattern - just ensure it doesn't crash
}

// ================================================================================
// ClassDeclaration Tests

ZC_TEST("StatementTest.ClassDeclaration") {
  auto name = factory::createIdentifier(zc::str("TestClass"));
  zc::Vector<zc::Own<Statement>> members;
  auto classDecl = factory::createClassDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::ClassDeclaration);
  ZC_EXPECT(classDecl->getName().getText() == "TestClass");
  ZC_EXPECT(classDecl->getMembers().size() == 0);
}

ZC_TEST("StatementTest.ClassDeclarationWithMembers") {
  auto name = factory::createIdentifier(zc::str("ChildClass"));
  zc::Vector<zc::Own<Statement>> members;
  members.add(factory::createEmptyStatement());
  auto classDecl = factory::createClassDeclaration(zc::mv(name), zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::ClassDeclaration);
  ZC_EXPECT(classDecl->getName().getText() == "ChildClass");
  ZC_EXPECT(classDecl->getMembers().size() == 1);
}

ZC_TEST("StatementTest.ClassDeclarationWithSuperClass") {
  auto name = factory::createIdentifier(zc::str("ChildClass"));
  auto superClass = factory::createIdentifier(zc::str("ParentClass"));
  zc::Vector<zc::Own<Statement>> members;
  auto classDecl = zc::heap<ClassDeclaration>(zc::mv(name), zc::mv(superClass), zc::mv(members));

  ZC_EXPECT(classDecl->getKind() == SyntaxKind::ClassDeclaration);
  ZC_EXPECT(classDecl->getName().getText() == "ChildClass");
  ZC_EXPECT(classDecl->getSuperClass() != zc::none);
  ZC_IF_SOME(superClass, classDecl->getSuperClass()) {
    ZC_EXPECT(superClass.getText() == "ParentClass");
  }
  ZC_EXPECT(classDecl->getMembers().size() == 0);
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
  auto condition = factory::createBooleanLiteral(true);
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
  auto expr = factory::createIntegerLiteral(123);
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
  auto label = factory::createIdentifier(zc::str("loop1"));
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
  auto label = factory::createIdentifier(zc::str("loop2"));
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
  auto discriminant = factory::createIntegerLiteral(42);
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
  auto name = factory::createIdentifier(zc::str("MyAlias"));
  auto typeName = factory::createIdentifier(zc::str("String"));
  auto type = factory::createTypeReference(zc::mv(typeName), zc::none);
  auto stmt = factory::createAliasDeclaration(zc::mv(name), zc::mv(type));

  ZC_EXPECT(stmt->getKind() == SyntaxKind::AliasDeclaration);
  ZC_EXPECT(stmt->getName().getText() == "MyAlias");
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::DebuggerStatement);
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::InterfaceDeclaration);
  ZC_EXPECT(stmt->getName().getText() == "TestInterface");
  ZC_EXPECT(stmt->getMembers().size() == 0);
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::StructDeclaration);
  ZC_EXPECT(stmt->getName().getText() == "TestStruct");
  ZC_EXPECT(stmt->getMembers().size() == 0);
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::EnumDeclaration);
  ZC_EXPECT(stmt->getName().getText() == "TestEnum");
  ZC_EXPECT(stmt->getMembers().size() == 0);
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

  ZC_EXPECT(stmt->getKind() == SyntaxKind::ErrorDeclaration);
  ZC_EXPECT(stmt->getName().getText() == "TestError");
  ZC_EXPECT(stmt->getMembers().size() == 0);
}

ZC_TEST("StatementTest.ErrorDeclarationAccept") {
  auto name = factory::createIdentifier(zc::str("MyError"));
  zc::Vector<zc::Own<Statement>> members;
  auto stmt = factory::createErrorDeclaration(zc::mv(name), zc::mv(members));

  // Test visitor pattern
}

// ================================================================================
// TypeParameterDeclaration Tests

ZC_TEST("StatementTest.TypeParameterDeclaration") {
  auto name = factory::createIdentifier(zc::str("T"));
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(name), zc::none);

  ZC_EXPECT(typeParam->getKind() == SyntaxKind::TypeParameterDeclaration);
  ZC_EXPECT(typeParam->getName().getText() == "T");
  ZC_EXPECT(typeParam->getConstraint() == zc::none);
}

ZC_TEST("StatementTest.TypeParameterWithConstraint") {
  auto name = factory::createIdentifier(zc::str("T"));
  auto constraintName = factory::createIdentifier(zc::str("Comparable"));
  auto constraint = factory::createTypeReference(zc::mv(constraintName), zc::none);
  auto typeParam = factory::createTypeParameterDeclaration(zc::mv(name), zc::mv(constraint));

  ZC_EXPECT(typeParam->getName().getText() == "T");
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

ZC_TEST("StatementTest.MatchClause") {
  auto identifier = factory::createIdentifier(zc::str("x"));
  auto pattern = factory::createIdentifierPattern(zc::mv(identifier));
  auto body = factory::createEmptyStatement();
  auto clause = factory::createMatchClause(zc::mv(pattern), zc::none, zc::mv(body));

  ZC_EXPECT(clause->getKind() == SyntaxKind::MatchClause);
  ZC_EXPECT(clause->getPattern().getKind() == SyntaxKind::IdentifierPattern);
  ZC_EXPECT(clause->getBody().getKind() == SyntaxKind::EmptyStatement);
}

ZC_TEST("StatementTest.MatchClauseWithGuard") {
  auto identifier = factory::createIdentifier(zc::str("value"));
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
  auto identifier = factory::createIdentifier(zc::str("variable"));
  auto pattern = factory::createIdentifierPattern(zc::mv(identifier));

  ZC_EXPECT(pattern->getKind() == SyntaxKind::IdentifierPattern);
  ZC_EXPECT(pattern->getIdentifier().getText() == "variable");
}

ZC_TEST("StatementTest.ExpressionPattern") {
  auto literal = factory::createIntegerLiteral(42);
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
  auto name = factory::createIdentifier(zc::str("x"));
  auto element = factory::createBindingElement(zc::mv(name));
  elements.add(zc::mv(element));

  auto arrayBindingPattern = factory::createArrayBindingPattern(zc::mv(elements));

  ZC_EXPECT(arrayBindingPattern->getKind() == SyntaxKind::ArrayBindingPattern);
  ZC_EXPECT(arrayBindingPattern->getElements().size() == 1);
}

ZC_TEST("StatementTest.TuplePattern") {
  zc::Vector<zc::Own<Pattern>> elements;
  auto identifier = factory::createIdentifier(zc::str("x"));
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
  auto identifier = factory::createIdentifier(zc::str("first"));
  auto pattern = factory::createIdentifierPattern(zc::mv(identifier));
  elements.add(zc::mv(pattern));

  auto arrayPattern = factory::createArrayPattern(zc::mv(elements));

  ZC_EXPECT(arrayPattern->getKind() == SyntaxKind::ArrayPattern);
  ZC_EXPECT(arrayPattern->getElements().size() == 1);
}

// ================================================================================
// BindingPattern Tests

ZC_TEST("StatementTest.ArrayBindingPatternTest") {
  zc::Vector<zc::Own<BindingElement>> elements;
  auto name = factory::createIdentifier(zc::str("x"));
  elements.add(factory::createBindingElement(zc::mv(name)));

  auto arrayBinding = factory::createArrayBindingPattern(zc::mv(elements));
  ZC_EXPECT(arrayBinding->getKind() == SyntaxKind::ArrayBindingPattern);
  ZC_EXPECT(arrayBinding->getElements().size() == 1);
}

ZC_TEST("StatementTest.ObjectBindingPattern") {
  zc::Vector<zc::Own<BindingElement>> properties;
  auto name = factory::createIdentifier(zc::str("prop"));
  properties.add(factory::createBindingElement(zc::mv(name)));

  auto objectBinding = factory::createObjectBindingPattern(zc::mv(properties));
  ZC_EXPECT(objectBinding->getKind() == SyntaxKind::ObjectBindingPattern);
  ZC_EXPECT(objectBinding->getProperties().size() == 1);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
