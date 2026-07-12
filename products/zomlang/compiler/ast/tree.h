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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-layout.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/identity/frozen-registry.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace ast {

/// \brief Stable string table id stored in syntax node payloads.
struct StringId final {
  uint32_t value = 0;

  constexpr StringId() noexcept = default;
  constexpr explicit StringId(uint32_t value) noexcept : value(value) {}
  constexpr explicit operator bool() const noexcept { return value != 0; }

  constexpr bool operator==(StringId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(StringId other) const noexcept { return !operator==(other); }
};

/// \brief Stable identifier table id stored in syntax node payloads.
struct IdentId final {
  uint32_t value = 0;

  constexpr IdentId() noexcept = default;
  constexpr explicit IdentId(uint32_t value) noexcept : value(value) {}
  constexpr explicit operator bool() const noexcept { return value != 0; }

  constexpr bool operator==(IdentId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(IdentId other) const noexcept { return !operator==(other); }
};

/// \brief Stable arbitrary-precision integer literal table id.
struct BigIntId final {
  uint32_t value = 0;

  constexpr BigIntId() noexcept = default;
  constexpr explicit BigIntId(uint32_t value) noexcept : value(value) {}
  constexpr explicit operator bool() const noexcept { return value != 0; }

  constexpr bool operator==(BigIntId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(BigIntId other) const noexcept { return !operator==(other); }
};

/// \brief Stable floating-point literal table id.
struct FloatId final {
  uint32_t value = 0;

  constexpr FloatId() noexcept = default;
  constexpr explicit FloatId(uint32_t value) noexcept : value(value) {}
  constexpr explicit operator bool() const noexcept { return value != 0; }

  constexpr bool operator==(FloatId other) const noexcept { return value == other.value; }
  constexpr bool operator!=(FloatId other) const noexcept { return !operator==(other); }
};

/// \brief Contiguous list of child NodeId values in ast::Tree list storage.
struct NodeList final {
  uint32_t first = 0;
  uint32_t size = 0;

  constexpr bool empty() const noexcept { return size == 0; }
};

/// \brief Contiguous list of IdentId values in ast::Tree identifier-list storage.
struct IdentList final {
  uint32_t first = 0;
  uint32_t size = 0;

  constexpr bool empty() const noexcept { return size == 0; }
};

/// \brief Fixed payload words for schema-generated syntax nodes.
struct NodePayload final {
  uint32_t words[kNodePayloadWordCount] = {};
};

static_assert(sizeof(NodePayload) == kNodePayloadByteCount,
              "NodePayload storage must match the generated schema layout");

/// \brief Main syntax record used by ast::Tree.
struct Node final {
  SyntaxKind kind = SyntaxKind::Unknown;
  source::SourceRange range;
  NodePayload payload;
};

class TreeBuilder;

/// \brief Owning immutable syntax tree.
class Tree final {
public:
  Tree() noexcept;
  ~Tree() noexcept(false);

  Tree(Tree&& other) noexcept;
  Tree& operator=(Tree&& other) noexcept;
  ZC_DISALLOW_COPY(Tree);

  /// \brief Return the root source-file node.
  NodeId root() const;

  /// \brief Number of syntax nodes stored in this tree.
  size_t nodeCount() const;

  /// \brief Return true when id belongs to this tree.
  bool contains(NodeId id) const;

  /// \brief Return true when list belongs to this tree.
  bool contains(NodeList list) const;

  /// \brief Return true when identifier list belongs to this tree.
  bool contains(IdentList list) const;

  /// \brief Look up a syntax node by id.
  const Node& node(NodeId id) const;

  /// \brief Return all nodes in allocation order.
  zc::ArrayPtr<const Node> nodes() const;

  /// \brief Resolve a list handle to child node ids.
  zc::ArrayPtr<const NodeId> list(NodeList list) const;

  /// \brief Resolve an identifier-list handle to identifier ids.
  zc::ArrayPtr<const IdentId> identList(IdentList list) const;

  /// \brief Resolve a string table id.
  zc::StringPtr string(StringId id) const;

  /// \brief Resolve an identifier table id.
  zc::StringPtr ident(IdentId id) const;

  /// \brief Resolve an integer literal table id.
  zc::StringPtr bigInt(BigIntId id) const;

  /// \brief Resolve a floating-point literal table id.
  zc::StringPtr floatLiteral(FloatId id) const;

private:
  struct Impl;
  zc::Own<Impl> impl;

  friend class TreeBuilder;

  NodeId appendNode(Node node);
  NodeList appendList(zc::ArrayPtr<const NodeId> nodes);
  IdentList appendIdentList(zc::ArrayPtr<const IdentId> ids);
  StringId appendString(zc::StringPtr value);
  IdentId appendIdent(zc::StringPtr value);
  BigIntId appendBigInt(zc::StringPtr value);
  FloatId appendFloat(zc::StringPtr value);
  void setRoot(NodeId id);
};

/// \brief Mutable construction API used by parser code.
class TreeBuilder final {
public:
  TreeBuilder() noexcept;
  ~TreeBuilder() noexcept(false);

  TreeBuilder(TreeBuilder&& other) noexcept;
  TreeBuilder& operator=(TreeBuilder&& other) noexcept;
  ZC_DISALLOW_COPY(TreeBuilder);

  NodeId makeNode(SyntaxKind kind, source::SourceRange range, NodePayload payload = {});
  NodeList makeList(zc::ArrayPtr<const NodeId> nodes);
  IdentList makeIdentList(zc::ArrayPtr<const IdentId> ids);
  StringId internString(zc::StringPtr value);
  IdentId internIdent(zc::StringPtr value);
  BigIntId internBigInt(zc::StringPtr value);
  FloatId internFloat(zc::StringPtr value);
  void setRoot(NodeId id);
  Tree finish();

private:
  Tree tree;
};

/// \brief Binder/checker side metadata keyed by NodeId.
class BindingMetadata final {
public:
  BindingMetadata() noexcept;
  ~BindingMetadata() noexcept(false);

  BindingMetadata(BindingMetadata&& other) noexcept;
  BindingMetadata& operator=(BindingMetadata&& other) noexcept;
  ZC_DISALLOW_COPY(BindingMetadata);

  void resizeFor(const Tree& tree);

  /// \brief Return whether every metadata side table is sized for the tree.
  /// \param tree Syntax tree whose node capacity must be covered exactly.
  /// \return True when all side tables match the tree node count.
  bool isSizedFor(const Tree& tree) const;

  void setParent(NodeId node, NodeId parent);
  NodeId parent(NodeId node) const;

  void setScope(NodeId node, uint32_t scopeId);
  uint32_t scope(NodeId node) const;

  void setDefinition(NodeId node, identity::DefId definition);
  identity::DefId definition(NodeId node) const;

  void setIsUnresolved(NodeId node, bool value);
  bool isUnresolved(NodeId node) const;

  void setIsDeferredMember(NodeId node, bool value);
  bool isDeferredMember(NodeId node) const;

  void setShadowOf(NodeId node, NodeId shadowed);
  NodeId shadowOf(NodeId node) const;

  void setIsReexport(NodeId node, bool value);
  bool isReexport(NodeId node) const;

  void setCaptures(NodeId node, NodeList captures);
  NodeList captures(NodeId node) const;

  void setLabelTarget(NodeId node, NodeId target);
  NodeId labelTarget(NodeId node) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
