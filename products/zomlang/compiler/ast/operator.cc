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

#include "zomlang/compiler/ast/operator.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// Operator::Impl

struct Operator::Impl {
  const zc::String symbol;
  const OperatorType type;
  const OperatorPrecedence precedence;
  const OperatorAssociativity associativity;

  Impl(zc::String s, OperatorType t, OperatorPrecedence p, OperatorAssociativity a)
      : symbol(zc::mv(s)), type(t), precedence(p), associativity(a) {}
};

// ================================================================================
// Operator

Operator::Operator(zc::String symbol, OperatorType type, OperatorPrecedence precedence,
                   OperatorAssociativity associativity)
    : Node(SyntaxKind::kOperator),
      impl(zc::heap<Impl>(zc::mv(symbol), type, precedence, associativity)) {}

Operator::~Operator() noexcept(false) = default;

zc::StringPtr Operator::getSymbol() const { return impl->symbol; }

OperatorType Operator::getType() const { return impl->type; }

OperatorPrecedence Operator::getPrecedence() const { return impl->precedence; }

OperatorAssociativity Operator::getAssociativity() const { return impl->associativity; }

bool Operator::isBinary() const { return impl->type == OperatorType::kBinary; }

bool Operator::isUnary() const { return impl->type == OperatorType::kUnary; }

bool Operator::isAssignment() const { return impl->type == OperatorType::kAssignment; }

bool Operator::isUpdate() const { return impl->type == OperatorType::kUpdate; }

bool Operator::hasHigherPrecedenceThan(const Operator& other) const {
  return static_cast<int>(impl->precedence) > static_cast<int>(other.impl->precedence);
}

bool Operator::hasLowerPrecedenceThan(const Operator& other) const {
  return static_cast<int>(impl->precedence) < static_cast<int>(other.impl->precedence);
}

bool Operator::hasSamePrecedenceAs(const Operator& other) const {
  return impl->precedence == other.impl->precedence;
}

// ================================================================================
// BinaryOperator

BinaryOperator::BinaryOperator(zc::String symbol, OperatorPrecedence precedence,
                               OperatorAssociativity associativity)
    : Operator(zc::mv(symbol), OperatorType::kBinary, precedence, associativity) {
  // Update the syntax kind to be more specific
  // Note: We need to access the impl to change the kind, but it's private
  // For now, we'll keep the base kOperator kind
}

BinaryOperator::~BinaryOperator() noexcept(false) = default;

// ================================================================================
// UnaryOperator::Impl

struct UnaryOperator::Impl {
  const bool prefix;

  explicit Impl(bool p) : prefix(p) {}
};

// ================================================================================
// UnaryOperator

UnaryOperator::UnaryOperator(zc::String symbol, bool prefix)
    : Operator(zc::mv(symbol), OperatorType::kUnary, OperatorPrecedence::kUnary,
               OperatorAssociativity::kRight),
      impl(zc::heap<Impl>(prefix)) {}

UnaryOperator::~UnaryOperator() noexcept(false) = default;

bool UnaryOperator::isPrefix() const { return impl->prefix; }

// ================================================================================
// AssignmentOperator

AssignmentOperator::AssignmentOperator(zc::String symbol)
    : Operator(zc::mv(symbol), OperatorType::kAssignment, OperatorPrecedence::kAssignment,
               OperatorAssociativity::kRight) {}

AssignmentOperator::~AssignmentOperator() noexcept(false) = default;

bool AssignmentOperator::isCompound() const {
  zc::StringPtr symbol = getSymbol();
  return symbol != "=";
}

// ================================================================================
// UpdateOperator::Impl

struct UpdateOperator::Impl {
  const bool prefix;

  explicit Impl(bool p) : prefix(p) {}
};

// ================================================================================
// UpdateOperator

UpdateOperator::UpdateOperator(zc::String symbol, bool prefix)
    : Operator(zc::mv(symbol), OperatorType::kUpdate,
               prefix ? OperatorPrecedence::kUnary : OperatorPrecedence::kPostfix,
               OperatorAssociativity::kRight),
      impl(zc::heap<Impl>(prefix)) {}

UpdateOperator::~UpdateOperator() noexcept(false) = default;

bool UpdateOperator::isPrefix() const { return impl->prefix; }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang