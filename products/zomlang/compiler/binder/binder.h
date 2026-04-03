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
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/visitor.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"

namespace zomlang {
namespace compiler {

namespace ast {
class Node;
class SourceFile;
class Statement;
class Expression;
class Declaration;
}  // namespace ast

namespace symbol {
class Symbol;
class SymbolTable;
class Scope;
class ScopeManager;
}  // namespace symbol

namespace binder {

/// \brief Container flags for different AST node types
enum class ContainerFlags : uint32_t {
  None = 0,
  IsContainer = 1 << 0,             // Node is a container (class, interface, etc.)
  IsBlockScopedContainer = 1 << 1,  // Node is block-scoped (blocks, catch, etc.)
  IsControlFlowContainer = 1 << 2,  // Node controls flow (functions, etc.)
  IsFunctionLike = 1 << 3,          // Node is function-like
  IsFunctionExpression = 1 << 4,    // Node is function expression
  HasLocals = 1 << 5,               // Node has local symbols
  IsInterface = 1 << 6,             // Node is interface
  IsClassExpression = 1 << 7,       // Node is class expression
  IsThisContainer = 1 << 8,         // Node is 'this' container
  IsModuleContainer = 1 << 9,       // Node is module container
  IsPackageContainer = 1 << 10,     // Node is package container
};

/// \brief Bitwise operators for ContainerFlags
inline ContainerFlags operator|(ContainerFlags lhs, ContainerFlags rhs) {
  return static_cast<ContainerFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline ContainerFlags operator&(ContainerFlags lhs, ContainerFlags rhs) {
  return static_cast<ContainerFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline ContainerFlags operator^(ContainerFlags lhs, ContainerFlags rhs) {
  return static_cast<ContainerFlags>(static_cast<uint32_t>(lhs) ^ static_cast<uint32_t>(rhs));
}

inline ContainerFlags operator~(ContainerFlags flags) {
  return static_cast<ContainerFlags>(~static_cast<uint32_t>(flags));
}

inline ContainerFlags& operator|=(ContainerFlags& lhs, ContainerFlags rhs) {
  lhs = lhs | rhs;
  return lhs;
}

inline ContainerFlags& operator&=(ContainerFlags& lhs, ContainerFlags rhs) {
  lhs = lhs & rhs;
  return lhs;
}

inline ContainerFlags& operator^=(ContainerFlags& lhs, ContainerFlags rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

inline bool operator==(ContainerFlags lhs, ContainerFlags rhs) {
  return static_cast<uint32_t>(lhs) == static_cast<uint32_t>(rhs);
}

inline bool operator!=(ContainerFlags lhs, ContainerFlags rhs) { return !(lhs == rhs); }

inline bool hasFlag(ContainerFlags flags, ContainerFlags flag) { return (flags & flag) == flag; }

/// \brief Binding context flags for different contexts
enum class BindingContextFlags : uint32_t {
  None = 0,
  AwaitContext = 1 << 0,     // Inside async function or top-level await
  YieldContext = 1 << 1,     // Inside generator function
  StrictMode = 1 << 2,       // In strict mode
  ModuleContext = 1 << 3,    // In module context
  TopLevelContext = 1 << 4,  // At top level
};

/// \brief Bitwise operators for BindingContextFlags
inline BindingContextFlags operator|(BindingContextFlags lhs, BindingContextFlags rhs) {
  return static_cast<BindingContextFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline BindingContextFlags operator&(BindingContextFlags lhs, BindingContextFlags rhs) {
  return static_cast<BindingContextFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

inline BindingContextFlags operator^(BindingContextFlags lhs, BindingContextFlags rhs) {
  return static_cast<BindingContextFlags>(static_cast<uint32_t>(lhs) ^ static_cast<uint32_t>(rhs));
}

inline BindingContextFlags operator~(BindingContextFlags flags) {
  return static_cast<BindingContextFlags>(~static_cast<uint32_t>(flags));
}

inline BindingContextFlags& operator|=(BindingContextFlags& lhs, BindingContextFlags rhs) {
  lhs = lhs | rhs;
  return lhs;
}

inline BindingContextFlags& operator&=(BindingContextFlags& lhs, BindingContextFlags rhs) {
  lhs = lhs & rhs;
  return lhs;
}

inline BindingContextFlags& operator^=(BindingContextFlags& lhs, BindingContextFlags rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

inline bool operator==(BindingContextFlags lhs, BindingContextFlags rhs) {
  return static_cast<uint32_t>(lhs) == static_cast<uint32_t>(rhs);
}

inline bool operator!=(BindingContextFlags lhs, BindingContextFlags rhs) { return !(lhs == rhs); }

inline bool hasFlag(BindingContextFlags flags, BindingContextFlags flag) {
  return (flags & flag) == flag;
}

/// \brief Binding context for the binder
struct BindingContext {
  zc::Maybe<const ast::Node&> container;
  zc::Maybe<const ast::Node&> thisContainer;
  zc::Maybe<const ast::Node&> blockScopeContainer;
  zc::Maybe<symbol::Scope&> currentScope;
  bool inAssignmentPattern = false;
  uint32_t symbolCount = 0;
  BindingContextFlags flags = BindingContextFlags::None;
};

/// \brief Main Binder class for symbol binding and scope management
///
/// The Binder class is responsible for:
/// 1. Creating symbols from AST declarations
/// 2. Managing lexical scopes and containers
/// 3. Binding AST nodes to their corresponding symbols
/// 4. Handling symbol conflicts and error reporting
/// 5. Managing symbol tables and scope hierarchies
class Binder : public ast::Visitor {
public:
  /// \brief Constructor
  /// \param symbolTable Symbol table for managing symbols
  /// \param diagnosticEngine Diagnostic engine for error reporting
  explicit Binder(symbol::SymbolTable& symbolTable,
                  diagnostics::DiagnosticEngine& diagnosticEngine) noexcept;

  /// \brief Destructor
  ~Binder() noexcept(false);

  // Disable copy and move
  ZC_DISALLOW_COPY_AND_MOVE(Binder);

  /// \brief Main binding entry point
  /// \param sourceFile Source file to bind
  void bindSourceFile(ast::SourceFile& sourceFile);

  /// \brief Bind a single AST node
  /// \param node AST node to bind
  void bind(ast::Node& node);

  /// \brief Get symbol associated with AST node
  /// \param node AST node
  /// \return Associated symbol if exists
  zc::Maybe<const symbol::Symbol&> getSymbol(const ast::Node& node) const;

  /// \brief Add a declaration node to a symbol (for declarations only)
  void addDeclarationToSymbol(symbol::Symbol& symbol, ast::Node& node, symbol::SymbolFlags flags);

  /// \brief Get current binding context
  const BindingContext& getContext() const;

  /// \brief Visitor interface implementation
  void visit(const ast::Node& node) override;
  void visit(const ast::Statement& statement) override;
  void visit(const ast::Expression& expression) override;
  void visit(const ast::TypeNode& type) override;
  void visit(const ast::TokenNode& token) override;

  // Interface node visitors (abstract base classes)
  void visit(const ast::Declaration& node) override;
  void visit(const ast::NamedDeclaration& node) override;
  void visit(const ast::Pattern& node) override;
  void visit(const ast::PrimaryPattern& node) override;
  void visit(const ast::BindingPattern& node) override;
  void visit(const ast::ClassElement& node) override;
  void visit(const ast::ObjectLiteralElement& node) override;
  void visit(const ast::InterfaceElement& node) override;

  // Declaration visitors
  void visit(const ast::VariableDeclaration& node) override;
  void visit(const ast::VariableDeclarationList& node) override;
  void visit(const ast::VariableStatement& node) override;
  void visit(const ast::FunctionDeclaration& node) override;
  void visit(const ast::ClassDeclaration& node) override;
  void visit(const ast::InterfaceDeclaration& node) override;
  void visit(const ast::StructDeclaration& node) override;
  void visit(const ast::EnumDeclaration& node) override;
  void visit(const ast::AliasDeclaration& node) override;
  void visit(const ast::MethodDeclaration& node) override;
  void visit(const ast::GetAccessor& node) override;
  void visit(const ast::SetAccessor& node) override;
  void visit(const ast::InitDeclaration& node) override;
  void visit(const ast::DeinitDeclaration& node) override;
  void visit(const ast::ParameterDeclaration& node) override;
  void visit(const ast::PropertyDeclaration& node) override;
  void visit(const ast::SemicolonClassElement& node) override;
  void visit(const ast::SemicolonInterfaceElement& node) override;
  void visit(const ast::MissingDeclaration& node) override;

  // Statement visitors
  void visit(const ast::BlockStatement& node) override;
  void visit(const ast::ExpressionStatement& node) override;
  void visit(const ast::IfStatement& node) override;
  void visit(const ast::WhileStatement& node) override;
  void visit(const ast::ForStatement& node) override;
  void visit(const ast::ForInStatement& node) override;
  void visit(const ast::ReturnStatement& node) override;
  void visit(const ast::MatchStatement& node) override;
  void visit(const ast::MatchClause& node) override;
  void visit(const ast::DefaultClause& node) override;
  void visit(const ast::IterationStatement& node) override;
  void visit(const ast::DeclarationStatement& node) override;

  // Expression visitors
  void visit(const ast::Identifier& node) override;
  void visit(const ast::BinaryExpression& node) override;
  void visit(const ast::CallExpression& node) override;
  void visit(const ast::MemberExpression& node) override;
  void visit(const ast::SpreadElement& node) override;

  // Module visitors
  void visit(const ast::SourceFile& sourceFile) override;
  void visit(const ast::ModuleDeclaration& moduleDecl) override;
  void visit(const ast::ImportDeclaration& importDecl) override;
  void visit(const ast::ImportSpecifier& importSpecifier) override;
  void visit(const ast::ExportDeclaration& exportDecl) override;
  void visit(const ast::ExportSpecifier& exportSpecifier) override;

  // Type visitors
  void visit(const ast::TypeReferenceNode& typeRef) override;
  void visit(const ast::ArrayTypeNode& arrayType) override;
  void visit(const ast::FunctionTypeNode& functionType) override;
  void visit(const ast::UnionTypeNode& unionType) override;
  void visit(const ast::IntersectionTypeNode& intersectionType) override;
  void visit(const ast::ParenthesizedTypeNode& parenType) override;
  void visit(const ast::PredefinedTypeNode& predefinedType) override;
  void visit(const ast::ObjectTypeNode& objectType) override;
  void visit(const ast::TupleTypeNode& tupleType) override;
  void visit(const ast::ReturnTypeNode& returnType) override;
  void visit(const ast::OptionalTypeNode& optionalType) override;
  void visit(const ast::TypeQueryNode& typeQuery) override;
  void visit(const ast::NamedTupleElement& node) override;

  // Predefined type visitors
  void visit(const ast::BoolTypeNode& node) override;
  void visit(const ast::I8TypeNode& node) override;
  void visit(const ast::I16TypeNode& node) override;
  void visit(const ast::I32TypeNode& node) override;
  void visit(const ast::I64TypeNode& node) override;
  void visit(const ast::U8TypeNode& node) override;
  void visit(const ast::U16TypeNode& node) override;
  void visit(const ast::U32TypeNode& node) override;
  void visit(const ast::U64TypeNode& node) override;
  void visit(const ast::F32TypeNode& node) override;
  void visit(const ast::F64TypeNode& node) override;
  void visit(const ast::StrTypeNode& node) override;
  void visit(const ast::UnitTypeNode& node) override;
  void visit(const ast::NullTypeNode& node) override;

  // Binding pattern visitors
  void visit(const ast::ArrayBindingPattern& node) override;
  void visit(const ast::ObjectBindingPattern& node) override;

  // Expression visitors
  void visit(const ast::ThisExpression& node) override;
  void visit(const ast::SuperExpression& node) override;

  // Other required visitors (default implementations)
  void visit(const ast::TypeParameterDeclaration& node) override;
  void visit(const ast::BindingElement& node) override;
  void visit(const ast::EnumMember& node) override;
  void visit(const ast::ErrorDeclaration& node) override;
  void visit(const ast::EmptyStatement& node) override;
  void visit(const ast::LabeledStatement& node) override;
  void visit(const ast::PatternProperty& node) override;
  void visit(const ast::BreakStatement& node) override;
  void visit(const ast::ContinueStatement& node) override;
  void visit(const ast::DebuggerStatement& node) override;
  void visit(const ast::UnaryExpression& node) override;
  void visit(const ast::UpdateExpression& node) override;
  void visit(const ast::PrefixUnaryExpression& node) override;
  void visit(const ast::PostfixUnaryExpression& node) override;
  void visit(const ast::LeftHandSideExpression& node) override;
  void visit(const ast::PrimaryExpression& node) override;
  void visit(const ast::PropertyAccessExpression& node) override;
  void visit(const ast::ElementAccessExpression& node) override;
  void visit(const ast::NewExpression& node) override;
  void visit(const ast::ParenthesizedExpression& node) override;
  void visit(const ast::ConditionalExpression& node) override;
  void visit(const ast::LiteralExpression& node) override;
  void visit(const ast::StringLiteral& node) override;
  void visit(const ast::IntegerLiteral& node) override;
  void visit(const ast::FloatLiteral& node) override;
  void visit(const ast::BigIntLiteral& node) override;
  void visit(const ast::BooleanLiteral& node) override;
  void visit(const ast::NullLiteral& node) override;
  void visit(const ast::TemplateSpan& node) override;
  void visit(const ast::TemplateLiteralExpression& node) override;
  void visit(const ast::CastExpression& node) override;
  void visit(const ast::AsExpression& node) override;
  void visit(const ast::ForcedAsExpression& node) override;
  void visit(const ast::ConditionalAsExpression& node) override;
  void visit(const ast::NonNullExpression& node) override;
  void visit(const ast::ExpressionWithTypeArguments& node) override;
  void visit(const ast::VoidExpression& node) override;
  void visit(const ast::TypeOfExpression& node) override;
  void visit(const ast::AwaitExpression& node) override;
  void visit(const ast::FunctionExpression& node) override;
  void visit(const ast::CaptureElement& node) override;
  void visit(const ast::ArrayLiteralExpression& node) override;
  void visit(const ast::ObjectLiteralExpression& node) override;
  void visit(const ast::PropertyAssignment& node) override;
  void visit(const ast::ShorthandPropertyAssignment& node) override;
  void visit(const ast::SpreadAssignment& node) override;
  void visit(const ast::ModulePath& modulePath) override;
  void visit(const ast::InterfaceBody& node) override;
  void visit(const ast::StructBody& node) override;
  void visit(const ast::ErrorBody& node) override;
  void visit(const ast::EnumBody& node) override;

  // Pattern visitors
  void visit(const ast::WildcardPattern& node) override;
  void visit(const ast::IdentifierPattern& node) override;
  void visit(const ast::TuplePattern& node) override;
  void visit(const ast::StructurePattern& node) override;
  void visit(const ast::ArrayPattern& node) override;
  void visit(const ast::IsPattern& node) override;
  void visit(const ast::ExpressionPattern& node) override;
  void visit(const ast::EnumPattern& node) override;

  // Signature visitors
  void visit(const ast::PropertySignature& node) override;
  void visit(const ast::MethodSignature& node) override;

  // Body visitors
  void visit(const ast::ClassBody& node) override;
  void visit(const ast::HeritageClause& node) override;

protected:
  /// \brief Symbol creation methods
  symbol::Symbol& declareSymbol(symbol::SymbolTable& symbolTable, const ast::Declaration& node,
                                symbol::SymbolFlags includes, symbol::SymbolFlags excludes);

  symbol::Symbol& declareModuleMember(const ast::Node& node, symbol::SymbolFlags symbolFlags,
                                      symbol::SymbolFlags symbolExcludes);

  symbol::Symbol& declareClassMember(const ast::Node& node, symbol::SymbolFlags symbolFlags,
                                     symbol::SymbolFlags symbolExcludes);

  symbol::Symbol& declareSourceFileMember(const ast::Node& node, symbol::SymbolFlags symbolFlags,
                                          symbol::SymbolFlags symbolExcludes);

  /// \brief Scope and container management
  void enterContainer(const ast::Node& node, ContainerFlags flags);
  void exitContainer();

  void enterScope(symbol::Scope& scope);
  void exitScope();

  ContainerFlags getContainerFlags(const ast::Node& node) const;
  bool isContainer(const ast::Node& node) const;
  bool isBlockScopedContainer(const ast::Node& node) const;

  /// \brief Symbol lookup and resolution
  zc::Maybe<const symbol::Symbol&> lookupSymbol(zc::StringPtr name) const;
  zc::Maybe<symbol::Symbol&> lookupSymbolInScope(zc::StringPtr name, symbol::Scope& scope) const;

  /// \brief Name resolution
  zc::StringPtr getDeclarationName(const ast::Declaration& node) const;
  zc::StringPtr getDisplayName(const ast::Declaration& node) const;

  /// \brief Error handling
  void checkNoConflict(const ast::Node& node, zc::StringPtr name);

  /// \brief Binding helpers for specific node types
  void bindDeclaration(const ast::Declaration& declaration);
  void bindVariableDeclaration(const ast::VariableDeclaration& varDecl);
  void bindFunctionDeclaration(const ast::FunctionDeclaration& funcDecl);
  void bindClassDeclaration(const ast::ClassDeclaration& classDecl);
  void bindInterfaceDeclaration(const ast::InterfaceDeclaration& interfaceDecl);

  void bindImportDeclaration(const ast::ImportDeclaration& importDecl);
  void bindExportDeclaration(const ast::ExportDeclaration& exportDecl);

  /// \brief Container-specific binding
  void bindContainerMembers(const ast::Node& container);
  void bindBlockStatement(const ast::Node& block);
  void bindFunctionBody(const ast::Node& function);

  /// \brief Check contextual identifier usage (await/yield in wrong contexts)
  void checkContextualIdentifier(const ast::Identifier& node);

  /// \brief Helper function to create symbol by flags
  symbol::Symbol& createSymbolByFlags(symbol::SymbolTable& symbolTable,
                                      const ast::Declaration& node, symbol::SymbolFlags includes,
                                      symbol::SymbolFlags excludes);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

/// \brief Utility functions for binding operations
namespace utils {

/// \brief Check if node creates a new scope
bool createsScope(const ast::Node& node);

/// \brief Check if node is a declaration
bool isDeclaration(const ast::Node& node);

/// \brief Check if node is a named declaration
bool isNamedDeclaration(const ast::Node& node);

/// \brief Check if node is exportable
bool isExportable(const ast::Node& node);

/// \brief Get symbol flags for node
symbol::SymbolFlags getSymbolFlags(const ast::Node& node);

/// \brief Get symbol excludes for node
symbol::SymbolFlags getSymbolExcludes(const ast::Node& node);

/// \brief Create scope for container node
zc::Own<symbol::Scope> createScopeForContainer(const ast::Node& container,
                                               zc::Maybe<const symbol::Scope&> parent);

}  // namespace utils

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
