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
/// \brief Unit tests for AST operator functionality.
///
/// This file contains ztest-based unit tests for the AST operator classes,
/// testing operator creation, properties, and precedence rules.

#include "zomlang/compiler/ast/operator.h"

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace compiler {
namespace ast {

ZC_TEST("OperatorTest: BasicOperatorCreation") {
  // Test basic operator creation
  Operator addOp(zc::str("+"), OperatorType::kBinary, OperatorPrecedence::kAdditive,
                 OperatorAssociativity::kLeft);

  ZC_EXPECT(addOp.getSymbol() == "+", "Operator should have correct symbol");
  ZC_EXPECT(addOp.getType() == OperatorType::kBinary, "Operator should be binary");
  ZC_EXPECT(addOp.getPrecedence() == OperatorPrecedence::kAdditive,
            "Operator should have additive precedence");
  ZC_EXPECT(addOp.getAssociativity() == OperatorAssociativity::kLeft,
            "Operator should be left associative");
  ZC_EXPECT(addOp.isBinary(), "Operator should be binary");
  ZC_EXPECT(!addOp.isUnary(), "Operator should not be unary");
  ZC_EXPECT(!addOp.isAssignment(), "Operator should not be assignment");
  ZC_EXPECT(!addOp.isUpdate(), "Operator should not be update");
}

ZC_TEST("OperatorTest: BinaryOperatorCreation") {
  // Test binary operator creation
  BinaryOperator mulOp(zc::str("*"), OperatorPrecedence::kMultiplicative,
                       OperatorAssociativity::kLeft);

  ZC_EXPECT(mulOp.getSymbol() == "*", "Binary operator should have correct symbol");
  ZC_EXPECT(mulOp.getType() == OperatorType::kBinary, "Binary operator should be binary type");
  ZC_EXPECT(mulOp.getPrecedence() == OperatorPrecedence::kMultiplicative,
            "Binary operator should have multiplicative precedence");
  ZC_EXPECT(mulOp.isBinary(), "Binary operator should be binary");
}

ZC_TEST("OperatorTest: UnaryOperatorCreation") {
  // Test unary operator creation
  UnaryOperator notOp(zc::str("!"), true);

  ZC_EXPECT(notOp.getSymbol() == "!", "Unary operator should have correct symbol");
  ZC_EXPECT(notOp.getType() == OperatorType::kUnary, "Unary operator should be unary type");
  ZC_EXPECT(notOp.getPrecedence() == OperatorPrecedence::kUnary,
            "Unary operator should have unary precedence");
  ZC_EXPECT(notOp.getAssociativity() == OperatorAssociativity::kRight,
            "Unary operator should be right associative");
  ZC_EXPECT(notOp.isUnary(), "Unary operator should be unary");
  ZC_EXPECT(notOp.isPrefix(), "Unary operator should be prefix");
  ZC_EXPECT(!notOp.isBinary(), "Unary operator should not be binary");
}

ZC_TEST("OperatorTest: AssignmentOperatorCreation") {
  // Test assignment operator creation
  AssignmentOperator assignOp(zc::str("="));

  ZC_EXPECT(assignOp.getSymbol() == "=", "Assignment operator should have correct symbol");
  ZC_EXPECT(assignOp.getType() == OperatorType::kAssignment,
            "Assignment operator should be assignment type");
  ZC_EXPECT(assignOp.getPrecedence() == OperatorPrecedence::kAssignment,
            "Assignment operator should have assignment precedence");
  ZC_EXPECT(assignOp.getAssociativity() == OperatorAssociativity::kRight,
            "Assignment operator should be right associative");
  ZC_EXPECT(assignOp.isAssignment(), "Assignment operator should be assignment");
  ZC_EXPECT(!assignOp.isCompound(), "Simple assignment should not be compound");
}

ZC_TEST("OperatorTest: CompoundAssignmentOperator") {
  // Test compound assignment operator creation
  AssignmentOperator addAssignOp(zc::str("+="));

  ZC_EXPECT(addAssignOp.getSymbol() == "+=", "Compound assignment should have correct symbol");
  ZC_EXPECT(addAssignOp.isCompound(), "Compound assignment should be compound");
}

ZC_TEST("OperatorTest: OperatorPrecedenceComparison") {
  // Test operator precedence comparison
  Operator addOp(zc::str("+"), OperatorType::kBinary, OperatorPrecedence::kAdditive,
                 OperatorAssociativity::kLeft);
  Operator mulOp(zc::str("*"), OperatorType::kBinary, OperatorPrecedence::kMultiplicative,
                 OperatorAssociativity::kLeft);
  Operator eqOp(zc::str("=="), OperatorType::kBinary, OperatorPrecedence::kEquality,
                OperatorAssociativity::kLeft);

  ZC_EXPECT(mulOp.hasHigherPrecedenceThan(addOp),
            "Multiplication should have higher precedence than addition");
  ZC_EXPECT(addOp.hasLowerPrecedenceThan(mulOp),
            "Addition should have lower precedence than multiplication");
  ZC_EXPECT(addOp.hasSamePrecedenceAs(addOp), "Same operator should have same precedence");
  ZC_EXPECT(addOp.hasHigherPrecedenceThan(eqOp),
            "Addition should have higher precedence than equality");
}

ZC_TEST("OperatorTest: VariousOperatorTypes") {
  // Test various operator types and their properties
  BinaryOperator divOp(zc::str("/"), OperatorPrecedence::kMultiplicative,
                       OperatorAssociativity::kLeft);
  UnaryOperator negOp(zc::str("-"), false);
  AssignmentOperator subAssignOp(zc::str("-="));

  ZC_EXPECT(divOp.getSymbol() == "/", "Division operator should have correct symbol");
  ZC_EXPECT(negOp.getSymbol() == "-", "Negation operator should have correct symbol");
  ZC_EXPECT(subAssignOp.getSymbol() == "-=", "Subtraction assignment should have correct symbol");

  ZC_EXPECT(divOp.isBinary(), "Division should be binary");
  ZC_EXPECT(negOp.isUnary(), "Negation should be unary");
  ZC_EXPECT(subAssignOp.isAssignment(), "Subtraction assignment should be assignment");
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang