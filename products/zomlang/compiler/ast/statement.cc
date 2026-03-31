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

#include "zomlang/compiler/ast/statement.h"

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/one-of.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/cast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// Declaration::Impl

struct DeclarationImpl::Impl {
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;
};

DeclarationImpl::DeclarationImpl() noexcept : impl(zc::heap<Impl>()) {}
DeclarationImpl::~DeclarationImpl() noexcept(false) = default;

zc::Maybe<const symbol::Symbol&> DeclarationImpl::getSymbol() const { return impl->symbol; }

void DeclarationImpl::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) { impl->symbol = symbol; }

// ================================================================================
// NamedDeclarationImpl::Impl

struct NamedDeclarationImpl::Impl {
  const zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> name;
};

NamedDeclarationImpl::NamedDeclarationImpl(
    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> name) noexcept
    : DeclarationImpl(), impl(zc::heap<Impl>(zc::mv(name))) {}

NamedDeclarationImpl::~NamedDeclarationImpl() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
NamedDeclarationImpl::getName() const {
  ZC_SWITCH_ONEOF(impl->name) {
    ZC_CASE_ONEOF(identifier, zc::Own<ast::Identifier>) {
      return zc::some<const Identifier&>(*identifier);
    }
    ZC_CASE_ONEOF(pattern, zc::Own<BindingPattern>) {
      return zc::some<const BindingPattern&>(*pattern);
    }
  }
  ZC_UNREACHABLE;
}

// ================================================================================
// BindingElement::Impl

struct BindingElement::Impl : private NamedDeclarationImpl, private NodeImpl {
  const zc::Maybe<zc::Own<TokenNode>> dotDotDotToken;
  const zc::Maybe<zc::Own<Identifier>> propertyName;
  const zc::Maybe<zc::Own<Expression>> initializer;

  Impl(zc::Maybe<zc::Own<TokenNode>> dotDotDotToken, zc::Maybe<zc::Own<Identifier>> propertyName,
       zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> nameOrPattern,
       zc::Maybe<zc::Own<Expression>> init)
      : NamedDeclarationImpl(zc::mv(nameOrPattern)),
        NodeImpl(SyntaxKind::BindingElement),
        dotDotDotToken(zc::mv(dotDotDotToken)),
        propertyName(zc::mv(propertyName)),
        initializer(zc::mv(init)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// BindingElement

BindingElement::BindingElement(
    zc::Maybe<zc::Own<TokenNode>> dotDotDotToken, zc::Maybe<zc::Own<Identifier>> propertyName,
    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> nameOrPattern,
    zc::Maybe<zc::Own<Expression>> initializer) noexcept
    : NamedDeclaration(),
      impl(zc::heap<Impl>(zc::mv(dotDotDotToken), zc::mv(propertyName), zc::mv(nameOrPattern),
                          zc::mv(initializer))) {}

BindingElement::~BindingElement() noexcept(false) = default;

zc::Maybe<const Expression&> BindingElement::getInitializer() const { return impl->initializer; }

SyntaxKind BindingElement::getKind() const { return SyntaxKind::BindingElement; }

void BindingElement::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> BindingElement::getSymbol() const { return impl->getSymbol(); }

void BindingElement::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) { impl->setSymbol(symbol); }

void BindingElement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& BindingElement::getSourceRange() const { return impl->getSourceRange(); }

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> BindingElement::getName()
    const {
  return impl->getName();
}

zc::Maybe<const BindingPattern&> BindingElement::getBindingPattern() const {
  auto name = impl->getName();
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) { return zc::none; }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const BindingPattern&>) { return maybePattern; }
  }
  ZC_UNREACHABLE;
}

// ================================================================================
// ParameterDeclaration::Impl

struct ParameterDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const zc::Maybe<zc::Own<TokenNode>> dotDotDotToken;
  const zc::Maybe<zc::Own<TokenNode>> questionToken;
  const zc::Maybe<zc::Own<TypeNode>> type;
  const zc::Maybe<zc::Own<Expression>> initializer;

  Impl(zc::Vector<ast::SyntaxKind> mods, zc::Maybe<zc::Own<TokenNode>> ddd,
       zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> n, zc::Maybe<zc::Own<TokenNode>> qt,
       zc::Maybe<zc::Own<TypeNode>> t, zc::Maybe<zc::Own<Expression>> init)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::ParameterDeclaration),
        modifiers(zc::mv(mods)),
        dotDotDotToken(zc::mv(ddd)),
        questionToken(zc::mv(qt)),
        type(zc::mv(t)),
        initializer(zc::mv(init)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ParameterDeclaration

ParameterDeclaration::ParameterDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Maybe<zc::Own<TokenNode>> dotDotDotToken,
    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> name,
    zc::Maybe<zc::Own<TokenNode>> questionToken, zc::Maybe<zc::Own<TypeNode>> type,
    zc::Maybe<zc::Own<Expression>> initializer) noexcept
    : NamedDeclaration(),
      impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(dotDotDotToken), zc::mv(name),
                          zc::mv(questionToken), zc::mv(type), zc::mv(initializer))) {}

ParameterDeclaration::~ParameterDeclaration() noexcept(false) = default;

zc::ArrayPtr<const ast::SyntaxKind> ParameterDeclaration::getModifiers() const {
  return impl->modifiers;
}

zc::Maybe<const TokenNode&> ParameterDeclaration::getDotDotDotToken() const {
  return impl->dotDotDotToken;
}

zc::Maybe<const TokenNode&> ParameterDeclaration::getQuestionToken() const {
  return impl->questionToken;
}

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
ParameterDeclaration::getName() const {
  return impl->getName();
}

zc::Maybe<const TypeNode&> ParameterDeclaration::getType() const { return impl->type; }

zc::Maybe<const Expression&> ParameterDeclaration::getInitializer() const {
  return impl->initializer;
}

SyntaxKind ParameterDeclaration::getKind() const { return SyntaxKind::ParameterDeclaration; }

void ParameterDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> ParameterDeclaration::getSymbol() const {
  return impl->getSymbol();
}

void ParameterDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void ParameterDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ParameterDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

zc::Maybe<const BindingPattern&> ParameterDeclaration::getBindingPattern() const {
  auto name = impl->getName();
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) { return zc::none; }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const BindingPattern&>) { return maybePattern; }
  }
  ZC_UNREACHABLE;
}

// ================================================================================
// VariableDeclaration::Impl

struct VariableDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const zc::Maybe<zc::Own<TypeNode>> type;
  const zc::Maybe<zc::Own<Expression>> initializer;

  Impl(zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> n, zc::Maybe<zc::Own<TypeNode>> t,
       zc::Maybe<zc::Own<Expression>> init)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::VariableDeclaration),
        type(zc::mv(t)),
        initializer(zc::mv(init)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// VariableDeclaration

VariableDeclaration::VariableDeclaration(
    zc::OneOf<zc::Own<Identifier>, zc::Own<BindingPattern>> name, zc::Maybe<zc::Own<TypeNode>> type,
    zc::Maybe<zc::Own<Expression>> initializer) noexcept
    : NamedDeclaration(), impl(zc::heap<Impl>(zc::mv(name), zc::mv(type), zc::mv(initializer))) {}

VariableDeclaration::~VariableDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
VariableDeclaration::getName() const {
  return impl->getName();
}

zc::Maybe<const BindingPattern&> VariableDeclaration::getBindingPattern() const {
  auto name = impl->getName();
  ZC_SWITCH_ONEOF(name) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const Identifier&>) { return zc::none; }
    ZC_CASE_ONEOF(maybePattern, zc::Maybe<const BindingPattern&>) { return maybePattern; }
  }
  ZC_UNREACHABLE;
}

