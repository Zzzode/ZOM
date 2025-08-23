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
#include "zomlang/compiler/ast/type.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("ExpressionTest.BinaryExpressionCreation") {
  auto left = factory::createFloatLiteral(5.0);
  auto right = factory::createFloatLiteral(3.0);
  auto op = factory::createAddOperator();
  auto expr = factory::createBinaryExpression(zc::mv(left), zc::mv(op), zc::mv(right));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kBinaryExpression);
  ZC_EXPECT(expr->isExpression());

  ZC_EXPECT(expr->getLeft().getKind() == SyntaxKind::kFloatLiteral);
  ZC_EXPECT(expr->getOperator().getSymbol() == "+");
}

ZC_TEST("ExpressionTest.UnaryExpression") {
  auto operand = factory::createFloatLiteral(10.0);
  auto op = factory::createLogicalNotOperator();
  auto expr = factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kPrefixUnaryExpression);
  ZC_EXPECT(expr->getOperand().getKind() == SyntaxKind::kFloatLiteral);
}

ZC_TEST("ExpressionTest.AssignmentExpression") {
  auto lhs = factory::createIdentifier(zc::str("x"));
  auto rhs = factory::createFloatLiteral(42.0);
  auto op = factory::createAssignOperator();
  auto expr = factory::createAssignmentExpression(zc::mv(lhs), zc::mv(op), zc::mv(rhs));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kAssignmentExpression);
}

ZC_TEST("ExpressionTest.LiteralExpressions") {
  auto strLit = factory::createStringLiteral(zc::str("test"));
  ZC_EXPECT(strLit->getValue() == "test");

  auto numLit = factory::createFloatLiteral(3.14);
  ZC_EXPECT(numLit->getValue() == 3.14);

  auto boolLit = factory::createBooleanLiteral(true);
  ZC_EXPECT(boolLit->getValue() == true);

  auto nullLit = factory::createNullLiteral();
  ZC_EXPECT(nullLit->getKind() == SyntaxKind::kNullLiteral);
}

ZC_TEST("ExpressionTest.CallExpression") {
  auto callee = factory::createIdentifier(zc::str("func"));
  zc::Vector<zc::Own<Expression>> args;
  args.add(factory::createIntegerLiteral(1));
  args.add(factory::createIntegerLiteral(2));
  auto expr = factory::createCallExpression(zc::mv(callee), zc::mv(args));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kCallExpression);
  ZC_EXPECT(expr->getArguments().size() == 2);
}

