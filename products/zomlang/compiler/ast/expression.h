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
#include "zomlang/compiler/ast/classof.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Expression : public Node {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Expression);

  /// \brief Virtual destructor for proper RTTI support
  virtual ~Expression() noexcept(false) = default;

  /// \brief Accept a visitor for traversal
  /// This is a pure virtual method that must be implemented by concrete Expression subclasses
  virtual void accept(Visitor& visitor) const = 0;

  /// \brief Get the syntax kind of this expression
  /// This is a pure virtual method that must be implemented by concrete Expression subclasses
  virtual SyntaxKind getKind() const = 0;

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is an Expression or derived class
  GENERATE_CLASSOF_IMPL(Expression)

protected:
  Expression() noexcept = default;
};

class UnaryExpression : public Expression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(UnaryExpression);

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a UnaryExpression or derived class
  GENERATE_CLASSOF_IMPL(UnaryExpression)

protected:
  UnaryExpression() noexcept = default;
};

class UpdateExpression : public UnaryExpression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(UpdateExpression);

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is an UpdateExpression or derived class
  GENERATE_CLASSOF_IMPL(UpdateExpression)

protected:
  UpdateExpression() noexcept = default;
};

class PrefixUnaryExpression final : public UpdateExpression {
public:
  PrefixUnaryExpression(SyntaxKind op, zc::Own<Expression> operand) noexcept;
  ~PrefixUnaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PrefixUnaryExpression);

  SyntaxKind getOperator() const;
  const Expression& getOperand() const;
  bool isPrefix() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class PostfixUnaryExpression final : public UpdateExpression {
public:
  PostfixUnaryExpression(SyntaxKind op, zc::Own<Expression> operand) noexcept;
  ~PostfixUnaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PostfixUnaryExpression);

  SyntaxKind getOperator() const;
  const Expression& getOperand() const;
  bool isPrefix() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class LeftHandSideExpression : public UpdateExpression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(LeftHandSideExpression);

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a LeftHandSideExpression or derived class
  GENERATE_CLASSOF_IMPL(LeftHandSideExpression)

protected:
  LeftHandSideExpression() noexcept = default;
};

class MemberExpression : public LeftHandSideExpression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(MemberExpression);

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a MemberExpression or derived class
  GENERATE_CLASSOF_IMPL(MemberExpression)

protected:
  MemberExpression() noexcept = default;
};

class PrimaryExpression : public MemberExpression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(PrimaryExpression);

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a PrimaryExpression or derived class
  GENERATE_CLASSOF_IMPL(PrimaryExpression)

protected:
  PrimaryExpression() noexcept = default;
};

class Identifier final : public PrimaryExpression {
public:
  explicit Identifier(zc::StringPtr name) noexcept;
  ~Identifier() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Identifier);

  const zc::StringPtr getText() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class PropertyAccessExpression final : public MemberExpression {
public:
  PropertyAccessExpression(zc::Own<LeftHandSideExpression> expression, zc::Own<Identifier> name,
                           bool questionDot = false) noexcept;
  ~PropertyAccessExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PropertyAccessExpression);

  const LeftHandSideExpression& getExpression() const;
  const Identifier& getName() const;
  bool isQuestionDot() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ElementAccessExpression final : public MemberExpression, public Declaration {
public:
  ElementAccessExpression(zc::Own<LeftHandSideExpression> expression, zc::Own<Expression> index,
                          bool questionDot = false) noexcept;
  ~ElementAccessExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ElementAccessExpression);

  const LeftHandSideExpression& getExpression() const;
  const Expression& getIndex() const;
  bool isQuestionDot() const;

  NODE_METHOD_DECLARE();
  DECLARATION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ExpressionWithTypeArguments final : public MemberExpression {
public:
  ExpressionWithTypeArguments(zc::Own<LeftHandSideExpression> expression,
                              zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments) noexcept;
  ~ExpressionWithTypeArguments() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ExpressionWithTypeArguments);

  const LeftHandSideExpression& getExpression() const;
  const NodeList<TypeNode>& getTypeArguments() const;
  zc::Own<LeftHandSideExpression> takeExpression();
  zc::Maybe<zc::Vector<zc::Own<TypeNode>>> takeTypeArguments();

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class NewExpression final : public PrimaryExpression, public Declaration {
public:
  NewExpression(zc::Own<Expression> callee, zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments,
                zc::Maybe<zc::Vector<zc::Own<Expression>>> arguments) noexcept;
  ~NewExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NewExpression);

  const Expression& getCallee() const;
  const zc::Maybe<zc::Vector<zc::Own<Expression>>>& getArguments() const;

  NODE_METHOD_DECLARE();
  DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ParenthesizedExpression final : public PrimaryExpression {
public:
  explicit ParenthesizedExpression(zc::Own<Expression> expression) noexcept;
  ~ParenthesizedExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ParenthesizedExpression);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ThisExpression final : public PrimaryExpression {
public:
  ThisExpression() noexcept;
  ~ThisExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ThisExpression);

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BinaryExpression final : public Expression, public Declaration {
public:
  BinaryExpression(zc::Own<Expression> left, zc::Own<TokenNode> op,
                   zc::Own<Expression> right) noexcept;
  ~BinaryExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BinaryExpression);

  /// \brief Get the left operand
  const Expression& getLeft() const;

  /// \brief Get the operator
  const TokenNode& getOperator() const;

  /// \brief Get the right operand
  const Expression& getRight() const;

  NODE_METHOD_DECLARE();
  DECLARATION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ConditionalExpression final : public Expression {
public:
  ConditionalExpression(zc::Own<Expression> test, zc::Own<Expression> consequent,
                        zc::Own<Expression> alternate) noexcept;
  ConditionalExpression(zc::Own<Expression> test, zc::Maybe<zc::Own<TokenNode>> questionToken,
                        zc::Own<Expression> consequent, zc::Maybe<zc::Own<TokenNode>> colonToken,
                        zc::Own<Expression> alternate) noexcept;
  ~ConditionalExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ConditionalExpression);

  const Expression& getTest() const;
  const Expression& getConsequent() const;
  const Expression& getAlternate() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class CallExpression final : public LeftHandSideExpression {
public:
  CallExpression(zc::Own<Expression> callee, zc::Maybe<zc::Own<TokenNode>> questionDotToken,
                 zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments,
                 zc::Vector<zc::Own<Expression>>&& arguments) noexcept;
  ~CallExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(CallExpression);

  const Expression& getCallee() const;
  const NodeList<Expression>& getArguments() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class LiteralExpression : public PrimaryExpression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(LiteralExpression);

  virtual zc::StringPtr getText() const = 0;

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a LiteralExpression or derived class
  GENERATE_CLASSOF_IMPL(LiteralExpression)

protected:
  LiteralExpression() noexcept = default;
};

class LiteralExpressionImpl {
public:
  LiteralExpressionImpl(zc::StringPtr p) noexcept;
  ~LiteralExpressionImpl() noexcept(false) = default;

  zc::StringPtr getText() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

#define LITERAL_EXPRESSION_METHOD_DECL() zc::StringPtr getText() const override;

class StringLiteral final : public LiteralExpression {
public:
  explicit StringLiteral(zc::StringPtr value) noexcept;
  ~StringLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(StringLiteral);

  const zc::StringPtr getValue() const;

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class IntegerLiteral final : public LiteralExpression {
public:
  explicit IntegerLiteral(int64_t value) noexcept;
  ~IntegerLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IntegerLiteral);

  int64_t getValue() const;

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class FloatLiteral final : public LiteralExpression {
public:
  explicit FloatLiteral(double value) noexcept;
  ~FloatLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FloatLiteral);

  double getValue() const;

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BigIntLiteral final : public LiteralExpression {
public:
  explicit BigIntLiteral(zc::StringPtr text) noexcept;
  ~BigIntLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BigIntLiteral);

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class BooleanLiteral final : public LiteralExpression {
public:
  explicit BooleanLiteral(bool value) noexcept;
  ~BooleanLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BooleanLiteral);

  bool getValue() const;

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class NullLiteral final : public LiteralExpression {
public:
  NullLiteral() noexcept;
  ~NullLiteral() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NullLiteral);

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class TemplateSpan final : public Node {
public:
  TemplateSpan(zc::Own<Expression> expression, zc::Own<StringLiteral> literal) noexcept;
  ~TemplateSpan() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TemplateSpan);

  const Expression& getExpression() const;
  const StringLiteral& getLiteral() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class TemplateLiteralExpression final : public LiteralExpression {
public:
  TemplateLiteralExpression(zc::Own<StringLiteral> head,
                            zc::Vector<zc::Own<TemplateSpan>>&& spans) noexcept;
  ~TemplateLiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TemplateLiteralExpression);

  const StringLiteral& getHead() const;
  const NodeList<TemplateSpan>& getSpans() const;

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class CastExpression : public Expression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(CastExpression);

  /// \brief Get the expression being cast
  virtual const Expression& getExpression() const = 0;

