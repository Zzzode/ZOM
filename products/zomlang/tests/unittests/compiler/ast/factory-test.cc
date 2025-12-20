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
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/kinds.h"
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
  auto identifier = createIdentifier("testVar"_zc);
  ZC_EXPECT(identifier->getText() == "testVar", "Identifier should have correct name");

  // Test literal creation
  auto stringLit = createStringLiteral("hello world"_zc);
  ZC_EXPECT(stringLit->getValue() == "hello world", "StringLiteral should have correct value");

  auto numLit = createFloatLiteral(42.5);
  ZC_EXPECT(numLit->getValue() == 42.5, "FloatLiteral should have correct value");

  auto boolLit = createBooleanLiteral(true);
  ZC_EXPECT(boolLit->getValue() == true, "BooleanLiteral should have correct value");

  auto nullLit = createNullLiteral();
  ZC_EXPECT(nullLit->getKind() == SyntaxKind::NullLiteral, "NullLiteral should have correct kind");
}

ZC_TEST("ASTFactory: Function Declaration Creation") {
  // Test function declaration AST node creation
  using namespace zomlang::compiler::ast::factory;

  // Create function name
  auto funcName = createIdentifier("testFunction"_zc);

  // Create parameters
  zc::Vector<zc::Own<ast::BindingElement>> parameters;
  auto param1Name = createIdentifier("n"_zc);
  auto param1 = createBindingElement(zc::mv(param1Name));
  parameters.add(zc::mv(param1));

  auto param2Name = createIdentifier("s"_zc);
  auto param2 = createBindingElement(zc::mv(param2Name));
  parameters.add(zc::mv(param2));

  // Create function body (empty block statement)
  zc::Vector<zc::Own<ast::Statement>> bodyStatements;
  auto body = createBlockStatement(zc::mv(bodyStatements));

  // Create type parameters (empty)
  zc::Vector<zc::Own<ast::TypeParameterDeclaration>> typeParams;

  // Create function declaration
  auto funcDecl = createFunctionDeclaration(zc::mv(funcName), zc::mv(typeParams),
                                            zc::mv(parameters), zc::none, zc::mv(body));

  ZC_EXPECT(funcDecl->getName().getText() == "testFunction", "Function should have correct name");
  ZC_EXPECT(funcDecl->getParameters().size() == 2, "Function should have 2 parameters");
  ZC_EXPECT(funcDecl->getTypeParameters().size() == 0, "Function should have no type parameters");
  ZC_EXPECT(funcDecl->getReturnType() == nullptr, "Function should have no return type");
  // Function always has a body (reference, not pointer)
}

ZC_TEST("ASTFactory: Expression Creation") {
  // Test various expression creation functionality
  using namespace zomlang::compiler::ast::factory;

  // Test binary expression creation
  auto left = createFloatLiteral(10.0);
  auto right = createFloatLiteral(20.0);
  auto addOp = createTokenNode(SyntaxKind::Plus);
  auto binaryExpr = createBinaryExpression(zc::mv(left), zc::mv(addOp), zc::mv(right));

  // Note: We can't directly access BinaryExpression methods from Expression pointer
  // This tests that the factory function returns a valid expression

  // Test call expression creation
  auto callee = createIdentifier("myFunction"_zc);
  zc::Vector<zc::Own<ast::Expression>> args;
  args.add(createStringLiteral("arg1"_zc));
  args.add(createFloatLiteral(42.0));
  auto callExpr = createCallExpression(zc::mv(callee), zc::none, zc::none, zc::mv(args));

  // CallExpression always has callee (reference, not pointer)
  ZC_EXPECT(callExpr->getArguments().size() == 2, "CallExpression should have 2 arguments");

  // Test conditional expression creation
  auto test = createBooleanLiteral(true);
  auto consequent = createStringLiteral("true_branch"_zc);
  auto alternate = createStringLiteral("false_branch"_zc);
  auto condExpr = createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));

  // ConditionalExpression always has test, consequent, and alternate (references, not pointers)
}

