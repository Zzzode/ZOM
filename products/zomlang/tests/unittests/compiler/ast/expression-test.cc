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

#include "zomlang/compiler/ast/expression.h"

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("ExpressionTest.BinaryExpressionCreation") {
  auto left = factory::createFloatLiteral(5.0);
  auto right = factory::createFloatLiteral(3.0);
  auto op = factory::createTokenNode(SyntaxKind::Plus);
  auto expr = factory::createBinaryExpression(zc::mv(left), zc::mv(op), zc::mv(right));

  ZC_EXPECT(expr->getKind() == SyntaxKind::BinaryExpression);

  ZC_EXPECT(expr->getLeft().getKind() == SyntaxKind::FloatLiteral);
  ZC_EXPECT(expr->getOperator().getKind() == SyntaxKind::Plus);
}

ZC_TEST("ExpressionTest.UnaryExpression") {
  auto operand = factory::createFloatLiteral(10.0);
  auto expr = factory::createPrefixUnaryExpression(SyntaxKind::Exclamation, zc::mv(operand));

  ZC_EXPECT(expr->getKind() == SyntaxKind::PrefixUnaryExpression);
  ZC_EXPECT(expr->getOperand().getKind() == SyntaxKind::FloatLiteral);
}

ZC_TEST("ExpressionTest.AssignmentExpression") {
  auto lhs = factory::createIdentifier("x"_zc);
  auto rhs = factory::createFloatLiteral(42.0);
  auto op = factory::createTokenNode(SyntaxKind::Equals);
  auto expr = factory::createBinaryExpression(zc::mv(lhs), zc::mv(op), zc::mv(rhs));

  ZC_EXPECT(expr->getKind() == SyntaxKind::BinaryExpression);
}

ZC_TEST("ExpressionTest.LiteralExpressions") {
  auto strLit = factory::createStringLiteral("test");
  ZC_EXPECT(strLit->getValue() == "test");

  auto numLit = factory::createFloatLiteral(3.14);
  ZC_EXPECT(numLit->getValue() == 3.14);

  auto boolLit = factory::createBooleanLiteral(true);
  ZC_EXPECT(boolLit->getValue() == true);

  auto nullLit = factory::createNullLiteral();
  ZC_EXPECT(nullLit->getKind() == SyntaxKind::NullLiteral);
}

ZC_TEST("ExpressionTest.CallExpression") {
  auto callee = factory::createIdentifier("func"_zc);
  zc::Vector<zc::Own<Expression>> args;
  args.add(factory::createIntegerLiteral(1));
  args.add(factory::createIntegerLiteral(2));
  auto expr = factory::createCallExpression(zc::mv(callee), zc::none, zc::none, zc::mv(args));

  ZC_EXPECT(expr->getKind() == SyntaxKind::CallExpression);
  ZC_EXPECT(expr->getArguments().size() == 2);
}

ZC_TEST("ExpressionTest.PostfixUnaryExpression") {
  // Test postfix increment
  auto operand1 = factory::createIdentifier("x"_zc);
  auto expr1 = factory::createPostfixUnaryExpression(SyntaxKind::PlusPlus, zc::mv(operand1));

  ZC_EXPECT(expr1->getKind() == SyntaxKind::PostfixUnaryExpression);
  ZC_EXPECT(expr1->getOperator() == SyntaxKind::PlusPlus);
  ZC_EXPECT(!expr1->isPrefix());

  // Test postfix decrement
  auto operand2 = factory::createIdentifier("y"_zc);
  auto expr2 = factory::createPostfixUnaryExpression(SyntaxKind::MinusMinus, zc::mv(operand2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::PostfixUnaryExpression);
  ZC_EXPECT(expr2->getOperator() == SyntaxKind::MinusMinus);
  ZC_EXPECT(!expr2->isPrefix());
}

ZC_TEST("ExpressionTest.NewExpression") {
  // Test new expression with arguments
  auto callee1 = factory::createIdentifier("MyClass"_zc);
  zc::Vector<zc::Own<Expression>> args1;
  args1.add(factory::createIntegerLiteral(42));
  args1.add(factory::createStringLiteral("test"_zc));
  auto expr1 = factory::createNewExpression(zc::mv(callee1), zc::none, zc::mv(args1));

  ZC_EXPECT(expr1->getKind() == SyntaxKind::NewExpression);
  ZC_EXPECT(expr1->getArguments() != zc::none);
  ZC_EXPECT(zc::_::readMaybe(expr1->getArguments()) != nullptr);
  ZC_EXPECT(zc::_::readMaybe(expr1->getArguments())->size() == 2);

  // Test new expression without arguments
  auto callee2 = factory::createIdentifier("EmptyClass"_zc);
  zc::Vector<zc::Own<Expression>> args2;
  auto expr2 = factory::createNewExpression(zc::mv(callee2), zc::none, zc::mv(args2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::NewExpression);
  ZC_EXPECT(expr2->getArguments() != zc::none);
  ZC_EXPECT(zc::_::readMaybe(expr2->getArguments()) != nullptr);
  ZC_EXPECT(zc::_::readMaybe(expr2->getArguments())->size() == 0);
}

ZC_TEST("ExpressionTest.ConditionalExpression") {
  // Test ternary operator: condition ? consequent : alternate
  auto test = factory::createBooleanLiteral(true);
  auto consequent = factory::createStringLiteral("yes"_zc);
  auto alternate = factory::createStringLiteral("no"_zc);
  auto expr = factory::createConditionalExpression(zc::mv(test), zc::none, zc::mv(consequent),
                                                   zc::none, zc::mv(alternate));

  ZC_EXPECT(expr->getKind() == SyntaxKind::ConditionalExpression);

  // Test with complex condition
  auto test2 = factory::createBinaryExpression(factory::createIntegerLiteral(5),
                                               factory::createTokenNode(SyntaxKind::GreaterThan),
                                               factory::createIntegerLiteral(3));
  auto consequent2 = factory::createIntegerLiteral(100);
  auto alternate2 = factory::createIntegerLiteral(0);
  auto expr2 = factory::createConditionalExpression(zc::mv(test2), zc::none, zc::mv(consequent2),
                                                    zc::none, zc::mv(alternate2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::ConditionalExpression);
}

ZC_TEST("ExpressionTest.AsExpression") {
  // Test type conversion expression
  auto expression = factory::createIntegerLiteral(42);
  auto targetType = factory::createPredefinedType("str"_zc);
  auto expr = factory::createAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::AsExpression);

  // Test with custom type
  auto expression2 = factory::createIdentifier("obj"_zc);
  auto targetType2 =
      factory::createTypeReference(factory::createIdentifier("MyClass"_zc), zc::none);
  auto expr2 = factory::createAsExpression(zc::mv(expression2), zc::mv(targetType2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::AsExpression);
}

ZC_TEST("ExpressionTest.ForcedAsExpression") {
  // Test forced type conversion expression
  auto expression = factory::createIdentifier("value"_zc);
  auto targetType = factory::createPredefinedType("i32"_zc);
  auto expr = factory::createForcedAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::ForcedAsExpression);
}

ZC_TEST("ExpressionTest.ConditionalAsExpression") {
  // Test conditional type conversion expression
  auto expression = factory::createIdentifier("maybeValue"_zc);
  auto targetType = factory::createPredefinedType("bool"_zc);
  auto expr = factory::createConditionalAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::ConditionalAsExpression);
}

ZC_TEST("ExpressionTest.VoidExpression") {
  // Test void expression
  auto expression =
      factory::createCallExpression(factory::createIdentifier("sideEffect"_zc), zc::none, zc::none,
                                    zc::Vector<zc::Own<Expression>>());
  auto expr = factory::createVoidExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::VoidExpression);
}

