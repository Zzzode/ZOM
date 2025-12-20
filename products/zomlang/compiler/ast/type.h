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
#include "zomlang/compiler/ast/classof.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {
namespace ast {

// Forward declarations
class Expression;
class Identifier;

// Base class for all type nodes
class TypeNode : public Node {
public:
  ZC_DISALLOW_COPY_AND_MOVE(TypeNode);

  /// \brief Virtual destructor for proper RTTI support
  virtual ~TypeNode() noexcept(false) = default;

  /// \brief Check if a node is a TypeNode
  GENERATE_CLASSOF_IMPL(TypeNode)

protected:
  using Node::Node;
};

// ================================================================================
// PredefinedTypeNode

// Base class for predefined types
class PredefinedTypeNode : public TypeNode {
public:
  ZC_DISALLOW_COPY_AND_MOVE(PredefinedTypeNode);

  virtual zc::StringPtr getName() const = 0;

  /// \brief Check if a node is a PredefinedTypeNode
  GENERATE_CLASSOF_IMPL(PredefinedTypeNode)

protected:
  PredefinedTypeNode() noexcept = default;
};

// ================================================================================
// TypeReferenceNode

class TypeReferenceNode final : public TypeNode {
public:
  TypeReferenceNode(zc::Own<Identifier> name,
                    zc::Maybe<zc::Vector<zc::Own<TypeNode>>> typeArguments) noexcept;
  ~TypeReferenceNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeReferenceNode);

  const Identifier& getName() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Array type: T[]
class ArrayTypeNode final : public TypeNode {
public:
  explicit ArrayTypeNode(zc::Own<TypeNode> elementTypeNode) noexcept;
  ~ArrayTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ArrayTypeNode);

  const TypeNode& getElementType() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Union type: T | U
class UnionTypeNode final : public TypeNode {
public:
  explicit UnionTypeNode(zc::Vector<zc::Own<TypeNode>>&& types) noexcept;
  ~UnionTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UnionTypeNode);

  const NodeList<TypeNode>& getTypes() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Intersection type: T & U
class IntersectionTypeNode final : public TypeNode {
public:
  explicit IntersectionTypeNode(zc::Vector<zc::Own<TypeNode>>&& types) noexcept;
  ~IntersectionTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(IntersectionTypeNode);

  const NodeList<TypeNode>& getTypes() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Parenthesized type: (T)
class ParenthesizedTypeNode final : public TypeNode {
public:
  explicit ParenthesizedTypeNode(zc::Own<TypeNode> type) noexcept;
  ~ParenthesizedTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ParenthesizedTypeNode);

  const TypeNode& getType() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class BoolTypeNode final : public PredefinedTypeNode {
public:
  BoolTypeNode() noexcept;
  ~BoolTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(BoolTypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class StrTypeNode final : public PredefinedTypeNode {
public:
  StrTypeNode() noexcept;
  ~StrTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(StrTypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class UnitTypeNode final : public PredefinedTypeNode {
public:
  UnitTypeNode() noexcept;
  ~UnitTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(UnitTypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class NullTypeNode final : public PredefinedTypeNode {
public:
  NullTypeNode() noexcept;
  ~NullTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(NullTypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class I8TypeNode final : public PredefinedTypeNode {
public:
  I8TypeNode() noexcept;
  ~I8TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(I8TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class I16TypeNode final : public PredefinedTypeNode {
public:
  I16TypeNode() noexcept;
  ~I16TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(I16TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class I32TypeNode final : public PredefinedTypeNode {
public:
  I32TypeNode() noexcept;
  ~I32TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(I32TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class I64TypeNode final : public PredefinedTypeNode {
public:
  I64TypeNode() noexcept;
  ~I64TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(I64TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class U8TypeNode final : public PredefinedTypeNode {
public:
  U8TypeNode() noexcept;
  ~U8TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(U8TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class U16TypeNode final : public PredefinedTypeNode {
public:
  U16TypeNode() noexcept;
  ~U16TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(U16TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class U32TypeNode final : public PredefinedTypeNode {
public:
  U32TypeNode() noexcept;
  ~U32TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(U32TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class U64TypeNode final : public PredefinedTypeNode {
public:
  U64TypeNode() noexcept;
  ~U64TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(U64TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class F32TypeNode final : public PredefinedTypeNode {
public:
  F32TypeNode() noexcept;
  ~F32TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(F32TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

class F64TypeNode final : public PredefinedTypeNode {
public:
  F64TypeNode() noexcept;
  ~F64TypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(F64TypeNode);

  zc::StringPtr getName() const override;

  NODE_METHOD_DECLARE();
};

// Object type: { ... }
class ObjectTypeNode final : public TypeNode {
public:
  explicit ObjectTypeNode(zc::Vector<zc::Own<Node>>&& members) noexcept;
  ~ObjectTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ObjectTypeNode);

  const NodeList<Node>& getMembers() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Tuple type: [T, U, ...]
class TupleTypeNode final : public TypeNode {
public:
  explicit TupleTypeNode(zc::Vector<zc::Own<TypeNode>>&& elementTypes) noexcept;
  ~TupleTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TupleTypeNode);

  const NodeList<TypeNode>& getElementTypes() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Return type: -> T raises E
class ReturnTypeNode final : public TypeNode {
public:
  explicit ReturnTypeNode(zc::Own<TypeNode> type,
                          zc::Maybe<zc::Own<TypeNode>> errorType = zc::none) noexcept;
  ~ReturnTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ReturnTypeNode);

  const TypeNode& getType() const;
  zc::Maybe<const TypeNode&> getErrorType() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Function type: (T, U) -> R
class FunctionTypeNode final : public TypeNode {
public:
  FunctionTypeNode(zc::Vector<zc::Own<TypeParameterDeclaration>>&& typeParameters,
                   zc::Vector<zc::Own<BindingElement>>&& parameters,
                   zc::Own<ReturnTypeNode> returnType) noexcept;
  ~FunctionTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(FunctionTypeNode);

  const NodeList<TypeParameterDeclaration>& getTypeParameters() const;
  const NodeList<BindingElement>& getParameters() const;
  const ReturnTypeNode& getReturnType() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Optional type: T?
class OptionalTypeNode final : public TypeNode {
public:
  explicit OptionalTypeNode(zc::Own<TypeNode> type) noexcept;
  ~OptionalTypeNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(OptionalTypeNode);

  const TypeNode& getType() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

// Type query: typeof T
class TypeQueryNode final : public TypeNode {
public:
  explicit TypeQueryNode(zc::Own<Expression> expression) noexcept;
  ~TypeQueryNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TypeQueryNode);

  const Expression& getExpression() const;

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  const zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
