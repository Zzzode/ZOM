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
#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/node-id.h"

namespace zomlang {
namespace compiler {

namespace ast {
class Tree;
struct Node;
class BindingMetadata;
}  // namespace ast

namespace diagnostics {
class DiagnosticEngine;
}  // namespace diagnostics

namespace symbol {
class SymbolTable;
class Scope;
class Symbol;
class FunctionSymbol;
class ClassSymbol;
class InterfaceSymbol;
class VariableSymbol;
}  // namespace symbol

namespace type {
class Type;
class TypeEnv;
}  // namespace type

namespace checker {

/// \brief DeclSignatureComputer - Phase A of the type checker.
///
/// Computes type signatures for all declarations in the compilation unit.
/// This is the first phase of the two-phase type checking process as
/// specified in RFC 0005:
///
///   Phase A: Signature Computation - compute type signatures for all
///            declarations (functions, classes, interfaces, variables)
///   Phase B: Body Checking - check function bodies, expressions, statements
///
/// Phase A resolves all type annotations in declarations and produces
/// a complete type signature for each symbol. This enables Phase B to
/// perform body checking with full type information available for all
/// symbols, including those declared later in the source (mutual recursion).
///
/// The computer walks the AST and for each declaration:
/// 1. Extracts the type annotation from the AST payload
/// 2. Resolves the type expression to a concrete Type object
/// 3. Stores the result in the TypeEnv keyed by the declaration's NodeId
///
/// Type resolution supports all 15 type forms defined in RFC 0005:
/// primitives, functions, tuples, objects, arrays, named, type variables,
/// interfaces, unions, intersections, references, raw pointers, existentials,
/// associated types, and error types.
class DeclSignatureComputer final {
public:
  /// \brief Construct a DeclSignatureComputer.
  ///
  /// \param typeEnv   The type environment for storing computed signatures.
  /// \param symbols   The symbol table (populated by the binder).
  /// \param tree      The immutable AST tree to process.
  /// \param metadata  Binding metadata from the binder (symbol/scope bindings).
  /// \param diags     Diagnostic engine for error reporting.
  DeclSignatureComputer(type::TypeEnv& typeEnv, symbol::SymbolTable& symbols, const ast::Tree& tree,
                        const ast::BindingMetadata& metadata,
                        diagnostics::DiagnosticEngine& diags) noexcept;

  ~DeclSignatureComputer() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(DeclSignatureComputer);

  /// \brief Run Phase A: compute type signatures for all declarations.
  ///
  /// Walks the entire AST, resolves type annotations on every declaration,
  /// and stores the resulting Type objects in the TypeEnv.
  ///
  /// \return true if no fatal errors were produced.
  bool computeSignatures();

private:
  struct Impl;
  zc::Own<Impl> impl;

  // ==========================================================================
  // Declaration signature computation
  // ==========================================================================

  /// \brief Compute the type signature for a function declaration.
  ///
  /// Resolves parameter types and return type, producing a FunctionType.
  void computeFunctionSignature(symbol::FunctionSymbol& fn, ast::NodeId fnDecl);

  /// \brief Compute the type signature for a class declaration.
  ///
  /// Produces a NamedType referencing the class symbol.
  void computeClassSignature(symbol::ClassSymbol& cls, ast::NodeId classDecl);

  /// \brief Compute the type signature for an interface declaration.
  ///
  /// Produces a NamedType referencing the interface symbol.
  void computeInterfaceSignature(symbol::InterfaceSymbol& iface, ast::NodeId ifaceDecl);

  /// \brief Compute the type signature for a method declaration.
  ///
  /// Resolves parameter types and return type, producing a FunctionType.
  void computeMethodSignature(ast::NodeId methodDecl);

  /// \brief Compute the type signature for a variable declaration.
  ///
  /// Resolves the variable's type annotation.
  void computeVariableSignature(symbol::VariableSymbol& var, ast::NodeId varDecl);

  // ==========================================================================
  // Type expression resolution
  // ==========================================================================

  /// \brief Resolve a type expression AST node to a concrete Type.
  ///
  /// Dispatches to the appropriate resolver based on the node's SyntaxKind.
  /// Returns an ErrorType if the node kind is not recognized or resolution
  /// fails.
  zc::Own<type::Type> resolveTypeExpr(ast::NodeId typeExpr);

  /// \brief Resolve a predefined (primitive) type expression.
  zc::Own<type::Type> resolvePredefinedType(const ast::Node& node);

  /// \brief Resolve a named type reference (e.g., MyClass, module::Trait).
  zc::Own<type::Type> resolveNamedType(const ast::Node& node);

  /// \brief Resolve the target type of a type alias symbol with cycle detection.
  zc::Own<type::Type> resolveTypeAliasTarget(symbol::Symbol& symbol, ast::NodeId useSite);

  /// \brief Resolve a function type expression (e.g., fn(i32) -> bool).
  zc::Own<type::Type> resolveFunctionType(const ast::Node& node);

  /// \brief Resolve a tuple type expression (e.g., (i32, str)).
  zc::Own<type::Type> resolveTupleType(const ast::Node& node);

  /// \brief Resolve an object type expression (e.g., { x: i32, y: i32 }).
  zc::Own<type::Type> resolveObjectType(const ast::Node& node);

  /// \brief Resolve an array type expression (e.g., i32[]).
  zc::Own<type::Type> resolveArrayType(const ast::Node& node);

  /// \brief Resolve a union type expression (e.g., i32 | str).
  zc::Own<type::Type> resolveUnionType(const ast::Node& node);

  /// \brief Resolve an intersection type expression (e.g., Drawable & Clickable).
  zc::Own<type::Type> resolveIntersectionType(const ast::Node& node);

  /// \brief Resolve a reference type (e.g., &T, &mut T).
  zc::Own<type::Type> resolveReferenceType(ast::NodeId typeExpr);

  /// \brief Resolve a raw pointer type (e.g., *const T, *mut T).
  zc::Own<type::Type> resolveRawPointerType(ast::NodeId typeExpr);

  /// \brief Resolve a dynamic/existential type (e.g., dyn Drawable).
  zc::Own<type::Type> resolveDynType(const ast::Node& node);

  /// \brief Resolve a fully qualified associated type projection.
  zc::Own<type::Type> resolveAssociatedTypeProjection(const ast::Node& node);

  /// \brief Resolve an optional type (e.g., T?).
  zc::Own<type::Type> resolveOptionalType(const ast::Node& node);

  /// \brief Find the interface declaration node for a resolved interface name.
  ast::NodeId findInterfaceDecl(zc::StringPtr name) const;

  /// \brief Return true if an interface or its superinterfaces declare an associated type.
  bool interfaceDeclaresAssociatedType(zc::StringPtr ifaceName, zc::StringPtr assocName,
                                       zc::HashSet<zc::StringPtr>& activeIfaces) const;

  /// \brief Emit object-safety diagnostics for a dyn interface head.
  void checkDynObjectSafety(ast::NodeId objectSafetyDiagExpr, ast::NodeId assocBindingsId,
                            zc::StringPtr ifaceName);

  /// \brief Check whether an interface and its superinterfaces are object-safe.
  bool isDynObjectSafe(ast::NodeId objectSafetyDiagExpr, ast::NodeId assocBindingsId,
                       zc::StringPtr ifaceName, zc::StringPtr& failingIface,
                       zc::HashSet<zc::StringPtr>& activeIfaces, bool emitDirectDiagnostics);

  // ==========================================================================
  // Helpers
  // ==========================================================================

  /// \brief Extract the name string from a ModulePath node.
  zc::StringPtr resolvePathName(ast::NodeId pathNode) const;

  /// \brief Get the current scope from the symbol table's scope manager.
  const symbol::Scope& currentScope();

  /// \brief Look up a symbol by name in the current scope chain.
  zc::Maybe<symbol::Symbol&> lookupSymbol(zc::StringPtr name);
};

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
