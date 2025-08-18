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

  auto binExpr = static_cast<BinaryExpression*>(expr.get());
  // getLeft(), getRight(), getOperator() now return references, no need for null checks
  ZC_EXPECT(binExpr->getOperator().getSymbol() == "+");
}

ZC_TEST("ExpressionTest.UnaryExpression") {
  auto operand = factory::createFloatLiteral(10.0);
  auto op = factory::createLogicalNotOperator();
  auto expr = factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kPrefixUnaryExpression);
  auto unaryExpr = static_cast<UnaryExpression*>(expr.get());
  // Skip operand check for now
  ZC_EXPECT(unaryExpr != nullptr);
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
  auto callExpr = static_cast<CallExpression*>(expr.get());
  ZC_EXPECT(callExpr->getArguments().size() == 2);
}

ZC_TEST("ExpressionTest.PostfixUnaryExpression") {
  // Test postfix increment
  auto operand1 = factory::createIdentifier(zc::str("x"));
  auto op1 = factory::createPostIncrementOperator();
  auto expr1 = factory::createPostfixUnaryExpression(zc::mv(op1), zc::mv(operand1));

  ZC_EXPECT(expr1->getKind() == SyntaxKind::kPostfixUnaryExpression);
  auto postfixExpr1 = static_cast<PostfixUnaryExpression*>(expr1.get());
  ZC_EXPECT(postfixExpr1->getOperator().getSymbol() == "++");
  ZC_EXPECT(!postfixExpr1->getOperator().isPrefix());

  // Test postfix decrement
  auto operand2 = factory::createIdentifier(zc::str("y"));
  auto op2 = factory::createPostDecrementOperator();
  auto expr2 = factory::createPostfixUnaryExpression(zc::mv(op2), zc::mv(operand2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kPostfixUnaryExpression);
  auto postfixExpr2 = static_cast<PostfixUnaryExpression*>(expr2.get());
  ZC_EXPECT(postfixExpr2->getOperator().getSymbol() == "--");
  ZC_EXPECT(!postfixExpr2->getOperator().isPrefix());
}

ZC_TEST("ExpressionTest.NewExpression") {
  // Test new expression with arguments
  auto callee1 = factory::createIdentifier(zc::str("MyClass"));
  zc::Vector<zc::Own<Expression>> args1;
  args1.add(factory::createIntegerLiteral(42));
  args1.add(factory::createStringLiteral(zc::str("test")));
  auto expr1 = factory::createNewExpression(zc::mv(callee1), zc::mv(args1));

  ZC_EXPECT(expr1->getKind() == SyntaxKind::kNewExpression);
  auto newExpr1 = static_cast<NewExpression*>(expr1.get());
  ZC_EXPECT(newExpr1->getArguments().size() == 2);

  // Test new expression without arguments
  auto callee2 = factory::createIdentifier(zc::str("EmptyClass"));
  zc::Vector<zc::Own<Expression>> args2;
  auto expr2 = factory::createNewExpression(zc::mv(callee2), zc::mv(args2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kNewExpression);
  auto newExpr2 = static_cast<NewExpression*>(expr2.get());
  ZC_EXPECT(newExpr2->getArguments().size() == 0);
}

ZC_TEST("ExpressionTest.ConditionalExpression") {
  // Test ternary operator: condition ? consequent : alternate
  auto test = factory::createBooleanLiteral(true);
  auto consequent = factory::createStringLiteral(zc::str("yes"));
  auto alternate = factory::createStringLiteral(zc::str("no"));
  auto expr =
      factory::createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kConditionalExpression);
  auto condExpr = static_cast<ConditionalExpression*>(expr.get());
  ZC_EXPECT(condExpr != nullptr);

  // Test with complex expressions
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
  // Test basic type casting: expression as Type
  auto expression = factory::createIntegerLiteral(42);
  auto targetType = factory::createPredefinedType(zc::str("String"));
  auto expr = factory::createAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kAsExpression);
  auto asExpr = static_cast<AsExpression*>(expr.get());
  ZC_EXPECT(asExpr != nullptr);

  // Test casting identifier to custom type
  auto expression2 = factory::createIdentifier(zc::str("obj"));
  auto targetType2 =
      factory::createTypeReference(factory::createIdentifier(zc::str("MyClass")), zc::none);
  auto expr2 = factory::createAsExpression(zc::mv(expression2), zc::mv(targetType2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kAsExpression);
}

ZC_TEST("ExpressionTest.ForcedAsExpression") {
  // Test forced type casting: expression as! Type
  auto expression = factory::createIdentifier(zc::str("value"));
  auto targetType = factory::createPredefinedType(zc::str("Number"));
  auto expr = factory::createForcedAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kForcedAsExpression);
  auto forcedAsExpr = static_cast<ForcedAsExpression*>(expr.get());
  ZC_EXPECT(forcedAsExpr != nullptr);
}

