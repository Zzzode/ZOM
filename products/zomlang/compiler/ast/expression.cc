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

#include "zomlang/compiler/ast/expression.h"

#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/operator.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// Expression
Expression::Expression(SyntaxKind kind) noexcept : Node(kind) {}
Expression::~Expression() noexcept(false) = default;

// ================================================================================
// UnaryExpression
UnaryExpression::UnaryExpression(SyntaxKind kind) noexcept : Expression(kind) {}
UnaryExpression::~UnaryExpression() noexcept(false) = default;

// ================================================================================
// UpdateExpression

UpdateExpression::UpdateExpression(SyntaxKind kind) noexcept : UnaryExpression(kind) {}
UpdateExpression::~UpdateExpression() noexcept(false) = default;

// ================================================================================
// PrefixUnaryExpression::Impl

struct PrefixUnaryExpression::Impl {
  const zc::Own<UnaryOperator> op;
  const zc::Own<Expression> operand;

  Impl(zc::Own<UnaryOperator> o, zc::Own<Expression> operand)
      : op(zc::mv(o)), operand(zc::mv(operand)) {}
};

// ================================================================================
// PrefixUnaryExpression

PrefixUnaryExpression::PrefixUnaryExpression(zc::Own<UnaryOperator> op,
                                             zc::Own<Expression> operand) noexcept
    : UpdateExpression(SyntaxKind::kPrefixUnaryExpression),
      impl(zc::heap<Impl>(zc::mv(op), zc::mv(operand))) {}

PrefixUnaryExpression::~PrefixUnaryExpression() noexcept(false) = default;

const UnaryOperator* PrefixUnaryExpression::getOperator() const { return impl->op.get(); }

const Expression* PrefixUnaryExpression::getOperand() const { return impl->operand.get(); }

bool PrefixUnaryExpression::isPrefix() const { return true; }

// ================================================================================
// PostfixUnaryExpression::Impl

struct PostfixUnaryExpression::Impl {
  const zc::Own<UnaryOperator> op;
  const zc::Own<Expression> operand;

  Impl(zc::Own<UnaryOperator> o, zc::Own<Expression> operand)
      : op(zc::mv(o)), operand(zc::mv(operand)) {}
};

// ================================================================================
// PostfixUnaryExpression

PostfixUnaryExpression::PostfixUnaryExpression(zc::Own<UnaryOperator> op,
                                               zc::Own<Expression> operand) noexcept
    : UpdateExpression(SyntaxKind::kPostfixUnaryExpression),
      impl(zc::heap<Impl>(zc::mv(op), zc::mv(operand))) {}

PostfixUnaryExpression::~PostfixUnaryExpression() noexcept(false) = default;

const UnaryOperator* PostfixUnaryExpression::getOperator() const { return impl->op.get(); }

const Expression* PostfixUnaryExpression::getOperand() const { return impl->operand.get(); }

bool PostfixUnaryExpression::isPrefix() const { return false; }

// ================================================================================
// LeftHandSideExpression

LeftHandSideExpression::LeftHandSideExpression(SyntaxKind kind) noexcept : UpdateExpression(kind) {}

LeftHandSideExpression::~LeftHandSideExpression() noexcept(false) = default;

// ================================================================================
// NewExpression::Impl

struct NewExpression::Impl {
  const zc::Own<Expression> callee;
  const NodeList<Expression> arguments;

  Impl(zc::Own<Expression> c, zc::Vector<zc::Own<Expression>>&& args)
      : callee(zc::mv(c)), arguments(zc::mv(args)) {}
};

// ================================================================================
// NewExpression

NewExpression::NewExpression(zc::Own<Expression> callee,
                             zc::Vector<zc::Own<Expression>>&& arguments) noexcept
    : PrimaryExpression(SyntaxKind::kNewExpression),
      impl(zc::heap<Impl>(zc::mv(callee), zc::mv(arguments))) {}

NewExpression::~NewExpression() noexcept(false) = default;

const Expression* NewExpression::getCallee() const { return impl->callee.get(); }

const NodeList<Expression>& NewExpression::getArguments() const { return impl->arguments; }

// ================================================================================
// MemberExpression

MemberExpression::MemberExpression(SyntaxKind kind) noexcept : LeftHandSideExpression(kind) {}
MemberExpression::~MemberExpression() noexcept(false) = default;

// ================================================================================
// PrimaryExpression

PrimaryExpression::PrimaryExpression(SyntaxKind kind) noexcept : MemberExpression(kind) {}
PrimaryExpression::~PrimaryExpression() noexcept(false) = default;

// ================================================================================
// CallExpression::Impl

struct CallExpression::Impl {
  const zc::Own<Expression> callee;
  const NodeList<Expression> arguments;

  Impl(zc::Own<Expression> c, zc::Vector<zc::Own<Expression>>&& args)
      : callee(zc::mv(c)), arguments(zc::mv(args)) {}
};

// ================================================================================
// CallExpression

CallExpression::CallExpression(zc::Own<Expression> callee,
                               zc::Vector<zc::Own<Expression>>&& arguments) noexcept
    : LeftHandSideExpression(SyntaxKind::kCallExpression),
      impl(zc::heap<Impl>(zc::mv(callee), zc::mv(arguments))) {}

CallExpression::~CallExpression() noexcept(false) = default;

const Expression* CallExpression::getCallee() const { return impl->callee.get(); }

const NodeList<Expression>& CallExpression::getArguments() const { return impl->arguments; }

// ================================================================================
// OptionalExpression::Impl

struct OptionalExpression::Impl {
  const zc::Own<Expression> object;
  const zc::Own<Expression> property;

  Impl(zc::Own<Expression> o, zc::Own<Expression> p) : object(zc::mv(o)), property(zc::mv(p)) {}
};

// ================================================================================
// OptionalExpression

OptionalExpression::OptionalExpression(zc::Own<Expression> object, zc::Own<Expression> property)
    : LeftHandSideExpression(SyntaxKind::kOptionalExpression),
      impl(zc::heap<Impl>(zc::mv(object), zc::mv(property))) {}

OptionalExpression::~OptionalExpression() noexcept(false) = default;

const Expression* OptionalExpression::getObject() const { return impl->object.get(); }

const Expression* OptionalExpression::getProperty() const { return impl->property.get(); }

// ================================================================================
// BinaryExpression::Impl

struct BinaryExpression::Impl {
  const zc::Own<Expression> left;
  const zc::Own<BinaryOperator> op;
  const zc::Own<Expression> right;

  explicit Impl(zc::Own<Expression>&& l, zc::Own<BinaryOperator>&& op, zc::Own<Expression>&& r)
      : left(zc::mv(l)), op(zc::mv(op)), right(zc::mv(r)) {}
};

// ================================================================================
// BinaryExpression

BinaryExpression::BinaryExpression(zc::Own<Expression> left, zc::Own<BinaryOperator> op,
                                   zc::Own<Expression> right) noexcept
    : Expression(SyntaxKind::kBinaryExpression),
      impl(zc::heap<Impl>(zc::mv(left), zc::mv(op), zc::mv(right))) {}

BinaryExpression::~BinaryExpression() noexcept(false) = default;

const Expression* BinaryExpression::getLeft() const { return impl->left.get(); }

const BinaryOperator* BinaryExpression::getOperator() const { return impl->op.get(); }

const Expression* BinaryExpression::getRight() const { return impl->right.get(); }

// ================================================================================
// AssignmentExpression::Impl

struct AssignmentExpression::Impl {
  const zc::Own<Expression> left;
  const zc::Own<AssignmentOperator> op;
  const zc::Own<Expression> right;

  Impl(zc::Own<Expression> l, zc::Own<AssignmentOperator> o, zc::Own<Expression> r)
      : left(zc::mv(l)), op(zc::mv(o)), right(zc::mv(r)) {}
};

// ================================================================================
// AssignmentExpression