ZC_TEST("ASTFactory: Statement Creation") {
  // Test various statement creation functionality
  using namespace zomlang::compiler::ast::factory;

  // Test variable declaration creation
  zc::Vector<zc::Own<ast::BindingElement>> bindings;
  auto varName = createIdentifier("myVar"_zc);
  auto initValue = createFloatLiteral(100.0);
  auto binding = createBindingElement(zc::mv(varName), zc::none, zc::mv(initValue));
  bindings.add(zc::mv(binding));
  auto varDecl = createVariableDeclarationList(zc::mv(bindings));

  ZC_EXPECT(varDecl->getBindings().size() == 1, "VariableDeclarationList should have 1 binding");

  // Test if statement creation
  auto condition = createBooleanLiteral(true);
  auto thenStmt = createEmptyStatement();
  auto elseStmt = createEmptyStatement();
  auto ifStmt = createIfStatement(zc::mv(condition), zc::mv(thenStmt), zc::mv(elseStmt));

  // IfStatement always has condition and then statement (references, not pointers)
  // Note: else statement can be null pointer since it's optional

  // Test return statement creation
  auto returnValue = createStringLiteral("success"_zc);
  auto returnStmt = createReturnStatement(zc::mv(returnValue));

  // ReturnStatement expression can be null pointer since it's optional

  // Test empty statement creation
  auto emptyStmt = createEmptyStatement();

  // Test block statement creation
  zc::Vector<zc::Own<ast::Statement>> statements;
  statements.add(createEmptyStatement());
  statements.add(createEmptyStatement());
  auto blockStmt = createBlockStatement(zc::mv(statements));

  ZC_EXPECT(blockStmt->getStatements().size() == 2, "BlockStatement should have 2 statements");
}

ZC_TEST("ASTFactory: Type Creation") {
  using namespace zomlang::compiler::ast::factory;

  // Test TypeReference creation
  auto typeName = createIdentifier("Int"_zc);
  auto typeRef = createTypeReference(zc::mv(typeName), zc::none);
  ZC_EXPECT(typeRef->getName().getText() == "Int", "TypeReference should have correct type name");

  // Test ArrayType creation
  auto elemType = createPredefinedType("str"_zc);
  auto arrayType = createArrayType(zc::mv(elemType));
  // ArrayType always has element type (reference, not pointer)

  // Test UnionType creation
  zc::Vector<zc::Own<ast::TypeNode>> unionTypes;
  unionTypes.add(createPredefinedType("i32"_zc));
  unionTypes.add(createPredefinedType("str"_zc));
  auto unionType = createUnionType(zc::mv(unionTypes));
  ZC_EXPECT(unionType->getTypes().size() == 2, "UnionType should have 2 types");

  // Test IntersectionType creation
  zc::Vector<zc::Own<ast::TypeNode>> intersectionTypes;
  intersectionTypes.add(createPredefinedType("i32"_zc));
  intersectionTypes.add(createPredefinedType("str"_zc));
  auto intersectionType = createIntersectionType(zc::mv(intersectionTypes));
  ZC_EXPECT(intersectionType->getTypes().size() == 2, "IntersectionType should have 2 types");
}

ZC_TEST("ASTFactory: Operator Creation") {
  using namespace zomlang::compiler::ast::factory;

  // Test UnaryOperator creation
  auto unaryOp = createTokenNode(SyntaxKind::Exclamation);
  ZC_EXPECT(unaryOp->getKind() == SyntaxKind::Exclamation, "UnaryOperator should be prefix");

  // Test AssignmentOperator creation
  auto assignOp = createTokenNode(SyntaxKind::Equals);
  ZC_EXPECT(assignOp->getKind() == SyntaxKind::Equals,
            "AssignmentOperator should have correct kind");
}

ZC_TEST("ASTFactory: Alias and Debugger Creation") {
  using namespace zomlang::compiler::ast::factory;

  // Test AliasDeclaration creation
  auto aliasName = createIdentifier("MyAlias"_zc);
  auto targetType = createPredefinedType("i32"_zc);
  auto aliasDecl = createAliasDeclaration(zc::mv(aliasName), zc::mv(targetType));
  ZC_EXPECT(aliasDecl->getName().getText() == "MyAlias",
            "AliasDeclaration should have correct name");
  // AliasDeclaration always has target type (reference, not pointer)

  // Test DebuggerStatement creation
  auto debuggerStmt = createDebuggerStatement();
  ZC_EXPECT(debuggerStmt->getKind() == SyntaxKind::DebuggerStatement,
            "DebuggerStatement should have correct kind");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
