// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/binder/definition-inventory.h"

#include "zomlang/compiler/ast/generated/node-traverse.h"

namespace zomlang::compiler::binder {
namespace {

zc::Vector<StructuralIdentityParent> cloneParents(
    zc::ArrayPtr<const StructuralIdentityParent> parents) {
  zc::Vector<StructuralIdentityParent> result(parents.size());
  result.addAll(parents);
  return result;
}

}  // namespace

struct DefinitionInventory::Impl final {
  zc::Vector<ModuleInventoryEntry> modules;
  zc::Vector<DefinitionInventoryEntry> definitions;
  zc::Vector<ImplInventoryEntry> impls;

  const ast::Tree* tree = nullptr;
  zc::Vector<StructuralIdentityParent> parents;
  ast::NodeId currentModule;

  void addDeclared(ast::NodeId node, identity::DefinitionKind kind, ast::IdentId name) {
    const auto& syntax = tree->node(node);
    definitions.add(DefinitionInventoryEntry{node, currentModule, kind,
                                             InventoryDefinitionNameKind::Declared, name, zc::none,
                                             syntax.range, cloneParents(parents.asPtr())});
  }

  void addAnonymous(ast::NodeId node, identity::AnonymousDefinitionRole role) {
    const auto& syntax = tree->node(node);
    definitions.add(DefinitionInventoryEntry{node, currentModule, identity::DefinitionKind::Closure,
                                             InventoryDefinitionNameKind::Anonymous, ast::IdentId(),
                                             zc::Maybe<identity::AnonymousDefinitionRole>(role),
                                             syntax.range, cloneParents(parents.asPtr())});
  }

  void visitDefinition(ast::NodeId node, identity::DefinitionKind kind, ast::IdentId name) {
    addDeclared(node, kind, name);
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Definition, node});
    visitChildren(node, false);
    parents.removeLast();
  }

  void visitClosure(ast::NodeId node, identity::AnonymousDefinitionRole role) {
    addAnonymous(node, role);
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Definition, node});
    visitChildren(node, false);
    parents.removeLast();
  }

  void visitImpl(ast::NodeId node) {
    const auto& syntax = tree->node(node);
    impls.add(ImplInventoryEntry{node, currentModule, syntax.range, cloneParents(parents.asPtr())});
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Impl, node});
    visitChildren(node, false);
    parents.removeLast();
  }

  void collectPatternBindings(ast::NodeId node, identity::DefinitionKind kind) {
    if (!tree->contains(node)) { return; }
    const auto& syntax = tree->node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::RestPattern: {
        ast::IdentId name(syntax.payload.words[ast::kRestPatternBindingWord]);
        if (name) { addDeclared(node, kind, name); }
        return;
      }
      case ast::SyntaxKind::BindingPattern: {
        addDeclared(node, kind, ast::IdentId(syntax.payload.words[ast::kBindingPatternNameWord]));
        collectPatternBindings(ast::NodeId(syntax.payload.words[ast::kBindingPatternSubWord]),
                               kind);
        return;
      }
      case ast::SyntaxKind::IdentifierPattern:
        addDeclared(node, kind,
                    ast::IdentId(syntax.payload.words[ast::kIdentifierPatternNameWord]));
        return;
      case ast::SyntaxKind::PatternProperty: {
        const bool shortForm = syntax.payload.words[ast::kPatternPropertyShortFormWord] != 0;
        if (shortForm) {
          addDeclared(node, kind,
                      ast::IdentId(syntax.payload.words[ast::kPatternPropertyNameWord]));
        } else {
          collectPatternBindings(ast::NodeId(syntax.payload.words[ast::kPatternPropertyPatWord]),
                                 kind);
        }
        return;
      }
      case ast::SyntaxKind::TuplePattern:
      case ast::SyntaxKind::StructPattern:
      case ast::SyntaxKind::ArrayPattern:
      case ast::SyntaxKind::EnumPattern:
        ast::visitChildNodeIds(*tree, syntax, [this, kind](ast::NodeId child) {
          collectPatternBindings(child, kind);
        });
        return;
      default:
        return;
    }
  }

  void visitLet(ast::NodeId node, bool moduleScope) {
    const auto& syntax = tree->node(node);
    const auto declarationKind =
        static_cast<ast::BindingDeclarationKind>(syntax.payload.words[ast::kLetStmtKindWord]);
    identity::DefinitionKind kind = identity::DefinitionKind::Local;
    if (moduleScope) {
      kind = declarationKind == ast::BindingDeclarationKind::Const
                 ? identity::DefinitionKind::Constant
                 : identity::DefinitionKind::Static;
    }

    const ast::NodeId declarations(syntax.payload.words[ast::kLetStmtDeclarationsWord]);
    if (tree->contains(declarations)) {
      const auto& list = tree->node(declarations);
      ast::NodeList declarators{list.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
                                list.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
      for (ast::NodeId declarator : tree->list(declarators)) {
        const auto& declaration = tree->node(declarator);
        collectPatternBindings(
            ast::NodeId(declaration.payload.words[ast::kVariableDeclaratorPatternWord]), kind);
      }
    }
    visitChildren(node, moduleScope);
  }

  ast::IdentId lastModulePathSegment(ast::NodeId pathNode) const {
    if (!tree->contains(pathNode)) { return ast::IdentId(); }
    const auto& path = tree->node(pathNode);
    if (path.kind != ast::SyntaxKind::ModulePath) { return ast::IdentId(); }
    ast::IdentList segments{path.payload.words[ast::kModulePathSegmentsFirstWord],
                            path.payload.words[ast::kModulePathSegmentsSizeWord]};
    const auto values = tree->identList(segments);
    return values.size() == 0 ? ast::IdentId() : values.back();
  }

  void visitImport(ast::NodeId node, bool moduleScope) {
    const auto& syntax = tree->node(node);
    ast::NodeList specifiers{syntax.payload.words[ast::kImportDeclarationSpecifiersFirstWord],
                             syntax.payload.words[ast::kImportDeclarationSpecifiersSizeWord]};
    if (specifiers.empty()) {
      ast::IdentId name(syntax.payload.words[ast::kImportDeclarationAliasWord]);
      if (!name) {
        name = lastModulePathSegment(
            ast::NodeId(syntax.payload.words[ast::kImportDeclarationPathWord]));
      }
      if (name) { addDeclared(node, identity::DefinitionKind::ImportAlias, name); }
    }
    visitChildren(node, moduleScope);
  }

  void visitExport(ast::NodeId node, bool moduleScope) {
    const auto& syntax = tree->node(node);
    const ast::NodeId declaration(syntax.payload.words[ast::kExportDeclarationDeclarationWord]);
    ast::NodeList specifiers{syntax.payload.words[ast::kExportDeclarationSpecifiersFirstWord],
                             syntax.payload.words[ast::kExportDeclarationSpecifiersSizeWord]};
    const ast::NodeId path(syntax.payload.words[ast::kExportDeclarationPathWord]);
    if (!tree->contains(declaration) && specifiers.empty() && tree->contains(path)) {
      const ast::IdentId name = lastModulePathSegment(path);
      if (name) { addDeclared(node, identity::DefinitionKind::ReexportAlias, name); }
    }
    visitChildren(node, moduleScope);
  }

  void visitModule(ast::NodeId node) {
    const auto& syntax = tree->node(node);
    const auto form = static_cast<ast::ModuleDeclarationForm>(
        syntax.payload.words[ast::kModuleDeclarationFormWord]);
    const ast::IdentId name(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord]);
    if (form == ast::ModuleDeclarationForm::Alias) {
      addDeclared(node, identity::DefinitionKind::ModuleAlias, name);
      visitChildren(node, true);
      return;
    }

    modules.add(ModuleInventoryEntry{node, currentModule, form, name, syntax.range});
    const ast::NodeId savedModule = currentModule;
    auto savedParents = zc::mv(parents);
    parents = zc::Vector<StructuralIdentityParent>();
    currentModule = node;
    visitChildren(node, true);
    currentModule = savedModule;
    parents = zc::mv(savedParents);
  }

  void visitSourceFile(ast::NodeId node) {
    const auto& syntax = tree->node(node);
    const ast::NodeId module(syntax.payload.words[ast::kSourceFileModuleWord]);
    ast::NodeId sourceModule;
    if (tree->contains(module)) {
      const auto& moduleSyntax = tree->node(module);
      const auto form = static_cast<ast::ModuleDeclarationForm>(
          moduleSyntax.payload.words[ast::kModuleDeclarationFormWord]);
      visitNode(module, true);
      if (form == ast::ModuleDeclarationForm::RootDeclaration) { sourceModule = module; }
    }

    const ast::NodeId savedModule = currentModule;
    currentModule = sourceModule;
    ast::NodeList statements{syntax.payload.words[ast::kSourceFileStatementsFirstWord],
                             syntax.payload.words[ast::kSourceFileStatementsSizeWord]};
    for (ast::NodeId statement : tree->list(statements)) { visitNode(statement, true); }
    currentModule = savedModule;
  }

  void visitChildren(ast::NodeId node, bool moduleScope) {
    ast::visitChildNodeIds(*tree, tree->node(node), [this, moduleScope](ast::NodeId child) {
      visitNode(child, moduleScope);
    });
  }

  void visitNode(ast::NodeId node, bool moduleScope) {
    if (!tree->contains(node)) { return; }
    const auto& syntax = tree->node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::SourceFile:
        visitSourceFile(node);
        return;
      case ast::SyntaxKind::ModuleDeclaration:
        visitModule(node);
        return;
      case ast::SyntaxKind::ExternDecl:
        visitDefinition(node, identity::DefinitionKind::Function,
                        ast::IdentId(syntax.payload.words[ast::kExternDeclNameWord]));
        return;
      case ast::SyntaxKind::ExternVarDecl:
        visitDefinition(node, identity::DefinitionKind::Static,
                        ast::IdentId(syntax.payload.words[ast::kExternVarDeclNameWord]));
        return;
      case ast::SyntaxKind::UnitVariant:
        visitDefinition(node, identity::DefinitionKind::EnumVariant,
                        ast::IdentId(syntax.payload.words[ast::kUnitVariantNameWord]));
        return;
      case ast::SyntaxKind::TupleVariant:
        visitDefinition(node, identity::DefinitionKind::EnumVariant,
                        ast::IdentId(syntax.payload.words[ast::kTupleVariantNameWord]));
        return;
      case ast::SyntaxKind::EnumDeclaration:
        visitDefinition(node, identity::DefinitionKind::Enum,
                        ast::IdentId(syntax.payload.words[ast::kEnumDeclarationNameWord]));
        return;
      case ast::SyntaxKind::FunctionDecl:
        visitDefinition(node, identity::DefinitionKind::Function,
                        ast::IdentId(syntax.payload.words[ast::kFunctionDeclNameWord]));
        return;
      case ast::SyntaxKind::ClassDecl:
        visitDefinition(node, identity::DefinitionKind::Class,
                        ast::IdentId(syntax.payload.words[ast::kClassDeclNameWord]));
        return;
      case ast::SyntaxKind::StructDecl:
        visitDefinition(node, identity::DefinitionKind::Struct,
                        ast::IdentId(syntax.payload.words[ast::kStructDeclNameWord]));
        return;
      case ast::SyntaxKind::FunctionParameterDecl:
        visitDefinition(node, identity::DefinitionKind::Parameter,
                        ast::IdentId(syntax.payload.words[ast::kFunctionParameterDeclNameWord]));
        return;
      case ast::SyntaxKind::InterfaceDecl:
        visitDefinition(node, identity::DefinitionKind::Interface,
                        ast::IdentId(syntax.payload.words[ast::kInterfaceDeclNameWord]));
        return;
      case ast::SyntaxKind::ErrorDecl:
        visitDefinition(node, identity::DefinitionKind::Error,
                        ast::IdentId(syntax.payload.words[ast::kErrorDeclNameWord]));
        return;
      case ast::SyntaxKind::AliasDecl:
        visitDefinition(node, identity::DefinitionKind::TypeAlias,
                        ast::IdentId(syntax.payload.words[ast::kAliasDeclNameWord]));
        return;
      case ast::SyntaxKind::MethodDecl:
        visitDefinition(node, identity::DefinitionKind::Method,
                        ast::IdentId(syntax.payload.words[ast::kMethodDeclNameWord]));
        return;
      case ast::SyntaxKind::FieldDecl:
        visitDefinition(node, identity::DefinitionKind::Field,
                        ast::IdentId(syntax.payload.words[ast::kFieldDeclNameWord]));
        return;
      case ast::SyntaxKind::AssociatedTypeDecl:
        visitDefinition(node, identity::DefinitionKind::AssociatedType,
                        ast::IdentId(syntax.payload.words[ast::kAssociatedTypeDeclNameWord]));
        return;
      case ast::SyntaxKind::GenericTypeParam:
        visitDefinition(node, identity::DefinitionKind::TypeParameter,
                        ast::IdentId(syntax.payload.words[ast::kGenericTypeParamNameWord]));
        return;
      case ast::SyntaxKind::ConstructorDecl:
        visitDefinition(node, identity::DefinitionKind::Constructor,
                        ast::IdentId(syntax.payload.words[ast::kConstructorDeclNameWord]));
        return;
      case ast::SyntaxKind::DestructorDecl:
        visitDefinition(node, identity::DefinitionKind::Destructor,
                        ast::IdentId(syntax.payload.words[ast::kDestructorDeclNameWord]));
        return;
      case ast::SyntaxKind::ClassConstDecl:
        visitDefinition(node, identity::DefinitionKind::Constant,
                        ast::IdentId(syntax.payload.words[ast::kClassConstDeclNameWord]));
        return;
      case ast::SyntaxKind::StandaloneImplDecl:
      case ast::SyntaxKind::MarkerImpl:
        visitImpl(node);
        return;
      case ast::SyntaxKind::LetStmt:
        visitLet(node, moduleScope);
        return;
      case ast::SyntaxKind::ForInStatement:
        collectPatternBindings(ast::NodeId(syntax.payload.words[ast::kForInStatementBindingWord]),
                               identity::DefinitionKind::PatternBinding);
        visitChildren(node, false);
        return;
      case ast::SyntaxKind::MatchArmStmt:
        collectPatternBindings(ast::NodeId(syntax.payload.words[ast::kMatchArmStmtPatternWord]),
                               identity::DefinitionKind::PatternBinding);
        visitChildren(node, false);
        return;
      case ast::SyntaxKind::FunctionExpression:
        visitClosure(node, identity::AnonymousDefinitionRole::FunctionExpression);
        return;
      case ast::SyntaxKind::LambdaExpression:
        visitClosure(node, identity::AnonymousDefinitionRole::Lambda);
        return;
      case ast::SyntaxKind::ImportDeclaration:
        visitImport(node, moduleScope);
        return;
      case ast::SyntaxKind::ImportSpecifier: {
        ast::IdentId name(syntax.payload.words[ast::kImportSpecifierAliasWord]);
        if (!name) { name = ast::IdentId(syntax.payload.words[ast::kImportSpecifierNameWord]); }
        visitDefinition(node, identity::DefinitionKind::ImportAlias, name);
        return;
      }
      case ast::SyntaxKind::ExportDeclaration:
        visitExport(node, moduleScope);
        return;
      case ast::SyntaxKind::ExportSpecifier: {
        ast::IdentId name(syntax.payload.words[ast::kExportSpecifierAliasWord]);
        if (!name) { name = ast::IdentId(syntax.payload.words[ast::kExportSpecifierNameWord]); }
        visitDefinition(node, identity::DefinitionKind::ReexportAlias, name);
        return;
      }
      default:
        visitChildren(node, moduleScope);
        return;
    }
  }

  void collect(const ast::Tree& input) {
    tree = &input;
    if (input.contains(input.root())) { visitNode(input.root(), true); }
    tree = nullptr;
  }
};

DefinitionInventory::DefinitionInventory() noexcept : impl(zc::heap<Impl>()) {}

DefinitionInventory::~DefinitionInventory() noexcept(false) = default;

DefinitionInventory::DefinitionInventory(DefinitionInventory&&) noexcept = default;

DefinitionInventory& DefinitionInventory::operator=(DefinitionInventory&&) noexcept = default;

DefinitionInventory DefinitionInventory::collect(const ast::Tree& tree) {
  DefinitionInventory result;
  result.impl->collect(tree);
  return result;
}

DefinitionInventory DefinitionInventory::clone() const {
  DefinitionInventory result;
  for (const auto& module : impl->modules) {
    result.impl->modules.add(ModuleInventoryEntry{module.node, module.parentModuleNode, module.form,
                                                  module.declaredName, module.source});
  }
  for (const auto& definition : impl->definitions) {
    result.impl->definitions.add(DefinitionInventoryEntry{
        definition.node, definition.moduleNode, definition.kind, definition.nameKind,
        definition.declaredName, definition.anonymousRole, definition.source,
        cloneParents(definition.parentPath.asPtr())});
  }
  for (const auto& implementation : impl->impls) {
    result.impl->impls.add(ImplInventoryEntry{implementation.node, implementation.moduleNode,
                                              implementation.source,
                                              cloneParents(implementation.parentPath.asPtr())});
  }
  return result;
}

zc::ArrayPtr<const ModuleInventoryEntry> DefinitionInventory::modules() const {
  return impl->modules.asPtr();
}

zc::ArrayPtr<const DefinitionInventoryEntry> DefinitionInventory::definitions() const {
  return impl->definitions.asPtr();
}

zc::ArrayPtr<const ImplInventoryEntry> DefinitionInventory::impls() const {
  return impl->impls.asPtr();
}

}  // namespace zomlang::compiler::binder
