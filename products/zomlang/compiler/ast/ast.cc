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

#include "zomlang/compiler/ast/ast.h"

#include "zc/core/memory.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/ast/visitor.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/symbol/symbol-table.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// NodeImpl::Impl

struct NodeImpl::Impl {
  SyntaxKind kind;

  source::SourceRange range;

  zc::Maybe<const Node&> parent = zc::none;

  Impl(SyntaxKind kind) : kind(kind) {}
};

NodeImpl::NodeImpl(SyntaxKind kind) noexcept : impl(zc::heap<Impl>(kind)) {}

NodeImpl::~NodeImpl() noexcept(false) = default;

void NodeImpl::setSourceRange(const source::SourceRange&& range) { impl->range = zc::mv(range); }

const source::SourceRange& NodeImpl::getSourceRange() const { return impl->range; }

SyntaxKind NodeImpl::getKind() const { return impl->kind; }

// ================================================================================
// TokenNode

struct TokenNode::Impl : private NodeImpl {
  explicit Impl(SyntaxKind kind) : NodeImpl(kind) {}

  // Forward NodeImpl methods
  using NodeImpl::getKind;
  using NodeImpl::getSourceRange;
  using NodeImpl::setSourceRange;
};

TokenNode::TokenNode(SyntaxKind kind) noexcept : impl(zc::heap<Impl>(kind)) {}

TokenNode::~TokenNode() noexcept(false) = default;

void TokenNode::setSourceRange(const source::SourceRange&& range) {
  impl->setSourceRange(zc::mv(range));
}

const source::SourceRange& TokenNode::getSourceRange() const { return impl->getSourceRange(); }

SyntaxKind TokenNode::getKind() const { return impl->getKind(); }

void TokenNode::accept(Visitor& visitor) const { visitor.visit(*this); }

// ================================================================================
// LocalsContainerImpl::Impl

struct LocalsContainerImpl::Impl {
  zc::Maybe<const symbol::SymbolTable&> locals = zc::none;
  zc::Maybe<const LocalsContainer&> nextContainer = zc::none;

  Impl() = default;
};

LocalsContainerImpl::LocalsContainerImpl() noexcept : impl(zc::heap<Impl>()) {}

LocalsContainerImpl::~LocalsContainerImpl() noexcept(false) = default;

zc::Maybe<const symbol::SymbolTable&> LocalsContainerImpl::getLocals() const {
  return impl->locals;
}

void LocalsContainerImpl::setLocals(zc::Maybe<const symbol::SymbolTable&> locals) {
  impl->locals = locals;
}

zc::Maybe<const LocalsContainer&> LocalsContainerImpl::getNextContainer() const {
  return impl->nextContainer;
}

void LocalsContainerImpl::setNextContainer(zc::Maybe<const LocalsContainer&> nextContainer) {
  impl->nextContainer = nextContainer;
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
