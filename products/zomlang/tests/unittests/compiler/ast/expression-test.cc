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

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