ZC_TEST("ExpressionTest.PostfixUnaryExpression") {
  // Test postfix increment
  auto operand1 = factory::createIdentifier(zc::str("x"));
  auto op1 = factory::createPostIncrementOperator();
  auto expr1 = factory::createPostfixUnaryExpression(zc::mv(op1), zc::mv(operand1));

  ZC_EXPECT(expr1->getKind() == SyntaxKind::kPostfixUnaryExpression);
  ZC_EXPECT(expr1->getOperator().getSymbol() == "++");
  ZC_EXPECT(!expr1->getOperator().isPrefix());

  // Test postfix decrement
  auto operand2 = factory::createIdentifier(zc::str("y"));
  auto op2 = factory::createPostDecrementOperator();
  auto expr2 = factory::createPostfixUnaryExpression(zc::mv(op2), zc::mv(operand2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kPostfixUnaryExpression);
  ZC_EXPECT(expr2->getOperator().getSymbol() == "--");
  ZC_EXPECT(!expr2->getOperator().isPrefix());
}

ZC_TEST("ExpressionTest.NewExpression") {
  // Test new expression with arguments
  auto callee1 = factory::createIdentifier(zc::str("MyClass"));
  zc::Vector<zc::Own<Expression>> args1;
  args1.add(factory::createIntegerLiteral(42));
  args1.add(factory::createStringLiteral(zc::str("test")));
  auto expr1 = factory::createNewExpression(zc::mv(callee1), zc::mv(args1));

  ZC_EXPECT(expr1->getKind() == SyntaxKind::kNewExpression);
  ZC_EXPECT(expr1->getArguments().size() == 2);

  // Test new expression without arguments
  auto callee2 = factory::createIdentifier(zc::str("EmptyClass"));
  zc::Vector<zc::Own<Expression>> args2;
  auto expr2 = factory::createNewExpression(zc::mv(callee2), zc::mv(args2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kNewExpression);
  ZC_EXPECT(expr2->getArguments().size() == 0);
}

ZC_TEST("ExpressionTest.ConditionalExpression") {
  // Test ternary operator: condition ? consequent : alternate
  auto test = factory::createBooleanLiteral(true);
  auto consequent = factory::createStringLiteral(zc::str("yes"));
  auto alternate = factory::createStringLiteral(zc::str("no"));
  auto expr =
      factory::createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kConditionalExpression);

  // Test with complex condition
  auto test2 = factory::createBinaryExpression(factory::createIntegerLiteral(5),
                                               factory::createGreaterOperator(),
                                               factory::createIntegerLiteral(3));
  auto consequent2 = factory::createIntegerLiteral(100);
  auto alternate2 = factory::createIntegerLiteral(0);
  auto expr2 =
      factory::createConditionalExpression(zc::mv(test2), zc::mv(consequent2), zc::mv(alternate2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kConditionalExpression);
}

ZC_TEST("ExpressionTest.AsExpression") {
  // Test type conversion expression
  auto expression = factory::createIntegerLiteral(42);
  auto targetType = factory::createPredefinedType(zc::str("str"));
  auto expr = factory::createAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kAsExpression);

  // Test with custom type
  auto expression2 = factory::createIdentifier(zc::str("obj"));
  auto targetType2 =
      factory::createTypeReference(factory::createIdentifier(zc::str("MyClass")), zc::none);
  auto expr2 = factory::createAsExpression(zc::mv(expression2), zc::mv(targetType2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kAsExpression);
}

ZC_TEST("ExpressionTest.ForcedAsExpression") {
  // Test forced type conversion expression
  auto expression = factory::createIdentifier(zc::str("value"));
  auto targetType = factory::createPredefinedType(zc::str("i32"));
  auto expr = factory::createForcedAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kForcedAsExpression);
}

ZC_TEST("ExpressionTest.ConditionalAsExpression") {
  // Test conditional type conversion expression
  auto expression = factory::createIdentifier(zc::str("maybeValue"));
  auto targetType = factory::createPredefinedType(zc::str("bool"));
  auto expr = factory::createConditionalAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kConditionalAsExpression);
}

ZC_TEST("ExpressionTest.VoidExpression") {
  // Test void expression
  auto expression = factory::createCallExpression(factory::createIdentifier(zc::str("sideEffect")),
                                                  zc::Vector<zc::Own<Expression>>());
  auto expr = factory::createVoidExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kVoidExpression);
}

ZC_TEST("ExpressionTest.TypeOfExpression") {
  // Test typeof expression with identifier
  auto expression = factory::createIdentifier(zc::str("variable"));
  auto expr = factory::createTypeOfExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kTypeOfExpression);

  // Test typeof expression with literal
  auto expression2 = factory::createStringLiteral(zc::str("hello"));
  auto expr2 = factory::createTypeOfExpression(zc::mv(expression2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kTypeOfExpression);
}

ZC_TEST("ExpressionTest.AwaitExpression") {
  // Test await expression with function call
  auto expression = factory::createCallExpression(
      factory::createIdentifier(zc::str("asyncFunction")), zc::Vector<zc::Own<Expression>>());
  auto expr = factory::createAwaitExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kAwaitExpression);

  // Test await expression with identifier
  auto expression2 = factory::createIdentifier(zc::str("promise"));
  auto expr2 = factory::createAwaitExpression(zc::mv(expression2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kAwaitExpression);
}

ZC_TEST("ExpressionTest.OptionalExpression") {
  // Test optional chaining with property access
  auto object = factory::createIdentifier(zc::str("obj"));
  auto property = factory::createIdentifier(zc::str("prop"));
  auto expr = factory::createOptionalExpression(zc::mv(object), zc::mv(property));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kOptionalExpression);

  // Test optional chaining with method call
  auto object2 = factory::createIdentifier(zc::str("service"));
  auto methodCall = factory::createCallExpression(factory::createIdentifier(zc::str("getData")),
                                                  zc::Vector<zc::Own<Expression>>());
  auto expr2 = factory::createOptionalExpression(zc::mv(object2), zc::mv(methodCall));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kOptionalExpression);
}

