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

#include <type_traits>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/kinds.h"

namespace zomlang {
namespace compiler {
namespace ast {

/// \brief Type-safe casting utilities for AST nodes, similar to LLVM's cast system
///
/// This provides a safe alternative to static_cast by checking the SyntaxKind
/// or using classof methods before performing the cast operation.
///
/// ## Function Categories:
/// - **isa<T>()**: Type checking - returns bool indicating if node is of type T
/// - **cast<T>()**: Checked casting - throws on failure, use when certain cast will succeed
/// - **dyn_cast<T>()**: Safe casting - returns Maybe<T>, use when cast might fail
///
/// ## Usage Guidelines:
/// - Use `isa<T>()` to check if a node is of a specific type
/// - Use `cast<T>()` when you're certain the cast will succeed (throws on failure)
/// - Use `dyn_cast<T>()` when the cast might fail (returns Maybe)
///
/// ## Examples:
/// \code
/// // Type checking
/// if (isa<FunctionDeclaration>(node)) {
///   // node is a FunctionDeclaration
/// }
///
/// // Checked casting (throws if invalid)
/// auto& funcDecl = cast<FunctionDeclaration>(node);
///
/// // Safe casting (returns Maybe)
/// if (auto funcDecl = dyn_cast<FunctionDeclaration>(node)) {
///   // Use funcDecl.value()
/// }
/// \endcode

/// \brief Type trait to get the SyntaxKind for a given AST node type
/// Each concrete AST node type should specialize this template
template <typename T>
struct SyntaxKindTrait;

// Forward declarations for all concrete AST node types using X-macro
#define AST_ELEMENT_NODE(ClassName, ...) class ClassName;
#define AST_INTERFACE_NODE(ClassName, Parent) class ClassName;
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef AST_INTERFACE_NODE

// SyntaxKindTrait specializations for all AST node types using X-macro
// Helper for stripping parens to handle multiple inheritance in X-macro
#define ESC(...) __VA_ARGS__
#define STRIP_PARENS(X) ESC X

#define AST_ELEMENT_NODE(ClassName, ...)                       \
  template <>                                                  \
  struct SyntaxKindTrait<ClassName> {                          \
    static constexpr SyntaxKind value = SyntaxKind::ClassName; \
  };                                                           \
  template <>                                                  \
  struct SyntaxKindTrait<ClassName&> {                         \
    static constexpr SyntaxKind value = SyntaxKind::ClassName; \
  };                                                           \
  template <>                                                  \
  struct SyntaxKindTrait<const ClassName&> {                   \
    static constexpr SyntaxKind value = SyntaxKind::ClassName; \
  };
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
#undef STRIP_PARENS
#undef ESC

namespace _ {  // Private implementation details

/// \brief Convert SyntaxKind to string for debugging purposes
/// \param kind The SyntaxKind to convert
/// \return String representation of the SyntaxKind
inline zc::StringPtr syntaxKindToString(SyntaxKind kind) noexcept {
  switch (kind) {
#define AST_ELEMENT_NODE(ClassName, ...) \
  case SyntaxKind::ClassName:            \
    return #ClassName##_zc;
#include "zomlang/compiler/ast/ast-nodes.def"
#undef AST_ELEMENT_NODE
    default:
      return "Unknown"_zc;
  }
}

/// \brief Concept for concrete AST nodes (those with a SyntaxKind)
template <typename T>
concept ConcreteASTNode = requires {
  typename SyntaxKindTrait<std::remove_reference_t<T>>;
  SyntaxKindTrait<std::remove_reference_t<T>>::value;
};

/// \brief Get the expected SyntaxKind for a concrete AST node type
/// \tparam T The AST node type
/// \return The SyntaxKind for the type T
template <ConcreteASTNode T>
constexpr SyntaxKind getExpectedSyntaxKind() noexcept {
  return SyntaxKindTrait<std::remove_reference_t<T>>::value;
}

/// \brief Concept to check if a type is an interface AST node (has classof method)
template <typename T>
concept InterfaceASTNode = requires(const Node& n) { T::classof(n); } && !ConcreteASTNode<T>;

/// \brief Unified concept for all AST node types
template <typename T>
concept ASTNode = ConcreteASTNode<T> || InterfaceASTNode<T>;

/// \brief Helper to determine if a node matches a concrete type
template <ConcreteASTNode T>
ZC_NODISCARD bool nodeMatches(const Node& node) noexcept {
  return node.getKind() == SyntaxKindTrait<std::remove_reference_t<T>>::value;
}

/// \brief Helper to determine if a node matches an interface type
template <InterfaceASTNode T>
ZC_NODISCARD bool nodeMatches(const Node& node) noexcept {
  return T::classof(node);
}

}  // namespace _

//==============================================================================
// Type Checking Functions (isa)
//==============================================================================

/// \brief Check if a node is of a specific type
/// \tparam T The target AST node type (concrete or interface)
/// \param node The node to check
/// \return true if the node is of type T, false otherwise
///
/// Example:
/// \code
/// if (isa<FunctionDeclaration>(node)) {
///   // node is a FunctionDeclaration
/// }
/// if (isa<Expression>(node)) {
///   // node is any kind of Expression
/// }
/// \endcode
template <_::ASTNode T>
ZC_NODISCARD bool isa(const Node& node) noexcept {
  return _::nodeMatches<T>(node);
}

/// \brief Check if a zc::Own<Node> is of a specific type
/// \tparam T The target AST node type
/// \param node The owned node to check
/// \return True if the node is of type T, false otherwise
template <_::ASTNode T>
ZC_NODISCARD bool isa(const zc::Own<Node>& node) noexcept {
  return isa<T>(*node);
}

//==============================================================================
// Checked Casting Functions (cast)
//==============================================================================

/// \brief Perform a checked cast to a specific AST node type
/// \tparam T The target AST node type
/// \param node The node to cast
/// \return Reference to the casted node
/// \throws zc::Exception if the cast is invalid
///
/// Use this when you're certain the cast will succeed. If there's any doubt,
/// use dyn_cast instead.
///
/// Example:
/// \code
/// auto& funcDecl = cast<FunctionDeclaration>(node);
/// auto& expr = cast<Expression>(node);
/// \endcode
template <_::ASTNode T>
ZC_NODISCARD auto cast(Node& node) -> std::conditional_t<std::is_reference_v<T>, T, T&> {
  if (!isa<T>(node)) {
    if constexpr (_::ConcreteASTNode<T>) {
      auto expectedKind = _::getExpectedSyntaxKind<T>();
      auto actualKind = node.getKind();
      ZC_FAIL_REQUIRE(zc::str("Invalid AST node cast: expected ",
                              _::syntaxKindToString(expectedKind), " but got ",
                              _::syntaxKindToString(actualKind)));
    } else {
      ZC_FAIL_REQUIRE(
          zc::str("Invalid AST node cast: node does not match interface type, actual kind is ",
                  _::syntaxKindToString(node.getKind())));
    }
  }
  if constexpr (std::is_reference_v<T>) {
    return static_cast<T>(node);
  } else {
    return static_cast<T&>(node);
  }
}

/// \brief Perform a checked cast to a specific AST node type (const version)
/// \tparam T The target AST node type
/// \param node The node to cast
/// \return Const reference to the casted node
/// \throws zc::Exception if the cast is invalid
template <_::ASTNode T>
ZC_NODISCARD auto cast(const Node& node)
    -> std::conditional_t<std::is_reference_v<T>, T, const T&> {
  if (!isa<T>(node)) {
    if constexpr (_::ConcreteASTNode<T>) {
      auto expectedKind = _::getExpectedSyntaxKind<T>();
      auto actualKind = node.getKind();
      ZC_FAIL_REQUIRE(zc::str("Invalid AST node cast: expected ",
                              _::syntaxKindToString(expectedKind), " but got ",
                              _::syntaxKindToString(actualKind)));
    } else {
      ZC_FAIL_REQUIRE(
          zc::str("Invalid AST node cast: node does not match interface type, actual kind is ",
                  _::syntaxKindToString(node.getKind())));
    }
  }
  if constexpr (std::is_reference_v<T>) {
    return static_cast<T>(node);
  } else {
    return static_cast<const T&>(node);
  }
}

/// \brief Perform a checked cast on a zc::Own<Node> to a specific AST node type
/// \tparam T The target AST node type
/// \param node The owned node to cast
/// \return Reference to the casted node
/// \throws zc::Exception if the cast is invalid
template <_::ASTNode T>
ZC_NODISCARD auto cast(zc::Own<Node>& node) -> std::conditional_t<std::is_reference_v<T>, T, T&> {
  return cast<T>(*node);
}

/// \brief Perform a checked cast on a const zc::Own<Node> to a specific AST node type
/// \tparam T The target AST node type
/// \param node The owned node to cast
/// \return Const reference to the casted node
/// \throws zc::Exception if the cast is invalid
template <_::ASTNode T>
ZC_NODISCARD auto cast(const zc::Own<Node>& node)
    -> std::conditional_t<std::is_reference_v<T>, T, const T&> {
  return cast<T>(*node);
}

/// \brief Perform a checked cast on a zc::Own<U> to a specific AST node type, transferring
/// ownership
/// \tparam T The target AST node type
/// \param node The owned node to cast
/// \return Owned pointer to the casted node
/// \throws zc::Exception if the cast is invalid
///
/// Use this when you're certain the cast will succeed and need to transfer ownership.
/// If there's any doubt, use dyn_cast instead.
///
/// Example:
/// \code
/// auto memberExpr = cast<MemberExpression>(zc::mv(expr));
/// \endcode
template <_::ASTNode T, typename U>
ZC_NODISCARD zc::Own<T> cast(zc::Own<U>&& node) {
  static_assert(std::is_base_of_v<Node, U>, "U must be derived from Node");
  if (!isa<T>(*node)) {
    if constexpr (_::ConcreteASTNode<T>) {
      auto expectedKind = _::getExpectedSyntaxKind<T>();
      auto actualKind = node->getKind();
      ZC_FAIL_REQUIRE(zc::str("Invalid AST node cast: expected ",
                              _::syntaxKindToString(expectedKind), " but got ",
                              _::syntaxKindToString(actualKind)));
    } else {
      ZC_FAIL_REQUIRE(
          zc::str("Invalid AST node cast: node does not match interface type, actual kind is ",
                  _::syntaxKindToString(node->getKind())));
    }
  }
  // Use downcast to safely transfer ownership with correct disposer
  return node.template downcast<T>();
}

//==============================================================================
// Safe Casting Functions (dyn_cast)
//==============================================================================

/// \brief Safely cast a node to a specific type, returning Maybe if invalid
/// \tparam T The target AST node type
/// \param node The node to cast
/// \return Maybe containing reference to the casted node, or none if cast is invalid
///
/// Use this when the cast might fail. It's safer than cast() but requires
/// checking the result.
///
/// Example:
/// \code
/// if (auto funcDecl = dyn_cast<FunctionDeclaration>(node)) {
///   // Use funcDecl.value()
/// }
/// \endcode
template <_::ASTNode T>
ZC_NODISCARD zc::Maybe<T&> dyn_cast(Node& node) noexcept {
  if (isa<T>(node)) { return static_cast<T&>(node); }
  return zc::none;
}

/// \brief Safely cast a const node to a specific type, returning Maybe if invalid
/// \tparam T The target AST node type
/// \param node The node to cast
/// \return Maybe containing const reference to the casted node, or none if cast fails
template <_::ASTNode T>
ZC_NODISCARD zc::Maybe<const T&> dyn_cast(const Node& node) noexcept {
  if (isa<T>(node)) { return static_cast<const T&>(node); }
  return zc::none;
}

/// \brief Safely cast a zc::Own<Node> to a specific type, returning Maybe if invalid
/// \tparam T The target AST node type
/// \param node The owned node to cast
/// \return Maybe containing the casted owned node, or none if cast is invalid
///
/// Use this when the cast might fail. It's safer than cast() but requires
/// checking the result.
///
/// Example:
/// \code
/// if (auto funcDecl = dyn_cast<FunctionDeclaration>(ownedNode)) {
///   // Use funcDecl.value()
/// }
/// \endcode
template <_::ASTNode T, typename U>
ZC_NODISCARD zc::Maybe<zc::Own<T>> dyn_cast(zc::Own<U>&& node) noexcept {
  static_assert(std::is_base_of_v<Node, U>, "U must be derived from Node");
  if (isa<T>(*node)) {
    // Use downcast to safely transfer ownership with correct disposer
    return node.template downcast<T>();
  }
  return zc::none;
}

/// \brief Safely cast a const zc::Own<Node> to a specific type, returning Maybe if invalid
/// \tparam T The target AST node type
/// \param node The owned node to cast
/// \return Maybe containing const reference to the casted node, or none if cast fails
template <_::ASTNode T>
ZC_NODISCARD zc::Maybe<const T&> dyn_cast(const zc::Own<Node>& node) noexcept {
  return dyn_cast<T>(*node);
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
