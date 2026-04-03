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

static zc::StringPtr getIdentifierName(const ast::NamedDeclaration& decl) {
  zc::StringPtr name;
  ZC_SWITCH_ONEOF(decl.getName()) {
    ZC_CASE_ONEOF(maybeId, zc::Maybe<const ast::Identifier&>) {
      ZC_IF_SOME(id, maybeId) { name = id.getText(); }
    }
    ZC_CASE_ONEOF(maybePat, zc::Maybe<const ast::BindingPattern&>) {
      ZC_ASSERT(false, "Expected identifier");
    }
  }
  return name;
}

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

  zc::Vector<zc::String> ownedScopeNames;
  uint32_t nextScopeId = 1;

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
  impl->ownedScopeNames.clear();
  impl->nextScopeId = 1;

  // Initialize with global scope
  auto& scopeManager = impl->symbolTable.getScopeManager();
  ZC_IF_SOME(globalScope, scopeManager.getGlobalScopeMutable()) {
    impl->scopeStack.add(globalScope);
    impl->context.currentScope = globalScope;
    impl->symbolTable.setCurrentScope(globalScope);
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
    case ast::SyntaxKind::ParameterDeclaration: {
      auto& paramDecl = ast::cast<ast::ParameterDeclaration&>(node);
      paramDecl.setSymbol(symbol);
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

void Binder::visit(const ast::ForInStatement& node) {
  // Visit initializer
  node.getInitializer().accept(*this);

  // Visit expression
  node.getExpression().accept(*this);

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

void Binder::visit(const ast::IterationStatement& node) {
  // IterationStatement is a base class, no specific members to visit
}

void Binder::visit(const ast::DeclarationStatement& node) {
  // DeclarationStatement is a base class, no specific members to visit
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

void Binder::visit(const ast::ModuleDeclaration& moduleDecl) {
  moduleDecl.getModulePath().accept(*this);
}

void Binder::visit(const ast::ImportDeclaration& importDecl) { bindImportDeclaration(importDecl); }

void Binder::visit(const ast::ImportSpecifier& importSpecifier) {
  importSpecifier.getImportedName().accept(*this);
  ZC_IF_SOME(alias, importSpecifier.getAlias()) { alias.accept(*this); }
}

void Binder::visit(const ast::ExportDeclaration& exportDecl) { bindExportDeclaration(exportDecl); }

void Binder::visit(const ast::ExportSpecifier& exportSpecifier) {
  exportSpecifier.getExportedName().accept(*this);
  ZC_IF_SOME(alias, exportSpecifier.getAlias()) { alias.accept(*this); }
}

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
void Binder::visit(const ast::BindingElement& node) {
  ZC_IF_SOME(pattern, node.getBindingPattern()) {
    for (auto& element : pattern.getElements()) { element.accept(*this); }
    ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
    return;
  }

  ZC_IF_SOME(scope, impl->context.currentScope) {
    ZC_SWITCH_ONEOF(node.getName()) {
      ZC_CASE_ONEOF(maybeId, zc::Maybe<const ast::Identifier&>) {
        ZC_IF_SOME(identifier, maybeId) {
          auto name = identifier.getText();
          auto loc = node.getSourceRange().getStart();

          if (impl->symbolTable.lookup(name, scope) != zc::none) {
            impl->diagEng.diagnose<DiagID::RedeclareVariable>(loc, name);
            return;
          }

          auto& symbol = impl->symbolTable.createVariable(name, scope);

          symbol::SymbolFlags storageFlag = symbol::SymbolFlags::Local;
          if (scope.getKind() == symbol::Scope::Kind::Global) {
            storageFlag = symbol::SymbolFlags::Global;
          }

          addDeclarationToSymbol(symbol, const_cast<ast::BindingElement&>(node), storageFlag);
        }
      }
      ZC_CASE_ONEOF(maybePattern, zc::Maybe<const ast::BindingPattern&>) {
        ZC_IF_SOME(pattern, maybePattern) { pattern.accept(*this); }
      }
    }

    ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  }
}

void Binder::visit(const ast::EnumMember& node) {
  ZC_IF_SOME(scope, impl->context.currentScope) {
    auto name = getIdentifierName(node);
    auto loc = node.getSourceRange().getStart();

    if (impl->symbolTable.lookup(name, scope) != zc::none) {
      impl->diagEng.diagnose<DiagID::RedeclareVariable>(loc, name);
      return;
    }

    auto& symbol = impl->symbolTable.createVariable(name, scope);

    // Enum members are immutable properties
    symbol::SymbolFlags flags = symbol::SymbolFlags::Property | symbol::SymbolFlags::Immutable;
    addDeclarationToSymbol(symbol, const_cast<ast::EnumMember&>(node), flags);

    ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
    ZC_IF_SOME(type, node.getTupleType()) { type.accept(*this); }
  }
}

void Binder::visit(const ast::ErrorDeclaration& node) {
  // Error declaration doesn't bind symbols but might have children
  for (const auto& member : node.getMembers()) { member.accept(*this); }
}
void Binder::visit(const ast::EmptyStatement& node) {}
void Binder::visit(const ast::LabeledStatement& node) {
  node.getLabel().accept(*this);
  node.getStatement().accept(*this);
}
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
  ZC_IF_SOME(args, node.getArguments()) {
    for (const auto& arg : args) { arg->accept(*this); }
  }
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

void Binder::visit(const ast::TemplateSpan& node) {
  node.getExpression().accept(*this);
  node.getLiteral().accept(*this);
}

void Binder::visit(const ast::TemplateLiteralExpression& node) {
  node.getHead().accept(*this);
  for (const auto& span : node.getSpans()) { span.accept(*this); }
}

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
void Binder::visit(const ast::NonNullExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::ExpressionWithTypeArguments& node) {
  node.getExpression().accept(*this);
  for (const auto& typeArg : node.getTypeArguments()) { typeArg.accept(*this); }
}
void Binder::visit(const ast::VoidExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::TypeOfExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::AwaitExpression& node) { node.getExpression().accept(*this); }
void Binder::visit(const ast::FunctionExpression& node) {
  // Function expressions are anonymous functions that need special handling
  // They create their own scope and bind parameters and body

  ZC_IF_SOME(typeParameters, node.getTypeParameters()) {
    for (const auto& typeParam : typeParameters) { typeParam->accept(*this); }
  }

  // Visit parameters
  for (const auto& param : node.getParameters()) { param.accept(*this); }

  // Visit return type if present
  ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }

  // Visit function body
  node.getBody().accept(*this);
}

void Binder::visit(const ast::CaptureElement& node) {
  ZC_IF_SOME(id, node.getIdentifier()) { id.accept(*this); }
}

void Binder::visit(const ast::ArrayLiteralExpression& node) {
  for (auto& element : node.getElements()) { element.accept(*this); }
}
void Binder::visit(const ast::ObjectLiteralExpression& node) {
  // Visit all property assignments
  for (const auto& property : node.getProperties()) { property.accept(*this); }
}

void Binder::visit(const ast::ObjectLiteralElement& node) {
  // Abstract interface, should not be visited directly
}

void Binder::visit(const ast::PropertyAssignment& node) {
  node.getNameIdentifier().accept(*this);
  ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
}

void Binder::visit(const ast::ShorthandPropertyAssignment& node) {
  node.getNameIdentifier().accept(*this);
}

void Binder::visit(const ast::SpreadAssignment& node) { node.getExpression().accept(*this); }

void Binder::visit(const ast::ModulePath& node) {
  // Module paths are just identifiers, no special binding needed
}

void Binder::visit(const ast::MethodDeclaration& node) {
  // Bind method declaration - similar to function declaration but within class/interface context
  // TODO: Implement method declaration binding
}

void Binder::visit(const ast::GetAccessor& node) {
  ZC_IF_SOME(parentScope, impl->context.currentScope) {
    auto name = getIdentifierName(node);

    // Create scope for the accessor
    zc::Maybe<symbol::Scope&> scopeParent = parentScope;
    impl->ownedScopeNames.add(zc::str("get_accessor#", impl->nextScopeId++, ":", name));
    symbol::Scope& scope = impl->symbolTable.getScopeManager().createScope(
        symbol::Scope::Kind::Function, impl->ownedScopeNames.back(), scopeParent);

    enterScope(scope);

    for (auto& typeParam : node.getTypeParameters()) { typeParam.accept(*this); }
    for (auto& param : node.getParameters()) { param.accept(*this); }
    ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
    ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }

    exitScope();
  }
}

void Binder::visit(const ast::SetAccessor& node) {
  ZC_IF_SOME(parentScope, impl->context.currentScope) {
    auto name = getIdentifierName(node);

    // Create scope for the accessor
    zc::Maybe<symbol::Scope&> scopeParent = parentScope;
    impl->ownedScopeNames.add(zc::str("set_accessor#", impl->nextScopeId++, ":", name));
    symbol::Scope& scope = impl->symbolTable.getScopeManager().createScope(
        symbol::Scope::Kind::Function, impl->ownedScopeNames.back(), scopeParent);

    enterScope(scope);

    for (auto& typeParam : node.getTypeParameters()) { typeParam.accept(*this); }
    for (auto& param : node.getParameters()) { param.accept(*this); }
    ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
    ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }

    exitScope();
  }
}

void Binder::visit(const ast::InitDeclaration& node) {
  ZC_IF_SOME(parentScope, impl->context.currentScope) {
    // Create scope for init
    zc::Maybe<symbol::Scope&> scopeParent = parentScope;
    impl->ownedScopeNames.add(zc::str("init#", impl->nextScopeId++));
    symbol::Scope& scope = impl->symbolTable.getScopeManager().createScope(
        symbol::Scope::Kind::Function, impl->ownedScopeNames.back(), scopeParent);

    enterScope(scope);

    for (auto& typeParam : node.getTypeParameters()) { typeParam.accept(*this); }
    for (auto& param : node.getParameters()) { param.accept(*this); }
    ZC_IF_SOME(returnType, node.getReturnType()) { returnType.accept(*this); }
    ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }

    exitScope();
  }
}

void Binder::visit(const ast::DeinitDeclaration& node) {
  ZC_IF_SOME(parentScope, impl->context.currentScope) {
    // Create scope for deinit
    zc::Maybe<symbol::Scope&> scopeParent = parentScope;
    impl->ownedScopeNames.add(zc::str("deinit#", impl->nextScopeId++));
    symbol::Scope& scope = impl->symbolTable.getScopeManager().createScope(
        symbol::Scope::Kind::Function, impl->ownedScopeNames.back(), scopeParent);

    enterScope(scope);

    ZC_IF_SOME(body, node.getBody()) { body.accept(*this); }

    exitScope();
  }
}

void Binder::visit(const ast::ParameterDeclaration& node) {
  ZC_IF_SOME(scope, impl->context.currentScope) {
    ZC_SWITCH_ONEOF(node.getName()) {
      ZC_CASE_ONEOF(maybeId, zc::Maybe<const ast::Identifier&>) {
        ZC_IF_SOME(identifier, maybeId) {
          auto name = identifier.getText();
          auto loc = node.getSourceRange().getStart();

          if (impl->symbolTable.lookup(name, scope) != zc::none) {
            impl->diagEng.diagnose<DiagID::RedeclareParameter>(loc, name);
            return;
          }

          auto& symbol = impl->symbolTable.createParameter(name, scope);

          symbol::SymbolFlags storageFlag = symbol::SymbolFlags::Local;

          addDeclarationToSymbol(symbol, const_cast<ast::ParameterDeclaration&>(node), storageFlag);
        }
      }
      ZC_CASE_ONEOF(maybePattern, zc::Maybe<const ast::BindingPattern&>) {
        ZC_IF_SOME(pattern, maybePattern) { pattern.accept(*this); }
      }
    }

    ZC_IF_SOME(type, node.getType()) { type.accept(*this); }
    ZC_IF_SOME(init, node.getInitializer()) { init.accept(*this); }
  }
}

void Binder::visit(const ast::PropertyDeclaration& node) {
  // Bind property declaration - create symbol for class/interface property
  // TODO: Implement property declaration binding
}

void Binder::visit(const ast::SemicolonClassElement& node) {
  // Semicolon class elements are empty, no binding needed.
}

void Binder::visit(const ast::SemicolonInterfaceElement& node) {}

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

void Binder::visit(const ast::Declaration& node) {
  // Declaration is an interface node, delegate to specific implementation
  // This should not be called directly as Declaration is abstract
}

void Binder::visit(const ast::NamedDeclaration& node) {
  // NamedDeclaration is an interface node, delegate to specific implementation
  // This should not be called directly as NamedDeclaration is abstract
}

void Binder::visit(const ast::Pattern& node) {
  // Pattern is an interface node, delegate to specific implementation
  // This should not be called directly as Pattern is abstract
}

void Binder::visit(const ast::PrimaryPattern& node) {
  // PrimaryPattern is an interface node, delegate to specific implementation
  // This should not be called directly as PrimaryPattern is abstract
}

void Binder::visit(const ast::BindingPattern& node) {
  // BindingPattern is an interface node, delegate to specific implementation
  // This should not be called directly as BindingPattern is abstract
}
void Binder::visit(const ast::ClassElement& node) {}
void Binder::visit(const ast::InterfaceElement& node) {}
void Binder::visit(const ast::ObjectTypeNode& objectType) {}
void Binder::visit(const ast::TupleTypeNode& tupleType) {
  for (auto& element : tupleType.getElementTypes()) { element.accept(*this); }
}
void Binder::visit(const ast::ReturnTypeNode& returnType) { returnType.getType().accept(*this); }
void Binder::visit(const ast::OptionalTypeNode& optionalType) {
  optionalType.getType().accept(*this);
}
void Binder::visit(const ast::TypeQueryNode& typeQuery) { typeQuery.getExpression().accept(*this); }

void Binder::visit(const ast::NamedTupleElement& node) {
  // Visit type
  node.getType().accept(*this);
}

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
void Binder::visit(const ast::NullTypeNode& node) {}

// Binding pattern visitors
void Binder::visit(const ast::ArrayBindingPattern& node) {
  // TODO: Implement array binding pattern binding
  for (const auto& element : node.getElements()) { element.accept(*this); }
}

void Binder::visit(const ast::ObjectBindingPattern& node) {
  // TODO: Implement object binding pattern binding
  for (const auto& property : node.getProperties()) { property.accept(*this); }
}

void Binder::visit(const ast::PatternProperty& node) {
  ZC_IF_SOME(pattern, node.getPattern()) { pattern.accept(*this); }
}

// Expression visitors
void Binder::visit(const ast::ThisExpression& node) {
  // TODO: Implement this expression binding
}

void Binder::visit(const ast::SuperExpression& node) {
  // TODO: Implement super expression binding
}

void Binder::visit(const ast::SpreadElement& node) { node.getExpression().accept(*this); }

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

void Binder::visit(const ast::HeritageClause& node) {
  // Heritage clause contains type references, visit them
  for (const auto& t : node.getTypes()) { t->accept(*this); }
}

void Binder::enterScope(symbol::Scope& scope) {
  impl->scopeStack.add(scope);
  impl->context.currentScope = scope;
  impl->symbolTable.setCurrentScope(scope);
}

void Binder::exitScope() {
  ZC_REQUIRE(!impl->scopeStack.empty(), "Scope stack is empty");

  impl->scopeStack.removeLast();

  if (!impl->scopeStack.empty()) {
    ZC_IF_SOME(scope, impl->scopeStack.back()) {
      impl->context.currentScope = scope;
      impl->symbolTable.setCurrentScope(scope);
    }
    else {
      impl->context.currentScope = zc::none;
      impl->symbolTable.setCurrentScope(
          ZC_ASSERT_NONNULL(impl->symbolTable.getScopeManager().getGlobalScope()));
    }
  } else {
    impl->context.currentScope = zc::none;
    impl->symbolTable.setCurrentScope(
        ZC_ASSERT_NONNULL(impl->symbolTable.getScopeManager().getGlobalScope()));
  }
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
    case SyntaxKind::InitDeclaration:
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
  return impl->symbolTable.lookupRecursive(name, scope);
}

void Binder::bindImportDeclaration(const ast::ImportDeclaration& importDecl) {
  importDecl.getModulePath().accept(*this);
  for (const auto& specifier : importDecl.getSpecifiers()) { specifier.accept(*this); }
  ZC_IF_SOME(alias, importDecl.getAlias()) { alias.accept(*this); }
}

void Binder::bindExportDeclaration(const ast::ExportDeclaration& exportDecl) {
  ZC_IF_SOME(modulePath, exportDecl.getModulePath()) { modulePath.accept(*this); }
  for (const auto& specifier : exportDecl.getSpecifiers()) { specifier.accept(*this); }
  ZC_IF_SOME(declaration, exportDecl.getDeclaration()) { declaration.accept(*this); }
}

void Binder::bindVariableDeclaration(const ast::VariableDeclaration& varDecl) {
  ZC_IF_SOME(scope, impl->context.currentScope) {
    ZC_SWITCH_ONEOF(varDecl.getName()) {
      ZC_CASE_ONEOF(maybeId, zc::Maybe<const ast::Identifier&>) {
        ZC_IF_SOME(identifier, maybeId) {
          auto name = identifier.getText();
          auto loc = varDecl.getSourceRange().getStart();

          if (impl->symbolTable.lookup(name, scope) != zc::none) {
            impl->diagEng.diagnose<DiagID::RedeclareVariable>(loc, name);
            return;
          }

          auto& symbol = impl->symbolTable.createVariable(name, scope);

          symbol::SymbolFlags storageFlag = symbol::SymbolFlags::Local;
          if (scope.getKind() == symbol::Scope::Kind::Global) {
            storageFlag = symbol::SymbolFlags::Global;
          }

          addDeclarationToSymbol(symbol, const_cast<ast::VariableDeclaration&>(varDecl),
                                 storageFlag);
        }
      }
      ZC_CASE_ONEOF(maybePattern, zc::Maybe<const ast::BindingPattern&>) {
        ZC_IF_SOME(pattern, maybePattern) { pattern.accept(*this); }
      }
    }

    ZC_IF_SOME(type, varDecl.getType()) { type.accept(*this); }
    ZC_IF_SOME(init, varDecl.getInitializer()) { init.accept(*this); }
  }
}

void Binder::bindFunctionDeclaration(const ast::FunctionDeclaration& funcDecl) {
  ZC_IF_SOME(parentScope, impl->context.currentScope) {
    auto name = getIdentifierName(funcDecl);
    auto loc = funcDecl.getSourceRange().getStart();

    if (impl->symbolTable.lookup(name, parentScope) != zc::none) {
      impl->diagEng.diagnose<DiagID::RedeclareFunction>(loc, name);
      return;
    }

    auto& symbol = impl->symbolTable.createFunction(name, parentScope);

    symbol::SymbolFlags storageFlag = symbol::SymbolFlags::Local;
    if (parentScope.getKind() == symbol::Scope::Kind::Global) {
      storageFlag = symbol::SymbolFlags::Global;
    }
    addDeclarationToSymbol(symbol, const_cast<ast::FunctionDeclaration&>(funcDecl), storageFlag);

    zc::Maybe<symbol::Scope&> scopeParent = parentScope;
    impl->ownedScopeNames.add(zc::str("function#", impl->nextScopeId++, ":", name));
    symbol::Scope& funcScope = impl->symbolTable.getScopeManager().createScope(
        symbol::Scope::Kind::Function, impl->ownedScopeNames.back(), scopeParent);

    enterScope(funcScope);

    const_cast<ast::FunctionDeclaration&>(funcDecl).setLocals(impl->symbolTable);

    const auto& typeParameters = funcDecl.getTypeParameters();
    for (const auto& typeParam : typeParameters) { typeParam.accept(*this); }
    for (const auto& param : funcDecl.getParameters()) { param.accept(*this); }
    ZC_IF_SOME(returnType, funcDecl.getReturnType()) { returnType.accept(*this); }

    funcDecl.getBody().accept(*this);

    exitScope();
  }
}

void Binder::bindClassDeclaration(const ast::ClassDeclaration& classDecl) {
  ZC_IF_SOME(parentScope, impl->context.currentScope) {
    auto name = getIdentifierName(classDecl);
    auto loc = classDecl.getSourceRange().getStart();

    if (impl->symbolTable.lookup(name, parentScope) != zc::none) {
      impl->diagEng.diagnose<DiagID::RedeclareClass>(loc, name);
      return;
    }

    auto& symbol = impl->symbolTable.createClass(name, parentScope);

    symbol::SymbolFlags storageFlag = symbol::SymbolFlags::Local;
    if (parentScope.getKind() == symbol::Scope::Kind::Global) {
      storageFlag = symbol::SymbolFlags::Global;
    }
    addDeclarationToSymbol(symbol, const_cast<ast::ClassDeclaration&>(classDecl), storageFlag);

    zc::Maybe<symbol::Scope&> scopeParent = parentScope;
    impl->ownedScopeNames.add(zc::str("class#", impl->nextScopeId++, ":", name));
    symbol::Scope& classScope = impl->symbolTable.getScopeManager().createScope(
        symbol::Scope::Kind::Class, impl->ownedScopeNames.back(), scopeParent);

    enterScope(classScope);

    const auto& typeParameters = classDecl.getTypeParameters();
    for (const auto& typeParam : typeParameters) { typeParam.accept(*this); }

    const auto& heritageClauses = classDecl.getHeritageClauses();
    for (const auto& clause : heritageClauses) { clause.accept(*this); }
    for (const auto& member : classDecl.getMembers()) {
      if (auto* node = dynamic_cast<const ast::Node*>(&member)) { node->accept(*this); }
    }

    exitScope();
  }
}

void Binder::bindInterfaceDeclaration(const ast::InterfaceDeclaration& interfaceDecl) {
  ZC_IF_SOME(parentScope, impl->context.currentScope) {
    auto name = getIdentifierName(interfaceDecl);
    auto loc = interfaceDecl.getSourceRange().getStart();

    if (impl->symbolTable.lookup(name, parentScope) != zc::none) {
      impl->diagEng.diagnose<DiagID::RedeclareInterface>(loc, name);
      return;
    }

    auto& symbol = impl->symbolTable.createInterface(name, parentScope);

    symbol::SymbolFlags storageFlag = symbol::SymbolFlags::Local;
    if (parentScope.getKind() == symbol::Scope::Kind::Global) {
      storageFlag = symbol::SymbolFlags::Global;
    }
    addDeclarationToSymbol(symbol, const_cast<ast::InterfaceDeclaration&>(interfaceDecl),
                           storageFlag);

    zc::Maybe<symbol::Scope&> scopeParent = parentScope;
    impl->ownedScopeNames.add(zc::str("interface#", impl->nextScopeId++, ":", name));
    symbol::Scope& interfaceScope = impl->symbolTable.getScopeManager().createScope(
        symbol::Scope::Kind::Interface, impl->ownedScopeNames.back(), scopeParent);

    enterScope(interfaceScope);

    for (const auto& member : interfaceDecl.getMembers()) {
      if (auto* node = dynamic_cast<const ast::Node*>(&member)) { node->accept(*this); }
    }

    exitScope();
  }
}

void Binder::bindBlockStatement(const ast::Node& block) {
  auto& blockStmt = ast::cast<ast::BlockStatement>(block);

  zc::Maybe<symbol::Scope&> parentScope = impl->context.currentScope;

  impl->ownedScopeNames.add(zc::str("block#", impl->nextScopeId++));
  symbol::Scope& scope = impl->symbolTable.getScopeManager().createScope(
      symbol::Scope::Kind::Block, impl->ownedScopeNames.back(), parentScope);

  enterScope(scope);

  const_cast<ast::BlockStatement&>(blockStmt).setLocals(impl->symbolTable);

  for (const auto& stmt : blockStmt.getStatements()) { stmt.accept(*this); }

  exitScope();
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

void Binder::visit(const ast::BigIntLiteral& node) {
  // No binding needed for BigInt literals
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