AssignmentExpression::AssignmentExpression(zc::Own<Expression> left, zc::Own<AssignmentOperator> op,
                                           zc::Own<Expression> right) noexcept
    : Expression(SyntaxKind::kAssignmentExpression),
      impl(zc::heap<Impl>(zc::mv(left), zc::mv(op), zc::mv(right))) {}

AssignmentExpression::~AssignmentExpression() noexcept(false) = default;

const Expression* AssignmentExpression::getLeft() const { return impl->left.get(); }

const AssignmentOperator* AssignmentExpression::getOperator() const { return impl->op.get(); }

const Expression* AssignmentExpression::getRight() const { return impl->right.get(); }

// ================================================================================
// ConditionalExpression::Impl

struct ConditionalExpression::Impl {
  const zc::Own<Expression> test;
  const zc::Own<Expression> consequent;
  const zc::Own<Expression> alternate;

  Impl(zc::Own<Expression> t, zc::Own<Expression> c, zc::Own<Expression> a)
      : test(zc::mv(t)), consequent(zc::mv(c)), alternate(zc::mv(a)) {}
};

// ================================================================================
// ConditionalExpression

ConditionalExpression::ConditionalExpression(zc::Own<Expression> test,
                                             zc::Own<Expression> consequent,
                                             zc::Own<Expression> alternate) noexcept
    : Expression(SyntaxKind::kConditionalExpression),
      impl(zc::heap<Impl>(zc::mv(test), zc::mv(consequent), zc::mv(alternate))) {}

ConditionalExpression::~ConditionalExpression() noexcept(false) = default;

const Expression* ConditionalExpression::getTest() const { return impl->test.get(); }

const Expression* ConditionalExpression::getConsequent() const { return impl->consequent.get(); }

const Expression* ConditionalExpression::getAlternate() const { return impl->alternate.get(); }

// ================================================================================
// Identifier::Impl

struct Identifier::Impl {
  const zc::String name;

  explicit Impl(zc::String n) : name(zc::mv(n)) {}
};

// ================================================================================
// Identifier

Identifier::Identifier(zc::String name) noexcept
    : PrimaryExpression(SyntaxKind::kIdentifier), impl(zc::heap<Impl>(zc::mv(name))) {}

Identifier::~Identifier() noexcept(false) = default;

const zc::StringPtr Identifier::getName() const { return impl->name.asPtr(); }

// ================================================================================
// LiteralExpression

LiteralExpression::LiteralExpression(SyntaxKind kind) noexcept : PrimaryExpression(kind) {}

LiteralExpression::~LiteralExpression() noexcept(false) = default;

// ================================================================================
// StringLiteral::Impl

struct StringLiteral::Impl {
  const zc::String value;

  explicit Impl(zc::String v) : value(zc::mv(v)) {}
};

// ================================================================================
// StringLiteral

StringLiteral::StringLiteral(zc::String value) noexcept
    : LiteralExpression(SyntaxKind::kStringLiteral), impl(zc::heap<Impl>(zc::mv(value))) {}

StringLiteral::~StringLiteral() noexcept(false) = default;

const zc::StringPtr StringLiteral::getValue() const { return impl->value.asPtr(); }

// ================================================================================
// NumericLiteral::Impl

struct NumericLiteral::Impl {
  const double value;

  explicit Impl(double v) : value(v) {}
};

// ================================================================================
// NumericLiteral

NumericLiteral::NumericLiteral(double value) noexcept
    : LiteralExpression(SyntaxKind::kNumericLiteral), impl(zc::heap<Impl>(value)) {}

NumericLiteral::~NumericLiteral() noexcept(false) = default;

double NumericLiteral::getValue() const { return impl->value; }

// ================================================================================
// BooleanLiteral::Impl

struct BooleanLiteral::Impl {
  const bool value;

  explicit Impl(bool v) : value(v) {}
};

// ================================================================================
// BooleanLiteral

BooleanLiteral::BooleanLiteral(bool value) noexcept
    : LiteralExpression(SyntaxKind::kBooleanLiteral), impl(zc::heap<Impl>(value)) {}

