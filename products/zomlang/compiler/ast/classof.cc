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

#include "zomlang/compiler/ast/classof.h"

#include <type_traits>

#include "zomlang/compiler/ast/kinds.h"

namespace zomlang::compiler::ast {

namespace hierarchy {
struct Root {};

template <typename... Bases>
struct Inherit : virtual Bases... {};

// Forward declarations
#define AST_INTERFACE_NODE(Class, Parent) struct Class;
#define AST_ELEMENT_NODE(Class, ...) struct Class;
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_INTERFACE_NODE
#undef AST_ELEMENT_NODE

// Define Interfaces
// Handle Node->Root and Declaration->Node mapping
// We use is_same_v checks to handle self-referential parents in the macro
#define AST_INTERFACE_NODE(Class, Parent)                                              \
  struct Class : virtual std::conditional_t<                                           \
                     std::is_same_v<Class, Parent>,                                    \
                     std::conditional_t<std::is_same_v<Class, hierarchy::Declaration>, \
                                        hierarchy::Node, hierarchy::Root>,             \
                     Parent> {};

// Define Elements
#define AST_ELEMENT_NODE(Class, ...) \
  struct Class : Inherit<__VA_ARGS__> {};
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_INTERFACE_NODE
#undef AST_ELEMENT_NODE
}  // namespace hierarchy

namespace {
template <typename Target>
bool isInstanceOf(SyntaxKind kind) {
  switch (kind) {
// Re-define ESC and STRIP_PARENS for this expansion as well, although not used in switch case logic
// but AST_ELEMENT_NODE macro might need them if we were defining classes.
// Here we just use the class name.
#define ESC(...) __VA_ARGS__
#define AST_ELEMENT_NODE(Class, ...) \
  case SyntaxKind::Class:            \
    return std::is_base_of_v<Target, hierarchy::Class>;
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef ESC
    default:
      return false;
  }
}
}  // namespace

#define AST_INTERFACE_NODE(Class, Parent) \
  bool is##Class(SyntaxKind kind) { return isInstanceOf<hierarchy::Class>(kind); }
// We don't need AST_ELEMENT_NODE here as they are not declared in header
#define AST_ELEMENT_NODE(Class, ...)

#include "zomlang/compiler/ast/ast-nodes.def"

#undef AST_INTERFACE_NODE
#undef AST_ELEMENT_NODE

}  // namespace zomlang::compiler::ast
