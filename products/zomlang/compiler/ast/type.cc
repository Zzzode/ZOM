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

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {

bool isValidPredefinedType(zc::StringPtr name) noexcept {
  constexpr zc::StringPtr VALID_TYPES[] = {"i8"_zc,  "i64"_zc,  "i32"_zc,  "i64"_zc, "u8"_zc,
                                           "u16"_zc, "u32"_zc,  "u64"_zc,  "f32"_zc, "f64"_zc,
                                           "str"_zc, "bool"_zc, "null"_zc, "unit"_zc};

  for (const auto& validType : VALID_TYPES) {
    if (name == validType) { return true; }
  }
  return false;
}

}  // namespace

// Type base class
Type::Type(SyntaxKind kind) noexcept : Node(kind) {}
Type::~Type() noexcept(false) = default;

void Type::accept(Visitor& visitor) const { visitor.visit(*this); }

// TypeReference
struct TypeReference::Impl {
  zc::Own<Identifier> name;
  zc::Maybe<zc::Vector<zc::Own<Type>>> typeArguments;

  explicit Impl(zc::Own<Identifier> name, zc::Maybe<zc::Vector<zc::Own<Type>>> typeArguments)
      : name(zc::mv(name)), typeArguments(zc::mv(typeArguments)) {}
};

TypeReference::TypeReference(zc::Own<Identifier> name,
                             zc::Maybe<zc::Vector<zc::Own<Type>>> typeArguments) noexcept
    : Type(SyntaxKind::kTypeReference), impl(zc::heap<Impl>(zc::mv(name), zc::mv(typeArguments))) {}

TypeReference::~TypeReference() noexcept(false) = default;

zc::StringPtr TypeReference::getName() const { return impl->name->getName(); }

void TypeReference::accept(Visitor& visitor) const { visitor.visit(*this); }

// ArrayType
struct ArrayType::Impl {
  zc::Own<Type> elementType;

  explicit Impl(zc::Own<Type> elementType) : elementType(zc::mv(elementType)) {}
};

ArrayType::ArrayType(zc::Own<Type> elementType) noexcept
    : Type(SyntaxKind::kArrayType), impl(zc::heap<Impl>(zc::mv(elementType))) {}

ArrayType::~ArrayType() noexcept(false) = default;

const Type& ArrayType::getElementType() const { return *impl->elementType; }

void ArrayType::accept(Visitor& visitor) const { visitor.visit(*this); }

// UnionType
struct UnionType::Impl {
  NodeList<Type> types;

  explicit Impl(zc::Vector<zc::Own<Type>>&& types) : types(zc::mv(types)) {}
};

UnionType::UnionType(zc::Vector<zc::Own<Type>>&& types) noexcept
    : Type(SyntaxKind::kUnionType), impl(zc::heap<Impl>(zc::mv(types))) {}

UnionType::~UnionType() noexcept(false) = default;

const NodeList<Type>& UnionType::getTypes() const { return impl->types; }

void UnionType::accept(Visitor& visitor) const { visitor.visit(*this); }

// IntersectionType
struct IntersectionType::Impl {
  NodeList<Type> types;

  explicit Impl(zc::Vector<zc::Own<Type>>&& types) : types(zc::mv(types)) {}
};

IntersectionType::IntersectionType(zc::Vector<zc::Own<Type>>&& types) noexcept
    : Type(SyntaxKind::kIntersectionType), impl(zc::heap<Impl>(zc::mv(types))) {}

IntersectionType::~IntersectionType() noexcept(false) = default;

const NodeList<Type>& IntersectionType::getTypes() const { return impl->types; }

void IntersectionType::accept(Visitor& visitor) const { visitor.visit(*this); }

// ParenthesizedType
struct ParenthesizedType::Impl {
  zc::Own<Type> type;

  explicit Impl(zc::Own<Type> type) : type(zc::mv(type)) {}
};

ParenthesizedType::ParenthesizedType(zc::Own<Type> type) noexcept
    : Type(SyntaxKind::kParenthesizedType), impl(zc::heap<Impl>(zc::mv(type))) {}

ParenthesizedType::~ParenthesizedType() noexcept(false) = default;

const Type& ParenthesizedType::getType() const { return *impl->type; }

void ParenthesizedType::accept(Visitor& visitor) const { visitor.visit(*this); }

// PredefinedType
struct PredefinedType::Impl {
  zc::String name;

  explicit Impl(zc::String name) : name(zc::mv(name)) {}
};

PredefinedType::PredefinedType(zc::String name) noexcept
    : Type(SyntaxKind::kPredefinedType), impl(zc::heap<Impl>(zc::mv(name))) {
  ZC_REQUIRE(isValidPredefinedType(impl->name), "Invalid predefined type name", impl->name);
}

PredefinedType::~PredefinedType() noexcept(false) = default;