BooleanLiteral::~BooleanLiteral() noexcept(false) = default;

bool BooleanLiteral::getValue() const { return impl->value; }

// ================================================================================
// NilLiteral

NilLiteral::NilLiteral() noexcept : LiteralExpression(SyntaxKind::kNilLiteral) {}
NilLiteral::~NilLiteral() noexcept(false) = default;

// ================================================================================
// CastExpression::Impl

struct CastExpression::Impl {
  const zc::Own<Expression> expression;
  const zc::String targetType;
  const bool isOptional;

  Impl(zc::Own<Expression> expr, zc::String type, bool optional)
      : expression(zc::mv(expr)), targetType(zc::mv(type)), isOptional(optional) {}
};

// ================================================================================
// CastExpression

CastExpression::CastExpression(zc::Own<Expression> expression, zc::String targetType,
                               bool isOptional) noexcept
    : Expression(SyntaxKind::kCastExpression),
      impl(zc::heap<Impl>(zc::mv(expression), zc::mv(targetType), isOptional)) {}

CastExpression::~CastExpression() noexcept(false) = default;

const Expression* CastExpression::getExpression() const { return impl->expression.get(); }

zc::StringPtr CastExpression::getTargetType() const { return impl->targetType.asPtr(); }

bool CastExpression::isOptional() const { return impl->isOptional; }

// ================================================================================
// VoidExpression::Impl

struct VoidExpression::Impl {
  const zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expr) : expression(zc::mv(expr)) {}
};

// ================================================================================
// VoidExpression

VoidExpression::VoidExpression(zc::Own<Expression> expression) noexcept
    : UnaryExpression(SyntaxKind::kVoidExpression), impl(zc::heap<Impl>(zc::mv(expression))) {}

VoidExpression::~VoidExpression() noexcept(false) = default;

const Expression* VoidExpression::getExpression() const { return impl->expression.get(); }

// ================================================================================
// TypeOfExpression::Impl

struct TypeOfExpression::Impl {
  const zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expr) : expression(zc::mv(expr)) {}
};

// ================================================================================
// TypeOfExpression

TypeOfExpression::TypeOfExpression(zc::Own<Expression> expression) noexcept
    : UnaryExpression(SyntaxKind::kTypeOfExpression), impl(zc::heap<Impl>(zc::mv(expression))) {}

TypeOfExpression::~TypeOfExpression() noexcept(false) = default;

const Expression* TypeOfExpression::getExpression() const { return impl->expression.get(); }

// ================================================================================
// AwaitExpression::Impl

struct AwaitExpression::Impl {
  const zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expr) : expression(zc::mv(expr)) {}
};

// ================================================================================
// AwaitExpression

AwaitExpression::AwaitExpression(zc::Own<Expression> expression) noexcept
    : Expression(SyntaxKind::kAwaitExpression), impl(zc::heap<Impl>(zc::mv(expression))) {}

AwaitExpression::~AwaitExpression() noexcept(false) = default;

const Expression* AwaitExpression::getExpression() const { return impl->expression.get(); }

// ================================================================================
// ParenthesizedExpression::Impl

struct ParenthesizedExpression::Impl {
  const zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expr) : expression(zc::mv(expr)) {}
};

// ================================================================================
// ParenthesizedExpression

ParenthesizedExpression::ParenthesizedExpression(zc::Own<Expression> expression) noexcept
    : PrimaryExpression(SyntaxKind::kParenthesizedExpression),
      impl(zc::heap<Impl>(zc::mv(expression))) {}

ParenthesizedExpression::~ParenthesizedExpression() noexcept(false) = default;

const Expression* ParenthesizedExpression::getExpression() const { return impl->expression.get(); }

// ================================================================================
// ArrayLiteralExpression::Impl

struct ArrayLiteralExpression::Impl {
  const NodeList<Expression> elements;

  explicit Impl(zc::Vector<zc::Own<Expression>>&& elems) : elements(zc::mv(elems)) {}
};

// ================================================================================
// ArrayLiteralExpression

