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

#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/symbol/symbol-id.h"
#include "zomlang/compiler/type/coercion.h"
#include "zomlang/compiler/type/type-interner.h"
#include "zomlang/compiler/type/type-scheme.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace type {

class TypeVar;
class ErrorType;

/// \brief Dispatch target category for checked call-like expressions.
enum class CallTargetKind {
  PrimitiveOperator,
  FreeFunction,
  InstanceMethod,
  StaticMethod,
  QualifiedInterfaceMethod,
  OperatorMethod,
  IndexMethod,
  DynVTable,
  ErrorTarget
};

/// \brief Receiver passing mode for checked call-like expressions.
enum class ReceiverMode {
  None,
  ExplicitFirstArgument,
  ImplicitSelf,
  OperatorLeftHandSide,
  OperatorOperand,
  IndexBase
};

/// \brief Resolved dispatch metadata for a checked call-like expression.
struct CallDispatchRecord {
  CallTargetKind targetKind = CallTargetKind::ErrorTarget;
  ReceiverMode receiverMode = ReceiverMode::None;
  zc::String interfaceName;
  zc::String methodName;
  symbol::SymbolId targetSymbol;
  ast::NodeId implNode;
  uint32_t vtableSlot = 0;
  zc::Vector<TypeId> argumentTypes;
  TypeId resultType;
};

/// \brief TypeEnv - Type environment for the ZOM type checker.
///
/// TypeEnv is the core data structure of the type checker, maintaining:
/// 1. NodeId -> Type mapping (the inferred type for each expression node)
/// 2. Type variable storage and substitution environment for unification
/// 3. Impl table (which types implement which interfaces)
/// 4. Type scheme generalization and instantiation (let-polymorphism)
///
/// The type environment supports:
/// - Binding type variables to concrete types during unification
/// - Resolving type variables through binding chains (path compression)
/// - Occurs check to prevent infinite types
/// - Generating fresh type variable IDs
/// - Interface implementation lookup
/// - Generalizing types to type schemes (∀-introduction)
/// - Instantiating type schemes to fresh instances (∀-elimination)
///
/// Lifetime note: The environment stores non-owning pointers for type
/// variable bindings. The caller must ensure that all bound types outlive
/// the environment (typically by allocating types in a type arena or
/// keeping them alive for the duration of type checking).
///
/// Type variables created via freshTypeVar() are owned by the environment.
class TypeEnv {
public:
  TypeEnv();

  ~TypeEnv() noexcept(false);

  ZC_DISALLOW_COPY(TypeEnv);

  // Move semantics
  TypeEnv(TypeEnv&& other) noexcept;
  TypeEnv& operator=(TypeEnv&& other) noexcept;

  // =========================================================================
  // Node type mapping
  // =========================================================================

  /// \brief Assign a type to an AST node.
  ///
  /// Records the inferred type for the given expression or declaration node.
  /// If the node already has a type, it is replaced. The environment takes
  /// ownership of the type.
  void setType(ast::NodeId node, zc::Own<Type> ty);

  /// \brief Get the type assigned to an AST node.
  ///
  /// The node must have a type assigned (use hasType() to check).
  /// Returns a reference to the stored type.
  const Type& getType(ast::NodeId node) const;

  /// \brief Check if a type has been assigned to the given node.
  bool hasType(ast::NodeId node) const;

  /// \brief Get the canonical interned type id assigned to an AST node.
  ///
  /// The node must have a type assigned (use hasTypeId() to check).
  TypeId getTypeId(ast::NodeId node) const;

  /// \brief Check if a canonical type id has been assigned to the given node.
  bool hasTypeId(ast::NodeId node) const;

  /// \brief Return the number of AST nodes with assigned types.
  size_t nodeTypeCount() const;

  /// \brief Intern an arbitrary type in the environment's canonical interner.
  TypeId internType(const Type& type);

  // =========================================================================
  // Coercion records
  // =========================================================================

  /// \brief Record a coercion inserted at an AST node.
  void setCoercion(ast::NodeId node, CoercionKind kind);

