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

#include "zomlang/compiler/ast/type.h"

#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/visitor.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declaration of DeclarationImpl::Impl to avoid incomplete type error
struct DeclarationImpl::Impl {
  zc::Maybe<const symbol::Symbol&> symbol = zc::none;
};

// Forward declaration of NamedDeclarationImpl::Impl
struct NamedDeclarationImpl::Impl {
  const zc::Own<Identifier> name;

  explicit Impl(zc::Own<Identifier> name) : name(zc::mv(name)) {}
};

// TypeReference
struct TypeReferenceNode::Impl : private NodeImpl {
  zc::Own<Identifier> name;
  zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments;

  explicit Impl(zc::Own<Identifier> name, zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments)
      : NodeImpl(SyntaxKind::TypeReferenceNode),
        name(zc::mv(name)),
        typeArguments(zc::mv(typeArguments)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

TypeReferenceNode::TypeReferenceNode(
    zc::Own<Identifier> name, zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(name), zc::mv(typeArguments))) {}

TypeReferenceNode::~TypeReferenceNode() noexcept(false) = default;

const ast::Identifier& TypeReferenceNode::getName() const { return *impl->name; }

void TypeReferenceNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TypeReferenceNode::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind TypeReferenceNode::getKind() const { return impl->getKind(); }

NodeFlags TypeReferenceNode::getFlags() const { return impl->getFlags(); }
void TypeReferenceNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void TypeReferenceNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// ArrayTypeNode
struct ArrayTypeNode::Impl : private NodeImpl {
  zc::Own<TypeNode> elementType;

  explicit Impl(zc::Own<TypeNode> elementType)
      : NodeImpl(SyntaxKind::ArrayTypeNode), elementType(zc::mv(elementType)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ArrayTypeNode::ArrayTypeNode(zc::Own<TypeNode> elementType) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(elementType))) {}

ArrayTypeNode::~ArrayTypeNode() noexcept(false) = default;

const TypeNode& ArrayTypeNode::getElementType() const { return *impl->elementType; }

SyntaxKind ArrayTypeNode::getKind() const { return impl->getKind(); }

NodeFlags ArrayTypeNode::getFlags() const { return impl->getFlags(); }
void ArrayTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void ArrayTypeNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ArrayTypeNode::getSourceRange() const { return impl->getSourceRange(); }

void ArrayTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// UnionTypeNode
struct UnionTypeNode::Impl : private NodeImpl {
  NodeList<TypeNode> types;

  explicit Impl(zc::Vector<zc::Own<TypeNode>>&& types)
      : NodeImpl(SyntaxKind::UnionTypeNode), types(zc::mv(types)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

UnionTypeNode::UnionTypeNode(zc::Vector<zc::Own<TypeNode>>&& types) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(types))) {}

UnionTypeNode::~UnionTypeNode() noexcept(false) = default;

const NodeList<TypeNode>& UnionTypeNode::getTypes() const { return impl->types; }

SyntaxKind UnionTypeNode::getKind() const { return impl->getKind(); }

NodeFlags UnionTypeNode::getFlags() const { return impl->getFlags(); }
void UnionTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void UnionTypeNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& UnionTypeNode::getSourceRange() const { return impl->getSourceRange(); }

void UnionTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// IntersectionTypeNode
struct IntersectionTypeNode::Impl : private NodeImpl {
  NodeList<TypeNode> types;

  explicit Impl(zc::Vector<zc::Own<TypeNode>>&& types)
      : NodeImpl(SyntaxKind::IntersectionTypeNode), types(zc::mv(types)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

IntersectionTypeNode::IntersectionTypeNode(zc::Vector<zc::Own<TypeNode>>&& types) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(types))) {}

IntersectionTypeNode::~IntersectionTypeNode() noexcept(false) = default;

const NodeList<TypeNode>& IntersectionTypeNode::getTypes() const { return impl->types; }

SyntaxKind IntersectionTypeNode::getKind() const { return impl->getKind(); }

NodeFlags IntersectionTypeNode::getFlags() const { return impl->getFlags(); }
void IntersectionTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void IntersectionTypeNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& IntersectionTypeNode::getSourceRange() const {
  return impl->getSourceRange();
}

void IntersectionTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// ParenthesizedTypeNode
struct ParenthesizedTypeNode::Impl : private NodeImpl {
  zc::Own<TypeNode> type;

