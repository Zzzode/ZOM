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
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations
class Expression;
class Identifier;

// Base class for all type nodes
class Type : public Node {
public:
  explicit Type(SyntaxKind kind) noexcept;
  ~Type() noexcept(false);

  void accept(Visitor& visitor) const override;

  ZC_DISALLOW_COPY_AND_MOVE(Type);
};

// Primary type (identifier-based type)
class TypeReference : public Type {
public:
  explicit TypeReference(zc::Own<Identifier> name,
                         zc::Maybe<zc::Vector<zc::Own<Type>>> typeParameters) noexcept;
  ~TypeReference() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeReference);

  void accept(Visitor& visitor) const override;
  zc::StringPtr getName() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Array type: T[]
class ArrayType : public Type {
public:
  explicit ArrayType(zc::Own<Type> elementType) noexcept;
  ~ArrayType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayType);

  void accept(Visitor& visitor) const override;
  const Type& getElementType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Union type: T | U
class UnionType : public Type {
public:
  explicit UnionType(zc::Vector<zc::Own<Type>>&& types) noexcept;
  ~UnionType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UnionType);

  void accept(Visitor& visitor) const override;
  const NodeList<Type>& getTypes() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Intersection type: T & U
class IntersectionType : public Type {
public:
  explicit IntersectionType(zc::Vector<zc::Own<Type>>&& types) noexcept;
  ~IntersectionType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IntersectionType);

  void accept(Visitor& visitor) const override;
  const NodeList<Type>& getTypes() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Parenthesized type: (T)
class ParenthesizedType : public Type {
public:
  explicit ParenthesizedType(zc::Own<Type> type) noexcept;
  ~ParenthesizedType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ParenthesizedType);

  void accept(Visitor& visitor) const override;
  const Type& getType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Predefined type (built-in primitive types)
class PredefinedType : public Type {
public:
  explicit PredefinedType(zc::String name) noexcept;
  ~PredefinedType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(PredefinedType);

  void accept(Visitor& visitor) const override;
  zc::StringPtr getName() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Object type: { prop: Type }
class ObjectType : public Type {
public:
  explicit ObjectType(zc::Vector<zc::Own<Node>>&& members) noexcept;
  ~ObjectType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ObjectType);

  void accept(Visitor& visitor) const override;
  const NodeList<Node>& getMembers() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Tuple type: [T, U]
class TupleType : public Type {
public:
  explicit TupleType(zc::Vector<zc::Own<Type>>&& elementTypes) noexcept;
  ~TupleType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TupleType);

  void accept(Visitor& visitor) const override;
  const NodeList<Type>& getElementTypes() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Return type with optional error type: -> T raises E
class ReturnType : public Type {
public:
  ReturnType(zc::Own<Type> type, zc::Maybe<zc::Own<Type>> errorType = zc::none) noexcept;
  ~ReturnType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ReturnType);

  void accept(Visitor& visitor) const override;
  const Type& getType() const;
  zc::Maybe<const Type&> getErrorType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Function type: (T, U) -> V
class FunctionType : public Type {
public:
  FunctionType(zc::Vector<zc::Own<TypeParameter>>&& typeParameters,
               zc::Vector<zc::Own<BindingElement>>&& parameters,
               zc::Own<ReturnType> returnType) noexcept;
  ~FunctionType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FunctionType);

  void accept(Visitor& visitor) const override;
  const NodeList<TypeParameter>& getTypeParameters() const;
  const NodeList<BindingElement>& getParameters() const;
  const ReturnType& getReturnType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

class OptionalType : public Type {
public:
  explicit OptionalType(zc::Own<Type> type) noexcept;
  ~OptionalType() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(OptionalType);

  void accept(Visitor& visitor) const override;
  const Type& getType() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Type query: typeof expr
class TypeQuery : public Type {
public:
  explicit TypeQuery(zc::Own<Expression> expr) noexcept;
  ~TypeQuery() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeQuery);

  void accept(Visitor& visitor) const override;
  const Expression& getExpression() const;

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