  /// \brief Get the target type
  virtual const TypeNode& getTargetType() const = 0;

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a CastExpression or derived class
  GENERATE_CLASSOF_IMPL(CastExpression)

protected:
  CastExpression() noexcept = default;
};

class CastExpressionImpl {
public:
  CastExpressionImpl(const Expression& expression, const TypeNode& targetType) noexcept;
  ~CastExpressionImpl() noexcept(false) = default;

  const Expression& getExpression() const;
  const TypeNode& getTargetType() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

#define CAST_EXPRESSION_METHOD_DECL()               \
  const Expression& getExpression() const override; \
  const TypeNode& getTargetType() const override;

class AsExpression final : public CastExpression {
public:
  AsExpression(zc::Own<Expression> expression, zc::Own<TypeNode> targetType) noexcept;
  ~AsExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AsExpression);

  NODE_METHOD_DECLARE();
  CAST_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ForcedAsExpression final : public CastExpression {
public:
  ForcedAsExpression(zc::Own<Expression> expression, zc::Own<TypeNode> targetType) noexcept;
  ~ForcedAsExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ForcedAsExpression);

  NODE_METHOD_DECLARE();
  CAST_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ConditionalAsExpression final : public CastExpression {
public:
  ConditionalAsExpression(zc::Own<Expression> expression, zc::Own<TypeNode> targetType) noexcept;
  ~ConditionalAsExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ConditionalAsExpression);