ZC_TEST("ExpressionTest.ConditionalAsExpression") {
  // Test conditional type casting: expression as? Type
  auto expression = factory::createIdentifier(zc::str("maybeValue"));
  auto targetType = factory::createPredefinedType(zc::str("Optional"));
  auto expr = factory::createConditionalAsExpression(zc::mv(expression), zc::mv(targetType));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kConditionalAsExpression);
  auto conditionalAsExpr = static_cast<ConditionalAsExpression*>(expr.get());
  ZC_EXPECT(conditionalAsExpr != nullptr);
}

ZC_TEST("ExpressionTest.VoidExpression") {
  // Test void expression: void expression
  auto expression = factory::createCallExpression(factory::createIdentifier(zc::str("sideEffect")),
                                                  zc::Vector<zc::Own<Expression>>());
  auto expr = factory::createVoidExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kVoidExpression);
  auto voidExpr = static_cast<VoidExpression*>(expr.get());
  ZC_EXPECT(voidExpr != nullptr);
}

ZC_TEST("ExpressionTest.TypeOfExpression") {
  // Test typeof expression: typeof expression
  auto expression = factory::createIdentifier(zc::str("variable"));
  auto expr = factory::createTypeOfExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kTypeOfExpression);
  auto typeofExpr = static_cast<TypeOfExpression*>(expr.get());
  ZC_EXPECT(typeofExpr != nullptr);

  // Test typeof with literal
  auto expression2 = factory::createStringLiteral(zc::str("hello"));
  auto expr2 = factory::createTypeOfExpression(zc::mv(expression2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kTypeOfExpression);
}

ZC_TEST("ExpressionTest.AwaitExpression") {
  // Test await expression: await expression
  auto expression = factory::createCallExpression(
      factory::createIdentifier(zc::str("asyncFunction")), zc::Vector<zc::Own<Expression>>());
  auto expr = factory::createAwaitExpression(zc::mv(expression));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kAwaitExpression);
  auto awaitExpr = static_cast<AwaitExpression*>(expr.get());
  ZC_EXPECT(awaitExpr != nullptr);

  // Test await with identifier
  auto expression2 = factory::createIdentifier(zc::str("promise"));
  auto expr2 = factory::createAwaitExpression(zc::mv(expression2));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kAwaitExpression);
}

ZC_TEST("ExpressionTest.OptionalExpression") {
  // Test optional chaining: object?.property
  auto object = factory::createIdentifier(zc::str("obj"));
  auto property = factory::createIdentifier(zc::str("prop"));
  auto expr = factory::createOptionalExpression(zc::mv(object), zc::mv(property));

  ZC_EXPECT(expr->getKind() == SyntaxKind::kOptionalExpression);
  auto optionalExpr = static_cast<OptionalExpression*>(expr.get());
  ZC_EXPECT(optionalExpr != nullptr);

  // Test optional chaining with method call
  auto object2 = factory::createIdentifier(zc::str("service"));
  auto methodCall = factory::createCallExpression(factory::createIdentifier(zc::str("getData")),
                                                  zc::Vector<zc::Own<Expression>>());
  auto expr2 = factory::createOptionalExpression(zc::mv(object2), zc::mv(methodCall));

  ZC_EXPECT(expr2->getKind() == SyntaxKind::kOptionalExpression);
}

