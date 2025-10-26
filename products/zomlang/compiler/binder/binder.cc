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

#include "zomlang/compiler/binder/binder.h"

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/cast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/ast/utilities.h"
#include "zomlang/compiler/ast/visitor.h"
#include "zomlang/compiler/binder/utilities.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/lexer/utils.h"
#include "zomlang/compiler/symbol/scope.h"
#include "zomlang/compiler/symbol/symbol-flags.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"

namespace zomlang {
namespace compiler {
namespace binder {

using diagnostics::DiagID;

// Implementation details for Binder class
struct Binder::Impl {
  symbol::SymbolTable& symbolTable;
  diagnostics::DiagnosticEngine& diagEng;

  // Current binding context
  BindingContext context;

  // Container stack for tracking nested containers
  zc::Vector<zc::Maybe<const ast::Node&>> containerStack;

  // Scope stack for tracking nested scopes
  zc::Vector<zc::Maybe<symbol::Scope&>> scopeStack;

  // Current symbol counter for unique IDs
  uint32_t nextSymbolId = 1;

  explicit Impl(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagEng) noexcept
      : symbolTable(symbolTable), diagEng(diagEng) {
    // Initialize with global scope using mutable version
    auto& scopeManager = symbolTable.getScopeManager();
    auto globalScope = scopeManager.getGlobalScopeMutable();
    ZC_IF_SOME(scope, globalScope) {
      scopeStack.add(scope);
      context.currentScope = scope;
    }
  }

  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  // Helper methods for symbol creation
  zc::String getDeclarationName(const ast::Node& node) const;
  void checkNoConflict(const ast::Node& node, zc::StringPtr name);
};

Binder::Binder(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagEng) noexcept
    : impl(zc::heap<Impl>(symbolTable, diagEng)) {}

Binder::~Binder() noexcept(false) = default;

void Binder::bindSourceFile(ast::SourceFile& sourceFile) {
  // Reset binding context for new source file
  impl->context = BindingContext{};
  impl->containerStack.clear();
  impl->scopeStack.clear();

  // Initialize with global scope
  auto& scopeManager = impl->symbolTable.getScopeManager();
  ZC_IF_SOME(globalScope, scopeManager.getGlobalScopeMutable()) {
    impl->scopeStack.add(globalScope);
    impl->context.currentScope = globalScope;
  }
  else { ZC_FAIL_REQUIRE("Global scope not found"); }

  // Visit the source file to bind all its contents
  sourceFile.accept(*this);
}

void Binder::bind(ast::Node& node) { node.accept(*this); }

void Binder::addDeclarationToSymbol(symbol::Symbol& symbol, ast::Node& node,
                                    symbol::SymbolFlags flags) {
  // Add flags to symbol
  symbol.addFlag(flags);

  // For nodes that implement Declaration interface, we need to cast to the specific type first
  // then access the Declaration interface through that type
  switch (node.getKind()) {
    case ast::SyntaxKind::BindingElement: {
      auto& bindingElement = ast::cast<ast::BindingElement&>(node);
      bindingElement.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::VariableDeclaration: {
      auto& varDecl = ast::cast<ast::VariableDeclaration&>(node);
      varDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::FunctionDeclaration: {
      auto& funcDecl = ast::cast<ast::FunctionDeclaration&>(node);
      funcDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::ClassDeclaration: {
      auto& classDecl = ast::cast<ast::ClassDeclaration&>(node);
      classDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::InterfaceDeclaration: {
      auto& interfaceDecl = ast::cast<ast::InterfaceDeclaration&>(node);
      interfaceDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::StructDeclaration: {
      auto& structDecl = ast::cast<ast::StructDeclaration&>(node);
      structDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::EnumDeclaration: {
      auto& enumDecl = ast::cast<ast::EnumDeclaration&>(node);
      enumDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::ErrorDeclaration: {
      auto& errorDecl = ast::cast<ast::ErrorDeclaration&>(node);
      errorDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::AliasDeclaration: {
      auto& aliasDecl = ast::cast<ast::AliasDeclaration&>(node);
      aliasDecl.setSymbol(symbol);
      break;
    }
    case ast::SyntaxKind::TypeParameterDeclaration: {
      auto& typeParamDecl = ast::cast<ast::TypeParameterDeclaration&>(node);
      typeParamDecl.setSymbol(symbol);
      break;
    }
    default:
      // For other declaration types that might be added in the future
      break;
  }
  symbol.addDeclarationNode(node);
}

const BindingContext& Binder::getContext() const { return impl->context; }

// Visitor implementations - base cases
void Binder::visit(const ast::Node& node) {
  // Default implementation - do nothing for base node
}

void Binder::visit(const ast::Statement& statement) {
  // Default implementation for statements
}

void Binder::visit(const ast::Expression& expression) {
  // Default implementation for expressions
}

void Binder::visit(const ast::TypeNode& type) {
  // Default implementation for types
}

void Binder::visit(const ast::TokenNode& token) {
  // Default implementation for token nodes - no binding needed
}

// Declaration visitors
void Binder::visit(const ast::VariableDeclaration& node) { bindVariableDeclaration(node); }

void Binder::visit(const ast::VariableDeclarationList& node) {
  // Visit all variable declarations in the list
  for (const auto& binding : node.getBindings()) { binding.accept(*this); }
}

void Binder::visit(const ast::VariableStatement& node) {
  // Visit the variable declaration list
  node.getDeclarations().accept(*this);
}

void Binder::visit(const ast::FunctionDeclaration& node) { bindFunctionDeclaration(node); }

void Binder::visit(const ast::ClassDeclaration& node) { bindClassDeclaration(node); }

void Binder::visit(const ast::InterfaceDeclaration& node) { bindInterfaceDeclaration(node); }

void Binder::visit(const ast::StructDeclaration& node) {
  // Bind struct declaration - create symbol and process members
  // TODO: Implement struct declaration binding
}

void Binder::visit(const ast::EnumDeclaration& node) {
  // Bind enum declaration - create symbol and process members
  // TODO: Implement enum declaration binding
}

void Binder::visit(const ast::AliasDeclaration& node) {
  // Bind alias declaration - create symbol for type alias
  // TODO: Implement alias declaration binding
}

// Statement visitors
void Binder::visit(const ast::BlockStatement& node) { bindBlockStatement(node); }

void Binder::visit(const ast::ExpressionStatement& node) {
  // Visit the expression
  node.getExpression().accept(*this);
}

void Binder::visit(const ast::IfStatement& node) {
  // Visit condition and branches
  node.getCondition().accept(*this);
  node.getThenStatement().accept(*this);

  // Visit else statement if it exists
  ZC_IF_SOME(elseStmt, node.getElseStatement()) { elseStmt.accept(*this); }
}

void Binder::visit(const ast::WhileStatement& node) {
  // Visit condition and body
  node.getCondition().accept(*this);
  node.getBody().accept(*this);
}

void Binder::visit(const ast::ForStatement& node) {
  // Visit initialization if present
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }

  // Visit condition if present
  ZC_IF_SOME(condition, node.getCondition()) { condition.accept(*this); }

  // Visit update if present
  ZC_IF_SOME(update, node.getUpdate()) { update.accept(*this); }

  // Visit body
  node.getBody().accept(*this);
}

void Binder::visit(const ast::ReturnStatement& node) {
  // Visit return expression if present
  ZC_IF_SOME(expr, node.getExpression()) { expr.accept(*this); }
}

void Binder::visit(const ast::MatchStatement& node) {
  // Visit discriminant
  node.getDiscriminant().accept(*this);

  // Visit all clauses
  for (const auto& clause : node.getClauses()) { clause.accept(*this); }
}

void Binder::visit(const ast::MatchClause& node) {
  // Visit pattern
  node.getPattern().accept(*this);

  // Visit guard if present
  ZC_IF_SOME(guard, node.getGuard()) { guard.accept(*this); }

  // Visit body
  node.getBody().accept(*this);
}

void Binder::visit(const ast::DefaultClause& node) {
  // Visit all statements
  for (const auto& stmt : node.getStatements()) { stmt.accept(*this); }
}

// Expression visitors
void Binder::visit(const ast::Identifier& node) {
  // Check contextual identifier usage (await/yield in wrong contexts)
  checkContextualIdentifier(node);
}

void Binder::visit(const ast::BinaryExpression& node) {
  // Visit left and right operands
  node.getLeft().accept(*this);
  node.getRight().accept(*this);
}

void Binder::visit(const ast::CallExpression& node) {
  // Visit callee and arguments
  node.getCallee().accept(*this);

  for (auto& arg : node.getArguments()) { arg.accept(*this); }
}

void Binder::visit(const ast::MemberExpression& node) {
  // MemberExpression is a base class, no specific members to visit
  // Derived classes like PropertyAccessExpression and ElementAccessExpression handle specifics
}

// Module visitors
void Binder::visit(const ast::SourceFile& sourceFile) {
  // Visit all top-level declarations
  for (auto& stmt : sourceFile.getStatements()) { stmt.accept(*this); }
}

void Binder::visit(const ast::ImportDeclaration& importDecl) { bindImportDeclaration(importDecl); }

void Binder::visit(const ast::ExportDeclaration& exportDecl) { bindExportDeclaration(exportDecl); }

// Type visitors
void Binder::visit(const ast::TypeReferenceNode& typeRef) {
  // Process type name - getName() returns StringPtr, not a visitable node
  // TODO: Look up type symbol in symbol table using the name
  // For now, just record that we've seen this type reference
}

void Binder::visit(const ast::ArrayTypeNode& arrayType) {
  // Visit element type
  arrayType.getElementType().accept(*this);
}

void Binder::visit(const ast::FunctionTypeNode& functionType) {
  // Visit parameter types and return type
  for (auto& param : functionType.getParameters()) { param.accept(*this); }

  // Visit return type directly - getReturnType() returns const ReturnTypeNode&
  functionType.getReturnType().accept(*this);
}

// Default implementations for other required visitors
void Binder::visit(const ast::TypeParameterDeclaration& node) {}
void Binder::visit(const ast::BindingElement& node) {}
void Binder::visit(const ast::ErrorDeclaration& node) {}
void Binder::visit(const ast::EmptyStatement& node) {}
void Binder::visit(const ast::BreakStatement& node) {}
void Binder::visit(const ast::ContinueStatement& node) {}
void Binder::visit(const ast::DebuggerStatement& node) {}
void Binder::visit(const ast::UnaryExpression& node) {
  // UnaryExpression is a base class, no specific members to visit
  // Derived classes like PrefixUnaryExpression and PostfixUnaryExpression handle specifics
}
void Binder::visit(const ast::UpdateExpression& node) {
  // UpdateExpression is a base class, no specific members to visit
  // Derived classes like PrefixUnaryExpression and PostfixUnaryExpression handle specifics
}
void Binder::visit(const ast::PrefixUnaryExpression& node) { node.getOperand().accept(*this); }
void Binder::visit(const ast::PostfixUnaryExpression& node) { node.getOperand().accept(*this); }
void Binder::visit(const ast::LeftHandSideExpression& node) {
  // LeftHandSideExpression is a base class, no specific members to visit
}
void Binder::visit(const ast::PrimaryExpression& node) {
  // PrimaryExpression is a base class, no specific members to visit
}
void Binder::visit(const ast::PropertyAccessExpression& node) {
  node.getExpression().accept(*this);
  // Property name is an identifier, no need to visit
}
void Binder::visit(const ast::ElementAccessExpression& node) {
  node.getExpression().accept(*this);
  node.getIndex().accept(*this);
}
void Binder::visit(const ast::NewExpression& node) {
  node.getCallee().accept(*this);
  for (auto& arg : node.getArguments()) { arg.accept(*this); }
}
void Binder::visit(const ast::ParenthesizedExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::ConditionalExpression& node) {
  node.getTest().accept(*this);
  node.getConsequent().accept(*this);
  node.getAlternate().accept(*this);
}
void Binder::visit(const ast::LiteralExpression& node) {
  // LiteralExpression is a base class, no specific members to visit
}
void Binder::visit(const ast::StringLiteral& node) {}
void Binder::visit(const ast::IntegerLiteral& node) {}
void Binder::visit(const ast::FloatLiteral& node) {}
void Binder::visit(const ast::BooleanLiteral& node) {}
void Binder::visit(const ast::NullLiteral& node) {}
void Binder::visit(const ast::CastExpression& node) {
  node.getExpression().accept(*this);
  node.getTargetType().accept(*this);
}
void Binder::visit(const ast::AsExpression& node) {
  node.getExpression().accept(*this);
  node.getTargetType().accept(*this);
}
void Binder::visit(const ast::ForcedAsExpression& node) {
  node.getExpression().accept(*this);
  node.getTargetType().accept(*this);
}
void Binder::visit(const ast::ConditionalAsExpression& node) {
  node.getExpression().accept(*this);
  node.getTargetType().accept(*this);
}
void Binder::visit(const ast::VoidExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::TypeOfExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::AwaitExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::FunctionExpression& node) {
  // Function expressions are anonymous functions that need special handling
  // They create their own scope and bind parameters and body

  // Visit type parameters if present
  for (const auto& typeParam : node.getTypeParameters()) { typeParam.accept(*this); }

  // Visit parameters
  for (const auto& param : node.getParameters()) { param.accept(*this); }

  // Visit return type if present
  ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }

  // Visit function body
  node.getBody().accept(*this);
}
void Binder::visit(const ast::ArrayLiteralExpression& node) {
  for (auto& element : node.getElements()) { element.accept(*this); }
}
void Binder::visit(const ast::ObjectLiteralExpression& node) {
  // Visit all property assignments
  for (const auto& property : node.getProperties()) { property.accept(*this); }
}

void Binder::visit(const ast::ModulePath& node) {
  // Module paths are just identifiers, no special binding needed
}

void Binder::visit(const ast::MethodDeclaration& node) {
  // Bind method declaration - similar to function declaration but within class/interface context
  // TODO: Implement method declaration binding
}

void Binder::visit(const ast::ConstructorDeclaration& node) {
  // Bind constructor declaration - special method for class instantiation
  // TODO: Implement constructor declaration binding
}

void Binder::visit(const ast::ParameterDeclaration& node) {
  // Bind parameter declaration - create symbol for parameter
  // TODO: Implement parameter declaration binding
}

void Binder::visit(const ast::PropertyDeclaration& node) {
  // Bind property declaration - create symbol for class/interface property
  // TODO: Implement property declaration binding
}

void Binder::visit(const ast::MissingDeclaration& node) {
  // Missing declarations are error recovery nodes, no binding needed
}

void Binder::visit(const ast::InterfaceBody& node) {
  // InterfaceBody doesn't have getMembers method, it's a simple container
  // TODO: Implement interface body binding when interface is available
}

void Binder::visit(const ast::StructBody& node) {
  // StructBody doesn't have getMembers method, it's a simple container
  // TODO: Implement struct body binding when interface is available
}

void Binder::visit(const ast::ErrorBody& node) {
  // Error bodies are error recovery nodes, no binding needed
}

void Binder::visit(const ast::EnumBody& node) {
  // EnumBody doesn't have getMembers method, it's a simple container
  // TODO: Implement enum body binding when interface is available
}

void Binder::visit(const ast::UnionTypeNode& unionType) {
  for (auto& type : unionType.getTypes()) { type.accept(*this); }
}
void Binder::visit(const ast::IntersectionTypeNode& intersectionType) {
  for (auto& type : intersectionType.getTypes()) { type.accept(*this); }
}
void Binder::visit(const ast::ParenthesizedTypeNode& parenType) {
  parenType.getType().accept(*this);
}
void Binder::visit(const ast::PredefinedTypeNode& predefinedType) {}
void Binder::visit(const ast::ObjectTypeNode& objectType) {}
void Binder::visit(const ast::TupleTypeNode& tupleType) {
  for (auto& element : tupleType.getElementTypes()) { element.accept(*this); }
}
void Binder::visit(const ast::ReturnTypeNode& returnType) { returnType.getType().accept(*this); }
void Binder::visit(const ast::OptionalTypeNode& optionalType) {
  optionalType.getType().accept(*this);
}
void Binder::visit(const ast::TypeQueryNode& typeQuery) { typeQuery.getExpression().accept(*this); }

// Predefined type visitors - these are leaf nodes with no children to visit
void Binder::visit(const ast::BoolTypeNode& node) {}
void Binder::visit(const ast::I8TypeNode& node) {}
void Binder::visit(const ast::I16TypeNode& node) {}
void Binder::visit(const ast::I32TypeNode& node) {}
void Binder::visit(const ast::I64TypeNode& node) {}
void Binder::visit(const ast::U8TypeNode& node) {}
void Binder::visit(const ast::U16TypeNode& node) {}
void Binder::visit(const ast::U32TypeNode& node) {}
void Binder::visit(const ast::U64TypeNode& node) {}
void Binder::visit(const ast::F32TypeNode& node) {}
void Binder::visit(const ast::F64TypeNode& node) {}
void Binder::visit(const ast::StrTypeNode& node) {}
void Binder::visit(const ast::UnitTypeNode& node) {}

// Binding pattern visitors
void Binder::visit(const ast::ArrayBindingPattern& node) {
  // TODO: Implement array binding pattern binding
  for (const auto& element : node.getElements()) { element.accept(*this); }
}

void Binder::visit(const ast::ObjectBindingPattern& node) {
  // TODO: Implement object binding pattern binding
  for (const auto& property : node.getProperties()) { property.accept(*this); }
}

// Expression visitors
void Binder::visit(const ast::ThisExpression& node) {
  // TODO: Implement this expression binding
}

void Binder::visit(const ast::SuperExpression& node) {
  // TODO: Implement super expression binding
}

// Module visitor
void Binder::visit(const ast::Module& node) {
  // TODO: Implement module binding
}

// Pattern visitors
void Binder::visit(const ast::WildcardPattern& node) {
  // Wildcard patterns don't bind anything, they match everything
}

void Binder::visit(const ast::IdentifierPattern& node) {
  // Identifier patterns bind the identifier to the matched value
  // TODO: Implement identifier pattern binding
}

void Binder::visit(const ast::TuplePattern& node) {
  // Tuple patterns destructure tuples
  // TODO: Implement tuple pattern binding when interface is available
}

void Binder::visit(const ast::StructurePattern& node) {
  // Structure patterns destructure structs/objects
  // TODO: Implement structure pattern binding when interface is available
}

void Binder::visit(const ast::ArrayPattern& node) {
  // Array patterns destructure arrays
  // TODO: Implement array pattern binding when interface is available
}

void Binder::visit(const ast::IsPattern& node) {
  // Is patterns perform type checking
  // TODO: Implement is pattern binding when interface is available
}

void Binder::visit(const ast::ExpressionPattern& node) {
  // Expression patterns match against expressions
  // TODO: Implement expression pattern binding when interface is available
}

void Binder::visit(const ast::EnumPattern& node) {
  // Enum patterns match enum variants
  // TODO: Implement enum pattern binding when interface is available
}

// Signature visitors
void Binder::visit(const ast::PropertySignature& node) {
  // Property signatures define property types in interfaces
  // TODO: Implement property signature binding when interface is available
}

void Binder::visit(const ast::MethodSignature& node) {
  // Method signatures define method types in interfaces
  // TODO: Implement method signature binding when interface is available
}

// Body visitors
void Binder::visit(const ast::ClassBody& node) {
  // Class bodies contain class members
  // TODO: Implement class body binding when interface is available
}

void Binder::enterScope(symbol::Scope& scope) {
  // TODO: Implement scope management
}

ContainerFlags Binder::getContainerFlags(const ast::Node& node) const {
  using ast::SyntaxKind;

  auto kind = node.getKind();
  ContainerFlags flags = ContainerFlags::None;

  switch (kind) {
    case SyntaxKind::SourceFile:
      flags |= ContainerFlags::IsContainer | ContainerFlags::IsModuleContainer;
      break;
    case SyntaxKind::FunctionDeclaration:
    case SyntaxKind::MethodDeclaration:
    case SyntaxKind::ConstructorDeclaration:
      flags |= ContainerFlags::IsContainer | ContainerFlags::IsControlFlowContainer |
               ContainerFlags::IsFunctionLike | ContainerFlags::HasLocals |
               ContainerFlags::IsThisContainer;
      break;
    case SyntaxKind::FunctionExpression:
      flags |= ContainerFlags::IsContainer | ContainerFlags::IsControlFlowContainer |
               ContainerFlags::IsFunctionLike | ContainerFlags::IsFunctionExpression |
               ContainerFlags::HasLocals | ContainerFlags::IsThisContainer;
      break;
    case SyntaxKind::ClassDeclaration:
      flags |=
          ContainerFlags::IsContainer | ContainerFlags::HasLocals | ContainerFlags::IsThisContainer;
      break;
    case SyntaxKind::InterfaceDeclaration:
      flags |= ContainerFlags::IsContainer | ContainerFlags::IsInterface;
      break;
    case SyntaxKind::BlockStatement:
      flags |= ContainerFlags::IsBlockScopedContainer;
      break;
    case SyntaxKind::Module:
      flags |= ContainerFlags::IsContainer | ContainerFlags::IsModuleContainer;
      break;
    default:
      // No container flags for other node types
      break;
  }

  return flags;
}

bool Binder::isContainer(const ast::Node& node) const {
  auto flags = getContainerFlags(node);
  return hasFlag(flags, ContainerFlags::IsContainer);
}

bool Binder::isBlockScopedContainer(const ast::Node& node) const {
  auto flags = getContainerFlags(node);
  return hasFlag(flags, ContainerFlags::IsBlockScopedContainer);
}

zc::Maybe<const symbol::Symbol&> Binder::lookupSymbol(zc::StringPtr name) const {
  // Look up symbol in current scope chain
  ZC_IF_SOME(currentScope, impl->context.currentScope) {
    return lookupSymbolInScope(name, currentScope);
  }
  return zc::none;
}

zc::Maybe<symbol::Symbol&> Binder::lookupSymbolInScope(zc::StringPtr name,
                                                       symbol::Scope& scope) const {
  // TODO: Implement symbol lookup in specific scope
  return zc::none;
}

void Binder::bindImportDeclaration(const ast::ImportDeclaration& importDecl) {
  // TODO: Implement import declaration binding
  // This would involve:
  // 1. Resolving the module path
  // 2. Creating symbols for imported names
  // 3. Adding them to the current scope
}

void Binder::bindExportDeclaration(const ast::ExportDeclaration& exportDecl) {
  // TODO: Implement export declaration binding
  // This would involve:
  // 1. Processing the exported declaration
  // 2. Marking symbols as exported
  // 3. Adding to module's export table
}

void Binder::bindVariableDeclaration(const ast::VariableDeclaration& varDecl) {
  // TODO: Implement variable declaration binding
  // This would involve:
  // 1. Creating a symbol for the variable
  // 2. Adding it to the current scope
  // 3. Processing the initializer if present
}

void Binder::bindFunctionDeclaration(const ast::FunctionDeclaration& funcDecl) {
  // TODO: Implement function declaration binding
  // This would involve:
  // 1. Creating a symbol for the function
  // 2. Creating a new scope for the function body
  // 3. Processing parameters and body
}

void Binder::bindClassDeclaration(const ast::ClassDeclaration& classDecl) {
  // TODO: Implement class declaration binding
  // This would involve:
  // 1. Creating a symbol for the class
  // 2. Creating a new scope for class members
  // 3. Processing class body and members
}

void Binder::bindInterfaceDeclaration(const ast::InterfaceDeclaration& interfaceDecl) {
  // TODO: Implement interface declaration binding
  // This would involve:
  // 1. Creating a symbol for the interface
  // 2. Processing interface members
  // 3. Setting up inheritance relationships
}

void Binder::bindBlockStatement(const ast::Node& block) {
  // TODO: Implement block statement binding
  // This would involve:
  // 1. Creating a new block scope
  // 2. Processing all statements in the block
  // 3. Managing local variable declarations
}

void Binder::checkContextualIdentifier(const ast::Identifier& node) {
  // Report error only if there are no parse errors in file and node is not ambient or JSDoc
  if (impl->diagEng.hasErrors()) {
    return;  // Skip contextual identifier checks if there are parse errors
  }

  // Get the identifier text
  auto identifierText = node.getText();

  // Create a temporary lexer instance to get keyword kind
  // Note: This is a simplified approach - in a real implementation, we might want to cache this
  ast::SyntaxKind originalKeywordKind = ast::SyntaxKind::Identifier;

  // Check common contextual keywords directly
  if (identifierText == "await"_zc) {
    originalKeywordKind = ast::SyntaxKind::AwaitKeyword;
  } else if (identifierText == "yield"_zc) {
    originalKeywordKind = ast::SyntaxKind::YieldKeyword;
  } else if (identifierText == "async"_zc) {
    originalKeywordKind = ast::SyntaxKind::AsyncKeyword;
  } else if (identifierText == "let"_zc) {
    originalKeywordKind = ast::SyntaxKind::LetKeyword;
  } else if (identifierText == "static"_zc) {
    originalKeywordKind = ast::SyntaxKind::StaticKeyword;
  } else if (identifierText == "public"_zc) {
    originalKeywordKind = ast::SyntaxKind::PublicKeyword;
  } else if (identifierText == "private"_zc) {
    originalKeywordKind = ast::SyntaxKind::PrivateKeyword;
  } else if (identifierText == "protected"_zc) {
    originalKeywordKind = ast::SyntaxKind::ProtectedKeyword;
  } else if (identifierText == "abstract"_zc) {
    originalKeywordKind = ast::SyntaxKind::AbstractKeyword;
  } else if (identifierText == "implements"_zc) {
    originalKeywordKind = ast::SyntaxKind::ImplementsKeyword;
  } else if (identifierText == "interface"_zc) {
    originalKeywordKind = ast::SyntaxKind::InterfaceKeyword;
  } else if (identifierText == "package"_zc) {
    originalKeywordKind = ast::SyntaxKind::PackageKeyword;
  }

  // If it's just a regular identifier, no contextual checks needed
  if (originalKeywordKind == ast::SyntaxKind::Identifier) { return; }

  // Get source location for error reporting
  source::SourceLoc loc = node.getSourceRange().getStart();

  // Check for reserved words (future reserved words)
  if ((originalKeywordKind >= ast::SyntaxKind::ImplementsKeyword &&
       originalKeywordKind <= ast::SyntaxKind::PackageKeyword) ||
      originalKeywordKind == ast::SyntaxKind::InterfaceKeyword ||
      originalKeywordKind == ast::SyntaxKind::PrivateKeyword ||
      originalKeywordKind == ast::SyntaxKind::ProtectedKeyword ||
      originalKeywordKind == ast::SyntaxKind::PublicKeyword ||
      originalKeywordKind == ast::SyntaxKind::StaticKeyword) {
    impl->diagEng.diagnose<DiagID::ReservedWord>(loc, identifierText);
    return;
  }

  // Check for await keyword
  if (originalKeywordKind == ast::SyntaxKind::AwaitKeyword) {
    // TODO: Check if we're in a module context and at top level
    // For now, check if we're in an await context
    if (hasFlag(impl->context.flags, BindingContextFlags::AwaitContext)) {
      impl->diagEng.diagnose<DiagID::ReservedInContext>(loc, identifierText);
    }
    // TODO: Add check for top-level module context
    // else if (isExternalModule && isInTopLevelContext) {
    //   impl->diagEng.diagnose<DiagID::ReservedInModule>(loc, identifierText);
    // }
    return;
  }

  // Check for yield keyword
  if (originalKeywordKind == ast::SyntaxKind::YieldKeyword &&
      hasFlag(impl->context.flags, BindingContextFlags::YieldContext)) {
    impl->diagEng.diagnose<DiagID::ReservedInContext>(loc, identifierText);
    return;
  }
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
