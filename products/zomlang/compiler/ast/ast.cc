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
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace ast {

// ================================================================================
// Node::Impl

struct Node::Impl {
  const source::SourceRange range;
};

Node::Node(source::SourceRange range) noexcept : impl(zc::heap<Impl>(range)) {}
Node::~Node() noexcept(false) = default;

// Node sourceRange method implementation
const source::SourceRange Node::sourceRange() const { return impl->range; }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang