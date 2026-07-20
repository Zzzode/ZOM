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

enum class DefinitionPlacement : uint8_t { Lexical, ModuleItem };

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
  zc::Vector<DefinitionInventoryEntry> genericParameters;
  zc::Vector<DefinitionInventoryEntry> callableParameters;
  zc::Vector<DefinitionInventoryEntry> ownerLocalBindings;
  zc::Vector<DefinitionInventoryEntry> anonymousEntities;
  zc::Vector<ImplInventoryEntry> impls;

  zc::Maybe<const ast::Tree&> tree;
  zc::Vector<StructuralIdentityParent> parents;
  ast::NodeId currentModule;

  const ast::Tree& syntaxTree() const {
    ZC_IF_SOME(value, tree) { return value; }
    ZC_UNREACHABLE;
  }

  void addByDomain(DefinitionInventoryEntry&& entry) {
    switch (entry.kind) {
      case identity::DefinitionKind::TypeParameter:
        genericParameters.add(zc::mv(entry));
        return;
      case identity::DefinitionKind::Parameter:
        callableParameters.add(zc::mv(entry));
        return;
      case identity::DefinitionKind::Local:
      case identity::DefinitionKind::PatternBinding:
        ownerLocalBindings.add(zc::mv(entry));
        return;
      case identity::DefinitionKind::Closure:
        anonymousEntities.add(zc::mv(entry));
        return;
      default:
        if (identity::isStableDefinitionKind(entry.kind)) {
          definitions.add(zc::mv(entry));
          return;
        }
        ZC_UNREACHABLE
    }
  }

  void addDeclared(ast::NodeId node, identity::DefinitionKind kind, ast::IdentId name) {
    const auto& syntax = syntaxTree().node(node);
    addByDomain(DefinitionInventoryEntry{node, DefinitionSite::declaration(node), currentModule,
                                         kind, InventoryDefinitionNameKind::Declared, name,
                                         zc::none, syntax.range, cloneParents(parents.asPtr())});
  }

  void addPatternBinding(ast::NodeId node, ast::NodeId introducer,
                         zc::ArrayPtr<const uint32_t> patternPath, identity::DefinitionKind kind,
                         ast::IdentId name) {
    const auto& syntax = syntaxTree().node(node);
    zc::Vector<uint32_t> path(patternPath.size());
    path.addAll(patternPath);
    addByDomain(DefinitionInventoryEntry{node, DefinitionSite::pattern(introducer, zc::mv(path)),
                                         currentModule, kind, InventoryDefinitionNameKind::Declared,
                                         name, zc::none, syntax.range,
                                         cloneParents(parents.asPtr())});
  }

  void addAnonymous(ast::NodeId node, AnonymousSyntaxRole role) {
    const auto& syntax = syntaxTree().node(node);
    addByDomain(DefinitionInventoryEntry{
        node, DefinitionSite::declaration(node), currentModule, identity::DefinitionKind::Closure,
        InventoryDefinitionNameKind::Anonymous, ast::IdentId(),
        zc::Maybe<AnonymousSyntaxRole>(role), syntax.range, cloneParents(parents.asPtr())});
  }

  void visitDefinition(ast::NodeId node, identity::DefinitionKind kind, ast::IdentId name) {
    addDeclared(node, kind, name);
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Definition, node});
    visitChildren(node, DefinitionPlacement::Lexical);
    parents.removeLast();
  }

  void visitClosure(ast::NodeId node, AnonymousSyntaxRole role) {
    addAnonymous(node, role);
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Definition, node});
    visitChildren(node, DefinitionPlacement::Lexical);
    parents.removeLast();
  }

  void visitImpl(ast::NodeId node) {
    const auto& syntax = syntaxTree().node(node);
    impls.add(ImplInventoryEntry{node, currentModule, syntax.range, cloneParents(parents.asPtr())});
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Impl, node});
    visitChildren(node, DefinitionPlacement::Lexical);
    parents.removeLast();
  }

  void collectPatternBindings(ast::NodeId node, ast::NodeId introducer, zc::Vector<uint32_t>& path,
                              identity::DefinitionKind kind) {
    if (!syntaxTree().contains(node)) { return; }
    const auto& syntax = syntaxTree().node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::RestPattern: {
        ast::IdentId name(syntax.payload.words[ast::kRestPatternBindingWord]);
        if (name) { addPatternBinding(node, introducer, path.asPtr(), kind, name); }
        return;
      }
      case ast::SyntaxKind::BindingPattern: {
        addPatternBinding(node, introducer, path.asPtr(), kind,
                          ast::IdentId(syntax.payload.words[ast::kBindingPatternNameWord]));
        const ast::NodeId sub(syntax.payload.words[ast::kBindingPatternSubWord]);
        if (syntaxTree().contains(sub)) {
          path.add(3);
          collectPatternBindings(sub, introducer, path, kind);
          path.removeLast();
        }
        return;
      }
      case ast::SyntaxKind::IdentifierPattern:
        addPatternBinding(node, introducer, path.asPtr(), kind,
                          ast::IdentId(syntax.payload.words[ast::kIdentifierPatternNameWord]));
        return;
      case ast::SyntaxKind::PatternProperty: {
        const bool shortForm = syntax.payload.words[ast::kPatternPropertyShortFormWord] != 0;
        if (shortForm) {
          addPatternBinding(node, introducer, path.asPtr(), kind,
                            ast::IdentId(syntax.payload.words[ast::kPatternPropertyNameWord]));
        } else {
          path.add(2);
          collectPatternBindings(ast::NodeId(syntax.payload.words[ast::kPatternPropertyPatWord]),
                                 introducer, path, kind);
          path.removeLast();
        }
        return;
      }
      case ast::SyntaxKind::TuplePattern: {
        ast::NodeList elements{syntax.payload.words[ast::kTuplePatternPatsFirstWord],
                               syntax.payload.words[ast::kTuplePatternPatsSizeWord]};
        uint32_t index = 0;
        for (ast::NodeId child : syntaxTree().list(elements)) {
          path.add(0);
          path.add(index++);
          collectPatternBindings(child, introducer, path, kind);
          path.removeLast();
          path.removeLast();
        }
        return;
      }
      case ast::SyntaxKind::StructPattern: {
        ast::NodeList fields{syntax.payload.words[ast::kStructPatternFieldsFirstWord],
                             syntax.payload.words[ast::kStructPatternFieldsSizeWord]};
        uint32_t index = 0;
        for (ast::NodeId child : syntaxTree().list(fields)) {
          path.add(1);
          path.add(index++);
          collectPatternBindings(child, introducer, path, kind);
          path.removeLast();
          path.removeLast();
        }
        const ast::NodeId rest(syntax.payload.words[ast::kStructPatternRestWord]);
        if (syntaxTree().contains(rest)) {
          path.add(2);
          collectPatternBindings(rest, introducer, path, kind);
          path.removeLast();
        }
        return;
      }
      case ast::SyntaxKind::ArrayPattern: {
        ast::NodeList elements{syntax.payload.words[ast::kArrayPatternPatsFirstWord],
                               syntax.payload.words[ast::kArrayPatternPatsSizeWord]};
        uint32_t index = 0;
        for (ast::NodeId child : syntaxTree().list(elements)) {
          path.add(0);
          path.add(index++);
          collectPatternBindings(child, introducer, path, kind);
          path.removeLast();
          path.removeLast();
        }
        const ast::NodeId rest(syntax.payload.words[ast::kArrayPatternRestWord]);
        if (syntaxTree().contains(rest)) {
          path.add(1);
          collectPatternBindings(rest, introducer, path, kind);
          path.removeLast();
        }
        return;
      }
      case ast::SyntaxKind::EnumPattern: {
        ast::NodeList arguments{syntax.payload.words[ast::kEnumPatternArgsFirstWord],
                                syntax.payload.words[ast::kEnumPatternArgsSizeWord]};
        uint32_t index = 0;
        for (ast::NodeId child : syntaxTree().list(arguments)) {
          path.add(1);
          path.add(index++);
          collectPatternBindings(child, introducer, path, kind);
          path.removeLast();
          path.removeLast();
        }
        return;
      }
      default:
        return;
    }
  }

  void visitLet(ast::NodeId node, DefinitionPlacement placement) {
    const auto& syntax = syntaxTree().node(node);
    const auto declarationKind =
        static_cast<ast::BindingDeclarationKind>(syntax.payload.words[ast::kLetStmtKindWord]);
    identity::DefinitionKind kind = identity::DefinitionKind::Local;
    if (placement == DefinitionPlacement::ModuleItem) {
      kind = declarationKind == ast::BindingDeclarationKind::Const
                 ? identity::DefinitionKind::Constant
                 : identity::DefinitionKind::Static;
    }

    const ast::NodeId declarations(syntax.payload.words[ast::kLetStmtDeclarationsWord]);
    if (syntaxTree().contains(declarations)) {
      const auto& list = syntaxTree().node(declarations);
      ast::NodeList declarators{list.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
                                list.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
      for (ast::NodeId declarator : syntaxTree().list(declarators)) {
        const auto& declaration = syntaxTree().node(declarator);
        zc::Vector<uint32_t> path;
        collectPatternBindings(
            ast::NodeId(declaration.payload.words[ast::kVariableDeclaratorPatternWord]), declarator,
            path, kind);
      }
    }
    visitChildren(node, DefinitionPlacement::Lexical);
  }

  void visitModule(ast::NodeId node) {
    const auto& syntax = syntaxTree().node(node);
    const auto form = static_cast<ast::ModuleDeclarationForm>(
        syntax.payload.words[ast::kModuleDeclarationFormWord]);
    const ast::IdentId name(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord]);
    if (form == ast::ModuleDeclarationForm::Alias) {
      addDeclared(node, identity::DefinitionKind::ModuleAlias, name);
      visitChildren(node, DefinitionPlacement::ModuleItem);
      return;
    }

    modules.add(ModuleInventoryEntry{node, currentModule, form, name, syntax.range});
    const ast::NodeId savedModule = currentModule;
    auto savedParents = zc::mv(parents);
    parents = zc::Vector<StructuralIdentityParent>();
    currentModule = node;
    visitChildren(node, DefinitionPlacement::ModuleItem);
    currentModule = savedModule;
    parents = zc::mv(savedParents);
  }

  void visitSourceFile(ast::NodeId node) {
    const auto& syntax = syntaxTree().node(node);
    const ast::NodeId module(syntax.payload.words[ast::kSourceFileModuleWord]);
    ast::NodeId sourceModule;
    if (syntaxTree().contains(module)) {
      const auto& moduleSyntax = syntaxTree().node(module);
      const auto form = static_cast<ast::ModuleDeclarationForm>(
          moduleSyntax.payload.words[ast::kModuleDeclarationFormWord]);
      visitNode(module, DefinitionPlacement::ModuleItem);
      if (form == ast::ModuleDeclarationForm::RootDeclaration) { sourceModule = module; }
    }

    const ast::NodeId savedModule = currentModule;
    currentModule = sourceModule;
    ast::NodeList statements{syntax.payload.words[ast::kSourceFileStatementsFirstWord],
                             syntax.payload.words[ast::kSourceFileStatementsSizeWord]};
    for (ast::NodeId statement : syntaxTree().list(statements)) {
      visitNode(statement, DefinitionPlacement::ModuleItem);
    }
    currentModule = savedModule;
  }

  void visitChildren(ast::NodeId node, DefinitionPlacement placement) {
    ast::visitChildNodeIds(syntaxTree(), syntaxTree().node(node),
                           [this, placement](ast::NodeId child) { visitNode(child, placement); });
  }

  void visitNode(ast::NodeId node, DefinitionPlacement placement) {
    if (!syntaxTree().contains(node)) { return; }
    const auto& syntax = syntaxTree().node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::SourceFile:
        visitSourceFile(node);
        return;
      case ast::SyntaxKind::StatementListItem:
        visitChildren(node, placement);
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
        visitLet(node, placement);
        return;
      case ast::SyntaxKind::ForInStatement: {
        zc::Vector<uint32_t> path;
        collectPatternBindings(ast::NodeId(syntax.payload.words[ast::kForInStatementBindingWord]),
                               node, path, identity::DefinitionKind::PatternBinding);
        visitChildren(node, DefinitionPlacement::Lexical);
        return;
      }
      case ast::SyntaxKind::MatchArmStmt: {
        zc::Vector<uint32_t> path;
        collectPatternBindings(ast::NodeId(syntax.payload.words[ast::kMatchArmStmtPatternWord]),
                               node, path, identity::DefinitionKind::PatternBinding);
        visitChildren(node, DefinitionPlacement::Lexical);
        return;
      }
      case ast::SyntaxKind::FunctionExpression:
        visitClosure(node, AnonymousSyntaxRole::FunctionExpression);
        return;
      case ast::SyntaxKind::LambdaExpression:
        visitClosure(node, AnonymousSyntaxRole::Lambda);
        return;
      case ast::SyntaxKind::ImportDeclaration:
      case ast::SyntaxKind::ExportDeclaration:
      case ast::SyntaxKind::ImportSpecifier:
      case ast::SyntaxKind::ExportSpecifier:
        visitChildren(node, placement);
        return;
      default:
        visitChildren(node, DefinitionPlacement::Lexical);
        return;
    }
  }

  void collect(const ast::Tree& input) {
    tree = input;
    if (input.contains(input.root())) { visitNode(input.root(), DefinitionPlacement::ModuleItem); }
    tree = zc::none;
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
  const auto cloneEntries = [](zc::ArrayPtr<const DefinitionInventoryEntry> source,
                               zc::Vector<DefinitionInventoryEntry>& destination) {
    for (const auto& definition : source) {
      destination.add(DefinitionInventoryEntry{
          definition.node, definition.site.clone(), definition.moduleNode, definition.kind,
          definition.nameKind, definition.declaredName, definition.anonymousRole, definition.source,
          cloneParents(definition.parentPath.asPtr())});
    }
  };
  cloneEntries(impl->definitions.asPtr(), result.impl->definitions);
  cloneEntries(impl->genericParameters.asPtr(), result.impl->genericParameters);
  cloneEntries(impl->callableParameters.asPtr(), result.impl->callableParameters);
  cloneEntries(impl->ownerLocalBindings.asPtr(), result.impl->ownerLocalBindings);
  cloneEntries(impl->anonymousEntities.asPtr(), result.impl->anonymousEntities);
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

zc::ArrayPtr<const DefinitionInventoryEntry> DefinitionInventory::genericParameters() const {
  return impl->genericParameters.asPtr();
}

zc::ArrayPtr<const DefinitionInventoryEntry> DefinitionInventory::callableParameters() const {
  return impl->callableParameters.asPtr();
}

zc::ArrayPtr<const DefinitionInventoryEntry> DefinitionInventory::ownerLocalBindings() const {
  return impl->ownerLocalBindings.asPtr();
}

zc::ArrayPtr<const DefinitionInventoryEntry> DefinitionInventory::anonymousEntities() const {
  return impl->anonymousEntities.asPtr();
}

zc::ArrayPtr<const ImplInventoryEntry> DefinitionInventory::impls() const {
  return impl->impls.asPtr();
}

}  // namespace zomlang::compiler::binder