ArrayLiteralExpression::ArrayLiteralExpression(zc::Vector<zc::Own<Expression>>&& elements) noexcept
    : PrimaryExpression(SyntaxKind::kArrayLiteralExpression),
      impl(zc::heap<Impl>(zc::mv(elements))) {}

ArrayLiteralExpression::~ArrayLiteralExpression() noexcept(false) = default;

const NodeList<Expression>& ArrayLiteralExpression::getElements() const { return impl->elements; }

// ================================================================================
// ObjectLiteralExpression::Impl

struct ObjectLiteralExpression::Impl {
  const NodeList<Expression> properties;

  explicit Impl(zc::Vector<zc::Own<Expression>>&& props) : properties(zc::mv(props)) {}
};

// ================================================================================
// ObjectLiteralExpression

ObjectLiteralExpression::ObjectLiteralExpression(
    zc::Vector<zc::Own<Expression>>&& properties) noexcept
    : PrimaryExpression(SyntaxKind::kObjectLiteralExpression),
      impl(zc::heap<Impl>(zc::mv(properties))) {}

ObjectLiteralExpression::~ObjectLiteralExpression() noexcept(false) = default;

const NodeList<Expression>& ObjectLiteralExpression::getProperties() const {
  return impl->properties;
}

// ================================================================================
// PropertyAccessExpression::Impl

struct PropertyAccessExpression::Impl {
  zc::Own<LeftHandSideExpression> expression;
  zc::Own<Identifier> name;
  const bool questionDot;

  Impl(zc::Own<LeftHandSideExpression> expr, zc::Own<Identifier> n, bool qDot)
      : expression(zc::mv(expr)), name(zc::mv(n)), questionDot(qDot) {}

  Impl() : questionDot(false) {}
};

// ================================================================================
// PropertyAccessExpression

PropertyAccessExpression::PropertyAccessExpression(zc::Own<LeftHandSideExpression> expression,
                                                   zc::Own<Identifier> name,
                                                   bool questionDot) noexcept
    : MemberExpression(SyntaxKind::kPropertyAccessExpression),
      impl(zc::heap<Impl>(zc::mv(expression), zc::mv(name), questionDot)) {}

PropertyAccessExpression::~PropertyAccessExpression() noexcept(false) = default;

LeftHandSideExpression* PropertyAccessExpression::getExpression() {
  return const_cast<LeftHandSideExpression*>(impl->expression.get());
}

Identifier* PropertyAccessExpression::getName() {
  return const_cast<Identifier*>(impl->name.get());
}

bool PropertyAccessExpression::isQuestionDot() { return impl->questionDot; }

// ================================================================================
// ElementAccessExpression::Impl

struct ElementAccessExpression::Impl {
  zc::Own<LeftHandSideExpression> expression;
  zc::Own<Expression> index;
  const bool questionDot;

  Impl(zc::Own<LeftHandSideExpression> expr, zc::Own<Expression> idx, bool qDot)
      : expression(zc::mv(expr)), index(zc::mv(idx)), questionDot(qDot) {}

  Impl() : questionDot(false) {}
};

// ================================================================================
// ElementAccessExpression

ElementAccessExpression::ElementAccessExpression(SyntaxKind kind) noexcept
    : MemberExpression(kind), impl(zc::heap<Impl>()) {}

ElementAccessExpression::ElementAccessExpression(zc::Own<LeftHandSideExpression> expression,
                                                 zc::Own<Expression> index,
                                                 bool questionDot) noexcept
    : MemberExpression(SyntaxKind::kElementAccessExpression),
      impl(zc::heap<Impl>(zc::mv(expression), zc::mv(index), questionDot)) {}

ElementAccessExpression::~ElementAccessExpression() noexcept(false) = default;

const LeftHandSideExpression* ElementAccessExpression::getExpression() {
  return impl->expression.get();
}

const Expression* ElementAccessExpression::getIndex() { return impl->index.get(); }

bool ElementAccessExpression::isQuestionDot() { return impl->questionDot; }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
