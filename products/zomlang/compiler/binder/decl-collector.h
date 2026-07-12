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
#include "zomlang/compiler/ast/node-id.h"
#include "zomlang/compiler/symbol/scope.h"

namespace zc {
class StringPtr;
}  // namespace zc

namespace zomlang {
namespace compiler {

namespace ast {
class Tree;
class BindingMetadata;
}  // namespace ast

namespace diagnostics {
class DiagnosticEngine;
}  // namespace diagnostics

namespace symbol {
class Symbol;
class SymbolTable;
enum class SymbolKind : int;
}  // namespace symbol

namespace binder {

class DefinitionIdentityMap;

/// \brief DeclCollector - Phase 1 of the Binder
///
/// Traverses the AST and collects all declarations into the symbol table.
/// This is the first phase of the two-phase binding process:
///   Phase 1:   Collection   - gather all declarations
///   Phase 1.5: Import Resolution - resolve imports
///   Phase 2:   Resolution   - resolve all name references
///
/// The collector creates appropriate Symbol instances for each declaration
/// and inserts them into the correct Scope. It also detects duplicate
/// declarations and reports them as errors.
class DeclCollector final {
public:
  /// \brief Construct a DeclCollector.
  /// \param symbols The symbol table to populate.
  /// \param scopes The scope manager for scope lifecycle.
  /// \param tree The AST tree to traverse.
  /// \param metadata Binding metadata to populate with symbol references.
  /// \param diags Diagnostic engine for error reporting.
  DeclCollector(symbol::SymbolTable& symbols, symbol::ScopeManager& scopes, const ast::Tree& tree,
                const DefinitionIdentityMap& identities, ast::BindingMetadata& metadata,
                diagnostics::DiagnosticEngine& diags) noexcept;

  ~DeclCollector() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(DeclCollector);

  /// \brief Run the collection phase over the entire AST.
  /// \return true if no fatal errors were encountered.
  bool collect();

private:
  struct Impl;
  zc::Own<Impl> impl;

  // ============================================================================
  // Visitor methods - one per interesting AST node kind
  // ============================================================================

  void visitNode(ast::NodeId node);
  void visitChildren(ast::NodeId node);

  // Top-level and module structure
  void visitSourceFile(ast::NodeId node);
  void visitStatementListItem(ast::NodeId node);

  // Declarations
  void visitFunctionDecl(ast::NodeId node);
  void visitClassDecl(ast::NodeId node);
  void visitStructDecl(ast::NodeId node);
  void visitInterfaceDecl(ast::NodeId node);
  void visitEnumDeclaration(ast::NodeId node);
  void visitAliasDecl(ast::NodeId node);
  void visitStandaloneImplDecl(ast::NodeId node);

  // Statements that introduce scopes or bindings
  void visitLetStmt(ast::NodeId node);
  void visitVariableDeclarator(ast::NodeId node, bool declarationIsMutable = false,
                               bool declarationIsConst = false);
  void visitBlockStmt(ast::NodeId node);
  void visitIfStmt(ast::NodeId node);
  void visitWhileStmt(ast::NodeId node);
  void visitForStmt(ast::NodeId node);
  void visitForInStatement(ast::NodeId node);
  void visitDoWhileStatement(ast::NodeId node);
  void visitMatchStmt(ast::NodeId node);
  void visitMatchArmStmt(ast::NodeId node);

  // Class/interface members
  void visitCallableDecl(ast::NodeId node, symbol::SymbolKind symbolKind);
  void visitFieldDecl(ast::NodeId node);
  void visitClassConstDecl(ast::NodeId node);

  // Function parameters
  void visitFunctionParameterDecl(ast::NodeId node);

  // Expressions that introduce scopes
  void visitLambdaExpression(ast::NodeId node);
  void visitFunctionExpression(ast::NodeId node);

  // Import/Export (deferred to Phase 1.5, but we still need to skip them gracefully)
  void visitImportDeclaration(ast::NodeId node);
  void visitExportDeclaration(ast::NodeId node);

  // ============================================================================
  // Scope management helpers
  // ============================================================================

  /// \brief Enter a new scope of the given kind.
  /// \param kind The kind of scope to create.
  /// \param name A name for the scope (e.g. function/class name).
  /// \return A reference to the newly created scope.
  symbol::Scope& enterScope(symbol::Scope::Kind kind, zc::StringPtr name);

  /// \brief Leave the current scope and restore the parent.
  void leaveScope();

  // ============================================================================
  // Symbol declaration helpers
  // ============================================================================

  /// \brief Declare a symbol in the current scope.
  ///
  /// Creates the appropriate symbol via SymbolTable, registers it in the
  /// current scope, records the binding metadata, and checks for duplicates.
  ///
  /// \param name The declaration name.
  /// \param declNode The AST node that declares this symbol.
  /// \param kind The expected symbol kind (for duplicate error messages).
  /// \return The created Symbol, or the existing one if a duplicate was
  ///         detected (in which case an error was also emitted).
  symbol::Symbol& declareSymbol(zc::StringPtr name, ast::NodeId declNode, symbol::SymbolKind kind);
  symbol::Symbol& declareSymbol(zc::StringPtr name, ast::NodeId declNode, ast::NodeId identityNode,
                                symbol::SymbolKind kind);

  /// \brief Check whether a name is already declared in the current scope.
  ///
  /// If a duplicate is found, emits the appropriate diagnostic.
  ///
  /// \param name The name to check.
  /// \param declNode The new declaration node (for error location).
  /// \param kind The kind of symbol being declared (for error message).
  /// \return true if a duplicate was found and reported.
  bool checkDuplicate(zc::StringPtr name, ast::NodeId declNode, symbol::SymbolKind kind);

  /// \brief Get the current scope from the scope manager.
  symbol::Scope& currentScope();

  /// \brief Extract the name string from a declaration node's IdentId field.
  zc::StringPtr declName(ast::NodeId node, uint32_t wordIndex);

  /// \brief Extract the name string from a SourceFile's StringId field.
  zc::StringPtr fileName(ast::NodeId node);

  /// \brief Record a symbol association in the binding metadata.
  void bindSymbol(ast::NodeId node, symbol::Symbol& sym);

  /// \brief Record a scope association in the binding metadata.
  void bindScope(ast::NodeId node, symbol::Scope& scope);
};

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
