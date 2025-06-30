// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#pragma once

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Operator precedence levels
enum class OperatorPrecedence {
  kLowest = 0,
  kAssignment = 1,       // =, +=, -=, etc.
  kConditional = 2,      // ?:
  kLogicalOr = 3,        // ||
  kLogicalAnd = 4,       // &&
  kBitwiseOr = 5,        // |
  kBitwiseXor = 6,       // ^
  kBitwiseAnd = 7,       // &
  kEquality = 8,         // ==, !=
  kRelational = 9,       // <, >, <=, >=
  kShift = 10,           // <<, >>
  kAdditive = 11,        // +, -
  kMultiplicative = 12,  // *, /, %
  kExponentiation = 13,  // **
  kUnary = 14,           // +, -, !, ~, ++, --
  kPostfix = 15,         // ++, --, [], (), .
  kPrimary = 16          // literals, identifiers
};

// Operator associativity
enum class OperatorAssociativity { kLeft, kRight, kNone };

// Operator types
enum class OperatorType { kBinary, kUnary, kAssignment, kUpdate };

// Base class for all operators
class Operator : public Node {
public:
  explicit Operator(zc::String symbol, OperatorType type, OperatorPrecedence precedence,
                    OperatorAssociativity associativity);
  ~Operator() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(Operator);

  zc::StringPtr getSymbol() const;
  OperatorType getType() const;
  OperatorPrecedence getPrecedence() const;
  OperatorAssociativity getAssociativity() const;

  // Helper methods for operator classification
  bool isBinary() const;
  bool isUnary() const;
  bool isAssignment() const;
  bool isUpdate() const;

  // Precedence comparison
  bool hasHigherPrecedenceThan(const Operator& other) const;
  bool hasLowerPrecedenceThan(const Operator& other) const;
  bool hasSamePrecedenceAs(const Operator& other) const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Binary operators: +, -, *, /, %, ==, !=, <, >, <=, >=, &&, ||, &, |, ^, <<, >>
class BinaryOperator : public Operator {
public:
  explicit BinaryOperator(zc::String symbol, OperatorPrecedence precedence,
                          OperatorAssociativity associativity = OperatorAssociativity::kLeft);
  ~BinaryOperator() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(BinaryOperator);
};

// Unary operators: +, -, !, ~
class UnaryOperator : public Operator {
public:
  explicit UnaryOperator(zc::String symbol, bool prefix = true);
  ~UnaryOperator() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(UnaryOperator);

  bool isPrefix() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Assignment operators: =, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=
class AssignmentOperator : public Operator {
public:
  explicit AssignmentOperator(zc::String symbol);
  ~AssignmentOperator() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(AssignmentOperator);

  bool isCompound() const;
};

// Update operators: ++, --
class UpdateOperator : public Operator {
public:
  explicit UpdateOperator(zc::String symbol, bool prefix = true);
  ~UpdateOperator() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(UpdateOperator);

  bool isPrefix() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang