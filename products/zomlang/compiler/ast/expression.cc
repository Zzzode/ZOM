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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// LiteralExpressionImpl
struct LiteralExpressionImpl::Impl {
  zc::String text;

  explicit Impl(zc::String t) : text(zc::mv(t)) {}
};

LiteralExpressionImpl::LiteralExpressionImpl(zc::StringPtr p) noexcept
    : impl(zc::heap<Impl>(zc::str(p))) {}

zc::StringPtr LiteralExpressionImpl::getText() const { return impl->text; }

// ================================================================================
// CastExpressionImpl
struct CastExpressionImpl::Impl {
  const Expression& expression;
  const TypeNode& targetType;

  Impl(const Expression& e, const TypeNode& t) : expression(e), targetType(t) {}
};

CastExpressionImpl::CastExpressionImpl(const Expression& expression,
                                       const TypeNode& targetType) noexcept
    : impl(zc::heap<Impl>(expression, targetType)) {}

const Expression& CastExpressionImpl::getExpression() const { return impl->expression; }

const TypeNode& CastExpressionImpl::getTargetType() const { return impl->targetType; }

// ================================================================================
// PrefixUnaryExpression
struct PrefixUnaryExpression::Impl : private NodeImpl {
  const SyntaxKind op;
  const zc::Own<Expression> operand;

  Impl(SyntaxKind o, zc::Own<Expression> operand)
      : NodeImpl(SyntaxKind::PrefixUnaryExpression), op(o), operand(zc::mv(operand)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

PrefixUnaryExpression::PrefixUnaryExpression(SyntaxKind op, zc::Own<Expression> operand) noexcept
    : impl(zc::heap<Impl>(zc::mv(op), zc::mv(operand))) {}

PrefixUnaryExpression::~PrefixUnaryExpression() noexcept(false) = default;

SyntaxKind PrefixUnaryExpression::getOperator() const { return impl->op; }

const Expression& PrefixUnaryExpression::getOperand() const { return *impl->operand; }

bool PrefixUnaryExpression::isPrefix() const { return true; }

SyntaxKind PrefixUnaryExpression::getKind() const { return impl->getKind(); }

NodeFlags PrefixUnaryExpression::getFlags() const { return impl->getFlags(); }
void PrefixUnaryExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void PrefixUnaryExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& PrefixUnaryExpression::getSourceRange() const {
  return impl->getSourceRange();
}

void PrefixUnaryExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// PostfixUnaryExpression
struct PostfixUnaryExpression::Impl : private NodeImpl {
  const SyntaxKind op;
  const zc::Own<Expression> operand;

  Impl(SyntaxKind o, zc::Own<Expression> operand)
      : NodeImpl(SyntaxKind::PostfixUnaryExpression), op(o), operand(zc::mv(operand)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

PostfixUnaryExpression::PostfixUnaryExpression(SyntaxKind op, zc::Own<Expression> operand) noexcept
    : impl(zc::heap<Impl>(zc::mv(op), zc::mv(operand))) {}

PostfixUnaryExpression::~PostfixUnaryExpression() noexcept(false) = default;

SyntaxKind PostfixUnaryExpression::getOperator() const { return impl->op; }

const Expression& PostfixUnaryExpression::getOperand() const { return *impl->operand; }

bool PostfixUnaryExpression::isPrefix() const { return false; }

SyntaxKind PostfixUnaryExpression::getKind() const { return impl->getKind(); }

NodeFlags PostfixUnaryExpression::getFlags() const { return impl->getFlags(); }
void PostfixUnaryExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void PostfixUnaryExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& PostfixUnaryExpression::getSourceRange() const {
  return impl->getSourceRange();
}

void PostfixUnaryExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// Identifier
struct Identifier::Impl : private NodeImpl {
  const zc::StringPtr name;
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;

  explicit Impl(zc::StringPtr n) : NodeImpl(SyntaxKind::Identifier), name(zc::mv(n)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

Identifier::Identifier(zc::StringPtr name) noexcept : impl(zc::heap<Impl>(zc::mv(name))) {}

Identifier::~Identifier() noexcept(false) = default;

const zc::StringPtr Identifier::getText() const { return impl->name; }

SyntaxKind Identifier::getKind() const { return impl->getKind(); }

NodeFlags Identifier::getFlags() const { return impl->getFlags(); }
void Identifier::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void Identifier::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& Identifier::getSourceRange() const { return impl->getSourceRange(); }

void Identifier::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// PropertyAccessExpression
struct PropertyAccessExpression::Impl : private NodeImpl {
  const zc::Own<LeftHandSideExpression> expression;
  const zc::Own<Identifier> name;
  const bool questionDot;

  Impl(zc::Own<LeftHandSideExpression> e, zc::Own<Identifier> n, bool q)
      : NodeImpl(SyntaxKind::PropertyAccessExpression),
        expression(zc::mv(e)),
        name(zc::mv(n)),
        questionDot(q) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

PropertyAccessExpression::PropertyAccessExpression(zc::Own<LeftHandSideExpression> expression,
                                                   zc::Own<Identifier> name,
                                                   bool questionDot) noexcept
    : MemberExpression(), impl(zc::heap<Impl>(zc::mv(expression), zc::mv(name), questionDot)) {}

PropertyAccessExpression::~PropertyAccessExpression() noexcept(false) = default;

const LeftHandSideExpression& PropertyAccessExpression::getExpression() const {
  return *impl->expression;
}

bool PropertyAccessExpression::isQuestionDot() const { return impl->questionDot; }

SyntaxKind PropertyAccessExpression::getKind() const { return impl->getKind(); }

NodeFlags PropertyAccessExpression::getFlags() const { return impl->getFlags(); }
void PropertyAccessExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void PropertyAccessExpression::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& PropertyAccessExpression::getSourceRange() const {
  return impl->getSourceRange();
}

void PropertyAccessExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

const Identifier& PropertyAccessExpression::getName() const { return *impl->name; }

// ================================================================================
// ElementAccessExpression
struct ElementAccessExpression::Impl : private NodeImpl {
  const zc::Own<LeftHandSideExpression> expression;
  const zc::Own<Expression> index;
  const bool questionDot;
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;

  Impl(zc::Own<LeftHandSideExpression> e, zc::Own<Expression> i, bool q)
      : NodeImpl(SyntaxKind::ElementAccessExpression),
        expression(zc::mv(e)),
        index(zc::mv(i)),
        questionDot(q) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;

  zc::Maybe<const symbol::Symbol&> getSymbol() const { return symbol; }
  void setSymbol(zc::Maybe<const symbol::Symbol&> s) { symbol = s; }
};

ElementAccessExpression::ElementAccessExpression(zc::Own<LeftHandSideExpression> expression,
                                                 zc::Own<Expression> index,
                                                 bool questionDot) noexcept
    : MemberExpression(),
      Declaration(),
      impl(zc::heap<Impl>(zc::mv(expression), zc::mv(index), questionDot)) {}

ElementAccessExpression::~ElementAccessExpression() noexcept(false) = default;

const LeftHandSideExpression& ElementAccessExpression::getExpression() const {
  return *impl->expression;
}

const Expression& ElementAccessExpression::getIndex() const { return *impl->index; }

bool ElementAccessExpression::isQuestionDot() const { return impl->questionDot; }

SyntaxKind ElementAccessExpression::getKind() const { return impl->getKind(); }

NodeFlags ElementAccessExpression::getFlags() const { return impl->getFlags(); }
void ElementAccessExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ElementAccessExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

void ElementAccessExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ElementAccessExpression::getSourceRange() const {
  return impl->getSourceRange();
}

zc::Maybe<const symbol::Symbol&> ElementAccessExpression::getSymbol() const {
  return impl->getSymbol();
}

void ElementAccessExpression::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  const_cast<Impl*>(impl.get())->setSymbol(symbol);
}

// ================================================================================
// CaptureElement
struct CaptureElement::Impl : private NodeImpl {
  const bool isByReference;
  const bool isThis;
  const zc::Maybe<zc::Own<Identifier>> identifier;

  Impl(bool ref, zc::Maybe<zc::Own<Identifier>> id, bool ths)
      : NodeImpl(SyntaxKind::CaptureElement),
        isByReference(ref),
        isThis(ths),
        identifier(zc::mv(id)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

CaptureElement::CaptureElement(bool isByReference, zc::Maybe<zc::Own<Identifier>> identifier,
                               bool isThis) noexcept
    : Node(), impl(zc::heap<Impl>(isByReference, zc::mv(identifier), isThis)) {}

CaptureElement::~CaptureElement() noexcept(false) = default;

bool CaptureElement::isByReference() const { return impl->isByReference; }

bool CaptureElement::isThis() const { return impl->isThis; }

zc::Maybe<const Identifier&> CaptureElement::getIdentifier() const { return impl->identifier; }

void CaptureElement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& CaptureElement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind CaptureElement::getKind() const { return impl->getKind(); }

NodeFlags CaptureElement::getFlags() const { return impl->getFlags(); }
void CaptureElement::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void CaptureElement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// FunctionExpression
struct FunctionExpression::Impl : private NodeImpl {
  const zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const NodeList<CaptureElement> captures;
  const zc::Maybe<zc::Own<TypeNode>> returnType;
  const zc::Own<Statement> body;
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;

  Impl(zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Vector<zc::Own<ParameterDeclaration>>&& p, zc::Vector<zc::Own<CaptureElement>>&& c,
       zc::Maybe<zc::Own<TypeNode>> rt, zc::Own<Statement> b)
      : NodeImpl(SyntaxKind::FunctionExpression),
        typeParameters(zc::mv(tp)),
        parameters(NodeList<ParameterDeclaration>(zc::mv(p))),
        captures(NodeList<CaptureElement>(zc::mv(c))),
        returnType(zc::mv(rt)),
        body(zc::mv(b)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;

  zc::Maybe<const symbol::Symbol&> getSymbol() const { return symbol; }
  void setSymbol(zc::Maybe<const symbol::Symbol&> s) { symbol = s; }
};

FunctionExpression::FunctionExpression(
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Vector<zc::Own<CaptureElement>>&& captures, zc::Maybe<zc::Own<TypeNode>> returnType,
    zc::Own<Statement> body) noexcept
    : impl(zc::heap<Impl>(zc::mv(typeParameters), zc::mv(parameters), zc::mv(captures),
                          zc::mv(returnType), zc::mv(body))) {}

FunctionExpression::~FunctionExpression() noexcept(false) = default;

zc::Maybe<zc::ArrayPtr<const zc::Own<TypeParameterDeclaration>>>
FunctionExpression::getTypeParameters() const {
  return impl->typeParameters.map([](auto& typeParameters) { return typeParameters.asPtr(); });
}

const NodeList<ParameterDeclaration>& FunctionExpression::getParameters() const {
  return impl->parameters;
}

const NodeList<CaptureElement>& FunctionExpression::getCaptures() const { return impl->captures; }

zc::Maybe<const TypeNode&> FunctionExpression::getReturnType() const { return impl->returnType; }

const Statement& FunctionExpression::getBody() const { return *impl->body; }

void FunctionExpression::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& FunctionExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind FunctionExpression::getKind() const { return impl->getKind(); }

NodeFlags FunctionExpression::getFlags() const { return impl->getFlags(); }
void FunctionExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void FunctionExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> FunctionExpression::getSymbol() const { return impl->getSymbol(); }

void FunctionExpression::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

// ================================================================================
// WildcardPattern

struct WildcardPattern::Impl : private NodeImpl {
  zc::Maybe<zc::Own<TypeNode>> typeAnnotation;

  explicit Impl(zc::Maybe<zc::Own<TypeNode>> typeAnnotation)
      : NodeImpl(SyntaxKind::WildcardPattern), typeAnnotation(zc::mv(typeAnnotation)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

WildcardPattern::WildcardPattern(zc::Maybe<zc::Own<TypeNode>> typeAnnotation) noexcept
    : impl(zc::heap<Impl>(zc::mv(typeAnnotation))) {}

WildcardPattern::~WildcardPattern() noexcept(false) = default;

zc::Maybe<const TypeNode&> WildcardPattern::getTypeAnnotation() const {
  return impl->typeAnnotation;
}

void WildcardPattern::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& WildcardPattern::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind WildcardPattern::getKind() const { return impl->getKind(); }

NodeFlags WildcardPattern::getFlags() const { return impl->getFlags(); }
void WildcardPattern::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void WildcardPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// IdentifierPattern

struct IdentifierPattern::Impl : private NodeImpl {
  zc::Own<Identifier> identifier;
  zc::Maybe<zc::Own<TypeNode>> typeAnnotation;

  Impl(zc::Own<Identifier> identifier, zc::Maybe<zc::Own<TypeNode>> typeAnnotation)
      : NodeImpl(SyntaxKind::IdentifierPattern),
        identifier(zc::mv(identifier)),
        typeAnnotation(zc::mv(typeAnnotation)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

IdentifierPattern::IdentifierPattern(zc::Own<Identifier> identifier,
                                     zc::Maybe<zc::Own<TypeNode>> typeAnnotation) noexcept
    : impl(zc::heap<Impl>(zc::mv(identifier), zc::mv(typeAnnotation))) {}

IdentifierPattern::~IdentifierPattern() noexcept(false) = default;

const Identifier& IdentifierPattern::getIdentifier() const { return *impl->identifier; }

zc::Maybe<const TypeNode&> IdentifierPattern::getTypeAnnotation() const {
  return impl->typeAnnotation;
}

void IdentifierPattern::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& IdentifierPattern::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind IdentifierPattern::getKind() const { return impl->getKind(); }

NodeFlags IdentifierPattern::getFlags() const { return impl->getFlags(); }
void IdentifierPattern::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void IdentifierPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// TuplePattern

struct TuplePattern::Impl : private NodeImpl {
  NodeList<Pattern> elements;

  explicit Impl(zc::Vector<zc::Own<Pattern>>&& elements)
      : NodeImpl(SyntaxKind::TuplePattern), elements(zc::mv(elements)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

TuplePattern::TuplePattern(zc::Vector<zc::Own<Pattern>>&& elements) noexcept
    : impl(zc::heap<Impl>(zc::mv(elements))) {}

TuplePattern::~TuplePattern() noexcept(false) = default;

const NodeList<Pattern>& TuplePattern::getElements() const { return impl->elements; }

void TuplePattern::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TuplePattern::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind TuplePattern::getKind() const { return impl->getKind(); }

NodeFlags TuplePattern::getFlags() const { return impl->getFlags(); }
void TuplePattern::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void TuplePattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// StructurePattern

struct StructurePattern::Impl : private NodeImpl {
  NodeList<Pattern> properties;

  explicit Impl(zc::Vector<zc::Own<Pattern>>&& properties)
      : NodeImpl(SyntaxKind::StructurePattern), properties(zc::mv(properties)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

StructurePattern::StructurePattern(zc::Vector<zc::Own<Pattern>>&& properties) noexcept
    : impl(zc::heap<Impl>(zc::mv(properties))) {}

StructurePattern::~StructurePattern() noexcept(false) = default;

const NodeList<Pattern>& StructurePattern::getProperties() const { return impl->properties; }

void StructurePattern::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& StructurePattern::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind StructurePattern::getKind() const { return impl->getKind(); }

NodeFlags StructurePattern::getFlags() const { return impl->getFlags(); }
void StructurePattern::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void StructurePattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ArrayPattern

struct ArrayPattern::Impl : private NodeImpl {
  NodeList<Pattern> elements;

  explicit Impl(zc::Vector<zc::Own<Pattern>>&& elements)
      : NodeImpl(SyntaxKind::ArrayPattern), elements(zc::mv(elements)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ArrayPattern::ArrayPattern(zc::Vector<zc::Own<Pattern>>&& elements) noexcept
    : impl(zc::heap<Impl>(zc::mv(elements))) {}

ArrayPattern::~ArrayPattern() noexcept(false) = default;

const NodeList<Pattern>& ArrayPattern::getElements() const { return impl->elements; }

void ArrayPattern::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ArrayPattern::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind ArrayPattern::getKind() const { return impl->getKind(); }

NodeFlags ArrayPattern::getFlags() const { return impl->getFlags(); }
void ArrayPattern::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void ArrayPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// IsPattern

struct IsPattern::Impl : private NodeImpl {
  zc::Own<TypeNode> type;

  explicit Impl(zc::Own<TypeNode> type) : NodeImpl(SyntaxKind::IsPattern), type(zc::mv(type)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

IsPattern::IsPattern(zc::Own<TypeNode> type) noexcept : impl(zc::heap<Impl>(zc::mv(type))) {}

IsPattern::~IsPattern() noexcept(false) = default;

const TypeNode& IsPattern::getType() const { return *impl->type; }

void IsPattern::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& IsPattern::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind IsPattern::getKind() const { return impl->getKind(); }

NodeFlags IsPattern::getFlags() const { return impl->getFlags(); }
void IsPattern::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void IsPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// PatternProperty

struct PatternProperty::Impl : private NodeImpl {
  const zc::Own<Identifier> name;
  const zc::Maybe<zc::Own<Pattern>> pattern;

  Impl(zc::Own<Identifier> name, zc::Maybe<zc::Own<Pattern>> pattern)
      : NodeImpl(SyntaxKind::PatternProperty), name(zc::mv(name)), pattern(zc::mv(pattern)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

PatternProperty::PatternProperty(zc::Own<Identifier> name,
                                 zc::Maybe<zc::Own<Pattern>> pattern) noexcept
    : impl(zc::heap<Impl>(zc::mv(name), zc::mv(pattern))) {}

PatternProperty::~PatternProperty() noexcept(false) = default;

const Identifier& PatternProperty::getName() const { return *impl->name; }

zc::Maybe<const Pattern&> PatternProperty::getPattern() const {
  ZC_IF_SOME(pattern, impl->pattern) { return *pattern; }
  return zc::none;
}

void PatternProperty::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& PatternProperty::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind PatternProperty::getKind() const { return impl->getKind(); }

NodeFlags PatternProperty::getFlags() const { return impl->getFlags(); }
void PatternProperty::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void PatternProperty::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ExpressionPattern

struct ExpressionPattern::Impl : private NodeImpl {
  zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expression)
      : NodeImpl(SyntaxKind::ExpressionPattern), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ExpressionPattern::ExpressionPattern(zc::Own<Expression> expression) noexcept
    : impl(zc::heap<Impl>(zc::mv(expression))) {}

ExpressionPattern::~ExpressionPattern() noexcept(false) = default;

const Expression& ExpressionPattern::getExpression() const { return *impl->expression; }

void ExpressionPattern::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ExpressionPattern::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ExpressionPattern::getKind() const { return impl->getKind(); }

NodeFlags ExpressionPattern::getFlags() const { return impl->getFlags(); }
void ExpressionPattern::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ExpressionPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// EnumPattern

struct EnumPattern::Impl : private NodeImpl {
  zc::Maybe<zc::Own<TypeNode>> typeReference;
  zc::Own<Identifier> propertyName;
  zc::Own<TuplePattern> tuplePattern;

  Impl(zc::Maybe<zc::Own<TypeNode>> typeReference, zc::Own<Identifier> propertyName,
       zc::Own<TuplePattern> tuplePattern)
      : NodeImpl(SyntaxKind::EnumPattern),
        typeReference(zc::mv(typeReference)),
        propertyName(zc::mv(propertyName)),
        tuplePattern(zc::mv(tuplePattern)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

EnumPattern::EnumPattern(zc::Maybe<zc::Own<TypeNode>> typeReference,
                         zc::Own<Identifier> propertyName,
                         zc::Own<TuplePattern> tuplePattern) noexcept
    : impl(zc::heap<Impl>(zc::mv(typeReference), zc::mv(propertyName), zc::mv(tuplePattern))) {}

EnumPattern::~EnumPattern() noexcept(false) = default;

zc::Maybe<const TypeNode&> EnumPattern::getTypeReference() const { return impl->typeReference; }

const Identifier& EnumPattern::getPropertyName() const { return *impl->propertyName; }

const TuplePattern& EnumPattern::getTuplePattern() const { return *impl->tuplePattern; }

void EnumPattern::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& EnumPattern::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind EnumPattern::getKind() const { return impl->getKind(); }

NodeFlags EnumPattern::getFlags() const { return impl->getFlags(); }
void EnumPattern::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void EnumPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// NewExpression

struct NewExpression::Impl : private NodeImpl {
  zc::Own<Expression> callee;
  zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments;
  zc::Maybe<zc::Vector<zc::Own<Expression>>> arguments;
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;

  Impl(zc::Own<Expression> callee, zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments,
       zc::Maybe<zc::Vector<zc::Own<Expression>>> arguments) noexcept
      : NodeImpl(SyntaxKind::NewExpression),
        callee(zc::mv(callee)),
        typeArguments(zc::mv(typeArguments)),
        arguments(zc::mv(arguments)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;

  zc::Maybe<const symbol::Symbol&> getSymbol() const { return symbol; }
  void setSymbol(zc::Maybe<const symbol::Symbol&> s) { symbol = s; }
};

NewExpression::NewExpression(zc::Own<Expression> callee,
                             zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments,
                             zc::Maybe<zc::Vector<zc::Own<Expression>>> arguments) noexcept
    : PrimaryExpression(),
      Declaration(),
      impl(zc::heap<Impl>(zc::mv(callee), zc::mv(typeArguments), zc::mv(arguments))) {}

NewExpression::~NewExpression() noexcept(false) = default;

const Expression& NewExpression::getCallee() const { return *impl->callee; }

const zc::Maybe<zc::Vector<zc::Own<Expression>>>& NewExpression::getArguments() const {
  return impl->arguments;
}

void NewExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& NewExpression::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind NewExpression::getKind() const { return impl->getKind(); }

NodeFlags NewExpression::getFlags() const { return impl->getFlags(); }
void NewExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void NewExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> NewExpression::getSymbol() const { return impl->getSymbol(); }

void NewExpression::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) { impl->setSymbol(symbol); }

// ================================================================================
// ThisExpression

struct ThisExpression::Impl : private NodeImpl {
  Impl() : NodeImpl(SyntaxKind::ThisExpression) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ThisExpression::ThisExpression() noexcept : impl(zc::heap<Impl>()) {}

ThisExpression::~ThisExpression() noexcept(false) = default;

SyntaxKind ThisExpression::getKind() const { return impl->getKind(); }

NodeFlags ThisExpression::getFlags() const { return impl->getFlags(); }
void ThisExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void ThisExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ThisExpression::getSourceRange() const { return impl->getSourceRange(); }

void ThisExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// BinaryExpression

struct BinaryExpression::Impl : private NodeImpl {
  zc::Own<Expression> left;
  zc::Own<TokenNode> op;
  zc::Own<Expression> right;
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;

  Impl(zc::Own<Expression> left, zc::Own<TokenNode> op, zc::Own<Expression> right) noexcept
      : NodeImpl(SyntaxKind::BinaryExpression),
        left(zc::mv(left)),
        op(zc::mv(op)),
        right(zc::mv(right)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;

  zc::Maybe<const symbol::Symbol&> getSymbol() const { return symbol; }
  void setSymbol(zc::Maybe<const symbol::Symbol&> s) { symbol = s; }
};

BinaryExpression::BinaryExpression(zc::Own<Expression> left, zc::Own<TokenNode> op,
                                   zc::Own<Expression> right) noexcept
    : Expression(), Declaration(), impl(zc::heap<Impl>(zc::mv(left), zc::mv(op), zc::mv(right))) {}

BinaryExpression::~BinaryExpression() noexcept(false) = default;

const Expression& BinaryExpression::getLeft() const { return *impl->left; }

const TokenNode& BinaryExpression::getOperator() const { return *impl->op; }

const Expression& BinaryExpression::getRight() const { return *impl->right; }

void BinaryExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& BinaryExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind BinaryExpression::getKind() const { return impl->getKind(); }

NodeFlags BinaryExpression::getFlags() const { return impl->getFlags(); }
void BinaryExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void BinaryExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> BinaryExpression::getSymbol() const { return impl->getSymbol(); }

void BinaryExpression::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  const_cast<Impl*>(impl.get())->setSymbol(symbol);
}

// ================================================================================
// ConditionalExpression

struct ConditionalExpression::Impl : private NodeImpl {
  zc::Own<Expression> test;
  zc::Maybe<zc::Own<TokenNode>> questionToken;
  zc::Own<Expression> consequent;
  zc::Maybe<zc::Own<TokenNode>> colonToken;
  zc::Own<Expression> alternate;

  Impl(zc::Own<Expression> test, zc::Own<Expression> consequent,
       zc::Own<Expression> alternate) noexcept
      : NodeImpl(SyntaxKind::ConditionalExpression),
        test(zc::mv(test)),
        questionToken(zc::none),
        consequent(zc::mv(consequent)),
        colonToken(zc::none),
        alternate(zc::mv(alternate)) {}

  Impl(zc::Own<Expression> test, zc::Maybe<zc::Own<TokenNode>> questionToken,
       zc::Own<Expression> consequent, zc::Maybe<zc::Own<TokenNode>> colonToken,
       zc::Own<Expression> alternate) noexcept
      : NodeImpl(SyntaxKind::ConditionalExpression),
        test(zc::mv(test)),
        questionToken(zc::mv(questionToken)),
        consequent(zc::mv(consequent)),
        colonToken(zc::mv(colonToken)),
        alternate(zc::mv(alternate)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ConditionalExpression::ConditionalExpression(zc::Own<Expression> test,
                                             zc::Own<Expression> consequent,
                                             zc::Own<Expression> alternate) noexcept
    : Expression(), impl(zc::heap<Impl>(zc::mv(test), zc::mv(consequent), zc::mv(alternate))) {}

ConditionalExpression::ConditionalExpression(zc::Own<Expression> test,
                                             zc::Maybe<zc::Own<TokenNode>> questionToken,
                                             zc::Own<Expression> consequent,
                                             zc::Maybe<zc::Own<TokenNode>> colonToken,
                                             zc::Own<Expression> alternate) noexcept
    : Expression(),
      impl(zc::heap<Impl>(zc::mv(test), zc::mv(questionToken), zc::mv(consequent),
                          zc::mv(colonToken), zc::mv(alternate))) {}

ConditionalExpression::~ConditionalExpression() noexcept(false) = default;

const Expression& ConditionalExpression::getTest() const { return *impl->test; }

const Expression& ConditionalExpression::getConsequent() const { return *impl->consequent; }

const Expression& ConditionalExpression::getAlternate() const { return *impl->alternate; }

void ConditionalExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ConditionalExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ConditionalExpression::getKind() const { return impl->getKind(); }

NodeFlags ConditionalExpression::getFlags() const { return impl->getFlags(); }
void ConditionalExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ConditionalExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// CallExpression

struct CallExpression::Impl : private NodeImpl {
  zc::Own<Expression> callee;
  zc::Maybe<zc::Own<TokenNode>> questionDotToken;
  zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments;
  NodeList<Expression> arguments;

  Impl(zc::Own<Expression> callee, zc::Maybe<zc::Own<TokenNode>> questionDotToken,
       zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments,
       zc::Vector<zc::Own<Expression>>&& arguments) noexcept
      : NodeImpl(SyntaxKind::CallExpression),
        callee(zc::mv(callee)),
        questionDotToken(zc::mv(questionDotToken)),
        typeArguments(zc::mv(typeArguments)),
        arguments(zc::mv(arguments)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

CallExpression::CallExpression(zc::Own<Expression> callee,
                               zc::Maybe<zc::Own<TokenNode>> questionDotToken,
                               zc::Maybe<zc::Vector<zc::Own<ast::TypeNode>>> typeArguments,
                               zc::Vector<zc::Own<Expression>>&& arguments) noexcept
    : LeftHandSideExpression(),
      impl(zc::heap<Impl>(zc::mv(callee), zc::mv(questionDotToken), zc::mv(typeArguments),
                          zc::mv(arguments))) {}

CallExpression::~CallExpression() noexcept(false) = default;

const Expression& CallExpression::getCallee() const { return *impl->callee; }

const NodeList<Expression>& CallExpression::getArguments() const { return impl->arguments; }

void CallExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& CallExpression::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind CallExpression::getKind() const { return impl->getKind(); }

NodeFlags CallExpression::getFlags() const { return impl->getFlags(); }
void CallExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void CallExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ParenthesizedExpression

struct ParenthesizedExpression::Impl : private NodeImpl {
  zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expression) noexcept
      : NodeImpl(SyntaxKind::ParenthesizedExpression), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ParenthesizedExpression::ParenthesizedExpression(zc::Own<Expression> expression) noexcept
    : PrimaryExpression(), impl(zc::heap<Impl>(zc::mv(expression))) {}

ParenthesizedExpression::~ParenthesizedExpression() noexcept(false) = default;

const Expression& ParenthesizedExpression::getExpression() const { return *impl->expression; }

void ParenthesizedExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ParenthesizedExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ParenthesizedExpression::getKind() const { return impl->getKind(); }

NodeFlags ParenthesizedExpression::getFlags() const { return impl->getFlags(); }
void ParenthesizedExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ParenthesizedExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// SpreadElement

struct SpreadElement::Impl : private NodeImpl {
  const zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expression)
      : NodeImpl(SyntaxKind::SpreadElement), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

SpreadElement::SpreadElement(zc::Own<Expression> expression) noexcept
    : impl(zc::heap<Impl>(zc::mv(expression))) {}

SpreadElement::~SpreadElement() noexcept(false) = default;

const Expression& SpreadElement::getExpression() const { return *impl->expression; }

void SpreadElement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& SpreadElement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind SpreadElement::getKind() const { return impl->getKind(); }

NodeFlags SpreadElement::getFlags() const { return impl->getFlags(); }
void SpreadElement::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void SpreadElement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ArrayLiteralExpression

struct ArrayLiteralExpression::Impl : private NodeImpl, public LiteralExpressionImpl {
  NodeList<Expression> elements;
  bool multiLine;

  explicit Impl(zc::Vector<zc::Own<Expression>>&& elements, bool multiLine) noexcept
      : NodeImpl(SyntaxKind::ArrayLiteralExpression),
        LiteralExpressionImpl("[]"),
        elements(zc::mv(elements)),
        multiLine(multiLine) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ArrayLiteralExpression::ArrayLiteralExpression(zc::Vector<zc::Own<Expression>>&& elements,
                                               bool multiLine) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(zc::mv(elements), multiLine)) {}

ArrayLiteralExpression::~ArrayLiteralExpression() noexcept(false) = default;

const NodeList<Expression>& ArrayLiteralExpression::getElements() const { return impl->elements; }

bool ArrayLiteralExpression::isMultiLine() const { return impl->multiLine; }

zc::StringPtr ArrayLiteralExpression::getText() const { return impl->getText(); }

void ArrayLiteralExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ArrayLiteralExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ArrayLiteralExpression::getKind() const { return impl->getKind(); }

NodeFlags ArrayLiteralExpression::getFlags() const { return impl->getFlags(); }
void ArrayLiteralExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ArrayLiteralExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ObjectLiteralExpression

struct ObjectLiteralExpression::Impl : private NodeImpl, public LiteralExpressionImpl {
  NodeList<ObjectLiteralElement> properties;
  bool multiLine;

  explicit Impl(zc::Vector<zc::Own<ObjectLiteralElement>>&& properties, bool multiLine) noexcept
      : NodeImpl(SyntaxKind::ObjectLiteralExpression),
        LiteralExpressionImpl("{}"),
        properties(zc::mv(properties)),
        multiLine(multiLine) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ObjectLiteralExpression::ObjectLiteralExpression(
    zc::Vector<zc::Own<ObjectLiteralElement>>&& properties, bool multiLine) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(zc::mv(properties), multiLine)) {}

ObjectLiteralExpression::~ObjectLiteralExpression() noexcept(false) = default;

const NodeList<ObjectLiteralElement>& ObjectLiteralExpression::getProperties() const {
  return impl->properties;
}

bool ObjectLiteralExpression::isMultiLine() const { return impl->multiLine; }

zc::StringPtr ObjectLiteralExpression::getText() const { return impl->getText(); }

void ObjectLiteralExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ObjectLiteralExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ObjectLiteralExpression::getKind() const { return impl->getKind(); }

NodeFlags ObjectLiteralExpression::getFlags() const { return impl->getFlags(); }
void ObjectLiteralExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ObjectLiteralExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// StringLiteral
struct StringLiteral::Impl : private NodeImpl, public LiteralExpressionImpl {
  explicit Impl(zc::StringPtr v)
      : NodeImpl(SyntaxKind::StringLiteral), LiteralExpressionImpl(zc::mv(v)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

StringLiteral::StringLiteral(zc::StringPtr value) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(zc::mv(value))) {}

StringLiteral::~StringLiteral() noexcept(false) = default;

const zc::StringPtr StringLiteral::getValue() const { return impl->getText(); }

zc::StringPtr StringLiteral::getText() const { return impl->getText(); }

void StringLiteral::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& StringLiteral::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind StringLiteral::getKind() const { return impl->getKind(); }

NodeFlags StringLiteral::getFlags() const { return impl->getFlags(); }
void StringLiteral::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void StringLiteral::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// IntegerLiteral

struct IntegerLiteral::Impl : private NodeImpl, public LiteralExpressionImpl {
  int64_t value;

  explicit Impl(int64_t value) noexcept
      : NodeImpl(SyntaxKind::IntegerLiteral), LiteralExpressionImpl(zc::str(value)), value(value) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

IntegerLiteral::IntegerLiteral(int64_t value) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(value)) {}

IntegerLiteral::~IntegerLiteral() noexcept(false) = default;

int64_t IntegerLiteral::getValue() const { return impl->value; }

zc::StringPtr IntegerLiteral::getText() const { return impl->getText(); }

void IntegerLiteral::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& IntegerLiteral::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind IntegerLiteral::getKind() const { return impl->getKind(); }

NodeFlags IntegerLiteral::getFlags() const { return impl->getFlags(); }
void IntegerLiteral::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void IntegerLiteral::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// FloatLiteral

struct FloatLiteral::Impl : private NodeImpl, public LiteralExpressionImpl {
  double value;

  explicit Impl(double value) noexcept
      : NodeImpl(SyntaxKind::FloatLiteral), LiteralExpressionImpl(zc::str(value)), value(value) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

FloatLiteral::FloatLiteral(double value) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(value)) {}

FloatLiteral::~FloatLiteral() noexcept(false) = default;

double FloatLiteral::getValue() const { return impl->value; }

zc::StringPtr FloatLiteral::getText() const { return impl->getText(); }

void FloatLiteral::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& FloatLiteral::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind FloatLiteral::getKind() const { return impl->getKind(); }

NodeFlags FloatLiteral::getFlags() const { return impl->getFlags(); }
void FloatLiteral::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void FloatLiteral::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// BigIntLiteral

struct BigIntLiteral::Impl : private NodeImpl, public LiteralExpressionImpl {
  explicit Impl(zc::StringPtr text) noexcept
      : NodeImpl(SyntaxKind::BigIntLiteral), LiteralExpressionImpl(text) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

BigIntLiteral::BigIntLiteral(zc::StringPtr text) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(text)) {}

BigIntLiteral::~BigIntLiteral() noexcept(false) = default;

zc::StringPtr BigIntLiteral::getText() const { return impl->getText(); }

void BigIntLiteral::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& BigIntLiteral::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind BigIntLiteral::getKind() const { return impl->getKind(); }

NodeFlags BigIntLiteral::getFlags() const { return impl->getFlags(); }
void BigIntLiteral::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void BigIntLiteral::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// BooleanLiteral

struct BooleanLiteral::Impl : private NodeImpl, public LiteralExpressionImpl {
  bool value;

  explicit Impl(bool value) noexcept
      : NodeImpl(SyntaxKind::BooleanLiteral),
        LiteralExpressionImpl(value ? "true" : "false"),
        value(value) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

BooleanLiteral::BooleanLiteral(bool value) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(value)) {}

BooleanLiteral::~BooleanLiteral() noexcept(false) = default;

bool BooleanLiteral::getValue() const { return impl->value; }

zc::StringPtr BooleanLiteral::getText() const { return impl->getText(); }

void BooleanLiteral::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& BooleanLiteral::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind BooleanLiteral::getKind() const { return impl->getKind(); }

NodeFlags BooleanLiteral::getFlags() const { return impl->getFlags(); }
void BooleanLiteral::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void BooleanLiteral::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// NullLiteral

struct NullLiteral::Impl : private NodeImpl, public LiteralExpressionImpl {
  Impl() noexcept : NodeImpl(SyntaxKind::NullLiteral), LiteralExpressionImpl("null") {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

NullLiteral::NullLiteral() noexcept : LiteralExpression(), impl(zc::heap<Impl>()) {}

NullLiteral::~NullLiteral() noexcept(false) = default;

zc::StringPtr NullLiteral::getText() const { return impl->getText(); }

void NullLiteral::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& NullLiteral::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind NullLiteral::getKind() const { return impl->getKind(); }

NodeFlags NullLiteral::getFlags() const { return impl->getFlags(); }
void NullLiteral::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void NullLiteral::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// TemplateSpan

struct TemplateSpan::Impl : private NodeImpl {
  const zc::Own<Expression> expression;
  const zc::Own<StringLiteral> literal;

  Impl(zc::Own<Expression> e, zc::Own<StringLiteral> l)
      : NodeImpl(SyntaxKind::TemplateSpan), expression(zc::mv(e)), literal(zc::mv(l)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

TemplateSpan::TemplateSpan(zc::Own<Expression> expression, zc::Own<StringLiteral> literal) noexcept
    : impl(zc::heap<Impl>(zc::mv(expression), zc::mv(literal))) {}

TemplateSpan::~TemplateSpan() noexcept(false) = default;

const Expression& TemplateSpan::getExpression() const { return *impl->expression; }

const StringLiteral& TemplateSpan::getLiteral() const { return *impl->literal; }

void TemplateSpan::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TemplateSpan::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind TemplateSpan::getKind() const { return impl->getKind(); }

NodeFlags TemplateSpan::getFlags() const { return impl->getFlags(); }
void TemplateSpan::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void TemplateSpan::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// TemplateLiteralExpression

struct TemplateLiteralExpression::Impl : private NodeImpl, public LiteralExpressionImpl {
  const zc::Own<StringLiteral> head;
  const NodeList<TemplateSpan> spans;

  Impl(zc::Own<StringLiteral> h, zc::Vector<zc::Own<TemplateSpan>>&& s)
      : NodeImpl(SyntaxKind::TemplateLiteralExpression),
        LiteralExpressionImpl(h->getText()),
        head(zc::mv(h)),
        spans(zc::mv(s)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

TemplateLiteralExpression::TemplateLiteralExpression(
    zc::Own<StringLiteral> head, zc::Vector<zc::Own<TemplateSpan>>&& spans) noexcept
    : LiteralExpression(), impl(zc::heap<Impl>(zc::mv(head), zc::mv(spans))) {}

TemplateLiteralExpression::~TemplateLiteralExpression() noexcept(false) = default;

const StringLiteral& TemplateLiteralExpression::getHead() const { return *impl->head; }

const NodeList<TemplateSpan>& TemplateLiteralExpression::getSpans() const { return impl->spans; }

zc::StringPtr TemplateLiteralExpression::getText() const { return impl->getText(); }

void TemplateLiteralExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TemplateLiteralExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind TemplateLiteralExpression::getKind() const { return impl->getKind(); }

NodeFlags TemplateLiteralExpression::getFlags() const { return impl->getFlags(); }
void TemplateLiteralExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void TemplateLiteralExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// AsExpression

struct AsExpression::Impl : private NodeImpl {
  const zc::Own<Expression> expression;
  const zc::Own<TypeNode> targetType;
  CastExpressionImpl castImpl;

  Impl(zc::Own<Expression> expression, zc::Own<TypeNode> targetType) noexcept
      : NodeImpl(SyntaxKind::AsExpression),
        expression(zc::mv(expression)),
        targetType(zc::mv(targetType)),
        castImpl(*this->expression, *this->targetType) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

AsExpression::AsExpression(zc::Own<Expression> expression, zc::Own<TypeNode> targetType) noexcept
    : CastExpression(), impl(zc::heap<Impl>(zc::mv(expression), zc::mv(targetType))) {}

AsExpression::~AsExpression() noexcept(false) = default;

const Expression& AsExpression::getExpression() const { return impl->castImpl.getExpression(); }

const TypeNode& AsExpression::getTargetType() const { return impl->castImpl.getTargetType(); }

void AsExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& AsExpression::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind AsExpression::getKind() const { return impl->getKind(); }

NodeFlags AsExpression::getFlags() const { return impl->getFlags(); }
void AsExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void AsExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ForcedAsExpression

struct ForcedAsExpression::Impl : private NodeImpl {
  const zc::Own<Expression> expression;
  const zc::Own<TypeNode> targetType;
  CastExpressionImpl castImpl;

  Impl(zc::Own<Expression> expression, zc::Own<TypeNode> targetType) noexcept
      : NodeImpl(SyntaxKind::ForcedAsExpression),
        expression(zc::mv(expression)),
        targetType(zc::mv(targetType)),
        castImpl(*this->expression, *this->targetType) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ForcedAsExpression::ForcedAsExpression(zc::Own<Expression> expression,
                                       zc::Own<TypeNode> targetType) noexcept
    : CastExpression(), impl(zc::heap<Impl>(zc::mv(expression), zc::mv(targetType))) {}

ForcedAsExpression::~ForcedAsExpression() noexcept(false) = default;

const Expression& ForcedAsExpression::getExpression() const {
  return impl->castImpl.getExpression();
}

const TypeNode& ForcedAsExpression::getTargetType() const { return impl->castImpl.getTargetType(); }

void ForcedAsExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ForcedAsExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ForcedAsExpression::getKind() const { return impl->getKind(); }

NodeFlags ForcedAsExpression::getFlags() const { return impl->getFlags(); }
void ForcedAsExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ForcedAsExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ConditionalAsExpression

struct ConditionalAsExpression::Impl : private NodeImpl {
  const zc::Own<Expression> expression;
  const zc::Own<TypeNode> targetType;
  CastExpressionImpl castImpl;

  Impl(zc::Own<Expression> expression, zc::Own<TypeNode> targetType) noexcept
      : NodeImpl(SyntaxKind::ConditionalAsExpression),
        expression(zc::mv(expression)),
        targetType(zc::mv(targetType)),
        castImpl(*this->expression, *this->targetType) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ConditionalAsExpression::ConditionalAsExpression(zc::Own<Expression> expression,
                                                 zc::Own<TypeNode> targetType) noexcept
    : CastExpression(), impl(zc::heap<Impl>(zc::mv(expression), zc::mv(targetType))) {}

ConditionalAsExpression::~ConditionalAsExpression() noexcept(false) = default;

const Expression& ConditionalAsExpression::getExpression() const {
  return impl->castImpl.getExpression();
}

const TypeNode& ConditionalAsExpression::getTargetType() const {
  return impl->castImpl.getTargetType();
}

void ConditionalAsExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ConditionalAsExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ConditionalAsExpression::getKind() const { return impl->getKind(); }

NodeFlags ConditionalAsExpression::getFlags() const { return impl->getFlags(); }
void ConditionalAsExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ConditionalAsExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// VoidExpression

struct VoidExpression::Impl : private NodeImpl {
  zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expression) noexcept
      : NodeImpl(SyntaxKind::VoidExpression), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

VoidExpression::VoidExpression(zc::Own<Expression> expression) noexcept
    : UnaryExpression(), impl(zc::heap<Impl>(zc::mv(expression))) {}

VoidExpression::~VoidExpression() noexcept(false) = default;

const Expression& VoidExpression::getExpression() const { return *impl->expression; }

void VoidExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& VoidExpression::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind VoidExpression::getKind() const { return impl->getKind(); }

NodeFlags VoidExpression::getFlags() const { return impl->getFlags(); }
void VoidExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void VoidExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// TypeOfExpression

struct TypeOfExpression::Impl : private NodeImpl {
  zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expression) noexcept
      : NodeImpl(SyntaxKind::TypeOfExpression), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

TypeOfExpression::TypeOfExpression(zc::Own<Expression> expression) noexcept
    : UnaryExpression(), impl(zc::heap<Impl>(zc::mv(expression))) {}

TypeOfExpression::~TypeOfExpression() noexcept(false) = default;

const Expression& TypeOfExpression::getExpression() const { return *impl->expression; }

void TypeOfExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TypeOfExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind TypeOfExpression::getKind() const { return impl->getKind(); }

NodeFlags TypeOfExpression::getFlags() const { return impl->getFlags(); }
void TypeOfExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void TypeOfExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// AwaitExpression

struct AwaitExpression::Impl : private NodeImpl {
  zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expression) noexcept
      : NodeImpl(SyntaxKind::AwaitExpression), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

AwaitExpression::AwaitExpression(zc::Own<Expression> expression) noexcept
    : Expression(), impl(zc::heap<Impl>(zc::mv(expression))) {}

AwaitExpression::~AwaitExpression() noexcept(false) = default;

const Expression& AwaitExpression::getExpression() const { return *impl->expression; }

void AwaitExpression::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& AwaitExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind AwaitExpression::getKind() const { return impl->getKind(); }

NodeFlags AwaitExpression::getFlags() const { return impl->getFlags(); }
void AwaitExpression::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void AwaitExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// NonNullExpression
struct NonNullExpression::Impl : private NodeImpl {
  zc::Own<Expression> expression;

  explicit Impl(zc::Own<Expression> expression) noexcept
      : NodeImpl(SyntaxKind::NonNullExpression), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

NonNullExpression::NonNullExpression(zc::Own<Expression> expression) noexcept
    : impl(zc::heap<Impl>(zc::mv(expression))) {}

NonNullExpression::~NonNullExpression() noexcept(false) = default;

const Expression& NonNullExpression::getExpression() const { return *impl->expression; }

void NonNullExpression::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& NonNullExpression::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind NonNullExpression::getKind() const { return impl->getKind(); }

NodeFlags NonNullExpression::getFlags() const { return impl->getFlags(); }
void NonNullExpression::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void NonNullExpression::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ExpressionWithTypeArguments
struct ExpressionWithTypeArguments::Impl : private NodeImpl {
  zc::Own<LeftHandSideExpression> expression;
  zc::Maybe<NodeList<TypeNode>> typeArguments;

  Impl(zc::Own<LeftHandSideExpression> expression,
       zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments) noexcept
      : NodeImpl(SyntaxKind::ExpressionWithTypeArguments),
        expression(zc::mv(expression)),
        typeArguments(zc::mv(typeArguments)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ExpressionWithTypeArguments::ExpressionWithTypeArguments(
    zc::Own<LeftHandSideExpression> expression,
    zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments) noexcept
    : impl(zc::heap<Impl>(zc::mv(expression), zc::mv(typeArguments))) {}

ExpressionWithTypeArguments::~ExpressionWithTypeArguments() noexcept(false) = default;

const LeftHandSideExpression& ExpressionWithTypeArguments::getExpression() const {
  return *impl->expression;
}

const zc::Maybe<NodeList<TypeNode>>& ExpressionWithTypeArguments::getTypeArguments() const {
  return impl->typeArguments;
}

zc::Own<LeftHandSideExpression> ExpressionWithTypeArguments::takeExpression() {
  return zc::mv(impl->expression);
}

zc::Maybe<zc::Vector<zc::Own<TypeNode>>> ExpressionWithTypeArguments::takeTypeArguments() {
  ZC_IF_SOME(t, impl->typeArguments) {
    zc::Vector<zc::Own<TypeNode>> result;
    while (!t.empty()) { result.add(t.remove(0)); }
    return zc::mv(result);
  }
  return zc::none;
}

SyntaxKind ExpressionWithTypeArguments::getKind() const { return impl->getKind(); }

NodeFlags ExpressionWithTypeArguments::getFlags() const { return impl->getFlags(); }
void ExpressionWithTypeArguments::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ExpressionWithTypeArguments::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ExpressionWithTypeArguments::getSourceRange() const {
  return impl->getSourceRange();
}

void ExpressionWithTypeArguments::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// PropertyAssignment
struct PropertyAssignment::Impl : private NamedDeclarationImpl, private NodeImpl {
  zc::Maybe<zc::Own<Expression>> initializer;
  zc::Maybe<zc::Own<TokenNode>> questionToken;

  Impl(zc::Own<Identifier> name, zc::Maybe<zc::Own<Expression>> initializer,
       zc::Maybe<zc::Own<TokenNode>> questionToken) noexcept
      : NamedDeclarationImpl(zc::mv(name)),
        NodeImpl(SyntaxKind::PropertyAssignment),
        initializer(zc::mv(initializer)),
        questionToken(zc::mv(questionToken)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

PropertyAssignment::PropertyAssignment(zc::Own<Identifier> name,
                                       zc::Maybe<zc::Own<Expression>> initializer,
                                       zc::Maybe<zc::Own<TokenNode>> questionToken) noexcept
    : ObjectLiteralElement(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(initializer), zc::mv(questionToken))) {}

PropertyAssignment::~PropertyAssignment() noexcept(false) = default;

const Identifier& PropertyAssignment::getNameIdentifier() const {
  auto name = impl->getName();
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) {
      ZC_IF_SOME(id, maybeId) { return id; }
    }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const BindingPattern&>) {}
  }
  ZC_UNREACHABLE;
}

zc::Maybe<const Expression&> PropertyAssignment::getInitializer() const {
  ZC_IF_SOME(val, impl->initializer) { return *val; }
  return zc::none;
}

zc::Maybe<const TokenNode&> PropertyAssignment::getQuestionToken() const {
  ZC_IF_SOME(token, impl->questionToken) { return *token; }
  return zc::none;
}

SyntaxKind PropertyAssignment::getKind() const { return impl->getKind(); }

NodeFlags PropertyAssignment::getFlags() const { return impl->getFlags(); }
void PropertyAssignment::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void PropertyAssignment::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& PropertyAssignment::getSourceRange() const {
  return impl->getSourceRange();
}

void PropertyAssignment::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> PropertyAssignment::getSymbol() const { return impl->getSymbol(); }

void PropertyAssignment::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  const_cast<Impl*>(impl.get())->setSymbol(symbol);
}

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
PropertyAssignment::getName() const {
  return impl->getName();
}

// ================================================================================
// ShorthandPropertyAssignment
struct ShorthandPropertyAssignment::Impl : private NamedDeclarationImpl, private NodeImpl {
  zc::Maybe<zc::Own<Expression>> objectAssignmentInitializer;
  zc::Maybe<zc::Own<TokenNode>> equalsToken;

  Impl(zc::Own<Identifier> name, zc::Maybe<zc::Own<Expression>> objectAssignmentInitializer,
       zc::Maybe<zc::Own<TokenNode>> equalsToken) noexcept
      : NamedDeclarationImpl(zc::mv(name)),
        NodeImpl(SyntaxKind::ShorthandPropertyAssignment),
        objectAssignmentInitializer(zc::mv(objectAssignmentInitializer)),
        equalsToken(zc::mv(equalsToken)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ShorthandPropertyAssignment::ShorthandPropertyAssignment(
    zc::Own<Identifier> name, zc::Maybe<zc::Own<Expression>> objectAssignmentInitializer,
    zc::Maybe<zc::Own<TokenNode>> equalsToken) noexcept
    : ObjectLiteralElement(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(objectAssignmentInitializer), zc::mv(equalsToken))) {
}

ShorthandPropertyAssignment::~ShorthandPropertyAssignment() noexcept(false) = default;

const Identifier& ShorthandPropertyAssignment::getNameIdentifier() const {
  auto name = impl->getName();
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) {
      ZC_IF_SOME(id, maybeId) { return id; }
    }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const BindingPattern&>) {}
  }
  ZC_UNREACHABLE;
}

zc::Maybe<const Expression&> ShorthandPropertyAssignment::getObjectAssignmentInitializer() const {
  ZC_IF_SOME(init, impl->objectAssignmentInitializer) { return *init; }
  return zc::none;
}

zc::Maybe<const TokenNode&> ShorthandPropertyAssignment::getEqualsToken() const {
  ZC_IF_SOME(token, impl->equalsToken) { return *token; }
  return zc::none;
}

SyntaxKind ShorthandPropertyAssignment::getKind() const { return impl->getKind(); }

NodeFlags ShorthandPropertyAssignment::getFlags() const { return impl->getFlags(); }
void ShorthandPropertyAssignment::setFlags(NodeFlags flags) {
  const_cast<Impl*>(impl.get())->setFlags(flags);
}

void ShorthandPropertyAssignment::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ShorthandPropertyAssignment::getSourceRange() const {
  return impl->getSourceRange();
}

void ShorthandPropertyAssignment::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> ShorthandPropertyAssignment::getSymbol() const {
  return impl->getSymbol();
}

void ShorthandPropertyAssignment::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  const_cast<Impl*>(impl.get())->setSymbol(symbol);
}

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
ShorthandPropertyAssignment::getName() const {
  return impl->getName();
}

// ================================================================================
// SpreadAssignment
struct SpreadAssignment::Impl : private NodeImpl {
  zc::Own<Expression> expression;
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;

  explicit Impl(zc::Own<Expression> expression) noexcept
      : NodeImpl(SyntaxKind::SpreadAssignment), expression(zc::mv(expression)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;

  zc::Maybe<const symbol::Symbol&> getSymbol() const { return symbol; }
  void setSymbol(zc::Maybe<const symbol::Symbol&> s) { symbol = s; }
};

SpreadAssignment::SpreadAssignment(zc::Own<Expression> expression) noexcept
    : ObjectLiteralElement(), impl(zc::heap<Impl>(zc::mv(expression))) {}

SpreadAssignment::~SpreadAssignment() noexcept(false) = default;

const Expression& SpreadAssignment::getExpression() const { return *impl->expression; }

SyntaxKind SpreadAssignment::getKind() const { return impl->getKind(); }

NodeFlags SpreadAssignment::getFlags() const { return impl->getFlags(); }
void SpreadAssignment::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }

void SpreadAssignment::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& SpreadAssignment::getSourceRange() const {
  return impl->getSourceRange();
}

void SpreadAssignment::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> SpreadAssignment::getSymbol() const { return impl->getSymbol(); }

void SpreadAssignment::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  const_cast<Impl*>(impl.get())->setSymbol(symbol);
}

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
SpreadAssignment::getName() const {
  return zc::Maybe<const Identifier&>(zc::none);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