ZC_TEST("ExpressionTest.ElementAccessExpression") {
  // Test basic array access
  auto arrayExpr = factory::createIdentifier(zc::str("arr"));
  auto indexExpr = factory::createIntegerLiteral(0);
  auto elementAccess = factory::createElementAccessExpression(zc::mv(arrayExpr), zc::mv(indexExpr));

  ZC_EXPECT(elementAccess->getKind() == SyntaxKind::kElementAccessExpression);
  ZC_EXPECT(elementAccess->isQuestionDot() == false);

  // Test object property access with string key
  auto objExpr = factory::createIdentifier(zc::str("obj"));
  auto propIndexExpr = factory::createStringLiteral(zc::str("prop"));
  auto propElementAccess =
      factory::createElementAccessExpression(zc::mv(objExpr), zc::mv(propIndexExpr));

  ZC_EXPECT(propElementAccess->getKind() == SyntaxKind::kElementAccessExpression);

  // Test with questionDot = true
  auto maybeArrExpr = factory::createIdentifier(zc::str("maybeArr"));
  auto keyExpr = factory::createStringLiteral(zc::str("key"));
  auto optionalElementAccess =
      factory::createElementAccessExpression(zc::mv(maybeArrExpr), zc::mv(keyExpr), true);

  ZC_EXPECT(optionalElementAccess->getKind() == SyntaxKind::kElementAccessExpression);
  ZC_EXPECT(optionalElementAccess->isQuestionDot() == true);
}

ZC_TEST("ExpressionTest.PropertyAccessExpression") {
  // Test regular property access
  auto objExpr = factory::createIdentifier(zc::str("obj"));
  auto propName = factory::createIdentifier(zc::str("prop"));
  auto propertyAccess =
      factory::createPropertyAccessExpression(zc::mv(objExpr), zc::mv(propName), false);

  ZC_EXPECT(propertyAccess->getKind() == SyntaxKind::kPropertyAccessExpression);
  ZC_EXPECT(propertyAccess->isQuestionDot() == false);

  // Test optional property access
  auto optionalObjExpr = factory::createIdentifier(zc::str("obj"));
  auto optionalPropName = factory::createIdentifier(zc::str("prop"));
  auto optionalPropertyAccess = factory::createPropertyAccessExpression(
      zc::mv(optionalObjExpr), zc::mv(optionalPropName), true);

  ZC_EXPECT(optionalPropertyAccess->getKind() == SyntaxKind::kPropertyAccessExpression);
  ZC_EXPECT(optionalPropertyAccess->isQuestionDot() == true);
}

