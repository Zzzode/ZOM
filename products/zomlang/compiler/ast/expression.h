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

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {
namespace ast {

class AssignmentOperator;
class BinaryOperator;
class UnaryOperator;
class Type;

class Expression : public Node {
public:
  explicit Expression(SyntaxKind kind = SyntaxKind::kExpression) noexcept;
  ~Expression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Expression);

  // Visitor pattern support
  void accept(Visitor& visitor) const;
};

class UnaryExpression : public Expression {
public:
  explicit UnaryExpression(SyntaxKind kind = SyntaxKind::kUnaryExpression) noexcept;
  ~UnaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UnaryExpression);
};

class UpdateExpression : public UnaryExpression {
public:
  UpdateExpression(SyntaxKind kind = SyntaxKind::kUpdateExpression) noexcept;
  ~UpdateExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UpdateExpression);
};

class PrefixUnaryExpression : public UpdateExpression {
public:
  PrefixUnaryExpression(zc::Own<UnaryOperator> op, zc::Own<Expression> operand) noexcept;
  ~PrefixUnaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PrefixUnaryExpression);

  const UnaryOperator* getOperator() const;
  const Expression* getOperand() const;
  bool isPrefix() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class PostfixUnaryExpression : public UpdateExpression {
public:
  PostfixUnaryExpression(zc::Own<UnaryOperator> op, zc::Own<Expression> operand) noexcept;
  ~PostfixUnaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PostfixUnaryExpression);

  const UnaryOperator* getOperator() const;
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

class MemberExpression : public LeftHandSideExpression {
public:
  explicit MemberExpression(SyntaxKind kind = SyntaxKind::kMemberExpression) noexcept;
  ~MemberExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(MemberExpression);
};

class PrimaryExpression : public MemberExpression {
public:
  explicit PrimaryExpression(SyntaxKind kind = SyntaxKind::kPrimaryExpression) noexcept;
  ~PrimaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PrimaryExpression);
};

class Identifier : public PrimaryExpression {
public:
  explicit Identifier(zc::String name) noexcept;
  ~Identifier() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Identifier);

  const zc::StringPtr getName() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class PropertyAccessExpression : public MemberExpression {
public:
  PropertyAccessExpression(zc::Own<LeftHandSideExpression> expression, zc::Own<Identifier> name,
                           bool questionDot = false) noexcept;
  ~PropertyAccessExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PropertyAccessExpression);

  LeftHandSideExpression* getExpression();
  Identifier* getName();
  bool isQuestionDot();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ElementAccessExpression : public MemberExpression {
public:
  explicit ElementAccessExpression(SyntaxKind kind = SyntaxKind::kElementAccessExpression) noexcept;
  ElementAccessExpression(zc::Own<LeftHandSideExpression> expression, zc::Own<Expression> index,
                          bool questionDot = false) noexcept;
  ~ElementAccessExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ElementAccessExpression);

  const LeftHandSideExpression* getExpression();
  const Expression* getIndex();
  bool isQuestionDot();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class NewExpression : public PrimaryExpression {
public:
  NewExpression(zc::Own<Expression> callee, zc::Vector<zc::Own<Expression>>&& arguments) noexcept;
  ~NewExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NewExpression);

  const Expression* getCallee() const;
  const NodeList<Expression>& getArguments() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ParenthesizedExpression : public PrimaryExpression {
public:
  explicit ParenthesizedExpression(zc::Own<Expression> expression) noexcept;
  ~ParenthesizedExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ParenthesizedExpression);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BinaryExpression : public Expression {
public:
  BinaryExpression(zc::Own<Expression> left, zc::Own<BinaryOperator> op,
                   zc::Own<Expression> right) noexcept;
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
                       zc::Own<Expression> right) noexcept;
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
                        zc::Own<Expression> alternate) noexcept;
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
  CallExpression(zc::Own<Expression> callee, zc::Vector<zc::Own<Expression>>&& arguments) noexcept;
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

class LiteralExpression : public PrimaryExpression {
public:
  explicit LiteralExpression(SyntaxKind kind = SyntaxKind::kLiteral) noexcept;
  ~LiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(LiteralExpression);
};

class StringLiteral : public LiteralExpression {
public:
  explicit StringLiteral(zc::String value) noexcept;
  ~StringLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(StringLiteral);

  const zc::StringPtr getValue() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class IntegerLiteral : public LiteralExpression {
public:
  explicit IntegerLiteral(int64_t value) noexcept;
  ~IntegerLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IntegerLiteral);

  int64_t getValue() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class FloatLiteral : public LiteralExpression {
public:
  explicit FloatLiteral(double value) noexcept;
  ~FloatLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FloatLiteral);

  double getValue() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BooleanLiteral : public LiteralExpression {
public:
  explicit BooleanLiteral(bool value) noexcept;
  ~BooleanLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BooleanLiteral);

  bool getValue() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class NullLiteral : public LiteralExpression {
public:
  NullLiteral() noexcept;
  ~NullLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NullLiteral);
};

class CastExpression : public Expression {
public:
  explicit CastExpression(SyntaxKind kind = SyntaxKind::kCastExpression) noexcept;
  ~CastExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(CastExpression);

  virtual const Expression* getExpression() const = 0;
  virtual const Type* getTargetType() const = 0;
};

class AsExpression : public CastExpression {
public:
  AsExpression(zc::Own<Expression> expression, zc::Own<Type> targetType) noexcept;
  ~AsExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AsExpression);

  const Expression* getExpression() const override;
  const Type* getTargetType() const override;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ForcedAsExpression : public CastExpression {
public:
  ForcedAsExpression(zc::Own<Expression> expression, zc::Own<Type> targetType) noexcept;
  ~ForcedAsExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ForcedAsExpression);

  const Expression* getExpression() const override;
  const Type* getTargetType() const override;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ConditionalAsExpression : public CastExpression {
public:
  ConditionalAsExpression(zc::Own<Expression> expression, zc::Own<Type> targetType) noexcept;
  ~ConditionalAsExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ConditionalAsExpression);

  const Expression* getExpression() const override;
  const Type* getTargetType() const override;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class VoidExpression : public UnaryExpression {
public:
  explicit VoidExpression(zc::Own<Expression> expression) noexcept;
  ~VoidExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(VoidExpression);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class TypeOfExpression : public UnaryExpression {
public:
  explicit TypeOfExpression(zc::Own<Expression> expression) noexcept;
  ~TypeOfExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeOfExpression);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class AwaitExpression : public Expression {
public:
  explicit AwaitExpression(zc::Own<Expression> expression) noexcept;
  ~AwaitExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AwaitExpression);

  const Expression* getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class FunctionExpression : public PrimaryExpression {
public:
  FunctionExpression(zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
                     zc::Vector<zc::Own<BindingElement>>&& parameters,
                     zc::Maybe<zc::Own<Type>> returnType, zc::Own<Statement> body);
  ~FunctionExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FunctionExpression);

  const NodeList<TypeParameter>& getTypeParameters() const;
  const NodeList<BindingElement>& getParameters() const;
  const Type* getReturnType() const;
  const Statement* getBody() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ArrayLiteralExpression : public PrimaryExpression {
public:
  explicit ArrayLiteralExpression(zc::Vector<zc::Own<Expression>>&& elements) noexcept;
  ~ArrayLiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayLiteralExpression);

  const NodeList<Expression>& getElements() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ObjectLiteralExpression : public PrimaryExpression {
public:
  explicit ObjectLiteralExpression(zc::Vector<zc::Own<Expression>>&& properties) noexcept;
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
