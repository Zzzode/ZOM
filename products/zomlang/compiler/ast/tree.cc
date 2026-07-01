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

#include "zomlang/compiler/ast/tree.h"

#include "zc/core/arena.h"
#include "zc/core/debug.h"
#include "zc/core/map.h"
#include "zomlang/compiler/symbol/symbol-id.h"

namespace zomlang {
namespace compiler {
namespace ast {

namespace {

size_t indexOf(NodeId id) {
  ZC_IREQUIRE(id.value != 0, "empty AST node id");
  return static_cast<size_t>(id.value - 1);
}

size_t indexOf(StringId id) {
  ZC_IREQUIRE(id.value != 0, "empty AST string id");
  return static_cast<size_t>(id.value - 1);
}

size_t indexOf(IdentId id) {
  ZC_IREQUIRE(id.value != 0, "empty AST identifier id");
  return static_cast<size_t>(id.value - 1);
}

size_t indexOf(BigIntId id) {
  ZC_IREQUIRE(id.value != 0, "empty AST integer literal id");
  return static_cast<size_t>(id.value - 1);
}

size_t indexOf(FloatId id) {
  ZC_IREQUIRE(id.value != 0, "empty AST floating-point literal id");
  return static_cast<size_t>(id.value - 1);
}

}  // namespace

struct Tree::Impl {
  zc::Arena stringArena;
  zc::Vector<Node> nodes;
  zc::Vector<NodeId> listStorage;
  zc::Vector<IdentId> identListStorage;
  zc::Vector<zc::StringPtr> strings;
  zc::Vector<zc::StringPtr> idents;
  zc::Vector<zc::StringPtr> bigInts;
  zc::Vector<zc::StringPtr> floats;
  zc::HashMap<zc::StringPtr, uint32_t> stringIndex;
  zc::HashMap<zc::StringPtr, uint32_t> identIndex;
  zc::HashMap<zc::StringPtr, uint32_t> bigIntIndex;
  zc::HashMap<zc::StringPtr, uint32_t> floatIndex;
  NodeId root;
};

Tree::Tree() noexcept : impl(zc::heap<Impl>()) {}

Tree::~Tree() noexcept(false) = default;

Tree::Tree(Tree&& other) noexcept = default;

Tree& Tree::operator=(Tree&& other) noexcept = default;

NodeId Tree::root() const { return impl->root; }

size_t Tree::nodeCount() const { return impl->nodes.size(); }

bool Tree::contains(NodeId id) const {
  return id.value != 0 && static_cast<size_t>(id.value) <= impl->nodes.size();
}

bool Tree::contains(NodeList list) const {
  return static_cast<size_t>(list.first) + static_cast<size_t>(list.size) <=
         impl->listStorage.size();
}

bool Tree::contains(IdentList list) const {
  return static_cast<size_t>(list.first) + static_cast<size_t>(list.size) <=
         impl->identListStorage.size();
}

const Node& Tree::node(NodeId id) const {
  ZC_IREQUIRE(contains(id), "AST node id is outside this tree");
  return impl->nodes[indexOf(id)];
}

zc::ArrayPtr<const Node> Tree::nodes() const { return impl->nodes.asPtr(); }

zc::ArrayPtr<const NodeId> Tree::list(NodeList list) const {
  ZC_IREQUIRE(contains(list), "AST node list is outside this tree");
  return impl->listStorage.slice(list.first, list.first + list.size);
}

zc::ArrayPtr<const IdentId> Tree::identList(IdentList list) const {
  ZC_IREQUIRE(contains(list), "AST identifier list is outside this tree");
  return impl->identListStorage.slice(list.first, list.first + list.size);
}

zc::StringPtr Tree::string(StringId id) const {
  ZC_IREQUIRE(indexOf(id) < impl->strings.size(), "AST string id is outside this tree");
  return impl->strings[indexOf(id)];
}

zc::StringPtr Tree::ident(IdentId id) const {
  ZC_IREQUIRE(indexOf(id) < impl->idents.size(), "AST identifier id is outside this tree");
  return impl->idents[indexOf(id)];
}

zc::StringPtr Tree::bigInt(BigIntId id) const {
  ZC_IREQUIRE(indexOf(id) < impl->bigInts.size(), "AST integer literal id is outside this tree");
  return impl->bigInts[indexOf(id)];
}

zc::StringPtr Tree::floatLiteral(FloatId id) const {
  ZC_IREQUIRE(indexOf(id) < impl->floats.size(),
              "AST floating-point literal id is outside this tree");
  return impl->floats[indexOf(id)];
}

NodeId Tree::appendNode(Node node) {
  impl->nodes.add(zc::mv(node));
  return NodeId(static_cast<uint32_t>(impl->nodes.size()));
}

NodeList Tree::appendList(zc::ArrayPtr<const NodeId> nodes) {
  NodeList result;
  result.first = static_cast<uint32_t>(impl->listStorage.size());
  result.size = static_cast<uint32_t>(nodes.size());
  for (NodeId node : nodes) {
    ZC_IREQUIRE(contains(node), "AST list element is outside this tree");
    impl->listStorage.add(node);
  }
  return result;
}

IdentList Tree::appendIdentList(zc::ArrayPtr<const IdentId> ids) {
  IdentList result;
  result.first = static_cast<uint32_t>(impl->identListStorage.size());
  result.size = static_cast<uint32_t>(ids.size());
  for (IdentId id : ids) {
    ZC_IREQUIRE(id.value != 0 && static_cast<size_t>(id.value) <= impl->idents.size(),
                "AST identifier list element is outside this tree");
    impl->identListStorage.add(id);
  }
  return result;
}

StringId Tree::appendString(zc::StringPtr value) {
  if (value.size() == 0) { return StringId(); }
  ZC_IF_SOME(found, impl->stringIndex.find(value)) { return StringId(found); }

  zc::StringPtr copy = impl->stringArena.copyString(value);
  impl->strings.add(copy);
  uint32_t id = static_cast<uint32_t>(impl->strings.size());
  impl->stringIndex.insert(copy, id);
  return StringId(id);
}

IdentId Tree::appendIdent(zc::StringPtr value) {
  if (value.size() == 0) { return IdentId(); }
  ZC_IF_SOME(found, impl->identIndex.find(value)) { return IdentId(found); }

  zc::StringPtr copy = impl->stringArena.copyString(value);
  impl->idents.add(copy);
  uint32_t id = static_cast<uint32_t>(impl->idents.size());
  impl->identIndex.insert(copy, id);
  return IdentId(id);
}

BigIntId Tree::appendBigInt(zc::StringPtr value) {
  if (value.size() == 0) { return BigIntId(); }
  ZC_IF_SOME(found, impl->bigIntIndex.find(value)) { return BigIntId(found); }

  zc::StringPtr copy = impl->stringArena.copyString(value);
  impl->bigInts.add(copy);
  uint32_t id = static_cast<uint32_t>(impl->bigInts.size());
  impl->bigIntIndex.insert(copy, id);
  return BigIntId(id);
}

FloatId Tree::appendFloat(zc::StringPtr value) {
  if (value.size() == 0) { return FloatId(); }
  ZC_IF_SOME(found, impl->floatIndex.find(value)) { return FloatId(found); }

  zc::StringPtr copy = impl->stringArena.copyString(value);
  impl->floats.add(copy);
  uint32_t id = static_cast<uint32_t>(impl->floats.size());
  impl->floatIndex.insert(copy, id);
  return FloatId(id);
}

void Tree::setRoot(NodeId id) {
  ZC_IREQUIRE(contains(id), "AST root id is outside this tree");
  impl->root = id;
}

TreeBuilder::TreeBuilder() noexcept = default;

TreeBuilder::~TreeBuilder() noexcept(false) = default;

TreeBuilder::TreeBuilder(TreeBuilder&& other) noexcept = default;

TreeBuilder& TreeBuilder::operator=(TreeBuilder&& other) noexcept = default;

NodeId TreeBuilder::makeNode(SyntaxKind kind, source::SourceRange range, NodePayload payload) {
  Node node;
  node.kind = kind;
  node.range = zc::mv(range);
  node.payload = payload;
  return tree.appendNode(zc::mv(node));
}

NodeList TreeBuilder::makeList(zc::ArrayPtr<const NodeId> nodes) { return tree.appendList(nodes); }

IdentList TreeBuilder::makeIdentList(zc::ArrayPtr<const IdentId> ids) {
  return tree.appendIdentList(ids);
}

StringId TreeBuilder::internString(zc::StringPtr value) { return tree.appendString(value); }

IdentId TreeBuilder::internIdent(zc::StringPtr value) { return tree.appendIdent(value); }

BigIntId TreeBuilder::internBigInt(zc::StringPtr value) { return tree.appendBigInt(value); }

FloatId TreeBuilder::internFloat(zc::StringPtr value) { return tree.appendFloat(value); }

void TreeBuilder::setRoot(NodeId id) { tree.setRoot(id); }

Tree TreeBuilder::finish() { return zc::mv(tree); }

struct BindingMetadata::Impl {
  zc::Vector<NodeId> parents;
  zc::Vector<uint32_t> scopes;
  zc::Vector<symbol::SymbolId> symbols;
};

BindingMetadata::BindingMetadata() noexcept : impl(zc::heap<Impl>()) {}

BindingMetadata::~BindingMetadata() noexcept(false) = default;

BindingMetadata::BindingMetadata(BindingMetadata&& other) noexcept = default;

BindingMetadata& BindingMetadata::operator=(BindingMetadata&& other) noexcept = default;

void BindingMetadata::resizeFor(const Tree& tree) {
  impl->parents.resize(tree.nodeCount());
  impl->scopes.resize(tree.nodeCount());
  impl->symbols.resize(tree.nodeCount());
}

void BindingMetadata::setParent(NodeId node, NodeId parent) {
  ZC_IREQUIRE(indexOf(node) < impl->parents.size(), "metadata parent write is outside tree");
  impl->parents[indexOf(node)] = parent;
}

NodeId BindingMetadata::parent(NodeId node) const {
  ZC_IREQUIRE(indexOf(node) < impl->parents.size(), "metadata parent read is outside tree");
  return impl->parents[indexOf(node)];
}

void BindingMetadata::setScope(NodeId node, uint32_t scopeId) {
  ZC_IREQUIRE(indexOf(node) < impl->scopes.size(), "metadata scope write is outside tree");
  impl->scopes[indexOf(node)] = scopeId;
}

uint32_t BindingMetadata::scope(NodeId node) const {
  ZC_IREQUIRE(indexOf(node) < impl->scopes.size(), "metadata scope read is outside tree");
  return impl->scopes[indexOf(node)];
}

void BindingMetadata::setSymbol(NodeId node, symbol::SymbolId symbolId) {
  ZC_IREQUIRE(indexOf(node) < impl->symbols.size(), "metadata symbol write is outside tree");
  impl->symbols[indexOf(node)] = symbolId;
}

symbol::SymbolId BindingMetadata::symbol(NodeId node) const {
  ZC_IREQUIRE(indexOf(node) < impl->symbols.size(), "metadata symbol read is outside tree");
  return impl->symbols[indexOf(node)];
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