  NODE_METHOD_DECLARE();
  CAST_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class VoidExpression final : public UnaryExpression {
public:
  explicit VoidExpression(zc::Own<Expression> expression) noexcept;
  ~VoidExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(VoidExpression);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class TypeOfExpression final : public UnaryExpression {
public:
  explicit TypeOfExpression(zc::Own<Expression> expression) noexcept;
  ~TypeOfExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeOfExpression);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class AwaitExpression final : public Expression {
public:
  explicit AwaitExpression(zc::Own<Expression> expression) noexcept;
  ~AwaitExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(AwaitExpression);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class NonNullExpression final : public LeftHandSideExpression {
public:
  explicit NonNullExpression(zc::Own<Expression> expression) noexcept;
  ~NonNullExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NonNullExpression);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class CaptureElement final : public Node {
public:
  CaptureElement(bool isByReference, zc::Maybe<zc::Own<Identifier>> identifier,
                 bool isThis) noexcept;
  ~CaptureElement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(CaptureElement);

  bool isByReference() const;
  bool isThis() const;
  zc::Maybe<const Identifier&> getIdentifier() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class FunctionExpression final : public Declaration, public PrimaryExpression {
public:
  FunctionExpression(zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
                     zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
                     zc::Vector<zc::Own<CaptureElement>>&& captures,
                     zc::Maybe<zc::Own<TypeNode>> returnTypeNode, zc::Own<Statement> body) noexcept;
  ~FunctionExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FunctionExpression);

  zc::Maybe<zc::ArrayPtr<const zc::Own<TypeParameterDeclaration>>> getTypeParameters() const;
  const NodeList<ParameterDeclaration>& getParameters() const;
  const NodeList<CaptureElement>& getCaptures() const;
  zc::Maybe<const TypeNode&> getReturnType() const;
  const Statement& getBody() const;

  NODE_METHOD_DECLARE();
  DECLARATION_METHOD_DECL();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class SpreadElement final : public Expression {
public:
  explicit SpreadElement(zc::Own<Expression> expression) noexcept;
  ~SpreadElement() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(SpreadElement);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();
  static bool classof(const Node& node) { return node.getKind() == SyntaxKind::SpreadElement; }

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ArrayLiteralExpression final : public LiteralExpression {
public:
  explicit ArrayLiteralExpression(zc::Vector<zc::Own<Expression>>&& elements,
                                  bool multiLine) noexcept;
  ~ArrayLiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayLiteralExpression);

  const NodeList<Expression>& getElements() const;
  bool isMultiLine() const;

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ObjectLiteralExpression final : public LiteralExpression {
public:
  explicit ObjectLiteralExpression(zc::Vector<zc::Own<ObjectLiteralElement>>&& properties,
                                   bool multiLine) noexcept;
  ~ObjectLiteralExpression() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ObjectLiteralExpression);

  const NodeList<ObjectLiteralElement>& getProperties() const;
  bool isMultiLine() const;

  NODE_METHOD_DECLARE();
  LITERAL_EXPRESSION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class PropertyAssignment final : public ObjectLiteralElement, public Node {
public:
  PropertyAssignment(zc::Own<Identifier> name, zc::Maybe<zc::Own<Expression>> initializer,
                     zc::Maybe<zc::Own<TokenNode>> questionToken) noexcept;
  ~PropertyAssignment() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PropertyAssignment);

  const Identifier& getNameIdentifier() const;
  zc::Maybe<const Expression&> getInitializer() const;
  zc::Maybe<const TokenNode&> getQuestionToken() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ShorthandPropertyAssignment final : public ObjectLiteralElement, public Node {
public:
  ShorthandPropertyAssignment(zc::Own<Identifier> name,
                              zc::Maybe<zc::Own<Expression>> objectAssignmentInitializer,
                              zc::Maybe<zc::Own<TokenNode>> equalsToken) noexcept;
  ~ShorthandPropertyAssignment() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ShorthandPropertyAssignment);

  const Identifier& getNameIdentifier() const;
  zc::Maybe<const Expression&> getObjectAssignmentInitializer() const;
  zc::Maybe<const TokenNode&> getEqualsToken() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class SpreadAssignment final : public ObjectLiteralElement, public Node {
public:
  explicit SpreadAssignment(zc::Own<Expression> expression) noexcept;
  ~SpreadAssignment() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(SpreadAssignment);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();
  NAMED_DECLARATION_METHOD_DECL();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Pattern classes
class Pattern : public Expression {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Pattern);

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a Pattern or derived class
  GENERATE_CLASSOF_IMPL(Pattern)

protected:
  Pattern() noexcept = default;
};

class PrimaryPattern : public Pattern {
public:
  ZC_DISALLOW_COPY_AND_MOVE(PrimaryPattern);

  /// \brief LLVM-style RTTI support
  /// \param node The node to check
  /// \return true if the node is a PrimaryPattern or derived class
  GENERATE_CLASSOF_IMPL(PrimaryPattern)

protected:
  PrimaryPattern() noexcept = default;
};

class WildcardPattern final : public PrimaryPattern {
public:
  explicit WildcardPattern(zc::Maybe<zc::Own<TypeNode>> typeAnnotation = zc::none) noexcept;
  ~WildcardPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(WildcardPattern);

  zc::Maybe<const TypeNode&> getTypeAnnotation() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class IdentifierPattern final : public PrimaryPattern {
public:
  IdentifierPattern(zc::Own<Identifier> identifier,
                    zc::Maybe<zc::Own<TypeNode>> typeAnnotation = zc::none) noexcept;
  ~IdentifierPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IdentifierPattern);

  const Identifier& getIdentifier() const;
  zc::Maybe<const TypeNode&> getTypeAnnotation() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class TuplePattern final : public PrimaryPattern {
public:
  explicit TuplePattern(zc::Vector<zc::Own<Pattern>>&& elements) noexcept;
  ~TuplePattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TuplePattern);

  const NodeList<Pattern>& getElements() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class StructurePattern final : public PrimaryPattern {
public:
  explicit StructurePattern(zc::Vector<zc::Own<Pattern>>&& properties) noexcept;
  ~StructurePattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(StructurePattern);

  const NodeList<Pattern>& getProperties() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ArrayPattern final : public PrimaryPattern {
public:
  explicit ArrayPattern(zc::Vector<zc::Own<Pattern>>&& elements) noexcept;
  ~ArrayPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayPattern);

  const NodeList<Pattern>& getElements() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class IsPattern final : public PrimaryPattern {
public:
  explicit IsPattern(zc::Own<TypeNode> type) noexcept;
  ~IsPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IsPattern);

  const TypeNode& getType() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class PatternProperty final : public Pattern {
public:
  PatternProperty(zc::Own<Identifier> name,
                  zc::Maybe<zc::Own<Pattern>> pattern = zc::none) noexcept;
  ~PatternProperty() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PatternProperty);

  const Identifier& getName() const;
  zc::Maybe<const Pattern&> getPattern() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class ExpressionPattern final : public PrimaryPattern {
public:
  explicit ExpressionPattern(zc::Own<Expression> expression) noexcept;
  ~ExpressionPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ExpressionPattern);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

/// \brief Represents an enum pattern.
///
/// Enum patterns match against enum values.
/// \code
/// enum MyEnum {
///   Value1(i32, i32),
///   Value2(i32),
/// }
/// let x = MyEnum::Value1(1, 2);
/// match x {
///   when MyEnum.Value1(a, b) => {
///     // a is 1, b is 2
///   }
/// }
/// \endcode
class EnumPattern final : public PrimaryPattern {
public:
  EnumPattern(zc::Maybe<zc::Own<TypeNode>> typeReference, zc::Own<Identifier> propertyName,
              zc::Own<TuplePattern> tuplePattern) noexcept;
  ~EnumPattern() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(EnumPattern);

  zc::Maybe<const TypeNode&> getTypeReference() const;
  const Identifier& getPropertyName() const;
  const TuplePattern& getTuplePattern() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