ZC_TEST("ExpressionTest.TypeOfExpression") {
  // Test typeof expression with identifier
  auto expression = factory::createIdentifier("variable"_zc);
  auto expr = factory::createTypeOfExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::TypeOfExpression);

  // Test typeof expression with literal
  auto expression2 = factory::createStringLiteral("hello"_zc);
  auto expr2 = factory::createTypeOfExpression(zc::mv(expression2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::TypeOfExpression);
}

ZC_TEST("ExpressionTest.AwaitExpression") {
  // Test await expression with function call
  auto expression =
      factory::createCallExpression(factory::createIdentifier("asyncFunction"_zc), zc::none,
                                    zc::none, zc::Vector<zc::Own<Expression>>());
  auto expr = factory::createAwaitExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::AwaitExpression);

  // Test await expression with identifier
  auto expression2 = factory::createIdentifier("promise"_zc);
  auto expr2 = factory::createAwaitExpression(zc::mv(expression2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::AwaitExpression);
}

ZC_TEST("ExpressionTest.ElementAccessExpression") {
  // Test basic array access
  auto arrayExpr = factory::createIdentifier("arr"_zc);
  auto indexExpr = factory::createIntegerLiteral(0);
  auto elementAccess = factory::createElementAccessExpression(zc::mv(arrayExpr), zc::mv(indexExpr));

  ZC_EXPECT(elementAccess->getKind() == SyntaxKind::ElementAccessExpression);
  ZC_EXPECT(elementAccess->isQuestionDot() == false);

  // Test object property access with string key
  auto objExpr = factory::createIdentifier("obj"_zc);
  auto propIndexExpr = factory::createStringLiteral("prop"_zc);
  auto propElementAccess =
      factory::createElementAccessExpression(zc::mv(objExpr), zc::mv(propIndexExpr));

  ZC_EXPECT(propElementAccess->getKind() == SyntaxKind::ElementAccessExpression);

  // Test with questionDot = true
  auto maybeArrExpr = factory::createIdentifier("maybeArr"_zc);
  auto keyExpr = factory::createStringLiteral("key"_zc);
  auto optionalElementAccess =
      factory::createElementAccessExpression(zc::mv(maybeArrExpr), zc::mv(keyExpr), true, true);

  ZC_EXPECT(optionalElementAccess->getKind() == SyntaxKind::ElementAccessExpression);
  ZC_EXPECT(optionalElementAccess->isQuestionDot() == true);
  ZC_EXPECT(hasFlag(optionalElementAccess->getFlags(), NodeFlags::OptionalChain));
}

ZC_TEST("ExpressionTest.PropertyAccessExpression") {
  // Test regular property access
  auto objExpr = factory::createIdentifier("obj"_zc);
  auto propName = factory::createIdentifier("prop"_zc);
  auto propertyAccess =
      factory::createPropertyAccessExpression(zc::mv(objExpr), zc::mv(propName), false);

  ZC_EXPECT(propertyAccess->getKind() == SyntaxKind::PropertyAccessExpression);
  ZC_EXPECT(propertyAccess->isQuestionDot() == false);

  // Test optional property access
  auto optionalObjExpr = factory::createIdentifier("obj"_zc);
  auto optionalPropName = factory::createIdentifier("prop"_zc);
  auto optionalPropertyAccess = factory::createPropertyAccessExpression(
      zc::mv(optionalObjExpr), zc::mv(optionalPropName), true, true);

  ZC_EXPECT(optionalPropertyAccess->getKind() == SyntaxKind::PropertyAccessExpression);
  ZC_EXPECT(optionalPropertyAccess->isQuestionDot() == true);
  ZC_EXPECT(hasFlag(optionalPropertyAccess->getFlags(), NodeFlags::OptionalChain));
}