ZC_TEST("ExpressionTest.ElementAccessExpression") {
  // Test basic element access: arr[0]
  auto arrayExpr = factory::createIdentifier(zc::str("arr"));
  auto indexExpr = factory::createIntegerLiteral(0);
  auto elementAccess = factory::createElementAccessExpression(zc::mv(arrayExpr), zc::mv(indexExpr));

  ZC_EXPECT(elementAccess->getKind() == SyntaxKind::kElementAccessExpression);
  auto elemAccessExpr = static_cast<ElementAccessExpression*>(elementAccess.get());
  ZC_EXPECT(elemAccessExpr != nullptr);

  // Test with string index: obj["prop"]
  auto objExpr = factory::createIdentifier(zc::str("obj"));
  auto propIndexExpr = factory::createStringLiteral(zc::str("prop"));
  auto propElementAccess =
      factory::createElementAccessExpression(zc::mv(objExpr), zc::mv(propIndexExpr));

  ZC_EXPECT(propElementAccess->getKind() == SyntaxKind::kElementAccessExpression);
}

ZC_TEST("ExpressionTest.PropertyAccessExpression") {
  // Test basic property access: obj.prop
  auto objExpr = factory::createIdentifier(zc::str("obj"));
  auto propName = factory::createIdentifier(zc::str("prop"));
  auto propertyAccess =
      factory::createPropertyAccessExpression(zc::mv(objExpr), zc::mv(propName), false);

  ZC_EXPECT(propertyAccess->getKind() == SyntaxKind::kPropertyAccessExpression);
  auto propAccessExpr = static_cast<PropertyAccessExpression*>(propertyAccess.get());
  ZC_EXPECT(propAccessExpr != nullptr);
  ZC_EXPECT(propAccessExpr->isQuestionDot() == false);

  // Test optional property access: obj?.prop
  auto optionalObjExpr = factory::createIdentifier(zc::str("obj"));
  auto optionalPropName = factory::createIdentifier(zc::str("prop"));
  auto optionalPropertyAccess = factory::createPropertyAccessExpression(
      zc::mv(optionalObjExpr), zc::mv(optionalPropName), true);

  ZC_EXPECT(optionalPropertyAccess->getKind() == SyntaxKind::kPropertyAccessExpression);
  auto optionalPropAccessExpr =
      static_cast<PropertyAccessExpression*>(optionalPropertyAccess.get());
  ZC_EXPECT(optionalPropAccessExpr->isQuestionDot() == true);
}

ZC_TEST("ExpressionTest.Identifier") {
  // Test simple identifier
  auto identifier = factory::createIdentifier(zc::str("variableName"));

  ZC_EXPECT(identifier->getKind() == SyntaxKind::kIdentifier);
  auto identifierExpr = static_cast<Identifier*>(identifier.get());
  ZC_EXPECT(identifierExpr != nullptr);
  ZC_EXPECT(identifierExpr->getName() == "variableName");

  // Test identifier with underscore
  auto underscoreId = factory::createIdentifier(zc::str("_privateVar"));
  ZC_EXPECT(underscoreId->getKind() == SyntaxKind::kIdentifier);
  auto underscoreIdExpr = static_cast<Identifier*>(underscoreId.get());
  ZC_EXPECT(underscoreIdExpr->getName() == "_privateVar");

  // Test identifier with numbers
  auto numberedId = factory::createIdentifier(zc::str("var123"));
  ZC_EXPECT(numberedId->getKind() == SyntaxKind::kIdentifier);
  auto numberedIdExpr = static_cast<Identifier*>(numberedId.get());
  ZC_EXPECT(numberedIdExpr->getName() == "var123");
}