  explicit Impl(zc::Own<TypeNode> type)
      : NodeImpl(SyntaxKind::ParenthesizedTypeNode), type(zc::mv(type)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ParenthesizedTypeNode::ParenthesizedTypeNode(zc::Own<TypeNode> type) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(type))) {}

ParenthesizedTypeNode::~ParenthesizedTypeNode() noexcept(false) = default;

const TypeNode& ParenthesizedTypeNode::getType() const { return *impl->type; }

SyntaxKind ParenthesizedTypeNode::getKind() const { return impl->getKind(); }

NodeFlags ParenthesizedTypeNode::getFlags() const { return impl->getFlags(); }
void ParenthesizedTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void ParenthesizedTypeNode::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& ParenthesizedTypeNode::getSourceRange() const {
  return impl->getSourceRange();
}

void ParenthesizedTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// PredefinedType implementations
BoolTypeNode::BoolTypeNode() noexcept : PredefinedTypeNode() {}
BoolTypeNode::~BoolTypeNode() noexcept(false) = default;
zc::StringPtr BoolTypeNode::getName() const { return "bool"_zc; }
SyntaxKind BoolTypeNode::getKind() const { return SyntaxKind::BoolTypeNode; }

NodeFlags BoolTypeNode::getFlags() const { return NodeFlags::None; }
void BoolTypeNode::setFlags(NodeFlags flags) { }

void BoolTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void BoolTypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& BoolTypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

I8TypeNode::I8TypeNode() noexcept : PredefinedTypeNode() {}
I8TypeNode::~I8TypeNode() noexcept(false) = default;
zc::StringPtr I8TypeNode::getName() const { return "i8"_zc; }
SyntaxKind I8TypeNode::getKind() const { return SyntaxKind::I8TypeNode; }

NodeFlags I8TypeNode::getFlags() const { return NodeFlags::None; }
void I8TypeNode::setFlags(NodeFlags flags) { }

void I8TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void I8TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& I8TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

I16TypeNode::I16TypeNode() noexcept : PredefinedTypeNode() {}
I16TypeNode::~I16TypeNode() noexcept(false) = default;
zc::StringPtr I16TypeNode::getName() const { return "i16"_zc; }
SyntaxKind I16TypeNode::getKind() const { return SyntaxKind::I16TypeNode; }

NodeFlags I16TypeNode::getFlags() const { return NodeFlags::None; }
void I16TypeNode::setFlags(NodeFlags flags) { }

void I16TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void I16TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& I16TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

I32TypeNode::I32TypeNode() noexcept : PredefinedTypeNode() {}
I32TypeNode::~I32TypeNode() noexcept(false) = default;
zc::StringPtr I32TypeNode::getName() const { return "i32"_zc; }
SyntaxKind I32TypeNode::getKind() const { return SyntaxKind::I32TypeNode; }

NodeFlags I32TypeNode::getFlags() const { return NodeFlags::None; }
void I32TypeNode::setFlags(NodeFlags flags) { }

void I32TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void I32TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& I32TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

I64TypeNode::I64TypeNode() noexcept : PredefinedTypeNode() {}
I64TypeNode::~I64TypeNode() noexcept(false) = default;
zc::StringPtr I64TypeNode::getName() const { return "i64"_zc; }
SyntaxKind I64TypeNode::getKind() const { return SyntaxKind::I64TypeNode; }

NodeFlags I64TypeNode::getFlags() const { return NodeFlags::None; }
void I64TypeNode::setFlags(NodeFlags flags) { }

void I64TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void I64TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& I64TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

U8TypeNode::U8TypeNode() noexcept : PredefinedTypeNode() {}
U8TypeNode::~U8TypeNode() noexcept(false) = default;
zc::StringPtr U8TypeNode::getName() const { return "u8"_zc; }
SyntaxKind U8TypeNode::getKind() const { return SyntaxKind::U8TypeNode; }

NodeFlags U8TypeNode::getFlags() const { return NodeFlags::None; }
void U8TypeNode::setFlags(NodeFlags flags) { }

void U8TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void U8TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& U8TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

U16TypeNode::U16TypeNode() noexcept : PredefinedTypeNode() {}
U16TypeNode::~U16TypeNode() noexcept(false) = default;
zc::StringPtr U16TypeNode::getName() const { return "u16"_zc; }
SyntaxKind U16TypeNode::getKind() const { return SyntaxKind::U16TypeNode; }

NodeFlags U16TypeNode::getFlags() const { return NodeFlags::None; }
void U16TypeNode::setFlags(NodeFlags flags) { }

void U16TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void U16TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& U16TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

U32TypeNode::U32TypeNode() noexcept : PredefinedTypeNode() {}
U32TypeNode::~U32TypeNode() noexcept(false) = default;
zc::StringPtr U32TypeNode::getName() const { return "u32"_zc; }
SyntaxKind U32TypeNode::getKind() const { return SyntaxKind::U32TypeNode; }

NodeFlags U32TypeNode::getFlags() const { return NodeFlags::None; }
void U32TypeNode::setFlags(NodeFlags flags) { }

void U32TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void U32TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& U32TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

U64TypeNode::U64TypeNode() noexcept : PredefinedTypeNode() {}
U64TypeNode::~U64TypeNode() noexcept(false) = default;
zc::StringPtr U64TypeNode::getName() const { return "u64"_zc; }
SyntaxKind U64TypeNode::getKind() const { return SyntaxKind::U64TypeNode; }

NodeFlags U64TypeNode::getFlags() const { return NodeFlags::None; }
void U64TypeNode::setFlags(NodeFlags flags) { }

void U64TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void U64TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& U64TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

F32TypeNode::F32TypeNode() noexcept : PredefinedTypeNode() {}
F32TypeNode::~F32TypeNode() noexcept(false) = default;
zc::StringPtr F32TypeNode::getName() const { return "f32"_zc; }
SyntaxKind F32TypeNode::getKind() const { return SyntaxKind::F32TypeNode; }

NodeFlags F32TypeNode::getFlags() const { return NodeFlags::None; }
void F32TypeNode::setFlags(NodeFlags flags) { }

void F32TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void F32TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& F32TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

F64TypeNode::F64TypeNode() noexcept : PredefinedTypeNode() {}
F64TypeNode::~F64TypeNode() noexcept(false) = default;
zc::StringPtr F64TypeNode::getName() const { return "f64"_zc; }
SyntaxKind F64TypeNode::getKind() const { return SyntaxKind::F64TypeNode; }

NodeFlags F64TypeNode::getFlags() const { return NodeFlags::None; }
void F64TypeNode::setFlags(NodeFlags flags) { }

void F64TypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void F64TypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& F64TypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

StrTypeNode::StrTypeNode() noexcept : PredefinedTypeNode() {}
StrTypeNode::~StrTypeNode() noexcept(false) = default;
zc::StringPtr StrTypeNode::getName() const { return "str"_zc; }
SyntaxKind StrTypeNode::getKind() const { return SyntaxKind::StrTypeNode; }

NodeFlags StrTypeNode::getFlags() const { return NodeFlags::None; }
void StrTypeNode::setFlags(NodeFlags flags) { }

void StrTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void StrTypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& StrTypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

UnitTypeNode::UnitTypeNode() noexcept : PredefinedTypeNode() {}
UnitTypeNode::~UnitTypeNode() noexcept(false) = default;
zc::StringPtr UnitTypeNode::getName() const { return "unit"_zc; }
SyntaxKind UnitTypeNode::getKind() const { return SyntaxKind::UnitTypeNode; }

NodeFlags UnitTypeNode::getFlags() const { return NodeFlags::None; }
void UnitTypeNode::setFlags(NodeFlags flags) { }

void UnitTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void UnitTypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& UnitTypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

NullTypeNode::NullTypeNode() noexcept : PredefinedTypeNode() {}
NullTypeNode::~NullTypeNode() noexcept(false) = default;
zc::StringPtr NullTypeNode::getName() const { return "null"_zc; }
SyntaxKind NullTypeNode::getKind() const { return SyntaxKind::NullTypeNode; }

NodeFlags NullTypeNode::getFlags() const { return NodeFlags::None; }
void NullTypeNode::setFlags(NodeFlags flags) { }

void NullTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }
void NullTypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for predefined types
}
const source::SourceRange& NullTypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

