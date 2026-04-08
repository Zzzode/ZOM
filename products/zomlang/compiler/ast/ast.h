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
#include <type_traits>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/visitor.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceRange;
}  // namespace source

namespace symbol {
class Symbol;
class SymbolTable;
}  // namespace symbol

namespace ast {

// Forward declarations
class Visitor;

enum class NodeFlags : uint32_t {
  None = 0,
  OptionalChain = 1u << 0,
};

constexpr NodeFlags operator|(NodeFlags lhs, NodeFlags rhs) {
  return static_cast<NodeFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr NodeFlags operator&(NodeFlags lhs, NodeFlags rhs) {
  return static_cast<NodeFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr NodeFlags operator^(NodeFlags lhs, NodeFlags rhs) {
  return static_cast<NodeFlags>(static_cast<uint32_t>(lhs) ^ static_cast<uint32_t>(rhs));
}

constexpr NodeFlags operator~(NodeFlags flags) {
  return static_cast<NodeFlags>(~static_cast<uint32_t>(flags));
}

inline NodeFlags& operator|=(NodeFlags& lhs, NodeFlags rhs) {
  lhs = lhs | rhs;
  return lhs;
}

inline NodeFlags& operator&=(NodeFlags& lhs, NodeFlags rhs) {
  lhs = lhs & rhs;
  return lhs;
}

inline NodeFlags& operator^=(NodeFlags& lhs, NodeFlags rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

constexpr bool hasFlag(NodeFlags flags, NodeFlags flag) {
  return (flags & flag) != NodeFlags::None;
}

class Visitable {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Visitable);

  virtual void accept(Visitor& visitor) const = 0;

protected:
  Visitable() = default;
};

// Base class for all AST nodes
class Node : public Visitable {
public:
  ZC_DISALLOW_COPY_AND_MOVE(Node);

  /// \brief Set the source range of this node
  virtual void setSourceRange(const source::SourceRange&& range) = 0;

  /// \brief Get the source range of this node
  virtual const source::SourceRange& getSourceRange() const = 0;

  /// \brief Get the syntax kind of this node
  virtual SyntaxKind getKind() const = 0;

  /// \brief Get the node flags
  virtual NodeFlags getFlags() const = 0;

  /// \brief Set the node flags
  virtual void setFlags(NodeFlags flags) = 0;

protected:
  Node() noexcept = default;
};

class NodeImpl {
public:
  explicit NodeImpl(SyntaxKind kind) noexcept;
  ~NodeImpl() noexcept(false);

  void setSourceRange(const source::SourceRange&& range);

  const source::SourceRange& getSourceRange() const;

  SyntaxKind getKind() const;

  NodeFlags getFlags() const;
  void setFlags(NodeFlags flags) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

#define NODE_METHOD_DECLARE()                                      \
  void setSourceRange(const source::SourceRange&& range) override; \
  const source::SourceRange& getSourceRange() const override;      \
  SyntaxKind getKind() const override;                             \
  NodeFlags getFlags() const override;                             \
  void setFlags(NodeFlags flags) override;                         \
  void accept(Visitor& visitor) const override;

class TokenNode final : public Node {
public:
  TokenNode(SyntaxKind kind) noexcept;
  ~TokenNode() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TokenNode);

  NODE_METHOD_DECLARE();

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Interface for AST nodes that can contain local symbols
///
/// This interface is implemented by nodes that create their own scope
/// and can contain local symbol declarations (variables, functions, etc.).
/// It provides access to the symbol table and maintains a chain of
/// containers for declaration order tracking.
class LocalsContainer {
public:
  ZC_DISALLOW_COPY_AND_MOVE(LocalsContainer);

  /// \brief Get the symbol table containing local symbols
  /// \return Reference to the symbol table, may be empty if not yet bound
  virtual zc::Maybe<const symbol::SymbolTable&> getLocals() const = 0;

  /// \brief Set the symbol table for this container
  /// \param locals The symbol table to associate with this container
  virtual void setLocals(zc::Maybe<const symbol::SymbolTable&> locals) = 0;

  /// \brief Get the next container in declaration order
  /// \return Reference to the next container, or none if this is the last
  virtual zc::Maybe<const LocalsContainer&> getNextContainer() const = 0;

  /// \brief Set the next container in declaration order
  /// \param nextContainer The next container to link to
  virtual void setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) = 0;

protected:
  LocalsContainer() noexcept = default;
};

/// \brief Implementation class for LocalsContainer interface
///
/// Provides concrete implementation of the LocalsContainer interface
/// using the Pimpl pattern for encapsulation.
class LocalsContainerImpl {
public:
  LocalsContainerImpl() noexcept;
  ~LocalsContainerImpl() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(LocalsContainerImpl);

  /// \brief Get the symbol table containing local symbols
  zc::Maybe<const symbol::SymbolTable&> getLocals() const;

  /// \brief Set the symbol table for this container
  void setLocals(zc::Maybe<const symbol::SymbolTable&> locals);

  /// \brief Get the next container in declaration order
  zc::Maybe<const LocalsContainer&> getNextContainer() const;

  /// \brief Set the next container in declaration order
  void setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Macro to declare LocalsContainer methods in derived classes
#define LOCALS_CONTAINER_METHOD_DECL()                                   \
  zc::Maybe<const symbol::SymbolTable&> getLocals() const override;      \
  void setLocals(zc::Maybe<const symbol::SymbolTable&> locals) override; \
  zc::Maybe<const LocalsContainer&> getNextContainer() const override;   \
  void setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) override;

// Concept to define valid AST node types for NodeList
template <typename T>
concept NodeLike = std::is_base_of_v<Node, T> || std::is_base_of_v<Declaration, T>;

// NodeList template class, used to store a list of nodes
template <NodeLike T>
class NodeList {
public:
  NodeList() noexcept = default;
  explicit NodeList(zc::Vector<zc::Own<T>>&& nodes) noexcept : nodes(zc::mv(nodes)) {}

  ~NodeList() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(NodeList);

  // Basic operations
  void add(zc::Own<T> node) {
    ZC_REQUIRE(node != nullptr, "Cannot add null node");
    nodes.add(zc::mv(node));
  }

  void insert(size_t index, zc::Own<T> node) {
    ZC_REQUIRE(node != nullptr, "Cannot insert null node");
    ZC_REQUIRE(index <= size(), "Index out of bounds");

    // Vector doesn't have insert, so we need to implement it
    nodes.resize(nodes.size() + 1);
    for (size_t i = nodes.size() - 1; i > index; --i) { nodes[i] = zc::mv(nodes[i - 1]); }
    nodes[index] = zc::mv(node);
  }

  zc::Own<T> remove(size_t index) {
    ZC_REQUIRE(index < size(), "Index out of bounds");

    zc::Own<T> result = zc::mv(nodes[index]);
    for (size_t i = index; i < nodes.size() - 1; ++i) { nodes[i] = zc::mv(nodes[i + 1]); }
    nodes.removeLast();
    return result;
  }
  void clear() { nodes.clear(); }

  // Access operations
  T& operator[](size_t index) { return *nodes[index]; }
  const T& operator[](size_t index) const { return *nodes[index]; }
  T& at(size_t index) {
    ZC_REQUIRE(index < size(), "Index out of bounds");
    return *nodes[index];
  }
  const T& at(size_t index) const {
    ZC_REQUIRE(index < size(), "Index out of bounds");
    return *nodes[index];
  }

  // Size operations
  size_t size() const { return nodes.size(); }
  bool empty() const { return nodes.empty(); }

public:
  // Iterators
  class Iterator;
  class ConstIterator;

  Iterator begin() { return Iterator(nodes, 0); }
  Iterator end() { return Iterator(nodes, nodes.size()); }
  Iterator back() { return Iterator(nodes, nodes.size() - 1); }
  ConstIterator begin() const { return ConstIterator(nodes, 0); }
  ConstIterator end() const { return ConstIterator(nodes, nodes.size()); }
  ConstIterator back() const { return ConstIterator(nodes, nodes.size() - 1); }
  ConstIterator cbegin() const { return ConstIterator(nodes, 0); }
  ConstIterator cend() const { return ConstIterator(nodes, nodes.size()); }

private:
  zc::Vector<zc::Own<T>> nodes;
};

// NodeList implementations of Iterator and ConstIterator
template <NodeLike T>
class NodeList<T>::Iterator {
public:
  Iterator(zc::Vector<zc::Own<T>>& vec, size_t index) : vec(&vec), index(index) {}

  T& operator*() { return *(*vec)[index]; }
  T* operator->() { return (*vec)[index].get(); }
  Iterator& operator++() {
    ++index;
    return *this;
  }
  Iterator operator++(int) {
    Iterator tmp = *this;
    ++index;
    return tmp;
  }
  bool operator==(const Iterator& other) const { return vec == other.vec && index == other.index; }
  bool operator!=(const Iterator& other) const { return vec != other.vec || index != other.index; }

private:
  zc::Vector<zc::Own<T>>* vec;
  size_t index;
};

template <NodeLike T>
class NodeList<T>::ConstIterator {
public:
  ConstIterator(const zc::Vector<zc::Own<T>>& vec, size_t index) : vec(&vec), index(index) {}

  const T& operator*() const { return *(*vec)[index]; }
  const T* operator->() const { return (*vec)[index].get(); }
  /// Prefix increment operator
  ConstIterator& operator++() {
    ++index;
    return *this;
  }
  /// Postfix increment operator
  ConstIterator operator++(int) {
    ConstIterator tmp = *this;
    ++index;
    return tmp;
  }
  bool operator==(const ConstIterator& other) const {
    return vec == other.vec && index == other.index;
  }
  bool operator!=(const ConstIterator& other) const {
    return vec != other.vec || index != other.index;
  }

private:
  const zc::Vector<zc::Own<T>>* vec;
  size_t index;
};

// Alias for NodeList<Node>
using NodeListPtr = NodeList<Node>;

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