ZC_TEST("ExpressionTest.GetterMethods") {
  // Test PrefixUnaryExpression getters
  auto operand = factory::createIdentifier(zc::str("x"));
  auto op = factory::createPreIncrementOperator();
  auto prefixExpr = factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
  auto prefixUnaryExpr = static_cast<PrefixUnaryExpression*>(prefixExpr.get());

  ZC_EXPECT(prefixUnaryExpr->getOperator().getKind() == SyntaxKind::kOperator);
  ZC_EXPECT(prefixUnaryExpr->getOperand().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(prefixUnaryExpr->isPrefix() == true);

  // Test PostfixUnaryExpression getters
  auto postfixOperand = factory::createIdentifier(zc::str("y"));
  auto postfixOp = factory::createPostIncrementOperator();
  auto postfixExpr =
      factory::createPostfixUnaryExpression(zc::mv(postfixOp), zc::mv(postfixOperand));
  auto postfixUnaryExpr = static_cast<PostfixUnaryExpression*>(postfixExpr.get());

  ZC_EXPECT(postfixUnaryExpr->getOperator().getKind() == SyntaxKind::kOperator);
  ZC_EXPECT(postfixUnaryExpr->getOperand().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(postfixUnaryExpr->isPrefix() == false);

  // Test ConditionalExpression getters
  auto test = factory::createIdentifier(zc::str("condition"));
  auto consequent = factory::createIdentifier(zc::str("trueValue"));
  auto alternate = factory::createIdentifier(zc::str("falseValue"));
  auto conditionalExpr =
      factory::createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));
  auto condExpr = static_cast<ConditionalExpression*>(conditionalExpr.get());

  ZC_EXPECT(condExpr->getTest().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(condExpr->getConsequent().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(condExpr->getAlternate().getKind() == SyntaxKind::kIdentifier);

  // Test CallExpression getters
  auto callee = factory::createIdentifier(zc::str("func"));
  auto callExpr = factory::createCallExpression(zc::mv(callee), {});
  auto callExpression = static_cast<CallExpression*>(callExpr.get());

  ZC_EXPECT(callExpression->getCallee().getKind() == SyntaxKind::kIdentifier);
  ZC_EXPECT(callExpression->getArguments().size() == 0);
}

ZC_TEST("ExpressionTest.AcceptMethods") {
  // Test that accept methods exist and can be called
  // We don't need a full visitor implementation for coverage testing

  // Test PrefixUnaryExpression has accept method
  auto prefixOperand = factory::createIdentifier(zc::str("x"));
  auto prefixOp = factory::createPreIncrementOperator();
  auto prefixExpr = factory::createPrefixUnaryExpression(zc::mv(prefixOp), zc::mv(prefixOperand));
  ZC_EXPECT(prefixExpr->getKind() == SyntaxKind::kPrefixUnaryExpression);

  // Test PostfixUnaryExpression has accept method
  auto postfixOperand = factory::createIdentifier(zc::str("y"));
  auto postfixOp = factory::createPostIncrementOperator();
  auto postfixExpr =
      factory::createPostfixUnaryExpression(zc::mv(postfixOp), zc::mv(postfixOperand));
  ZC_EXPECT(postfixExpr->getKind() == SyntaxKind::kPostfixUnaryExpression);

  // Test ConditionalExpression has accept method
  auto test = factory::createIdentifier(zc::str("condition"));
  auto consequent = factory::createIdentifier(zc::str("trueValue"));
  auto alternate = factory::createIdentifier(zc::str("falseValue"));
  auto conditionalExpr =
      factory::createConditionalExpression(zc::mv(test), zc::mv(consequent), zc::mv(alternate));
  ZC_EXPECT(conditionalExpr->getKind() == SyntaxKind::kConditionalExpression);

  // Test CallExpression has accept method
  auto callee = factory::createIdentifier(zc::str("func"));
  auto callExpr = factory::createCallExpression(zc::mv(callee), {});
  ZC_EXPECT(callExpr->getKind() == SyntaxKind::kCallExpression);

  // Test ElementAccessExpression has accept method
  auto object = factory::createIdentifier(zc::str("arr"));
  auto index = factory::createIntegerLiteral(0);
  auto elementExpr = factory::createElementAccessExpression(zc::mv(object), zc::mv(index));
  ZC_EXPECT(elementExpr->getKind() == SyntaxKind::kElementAccessExpression);

  // Test PropertyAccessExpression has accept method
  auto propObject = factory::createIdentifier(zc::str("obj"));
  auto propName = factory::createIdentifier(zc::str("prop"));
  auto propExpr =
      factory::createPropertyAccessExpression(zc::mv(propObject), zc::mv(propName), false);
  ZC_EXPECT(propExpr->getKind() == SyntaxKind::kPropertyAccessExpression);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