// ObjectTypeNode
struct ObjectTypeNode::Impl {
  NodeList<Node> members;

  explicit Impl(zc::Vector<zc::Own<Node>>&& members) : members(zc::mv(members)) {}
};

ObjectTypeNode::ObjectTypeNode(zc::Vector<zc::Own<Node>>&& members) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(members))) {}

ObjectTypeNode::~ObjectTypeNode() noexcept(false) = default;

const NodeList<Node>& ObjectTypeNode::getMembers() const { return impl->members; }

SyntaxKind ObjectTypeNode::getKind() const { return SyntaxKind::ObjectTypeNode; }

NodeFlags ObjectTypeNode::getFlags() const { return NodeFlags::None; }
void ObjectTypeNode::setFlags(NodeFlags flags) { }


void ObjectTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

void ObjectTypeNode::setSourceRange(const source::SourceRange&& range) {
  // TODO: Implement source range tracking for ObjectTypeNode
}

const source::SourceRange& ObjectTypeNode::getSourceRange() const {
  static source::SourceRange empty;
  return empty;
}

// NamedTupleElement
struct NamedTupleElement::Impl : private NodeImpl {
  zc::Own<Identifier> name;
  zc::Own<TypeNode> type;

  Impl(zc::Own<Identifier> name, zc::Own<TypeNode> type)
      : NodeImpl(SyntaxKind::NamedTupleElement), name(zc::mv(name)), type(zc::mv(type)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

NamedTupleElement::NamedTupleElement(zc::Own<Identifier> name, zc::Own<TypeNode> type) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(name), zc::mv(type))) {}

NamedTupleElement::~NamedTupleElement() noexcept(false) = default;

const Identifier& NamedTupleElement::getName() const { return *impl->name; }

const TypeNode& NamedTupleElement::getType() const { return *impl->type; }

SyntaxKind NamedTupleElement::getKind() const { return impl->getKind(); }

NodeFlags NamedTupleElement::getFlags() const { return impl->getFlags(); }
void NamedTupleElement::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void NamedTupleElement::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& NamedTupleElement::getSourceRange() const {
  return impl->getSourceRange();
}

void NamedTupleElement::accept(Visitor& visitor) const { visitor.visit(*this); }

// TupleTypeNode
struct TupleTypeNode::Impl : private NodeImpl {
  NodeList<TypeNode> elementTypes;

  explicit Impl(zc::Vector<zc::Own<TypeNode>>&& elementTypes)
      : NodeImpl(SyntaxKind::TupleTypeNode), elementTypes(zc::mv(elementTypes)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

TupleTypeNode::TupleTypeNode(zc::Vector<zc::Own<TypeNode>>&& elementTypes) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(elementTypes))) {}

TupleTypeNode::~TupleTypeNode() noexcept(false) = default;

const NodeList<TypeNode>& TupleTypeNode::getElementTypes() const { return impl->elementTypes; }