ZC_TEST("ExpressionTest.Identifier") {
  // Test basic identifier
  auto identifier = factory::createIdentifier("variableName"_zc);

  ZC_EXPECT(identifier->getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(identifier->getText() == "variableName");

  // Test identifier with underscore
  auto underscoreId = factory::createIdentifier("_privateVar"_zc);
  ZC_EXPECT(underscoreId->getKind() == SyntaxKind::Identifier);

  // Test identifier with numbers
  auto numberedId = factory::createIdentifier("var123"_zc);
  ZC_EXPECT(numberedId->getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(numberedId->getText() == "var123");
}

ZC_TEST("ExpressionTest.GetterMethods") {
  // Test prefix unary expression getters
  auto operand = factory::createIdentifier("x"_zc);
  auto prefixExpr = factory::createPrefixUnaryExpression(SyntaxKind::PlusPlus, zc::mv(operand));

  ZC_EXPECT(prefixExpr->getOperand().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(prefixExpr->isPrefix() == true);

  // Test postfix unary expression getters
  auto postfixOperand = factory::createIdentifier("y"_zc);
  auto postfixExpr =
      factory::createPostfixUnaryExpression(SyntaxKind::PlusPlus, zc::mv(postfixOperand));

  ZC_EXPECT(postfixExpr->getOperand().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(postfixExpr->isPrefix() == false);

  // Test conditional expression getters
  auto test = factory::createIdentifier("condition"_zc);
  auto consequent = factory::createIdentifier("trueValue"_zc);
  auto alternate = factory::createIdentifier("falseValue"_zc);
  auto conditionalExpr = factory::createConditionalExpression(
      zc::mv(test), zc::none, zc::mv(consequent), zc::none, zc::mv(alternate));

  ZC_EXPECT(conditionalExpr->getTest().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(conditionalExpr->getConsequent().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(conditionalExpr->getAlternate().getKind() == SyntaxKind::Identifier);

  // Test call expression getters
  auto callee = factory::createIdentifier("func"_zc);
  auto callExpr = factory::createCallExpression(zc::mv(callee), zc::none, zc::none, {});

  ZC_EXPECT(callExpr->getCallee().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(callExpr->getArguments().size() == 0);
}

ZC_TEST("ExpressionTest.ParenthesizedExpression") {
  auto innerExpr = factory::createFloatLiteral(42.0);
  auto parenExpr = factory::createParenthesizedExpression(zc::mv(innerExpr));

  ZC_EXPECT(parenExpr->getKind() == SyntaxKind::ParenthesizedExpression);
  ZC_EXPECT(parenExpr->getExpression().getKind() == SyntaxKind::FloatLiteral);

  // Test with complex nested expression
  auto left = factory::createFloatLiteral(1.0);
  auto right = factory::createFloatLiteral(2.0);
  auto op = factory::createTokenNode(SyntaxKind::Plus);
  auto binaryExpr = factory::createBinaryExpression(zc::mv(left), zc::mv(op), zc::mv(right));
  auto nestedParenExpr = factory::createParenthesizedExpression(zc::mv(binaryExpr));

  ZC_EXPECT(nestedParenExpr->getKind() == SyntaxKind::ParenthesizedExpression);
  ZC_EXPECT(nestedParenExpr->getExpression().getKind() == SyntaxKind::BinaryExpression);
}

ZC_TEST("ExpressionTest.FunctionExpression") {
  // Test simple function expression without type parameters
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto paramName = factory::createIdentifier("x"_zc);
  auto paramType = factory::createPredefinedType("i32"_zc);
  auto param = factory::createParameterDeclaration({}, zc::none, zc::mv(paramName), zc::none,
                                                   zc::mv(paramType), zc::none);
  params.add(zc::mv(param));

  auto returnTypeInner = factory::createPredefinedType("i32"_zc);
  auto returnType = factory::createReturnType(zc::mv(returnTypeInner), zc::none);
  auto body = factory::createBlockStatement(zc::Vector<zc::Own<Statement>>());

  auto funcExpr = factory::createFunctionExpression(zc::mv(typeParams), zc::mv(params), {},
                                                    zc::mv(returnType), zc::mv(body));

  ZC_EXPECT(funcExpr->getKind() == SyntaxKind::FunctionExpression);
  ZC_EXPECT(funcExpr->getParameters().size() == 1);
  ZC_EXPECT(funcExpr->getReturnType() != zc::none);
  ZC_EXPECT(funcExpr->getBody().getKind() == SyntaxKind::BlockStatement);

  // Test function expression with type parameters
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParamsWithGeneric;
  auto typeParam =
      factory::createTypeParameterDeclaration(factory::createIdentifier("T"_zc), zc::none);
  typeParamsWithGeneric.add(zc::mv(typeParam));

  zc::Vector<zc::Own<ParameterDeclaration>> genericParams;
  auto genericParamName = factory::createIdentifier("value"_zc);
  auto genericParamType = factory::createTypeReference(factory::createIdentifier("T"_zc), zc::none);
  auto genericParam = factory::createParameterDeclaration(
      {}, zc::none, zc::mv(genericParamName), zc::none, zc::mv(genericParamType), zc::none);
  genericParams.add(zc::mv(genericParam));

  auto genericReturnTypeInner =
      factory::createTypeReference(factory::createIdentifier("T"_zc), zc::none);
  auto genericReturnType = factory::createReturnType(zc::mv(genericReturnTypeInner), zc::none);
  auto genericBody = factory::createBlockStatement(zc::Vector<zc::Own<Statement>>());

  auto genericFuncExpr =
      factory::createFunctionExpression(zc::mv(typeParamsWithGeneric), zc::mv(genericParams), {},
                                        zc::mv(genericReturnType), zc::mv(genericBody));

  ZC_EXPECT(genericFuncExpr->getKind() == SyntaxKind::FunctionExpression);
  ZC_EXPECT(ZC_ASSERT_NONNULL(genericFuncExpr->getTypeParameters()).size() == 1);
  ZC_EXPECT(genericFuncExpr->getParameters().size() == 1);
}

ZC_TEST("ExpressionTest.ArrayLiteralExpression") {
  // Test empty array literal
  zc::Vector<zc::Own<Expression>> emptyElements;
  auto emptyArray = factory::createArrayLiteralExpression(zc::mv(emptyElements));

  ZC_EXPECT(emptyArray->getKind() == SyntaxKind::ArrayLiteralExpression);
  ZC_EXPECT(emptyArray->getElements().size() == 0);

  // Test array literal with elements
  zc::Vector<zc::Own<Expression>> elements;
  elements.add(factory::createIntegerLiteral(1));
  elements.add(factory::createIntegerLiteral(2));
  elements.add(factory::createIntegerLiteral(3));
  auto arrayExpr = factory::createArrayLiteralExpression(zc::mv(elements));

  ZC_EXPECT(arrayExpr->getKind() == SyntaxKind::ArrayLiteralExpression);
  ZC_EXPECT(arrayExpr->getElements().size() == 3);

  // Test array literal with mixed types
  zc::Vector<zc::Own<Expression>> mixedElements;
  mixedElements.add(factory::createStringLiteral("hello"_zc));
  mixedElements.add(factory::createFloatLiteral(3.14));
  mixedElements.add(factory::createBooleanLiteral(true));
  auto mixedArray = factory::createArrayLiteralExpression(zc::mv(mixedElements));

  ZC_EXPECT(mixedArray->getKind() == SyntaxKind::ArrayLiteralExpression);
  ZC_EXPECT(mixedArray->getElements().size() == 3);
}

ZC_TEST("ExpressionTest.ObjectLiteralExpression") {
  // Test empty object literal
  zc::Vector<zc::Own<ObjectLiteralElement>> emptyProperties;
  auto emptyObject = factory::createObjectLiteralExpression(zc::mv(emptyProperties));

  ZC_EXPECT(emptyObject->getKind() == SyntaxKind::ObjectLiteralExpression);
  ZC_EXPECT(emptyObject->getProperties().size() == 0);

  // Test object literal with properties
  zc::Vector<zc::Own<ObjectLiteralElement>> properties;

  // Property: "name": "Alice"
  auto nameKey = factory::createIdentifier("name"_zc);
  auto nameValue = factory::createStringLiteral("Alice"_zc);
  properties.add(factory::createPropertyAssignment(zc::mv(nameKey), zc::mv(nameValue)));

  // Property: "age": 30
  auto ageKey = factory::createIdentifier("age"_zc);
  auto ageValue = factory::createIntegerLiteral(30);
  properties.add(factory::createPropertyAssignment(zc::mv(ageKey), zc::mv(ageValue)));

  auto objectExpr = factory::createObjectLiteralExpression(zc::mv(properties));

  ZC_EXPECT(objectExpr->getKind() == SyntaxKind::ObjectLiteralExpression);
  ZC_EXPECT(objectExpr->getProperties().size() == 2);
}

ZC_TEST("ExpressionTest.ShorthandPropertyAssignment") {
  auto name = factory::createIdentifier("shorthand"_zc);
  auto shorthand = factory::createShorthandPropertyAssignment(zc::mv(name), zc::none, zc::none);

  ZC_EXPECT(shorthand->getKind() == SyntaxKind::ShorthandPropertyAssignment);
  ZC_EXPECT(shorthand->getNameIdentifier().getText() == "shorthand"_zc);

  // Test with initializer
  auto name2 = factory::createIdentifier("shorthandWithInit"_zc);
  auto init = factory::createIntegerLiteral(42);
  auto shorthand2 =
      factory::createShorthandPropertyAssignment(zc::mv(name2), zc::mv(init), zc::none);

  ZC_EXPECT(shorthand2->getNameIdentifier().getText() == "shorthandWithInit"_zc);
  ZC_IF_SOME(val, shorthand2->getObjectAssignmentInitializer()) {
    ZC_EXPECT(val.getKind() == SyntaxKind::IntegerLiteral);
  }
  else { ZC_EXPECT(false); }
}

ZC_TEST("ExpressionTest.AcceptMethods") {
  // Test that all expression types have proper accept methods
  // This is a basic test to ensure the visitor pattern works

  auto prefixOperand = factory::createIdentifier("x"_zc);
  auto prefixExpr =
      factory::createPrefixUnaryExpression(SyntaxKind::PlusPlus, zc::mv(prefixOperand));
  ZC_EXPECT(prefixExpr->getKind() == SyntaxKind::PrefixUnaryExpression);

  // Test postfix unary expression
  auto postfixOperand = factory::createIdentifier("y"_zc);
  auto postfixExpr =
      factory::createPostfixUnaryExpression(SyntaxKind::PlusPlus, zc::mv(postfixOperand));
  ZC_EXPECT(postfixExpr->getKind() == SyntaxKind::PostfixUnaryExpression);

  // Test conditional expression
  auto test = factory::createIdentifier("condition"_zc);
  auto consequent = factory::createIdentifier("trueValue"_zc);
  auto alternate = factory::createIdentifier("falseValue"_zc);
  auto conditionalExpr = factory::createConditionalExpression(
      zc::mv(test), zc::none, zc::mv(consequent), zc::none, zc::mv(alternate));
  ZC_EXPECT(conditionalExpr->getKind() == SyntaxKind::ConditionalExpression);

  // Test call expression
  auto callee = factory::createIdentifier("func"_zc);
  auto callExpr = factory::createCallExpression(zc::mv(callee), zc::none, zc::none, {});
  ZC_EXPECT(callExpr->getKind() == SyntaxKind::CallExpression);

  // Test element access expression
  auto object = factory::createIdentifier("arr"_zc);
  auto index = factory::createIntegerLiteral(0);
  auto elementExpr = factory::createElementAccessExpression(zc::mv(object), zc::mv(index));
  ZC_EXPECT(elementExpr->getKind() == SyntaxKind::ElementAccessExpression);

  // Test property access expression
  auto propObject = factory::createIdentifier("obj"_zc);
  auto propName = factory::createIdentifier("prop"_zc);
  auto propExpr =
      factory::createPropertyAccessExpression(zc::mv(propObject), zc::mv(propName), false);
  ZC_EXPECT(propExpr->getKind() == SyntaxKind::PropertyAccessExpression);

  // Test ParenthesizedExpression accept method
  auto parenExpr = factory::createParenthesizedExpression(factory::createFloatLiteral(42.0));
  ZC_EXPECT(parenExpr->getKind() == SyntaxKind::ParenthesizedExpression);
}

ZC_TEST("ExpressionTest.WildcardPattern") {
  // Test basic wildcard pattern creation
  auto wildcardPattern = factory::createWildcardPattern();

  ZC_EXPECT(wildcardPattern->getKind() == SyntaxKind::WildcardPattern);
}

ZC_TEST("ExpressionTest.IdentifierPattern") {
  // Test identifier pattern creation
  auto identifier = factory::createIdentifier("variableName"_zc);
  auto identifierPattern = factory::createIdentifierPattern(zc::mv(identifier));

  ZC_EXPECT(identifierPattern->getKind() == SyntaxKind::IdentifierPattern);
  ZC_EXPECT(identifierPattern->getIdentifier().getText() == "variableName"_zc);

  // Test with different identifier names
  auto identifier2 = factory::createIdentifier("_underscore"_zc);
  auto identifierPattern2 = factory::createIdentifierPattern(zc::mv(identifier2));
  ZC_EXPECT(identifierPattern2->getIdentifier().getText() == "_underscore"_zc);
}

ZC_TEST("ExpressionTest.TuplePattern") {
  // Test empty tuple pattern
  zc::Vector<zc::Own<Pattern>> emptyElements;
  auto emptyTuplePattern = factory::createTuplePattern(zc::mv(emptyElements));

  ZC_EXPECT(emptyTuplePattern->getKind() == SyntaxKind::TuplePattern);
  ZC_EXPECT(emptyTuplePattern->getElements().size() == 0);

  // Test tuple pattern with mixed elements
  zc::Vector<zc::Own<Pattern>> elements;
  auto identifier = factory::createIdentifier("x"_zc);
  auto identifierPattern = factory::createIdentifierPattern(zc::mv(identifier));
  auto wildcardPattern = factory::createWildcardPattern();

  elements.add(zc::mv(identifierPattern));
  elements.add(zc::mv(wildcardPattern));

  auto tuplePattern = factory::createTuplePattern(zc::mv(elements));
  ZC_EXPECT(tuplePattern->getKind() == SyntaxKind::TuplePattern);
  ZC_EXPECT(tuplePattern->getElements().size() == 2);
}

ZC_TEST("ExpressionTest.ArrayPattern") {
  // Test empty array pattern
  zc::Vector<zc::Own<Pattern>> emptyElements;
  auto emptyArrayPattern = factory::createArrayPattern(zc::mv(emptyElements));

  ZC_EXPECT(emptyArrayPattern->getKind() == SyntaxKind::ArrayPattern);
  ZC_EXPECT(emptyArrayPattern->getElements().size() == 0);

  // Test array pattern with elements
  zc::Vector<zc::Own<Pattern>> elements;
  auto identifier1 = factory::createIdentifier("first"_zc);
  auto pattern1 = factory::createIdentifierPattern(zc::mv(identifier1));
  auto identifier2 = factory::createIdentifier("second"_zc);
  auto pattern2 = factory::createIdentifierPattern(zc::mv(identifier2));

  elements.add(zc::mv(pattern1));
  elements.add(zc::mv(pattern2));

  auto arrayPattern = factory::createArrayPattern(zc::mv(elements));
  ZC_EXPECT(arrayPattern->getKind() == SyntaxKind::ArrayPattern);
  ZC_EXPECT(arrayPattern->getElements().size() == 2);
}

ZC_TEST("ExpressionTest.ExpressionPattern") {
  // Test expression pattern with literal
  auto literal = factory::createIntegerLiteral(42);
  auto expressionPattern = factory::createExpressionPattern(zc::mv(literal));

  ZC_EXPECT(expressionPattern->getKind() == SyntaxKind::ExpressionPattern);
  ZC_EXPECT(expressionPattern->getExpression().getKind() == SyntaxKind::IntegerLiteral);

  // Test expression pattern with identifier
  auto identifier = factory::createIdentifier("constant"_zc);
  auto expressionPattern2 = factory::createExpressionPattern(zc::mv(identifier));
  ZC_EXPECT(expressionPattern2->getExpression().getKind() == SyntaxKind::Identifier);
}

ZC_TEST("ExpressionTest.IsPattern") {
  // Test is pattern with predefined type
  auto type = factory::createPredefinedType("i32"_zc);
  auto isPattern = factory::createIsPattern(zc::mv(type));

  ZC_EXPECT(isPattern->getKind() == SyntaxKind::IsPattern);
  ZC_EXPECT(isPattern->getType().getKind() == SyntaxKind::I32TypeNode);
}

ZC_TEST("ExpressionTest.EnumPattern") {
  // Test enum pattern
  auto typeRef = factory::createTypeReference(factory::createIdentifier("MyEnum"_zc), zc::none);
  auto propertyName = factory::createIdentifier("Variant"_zc);

  // Create tuple pattern for enum variant
  zc::Vector<zc::Own<Pattern>> tupleElements;
  auto identifier = factory::createIdentifier("value"_zc);
  auto identifierPattern = factory::createIdentifierPattern(zc::mv(identifier));
  tupleElements.add(zc::mv(identifierPattern));
  auto tuplePattern = factory::createTuplePattern(zc::mv(tupleElements));

  auto enumPattern =
      factory::createEnumPattern(zc::mv(typeRef), zc::mv(propertyName), zc::mv(tuplePattern));

  ZC_EXPECT(enumPattern->getKind() == SyntaxKind::EnumPattern);
  ZC_EXPECT(enumPattern->getPropertyName().getText() == "Variant");
  ZC_EXPECT(enumPattern->getTuplePattern().getElements().size() == 1);

  // Test enum pattern without type reference
  auto propertyName2 = factory::createIdentifier("SimpleVariant"_zc);
  zc::Vector<zc::Own<Pattern>> emptyTupleElements;
  auto emptyTuplePattern = factory::createTuplePattern(zc::mv(emptyTupleElements));
  auto typeRef2 =
      factory::createTypeReference(factory::createIdentifier("SimpleEnum"_zc), zc::none);
  auto enumPattern2 = factory::createEnumPattern(zc::mv(typeRef2), zc::mv(propertyName2),
                                                 zc::mv(emptyTuplePattern));

  ZC_EXPECT(enumPattern2->getKind() == SyntaxKind::EnumPattern);
  ZC_EXPECT(enumPattern2->getPropertyName().getText() == "SimpleVariant");
  ZC_EXPECT(enumPattern2->getTuplePattern().getElements().size() == 0);
}

// ================================================================================
// Additional tests for uncovered expression types
// ================================================================================

ZC_TEST("ExpressionTest.CaptureElement ByValue") {
  // Test by-value capture with an identifier
  auto id = factory::createIdentifier("capturedVar"_zc);
  auto capture = factory::createCaptureElement(false, zc::mv(id), false);

  ZC_EXPECT(capture->getKind() == SyntaxKind::CaptureElement);
  ZC_EXPECT(capture->isByReference() == false);
  ZC_EXPECT(capture->isThis() == false);
  ZC_IF_SOME(ref, capture->getIdentifier()) { ZC_EXPECT(ref.getText() == "capturedVar"_zc); }
  else { ZC_FAIL_EXPECT("expected identifier to be present"); }
}

ZC_TEST("ExpressionTest.CaptureElement ByReference") {
  // Test by-reference capture
  auto id = factory::createIdentifier("refVar"_zc);
  auto capture = factory::createCaptureElement(true, zc::mv(id), false);

  ZC_EXPECT(capture->isByReference() == true);
  ZC_EXPECT(capture->isThis() == false);
  ZC_IF_SOME(ref, capture->getIdentifier()) { ZC_EXPECT(ref.getText() == "refVar"_zc); }
  else { ZC_FAIL_EXPECT("expected identifier to be present"); }
}

ZC_TEST("ExpressionTest.CaptureElement This") {
  // Test 'this' capture (isThis = true, no identifier)
  auto capture = factory::createCaptureElement(false, zc::none, true);

  ZC_EXPECT(capture->isByReference() == false);
  ZC_EXPECT(capture->isThis() == true);
  ZC_EXPECT(capture->getIdentifier() == zc::none);
}

ZC_TEST("ExpressionTest.SpreadElement") {
  // Test spread element creation and getter
  auto inner = factory::createIdentifier("items"_zc);
  auto spread = factory::createSpreadElement(zc::mv(inner));

  ZC_EXPECT(spread->getKind() == SyntaxKind::SpreadElement);
  ZC_EXPECT(spread->getExpression().getKind() == SyntaxKind::Identifier);
}

ZC_TEST("ExpressionTest.SpreadElement WithLiteral") {
  // Test spread element wrapping a call expression
  auto callee = factory::createIdentifier("getItems"_zc);
  auto call = factory::createCallExpression(zc::mv(callee), zc::none, zc::none, {});
  auto spread = factory::createSpreadElement(zc::mv(call));

  ZC_EXPECT(spread->getKind() == SyntaxKind::SpreadElement);
  ZC_EXPECT(spread->getExpression().getKind() == SyntaxKind::CallExpression);
}

ZC_TEST("ExpressionTest.SpreadAssignment") {
  // Test spread assignment creation and getter
  auto expr = factory::createIdentifier("otherObj"_zc);
  auto spreadAssign = factory::createSpreadAssignment(zc::mv(expr));

  ZC_EXPECT(spreadAssign->getKind() == SyntaxKind::SpreadAssignment);
  ZC_EXPECT(spreadAssign->getExpression().getKind() == SyntaxKind::Identifier);

  // SpreadAssignment has no name - getName() should return an empty Maybe
  auto nameResult = spreadAssign->getName();
  ZC_SWITCH_ONEOF(nameResult) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) { ZC_EXPECT(maybeId == zc::none); }
    ZC_CASE_ONEOF(maybeBp, zc::Maybe<const BindingPattern&>) { ZC_EXPECT(maybeBp == zc::none); }
  }
}

ZC_TEST("ExpressionTest.PropertyAssignment WithQuestionToken") {
  // Test property assignment with a question token
  auto name = factory::createIdentifier("optionalProp"_zc);
  auto init = factory::createStringLiteral("default"_zc);
  auto questionToken = factory::createTokenNode(SyntaxKind::Question);
  auto prop = factory::createPropertyAssignment(zc::mv(name), zc::mv(init), zc::mv(questionToken));

  ZC_EXPECT(prop->getKind() == SyntaxKind::PropertyAssignment);
  ZC_EXPECT(prop->getNameIdentifier().getText() == "optionalProp"_zc);
  ZC_IF_SOME(val, prop->getInitializer()) { ZC_EXPECT(val.getKind() == SyntaxKind::StringLiteral); }
  else { ZC_FAIL_EXPECT("expected initializer to be present"); }
  ZC_IF_SOME(qt, prop->getQuestionToken()) { ZC_EXPECT(qt.getKind() == SyntaxKind::Question); }
  else { ZC_FAIL_EXPECT("expected question token to be present"); }
}

ZC_TEST("ExpressionTest.PropertyAssignment WithoutQuestionToken") {
  // Test property assignment without a question token
  auto name = factory::createIdentifier("prop"_zc);
  auto init = factory::createIntegerLiteral(42);
  auto prop = factory::createPropertyAssignment(zc::mv(name), zc::mv(init), zc::none);

  ZC_EXPECT(prop->getNameIdentifier().getText() == "prop"_zc);
  ZC_IF_SOME(val, prop->getInitializer()) {
    ZC_EXPECT(val.getKind() == SyntaxKind::IntegerLiteral);
  }
  else { ZC_FAIL_EXPECT("expected initializer to be present"); }
  ZC_EXPECT(prop->getQuestionToken() == zc::none);
}

ZC_TEST("ExpressionTest.PropertyAssignment WithoutInitializer") {
  // Test property assignment without an initializer
  auto name = factory::createIdentifier("uninitialized"_zc);
  auto prop = factory::createPropertyAssignment(zc::mv(name), zc::none, zc::none);

  ZC_EXPECT(prop->getInitializer() == zc::none);
  ZC_EXPECT(prop->getQuestionToken() == zc::none);
}

ZC_TEST("ExpressionTest.ShorthandPropertyAssignment WithEqualsToken") {
  // Test shorthand property assignment with equals token
  auto name = factory::createIdentifier("x"_zc);
  auto init = factory::createIntegerLiteral(10);
  auto eqToken = factory::createTokenNode(SyntaxKind::Equals);
  auto shorthand =
      factory::createShorthandPropertyAssignment(zc::mv(name), zc::mv(init), zc::mv(eqToken));

  ZC_EXPECT(shorthand->getKind() == SyntaxKind::ShorthandPropertyAssignment);
  ZC_EXPECT(shorthand->getNameIdentifier().getText() == "x"_zc);
  ZC_IF_SOME(val, shorthand->getObjectAssignmentInitializer()) {
    ZC_EXPECT(val.getKind() == SyntaxKind::IntegerLiteral);
  }
  else { ZC_FAIL_EXPECT("expected initializer to be present"); }
  ZC_IF_SOME(et, shorthand->getEqualsToken()) { ZC_EXPECT(et.getKind() == SyntaxKind::Equals); }
  else { ZC_FAIL_EXPECT("expected equals token to be present"); }
}

ZC_TEST("ExpressionTest.ShorthandPropertyAssignment WithoutEqualsToken") {
  // Test shorthand property assignment without equals token
  auto name = factory::createIdentifier("y"_zc);
  auto shorthand = factory::createShorthandPropertyAssignment(zc::mv(name), zc::none, zc::none);

  ZC_EXPECT(shorthand->getNameIdentifier().getText() == "y"_zc);
  ZC_EXPECT(shorthand->getObjectAssignmentInitializer() == zc::none);
  ZC_EXPECT(shorthand->getEqualsToken() == zc::none);
}

ZC_TEST("ExpressionTest.TemplateSpan") {
  // Test template span creation
  auto expr = factory::createIdentifier("name"_zc);
  auto literal = factory::createStringLiteral(" world"_zc);
  auto span = factory::createTemplateSpan(zc::mv(expr), zc::mv(literal));

  ZC_EXPECT(span->getKind() == SyntaxKind::TemplateSpan);
  ZC_EXPECT(span->getExpression().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(span->getLiteral().getValue() == " world");
}

ZC_TEST("ExpressionTest.TemplateLiteralExpression") {
  // Test template literal expression with head and spans
  auto head = factory::createStringLiteral("Hello "_zc);

  zc::Vector<zc::Own<TemplateSpan>> spans;
  auto span1Expr = factory::createIdentifier("name"_zc);
  auto span1Lit = factory::createStringLiteral("! You are "_zc);
  spans.add(factory::createTemplateSpan(zc::mv(span1Expr), zc::mv(span1Lit)));

  auto span2Expr = factory::createIdentifier("age"_zc);
  auto span2Lit = factory::createStringLiteral(" years old."_zc);
  spans.add(factory::createTemplateSpan(zc::mv(span2Expr), zc::mv(span2Lit)));

  auto templateLit = factory::createTemplateLiteralExpression(zc::mv(head), zc::mv(spans));

  ZC_EXPECT(templateLit->getKind() == SyntaxKind::TemplateLiteralExpression);
  ZC_EXPECT(templateLit->getHead().getValue() == "Hello ");
  ZC_EXPECT(templateLit->getSpans().size() == 2);
}

ZC_TEST("ExpressionTest.TemplateLiteralExpression EmptySpans") {
  // Test template literal expression with no spans (simple string template)
  auto head = factory::createStringLiteral("simple string"_zc);
  auto templateLit = factory::createTemplateLiteralExpression(zc::mv(head), {});

  ZC_EXPECT(templateLit->getHead().getValue() == "simple string");
  ZC_EXPECT(templateLit->getSpans().size() == 0);
}

ZC_TEST("ExpressionTest.NonNullExpression") {
  // Test non-null expression creation and getter
  auto inner = factory::createIdentifier("maybeValue"_zc);
  auto nonNull = factory::createNonNullExpression(zc::mv(inner));

  ZC_EXPECT(nonNull->getKind() == SyntaxKind::NonNullExpression);
  ZC_EXPECT(nonNull->getExpression().getKind() == SyntaxKind::Identifier);
}

ZC_TEST("ExpressionTest.NonNullExpression WithPropertyAccess") {
  // Test non-null expression wrapping a property access
  auto obj = factory::createIdentifier("obj"_zc);
  auto prop = factory::createIdentifier("field"_zc);
  auto propAccess = factory::createPropertyAccessExpression(zc::mv(obj), zc::mv(prop), true);
  auto nonNull = factory::createNonNullExpression(zc::mv(propAccess));

  ZC_EXPECT(nonNull->getExpression().getKind() == SyntaxKind::PropertyAccessExpression);
}

ZC_TEST("ExpressionTest.ThisExpression") {
  // Test this expression creation
  auto thisExpr = factory::createThisExpression();

  ZC_EXPECT(thisExpr->getKind() == SyntaxKind::ThisExpression);
}

ZC_TEST("ExpressionTest.BigIntLiteral") {
  // Test BigInt literal creation (factory uses the string-based factory)
  auto bigInt = factory::createIdentifier("123n"_zc);
  ZC_EXPECT(bigInt->getText() == "123n"_zc);
}

ZC_TEST("ExpressionTest.ExpressionWithTypeArguments") {
  // Test expression with type arguments creation
  auto expr = factory::createIdentifier("List"_zc);
  zc::Vector<zc::Own<TypeNode>> typeArgs;
  typeArgs.add(factory::createPredefinedType("i32"_zc));
  auto ewta = factory::createExpressionWithTypeArguments(zc::mv(expr), zc::mv(typeArgs));

  ZC_EXPECT(ewta->getKind() == SyntaxKind::ExpressionWithTypeArguments);
  ZC_EXPECT(ewta->getExpression().getKind() == SyntaxKind::Identifier);

  ZC_IF_SOME(args, ewta->getTypeArguments()) { ZC_EXPECT(args.size() == 1); }
  else { ZC_FAIL_EXPECT("expected type arguments to be present"); }
}

ZC_TEST("ExpressionTest.ExpressionWithTypeArguments NoTypeArgs") {
  // Test expression with no type arguments
  auto expr = factory::createIdentifier("SimpleType"_zc);
  auto ewta = factory::createExpressionWithTypeArguments(zc::mv(expr), zc::none);

  ZC_EXPECT(ewta->getExpression().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(ewta->getTypeArguments() == zc::none);
}

ZC_TEST("ExpressionTest.WildcardPattern WithTypeAnnotation") {
  // Test wildcard pattern with a type annotation using direct construction
  auto type = factory::createPredefinedType("i32"_zc);
  auto wildcard = zc::heap<WildcardPattern>(zc::mv(type));

  ZC_EXPECT(wildcard->getKind() == SyntaxKind::WildcardPattern);
  ZC_IF_SOME(ta, wildcard->getTypeAnnotation()) {
    ZC_EXPECT(ta.getKind() == SyntaxKind::I32TypeNode);
  }
  else { ZC_FAIL_EXPECT("expected type annotation to be present"); }
}

ZC_TEST("ExpressionTest.IdentifierPattern WithTypeAnnotation") {
  // Test identifier pattern with a type annotation using direct construction
  auto id = factory::createIdentifier("value"_zc);
  auto type = factory::createPredefinedType("f64"_zc);
  auto pattern = zc::heap<IdentifierPattern>(zc::mv(id), zc::mv(type));

  ZC_EXPECT(pattern->getKind() == SyntaxKind::IdentifierPattern);
  ZC_EXPECT(pattern->getIdentifier().getText() == "value"_zc);
  ZC_IF_SOME(ta, pattern->getTypeAnnotation()) {
    ZC_EXPECT(ta.getKind() == SyntaxKind::F64TypeNode);
  }
  else { ZC_FAIL_EXPECT("expected type annotation to be present"); }
}

ZC_TEST("ExpressionTest.IdentifierPattern WithoutTypeAnnotation") {
  // Test identifier pattern without a type annotation
  auto id = factory::createIdentifier("untyped"_zc);
  auto pattern = factory::createIdentifierPattern(zc::mv(id));

  ZC_EXPECT(pattern->getIdentifier().getText() == "untyped"_zc);
  ZC_EXPECT(pattern->getTypeAnnotation() == zc::none);
}

ZC_TEST("ExpressionTest.StructurePattern") {
  // Test structure pattern with pattern properties
  zc::Vector<zc::Own<Pattern>> fields;

  auto name1 = factory::createIdentifier("x"_zc);
  auto pat1 = factory::createIdentifierPattern(factory::createIdentifier("a"_zc));
  fields.add(factory::createPatternProperty(zc::mv(name1), zc::mv(pat1)));

  auto name2 = factory::createIdentifier("y"_zc);
  fields.add(factory::createPatternProperty(zc::mv(name2), zc::none));

  auto structPat = factory::createStructurePattern(zc::mv(fields));

  ZC_EXPECT(structPat->getKind() == SyntaxKind::StructurePattern);
  ZC_EXPECT(structPat->getProperties().size() == 2);
}

ZC_TEST("ExpressionTest.StructurePattern Empty") {
  // Test empty structure pattern
  zc::Vector<zc::Own<Pattern>> emptyFields;
  auto structPat = factory::createStructurePattern(zc::mv(emptyFields));

  ZC_EXPECT(structPat->getKind() == SyntaxKind::StructurePattern);
  ZC_EXPECT(structPat->getProperties().size() == 0);
}

ZC_TEST("ExpressionTest.PatternProperty WithPattern") {
  // Test pattern property with a sub-pattern
  auto name = factory::createIdentifier("field"_zc);
  auto subPat = factory::createWildcardPattern();
  auto prop = factory::createPatternProperty(zc::mv(name), zc::mv(subPat));

  ZC_EXPECT(prop->getKind() == SyntaxKind::PatternProperty);
  ZC_EXPECT(prop->getName().getText() == "field"_zc);
  ZC_IF_SOME(p, prop->getPattern()) { ZC_EXPECT(p.getKind() == SyntaxKind::WildcardPattern); }
  else { ZC_FAIL_EXPECT("expected pattern to be present"); }
}

ZC_TEST("ExpressionTest.PatternProperty WithoutPattern") {
  // Test pattern property without a sub-pattern
  auto name = factory::createIdentifier("bare"_zc);
  auto prop = factory::createPatternProperty(zc::mv(name), zc::none);

  ZC_EXPECT(prop->getName().getText() == "bare"_zc);
  ZC_EXPECT(prop->getPattern() == zc::none);
}

ZC_TEST("ExpressionTest.ArrayLiteralExpression MultiLine") {
  // Test multi-line array literal
  zc::Vector<zc::Own<Expression>> elements;
  elements.add(factory::createIntegerLiteral(1));
  elements.add(factory::createIntegerLiteral(2));
  auto arrayExpr = factory::createArrayLiteralExpression(zc::mv(elements), true);

  ZC_EXPECT(arrayExpr->getKind() == SyntaxKind::ArrayLiteralExpression);
  ZC_EXPECT(arrayExpr->getElements().size() == 2);
  ZC_EXPECT(arrayExpr->isMultiLine() == true);
  ZC_EXPECT(arrayExpr->getText() == "[]");
}

ZC_TEST("ExpressionTest.ArrayLiteralExpression SingleLine") {
  // Test single-line array literal (default)
  zc::Vector<zc::Own<Expression>> elements;
  elements.add(factory::createIntegerLiteral(1));
  auto arrayExpr = factory::createArrayLiteralExpression(zc::mv(elements), false);

  ZC_EXPECT(arrayExpr->isMultiLine() == false);
  ZC_EXPECT(arrayExpr->getText() == "[]");
}

ZC_TEST("ExpressionTest.ObjectLiteralExpression MultiLine") {
  // Test multi-line object literal
  zc::Vector<zc::Own<ObjectLiteralElement>> properties;
  auto key = factory::createIdentifier("key"_zc);
  auto val = factory::createStringLiteral("value"_zc);
  properties.add(factory::createPropertyAssignment(zc::mv(key), zc::mv(val)));

  auto objExpr = factory::createObjectLiteralExpression(zc::mv(properties), true);

  ZC_EXPECT(objExpr->getKind() == SyntaxKind::ObjectLiteralExpression);
  ZC_EXPECT(objExpr->getProperties().size() == 1);
  ZC_EXPECT(objExpr->isMultiLine() == true);
  ZC_EXPECT(objExpr->getText() == "{}");
}

ZC_TEST("ExpressionTest.ObjectLiteralExpression SingleLine") {
  // Test single-line object literal
  zc::Vector<zc::Own<ObjectLiteralElement>> emptyProps;
  auto objExpr = factory::createObjectLiteralExpression(zc::mv(emptyProps), false);

  ZC_EXPECT(objExpr->isMultiLine() == false);
  ZC_EXPECT(objExpr->getText() == "{}");
}

ZC_TEST("ExpressionTest.ConditionalExpression WithTokens") {
  // Test conditional expression with explicit question and colon tokens
  auto test = factory::createBooleanLiteral(true);
  auto questionToken = factory::createTokenNode(SyntaxKind::Question);
  auto consequent = factory::createIntegerLiteral(1);
  auto colonToken = factory::createTokenNode(SyntaxKind::Colon);
  auto alternate = factory::createIntegerLiteral(0);

  auto expr =
      factory::createConditionalExpression(zc::mv(test), zc::mv(questionToken), zc::mv(consequent),
                                           zc::mv(colonToken), zc::mv(alternate));

  ZC_EXPECT(expr->getKind() == SyntaxKind::ConditionalExpression);
  ZC_EXPECT(expr->getTest().getKind() == SyntaxKind::BooleanLiteral);
  ZC_EXPECT(expr->getConsequent().getKind() == SyntaxKind::IntegerLiteral);
  ZC_EXPECT(expr->getAlternate().getKind() == SyntaxKind::IntegerLiteral);
}

ZC_TEST("ExpressionTest.NewExpression WithTypeArguments") {
  // Test new expression with type arguments and arguments
  auto callee = factory::createIdentifier("Map"_zc);
  zc::Vector<zc::Own<TypeNode>> typeArgs;
  typeArgs.add(factory::createPredefinedType("str"_zc));
  typeArgs.add(factory::createPredefinedType("i32"_zc));

  zc::Vector<zc::Own<Expression>> args;
  auto newExpr = factory::createNewExpression(zc::mv(callee), zc::mv(typeArgs), zc::mv(args));

  ZC_EXPECT(newExpr->getKind() == SyntaxKind::NewExpression);
  ZC_EXPECT(newExpr->getCallee().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(newExpr->getArguments() != zc::none);
}

ZC_TEST("ExpressionTest.NewExpression Symbol") {
  // Test getSymbol/setSymbol on NewExpression (it implements Declaration)
  auto callee = factory::createIdentifier("Foo"_zc);
  auto newExpr = factory::createNewExpression(zc::mv(callee), zc::none, zc::none);

  // Initially symbol should be none
  ZC_EXPECT(newExpr->getSymbol() == zc::none);

  // Setting symbol to none should not crash
  newExpr->setSymbol(zc::none);
  ZC_EXPECT(newExpr->getSymbol() == zc::none);
}

ZC_TEST("ExpressionTest.ElementAccessExpression Symbol") {
  // Test getSymbol/setSymbol on ElementAccessExpression (it implements Declaration)
  auto obj = factory::createIdentifier("arr"_zc);
  auto idx = factory::createIntegerLiteral(0);
  auto elemAccess = factory::createElementAccessExpression(zc::mv(obj), zc::mv(idx));

  ZC_EXPECT(elemAccess->getSymbol() == zc::none);
  elemAccess->setSymbol(zc::none);
  ZC_EXPECT(elemAccess->getSymbol() == zc::none);
}

ZC_TEST("ExpressionTest.FunctionExpression WithCaptures") {
  // Test function expression with captures
  auto captureId = factory::createIdentifier("env"_zc);
  zc::Vector<zc::Own<CaptureElement>> captures;
  captures.add(factory::createCaptureElement(true, zc::mv(captureId), false));

  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto returnType = factory::createReturnType(factory::createPredefinedType("unit"_zc), zc::none);
  auto body = factory::createBlockStatement(zc::Vector<zc::Own<Statement>>());

  auto funcExpr = factory::createFunctionExpression(
      zc::mv(typeParams), zc::mv(params), zc::mv(captures), zc::mv(returnType), zc::mv(body));

  ZC_EXPECT(funcExpr->getKind() == SyntaxKind::FunctionExpression);
  ZC_EXPECT(funcExpr->getCaptures().size() == 1);
  ZC_EXPECT(funcExpr->getBody().getKind() == SyntaxKind::BlockStatement);
}

ZC_TEST("ExpressionTest.FunctionExpression WithTypeParamsAndReturn") {
  // Test function expression with type parameters and return type
  zc::Vector<zc::Own<TypeParameterDeclaration>> typeParams;
  typeParams.add(
      factory::createTypeParameterDeclaration(factory::createIdentifier("T"_zc), zc::none));

  zc::Vector<zc::Own<ParameterDeclaration>> params;
  auto paramName = factory::createIdentifier("x"_zc);
  auto paramType = factory::createTypeReference(factory::createIdentifier("T"_zc), zc::none);
  params.add(factory::createParameterDeclaration({}, zc::none, zc::mv(paramName), zc::none,
                                                 zc::mv(paramType), zc::none));

  auto retType = factory::createReturnType(
      factory::createTypeReference(factory::createIdentifier("T"_zc), zc::none), zc::none);
  auto body = factory::createBlockStatement(zc::Vector<zc::Own<Statement>>());

  auto funcExpr = factory::createFunctionExpression(zc::mv(typeParams), zc::mv(params), {},
                                                    zc::mv(retType), zc::mv(body));

  ZC_EXPECT(ZC_ASSERT_NONNULL(funcExpr->getTypeParameters()).size() == 1);
  ZC_EXPECT(funcExpr->getParameters().size() == 1);
  ZC_IF_SOME(rt, funcExpr->getReturnType()) {
    ZC_EXPECT(rt.getKind() == SyntaxKind::ReturnTypeNode);
  }
  else { ZC_FAIL_EXPECT("expected return type to be present"); }
}

ZC_TEST("ExpressionTest.AsExpression Getters") {
  // Test AsExpression getExpression() and getTargetType() getters
  auto expression = factory::createIdentifier("value"_zc);
  auto targetType = factory::createPredefinedType("str"_zc);
  auto expr = factory::createAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::AsExpression);
  ZC_EXPECT(expr->getExpression().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(expr->getTargetType().getKind() == SyntaxKind::StrTypeNode);
}

ZC_TEST("ExpressionTest.ForcedAsExpression Getters") {
  // Test ForcedAsExpression getters
  auto expression = factory::createIdentifier("obj"_zc);
  auto targetType = factory::createTypeReference(factory::createIdentifier("MyClass"_zc), zc::none);
  auto expr = factory::createForcedAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::ForcedAsExpression);
  ZC_EXPECT(expr->getExpression().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(expr->getTargetType().getKind() == SyntaxKind::TypeReferenceNode);
}

ZC_TEST("ExpressionTest.ConditionalAsExpression Getters") {
  // Test ConditionalAsExpression getters
  auto expression = factory::createIdentifier("maybe"_zc);
  auto targetType = factory::createPredefinedType("bool"_zc);
  auto expr = factory::createConditionalAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::ConditionalAsExpression);
  ZC_EXPECT(expr->getExpression().getKind() == SyntaxKind::Identifier);
  ZC_EXPECT(expr->getTargetType().getKind() == SyntaxKind::BoolTypeNode);
}

ZC_TEST("ExpressionTest.VoidExpression Getter") {
  // Test VoidExpression getExpression() getter
  auto inner = factory::createIdentifier("x"_zc);
  auto expr = factory::createVoidExpression(zc::mv(inner));

  ZC_EXPECT(expr->getKind() == SyntaxKind::VoidExpression);
  ZC_EXPECT(expr->getExpression().getKind() == SyntaxKind::Identifier);
}

ZC_TEST("ExpressionTest.TypeOfExpression Getter") {
  // Test TypeOfExpression getExpression() getter
  auto inner = factory::createStringLiteral("hello"_zc);
  auto expr = factory::createTypeOfExpression(zc::mv(inner));

  ZC_EXPECT(expr->getKind() == SyntaxKind::TypeOfExpression);
  ZC_EXPECT(expr->getExpression().getKind() == SyntaxKind::StringLiteral);
}

ZC_TEST("ExpressionTest.AwaitExpression Getter") {
  // Test AwaitExpression getExpression() getter
  auto inner = factory::createIdentifier("promise"_zc);
  auto expr = factory::createAwaitExpression(zc::mv(inner));

  ZC_EXPECT(expr->getKind() == SyntaxKind::AwaitExpression);
  ZC_EXPECT(expr->getExpression().getKind() == SyntaxKind::Identifier);
}

ZC_TEST("ExpressionTest.EnumPattern WithoutTuplePattern") {
  // Test enum pattern without providing a tuple pattern (defaults to empty)
  auto typeRef = factory::createTypeReference(factory::createIdentifier("Color"_zc), zc::none);
  auto propertyName = factory::createIdentifier("Red"_zc);
  auto enumPattern = factory::createEnumPattern(zc::mv(typeRef), zc::mv(propertyName), zc::none);

  ZC_EXPECT(enumPattern->getKind() == SyntaxKind::EnumPattern);
  ZC_EXPECT(enumPattern->getPropertyName().getText() == "Red");
  // The factory creates an empty TuplePattern when none is provided
  ZC_EXPECT(enumPattern->getTuplePattern().getElements().size() == 0);
  ZC_IF_SOME(tr, enumPattern->getTypeReference()) {
    ZC_EXPECT(tr.getKind() == SyntaxKind::TypeReferenceNode);
  }
  else { ZC_FAIL_EXPECT("expected type reference to be present"); }
}

ZC_TEST("ExpressionTest.ObjectLiteralExpression WithMixedProperties") {
  // Test object literal with property assignment, shorthand, and spread
  zc::Vector<zc::Own<ObjectLiteralElement>> properties;

  // Regular property assignment
  auto name1 = factory::createIdentifier("name"_zc);
  auto val1 = factory::createStringLiteral("Alice"_zc);
  properties.add(factory::createPropertyAssignment(zc::mv(name1), zc::mv(val1)));

  // Shorthand property assignment
  auto name2 = factory::createIdentifier("age"_zc);
  properties.add(factory::createShorthandPropertyAssignment(zc::mv(name2), zc::none, zc::none));

  // Spread assignment
  auto spreadExpr = factory::createIdentifier("other"_zc);
  properties.add(factory::createSpreadAssignment(zc::mv(spreadExpr)));

  auto objExpr = factory::createObjectLiteralExpression(zc::mv(properties));

  ZC_EXPECT(objExpr->getProperties().size() == 3);
}

ZC_TEST("ExpressionTest.ArrayLiteralExpression WithSpreadElements") {
  // Test array literal with spread elements
  zc::Vector<zc::Own<Expression>> elements;
  elements.add(factory::createIntegerLiteral(1));
  elements.add(factory::createSpreadElement(factory::createIdentifier("rest"_zc)));
  elements.add(factory::createIntegerLiteral(3));

  auto arrayExpr = factory::createArrayLiteralExpression(zc::mv(elements));

  ZC_EXPECT(arrayExpr->getElements().size() == 3);
  ZC_EXPECT(arrayExpr->getElements()[0].getKind() == SyntaxKind::IntegerLiteral);
  ZC_EXPECT(arrayExpr->getElements()[1].getKind() == SyntaxKind::SpreadElement);
  ZC_EXPECT(arrayExpr->getElements()[2].getKind() == SyntaxKind::IntegerLiteral);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