zc::Maybe<const TypeNode&> VariableDeclaration::getType() const { return impl->type; }

zc::Maybe<const Expression&> VariableDeclaration::getInitializer() const {
  return impl->initializer;
}

SyntaxKind VariableDeclaration::getKind() const { return SyntaxKind::VariableDeclaration; }

void VariableDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> VariableDeclaration::getSymbol() const {
  return impl->getSymbol();
}

void VariableDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void VariableDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& VariableDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// VariableDeclarationList::Impl

struct VariableDeclarationList::Impl : private NodeImpl {
  const NodeList<VariableDeclaration> bindings;

  explicit Impl(zc::Vector<zc::Own<VariableDeclaration>>&& b)
      : NodeImpl(SyntaxKind::VariableDeclarationList), bindings(zc::mv(b)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// VariableDeclarationList

VariableDeclarationList::VariableDeclarationList(
    zc::Vector<zc::Own<VariableDeclaration>>&& bindings) noexcept
    : Node(), impl(zc::heap<Impl>(zc::mv(bindings))) {}

VariableDeclarationList::~VariableDeclarationList() noexcept(false) = default;

const NodeList<VariableDeclaration>& VariableDeclarationList::getBindings() const {
  return impl->bindings;
}

SyntaxKind VariableDeclarationList::getKind() const { return SyntaxKind::VariableDeclarationList; }

void VariableDeclarationList::accept(Visitor& visitor) const { visitor.visit(*this); }

void VariableDeclarationList::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& VariableDeclarationList::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// VariableStatement::Impl

struct VariableStatement::Impl : private NodeImpl {
  const zc::Own<VariableDeclarationList> declarations;

  explicit Impl(zc::Own<VariableDeclarationList> d)
      : NodeImpl(SyntaxKind::VariableStatement), declarations(zc::mv(d)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// VariableStatement

VariableStatement::VariableStatement(zc::Own<VariableDeclarationList> declarations) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(declarations))) {}

VariableStatement::~VariableStatement() noexcept(false) = default;

const VariableDeclarationList& VariableStatement::getDeclarations() const {
  return *impl->declarations;
}

SyntaxKind VariableStatement::getKind() const { return SyntaxKind::VariableStatement; }

void VariableStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

void VariableStatement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& VariableStatement::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// FunctionDeclaration::Impl

struct FunctionDeclaration::Impl : private NamedDeclarationImpl,
                                   private LocalsContainerImpl,
                                   private NodeImpl {
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const zc::Maybe<zc::Own<ReturnTypeNode>> returnType;
  const zc::Own<Statement> body;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Vector<zc::Own<ParameterDeclaration>>&& p, zc::Maybe<zc::Own<ReturnTypeNode>> r,
       zc::Own<Statement> b)
      : NamedDeclarationImpl(zc::mv(n)),
        LocalsContainerImpl(),
        NodeImpl(SyntaxKind::FunctionDeclaration),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        parameters(zc::mv(p)),
        returnType(zc::mv(r)),
        body(zc::mv(b)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;

  // Forward LocalsContainerImpl methods
  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
};

// ================================================================================
// FunctionDeclaration

FunctionDeclaration::FunctionDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Own<Statement> body) noexcept
    : DeclarationStatement(),
      LocalsContainer(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(typeParameters), zc::mv(parameters),
                          zc::mv(returnType), zc::mv(body))) {}

FunctionDeclaration::~FunctionDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
FunctionDeclaration::getName() const {
  return impl->getName();
}

const NodeList<TypeParameterDeclaration>& FunctionDeclaration::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<ParameterDeclaration>& FunctionDeclaration::getParameters() const {
  return impl->parameters;
}

zc::Maybe<const ReturnTypeNode&> FunctionDeclaration::getReturnType() const {
  return impl->returnType;
}

const Statement& FunctionDeclaration::getBody() const { return *impl->body; }

zc::Maybe<const symbol::SymbolTable&> FunctionDeclaration::getLocals() const {
  return impl->getLocals();
}

void FunctionDeclaration::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> FunctionDeclaration::getNextContainer() const {
  return impl->getNextContainer();
}

void FunctionDeclaration::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

SyntaxKind FunctionDeclaration::getKind() const { return SyntaxKind::FunctionDeclaration; }

void FunctionDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> FunctionDeclaration::getSymbol() const {
  return impl->getSymbol();
}

void FunctionDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void FunctionDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& FunctionDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// ClassDeclaration::Impl

struct ClassDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<HeritageClause> heritageClauses;
  const NodeList<ClassElement> members;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> hc, zc::Vector<zc::Own<ClassElement>>&& m)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::ClassDeclaration),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        heritageClauses(zc::mv(hc).orDefault(zc::Vector<zc::Own<HeritageClause>>())),
        members(zc::mv(m)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
};

// ================================================================================
// ClassDeclaration

ClassDeclaration::ClassDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<ClassElement>>&& members) noexcept
    : DeclarationStatement(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(typeParameters), zc::mv(heritageClauses),
                          zc::mv(members))) {}

ClassDeclaration::~ClassDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
ClassDeclaration::getName() const {
  return impl->getName();
}

const NodeList<TypeParameterDeclaration>& ClassDeclaration::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<HeritageClause>& ClassDeclaration::getHeritageClauses() const {
  return impl->heritageClauses;
}

const NodeList<ClassElement>& ClassDeclaration::getMembers() const { return impl->members; }

SyntaxKind ClassDeclaration::getKind() const { return SyntaxKind::ClassDeclaration; }

void ClassDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> ClassDeclaration::getSymbol() const { return impl->getSymbol(); }

void ClassDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void ClassDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ClassDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// InterfaceDeclaration::Impl

struct InterfaceDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<HeritageClause> heritageClauses;
  const NodeList<InterfaceElement> members;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> hc, zc::Vector<zc::Own<InterfaceElement>>&& m)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::InterfaceDeclaration),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        heritageClauses(zc::mv(hc).orDefault(zc::Vector<zc::Own<HeritageClause>>())),
        members(zc::mv(m)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
};

// ================================================================================
// InterfaceDeclaration

InterfaceDeclaration::InterfaceDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<InterfaceElement>>&& members) noexcept
    : DeclarationStatement(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(typeParameters), zc::mv(heritageClauses),
                          zc::mv(members))) {}

InterfaceDeclaration::~InterfaceDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
InterfaceDeclaration::getName() const {
  return impl->getName();
}

const NodeList<TypeParameterDeclaration>& InterfaceDeclaration::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<HeritageClause>& InterfaceDeclaration::getHeritageClauses() const {
  return impl->heritageClauses;
}

const NodeList<InterfaceElement>& InterfaceDeclaration::getMembers() const { return impl->members; }

SyntaxKind InterfaceDeclaration::getKind() const { return SyntaxKind::InterfaceDeclaration; }

void InterfaceDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> InterfaceDeclaration::getSymbol() const {
  return impl->getSymbol();
}

void InterfaceDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void InterfaceDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& InterfaceDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// StructDeclaration::Impl

struct StructDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<HeritageClause> heritageClauses;
  const NodeList<ClassElement> members;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> hc, zc::Vector<zc::Own<ClassElement>>&& m)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::StructDeclaration),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        heritageClauses(zc::mv(hc).orDefault(zc::Vector<zc::Own<HeritageClause>>())),
        members(zc::mv(m)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
};

// ================================================================================
// StructDeclaration

StructDeclaration::StructDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Maybe<zc::Vector<zc::Own<HeritageClause>>> heritageClauses,
    zc::Vector<zc::Own<ClassElement>>&& members) noexcept
    : DeclarationStatement(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(typeParameters), zc::mv(heritageClauses),
                          zc::mv(members))) {}