SyntaxKind TupleTypeNode::getKind() const { return impl->getKind(); }

NodeFlags TupleTypeNode::getFlags() const { return impl->getFlags(); }
void TupleTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void TupleTypeNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TupleTypeNode::getSourceRange() const { return impl->getSourceRange(); }

void TupleTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// ReturnTypeNode
struct ReturnTypeNode::Impl : private NodeImpl {
  zc::Own<TypeNode> type;
  zc::Maybe<zc::Own<TypeNode>> errorType;

  Impl(zc::Own<TypeNode> type, zc::Maybe<zc::Own<TypeNode>> errorType)
      : NodeImpl(SyntaxKind::ReturnTypeNode), type(zc::mv(type)), errorType(zc::mv(errorType)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

ReturnTypeNode::ReturnTypeNode(zc::Own<TypeNode> type,
                               zc::Maybe<zc::Own<TypeNode>> errorType) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(type), zc::mv(errorType))) {}

ReturnTypeNode::~ReturnTypeNode() noexcept(false) = default;

const TypeNode& ReturnTypeNode::getType() const { return *impl->type; }

zc::Maybe<const TypeNode&> ReturnTypeNode::getErrorType() const { return impl->errorType; }

void ReturnTypeNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& ReturnTypeNode::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind ReturnTypeNode::getKind() const { return impl->getKind(); }

NodeFlags ReturnTypeNode::getFlags() const { return impl->getFlags(); }
void ReturnTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void ReturnTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// FunctionTypeNode
struct FunctionTypeNode::Impl : private NodeImpl {
  const zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters;
  const NodeList<ParameterDeclaration> parameters;
  const zc::Own<ReturnTypeNode> returnType;

