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

#include "zomlang/compiler/binder/decl-collector.h"

#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-schema.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/symbol/symbol-table.h"
#include "zomlang/compiler/symbol/symbol.h"
#include "zomlang/compiler/symbol/type-symbol.h"
#include "zomlang/compiler/symbol/value-symbol.h"

namespace zomlang {
namespace compiler {
namespace binder {

using namespace zomlang::compiler::symbol;
using namespace zomlang::compiler::ast;
using namespace zomlang::compiler::diagnostics;

namespace {

// Mask for deriving a compact scope ID from a Scope pointer address.
constexpr uint32_t kScopeIdMask = 0xFFFFFFFF;

// Sentinel buffer ID used when a declaration has no associated source buffer.
const source::BufferId kNoBufferId{0};

// Convert AST visibility word encoding (0=Default, 1=Public, 2=Private,
// 3=Protected) to the symbol::Visibility enum.
Visibility visibilityFromAst(uint32_t word, Visibility defaultVisibility) {
  switch (word) {
    case 1:
      return Visibility::Public;
    case 2:
      return Visibility::Private;
    case 3:
      return Visibility::Protected;
    default:
      return defaultVisibility;
  }
}

}  // namespace

// ============================================================================
// Impl - Private implementation data
// ============================================================================

struct DeclCollector::Impl {
  Impl(SymbolTable& symbols, ScopeManager& scopes, const Tree& tree, BindingMetadata& metadata,
       DiagnosticEngine& diags)
      : symbols(symbols), scopes(scopes), tree(tree), metadata(metadata), diags(diags) {
    metadata.resizeFor(tree);
  }

  SymbolTable& symbols;
  ScopeManager& scopes;
  const Tree& tree;
  BindingMetadata& metadata;
  DiagnosticEngine& diags;

  // Scope stack for RAII-style management
  zc::Vector<Scope*> scopeStack;  // non-owning

  // Counter for generating unique anonymous scope names
  uint32_t anonymousScopeCounter = 0;
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

DeclCollector::DeclCollector(SymbolTable& symbols, ScopeManager& scopes, const Tree& tree,
                             BindingMetadata& metadata, DiagnosticEngine& diags) noexcept
    : impl(zc::heap<Impl>(symbols, scopes, tree, metadata, diags)) {}

DeclCollector::~DeclCollector() noexcept(false) = default;

// ============================================================================
// Scope management helpers
// ============================================================================

Scope& DeclCollector::enterScope(Scope::Kind kind, zc::StringPtr name) {
  // Determine the parent scope
  Scope* parent = nullptr;
  if (!impl->scopeStack.empty()) {
    parent = impl->scopeStack.back();
  } else {
    // Use the global scope as parent if stack is empty
    auto global = impl->scopes.getGlobalScopeMutable();
    ZC_IF_SOME(g, global) { parent = &g; }
  }

  // Generate a unique name for anonymous scopes.
  // Note: StringPtr cannot be assigned from String (operator= is deleted),
  // so we pass the empty name directly — anonymous scopes don't need names.
  zc::StringPtr scopeName = name;

  Scope& scope = impl->scopes.createScope(kind, scopeName, parent ? *parent : zc::Maybe<Scope&>{});
  impl->scopeStack.add(&scope);
  impl->scopes.pushScope(scope);
  return scope;
}

void DeclCollector::leaveScope() {
  ZC_REQUIRE(!impl->scopeStack.empty(), "Scope stack underflow in DeclCollector");
  impl->scopeStack.removeLast();
  impl->scopes.popScope();
}

Scope& DeclCollector::currentScope() {
  if (!impl->scopeStack.empty()) { return *impl->scopeStack.back(); }
  auto global = impl->scopes.getGlobalScopeMutable();
  ZC_IF_SOME(g, global) { return g; }
  ZC_UNREACHABLE;
}

// ============================================================================
// Name extraction helpers
// ============================================================================

zc::StringPtr DeclCollector::declName(NodeId node, uint32_t wordIndex) {
  const Node& n = impl->tree.node(node);
  IdentId id(n.payload.words[wordIndex]);
  return impl->tree.ident(id);
}

zc::StringPtr DeclCollector::fileName(NodeId node) {
  const Node& n = impl->tree.node(node);
  StringId id(n.payload.words[kSourceFileFileNameWord]);
  return impl->tree.string(id);
}

// ============================================================================
// Binding metadata helpers
// ============================================================================

void DeclCollector::bindSymbol(NodeId node, Symbol& sym) {
  impl->metadata.setSymbol(node, sym.getId());
}

void DeclCollector::bindScope(NodeId node, Scope& scope) {
  // Scope ID is derived from its address for now; a proper registry can be added later.
  uint32_t scopeId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&scope) & kScopeIdMask);
  impl->metadata.setScope(node, scopeId);
}

// ============================================================================
// Duplicate checking
// ============================================================================

bool DeclCollector::checkDuplicate(zc::StringPtr name, NodeId declNode, SymbolKind kind) {
  Scope& scope = currentScope();

  // Check if the name already exists in this scope
  auto existing = impl->symbols.lookup(name, scope);
  ZC_IF_SOME(existingSym, existing) {
    (void)existingSym;
    // Found a duplicate - emit the appropriate diagnostic
    const Node& n = impl->tree.node(declNode);
    auto loc = n.range.getStart();

    switch (kind) {
      case SymbolKind::Variable:
        impl->diags.diagnose<DiagID::RedeclareVariable>(loc, name);
        break;
      case SymbolKind::Function:
        impl->diags.diagnose<DiagID::RedeclareFunction>(loc, name);
        break;
      case SymbolKind::Class:
        impl->diags.diagnose<DiagID::RedeclareClass>(loc, name);
        break;
      case SymbolKind::Interface:
        impl->diags.diagnose<DiagID::RedeclareInterface>(loc, name);
        break;
      case SymbolKind::Enum:
        impl->diags.diagnose<DiagID::RedeclareEnum>(loc, name);
        break;
      case SymbolKind::TypeAlias:
        impl->diags.diagnose<DiagID::RedeclareTypeAlias>(loc, name);
        break;
      case SymbolKind::Parameter:
        impl->diags.diagnose<DiagID::RedeclareParameter>(loc, name);
        break;
      default:
        impl->diags.diagnose<DiagID::DuplicateIdentifier>(loc, name);
        break;
    }
    return true;
  }
  return false;
}

Symbol& DeclCollector::declareSymbol(zc::StringPtr name, NodeId declNode, SymbolKind kind) {
  Scope& scope = currentScope();

  // Check if this name already exists (duplicate detection is done by callers
  // via checkDuplicate, but we still handle it gracefully here by returning
  // the existing symbol instead of creating a duplicate).
  auto existing = impl->symbols.lookup(name, scope);
  ZC_IF_SOME(existingSym, existing) {
    // Bind the existing symbol to this AST node too
    bindSymbol(declNode, existingSym);
    return existingSym;
  }

  // Create the appropriate symbol type
  Symbol* sym = nullptr;
  switch (kind) {
    case SymbolKind::Variable:
      sym = &impl->symbols.createVariable(name, scope);
      break;
    case SymbolKind::Parameter:
      sym = &impl->symbols.createParameter(name, scope);
      break;
    case SymbolKind::Function:
      sym = &impl->symbols.createFunction(name, scope);
      break;
    case SymbolKind::Class:
      sym = &impl->symbols.createClass(name, scope);
      break;
    case SymbolKind::Interface:
      sym = &impl->symbols.createInterface(name, scope);
      break;
    case SymbolKind::Enum:
      // For enum, we create a Class-like symbol (enums are types)
      sym = &impl->symbols.createClass(name, scope);
      // Mark it with Enum flag
      sym->addFlag(SymbolFlags::Enum);
      break;
    case SymbolKind::TypeAlias:
      // Type aliases use ClassSymbol infrastructure with TypeAlias flag
      sym = &impl->symbols.createClass(name, scope);
      sym->addFlag(SymbolFlags::TypeAlias);
      sym->removeFlag(SymbolFlags::Class);
      break;
    case SymbolKind::Method:
      sym = &impl->symbols.createFunction(name, scope);
      sym->addFlag(SymbolFlags::Method);
      break;
    case SymbolKind::Constructor:
      sym = &impl->symbols.createFunction(name, scope);
      sym->addFlag(SymbolFlags::Constructor);
      break;
    case SymbolKind::Destructor:
      sym = &impl->symbols.createFunction(name, scope);
      sym->addFlag(SymbolFlags::Destructor);
      break;
    case SymbolKind::Constant:
      sym = &impl->symbols.createVariable(name, scope);
      sym->addFlag(SymbolFlags::Constant | SymbolFlags::Immutable);
      break;
    case SymbolKind::Field:
      sym = &impl->symbols.createVariable(name, scope);
      sym->addFlag(SymbolFlags::Field);
      break;
    default:
      // Fallback: create as a generic variable-like symbol
      sym = &impl->symbols.createVariable(name, scope);
      break;
  }

  // Record the declaration reference
  sym->addDeclarationRef(DeclarationRef(kNoBufferId, declNode));

  // Bind the symbol to the AST node
  bindSymbol(declNode, *sym);

  return *sym;
}

// ============================================================================
// Main entry point
// ============================================================================

bool DeclCollector::collect() {
  ZC_IREQUIRE(impl->tree.contains(impl->tree.root()), "cannot collect from tree without root");

  // Start with the global scope
  auto global = impl->scopes.getGlobalScopeMutable();
  ZC_IF_SOME(g, global) {
    impl->scopeStack.add(&g);
    impl->scopes.pushScope(g);
  }

  // Visit the root source file
  visitNode(impl->tree.root());

  // Pop the global scope
  if (!impl->scopeStack.empty()) {
    impl->scopeStack.removeLast();
    impl->scopes.popScope();
  }

  return !impl->diags.hasErrors();
}

// ============================================================================
// Generic node dispatch
// ============================================================================

void DeclCollector::visitNode(NodeId node) {
  if (!node) return;
  if (!impl->tree.contains(node)) return;

  const Node& n = impl->tree.node(node);

  switch (n.kind) {
    // Top-level structure
    case SyntaxKind::SourceFile:
      visitSourceFile(node);
      break;
    case SyntaxKind::StatementListItem:
      visitStatementListItem(node);
      break;

    // Declarations
    case SyntaxKind::FunctionDecl:
      visitFunctionDecl(node);
      break;
    case SyntaxKind::ClassDecl:
      visitClassDecl(node);
      break;
    case SyntaxKind::StructDecl:
      visitStructDecl(node);
      break;
    case SyntaxKind::InterfaceDecl:
      visitInterfaceDecl(node);
      break;
    case SyntaxKind::EnumDeclaration:
      visitEnumDeclaration(node);
      break;
    case SyntaxKind::AliasDecl:
      visitAliasDecl(node);
      break;
    case SyntaxKind::StandaloneImplDecl:
      visitStandaloneImplDecl(node);
      break;

    // Statements
    case SyntaxKind::LetStmt:
      visitLetStmt(node);
      break;
    case SyntaxKind::BlockStmt:
      visitBlockStmt(node);
      break;
    case SyntaxKind::IfStmt:
      visitIfStmt(node);
      break;
    case SyntaxKind::WhileStmt:
      visitWhileStmt(node);
      break;
    case SyntaxKind::ForStmt:
      visitForStmt(node);
      break;
    case SyntaxKind::ForInStatement:
      visitForInStatement(node);
      break;
    case SyntaxKind::DoWhileStatement:
      visitDoWhileStatement(node);
      break;
    case SyntaxKind::MatchStmt:
      visitMatchStmt(node);
      break;

    // Class/interface members
    case SyntaxKind::MethodDecl:
      visitCallableDecl(node, SymbolKind::Method);
      break;
    case SyntaxKind::ConstructorDecl:
      visitCallableDecl(node, SymbolKind::Constructor);
      break;
    case SyntaxKind::DestructorDecl:
      visitCallableDecl(node, SymbolKind::Destructor);
      break;
    case SyntaxKind::FieldDecl:
      visitFieldDecl(node);
      break;
    case SyntaxKind::ClassConstDecl:
      visitClassConstDecl(node);
      break;

    // Function parameters
    case SyntaxKind::FunctionParameterDecl:
      visitFunctionParameterDecl(node);
      break;
    case SyntaxKind::GenericTypeParam: {
      zc::StringPtr name = declName(node, kGenericTypeParamNameWord);
      if (name.size() > 0) {
        checkDuplicate(name, node, SymbolKind::TypeAlias);
        Symbol& sym = declareSymbol(name, node, SymbolKind::TypeAlias);
        sym.addFlag(SymbolFlags::TypeParameter);
      }
      visitChildren(node);
      break;
    }

    // Expressions that introduce scopes
    case SyntaxKind::LambdaExpression:
      visitLambdaExpression(node);
      break;
    case SyntaxKind::FunctionExpression:
      visitFunctionExpression(node);
      break;

    // Import/Export
    case SyntaxKind::ImportDeclaration:
      visitImportDeclaration(node);
      break;
    case SyntaxKind::ExportDeclaration:
      visitExportDeclaration(node);
      break;

    // For any other node, just visit its children
    default:
      visitChildren(node);
      break;
  }
}

void DeclCollector::visitChildren(NodeId node) {
  if (!node || !impl->tree.contains(node)) return;

  const Node& n = impl->tree.node(node);
  visitChildNodeIds(impl->tree, n, [this](NodeId child) { visitNode(child); });
}

// ============================================================================
// Top-level and module structure
// ============================================================================

void DeclCollector::visitSourceFile(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Bind the source file to the current scope (global scope).
  // Top-level declarations go directly into the global scope.
  bindScope(node, currentScope());

  // Visit the module declaration if present
  NodeId moduleNode(n.payload.words[kSourceFileModuleWord]);
  if (impl->tree.contains(moduleNode)) { visitNode(moduleNode); }

  // Visit all top-level statements
  NodeList stmts;
  stmts.first = n.payload.words[kSourceFileStatementsFirstWord];
  stmts.size = n.payload.words[kSourceFileStatementsSizeWord];

  for (NodeId child : impl->tree.list(stmts)) { visitNode(child); }
}

void DeclCollector::visitStatementListItem(NodeId node) {
  // A StatementListItem wraps an actual item with optional attributes.
  // We just need to visit the inner item.
  const Node& n = impl->tree.node(node);
  NodeId item(n.payload.words[kStatementListItemItemWord]);
  if (impl->tree.contains(item)) { visitNode(item); }
}

// ============================================================================
// Declarations
// ============================================================================

void DeclCollector::visitFunctionDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kFunctionDeclNameWord);

  // Check for duplicates before creating scope
  bool isDuplicate = checkDuplicate(name, node, SymbolKind::Function);

  // Declare the function symbol in the current scope
  Symbol& sym = declareSymbol(name, node, SymbolKind::Function);
  (void)sym;  // The symbol is already registered

  // Skip scope creation for duplicates to avoid name collision in parent scope
  if (isDuplicate) return;

  // Enter a function scope for the body
  Scope& funcScope = enterScope(Scope::Kind::Function, name);
  bindScope(node, funcScope);

  // Visit type parameters
  NodeId typeParams(n.payload.words[kFunctionDeclTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit parameters (this will declare ParameterSymbols)
  NodeId params(n.payload.words[kFunctionDeclParamsIdWord]);
  if (impl->tree.contains(params)) { visitNode(params); }

  // Visit the body
  NodeId body(n.payload.words[kFunctionDeclBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  leaveScope();
}

void DeclCollector::visitClassDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kClassDeclNameWord);

  // Check for duplicates before creating scope
  bool isDuplicate = checkDuplicate(name, node, SymbolKind::Class);

  // Declare the class symbol
  Symbol& sym = declareSymbol(name, node, SymbolKind::Class);
  (void)sym;

  // Skip scope creation for duplicates
  if (isDuplicate) return;

  // Enter a class scope
  Scope& classScope = enterScope(Scope::Kind::Class, name);
  bindScope(node, classScope);

  // Visit type parameters
  NodeId typeParams(n.payload.words[kClassDeclTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit the base type for name references.
  NodeId baseTy(n.payload.words[kClassDeclBaseTyWord]);
  if (impl->tree.contains(baseTy)) { visitNode(baseTy); }

  // Visit members
  NodeId members(n.payload.words[kClassDeclMembersIdWord]);
  if (impl->tree.contains(members)) { visitNode(members); }

  leaveScope();
}

void DeclCollector::visitStructDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kStructDeclNameWord);

  // Check for duplicates before creating scope
  bool isDuplicate = checkDuplicate(name, node, SymbolKind::Class);

  // Declare the struct as a ClassSymbol (structs and classes share infrastructure)
  Symbol& sym = declareSymbol(name, node, SymbolKind::Class);
  sym.addFlag(SymbolFlags::Class);  // Already set by declareSymbol for Class kind

  // Skip scope creation for duplicates
  if (isDuplicate) return;

  // Enter a class scope for the struct
  Scope& structScope = enterScope(Scope::Kind::Class, name);
  bindScope(node, structScope);

  // Visit type parameters
  NodeId typeParams(n.payload.words[kStructDeclTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit members
  NodeId members(n.payload.words[kStructDeclMembersIdWord]);
  if (impl->tree.contains(members)) { visitNode(members); }

  leaveScope();
}

void DeclCollector::visitInterfaceDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kInterfaceDeclNameWord);

  // Check for duplicates before creating scope
  bool isDuplicate = checkDuplicate(name, node, SymbolKind::Interface);

  // Declare the interface symbol
  Symbol& sym = declareSymbol(name, node, SymbolKind::Interface);
  (void)sym;

  // Skip scope creation for duplicates
  if (isDuplicate) return;

  // Enter an interface scope
  Scope& ifaceScope = enterScope(Scope::Kind::Interface, name);
  bindScope(node, ifaceScope);

  // Visit type parameters
  NodeId typeParams(n.payload.words[kInterfaceDeclTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit superinterface bounds
  NodeId ifaces(n.payload.words[kInterfaceDeclIfacesIdWord]);
  if (impl->tree.contains(ifaces)) { visitNode(ifaces); }

  // Visit members
  NodeId members(n.payload.words[kInterfaceDeclMembersIdWord]);
  if (impl->tree.contains(members)) { visitNode(members); }

  leaveScope();
}

void DeclCollector::visitEnumDeclaration(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kEnumDeclarationNameWord);

  // Check for duplicates before creating scope
  bool isDuplicate = checkDuplicate(name, node, SymbolKind::Enum);

  // Declare the enum symbol
  Symbol& sym = declareSymbol(name, node, SymbolKind::Enum);
  (void)sym;

  // Skip scope creation for duplicates
  if (isDuplicate) return;

  // Enter an enum scope
  Scope& enumScope = enterScope(Scope::Kind::Enum, name);
  bindScope(node, enumScope);

  // Visit type parameters
  NodeId typeParams(n.payload.words[kEnumDeclarationTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit variants (each variant becomes a symbol in the enum scope)
  NodeId variants(n.payload.words[kEnumDeclarationVariantsIdWord]);
  if (impl->tree.contains(variants)) {
    // The variants node is an EnumVariantList; visit its children
    const Node& varNode = impl->tree.node(variants);
    if (varNode.kind == SyntaxKind::EnumVariantList) {
      NodeList varList;
      varList.first = varNode.payload.words[kEnumVariantListVariantsFirstWord];
      varList.size = varNode.payload.words[kEnumVariantListVariantsSizeWord];
      for (NodeId variant : impl->tree.list(varList)) {
        // Each variant has a name we can extract
        const Node& v = impl->tree.node(variant);
        zc::StringPtr variantName;
        switch (v.kind) {
          case SyntaxKind::UnitVariant:
            variantName = impl->tree.ident(IdentId(v.payload.words[kUnitVariantNameWord]));
            break;
          case SyntaxKind::TupleVariant:
            variantName = impl->tree.ident(IdentId(v.payload.words[kTupleVariantNameWord]));
            break;
          default:
            continue;
        }
        if (variantName.size() > 0) {
          Symbol& caseSym = declareSymbol(variantName, variant, SymbolKind::Variable);
          caseSym.addFlag(SymbolFlags::Constant);
        }
        // Also visit the variant's children for any nested declarations
        visitNode(variant);
      }
    } else {
      visitNode(variants);
    }
  }

  leaveScope();
}

void DeclCollector::visitAliasDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kAliasDeclNameWord);

  // Check for duplicates before declaring
  bool isDuplicate = checkDuplicate(name, node, SymbolKind::TypeAlias);

  // Declare the type alias symbol
  Symbol& sym = declareSymbol(name, node, SymbolKind::TypeAlias);
  (void)sym;

  // Skip further processing for duplicates
  if (isDuplicate) return;

  // Visit type parameters
  NodeId typeParams(n.payload.words[kAliasDeclTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit the target type (no declarations to collect, but traverse for completeness)
  NodeId target(n.payload.words[kAliasDeclTargetWord]);
  if (impl->tree.contains(target)) { visitNode(target); }
}

void DeclCollector::visitStandaloneImplDecl(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Impl blocks don't declare a named symbol themselves (they attach to a type).
  // But they introduce a scope for their members.
  // We generate an anonymous scope name based on the "for" type.

  // Enter an impl scope (using Class kind since it holds members).
  Scope& implScope = enterScope(Scope::Kind::Class, zc::StringPtr{});
  bindScope(node, implScope);

  // Visit type parameters
  NodeId typeParams(n.payload.words[kStandaloneImplDeclTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit the "for" type (for name resolution later)
  NodeId forTy(n.payload.words[kStandaloneImplDeclForTyWord]);
  if (impl->tree.contains(forTy)) { visitNode(forTy); }

  // Visit interface list
  NodeId ifaces(n.payload.words[kStandaloneImplDeclIfacesIdWord]);
  if (impl->tree.contains(ifaces)) { visitNode(ifaces); }

  // Visit where clause
  NodeId whereClause(n.payload.words[kStandaloneImplDeclWhereWord]);
  if (impl->tree.contains(whereClause)) { visitNode(whereClause); }

  // Visit members
  NodeId members(n.payload.words[kStandaloneImplDeclMembersIdWord]);
  if (impl->tree.contains(members)) { visitNode(members); }

  leaveScope();
}

// ============================================================================
// Statements that introduce scopes or bindings
// ============================================================================

void DeclCollector::visitLetStmt(NodeId node) {
  const Node& n = impl->tree.node(node);
  auto kind = static_cast<BindingDeclarationKind>(n.payload.words[kLetStmtKindWord]);
  bool declarationIsMutable = kind == BindingDeclarationKind::Mut;
  bool declarationIsConst = kind == BindingDeclarationKind::Const;

  // Visit the declarator list (which will call visitVariableDeclarator)
  NodeId decls(n.payload.words[kLetStmtDeclarationsWord]);
  if (impl->tree.contains(decls)) {
    const Node& listNode = impl->tree.node(decls);
    if (listNode.kind == SyntaxKind::VariableDeclaratorList) {
      NodeList declList;
      declList.first = listNode.payload.words[kVariableDeclaratorListDeclsFirstWord];
      declList.size = listNode.payload.words[kVariableDeclaratorListDeclsSizeWord];
      for (NodeId decl : impl->tree.list(declList)) {
        visitVariableDeclarator(decl, declarationIsMutable, declarationIsConst);
      }
    } else {
      visitNode(decls);
    }
  }
}

void DeclCollector::visitVariableDeclarator(NodeId node, bool declarationIsMutable,
                                            bool declarationIsConst) {
  const Node& n = impl->tree.node(node);

  // The pattern may introduce names. For simple cases, the pattern is a
  // BindingPattern or IdentifierPattern that gives us the variable name.
  NodeId pattern(n.payload.words[kVariableDeclaratorPatternWord]);
  if (impl->tree.contains(pattern)) {
    const Node& pat = impl->tree.node(pattern);
    zc::StringPtr varName;
    bool patternIsMutable = false;

    switch (pat.kind) {
      case SyntaxKind::BindingPattern:
        varName = impl->tree.ident(IdentId(pat.payload.words[kBindingPatternNameWord]));
        patternIsMutable = pat.payload.words[kBindingPatternIsMutWord] != 0;
        break;
      case SyntaxKind::IdentifierPattern:
        varName = impl->tree.ident(IdentId(pat.payload.words[kIdentifierPatternNameWord]));
        break;
      default:
        // For complex patterns (tuple, struct, etc.), we'd need to destructure.
        // For now, just visit the pattern children recursively.
        visitNode(pattern);
        break;
    }

    if (varName.size() > 0) {
      // Check for duplicates before declaring
      bool isDuplicate = checkDuplicate(varName, node, SymbolKind::Variable);
      Symbol& sym = declareSymbol(varName, node, SymbolKind::Variable);
      sym.removeFlag(SymbolFlags::Mutable);
      sym.removeFlag(SymbolFlags::Immutable);
      sym.removeFlag(SymbolFlags::Constant);
      if (declarationIsConst) {
        sym.addFlag(SymbolFlags::Immutable | SymbolFlags::Constant);
      } else if (declarationIsMutable || patternIsMutable) {
        sym.addFlag(SymbolFlags::Mutable);
      } else {
        sym.addFlag(SymbolFlags::Immutable);
      }
      (void)isDuplicate;
    }
  }

  // Visit the type annotation
  NodeId ty(n.payload.words[kVariableDeclaratorTyWord]);
  if (impl->tree.contains(ty)) { visitNode(ty); }

  // Visit the initializer
  NodeId init(n.payload.words[kVariableDeclaratorInitWord]);
  if (impl->tree.contains(init)) { visitNode(init); }
}

void DeclCollector::visitBlockStmt(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Enter a block scope
  Scope& blockScope = enterScope(Scope::Kind::Block, zc::StringPtr{});
  bindScope(node, blockScope);

  // Visit all statements in the block
  NodeList stmts;
  stmts.first = n.payload.words[kBlockStmtStmtsFirstWord];
  stmts.size = n.payload.words[kBlockStmtStmtsSizeWord];

  for (NodeId stmt : impl->tree.list(stmts)) { visitNode(stmt); }

  leaveScope();
}

void DeclCollector::visitIfStmt(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Visit the condition (no new scope for the condition expression itself,
  // but some languages allow `if let` which introduces bindings)
  NodeId cond(n.payload.words[kIfStmtCondWord]);
  if (impl->tree.contains(cond)) { visitNode(cond); }

  // Then branch gets its own scope
  NodeId thenStmt(n.payload.words[kIfStmtThenStmtWord]);
  if (impl->tree.contains(thenStmt)) {
    Scope& thenScope = enterScope(Scope::Kind::If, "then");
    bindScope(node, thenScope);
    visitNode(thenStmt);
    leaveScope();
  }

  // Else branch gets its own scope
  NodeId elseStmt(n.payload.words[kIfStmtElseStmtWord]);
  if (impl->tree.contains(elseStmt)) {
    Scope& elseScope = enterScope(Scope::Kind::If, "else");
    (void)elseScope;
    visitNode(elseStmt);
    leaveScope();
  }
}

void DeclCollector::visitWhileStmt(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Enter a while scope
  Scope& whileScope = enterScope(Scope::Kind::While, "while");
  bindScope(node, whileScope);

  // Visit condition
  NodeId cond(n.payload.words[kWhileStmtCondWord]);
  if (impl->tree.contains(cond)) { visitNode(cond); }

  // Visit body
  NodeId body(n.payload.words[kWhileStmtBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  leaveScope();
}

void DeclCollector::visitForStmt(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Enter a for scope (the init, cond, update, and body all share this scope)
  Scope& forScope = enterScope(Scope::Kind::For, "for");
  bindScope(node, forScope);

  // Visit init (may declare variables, e.g. `for let i = 0; ...`)
  NodeId init(n.payload.words[kForStmtInitWord]);
  if (impl->tree.contains(init)) { visitNode(init); }

  // Visit condition
  NodeId cond(n.payload.words[kForStmtCondWord]);
  if (impl->tree.contains(cond)) { visitNode(cond); }

  // Visit update
  NodeId update(n.payload.words[kForStmtUpdateWord]);
  if (impl->tree.contains(update)) { visitNode(update); }

  // Visit body
  NodeId body(n.payload.words[kForStmtBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  leaveScope();
}

void DeclCollector::visitForInStatement(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Enter a for scope
  Scope& forScope = enterScope(Scope::Kind::For, "for_in");
  bindScope(node, forScope);

  // Visit the binding pattern (declares the loop variable)
  NodeId binding(n.payload.words[kForInStatementBindingWord]);
  if (impl->tree.contains(binding)) {
    // The binding is a Pattern; extract name from BindingPattern/IdentifierPattern
    const Node& pat = impl->tree.node(binding);
    zc::StringPtr varName;
    if (pat.kind == SyntaxKind::BindingPattern) {
      varName = impl->tree.ident(IdentId(pat.payload.words[kBindingPatternNameWord]));
    } else if (pat.kind == SyntaxKind::IdentifierPattern) {
      varName = impl->tree.ident(IdentId(pat.payload.words[kIdentifierPatternNameWord]));
    }
    if (varName.size() > 0) { declareSymbol(varName, binding, SymbolKind::Variable); }
    visitNode(binding);
  }

  // Visit the iterable expression
  NodeId expr(n.payload.words[kForInStatementExpressionWord]);
  if (impl->tree.contains(expr)) { visitNode(expr); }

  // Visit body
  NodeId body(n.payload.words[kForInStatementBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  leaveScope();
}

void DeclCollector::visitDoWhileStatement(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Enter a while scope
  Scope& doScope = enterScope(Scope::Kind::While, "do_while");
  bindScope(node, doScope);

  // Visit body
  NodeId body(n.payload.words[kDoWhileStatementBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  // Visit condition
  NodeId cond(n.payload.words[kDoWhileStatementCondWord]);
  if (impl->tree.contains(cond)) { visitNode(cond); }

  leaveScope();
}

void DeclCollector::visitMatchStmt(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Visit the scrutinee expression
  NodeId scrutinee(n.payload.words[kMatchStmtScrutineeWord]);
  if (impl->tree.contains(scrutinee)) { visitNode(scrutinee); }

  // Visit each match arm
  NodeList arms;
  arms.first = n.payload.words[kMatchStmtArmsFirstWord];
  arms.size = n.payload.words[kMatchStmtArmsSizeWord];

  for (NodeId arm : impl->tree.list(arms)) { visitMatchArmStmt(arm); }
}

void DeclCollector::visitMatchArmStmt(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Each match arm gets its own scope for pattern bindings
  Scope& armScope = enterScope(Scope::Kind::Block, "match_arm");
  bindScope(node, armScope);

  // Visit the pattern (may introduce bindings via BindingPattern)
  NodeId pattern(n.payload.words[kMatchArmStmtPatternWord]);
  if (impl->tree.contains(pattern)) {
    // For patterns that introduce names, we need to extract and declare them.
    // This is a simplified approach: we visit the pattern node, and if it's a
    // BindingPattern or IdentifierPattern, we extract the name.
    const Node& pat = impl->tree.node(pattern);
    if (pat.kind == SyntaxKind::BindingPattern) {
      zc::StringPtr name = impl->tree.ident(IdentId(pat.payload.words[kBindingPatternNameWord]));
      if (name.size() > 0) { declareSymbol(name, pattern, SymbolKind::Variable); }
    } else if (pat.kind == SyntaxKind::IdentifierPattern) {
      zc::StringPtr name = impl->tree.ident(IdentId(pat.payload.words[kIdentifierPatternNameWord]));
      if (name.size() > 0) { declareSymbol(name, pattern, SymbolKind::Variable); }
    }
    visitNode(pattern);
  }

  // Visit the guard expression
  NodeId guard(n.payload.words[kMatchArmStmtGuardWord]);
  if (impl->tree.contains(guard)) { visitNode(guard); }

  // Visit the body
  NodeId body(n.payload.words[kMatchArmStmtBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  leaveScope();
}

// ============================================================================
// Class/interface members
// ============================================================================

void DeclCollector::visitCallableDecl(NodeId node, SymbolKind symbolKind) {
  const Node& n = impl->tree.node(node);
  uint32_t visibilityWord = 0;
  uint32_t nameWord = 0;
  uint32_t paramsWord = 0;
  uint32_t typeParamsWord = 0;
  bool hasTypeParams = false;
  uint32_t signatureTypeWord = 0;
  uint32_t raisesTypeWord = 0;
  bool hasRaisesType = false;
  uint32_t bodyWord = 0;
  bool isStatic = false;

  switch (n.kind) {
    case SyntaxKind::MethodDecl:
      visibilityWord = n.payload.words[kMethodDeclVisibilityWord];
      nameWord = kMethodDeclNameWord;
      paramsWord = kMethodDeclParamsIdWord;
      typeParamsWord = kMethodDeclTypeParamsIdWord;
      hasTypeParams = true;
      signatureTypeWord = kMethodDeclRetTyWord;
      raisesTypeWord = kMethodDeclRaisesTyWord;
      hasRaisesType = true;
      bodyWord = kMethodDeclBodyWord;
      isStatic = n.payload.words[kMethodDeclIsStaticWord] != 0;
      break;
    case SyntaxKind::ConstructorDecl:
      visibilityWord = n.payload.words[kConstructorDeclVisibilityWord];
      nameWord = kConstructorDeclNameWord;
      paramsWord = kConstructorDeclParamsIdWord;
      signatureTypeWord = kConstructorDeclRaisesTyWord;
      bodyWord = kConstructorDeclBodyWord;
      break;
    case SyntaxKind::DestructorDecl:
      visibilityWord = n.payload.words[kDestructorDeclVisibilityWord];
      nameWord = kDestructorDeclNameWord;
      paramsWord = kDestructorDeclParamsIdWord;
      signatureTypeWord = kDestructorDeclRaisesTyWord;
      bodyWord = kDestructorDeclBodyWord;
      break;
    default:
      ZC_UNREACHABLE;
  }

  zc::StringPtr name = impl->tree.ident(IdentId(n.payload.words[nameWord]));

  Symbol& sym = declareSymbol(name, node, symbolKind);

  // Set visibility flag
  Visibility defaultVisibility = Visibility::Private;
  ZC_IF_SOME(scope, impl->scopes.getCurrentScope()) {
    if (scope.getKind() == Scope::Kind::Interface) { defaultVisibility = Visibility::Public; }
  }
  switch (visibilityFromAst(visibilityWord, defaultVisibility)) {
    case Visibility::Public:
      sym.addFlag(SymbolFlags::Public);
      break;
    case Visibility::Private:
      sym.addFlag(SymbolFlags::Private);
      break;
    case Visibility::Protected:
      sym.addFlag(SymbolFlags::Protected);
      break;
    case Visibility::Internal:
      // All current member defaults resolve to explicit public or private facts.
      break;
  }

  if (isStatic) { sym.addFlag(SymbolFlags::Static); }

  // Enter a function scope for the method body
  Scope& methodScope = enterScope(Scope::Kind::Function, name);
  bindScope(node, methodScope);

  // Visit parameters
  NodeId params(n.payload.words[paramsWord]);
  if (impl->tree.contains(params)) { visitNode(params); }

  if (hasTypeParams) {
    NodeId typeParams(n.payload.words[typeParamsWord]);
    if (impl->tree.contains(typeParams)) { visitNode(typeParams); }
  }

  // Visit the return type for methods or raises type for constructors/destructors.
  NodeId signatureType(n.payload.words[signatureTypeWord]);
  if (impl->tree.contains(signatureType)) { visitNode(signatureType); }

  if (hasRaisesType) {
    NodeId raisesType(n.payload.words[raisesTypeWord]);
    if (impl->tree.contains(raisesType)) { visitNode(raisesType); }
  }

  // Visit body
  NodeId body(n.payload.words[bodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  leaveScope();
}

void DeclCollector::visitFieldDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kFieldDeclNameWord);

  // Declare the field symbol
  Symbol& sym = declareSymbol(name, node, SymbolKind::Field);

  // Set mutability flag
  bool isMut = n.payload.words[kFieldDeclIsMutWord] != 0;
  if (isMut) {
    sym.addFlag(SymbolFlags::Mutable);
  } else {
    sym.addFlag(SymbolFlags::Immutable);
  }

  // Set static flag
  bool isStatic = n.payload.words[kFieldDeclIsStaticWord] != 0;
  if (isStatic) {
    sym.addFlag(SymbolFlags::Static);
  } else {
    sym.addFlag(SymbolFlags::Instance);
  }

  // Set visibility flag
  uint32_t visibilityWord = n.payload.words[kFieldDeclVisibilityWord];
  Visibility defaultVisibility = Visibility::Private;
  ZC_IF_SOME(scope, impl->scopes.getCurrentScope()) {
    if (scope.getKind() == Scope::Kind::Interface) { defaultVisibility = Visibility::Public; }
  }
  switch (visibilityFromAst(visibilityWord, defaultVisibility)) {
    case Visibility::Public:
      sym.addFlag(SymbolFlags::Public);
      break;
    case Visibility::Private:
      sym.addFlag(SymbolFlags::Private);
      break;
    case Visibility::Protected:
      sym.addFlag(SymbolFlags::Protected);
      break;
    case Visibility::Internal:
      break;
  }

  // Visit the type annotation
  NodeId ty(n.payload.words[kFieldDeclTyWord]);
  if (impl->tree.contains(ty)) { visitNode(ty); }

  // Visit the initializer
  NodeId init(n.payload.words[kFieldDeclInitWord]);
  if (impl->tree.contains(init)) { visitNode(init); }
}

void DeclCollector::visitClassConstDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kClassConstDeclNameWord);

  Symbol& sym = declareSymbol(name, node, SymbolKind::Constant);
  if (n.payload.words[kClassConstDeclIsStaticWord] != 0) { sym.addFlag(SymbolFlags::Static); }

  Visibility defaultVisibility = Visibility::Private;
  ZC_IF_SOME(scope, impl->scopes.getCurrentScope()) {
    if (scope.getKind() == Scope::Kind::Interface) { defaultVisibility = Visibility::Public; }
  }
  switch (visibilityFromAst(n.payload.words[kClassConstDeclVisibilityWord], defaultVisibility)) {
    case Visibility::Public:
      sym.addFlag(SymbolFlags::Public);
      break;
    case Visibility::Private:
      sym.addFlag(SymbolFlags::Private);
      break;
    case Visibility::Protected:
      sym.addFlag(SymbolFlags::Protected);
      break;
    case Visibility::Internal:
      break;
  }

  NodeId ty(n.payload.words[kClassConstDeclTyWord]);
  if (impl->tree.contains(ty)) { visitNode(ty); }
  NodeId init(n.payload.words[kClassConstDeclInitWord]);
  if (impl->tree.contains(init)) { visitNode(init); }
}

// ============================================================================
// Function parameters
// ============================================================================

void DeclCollector::visitFunctionParameterDecl(NodeId node) {
  const Node& n = impl->tree.node(node);
  zc::StringPtr name = declName(node, kFunctionParameterDeclNameWord);

  if (name.size() > 0) {
    // Declare the parameter symbol
    Symbol& sym = declareSymbol(name, node, SymbolKind::Parameter);
    (void)sym;
  }

  // Visit the type annotation
  NodeId ty(n.payload.words[kFunctionParameterDeclTyWord]);
  if (impl->tree.contains(ty)) { visitNode(ty); }

  // Visit the default value
  NodeId def(n.payload.words[kFunctionParameterDeclDefaultWord]);
  if (impl->tree.contains(def)) { visitNode(def); }

  // Visit attributes
  NodeId attrs(n.payload.words[kFunctionParameterDeclAttrsWord]);
  if (impl->tree.contains(attrs)) { visitNode(attrs); }
}

// ============================================================================
// Expressions that introduce scopes
// ============================================================================

void DeclCollector::visitLambdaExpression(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Lambdas introduce a new function scope
  Scope& lambdaScope = enterScope(Scope::Kind::Lambda, "lambda");
  bindScope(node, lambdaScope);

  // Visit parameters
  NodeId params(n.payload.words[kLambdaExpressionParamsIdWord]);
  if (impl->tree.contains(params)) { visitNode(params); }

  // Visit return type
  NodeId retTy(n.payload.words[kLambdaExpressionRetTyWord]);
  if (impl->tree.contains(retTy)) { visitNode(retTy); }

  // Visit raises type
  NodeId raisesTy(n.payload.words[kLambdaExpressionRaisesTyWord]);
  if (impl->tree.contains(raisesTy)) { visitNode(raisesTy); }

  // Visit body (block or expression)
  NodeId body(n.payload.words[kLambdaExpressionBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  // Visit expression body (if present)
  NodeId exprBody(n.payload.words[kLambdaExpressionExprBodyWord]);
  if (impl->tree.contains(exprBody)) { visitNode(exprBody); }

  leaveScope();
}

void DeclCollector::visitFunctionExpression(NodeId node) {
  const Node& n = impl->tree.node(node);

  // Function expressions (anonymous functions) introduce a scope
  Scope& funcScope = enterScope(Scope::Kind::Function, "anon_fn");
  bindScope(node, funcScope);

  // Visit type parameters
  NodeId typeParams(n.payload.words[kFunctionExpressionTypeParamsIdWord]);
  if (impl->tree.contains(typeParams)) { visitNode(typeParams); }

  // Visit parameters
  NodeId params(n.payload.words[kFunctionExpressionParamsIdWord]);
  if (impl->tree.contains(params)) { visitNode(params); }

  // Visit return type
  NodeId retTy(n.payload.words[kFunctionExpressionRetTyWord]);
  if (impl->tree.contains(retTy)) { visitNode(retTy); }

  // Visit raises type
  NodeId raisesTy(n.payload.words[kFunctionExpressionRaisesTyWord]);
  if (impl->tree.contains(raisesTy)) { visitNode(raisesTy); }

  // Visit body
  NodeId body(n.payload.words[kFunctionExpressionBodyWord]);
  if (impl->tree.contains(body)) { visitNode(body); }

  leaveScope();
}

// ============================================================================
// Import/Export (deferred to Phase 1.5)
// ============================================================================

void DeclCollector::visitImportDeclaration(NodeId node) {
  // Import declarations are deferred to Phase 1.5 (Import Resolution).
  // During Phase 1, we simply note their existence but do not create symbols.
  // The import specifiers will be resolved once we know what the imported
  // modules contain.
  //
  // We still visit children for completeness (e.g. the ModulePath).
  const Node& n = impl->tree.node(node);

  // Visit the import path
  NodeId path(n.payload.words[kImportDeclarationPathWord]);
  if (impl->tree.contains(path)) { visitNode(path); }

  // Visit specifiers (don't create symbols yet)
  NodeList specifiers;
  specifiers.first = n.payload.words[kImportDeclarationSpecifiersFirstWord];
  specifiers.size = n.payload.words[kImportDeclarationSpecifiersSizeWord];

  for (NodeId spec : impl->tree.list(specifiers)) { visitNode(spec); }

  // Mark this node as deferred in metadata
  impl->metadata.setIsDeferredMember(node, true);
}

void DeclCollector::visitExportDeclaration(NodeId node) {
  // Export declarations don't introduce new symbols; they re-export existing ones.
  // We just visit the inner declaration to collect its symbols normally.
  const Node& n = impl->tree.node(node);

  // Visit the inner declaration
  NodeId decl(n.payload.words[kExportDeclarationDeclarationWord]);
  if (impl->tree.contains(decl)) { visitNode(decl); }

  // Visit the export path (for `export * from "..."`)
  NodeId path(n.payload.words[kExportDeclarationPathWord]);
  if (impl->tree.contains(path)) { visitNode(path); }

  // Visit export specifiers
  NodeList specifiers;
  specifiers.first = n.payload.words[kExportDeclarationSpecifiersFirstWord];
  specifiers.size = n.payload.words[kExportDeclarationSpecifiersSizeWord];

  for (NodeId spec : impl->tree.list(specifiers)) { visitNode(spec); }
}

}  // namespace binder
}  // namespace compiler
}  // namespace zomlang
