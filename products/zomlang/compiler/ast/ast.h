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
#include "zc/core/memory.h"
#include "zc/core/vector.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceRange;
}  // namespace source

namespace ast {

// Base class for all AST nodes
class Node {
public:
  Node() noexcept;
  virtual ~Node() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Node);

  void setSourceRange(const source::SourceRange& range);
  [[nodiscard]] const source::SourceRange sourceRange() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

// NodeList template class, used to store a list of nodes
namespace _ {
// Default case
template <typename T, typename = void>
struct IsNodeLike : std::false_type {};

// Directly derived from Node
template <typename T>
struct IsNodeLike<T, std::enable_if_t<std::is_base_of_v<Node, T>>> : std::true_type {};

// zc::Own<T>
template <typename T>
struct IsNodeLike<zc::Own<T>, std::enable_if_t<std::is_base_of_v<Node, T>>> : std::true_type {};

// zc::Maybe<U> (Recursive)
template <typename U>
struct IsNodeLike<zc::Maybe<U>, std::enable_if_t<IsNodeLike<U>::value>> : std::true_type {};
}  // namespace _

template <typename T>
concept FlexibleNodeType = _::IsNodeLike<T>::value;

template <typename T>
concept DerivedFromNode = std::is_base_of_v<Node, T>;

template <DerivedFromNode T>
class NodeList {
public:
  NodeList() noexcept {};
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
  ConstIterator begin() const { return ConstIterator(nodes, 0); }
  ConstIterator end() const { return ConstIterator(nodes, nodes.size()); }
  ConstIterator cbegin() const { return ConstIterator(nodes, 0); }
  ConstIterator cend() const { return ConstIterator(nodes, nodes.size()); }

private:
  zc::Vector<zc::Own<T>> nodes;
};

// NodeList implementations of Iterator and ConstIterator
template <DerivedFromNode T>
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

template <DerivedFromNode T>
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
