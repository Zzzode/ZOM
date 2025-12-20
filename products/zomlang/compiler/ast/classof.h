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

namespace zomlang {
namespace compiler {
namespace ast {

class Node;
enum class SyntaxKind;

bool isNode(SyntaxKind kind);

#define AST_ELEMENT_NODE(Class, ...)
#define AST_INTERFACE_NODE(Class, Parent) bool is##Class(SyntaxKind kind);
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef AST_INTERFACE_NODE

#define GENERATE_CLASSOF_IMPL(name) \
  static bool classof(const Node& node) { return is##name(node.getKind()); }

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
