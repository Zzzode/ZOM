// Copyright (c) 2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

/// \file
/// \brief Unit tests for AST factory functionality.
///
/// This file contains ztest-based unit tests for the AST factory,
/// testing the creation and manipulation of AST nodes.

#include "zomlang/compiler/ast/factory.h"

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("ASTFactory: Basic Node Creation") {
  // Test basic AST node creation functionality
  using namespace zomlang::compiler::ast::factory;

  // Test SourceFile creation
  zc::Vector<zc::Own<ast::Statement>> statements;
  auto sourceFile = createSourceFile(zc::str("test.zom"), zc::mv(statements));
  ZC_EXPECT(sourceFile->getFileName() == "test.zom", "SourceFile should have correct filename");
  ZC_EXPECT(sourceFile->getStatements().size() == 0, "SourceFile should have empty statements");

  // Test Identifier creation
  auto identifier = createIdentifier(zc::str("testVar"));
  ZC_EXPECT(identifier->getName() == "testVar", "Identifier should have correct name");

  // Test literal creation
  auto stringLit = createStringLiteral(zc::str("hello world"));
  ZC_EXPECT(stringLit->getValue() == "hello world", "StringLiteral should have correct value");

  auto numLit = createNumericLiteral(42.5);
  ZC_EXPECT(numLit->getValue() == 42.5, "NumericLiteral should have correct value");

  auto boolLit = createBooleanLiteral(true);
  ZC_EXPECT(boolLit->getValue() == true, "BooleanLiteral should have correct value");

  auto nilLit = createNilLiteral();
}

ZC_TEST("ASTFactory: Function Declaration Creation") {
  // Test function declaration AST node creation
  using namespace zomlang::compiler::ast::factory;

  // Create function name
  auto funcName = createIdentifier(zc::str("testFunction"));

  // Create parameters
  zc::Vector<zc::Own<ast::BindingElement>> parameters;
  auto param1Name = createIdentifier(zc::str("n"));
  auto param1 = createBindingElement(zc::mv(param1Name));
  parameters.add(zc::mv(param1));

  auto param2Name = createIdentifier(zc::str("s"));
  auto param2 = createBindingElement(zc::mv(param2Name));
  parameters.add(zc::mv(param2));

  // Create function body (empty block statement)
  zc::Vector<zc::Own<ast::Statement>> bodyStatements;
  auto body = createBlockStatement(zc::mv(bodyStatements));

  // Create type parameters (empty)
  zc::Vector<zc::Own<ast::TypeParameter>> typeParams;

  // Create function declaration
  auto funcDecl = createFunctionDeclaration(zc::mv(funcName), zc::mv(typeParams),
                                            zc::mv(parameters), zc::none, zc::mv(body));

  ZC_EXPECT(funcDecl->getName()->getName() == "testFunction", "Function should have correct name");
  ZC_EXPECT(funcDecl->getParameters().size() == 2, "Function should have 2 parameters");
  ZC_EXPECT(funcDecl->getTypeParameters().size() == 0, "Function should have no type parameters");
  ZC_EXPECT(funcDecl->getReturnType() == nullptr, "Function should have no return type");
  ZC_EXPECT(funcDecl->getBody() != nullptr, "Function should have a body");
}

ZC_TEST("ASTFactory: Expression Creation") {
  // Test various expression creation functionality
  using namespace zomlang::compiler::ast::factory;

  // Test binary expression creation
  auto left = createNumericLiteral(10.0);
  auto right = createNumericLiteral(20.0);
  auto addOp = createAddOperator();
  auto binaryExpr = createBinaryExpression(zc::mv(left), zc::mv(addOp), zc::mv(right));

  // Note: We can't directly access BinaryExpression methods from Expression pointer
  // This tests that the factory function returns a valid expression

  // Test call expression creation
  auto callee = createIdentifier(zc::str("myFunction"));
  zc::Vector<zc::Own<ast::Expression>> args;
  args.add(createStringLiteral(zc::str("arg1")));
  args.add(createNumericLiteral(42.0));
  auto callExpr = createCallExpression(zc::mv(callee), zc::mv(args));

  ZC_EXPECT(callExpr->getCallee() != nullptr, "CallExpression should have callee");
  ZC_EXPECT(callExpr->getArguments().size() == 2, "CallExpression should have 2 arguments");

  // Test conditional expression creation
  auto test = createBooleanLiteral(true);
  auto consequent = createStringLiteral(zc::str("true_branch"));
  auto alternate = createStringLiteral(zc::str("false_branch"));
  auto condExpr = createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));

  ZC_EXPECT(condExpr->getTest() != nullptr, "ConditionalExpression should have test");
  ZC_EXPECT(condExpr->getConsequent() != nullptr, "ConditionalExpression should have consequent");
  ZC_EXPECT(condExpr->getAlternate() != nullptr, "ConditionalExpression should have alternate");
}

ZC_TEST("ASTFactory: Statement Creation") {
  // Test various statement creation functionality
  using namespace zomlang::compiler::ast::factory;

  // Test variable declaration creation
  zc::Vector<zc::Own<ast::BindingElement>> bindings;
  auto varName = createIdentifier(zc::str("myVar"));
  auto initValue = createNumericLiteral(100.0);
  auto binding = createBindingElement(zc::mv(varName), zc::none, zc::mv(initValue));
  bindings.add(zc::mv(binding));
  auto varDecl = createVariableDeclaration(zc::mv(bindings));

  ZC_EXPECT(varDecl->getBindings().size() == 1, "VariableDeclaration should have 1 binding");

  // Test if statement creation
  auto condition = createBooleanLiteral(true);
  auto thenStmt = createEmptyStatement();
  auto elseStmt = createEmptyStatement();
  auto ifStmt = createIfStatement(zc::mv(condition), zc::mv(thenStmt), zc::mv(elseStmt));

  ZC_EXPECT(ifStmt->getCondition() != nullptr, "IfStatement should have condition");
  ZC_EXPECT(ifStmt->getThenStatement() != nullptr, "IfStatement should have then statement");
  ZC_EXPECT(ifStmt->getElseStatement() != nullptr, "IfStatement should have else statement");

  // Test return statement creation
  auto returnValue = createStringLiteral(zc::str("success"));
  auto returnStmt = createReturnStatement(zc::mv(returnValue));

  ZC_EXPECT(returnStmt->getExpression() != nullptr, "ReturnStatement should have expression");

  // Test empty statement creation
  auto emptyStmt = createEmptyStatement();

  // Test block statement creation
  zc::Vector<zc::Own<ast::Statement>> statements;
  statements.add(createEmptyStatement());
  statements.add(createEmptyStatement());
  auto blockStmt = createBlockStatement(zc::mv(statements));

  ZC_EXPECT(blockStmt->getStatements().size() == 2, "BlockStatement should have 2 statements");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang