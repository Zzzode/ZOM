#include "zomlang/compiler/ast/type.h"

#include "zc/core/debug.h"
#include "zomlang/compiler/ast/expression.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Type base class
Type::Type(SyntaxKind kind) : Node(kind) {}
Type::~Type() noexcept(false) = default;

// TypeReference
struct TypeReference::Impl {
  zc::Own<Identifier> name;

  explicit Impl(zc::Own<Identifier> name) : name(zc::mv(name)) {}
};

TypeReference::TypeReference(zc::Own<Identifier> name)
    : Type(SyntaxKind::kTypeReference), impl(zc::heap<Impl>(zc::mv(name))) {}

TypeReference::~TypeReference() noexcept(false) = default;

zc::StringPtr TypeReference::getName() const { return impl->name->getName(); }

// ArrayType
struct ArrayType::Impl {
  zc::Own<Type> elementType;

  explicit Impl(zc::Own<Type> elementType) : elementType(zc::mv(elementType)) {}
};

ArrayType::ArrayType(zc::Own<Type> elementType)
    : Type(SyntaxKind::kArrayType), impl(zc::heap<Impl>(zc::mv(elementType))) {}

ArrayType::~ArrayType() noexcept(false) = default;

const Type* ArrayType::getElementType() const { return impl->elementType.get(); }

// UnionType
struct UnionType::Impl {
  NodeList<Type> types;

  explicit Impl(zc::Vector<zc::Own<Type>>&& types) : types(zc::mv(types)) {}
};

UnionType::UnionType(zc::Vector<zc::Own<Type>>&& types)
    : Type(SyntaxKind::kUnionType), impl(zc::heap<Impl>(zc::mv(types))) {}

UnionType::~UnionType() noexcept(false) = default;

const NodeList<Type>& UnionType::getTypes() const { return impl->types; }

// IntersectionType
struct IntersectionType::Impl {
  NodeList<Type> types;

  explicit Impl(zc::Vector<zc::Own<Type>>&& types) : types(zc::mv(types)) {}
};

IntersectionType::IntersectionType(zc::Vector<zc::Own<Type>>&& types)
    : Type(SyntaxKind::kIntersectionType), impl(zc::heap<Impl>(zc::mv(types))) {}

IntersectionType::~IntersectionType() noexcept(false) = default;

const NodeList<Type>& IntersectionType::getTypes() const { return impl->types; }

// ParenthesizedType
struct ParenthesizedType::Impl {
  zc::Own<Type> type;

  explicit Impl(zc::Own<Type> type) : type(zc::mv(type)) {}
};

ParenthesizedType::ParenthesizedType(zc::Own<Type> type)
    : Type(SyntaxKind::kParenthesizedType), impl(zc::heap<Impl>(zc::mv(type))) {}

ParenthesizedType::~ParenthesizedType() noexcept(false) = default;

const Type* ParenthesizedType::getType() const { return impl->type.get(); }

// PredefinedType
struct PredefinedType::Impl {
  zc::String name;

  explicit Impl(zc::String name) : name(zc::mv(name)) {}
};

PredefinedType::PredefinedType(zc::String name)
    : Type(SyntaxKind::kPredefinedType), impl(zc::heap<Impl>(zc::mv(name))) {}

PredefinedType::~PredefinedType() noexcept(false) = default;

zc::StringPtr PredefinedType::getName() const { return impl->name; }

// ObjectType
struct ObjectType::Impl {
  NodeList<Node> members;

  explicit Impl(zc::Vector<zc::Own<Node>>&& members) : members(zc::mv(members)) {}
};

ObjectType::ObjectType(zc::Vector<zc::Own<Node>>&& members)
    : Type(SyntaxKind::kObjectType), impl(zc::heap<Impl>(zc::mv(members))) {}

ObjectType::~ObjectType() noexcept(false) = default;

const NodeList<Node>& ObjectType::getMembers() const { return impl->members; }

// TupleType
struct TupleType::Impl {
  NodeList<Type> elementTypes;

  explicit Impl(zc::Vector<zc::Own<Type>>&& elementTypes) : elementTypes(zc::mv(elementTypes)) {}
};

TupleType::TupleType(zc::Vector<zc::Own<Type>>&& elementTypes)
    : Type(SyntaxKind::kTupleType), impl(zc::heap<Impl>(zc::mv(elementTypes))) {}

TupleType::~TupleType() noexcept(false) = default;

const NodeList<Type>& TupleType::getElementTypes() const { return impl->elementTypes; }

// FunctionType
struct FunctionType::Impl {
  NodeList<Type> parameterTypes;
  zc::Own<Type> returnType;

  Impl(zc::Vector<zc::Own<Type>>&& parameterTypes, zc::Own<Type> returnType)
      : parameterTypes(zc::mv(parameterTypes)), returnType(zc::mv(returnType)) {}
};

FunctionType::FunctionType(zc::Vector<zc::Own<Type>>&& parameterTypes, zc::Own<Type> returnType)
    : Type(SyntaxKind::kFunctionType),
      impl(zc::heap<Impl>(zc::mv(parameterTypes), zc::mv(returnType))) {}

FunctionType::~FunctionType() noexcept(false) = default;

const NodeList<Type>& FunctionType::getParameterTypes() const { return impl->parameterTypes; }

const Type* FunctionType::getReturnType() const { return impl->returnType.get(); }

// TypeAnnotation
struct TypeAnnotation::Impl {
  zc::Own<Type> type;

  explicit Impl(zc::Own<Type> type) : type(zc::mv(type)) {}
};

TypeAnnotation::TypeAnnotation(zc::Own<Type> type)
    : Node(SyntaxKind::kTypeAnnotation), impl(zc::heap<Impl>(zc::mv(type))) {}

TypeAnnotation::~TypeAnnotation() noexcept(false) = default;

const Type* TypeAnnotation::getType() const { return impl->type.get(); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
