#pragma once

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations
class Expression;
class Identifier;

// Base class for all type nodes
class Type : public Node {
public:
  explicit Type(SyntaxKind kind);
  ~Type() noexcept(false) override;

  ZC_DISALLOW_COPY_AND_MOVE(Type);
};

// Primary type (identifier-based type)
class TypeReference : public Type {
public:
  explicit TypeReference(zc::Own<Identifier> name);
  ~TypeReference() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeReference);

  zc::StringPtr getName() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Array type: T[]
class ArrayType : public Type {
public:
  explicit ArrayType(zc::Own<Type> elementType);
  ~ArrayType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayType);

  const Type* getElementType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Union type: T | U
class UnionType : public Type {
public:
  explicit UnionType(zc::Vector<zc::Own<Type>>&& types);
  ~UnionType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UnionType);

  const NodeList<Type>& getTypes() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Intersection type: T & U
class IntersectionType : public Type {
public:
  explicit IntersectionType(zc::Vector<zc::Own<Type>>&& types);
  ~IntersectionType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IntersectionType);

  const NodeList<Type>& getTypes() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Parenthesized type: (T)
class ParenthesizedType : public Type {
public:
  explicit ParenthesizedType(zc::Own<Type> type);
  ~ParenthesizedType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ParenthesizedType);

  const Type* getType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Predefined types (built-in types)
class PredefinedType : public Type {
public:
  explicit PredefinedType(zc::String name);
  ~PredefinedType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PredefinedType);

  zc::StringPtr getName() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Object type: { prop: Type }
class ObjectType : public Type {
public:
  explicit ObjectType(zc::Vector<zc::Own<Node>>&& members);
  ~ObjectType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ObjectType);

  const NodeList<Node>& getMembers() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Tuple type: [T, U]
class TupleType : public Type {
public:
  explicit TupleType(zc::Vector<zc::Own<Type>>&& elementTypes);
  ~TupleType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TupleType);

  const NodeList<Type>& getElementTypes() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Function type: (T, U) -> V
class FunctionType : public Type {
public:
  FunctionType(zc::Vector<zc::Own<Type>>&& parameterTypes, zc::Own<Type> returnType);
  ~FunctionType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FunctionType);

  const NodeList<Type>& getParameterTypes() const;
  const Type* getReturnType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Optional type: T?
class OptionalType : public Type {
public:
  explicit OptionalType(zc::Own<Type> type);
  ~OptionalType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(OptionalType);

  const Type* getType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
