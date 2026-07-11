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
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {

namespace symbol {
class ClassSymbol;
class InterfaceSymbol;
class TypeSymbol;
class VariableSymbol;
}  // namespace symbol

namespace checker {

enum class AssociatedTypeResolutionKind { NotFound, Resolved, Ambiguous };

struct AssociatedTypeResolution {
  AssociatedTypeResolutionKind kind;
  zc::Maybe<const type::Type&> type;
};

/// \brief TraitResolver - Resolves trait/interface implementations for types.
///
/// TraitResolver is responsible for the interface resolution phase of the type
/// checker as specified in RFC 0005:
///
/// 1. Finding which interfaces a type implements (via impl blocks)
/// 2. Checking impl coherence (orphan rule, no duplicate impls)
/// 3. Auto-deriving marker traits (Sendable, Shared) based on type structure
/// 4. Resolving associated type bindings from impl blocks
///
/// The resolver walks the AST to discover impl declarations, registers them
/// in the TypeEnv's impl table, and provides query methods for the rest of
/// the type checker.
///
/// Marker trait auto-derivation follows RFC 0005 rules:
/// - Sendable: all fields are Sendable types
/// - Shared: all fields are Shared types
/// - Primitive types are always Sendable + Shared
/// - &T is Shared if T is Shared; &mut T is Sendable if T is Sendable
/// - Raw pointers (*const T, *mut T) are neither Sendable nor Shared
class TraitResolver final {
public:
  /// \brief Construct a TraitResolver.
  ///
  /// \param typeEnv   The type environment for impl registration and lookup.
  /// \param symbols   The symbol table (populated by the binder).
  /// \param tree      The immutable AST tree to process.
  /// \param metadata  Binding metadata from the binder (symbol/scope bindings).
  /// \param diags     Diagnostic engine for error reporting.
  TraitResolver(type::TypeEnv& typeEnv, symbol::SymbolTable& symbols, const ast::Tree& tree,
                const ast::BindingMetadata& metadata,
                diagnostics::DiagnosticEngine& diags) noexcept;

  ~TraitResolver() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TraitResolver);

  // ==========================================================================
  // Public API
  // ==========================================================================

  /// \brief Check if a type implements a given interface.
  ///
  /// For marker traits (Sendable, Shared), this uses auto-derivation rules.
  /// For other interfaces, this checks the impl table and class hierarchy.
  ///
  /// \param ty         The type to check.
  /// \param ifaceName  The name of the interface (e.g., "Sendable", "Drawable").
  /// \return true if the type implements the interface.
  bool implements(const type::Type& ty, zc::StringPtr ifaceName);

  /// \brief Find the impl block AST node for a type implementing an interface.
  ///
  /// \return The NodeId of the impl declaration, or zc::none if not found.
  zc::Maybe<ast::NodeId> findImpl(const type::Type& ty, zc::StringPtr ifaceName);

  /// \brief Resolve an associated type for a type implementing an interface.
  ///
  /// Given a type and an associated type name, finds the relevant impl block
  /// and returns the concrete type bound to the associated type.
  ///
  /// \param ty         The implementing type.
  /// \param assocName  The associated type name (e.g., "Item").
  /// \return The resolved associated type, or zc::none if not found.
  zc::Maybe<const type::Type&> resolveAssociatedType(const type::Type& ty, zc::StringPtr assocName);

  /// \brief Resolve an associated type and report ambiguity separately.
  AssociatedTypeResolution resolveAssociatedTypeWithStatus(const type::Type& ty,
                                                           zc::StringPtr assocName);

  /// \brief Resolve an associated type through a specific interface impl.
  AssociatedTypeResolution resolveAssociatedTypeWithStatus(const type::Type& ty,
                                                           zc::StringPtr ifaceName,
                                                           zc::StringPtr assocName);

  /// \brief Check if a type auto-derives the Sendable marker trait.
  ///
  /// Follows RFC 0005 auto-derivation rules:
  /// - Primitive types: Sendable
  /// - &T: not Sendable (shared references are not movable across threads)
  /// - &mut T: Sendable if T is Sendable
  /// - *const T / *mut T: not Sendable (raw pointers are not thread-safe)
  /// - Struct/Class: Sendable if all fields are Sendable
  /// - Tuple: Sendable if all elements are Sendable
  /// - Array: Sendable if element type is Sendable
  /// - Function: Sendable (code pointers are safe to move)
  bool isAutoSendable(const type::Type& ty);

  /// \brief Check if a type auto-derives the Shared marker trait.
  ///
  /// Follows RFC 0005 auto-derivation rules:
  /// - Primitive types: Shared
  /// - &T: Shared if T is Shared
  /// - &mut T: not Shared (mutable references are not shareable)
  /// - *const T / *mut T: not Shared
  /// - Struct/Class: Shared if all fields are Shared
  /// - Tuple: Shared if all elements are Shared
  /// - Array: Shared if element type is Shared
  /// - Function: Shared
  bool isAutoShared(const type::Type& ty);

  /// \brief Run coherence checks on all impl blocks in the AST.
  ///
  /// Checks:
  /// 1. Orphan rule: at least one of the type or interface must be local
  /// 2. No duplicate impls for the same (type, interface) pair
  ///
  /// Reports diagnostics for any violations found.
  void checkCoherence();

  /// \brief Discover and register all impl blocks from the AST.
  ///
  /// Walks the AST, finds all StandaloneImplDecl and MarkerImpl nodes,
  /// extracts the implemented interfaces and target types, and registers
  /// them in the TypeEnv.
  void discoverImpls();

private:
  struct Impl;
  zc::Own<Impl> impl;

  // ==========================================================================
  // Internal helpers
  // ==========================================================================

  /// \brief Check if an impl block matches a given type and interface.
  ///
  /// Verifies that the impl's "for type" matches `ty` and that the
  /// interface list contains `ifaceName`.
  bool checkImplMatches(ast::NodeId implNode, const type::Type& ty, zc::StringPtr ifaceName);

  /// \brief Check if all fields of a named type satisfy the Sendable predicate.
  bool allFieldsAreSend(const type::NamedType& namedTy);

  /// \brief Check if all fields of a named type satisfy the Shared predicate.
  bool allFieldsAreSync(const type::NamedType& namedTy);

  /// \brief Check if all fields of a class symbol are Sendable.
  bool allClassFieldsAreSend(const symbol::ClassSymbol& cls);

  /// \brief Check if all fields of a class symbol are Shared.
  bool allClassFieldsAreSync(const symbol::ClassSymbol& cls);

  /// \brief Extract the "for type" from an impl declaration.
  ///
  /// Resolves the type expression in the impl's for clause to a Type.
  zc::Own<type::Type> resolveImplForType(ast::NodeId implNode);

  /// \brief Extract interface names from an impl's interface list.
  zc::Vector<zc::StringPtr> resolveImplIfaceNames(ast::NodeId implNode);

  /// \brief Extract the marker path name from a MarkerImpl node.
  zc::StringPtr resolveMarkerImplName(ast::NodeId markerImplNode);

  /// \brief Check if a type is "local" (defined in the current compilation unit).
  bool isTypeLocal(const type::Type& ty);

  /// \brief Check if an interface name is "local".
  bool isInterfaceLocal(zc::StringPtr ifaceName);

  /// \brief Get the type name for a type (for named types, returns the name).
  zc::StringPtr getTypeName(const type::Type& ty);

  /// \brief Look up a symbol by name in the current scope chain.
  zc::Maybe<symbol::Symbol&> lookupSymbol(zc::StringPtr name);

  /// \brief Get the current scope from the symbol table.
  const symbol::Scope& currentScope();

  /// \brief Resolve a type expression from the AST.
  zc::Own<type::Type> resolveTypeExpr(ast::NodeId typeExpr);

  /// \brief Extract a path name from a ModulePath or NamedTypeExpr node.
  zc::StringPtr resolvePathName(ast::NodeId pathNode);

  /// \brief Return direct parent interfaces declared by an interface.
  zc::Vector<zc::StringPtr> parentInterfaceNames(zc::StringPtr ifaceName);
};

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