zc::StringPtr PredefinedType::getName() const { return impl->name; }

void PredefinedType::accept(Visitor& visitor) const { visitor.visit(*this); }

// ObjectType
struct ObjectType::Impl {
  NodeList<Node> members;

  explicit Impl(zc::Vector<zc::Own<Node>>&& members) : members(zc::mv(members)) {}
};

ObjectType::ObjectType(zc::Vector<zc::Own<Node>>&& members) noexcept
    : Type(SyntaxKind::kObjectType), impl(zc::heap<Impl>(zc::mv(members))) {}

ObjectType::~ObjectType() noexcept(false) = default;

const NodeList<Node>& ObjectType::getMembers() const { return impl->members; }

void ObjectType::accept(Visitor& visitor) const { visitor.visit(*this); }

// TupleType
struct TupleType::Impl {
  NodeList<Type> elementTypes;

  explicit Impl(zc::Vector<zc::Own<Type>>&& elementTypes) : elementTypes(zc::mv(elementTypes)) {}
};

TupleType::TupleType(zc::Vector<zc::Own<Type>>&& elementTypes) noexcept
    : Type(SyntaxKind::kTupleType), impl(zc::heap<Impl>(zc::mv(elementTypes))) {}

TupleType::~TupleType() noexcept(false) = default;

const NodeList<Type>& TupleType::getElementTypes() const { return impl->elementTypes; }

void TupleType::accept(Visitor& visitor) const { visitor.visit(*this); }

// ReturnType
struct ReturnType::Impl {
  zc::Own<Type> type;
  zc::Maybe<zc::Own<Type>> errorType;

  Impl(zc::Own<Type> type, zc::Maybe<zc::Own<Type>> errorType)
      : type(zc::mv(type)), errorType(zc::mv(errorType)) {}
};

ReturnType::ReturnType(zc::Own<Type> type, zc::Maybe<zc::Own<Type>> errorType) noexcept
    : Type(SyntaxKind::kReturnType), impl(zc::heap<Impl>(zc::mv(type), zc::mv(errorType))) {}

ReturnType::~ReturnType() noexcept(false) = default;

const Type& ReturnType::getType() const { return *impl->type; }

zc::Maybe<const Type&> ReturnType::getErrorType() const {
  return impl->errorType.map([](const zc::Own<Type>& type) -> const Type& { return *type; });
}

void ReturnType::accept(Visitor& visitor) const { visitor.visit(*this); }

// FunctionType
struct FunctionType::Impl {
  const NodeList<TypeParameter> typeParameters;
  const NodeList<BindingElement> parameters;
  const zc::Own<ReturnType> returnType;

  Impl(zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
       zc::Vector<zc::Own<BindingElement>>&& parameters, zc::Own<ReturnType> returnType)
      : typeParameters(zc::mv(typeParameters)),
        parameters(zc::mv(parameters)),
        returnType(zc::mv(returnType)) {}
};

FunctionType::FunctionType(zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
                           zc::Vector<zc::Own<BindingElement>>&& parameters,
                           zc::Own<ReturnType> returnType) noexcept
    : Type(SyntaxKind::kFunctionType),
      impl(zc::heap<Impl>(zc::mv(typeParameters), zc::mv(parameters), zc::mv(returnType))) {}

FunctionType::~FunctionType() noexcept(false) = default;

const NodeList<TypeParameter>& FunctionType::getTypeParameters() const {
  return impl->typeParameters;
}

const NodeList<BindingElement>& FunctionType::getParameters() const { return impl->parameters; }

const ReturnType& FunctionType::getReturnType() const { return *impl->returnType; }

void FunctionType::accept(Visitor& visitor) const { visitor.visit(*this); }

// OptionalType
struct OptionalType::Impl {
  zc::Own<Type> type;

  explicit Impl(zc::Own<Type> type) : type(zc::mv(type)) {}
};

OptionalType::OptionalType(zc::Own<Type> type) noexcept
    : Type(SyntaxKind::kOptionalType), impl(zc::heap<Impl>(zc::mv(type))) {}

OptionalType::~OptionalType() noexcept(false) = default;

const Type& OptionalType::getType() const { return *impl->type; }

void OptionalType::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// TypeQuery

struct TypeQuery::Impl {
  const zc::Own<Expression> expr;

  explicit Impl(zc::Own<Expression> expr) : expr(zc::mv(expr)) {}
};

TypeQuery::TypeQuery(zc::Own<Expression> expr) noexcept
    : Type(SyntaxKind::kTypeQuery), impl(zc::heap<Impl>(zc::mv(expr))) {}
TypeQuery::~TypeQuery() noexcept(false) = default;

const Expression& TypeQuery::getExpression() const { return *impl->expr; }

void TypeQuery::accept(Visitor& visitor) const { visitor.visit(*this); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