StructDeclaration::~StructDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
StructDeclaration::getName() const {
  return impl->getName();
}

const NodeList<TypeParameterDeclaration>& StructDeclaration::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<HeritageClause>& StructDeclaration::getHeritageClauses() const {
  return impl->heritageClauses;
}

const NodeList<ClassElement>& StructDeclaration::getMembers() const { return impl->members; }

SyntaxKind StructDeclaration::getKind() const { return SyntaxKind::StructDeclaration; }

void StructDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> StructDeclaration::getSymbol() const { return impl->getSymbol(); }

void StructDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void StructDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& StructDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// EnumMember::Impl

struct EnumMember::Impl : private NamedDeclarationImpl, private NodeImpl {
  const zc::Maybe<zc::Own<Expression>> initializer;
  const zc::Maybe<zc::Own<TupleTypeNode>> tupleType;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Own<Expression>> init,
       zc::Maybe<zc::Own<TupleTypeNode>> tuple)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::EnumMember),
        initializer(zc::mv(init)),
        tupleType(zc::mv(tuple)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// EnumMember

EnumMember::EnumMember(zc::Own<Identifier> name, zc::Maybe<zc::Own<Expression>> initializer,
                       zc::Maybe<zc::Own<TupleTypeNode>> tupleType) noexcept
    : DeclarationStatement(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(initializer), zc::mv(tupleType))) {}

EnumMember::~EnumMember() noexcept(false) = default;

zc::Maybe<const Expression&> EnumMember::getInitializer() const { return impl->initializer; }

zc::Maybe<const TupleTypeNode&> EnumMember::getTupleType() const { return impl->tupleType; }

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> EnumMember::getName()
    const {
  return impl->getName();
}

SyntaxKind EnumMember::getKind() const { return SyntaxKind::EnumMember; }

void EnumMember::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> EnumMember::getSymbol() const { return impl->getSymbol(); }

void EnumMember::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) { impl->setSymbol(symbol); }

void EnumMember::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& EnumMember::getSourceRange() const { return impl->getSourceRange(); }

// ================================================================================
// EnumDeclaration::Impl

struct EnumDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const NodeList<EnumMember> members;

  Impl(zc::Own<Identifier> n, zc::Vector<zc::Own<EnumMember>>&& m)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::EnumDeclaration),
        members(zc::mv(m)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
};

// ================================================================================
// EnumDeclaration

EnumDeclaration::EnumDeclaration(zc::Own<Identifier> name,
                                 zc::Vector<zc::Own<EnumMember>>&& members) noexcept
    : DeclarationStatement(), impl(zc::heap<Impl>(zc::mv(name), zc::mv(members))) {}

EnumDeclaration::~EnumDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> EnumDeclaration::getName()
    const {
  return impl->getName();
}

const NodeList<EnumMember>& EnumDeclaration::getMembers() const { return impl->members; }

SyntaxKind EnumDeclaration::getKind() const { return SyntaxKind::EnumDeclaration; }

void EnumDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> EnumDeclaration::getSymbol() const { return impl->getSymbol(); }

void EnumDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void EnumDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& EnumDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// ErrorDeclaration::Impl

struct ErrorDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const NodeList<Statement> members;

  Impl(zc::Own<Identifier> n, zc::Vector<zc::Own<Statement>>&& m)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::ErrorDeclaration),
        members(zc::mv(m)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
};

// ================================================================================
// ErrorDeclaration

ErrorDeclaration::ErrorDeclaration(zc::Own<Identifier> name,
                                   zc::Vector<zc::Own<Statement>>&& members) noexcept
    : DeclarationStatement(), impl(zc::heap<Impl>(zc::mv(name), zc::mv(members))) {}

ErrorDeclaration::~ErrorDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
ErrorDeclaration::getName() const {
  return impl->getName();
}

const NodeList<Statement>& ErrorDeclaration::getMembers() const { return impl->members; }

SyntaxKind ErrorDeclaration::getKind() const { return SyntaxKind::ErrorDeclaration; }

void ErrorDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> ErrorDeclaration::getSymbol() const { return impl->getSymbol(); }

void ErrorDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void ErrorDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ErrorDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// AliasDeclaration::Impl

struct AliasDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const NodeList<TypeParameterDeclaration> typeParameters;
  const zc::Own<TypeNode> type;

  Impl(zc::Own<Identifier> n, zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tps,
       zc::Own<TypeNode> t)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::AliasDeclaration),
        typeParameters(zc::mv(tps).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        type(zc::mv(t)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
};

// ================================================================================
// AliasDeclaration

AliasDeclaration::AliasDeclaration(
    zc::Own<Identifier> name,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Own<TypeNode> type) noexcept
    : NamedDeclaration(),
      Statement(),
      impl(zc::heap<Impl>(zc::mv(name), zc::mv(typeParameters), zc::mv(type))) {}

AliasDeclaration::~AliasDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
AliasDeclaration::getName() const {
  return impl->getName();
}

const NodeList<TypeParameterDeclaration>& AliasDeclaration::getTypeParameters() const {
  return impl->typeParameters;
}

const TypeNode& AliasDeclaration::getType() const { return *impl->type; }

void AliasDeclaration::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& AliasDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind AliasDeclaration::getKind() const { return SyntaxKind::AliasDeclaration; }

void AliasDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> AliasDeclaration::getSymbol() const { return impl->getSymbol(); }

void AliasDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  const_cast<Impl*>(impl.get())->setSymbol(symbol);
}

// ================================================================================
// DebuggerStatement::Impl

struct DebuggerStatement::Impl : private NodeImpl {
  Impl() : NodeImpl(SyntaxKind::DebuggerStatement) {}

  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// DebuggerStatement

DebuggerStatement::DebuggerStatement() noexcept : Statement(), impl(zc::heap<Impl>()) {}

DebuggerStatement::~DebuggerStatement() noexcept(false) = default;

void DebuggerStatement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& DebuggerStatement::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind DebuggerStatement::getKind() const { return impl->getKind(); }

void DebuggerStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// MatchClause::Impl

struct MatchClause::Impl : private NodeImpl {
  const zc::Own<Pattern> pattern;
  const zc::Maybe<zc::Own<Expression>> guard;
  const zc::Own<Statement> body;

  Impl(zc::Own<Pattern> p, zc::Maybe<zc::Own<Expression>> g, zc::Own<Statement> b)
      : NodeImpl(SyntaxKind::MatchClause), pattern(zc::mv(p)), guard(zc::mv(g)), body(zc::mv(b)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// MatchClause

MatchClause::MatchClause(zc::Own<Pattern> pattern, zc::Maybe<zc::Own<Expression>> guard,
                         zc::Own<Statement> body) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(pattern), zc::mv(guard), zc::mv(body))) {}

MatchClause::~MatchClause() noexcept(false) = default;

const Pattern& MatchClause::getPattern() const { return *impl->pattern; }

zc::Maybe<const Expression&> MatchClause::getGuard() const { return impl->guard; }

const Statement& MatchClause::getBody() const { return *impl->body; }

void MatchClause::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& MatchClause::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind MatchClause::getKind() const { return SyntaxKind::MatchClause; }

void MatchClause::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// DefaultClause::Impl

struct DefaultClause::Impl : private NodeImpl {
  const NodeList<Statement> statements;

  explicit Impl(zc::Vector<zc::Own<Statement>>&& s)
      : NodeImpl(SyntaxKind::DefaultClause), statements(zc::mv(s)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// DefaultClause

DefaultClause::DefaultClause(zc::Vector<zc::Own<Statement>>&& statements) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(statements))) {}

DefaultClause::~DefaultClause() noexcept(false) = default;

const NodeList<Statement>& DefaultClause::getStatements() const { return impl->statements; }

SyntaxKind DefaultClause::getKind() const { return SyntaxKind::DefaultClause; }

void DefaultClause::accept(Visitor& visitor) const { visitor.visit(*this); }

void DefaultClause::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& DefaultClause::getSourceRange() const { return impl->getSourceRange(); }

// ================================================================================
// ArrayBindingPattern::Impl

struct ArrayBindingPattern::Impl : private NodeImpl {
  const NodeList<BindingElement> elements;

  explicit Impl(zc::Vector<zc::Own<BindingElement>>&& e)
      : NodeImpl(SyntaxKind::ArrayBindingPattern), elements(zc::mv(e)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ArrayBindingPattern

ArrayBindingPattern::ArrayBindingPattern(zc::Vector<zc::Own<BindingElement>>&& elements) noexcept
    : BindingPattern(), impl(zc::heap<Impl>(zc::mv(elements))) {}

ArrayBindingPattern::~ArrayBindingPattern() noexcept(false) = default;

const NodeList<BindingElement>& ArrayBindingPattern::getElements() const { return impl->elements; }

void ArrayBindingPattern::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ArrayBindingPattern::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ArrayBindingPattern::getKind() const { return SyntaxKind::ArrayBindingPattern; }

void ArrayBindingPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ObjectBindingPattern::Impl

struct ObjectBindingPattern::Impl : private NodeImpl {
  const NodeList<BindingElement> properties;

  explicit Impl(zc::Vector<zc::Own<BindingElement>>&& p)
      : NodeImpl(SyntaxKind::ObjectBindingPattern), properties(zc::mv(p)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ObjectBindingPattern

ObjectBindingPattern::ObjectBindingPattern(
    zc::Vector<zc::Own<BindingElement>>&& properties) noexcept
    : BindingPattern(), impl(zc::heap<Impl>(zc::mv(properties))) {}

ObjectBindingPattern::~ObjectBindingPattern() noexcept(false) = default;

const NodeList<BindingElement>& ObjectBindingPattern::getElements() const {
  return impl->properties;
}

const NodeList<BindingElement>& ObjectBindingPattern::getProperties() const {
  return impl->properties;
}

void ObjectBindingPattern::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ObjectBindingPattern::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ObjectBindingPattern::getKind() const { return SyntaxKind::ObjectBindingPattern; }

void ObjectBindingPattern::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// BlockStatement::Impl

struct BlockStatement::Impl : private NodeImpl, private LocalsContainerImpl {
  const NodeList<Statement> statements;

  Impl(zc::Vector<zc::Own<Statement>>&& s)
      : NodeImpl(SyntaxKind::BlockStatement), LocalsContainerImpl(), statements(zc::mv(s)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward LocalsContainerImpl methods
  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
};

// ================================================================================
// BlockStatement

BlockStatement::BlockStatement(zc::Vector<zc::Own<Statement>>&& statements) noexcept
    : Statement(), LocalsContainer(), impl(zc::heap<Impl>(zc::mv(statements))) {}

BlockStatement::~BlockStatement() noexcept(false) = default;

const NodeList<Statement>& BlockStatement::getStatements() const { return impl->statements; }

void BlockStatement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& BlockStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind BlockStatement::getKind() const { return SyntaxKind::BlockStatement; }

void BlockStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::SymbolTable&> BlockStatement::getLocals() const {
  return impl->getLocals();
}

void BlockStatement::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> BlockStatement::getNextContainer() const {
  return impl->getNextContainer();
}

void BlockStatement::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

// ================================================================================
// ExpressionStatement::Impl

struct ExpressionStatement::Impl : private NodeImpl {
  const zc::Own<Expression> expression;

  Impl(zc::Own<Expression> e) : NodeImpl(SyntaxKind::ExpressionStatement), expression(zc::mv(e)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ExpressionStatement

ExpressionStatement::ExpressionStatement(zc::Own<Expression> expression) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(expression))) {}

ExpressionStatement::~ExpressionStatement() noexcept(false) = default;

const Expression& ExpressionStatement::getExpression() const { return *impl->expression; }

void ExpressionStatement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ExpressionStatement::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ExpressionStatement::getKind() const { return SyntaxKind::ExpressionStatement; }

void ExpressionStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// IfStatement::Impl

struct IfStatement::Impl : private NodeImpl {
  const zc::Own<Expression> condition;
  const zc::Own<Statement> thenStatement;
  const zc::Maybe<zc::Own<Statement>> elseStatement;

  Impl(zc::Own<Expression> c, zc::Own<Statement> t, zc::Maybe<zc::Own<Statement>> e)
      : NodeImpl(SyntaxKind::IfStatement),
        condition(zc::mv(c)),
        thenStatement(zc::mv(t)),
        elseStatement(zc::mv(e)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// IfStatement

IfStatement::IfStatement(zc::Own<Expression> condition, zc::Own<Statement> thenStatement,
                         zc::Maybe<zc::Own<Statement>> elseStatement) noexcept
    : Statement(),
      impl(zc::heap<Impl>(zc::mv(condition), zc::mv(thenStatement), zc::mv(elseStatement))) {}

IfStatement::~IfStatement() noexcept(false) = default;

const Expression& IfStatement::getCondition() const { return *impl->condition; }

const Statement& IfStatement::getThenStatement() const { return *impl->thenStatement; }

zc::Maybe<const Statement&> IfStatement::getElseStatement() const { return impl->elseStatement; }

void IfStatement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& IfStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind IfStatement::getKind() const { return SyntaxKind::IfStatement; }

void IfStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// LabeledStatement::Impl

struct LabeledStatement::Impl : private NodeImpl {
  const zc::Own<Identifier> label;
  const zc::Own<Statement> statement;

  Impl(zc::Own<Identifier> l, zc::Own<Statement> s)
      : NodeImpl(SyntaxKind::LabeledStatement), label(zc::mv(l)), statement(zc::mv(s)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// LabeledStatement

LabeledStatement::LabeledStatement(zc::Own<Identifier> label, zc::Own<Statement> statement) noexcept
    : impl(zc::heap<Impl>(zc::mv(label), zc::mv(statement))) {}

LabeledStatement::~LabeledStatement() noexcept(false) = default;

const Identifier& LabeledStatement::getLabel() const { return *impl->label; }

const Statement& LabeledStatement::getStatement() const { return *impl->statement; }

void LabeledStatement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& LabeledStatement::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind LabeledStatement::getKind() const { return SyntaxKind::LabeledStatement; }

void LabeledStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// BreakStatement::Impl

struct BreakStatement::Impl : private NodeImpl {
  const zc::Maybe<zc::Own<Identifier>> label;

  explicit Impl(zc::Maybe<zc::Own<Identifier>> l)
      : NodeImpl(SyntaxKind::BreakStatement), label(zc::mv(l)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// BreakStatement

BreakStatement::BreakStatement(zc::Maybe<zc::Own<Identifier>> label) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(label))) {}

BreakStatement::~BreakStatement() noexcept(false) = default;

zc::Maybe<const Identifier&> BreakStatement::getLabel() const { return impl->label; }

void BreakStatement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& BreakStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind BreakStatement::getKind() const { return SyntaxKind::BreakStatement; }

void BreakStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ContinueStatement::Impl

struct ContinueStatement::Impl : private NodeImpl {
  const zc::Maybe<zc::Own<Identifier>> label;

  explicit Impl(zc::Maybe<zc::Own<Identifier>> l)
      : NodeImpl(SyntaxKind::ContinueStatement), label(zc::mv(l)) {}

  // Forward NodeImpl methods
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ContinueStatement

ContinueStatement::ContinueStatement(zc::Maybe<zc::Own<Identifier>> label) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(label))) {}

ContinueStatement::~ContinueStatement() noexcept(false) = default;

zc::Maybe<const Identifier&> ContinueStatement::getLabel() const { return impl->label; }

void ContinueStatement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ContinueStatement::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ContinueStatement::getKind() const { return SyntaxKind::ContinueStatement; }

void ContinueStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// WhileStatement::Impl

struct WhileStatement::Impl : private NodeImpl {
  const zc::Own<Expression> condition;
  const zc::Own<Statement> body;

  Impl(zc::Own<Expression> c, zc::Own<Statement> b)
      : NodeImpl(SyntaxKind::WhileStatement), condition(zc::mv(c)), body(zc::mv(b)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// WhileStatement

WhileStatement::WhileStatement(zc::Own<Expression> condition, zc::Own<Statement> body) noexcept
    : IterationStatement(), impl(zc::heap<Impl>(zc::mv(condition), zc::mv(body))) {}

WhileStatement::~WhileStatement() noexcept(false) = default;

const Expression& WhileStatement::getCondition() const { return *impl->condition; }

const Statement& WhileStatement::getBody() const { return *impl->body; }

void WhileStatement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& WhileStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind WhileStatement::getKind() const { return SyntaxKind::WhileStatement; }

void WhileStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ReturnStatement::Impl

struct ReturnStatement::Impl : private NodeImpl {
  const zc::Maybe<zc::Own<Expression>> expression;

  explicit Impl(zc::Maybe<zc::Own<Expression>> e)
      : NodeImpl(SyntaxKind::ReturnStatement), expression(zc::mv(e)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// ReturnStatement

ReturnStatement::ReturnStatement(zc::Maybe<zc::Own<Expression>> expression) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(expression))) {}

ReturnStatement::~ReturnStatement() noexcept(false) = default;

zc::Maybe<const Expression&> ReturnStatement::getExpression() const {
  ZC_IF_SOME(expr, impl->expression) { return *expr; }
  return zc::none;
}

void ReturnStatement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ReturnStatement::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind ReturnStatement::getKind() const { return SyntaxKind::ReturnStatement; }

void ReturnStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// EmptyStatement::Impl

struct EmptyStatement::Impl : private NodeImpl {
  Impl() : NodeImpl(SyntaxKind::EmptyStatement) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// EmptyStatement

EmptyStatement::EmptyStatement() noexcept : Statement(), impl(zc::heap<Impl>()) {}

EmptyStatement::~EmptyStatement() noexcept(false) = default;

void EmptyStatement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& EmptyStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind EmptyStatement::getKind() const { return SyntaxKind::EmptyStatement; }

void EmptyStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// MatchStatement::Impl

struct MatchStatement::Impl : private NodeImpl {
  const zc::Own<Expression> discriminant;
  const NodeList<Statement> clauses;

  Impl(zc::Own<Expression> d, zc::Vector<zc::Own<Statement>>&& c)
      : NodeImpl(SyntaxKind::MatchStatement), discriminant(zc::mv(d)), clauses(zc::mv(c)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// MatchStatement

MatchStatement::MatchStatement(zc::Own<Expression> discriminant,
                               zc::Vector<zc::Own<Statement>>&& clauses) noexcept
    : Statement(), impl(zc::heap<Impl>(zc::mv(discriminant), zc::mv(clauses))) {}

MatchStatement::~MatchStatement() noexcept(false) = default;

const Expression& MatchStatement::getDiscriminant() const { return *impl->discriminant; }

const NodeList<Statement>& MatchStatement::getClauses() const { return impl->clauses; }

void MatchStatement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& MatchStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind MatchStatement::getKind() const { return SyntaxKind::MatchStatement; }

void MatchStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// ForStatement::Impl

struct ForStatement::Impl : private NodeImpl, private LocalsContainerImpl {
  const zc::Maybe<zc::Own<Statement>> init;
  const zc::Maybe<zc::Own<Expression>> condition;
  const zc::Maybe<zc::Own<Expression>> update;
  const zc::Own<Statement> body;

  Impl(zc::Maybe<zc::Own<Statement>> i, zc::Maybe<zc::Own<Expression>> c,
       zc::Maybe<zc::Own<Expression>> u, zc::Own<Statement> b)
      : NodeImpl(SyntaxKind::ForStatement),
        LocalsContainerImpl(),
        init(zc::mv(i)),
        condition(zc::mv(c)),
        update(zc::mv(u)),
        body(zc::mv(b)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward LocalsContainerImpl methods
  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
};

// ================================================================================
// ForStatement

ForStatement::ForStatement(zc::Maybe<zc::Own<Statement>> init,
                           zc::Maybe<zc::Own<Expression>> condition,
                           zc::Maybe<zc::Own<Expression>> update, zc::Own<Statement> body) noexcept
    : IterationStatement(),
      LocalsContainer(),
      impl(zc::heap<Impl>(zc::mv(init), zc::mv(condition), zc::mv(update), zc::mv(body))) {}

ForStatement::~ForStatement() noexcept(false) = default;

zc::Maybe<const Statement&> ForStatement::getInitializer() const {
  ZC_IF_SOME(init, impl->init) { return *init; }
  return zc::none;
}

zc::Maybe<const Expression&> ForStatement::getCondition() const {
  ZC_IF_SOME(cond, impl->condition) { return *cond; }
  return zc::none;
}

zc::Maybe<const Expression&> ForStatement::getUpdate() const {
  ZC_IF_SOME(upd, impl->update) { return *upd; }
  return zc::none;
}

const Statement& ForStatement::getBody() const { return *impl->body; }

void ForStatement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ForStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind ForStatement::getKind() const { return SyntaxKind::ForStatement; }

void ForStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::SymbolTable&> ForStatement::getLocals() const { return impl->getLocals(); }

void ForStatement::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  const_cast<Impl*>(impl.get())->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> ForStatement::getNextContainer() const {
  return impl->getNextContainer();
}

void ForStatement::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  const_cast<Impl*>(impl.get())->setNextContainer(nextContainer);
}

// ================================================================================
// ForInStatement::Impl

struct ForInStatement::Impl : private NodeImpl, private LocalsContainerImpl {
  const zc::Own<Statement> initializer;
  const zc::Own<Expression> expression;
  const zc::Own<Statement> body;

  Impl(zc::Own<Statement> i, zc::Own<Expression> e, zc::Own<Statement> b)
      : NodeImpl(SyntaxKind::ForInStatement),
        LocalsContainerImpl(),
        initializer(zc::mv(i)),
        expression(zc::mv(e)),
        body(zc::mv(b)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;

  // Forward LocalsContainerImpl methods
  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
};

// ================================================================================
// ForInStatement

ForInStatement::ForInStatement(zc::Own<Statement> initializer, zc::Own<Expression> expression,
                               zc::Own<Statement> body) noexcept
    : IterationStatement(),
      LocalsContainer(),
      impl(zc::heap<Impl>(zc::mv(initializer), zc::mv(expression), zc::mv(body))) {}

ForInStatement::~ForInStatement() noexcept(false) = default;

const Statement& ForInStatement::getInitializer() const { return *impl->initializer; }

const Expression& ForInStatement::getExpression() const { return *impl->expression; }

const Statement& ForInStatement::getBody() const { return *impl->body; }

void ForInStatement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ForInStatement::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind ForInStatement::getKind() const { return SyntaxKind::ForInStatement; }

void ForInStatement::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::SymbolTable&> ForInStatement::getLocals() const {
  return impl->getLocals();
}

void ForInStatement::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  const_cast<Impl*>(impl.get())->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> ForInStatement::getNextContainer() const {
  return impl->getNextContainer();
}

void ForInStatement::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  const_cast<Impl*>(impl.get())->setNextContainer(nextContainer);
}

// ================================================================================
// HeritageClause::Impl

struct HeritageClause::Impl : private NodeImpl {
  const ast::SyntaxKind token;
  const zc::Vector<zc::Own<ExpressionWithTypeArguments>> types;

  Impl(ast::SyntaxKind t, zc::Vector<zc::Own<ExpressionWithTypeArguments>>&& list)
      : NodeImpl(SyntaxKind::HeritageClause), token(t), types(zc::mv(list)) {}

  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// HeritageClause

HeritageClause::HeritageClause(ast::SyntaxKind token,
                               zc::Vector<zc::Own<ExpressionWithTypeArguments>>&& types) noexcept
    : Node(), impl(zc::heap<Impl>(token, zc::mv(types))) {}

HeritageClause::~HeritageClause() noexcept(false) = default;

ast::SyntaxKind HeritageClause::getToken() const { return impl->token; }

const zc::Vector<zc::Own<ExpressionWithTypeArguments>>& HeritageClause::getTypes() const {
  return impl->types;
}

SyntaxKind HeritageClause::getKind() const { return SyntaxKind::HeritageClause; }

void HeritageClause::accept(Visitor& visitor) const { visitor.visit(*this); }

void HeritageClause::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& HeritageClause::getSourceRange() const { return impl->getSourceRange(); }

// ================================================================================
// PropertySignature::Impl

struct PropertySignature::Impl : private NamedDeclarationImpl, private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const zc::Maybe<zc::Own<ast::TokenNode>> optional;
  const zc::Maybe<zc::Own<TypeNode>> type;
  const zc::Maybe<zc::Own<Expression>> initializer;

  Impl(zc::Vector<ast::SyntaxKind> m, zc::Own<Identifier> n, zc::Maybe<zc::Own<ast::TokenNode>> opt,
       zc::Maybe<zc::Own<TypeNode>> t, zc::Maybe<zc::Own<Expression>> init)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::PropertySignature),
        modifiers(zc::mv(m)),
        optional(zc::mv(opt)),
        type(zc::mv(t)),
        initializer(zc::mv(init)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

PropertySignature::PropertySignature(zc::Vector<ast::SyntaxKind> modifiers,
                                     zc::Own<Identifier> name,
                                     zc::Maybe<zc::Own<ast::TokenNode>> optional,
                                     zc::Maybe<zc::Own<TypeNode>> type,
                                     zc::Maybe<zc::Own<Expression>> initializer) noexcept
    : impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(name), zc::mv(optional), zc::mv(type),
                          zc::mv(initializer))) {}

PropertySignature::~PropertySignature() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
PropertySignature::getName() const {
  return impl->getName();
}

zc::ArrayPtr<const ast::SyntaxKind> PropertySignature::getModifiers() const {
  return impl->modifiers;
}

bool PropertySignature::isOptional() const { return impl->optional != zc::none; }

zc::Maybe<const TypeNode&> PropertySignature::getType() const { return impl->type; }

zc::Maybe<const Expression&> PropertySignature::getInitializer() const { return impl->initializer; }

SyntaxKind PropertySignature::getKind() const { return SyntaxKind::PropertySignature; }

void PropertySignature::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> PropertySignature::getSymbol() const { return impl->getSymbol(); }

void PropertySignature::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void PropertySignature::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& PropertySignature::getSourceRange() const {
  return impl->getSourceRange();
}

// ================================================================================
// MethodSignature::Impl

struct MethodSignature::Impl : private NamedDeclarationImpl,
                               private LocalsContainerImpl,
                               private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const zc::Maybe<zc::Own<ast::TokenNode>> optional;
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const zc::Maybe<zc::Own<ReturnTypeNode>> returnType;

  Impl(zc::Vector<ast::SyntaxKind> m, zc::Own<Identifier> n, zc::Maybe<zc::Own<ast::TokenNode>> opt,
       zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Vector<zc::Own<ParameterDeclaration>>&& p, zc::Maybe<zc::Own<ReturnTypeNode>> r)
      : NamedDeclarationImpl(zc::mv(n)),
        LocalsContainerImpl(),
        NodeImpl(SyntaxKind::MethodSignature),
        modifiers(zc::mv(m)),
        optional(zc::mv(opt)),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        parameters(zc::mv(p)),
        returnType(zc::mv(r)) {}

  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

MethodSignature::MethodSignature(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> optional,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType) noexcept
    : impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(name), zc::mv(optional), zc::mv(typeParameters),
                          zc::mv(parameters), zc::mv(returnType))) {}

MethodSignature::~MethodSignature() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> MethodSignature::getName()
    const {
  return impl->getName();
}

zc::ArrayPtr<const ast::SyntaxKind> MethodSignature::getModifiers() const {
  return impl->modifiers;
}

bool MethodSignature::isOptional() const { return impl->optional != zc::none; }

const NodeList<TypeParameterDeclaration>& MethodSignature::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<ParameterDeclaration>& MethodSignature::getParameters() const {
  return impl->parameters;
}

zc::Maybe<const ReturnTypeNode&> MethodSignature::getReturnType() const { return impl->returnType; }

SyntaxKind MethodSignature::getKind() const { return SyntaxKind::MethodSignature; }

void MethodSignature::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> MethodSignature::getSymbol() const { return impl->getSymbol(); }

void MethodSignature::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void MethodSignature::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& MethodSignature::getSourceRange() const {
  return impl->getSourceRange();
}

zc::Maybe<const symbol::SymbolTable&> MethodSignature::getLocals() const {
  return impl->getLocals();
}

void MethodSignature::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> MethodSignature::getNextContainer() const {
  return impl->getNextContainer();
}

void MethodSignature::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

struct SemicolonInterfaceElement::Impl : private NamedDeclarationImpl, private NodeImpl {
  Impl()
      : NamedDeclarationImpl(zc::heap<Identifier>(zc::str(";"))),
        NodeImpl(SyntaxKind::SemicolonInterfaceElement) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

SemicolonInterfaceElement::SemicolonInterfaceElement() noexcept
    : InterfaceElement(), impl(zc::heap<Impl>()) {}

SemicolonInterfaceElement::~SemicolonInterfaceElement() noexcept(false) = default;

SyntaxKind SemicolonInterfaceElement::getKind() const {
  return SyntaxKind::SemicolonInterfaceElement;
}

void SemicolonInterfaceElement::accept(Visitor& visitor) const { visitor.visit(*this); }

void SemicolonInterfaceElement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& SemicolonInterfaceElement::getSourceRange() const {
  return impl->getSourceRange();
}

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
SemicolonInterfaceElement::getName() const {
  return impl->getName();
}

zc::Maybe<const symbol::Symbol&> SemicolonInterfaceElement::getSymbol() const {
  return impl->getSymbol();
}

void SemicolonInterfaceElement::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

// ================================================================================
// SemicolonClassElement::Impl

struct SemicolonClassElement::Impl : private NamedDeclarationImpl, private NodeImpl {
  Impl()
      : NamedDeclarationImpl(zc::heap<Identifier>(zc::str(";"))),
        NodeImpl(SyntaxKind::SemicolonClassElement) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// SemicolonClassElement

SemicolonClassElement::SemicolonClassElement() noexcept : ClassElement(), impl(zc::heap<Impl>()) {}

SemicolonClassElement::~SemicolonClassElement() noexcept(false) = default;

SyntaxKind SemicolonClassElement::getKind() const { return SyntaxKind::SemicolonClassElement; }

void SemicolonClassElement::accept(Visitor& visitor) const { visitor.visit(*this); }

void SemicolonClassElement::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& SemicolonClassElement::getSourceRange() const {
  return impl->getSourceRange();
}

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
SemicolonClassElement::getName() const {
  return impl->getName();
}

zc::Maybe<const symbol::Symbol&> SemicolonClassElement::getSymbol() const {
  return impl->getSymbol();
}

void SemicolonClassElement::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

// ================================================================================
// MethodDeclaration::Impl

struct MethodDeclaration::Impl : private NamedDeclarationImpl,
                                 private LocalsContainerImpl,
                                 private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const zc::Maybe<zc::Own<ast::TokenNode>> optional;
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const zc::Maybe<zc::Own<ReturnTypeNode>> returnType;
  const zc::Maybe<zc::Own<Statement>> body;

  Impl(zc::Vector<ast::SyntaxKind> m, zc::Own<Identifier> n, zc::Maybe<zc::Own<ast::TokenNode>> opt,
       zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Vector<zc::Own<ParameterDeclaration>>&& p, zc::Maybe<zc::Own<ReturnTypeNode>> r,
       zc::Maybe<zc::Own<Statement>> b)
      : NamedDeclarationImpl(zc::mv(n)),
        LocalsContainerImpl(),
        NodeImpl(SyntaxKind::MethodDeclaration),
        modifiers(zc::mv(m)),
        optional(zc::mv(opt)),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        parameters(zc::mv(p)),
        returnType(zc::mv(r)),
        body(zc::mv(b)) {}

  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// MethodDeclaration

MethodDeclaration::MethodDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
    zc::Maybe<zc::Own<ast::TokenNode>> optional,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Maybe<zc::Own<Statement>> body) noexcept
    : ClassElement(),
      LocalsContainer(),
      impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(name), zc::mv(optional), zc::mv(typeParameters),
                          zc::mv(parameters), zc::mv(returnType), zc::mv(body))) {}

MethodDeclaration::~MethodDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
MethodDeclaration::getName() const {
  return impl->getName();
}

zc::ArrayPtr<const ast::SyntaxKind> MethodDeclaration::getModifiers() const {
  return impl->modifiers;
}

bool MethodDeclaration::isOptional() const { return impl->optional != zc::none; }

const NodeList<TypeParameterDeclaration>& MethodDeclaration::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<ParameterDeclaration>& MethodDeclaration::getParameters() const {
  return impl->parameters;
}

zc::Maybe<const ReturnTypeNode&> MethodDeclaration::getReturnType() const {
  return impl->returnType;
}

zc::Maybe<const Statement&> MethodDeclaration::getBody() const { return impl->body; }

zc::Maybe<const symbol::SymbolTable&> MethodDeclaration::getLocals() const {
  return impl->getLocals();
}

void MethodDeclaration::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> MethodDeclaration::getNextContainer() const {
  return impl->getNextContainer();
}

void MethodDeclaration::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

SyntaxKind MethodDeclaration::getKind() const { return SyntaxKind::MethodDeclaration; }

void MethodDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> MethodDeclaration::getSymbol() const { return impl->getSymbol(); }

void MethodDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void MethodDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& MethodDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

struct InitDeclaration::Impl : private NamedDeclarationImpl,
                               private LocalsContainerImpl,
                               private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const zc::Maybe<zc::Own<ReturnTypeNode>> returnType;
  const zc::Maybe<zc::Own<Statement>> body;

  Impl(zc::Vector<ast::SyntaxKind> m, zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Vector<zc::Own<ParameterDeclaration>>&& p, zc::Maybe<zc::Own<ReturnTypeNode>> r,
       zc::Maybe<zc::Own<Statement>> b)
      : NamedDeclarationImpl(zc::heap<Identifier>("init"_zc)),
        LocalsContainerImpl(),
        NodeImpl(SyntaxKind::InitDeclaration),
        modifiers(zc::mv(m)),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        parameters(zc::mv(p)),
        returnType(zc::mv(r)),
        body(zc::mv(b)) {}

  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

InitDeclaration::InitDeclaration(
    zc::Vector<ast::SyntaxKind> modifiers,
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Maybe<zc::Own<ReturnTypeNode>> returnType, zc::Maybe<zc::Own<Statement>> body) noexcept
    : ClassElement(),
      LocalsContainer(),
      impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(typeParameters), zc::mv(parameters),
                          zc::mv(returnType), zc::mv(body))) {}

InitDeclaration::~InitDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> InitDeclaration::getName()
    const {
  return impl->getName();
}

zc::ArrayPtr<const ast::SyntaxKind> InitDeclaration::getModifiers() const {
  return impl->modifiers;
}

const NodeList<TypeParameterDeclaration>& InitDeclaration::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<ParameterDeclaration>& InitDeclaration::getParameters() const {
  return impl->parameters;
}

zc::Maybe<const ReturnTypeNode&> InitDeclaration::getReturnType() const { return impl->returnType; }

zc::Maybe<const Statement&> InitDeclaration::getBody() const { return impl->body; }

zc::Maybe<const symbol::SymbolTable&> InitDeclaration::getLocals() const {
  return impl->getLocals();
}

void InitDeclaration::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> InitDeclaration::getNextContainer() const {
  return impl->getNextContainer();
}

void InitDeclaration::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

SyntaxKind InitDeclaration::getKind() const { return SyntaxKind::InitDeclaration; }

void InitDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> InitDeclaration::getSymbol() const { return impl->getSymbol(); }

void InitDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void InitDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& InitDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

struct DeinitDeclaration::Impl : private NamedDeclarationImpl,
                                 private LocalsContainerImpl,
                                 private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const zc::Maybe<zc::Own<Statement>> body;

  Impl(zc::Vector<ast::SyntaxKind> m, zc::Maybe<zc::Own<Statement>> b)
      : NamedDeclarationImpl(zc::heap<Identifier>("deinit"_zc)),
        LocalsContainerImpl(),
        NodeImpl(SyntaxKind::DeinitDeclaration),
        modifiers(zc::mv(m)),
        body(zc::mv(b)) {}

  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

DeinitDeclaration::DeinitDeclaration(zc::Vector<ast::SyntaxKind> modifiers,
                                     zc::Maybe<zc::Own<Statement>> body) noexcept
    : ClassElement(), LocalsContainer(), impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(body))) {}

DeinitDeclaration::~DeinitDeclaration() noexcept(false) = default;

zc::ArrayPtr<const ast::SyntaxKind> DeinitDeclaration::getModifiers() const {
  return impl->modifiers;
}

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
DeinitDeclaration::getName() const {
  return impl->getName();
}

zc::Maybe<const Statement&> DeinitDeclaration::getBody() const { return impl->body; }

zc::Maybe<const symbol::SymbolTable&> DeinitDeclaration::getLocals() const {
  return impl->getLocals();
}

void DeinitDeclaration::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> DeinitDeclaration::getNextContainer() const {
  return impl->getNextContainer();
}

void DeinitDeclaration::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

SyntaxKind DeinitDeclaration::getKind() const { return SyntaxKind::DeinitDeclaration; }

void DeinitDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> DeinitDeclaration::getSymbol() const { return impl->getSymbol(); }

void DeinitDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

void DeinitDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& DeinitDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

struct GetAccessor::Impl : private NamedDeclarationImpl,
                           private LocalsContainerImpl,
                           private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const zc::Maybe<zc::Own<ReturnTypeNode>> returnType;
  const zc::Maybe<zc::Own<Statement>> body;

  Impl(zc::Vector<ast::SyntaxKind> m, zc::Own<Identifier> n,
       zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Vector<zc::Own<ParameterDeclaration>>&& p, zc::Maybe<zc::Own<ReturnTypeNode>> r,
       zc::Maybe<zc::Own<Statement>> b)
      : NamedDeclarationImpl(zc::mv(n)),
        LocalsContainerImpl(),
        NodeImpl(SyntaxKind::GetAccessor),
        modifiers(zc::mv(m)),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        parameters(zc::mv(p)),
        returnType(zc::mv(r)),
        body(zc::mv(b)) {}

  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// GetAccessor

GetAccessor::GetAccessor(zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
                         zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
                         zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
                         zc::Maybe<zc::Own<ReturnTypeNode>> returnType,
                         zc::Maybe<zc::Own<Statement>> body) noexcept
    : ClassElement(),
      LocalsContainer(),
      impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(name), zc::mv(typeParameters),
                          zc::mv(parameters), zc::mv(returnType), zc::mv(body))) {}

GetAccessor::~GetAccessor() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> GetAccessor::getName()
    const {
  return impl->getName();
}

zc::ArrayPtr<const ast::SyntaxKind> GetAccessor::getModifiers() const { return impl->modifiers; }

const NodeList<TypeParameterDeclaration>& GetAccessor::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<ParameterDeclaration>& GetAccessor::getParameters() const {
  return impl->parameters;
}

zc::Maybe<const ReturnTypeNode&> GetAccessor::getReturnType() const { return impl->returnType; }

zc::Maybe<const Statement&> GetAccessor::getBody() const { return impl->body; }

zc::Maybe<const symbol::SymbolTable&> GetAccessor::getLocals() const { return impl->getLocals(); }

void GetAccessor::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> GetAccessor::getNextContainer() const {
  return impl->getNextContainer();
}

void GetAccessor::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

SyntaxKind GetAccessor::getKind() const { return SyntaxKind::GetAccessor; }

void GetAccessor::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> GetAccessor::getSymbol() const { return impl->getSymbol(); }

void GetAccessor::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) { impl->setSymbol(symbol); }

void GetAccessor::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& GetAccessor::getSourceRange() const { return impl->getSourceRange(); }

// ================================================================================
// SetAccessor::Impl

struct SetAccessor::Impl : private NamedDeclarationImpl,
                           private LocalsContainerImpl,
                           private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const NodeList<TypeParameterDeclaration> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const zc::Maybe<zc::Own<ReturnTypeNode>> returnType;
  const zc::Maybe<zc::Own<Statement>> body;

  Impl(zc::Vector<ast::SyntaxKind> m, zc::Own<Identifier> n,
       zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> tp,
       zc::Vector<zc::Own<ParameterDeclaration>>&& p, zc::Maybe<zc::Own<ReturnTypeNode>> r,
       zc::Maybe<zc::Own<Statement>> b)
      : NamedDeclarationImpl(zc::mv(n)),
        LocalsContainerImpl(),
        NodeImpl(SyntaxKind::SetAccessor),
        modifiers(zc::mv(m)),
        typeParameters(zc::mv(tp).orDefault(zc::Vector<zc::Own<TypeParameterDeclaration>>())),
        parameters(zc::mv(p)),
        returnType(zc::mv(r)),
        body(zc::mv(b)) {}

  using LocalsContainerImpl::getLocals;
  using LocalsContainerImpl::getNextContainer;
  using LocalsContainerImpl::setLocals;
  using LocalsContainerImpl::setNextContainer;
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// SetAccessor

SetAccessor::SetAccessor(zc::Vector<ast::SyntaxKind> modifiers, zc::Own<Identifier> name,
                         zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
                         zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
                         zc::Maybe<zc::Own<ReturnTypeNode>> returnType,
                         zc::Maybe<zc::Own<Statement>> body) noexcept
    : ClassElement(),
      LocalsContainer(),
      impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(name), zc::mv(typeParameters),
                          zc::mv(parameters), zc::mv(returnType), zc::mv(body))) {}

SetAccessor::~SetAccessor() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>> SetAccessor::getName()
    const {
  return impl->getName();
}

zc::ArrayPtr<const ast::SyntaxKind> SetAccessor::getModifiers() const { return impl->modifiers; }

const NodeList<TypeParameterDeclaration>& SetAccessor::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<ParameterDeclaration>& SetAccessor::getParameters() const {
  return impl->parameters;
}

zc::Maybe<const ReturnTypeNode&> SetAccessor::getReturnType() const { return impl->returnType; }

zc::Maybe<const Statement&> SetAccessor::getBody() const { return impl->body; }

zc::Maybe<const symbol::SymbolTable&> SetAccessor::getLocals() const { return impl->getLocals(); }

void SetAccessor::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->setLocals(locals);
}

zc::Maybe<const LocalsContainer&> SetAccessor::getNextContainer() const {
  return impl->getNextContainer();
}

void SetAccessor::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->setNextContainer(nextContainer);
}

SyntaxKind SetAccessor::getKind() const { return SyntaxKind::SetAccessor; }

void SetAccessor::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> SetAccessor::getSymbol() const { return impl->getSymbol(); }

void SetAccessor::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) { impl->setSymbol(symbol); }

void SetAccessor::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& SetAccessor::getSourceRange() const { return impl->getSourceRange(); }

// ================================================================================
// PropertyDeclaration::Impl

struct PropertyDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  const zc::Vector<ast::SyntaxKind> modifiers;
  const zc::Maybe<zc::Own<TypeNode>> type;
  const zc::Maybe<zc::Own<Expression>> initializer;

  Impl(zc::Vector<ast::SyntaxKind> mods, zc::Own<Identifier> n, zc::Maybe<zc::Own<TypeNode>> t,
       zc::Maybe<zc::Own<Expression>> initializer)
      : NamedDeclarationImpl(zc::mv(n)),
        NodeImpl(SyntaxKind::PropertyDeclaration),
        modifiers(zc::mv(mods)),
        type(zc::mv(t)),
        initializer(zc::mv(initializer)) {}

  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