  /// \brief Check whether a coercion was recorded for an AST node.
  bool hasCoercion(ast::NodeId node) const;

  /// \brief Get the coercion kind recorded for an AST node.
  CoercionKind getCoercion(ast::NodeId node) const;

  // =========================================================================
  // Dispatch records
  // =========================================================================

  /// \brief Record resolved dispatch metadata for a call-like AST node.
  void setDispatch(ast::NodeId node, CallDispatchRecord record);

  /// \brief Check whether dispatch metadata was recorded for an AST node.
  bool hasDispatch(ast::NodeId node) const;

  /// \brief Get resolved dispatch metadata for an AST node.
  const CallDispatchRecord& getDispatch(ast::NodeId node) const;

  // =========================================================================
  // Type variable creation
  // =========================================================================

  /// \brief Create a new fresh type variable with a unique ID.
  ///
  /// The type variable is owned by the TypeEnv and will live for the
  /// environment's lifetime. Returns a reference to the new variable.
  TypeVar& freshTypeVar();

  /// \brief Create a new fresh type variable with a given name hint.
  TypeVar& freshTypeVar(zc::StringPtr name);

  // =========================================================================
  // Union-Find for type variables
  // =========================================================================

  /// \brief Find the representative type for a type.
  ///
  /// If `ty` is a TypeVar, follows the union-find chain and any concrete
  /// binding to find the ultimate representative. Implements path compression:
  /// intermediate links are updated to point directly to the root.
  ///
  /// For non-TypeVar types, returns the type unchanged.
  ///
  /// Returns a non-const reference so callers can modify the representative
  /// (e.g., to add bounds). The method is const because path compression is
  /// an internal optimization that does not change observable state.
  Type& find(Type& ty) const;

  /// \brief Const overload of find.
  const Type& find(const Type& ty) const;

  /// \brief Unify two types in the union-find structure.
  ///
  /// If both are TypeVars, links them via union by rank.
  /// If one is a TypeVar and the other is concrete, binds the variable.
  /// If both are concrete, this is a no-op (caller should verify equality).
  ///
  /// Note: This method takes const references because it modifies the
  /// union-find state stored in TypeEnv, not the type objects themselves.
  void unite(const Type& a, const Type& b);

  // =========================================================================
  // Let-polymorphism: Generalization and Instantiation
  // =========================================================================

  /// \brief Generalize a type to a type scheme (∀-introduction).
  ///
  /// Converts a monomorphic type into a polymorphic type scheme by
  /// quantifying over all type variables that are:
  /// 1. Not bound to concrete types (still "unknown")
  /// 2. Not referenced in the current environment's non-generic bindings
  ///
  /// This implements the HM rule:
  ///   Γ ⊢ e : τ    (α ∉ FTV(Γ))
  ///   --------------------------
  ///   Γ ⊢ e : ∀α.τ
  ///
  /// \param ty The type to generalize.
  /// \return A type scheme quantifying over the free type variables.
  zc::Own<TypeScheme> generalize(const Type& ty);

  /// \brief Instantiate a type scheme to a fresh instance (∀-elimination).
  ///
  /// Creates a fresh copy of the type scheme's body, replacing each
  /// quantified type variable with a fresh type variable.
  ///
  /// This implements the HM rule:
  ///   Γ ⊢ e : ∀α.τ
  ///   -------------------
  ///   Γ ⊢ e : τ[α := fresh]
  ///
  /// The returned type is owned by the caller and should be stored
  /// in the type environment or used for unification.
  ///
  /// \param scheme The type scheme to instantiate.
  /// \return A fresh instance of the type scheme's body.
  zc::Own<Type> instantiate(const TypeScheme& scheme);

  /// \brief Instantiate a generic function type.
  ///
  /// Convenience method: if the function type has generic parameters,
  /// creates a type scheme from it and instantiates. If monomorphic,
  /// returns a clone of the function type.
  ///
  /// \param fnTy The function type (possibly generic).
  /// \return A fresh instance with fresh type variables for each generic param.
  zc::Own<Type> instantiateFunction(const FunctionType& fnTy);

  // =========================================================================
  // Legacy binding API
  // =========================================================================

  /// \brief Resolve a type by following all type variable bindings.
  ///
  /// If `ty` is a TypeVar with a binding, follows the chain until
  /// a non-TypeVar or unbound TypeVar is found.
  ///
  /// Implements path compression: after resolution, intermediate
  /// variables in the chain are updated to point directly to the
  /// root for faster subsequent lookups.
  const Type& resolve(const Type& ty) const;

  /// \brief Non-const resolve: returns a mutable reference to the
  /// ultimate resolved type.
  Type& resolve(Type& ty) const;

  /// \brief Bind a type variable to a concrete type.
  ///
  /// Records the substitution: var := type.
  /// The type variable must not already be bound.
  ///
  /// The environment stores a non-owning reference to the type; the
  /// caller must ensure the type outlives all references through this
  /// environment.
  void bind(const TypeVar& var, const Type& type);

  /// \brief Bind a type variable to a concrete type (owning version).
  ///
  /// Records the substitution: var := type. The environment takes
  /// ownership of the type.
  void bind(const TypeVar& var, zc::Own<Type> type);

  /// \brief Look up the direct binding for a type variable.
  ///
  /// Returns the bound type, or zc::none if the variable is unbound.
  /// Does NOT follow chains of bindings (use resolve() for that).
  zc::Maybe<const Type&> lookup(const TypeVar& var) const;

  /// \brief Occurs check: does var appear anywhere inside type?
  ///
  /// Prevents construction of infinite types (e.g., T = T -> T).
  /// Returns true if var is found in type's structure.
  /// Resolves type before checking.
  bool occursIn(const TypeVar& var, const Type& type) const;

  /// \brief Generate a fresh unique type variable ID.
  uint64_t freshId();

  /// \brief Check if a type variable is bound.
  bool isBound(const TypeVar& var) const;

  // =========================================================================
  // Impl table
  // =========================================================================

  /// \brief Register that a type implements an interface.
  ///
  /// \param ifaceName  The name of the interface being implemented.
  /// \param forType    The type that implements the interface.
  /// \param implNode   The AST node of the impl declaration.
  void registerImpl(zc::StringPtr ifaceName, const Type& forType, ast::NodeId implNode);

  /// \brief Look up the impl node for a type implementing an interface.
  ///
  /// Returns the NodeId of the impl declaration if found, or zc::none.
  zc::Maybe<ast::NodeId> lookupImpl(zc::StringPtr ifaceName, const Type& forType) const;

  /// \brief Check if a type implements a given interface.
  bool implements(const Type& ty, zc::StringPtr ifaceName) const;

  // =========================================================================
  // Error type
  // =========================================================================

  /// \brief Get the singleton error type instance.
  ///
  /// The error type is used as a placeholder when type checking fails,
  /// to allow compilation to continue and report additional errors.
  const ErrorType& errorType() const;

  // =========================================================================
  // Utility
  // =========================================================================

  /// \brief Clear all bindings and node type assignments.
  void clear();

  /// \brief Get the number of active type variable bindings.
  size_t size() const;

private:
  /// Build a unique lookup key for a type variable.
  static zc::String makeKey(const TypeVar& var);

  /// Find the root type variable ID in the union-find structure.
  /// Performs path compression as a side effect (hence mutable).
  uint64_t findRoot(uint64_t varId) const;

  /// Collect all free type variables in a type.
  /// A type variable is "free" if it is unbound and not in the exclusion set.
  void collectFreeTypeVars(const Type& ty, zc::HashSet<uint64_t>& freeVars,
                           const zc::HashSet<uint64_t>& exclude) const;

  /// Clone a type, replacing quantified type variables with fresh ones.
  /// Used by instantiate().
  zc::Own<Type> substituteType(const Type& ty, const zc::HashMap<uint64_t, TypeVar*>& subst) const;

  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace type
}  // namespace compiler
}  // namespace zomlang
