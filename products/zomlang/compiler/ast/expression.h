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
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Expression;
class BinaryOperator;
class UnaryOperator;
class UpdateOperator;
class AssignmentOperator;

class Expression : public Node {
public:
  explicit Expression(SyntaxKind kind = SyntaxKind::kExpression) noexcept;
  ~Expression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Expression);
};

// Forward declarations for proper inheritance hierarchy
class UpdateExpression;
class LeftHandSideExpression;
class NewExpression;
class MemberExpression;
class PrimaryExpression;

class UnaryExpression : public Expression {
public:
  UnaryExpression(zc::Own<UnaryOperator> op, zc::Own<Expression> operand);
  explicit UnaryExpression(SyntaxKind kind);
  ~UnaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UnaryExpression);

  const UnaryOperator* getOperator() const;
  const Expression* getOperand() const;
  bool isPrefix() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class UpdateExpression : public UnaryExpression {
public:
  UpdateExpression(zc::Own<UpdateOperator> op, zc::Own<Expression> operand);
  explicit UpdateExpression(SyntaxKind kind);
  ~UpdateExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UpdateExpression);

  const UpdateOperator* getOperator() const;
  const Expression* getOperand() const;
  bool isPrefix() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class LeftHandSideExpression : public UpdateExpression {
public:
  explicit LeftHandSideExpression(SyntaxKind kind = SyntaxKind::kLeftHandSideExpression) noexcept;
  ~LeftHandSideExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(LeftHandSideExpression);
};

class NewExpression : public LeftHandSideExpression {
public:
  NewExpression(zc::Own<Expression> callee, zc::Vector<zc::Own<Expression>>&& arguments);
  explicit NewExpression(SyntaxKind kind);
  ~NewExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NewExpression);

  const Expression* getCallee() const;
  const NodeList<Expression>& getArguments() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class MemberExpression : public NewExpression {
public:
  MemberExpression(zc::Own<Expression> object, zc::Own<Expression> property, bool computed = false);
  explicit MemberExpression(SyntaxKind kind);
  ~MemberExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(MemberExpression);

  const Expression* getObject() const;
  const Expression* getProperty() const;
  bool isComputed() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class PrimaryExpression : public MemberExpression {
public:
  explicit PrimaryExpression(SyntaxKind kind = SyntaxKind::kPrimaryExpression) noexcept;
  ~PrimaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PrimaryExpression);
};

class ParenthesizedExpression : public PrimaryExpression {
public:
  explicit ParenthesizedExpression(zc::Own<Expression> expression);
  explicit ParenthesizedExpression(SyntaxKind kind = SyntaxKind::kParenthesizedExpression) noexcept;
  ~ParenthesizedExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ParenthesizedExpression);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BinaryExpression : public Expression {
public:
  BinaryExpression(zc::Own<Expression> left, zc::Own<BinaryOperator> op, zc::Own<Expression> right);

  ~BinaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BinaryExpression);

  const Expression* getLeft() const;
  const BinaryOperator* getOperator() const;
  const Expression* getRight() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class AssignmentExpression : public Expression {
public:
  AssignmentExpression(zc::Own<Expression> left, zc::Own<AssignmentOperator> op,
                       zc::Own<Expression> right);
  ~AssignmentExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AssignmentExpression);

  const Expression* getLeft() const;
  const AssignmentOperator* getOperator() const;
  const Expression* getRight() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ConditionalExpression : public Expression {
public:
  ConditionalExpression(zc::Own<Expression> test, zc::Own<Expression> consequent,
                        zc::Own<Expression> alternate);
  ~ConditionalExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ConditionalExpression);

  const Expression* getTest() const;
  const Expression* getConsequent() const;
  const Expression* getAlternate() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class CallExpression : public LeftHandSideExpression {
public:
  CallExpression(zc::Own<Expression> callee, zc::Vector<zc::Own<Expression>>&& arguments);
  ~CallExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(CallExpression);

  const Expression* getCallee() const;
  const NodeList<Expression>& getArguments() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class OptionalExpression : public LeftHandSideExpression {
public:
  OptionalExpression(zc::Own<Expression> object, zc::Own<Expression> property);
  ~OptionalExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(OptionalExpression);

  const Expression* getObject() const;
  const Expression* getProperty() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class Identifier : public PrimaryExpression {
public:
  explicit Identifier(zc::String name);
  ~Identifier() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Identifier);

  const zc::StringPtr getName() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class LiteralExpression : public PrimaryExpression {
public:
  explicit LiteralExpression(SyntaxKind kind = SyntaxKind::kLiteral);
  ~LiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(LiteralExpression);
};

class StringLiteral : public LiteralExpression {
public:
  explicit StringLiteral(zc::String value);
  ~StringLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(StringLiteral);

  const zc::StringPtr getValue() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class NumericLiteral : public LiteralExpression {
public:
  explicit NumericLiteral(double value);
  ~NumericLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NumericLiteral);

  double getValue() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BooleanLiteral : public LiteralExpression {
public:
  explicit BooleanLiteral(bool value);
  ~BooleanLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BooleanLiteral);

  bool getValue() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class NilLiteral : public LiteralExpression {
public:
  NilLiteral();
  ~NilLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NilLiteral);
};

class CastExpression : public Expression {
public:
  CastExpression(zc::Own<Expression> expression, zc::String targetType, bool isOptional = false);
  ~CastExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(CastExpression);

  const Expression* getExpression() const;
  zc::StringPtr getTargetType() const;
  bool isOptional() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class AwaitExpression : public Expression {
public:
  explicit AwaitExpression(zc::Own<Expression> expression);
  ~AwaitExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AwaitExpression);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ArrayLiteralExpression : public PrimaryExpression {
public:
  explicit ArrayLiteralExpression(zc::Vector<zc::Own<Expression>>&& elements);
  ~ArrayLiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayLiteralExpression);

  const NodeList<Expression>& getElements() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ObjectLiteralExpression : public PrimaryExpression {
public:
  explicit ObjectLiteralExpression(zc::Vector<zc::Own<Expression>>&& properties);
  ~ObjectLiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ObjectLiteralExpression);

  const NodeList<Expression>& getProperties() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