  Impl(zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
       zc::Vector<zc::Own<ParameterDeclaration>>&& parameters, zc::Own<ReturnTypeNode> returnType)
      : NodeImpl(SyntaxKind::FunctionTypeNode),
        typeParameters(zc::mv(typeParameters)),
        parameters(zc::mv(parameters)),
        returnType(zc::mv(returnType)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

FunctionTypeNode::FunctionTypeNode(
    zc::Maybe<zc::Vector<zc::Own<TypeParameterDeclaration>>> typeParameters,
    zc::Vector<zc::Own<ParameterDeclaration>>&& parameters,
    zc::Own<ReturnTypeNode> returnType) noexcept
    : TypeNode(),
      impl(zc::heap<Impl>(zc::mv(typeParameters), zc::mv(parameters), zc::mv(returnType))) {}

FunctionTypeNode::~FunctionTypeNode() noexcept(false) = default;

zc::Maybe<zc::ArrayPtr<const zc::Own<TypeParameterDeclaration>>>
FunctionTypeNode::getTypeParameters() const {
  return impl->typeParameters.map([](auto& typeParameters) { return typeParameters.asPtr(); });
}

const NodeList<ParameterDeclaration>& FunctionTypeNode::getParameters() const {
  return impl->parameters;
}

const ReturnTypeNode& FunctionTypeNode::getReturnType() const { return *impl->returnType; }

SyntaxKind FunctionTypeNode::getKind() const { return impl->getKind(); }

NodeFlags FunctionTypeNode::getFlags() const { return impl->getFlags(); }
void FunctionTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void FunctionTypeNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& FunctionTypeNode::getSourceRange() const {
  return impl->getSourceRange();
}

void FunctionTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// OptionalTypeNode
struct OptionalTypeNode::Impl : private NodeImpl {
  zc::Own<TypeNode> type;

  explicit Impl(zc::Own<TypeNode> type)
      : NodeImpl(SyntaxKind::OptionalTypeNode), type(zc::mv(type)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

OptionalTypeNode::OptionalTypeNode(zc::Own<TypeNode> type) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(type))) {}

OptionalTypeNode::~OptionalTypeNode() noexcept(false) = default;

const TypeNode& OptionalTypeNode::getType() const { return *impl->type; }

SyntaxKind OptionalTypeNode::getKind() const { return impl->getKind(); }

NodeFlags OptionalTypeNode::getFlags() const { return impl->getFlags(); }
void OptionalTypeNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void OptionalTypeNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& OptionalTypeNode::getSourceRange() const {
  return impl->getSourceRange();
}

void OptionalTypeNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// TypeQueryNode
struct TypeQueryNode::Impl : private NodeImpl {
  const zc::Own<Expression> expr;

  explicit Impl(zc::Own<Expression> expr)
      : NodeImpl(SyntaxKind::TypeQueryNode), expr(zc::mv(expr)) {}

  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;
};

TypeQueryNode::TypeQueryNode(zc::Own<Expression> expr) noexcept
    : TypeNode(), impl(zc::heap<Impl>(zc::mv(expr))) {}

TypeQueryNode::~TypeQueryNode() noexcept(false) = default;

const Expression& TypeQueryNode::getExpression() const { return *impl->expr; }

void TypeQueryNode::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TypeQueryNode::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind TypeQueryNode::getKind() const { return impl->getKind(); }

NodeFlags TypeQueryNode::getFlags() const { return impl->getFlags(); }
void TypeQueryNode::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void TypeQueryNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// TypeParameterDeclaration
struct TypeParameterDeclaration::Impl : private NamedDeclarationImpl, private NodeImpl {
  zc::Maybe<zc::Own<TypeNode>> constraint;

  Impl(zc::Own<Identifier> name, zc::Maybe<zc::Own<TypeNode>> constraint)
      : NamedDeclarationImpl(zc::mv(name)),
        NodeImpl(SyntaxKind::TypeParameterDeclaration),
        constraint(zc::mv(constraint)) {}

  // Forward NodeImpl methods
  using NodeImpl::getFlags;
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setFlags;
  using NodeImpl::setSourceRange;

  // Forward NamedDeclarationImpl methods
  using NamedDeclarationImpl::getName;
  using NamedDeclarationImpl::getSymbol;
  using NamedDeclarationImpl::setSymbol;
};

TypeParameterDeclaration::TypeParameterDeclaration(zc::Own<Identifier> name,
                                                   zc::Maybe<zc::Own<TypeNode>> constraint) noexcept
    : impl(zc::heap<Impl>(zc::mv(name), zc::mv(constraint))) {}

TypeParameterDeclaration::~TypeParameterDeclaration() noexcept(false) = default;

zc::OneOf<zc::Maybe<const Identifier&>, zc::Maybe<const BindingPattern&>>
TypeParameterDeclaration::getName() const {
  return impl->getName();
}

zc::Maybe<const TypeNode&> TypeParameterDeclaration::getConstraint() const {
  return impl->constraint;
}

void TypeParameterDeclaration::setSourceRange(const source::SourceRange&& range) {
  const_cast<Impl*>(impl.get())->setSourceRange(zc::mv(range));
}

const source::SourceRange& TypeParameterDeclaration::getSourceRange() const {
  return impl->getSourceRange();
}

SyntaxKind TypeParameterDeclaration::getKind() const { return impl->getKind(); }

NodeFlags TypeParameterDeclaration::getFlags() const { return impl->getFlags(); }
void TypeParameterDeclaration::setFlags(NodeFlags flags) { const_cast<Impl*>(impl.get())->setFlags(flags); }


void TypeParameterDeclaration::accept(Visitor& visitor) const { visitor.visit(*this); }

zc::Maybe<const symbol::Symbol&> TypeParameterDeclaration::getSymbol() const {
  return impl->getSymbol();
}

void TypeParameterDeclaration::setSymbol(zc::Maybe<const symbol::Symbol&> symbol) {
  const_cast<Impl*>(impl.get())->setSymbol(symbol);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