ZC_TEST("ExpressionTest.Identifier") {
  // Test basic identifier
  auto identifier = factory::createIdentifier(zc::str("variableName"));

  ZC_EXPECT(identifier->getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(identifier->getName() == "variableName");

  // Test identifier with underscore
  auto underscoreId = factory::createIdentifier(zc::str("_privateVar"));
  ZC_EXPECT(underscoreId->getKind() == SyntaxKind::kIdentifier);

  // Test identifier with numbers
  auto numberedId = factory::createIdentifier(zc::str("var123"));
  ZC_EXPECT(numberedId->getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(numberedId->getName() == "var123");
}

ZC_TEST("ExpressionTest.GetterMethods") {
  // Test prefix unary expression getters
  auto operand = factory::createIdentifier(zc::str("x"));
  auto op = factory::createPreIncrementOperator();
  auto prefixExpr = factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));

  ZC_EXPECT(prefixExpr->getOperator().getKind() == SyntaxKind::kOperator);
  ZC_EXPECT(prefixExpr->getOperand().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(prefixExpr->isPrefix() == true);

  // Test postfix unary expression getters
  auto postfixOperand = factory::createIdentifier(zc::str("y"));
  auto postfixOp = factory::createPostIncrementOperator();
  auto postfixExpr =
      factory::createPostfixUnaryExpression(zc::mv(postfixOp), zc::mv(postfixOperand));

  ZC_EXPECT(postfixExpr->getOperator().getKind() == SyntaxKind::kOperator);
  ZC_EXPECT(postfixExpr->getOperand().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(postfixExpr->isPrefix() == false);

  // Test conditional expression getters
  auto test = factory::createIdentifier(zc::str("condition"));
  auto consequent = factory::createIdentifier(zc::str("trueValue"));
  auto alternate = factory::createIdentifier(zc::str("falseValue"));
  auto conditionalExpr =
      factory::createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));

  ZC_EXPECT(conditionalExpr->getTest().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(conditionalExpr->getConsequent().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(conditionalExpr->getAlternate().getKind() == SyntaxKind::kIdentifier);

  // Test call expression getters
  auto callee = factory::createIdentifier(zc::str("func"));
  auto callExpr = factory::createCallExpression(zc::mv(callee), {});

  ZC_EXPECT(callExpr->getCallee().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(callExpr->getArguments().size() == 0);
}

ZC_TEST("ExpressionTest.ParenthesizedExpression") {
  auto innerExpr = factory::createFloatLiteral(42.0);
  auto parenExpr = factory::createParenthesizedExpression(zc::mv(innerExpr));

  ZC_EXPECT(parenExpr->getKind() == SyntaxKind::kParenthesizedExpression);
  ZC_EXPECT(parenExpr->getExpression().getKind() == SyntaxKind::kFloatLiteral);

  // Test with complex nested expression
  auto left = factory::createFloatLiteral(1.0);
  auto right = factory::createFloatLiteral(2.0);
  auto op = factory::createAddOperator();
  auto binaryExpr = factory::createBinaryExpression(zc::mv(left), zc::mv(op), zc::mv(right));
  auto nestedParenExpr = factory::createParenthesizedExpression(zc::mv(binaryExpr));

  ZC_EXPECT(nestedParenExpr->getKind() == SyntaxKind::kParenthesizedExpression);
  ZC_EXPECT(nestedParenExpr->getExpression().getKind() == SyntaxKind::kBinaryExpression);
}

ZC_TEST("ExpressionTest.FunctionExpression") {
  // Test simple function expression without type parameters
  zc::Vector<zc::Own<TypeParameter>> typeParams;
  zc::Vector<zc::Own<BindingElement>> params;
  auto paramName = factory::createIdentifier(zc::str("x"));
  auto paramType = factory::createPredefinedType(zc::str("i32"));
  auto param = factory::createBindingElement(zc::mv(paramName), zc::mv(paramType), zc::none);
  params.add(zc::mv(param));

  auto returnTypeInner = factory::createPredefinedType(zc::str("i32"));
  auto returnType = factory::createReturnType(zc::mv(returnTypeInner), zc::none);
  auto body = factory::createBlockStatement(zc::Vector<zc::Own<Statement>>());

  auto funcExpr = factory::createFunctionExpression(zc::mv(typeParams), zc::mv(params),
                                                    zc::mv(returnType), zc::mv(body));

  ZC_EXPECT(funcExpr->getKind() == SyntaxKind::kFunctionExpression);
  ZC_EXPECT(funcExpr->getParameters().size() == 1);
  ZC_EXPECT(funcExpr->getReturnType() != zc::none);
  ZC_EXPECT(funcExpr->getBody().getKind() == SyntaxKind::kBlockStatement);

  // Test function expression with type parameters
  zc::Vector<zc::Own<TypeParameter>> typeParamsWithGeneric;
  auto typeParam =
      factory::createTypeParameterDeclaration(factory::createIdentifier(zc::str("T")), zc::none);
  typeParamsWithGeneric.add(zc::mv(typeParam));

  zc::Vector<zc::Own<BindingElement>> genericParams;
  auto genericParamName = factory::createIdentifier(zc::str("value"));
  auto genericParamType =
      factory::createTypeReference(factory::createIdentifier(zc::str("T")), zc::none);
  auto genericParam =
      factory::createBindingElement(zc::mv(genericParamName), zc::mv(genericParamType), zc::none);
  genericParams.add(zc::mv(genericParam));

  auto genericReturnTypeInner =
      factory::createTypeReference(factory::createIdentifier(zc::str("T")), zc::none);
  auto genericReturnType = factory::createReturnType(zc::mv(genericReturnTypeInner), zc::none);
  auto genericBody = factory::createBlockStatement(zc::Vector<zc::Own<Statement>>());

  auto genericFuncExpr =
      factory::createFunctionExpression(zc::mv(typeParamsWithGeneric), zc::mv(genericParams),
                                        zc::mv(genericReturnType), zc::mv(genericBody));

  ZC_EXPECT(genericFuncExpr->getKind() == SyntaxKind::kFunctionExpression);
  ZC_EXPECT(genericFuncExpr->getTypeParameters().size() == 1);
  ZC_EXPECT(genericFuncExpr->getParameters().size() == 1);
}

ZC_TEST("ExpressionTest.ArrayLiteralExpression") {
  // Test empty array literal
  zc::Vector<zc::Own<Expression>> emptyElements;
  auto emptyArray = factory::createArrayLiteralExpression(zc::mv(emptyElements));

  ZC_EXPECT(emptyArray->getKind() == SyntaxKind::kArrayLiteralExpression);
  ZC_EXPECT(emptyArray->getElements().size() == 0);

  // Test array literal with elements
  zc::Vector<zc::Own<Expression>> elements;
  elements.add(factory::createIntegerLiteral(1));
  elements.add(factory::createIntegerLiteral(2));
  elements.add(factory::createIntegerLiteral(3));
  auto arrayExpr = factory::createArrayLiteralExpression(zc::mv(elements));

  ZC_EXPECT(arrayExpr->getKind() == SyntaxKind::kArrayLiteralExpression);
  ZC_EXPECT(arrayExpr->getElements().size() == 3);

  // Test array literal with mixed types
  zc::Vector<zc::Own<Expression>> mixedElements;
  mixedElements.add(factory::createStringLiteral(zc::str("hello")));
  mixedElements.add(factory::createFloatLiteral(3.14));
  mixedElements.add(factory::createBooleanLiteral(true));
  auto mixedArray = factory::createArrayLiteralExpression(zc::mv(mixedElements));

  ZC_EXPECT(mixedArray->getKind() == SyntaxKind::kArrayLiteralExpression);
  ZC_EXPECT(mixedArray->getElements().size() == 3);
}

ZC_TEST("ExpressionTest.ObjectLiteralExpression") {
  // Test empty object literal
  zc::Vector<zc::Own<Expression>> emptyProperties;
  auto emptyObject = factory::createObjectLiteralExpression(zc::mv(emptyProperties));

  ZC_EXPECT(emptyObject->getKind() == SyntaxKind::kObjectLiteralExpression);
  ZC_EXPECT(emptyObject->getProperties().size() == 0);

  // Test object literal with properties
  zc::Vector<zc::Own<Expression>> properties;
  properties.add(factory::createStringLiteral(zc::str("name")));
  properties.add(factory::createStringLiteral(zc::str("Alice")));
  properties.add(factory::createStringLiteral(zc::str("age")));
  properties.add(factory::createIntegerLiteral(30));
  auto objectExpr = factory::createObjectLiteralExpression(zc::mv(properties));

  ZC_EXPECT(objectExpr->getKind() == SyntaxKind::kObjectLiteralExpression);
  ZC_EXPECT(objectExpr->getProperties().size() == 4);
}

ZC_TEST("ExpressionTest.AcceptMethods") {
  // Test that all expression types have proper accept methods
  // This is a basic test to ensure the visitor pattern works

  auto prefixOperand = factory::createIdentifier(zc::str("x"));
  auto prefixOp = factory::createPreIncrementOperator();
  auto prefixExpr = factory::createPrefixUnaryExpression(zc::mv(prefixOp), zc::mv(prefixOperand));
  ZC_EXPECT(prefixExpr->getKind() == SyntaxKind::kPrefixUnaryExpression);

  // Test postfix unary expression
  auto postfixOperand = factory::createIdentifier(zc::str("y"));
  auto postfixOp = factory::createPostIncrementOperator();
  auto postfixExpr =
      factory::createPostfixUnaryExpression(zc::mv(postfixOp), zc::mv(postfixOperand));
  ZC_EXPECT(postfixExpr->getKind() == SyntaxKind::kPostfixUnaryExpression);

  // Test conditional expression
  auto test = factory::createIdentifier(zc::str("condition"));
  auto consequent = factory::createIdentifier(zc::str("trueValue"));
  auto alternate = factory::createIdentifier(zc::str("falseValue"));
  auto conditionalExpr =
      factory::createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));
  ZC_EXPECT(conditionalExpr->getKind() == SyntaxKind::kConditionalExpression);

  // Test call expression
  auto callee = factory::createIdentifier(zc::str("func"));
  auto callExpr = factory::createCallExpression(zc::mv(callee), {});
  ZC_EXPECT(callExpr->getKind() == SyntaxKind::kCallExpression);

  // Test element access expression
  auto object = factory::createIdentifier(zc::str("arr"));
  auto index = factory::createIntegerLiteral(0);
  auto elementExpr = factory::createElementAccessExpression(zc::mv(object), zc::mv(index));
  ZC_EXPECT(elementExpr->getKind() == SyntaxKind::kElementAccessExpression);

  // Test property access expression
  auto propObject = factory::createIdentifier(zc::str("obj"));
  auto propName = factory::createIdentifier(zc::str("prop"));
  auto propExpr =
      factory::createPropertyAccessExpression(zc::mv(propObject), zc::mv(propName), false);
  ZC_EXPECT(propExpr->getKind() == SyntaxKind::kPropertyAccessExpression);

  // Test ParenthesizedExpression accept method
  auto parenExpr = factory::createParenthesizedExpression(factory::createFloatLiteral(42.0));
  ZC_EXPECT(parenExpr->getKind() == SyntaxKind::kParenthesizedExpression);
}

ZC_TEST("ExpressionTest.WildcardPattern") {
  // Test basic wildcard pattern creation
  auto wildcardPattern = factory::createWildcardPattern();

  ZC_EXPECT(wildcardPattern->getKind() == SyntaxKind::kWildcardPattern);
}

ZC_TEST("ExpressionTest.IdentifierPattern") {
  // Test identifier pattern creation
  auto identifier = factory::createIdentifier(zc::str("variableName"));
  auto identifierPattern = factory::createIdentifierPattern(zc::mv(identifier));

  ZC_EXPECT(identifierPattern->getKind() == SyntaxKind::kIdentifierPattern);
  ZC_EXPECT(identifierPattern->getIdentifier().getName() == "variableName");

  // Test with different identifier names
  auto identifier2 = factory::createIdentifier(zc::str("_underscore"));
  auto identifierPattern2 = factory::createIdentifierPattern(zc::mv(identifier2));
  ZC_EXPECT(identifierPattern2->getIdentifier().getName() == "_underscore");
}

ZC_TEST("ExpressionTest.TuplePattern") {
  // Test empty tuple pattern
  zc::Vector<zc::Own<Pattern>> emptyElements;
  auto emptyTuplePattern = factory::createTuplePattern(zc::mv(emptyElements));

  ZC_EXPECT(emptyTuplePattern->getKind() == SyntaxKind::kTuplePattern);
  ZC_EXPECT(emptyTuplePattern->getElements().size() == 0);

  // Test tuple pattern with mixed elements
  zc::Vector<zc::Own<Pattern>> elements;
  auto identifier = factory::createIdentifier(zc::str("x"));
  auto identifierPattern = factory::createIdentifierPattern(zc::mv(identifier));
  auto wildcardPattern = factory::createWildcardPattern();

  elements.add(zc::mv(identifierPattern));
  elements.add(zc::mv(wildcardPattern));

  auto tuplePattern = factory::createTuplePattern(zc::mv(elements));
  ZC_EXPECT(tuplePattern->getKind() == SyntaxKind::kTuplePattern);
  ZC_EXPECT(tuplePattern->getElements().size() == 2);
}

ZC_TEST("ExpressionTest.ArrayPattern") {
  // Test empty array pattern
  zc::Vector<zc::Own<Pattern>> emptyElements;
  auto emptyArrayPattern = factory::createArrayPattern(zc::mv(emptyElements));

  ZC_EXPECT(emptyArrayPattern->getKind() == SyntaxKind::kArrayPattern);
  ZC_EXPECT(emptyArrayPattern->getElements().size() == 0);

  // Test array pattern with elements
  zc::Vector<zc::Own<Pattern>> elements;
  auto identifier1 = factory::createIdentifier(zc::str("first"));
  auto pattern1 = factory::createIdentifierPattern(zc::mv(identifier1));
  auto identifier2 = factory::createIdentifier(zc::str("second"));
  auto pattern2 = factory::createIdentifierPattern(zc::mv(identifier2));

  elements.add(zc::mv(pattern1));
  elements.add(zc::mv(pattern2));

  auto arrayPattern = factory::createArrayPattern(zc::mv(elements));
  ZC_EXPECT(arrayPattern->getKind() == SyntaxKind::kArrayPattern);
  ZC_EXPECT(arrayPattern->getElements().size() == 2);
}

ZC_TEST("ExpressionTest.ExpressionPattern") {
  // Test expression pattern with literal
  auto literal = factory::createIntegerLiteral(42);
  auto expressionPattern = factory::createExpressionPattern(zc::mv(literal));

  ZC_EXPECT(expressionPattern->getKind() == SyntaxKind::kExpressionPattern);
  ZC_EXPECT(expressionPattern->getExpression().getKind() == SyntaxKind::kIntegerLiteral);

  // Test expression pattern with identifier
  auto identifier = factory::createIdentifier(zc::str("constant"));
  auto expressionPattern2 = factory::createExpressionPattern(zc::mv(identifier));
  ZC_EXPECT(expressionPattern2->getExpression().getKind() == SyntaxKind::kIdentifier);
}

ZC_TEST("ExpressionTest.IsPattern") {
  // Test is pattern with predefined type
  auto type = factory::createPredefinedType(zc::str("i32"));
  auto wildcardPattern = factory::createWildcardPattern();
  auto isPattern = factory::createIsPattern(zc::mv(wildcardPattern), zc::mv(type));

  ZC_EXPECT(isPattern->getKind() == SyntaxKind::kIsPattern);
  ZC_EXPECT(isPattern->getType().getKind() == SyntaxKind::kPredefinedType);
}

ZC_TEST("ExpressionTest.EnumPattern") {
  // Test enum pattern
  auto typeRef =
      factory::createTypeReference(factory::createIdentifier(zc::str("MyEnum")), zc::none);
  auto propertyName = factory::createIdentifier(zc::str("Variant"));

  // Create tuple pattern for enum variant
  zc::Vector<zc::Own<Pattern>> tupleElements;
  auto identifier = factory::createIdentifier(zc::str("value"));
  auto identifierPattern = factory::createIdentifierPattern(zc::mv(identifier));
  tupleElements.add(zc::mv(identifierPattern));
  auto tuplePattern = factory::createTuplePattern(zc::mv(tupleElements));

  auto enumPattern =
      factory::createEnumPattern(zc::mv(typeRef), zc::mv(propertyName), zc::mv(tuplePattern));

  ZC_EXPECT(enumPattern->getKind() == SyntaxKind::kEnumPattern);
  ZC_EXPECT(enumPattern->getPropertyName().getName() == "Variant");
  ZC_EXPECT(enumPattern->getTuplePattern().getElements().size() == 1);

  // Test enum pattern without type reference
  auto propertyName2 = factory::createIdentifier(zc::str("SimpleVariant"));
  zc::Vector<zc::Own<Pattern>> emptyTupleElements;
  auto emptyTuplePattern = factory::createTuplePattern(zc::mv(emptyTupleElements));
  auto typeRef2 =
      factory::createTypeReference(factory::createIdentifier(zc::str("SimpleEnum")), zc::none);
  auto enumPattern2 = factory::createEnumPattern(zc::mv(typeRef2), zc::mv(propertyName2),
                                                 zc::mv(emptyTuplePattern));

  ZC_EXPECT(enumPattern2->getKind() == SyntaxKind::kEnumPattern);
  ZC_EXPECT(enumPattern2->getPropertyName().getName() == "SimpleVariant");
  ZC_EXPECT(enumPattern2->getTuplePattern().getElements().size() == 0);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