// ================================================================================
// PropertyDeclaration

PropertyDeclaration::PropertyDeclaration(zc::Vector<ast::SyntaxKind> modifiers,
                                         zc::Own<Identifier> name,
                                         zc::Maybe<zc::Own<TypeNode>> type,
                                         zc::Maybe<zc::Own<Expression>> initializer) noexcept
    : ClassElement(),
      impl(zc::heap<Impl>(zc::mv(modifiers), zc::mv(name), zc::mv(type), zc::mv(initializer))) {}

PropertyDeclaration::~PropertyDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
PropertyDeclaration::getName() const {
  return impl->getName();
}

zc::ArrayPtr<const ast::SyntaxKind> PropertyDeclaration::getModifiers() const {
  return impl->modifiers;
}

zc::Maybe<const TypeNode&> PropertyDeclaration::getType() const { return impl->type; }

zc::Maybe<const Expression&> PropertyDeclaration::getInitializer() const {
  return impl->initializer;
}

SyntaxKind PropertyDeclaration::getKind() const { return SyntaxKind::PropertyDeclaration; }

void PropertyDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

void PropertyDeclaration::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& PropertyDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

zc::Maybe<const symbol::Symbol&> PropertyDeclaration::getSymbol() const {
  return impl->getSymbol();
}

void PropertyDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  impl->setSymbol(symbol);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
