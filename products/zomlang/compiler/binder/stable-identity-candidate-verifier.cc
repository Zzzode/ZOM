// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable-identity-candidate-verifier.h"

#include "zc/core/string.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/canonical-header-verifier.h"
#include "zomlang/compiler/binder/definition-inventory.h"

namespace zomlang::compiler::binder {
namespace {

struct StableReference final {
  bool implementation;
  ast::NodeId node;
  zc::Vector<uint32_t> path;
};

struct VerifiedDefinitionKey final {
  ast::NodeId node;
  identity::DefinitionKey key;
};

struct VerifiedImplKey final {
  ast::NodeId node;
  identity::ImplKey key;
};

StableIdentityCandidateInvariant invariant(StableIdentityCandidateInvariantKind kind,
                                           ast::NodeId node) {
  return StableIdentityCandidateInvariant{kind, node};
}

bool callableDefinition(identity::DefinitionKind kind) noexcept {
  return kind == identity::DefinitionKind::Function || kind == identity::DefinitionKind::Method ||
         kind == identity::DefinitionKind::Constructor;
}

ast::NodeId stableDefinitionGenericBinder(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node)) { return {}; }
  const auto& syntax = tree.node(node);
  switch (syntax.kind) {
    case ast::SyntaxKind::EnumDeclaration:
      return ast::NodeId(syntax.payload.words[ast::kEnumDeclarationTypeParamsIdWord]);
    case ast::SyntaxKind::FunctionDecl:
      return ast::NodeId(syntax.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
    case ast::SyntaxKind::ClassDecl:
      return ast::NodeId(syntax.payload.words[ast::kClassDeclTypeParamsIdWord]);
    case ast::SyntaxKind::StructDecl:
      return ast::NodeId(syntax.payload.words[ast::kStructDeclTypeParamsIdWord]);
    case ast::SyntaxKind::InterfaceDecl:
      return ast::NodeId(syntax.payload.words[ast::kInterfaceDeclTypeParamsIdWord]);
    case ast::SyntaxKind::AliasDecl:
      return ast::NodeId(syntax.payload.words[ast::kAliasDeclTypeParamsIdWord]);
    case ast::SyntaxKind::MethodDecl:
      return ast::NodeId(syntax.payload.words[ast::kMethodDeclTypeParamsIdWord]);
    case ast::SyntaxKind::AssociatedTypeDecl:
      return ast::NodeId(syntax.payload.words[ast::kAssociatedTypeDeclTypeParamsIdWord]);
    default:
      return {};
  }
}

ast::NodeId stableImplementationGenericBinder(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::StandaloneImplDecl) {
    return {};
  }
  return ast::NodeId(tree.node(node).payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
}

struct DuplicateGenericParameterSyntax final {
  ast::NodeId node;
  identity::SourceSpan first;
  identity::SourceSpan duplicate;
  identity::DeclaredDefinitionName name;
};

struct SeenGenericParameter final {
  ast::NodeId node;
  identity::DeclaredDefinitionName name;
};

bool findFirstDuplicateGenericParameter(const CanonicalParsedModule& parsedModule,
                                        ast::NodeId genericParameters,
                                        zc::Maybe<DuplicateGenericParameterSyntax>& duplicate,
                                        ast::NodeId& badNode) {
  if (!genericParameters) { return true; }
  const auto& tree = parsedModule.tree();
  if (!tree.contains(genericParameters) ||
      tree.node(genericParameters).kind != ast::SyntaxKind::GenericParams) {
    badNode = genericParameters;
    return false;
  }
  const auto& syntax = tree.node(genericParameters);
  const ast::NodeList parameters{syntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                 syntax.payload.words[ast::kGenericParamsParamsSizeWord]};
  if (!tree.contains(parameters)) {
    badNode = genericParameters;
    return false;
  }
  zc::Vector<SeenGenericParameter> seen;
  for (const auto parameter : tree.list(parameters)) {
    if (!tree.contains(parameter) ||
        tree.node(parameter).kind != ast::SyntaxKind::GenericTypeParam) {
      badNode = parameter;
      return false;
    }
    const auto& parameterSyntax = tree.node(parameter);
    auto name = identity::DeclaredDefinitionName::fromSource(
        tree.ident(ast::IdentId(parameterSyntax.payload.words[ast::kGenericTypeParamNameWord])));
    if (name == zc::none) {
      badNode = parameter;
      return false;
    }
    ZC_IF_SOME(nameValue, name) {
      for (const auto& prior : seen) {
        if (prior.name != nameValue) { continue; }
        auto first = parsedModule.spanFor(tree.node(prior.node).range);
        auto repeated = parsedModule.spanFor(parameterSyntax.range);
        if (first == zc::none || repeated == zc::none) {
          badNode = parameter;
          return false;
        }
        ZC_IF_SOME(firstValue, first) {
          ZC_IF_SOME(repeatedValue, repeated) {
            duplicate = DuplicateGenericParameterSyntax{parameter, zc::mv(firstValue),
                                                        zc::mv(repeatedValue), nameValue.clone()};
          }
        }
        return true;
      }
      seen.add(SeenGenericParameter{parameter, zc::mv(nameValue)});
    }
  }
  return true;
}

int comparePath(zc::ArrayPtr<const uint32_t> left, zc::ArrayPtr<const uint32_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t shared = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < shared; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

zc::Vector<uint32_t> clonePath(zc::ArrayPtr<const uint32_t> path) {
  zc::Vector<uint32_t> result(path.size());
  result.addAll(path);
  return result;
}

bool isNonLiteralFixedArrayLength(const ast::Tree& tree, ast::NodeId owner, ast::NodeId node) {
  if (!tree.contains(owner) || !tree.contains(node) ||
      tree.node(node).kind == ast::SyntaxKind::IntLiteral) {
    return false;
  }
  bool found = false;
  ast::visitTreePreOrder(tree, owner, [&](ast::NodeId, const ast::Node& syntax) {
    if (syntax.kind == ast::SyntaxKind::FixedArrayTypeExpr &&
        ast::NodeId(syntax.payload.words[ast::kFixedArrayTypeExprLenExprWord]) == node) {
      found = true;
    }
  });
  return found;
}

zc::Maybe<const DefinitionInventoryEntry&> definitionAt(
    zc::ArrayPtr<const DefinitionInventoryEntry> definitions, ast::NodeId node) {
  for (const auto& entry : definitions) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<const ImplInventoryEntry&> implAt(zc::ArrayPtr<const ImplInventoryEntry> implementations,
                                            ast::NodeId node) {
  for (const auto& entry : implementations) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<const identity::DefinitionKey&> definitionKeyAt(
    zc::ArrayPtr<const VerifiedDefinitionKey> definitions, ast::NodeId node) {
  for (const auto& entry : definitions) {
    if (entry.node == node) { return entry.key; }
  }
  return zc::none;
}

zc::Maybe<const identity::ImplKey&> implKeyAt(zc::ArrayPtr<const VerifiedImplKey> impls,
                                              ast::NodeId node) {
  for (const auto& entry : impls) {
    if (entry.node == node) { return entry.key; }
  }
  return zc::none;
}

zc::Maybe<zc::Vector<identity::EnclosingStableOwnerKey>> buildOwners(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const VerifiedDefinitionKey> definitions,
    zc::ArrayPtr<const VerifiedImplKey> impls) {
  zc::Vector<identity::EnclosingStableOwnerKey> result(parents.size());
  for (const auto& parent : parents) {
    if (parent.kind == StructuralIdentityParentKind::Definition) {
      auto key = definitionKeyAt(definitions, parent.node);
      if (key == zc::none) { return zc::none; }
      ZC_IF_SOME(value, key) {
        result.add(identity::EnclosingStableOwnerKey::definition(value.clone()));
      }
      continue;
    }
    auto key = implKeyAt(impls, parent.node);
    if (key == zc::none) { return zc::none; }
    ZC_IF_SOME(value, key) {
      result.add(identity::EnclosingStableOwnerKey::implementation(value.clone()));
    }
  }
  return zc::mv(result);
}

enum class OracleDefinitionPlacement : uint8_t { Lexical, ModuleItem };

zc::Vector<StructuralIdentityParent> cloneParents(
    zc::ArrayPtr<const StructuralIdentityParent> parents) {
  zc::Vector<StructuralIdentityParent> result(parents.size());
  result.addAll(parents);
  return result;
}

/// Independent schema traversal for the stable-identity verification domain.
class StableSyntaxOracle final {
public:
  StableSyntaxOracle(const ast::Tree& tree, ast::NodeId selectedModule) noexcept
      : tree(tree), selectedModuleNode(selectedModule) {
    paths.resize(tree.nodeCount() + 1);
    visited.resize(tree.nodeCount() + 1);
    for (auto& state : visited) { state = 0; }
  }

  bool reconstruct() {
    zc::Vector<uint32_t> rootPath;
    if (!tree.contains(tree.root()) || !collectSyntaxPaths(tree.root(), rootPath)) { return false; }
    visitNode(tree.root(), OracleDefinitionPlacement::ModuleItem);
    if (!valid) { return false; }
    sortReferences();
    return true;
  }

  zc::ArrayPtr<const DefinitionInventoryEntry> definitions() const noexcept {
    return definitionEntries.asPtr();
  }
  zc::ArrayPtr<const ImplInventoryEntry> implementations() const noexcept {
    return implementationEntries.asPtr();
  }
  zc::ArrayPtr<const StableReference> references() const noexcept {
    return stableReferences.asPtr();
  }
  ast::NodeId failureNode() const noexcept { return badNode; }

private:
  bool collectSyntaxPaths(ast::NodeId node, zc::Vector<uint32_t>& path) {
    if (!tree.contains(node) || node.value >= visited.size() || visited[node.value] != 0) {
      badNode = node;
      return false;
    }
    visited[node.value] = 1;
    paths[node.value] = clonePath(path.asPtr());
    uint32_t childIndex = 0;
    bool complete = true;
    ast::visitChildNodeIds(tree, tree.node(node), [&](ast::NodeId child) {
      const uint32_t index = childIndex++;
      if (!complete) { return; }
      path.add(index);
      complete = collectSyntaxPaths(child, path);
      path.removeLast();
    });
    return complete;
  }

  bool selectedModule() const noexcept {
    return currentModule == selectedModuleNode || (!currentModule && !selectedModuleNode);
  }

  void addStableDefinition(ast::NodeId node, identity::DefinitionKind kind, ast::IdentId name) {
    if (!selectedModule() || !stableOwnerChain || !tree.contains(node) ||
        node.value >= paths.size()) {
      return;
    }
    const auto& syntax = tree.node(node);
    definitionEntries.add(
        DefinitionInventoryEntry{node, DefinitionSite::declaration(node), currentModule, kind,
                                 InventoryDefinitionNameKind::Declared, name, zc::none,
                                 syntax.range, cloneParents(parents.asPtr())});
    stableReferences.add(StableReference{false, node, clonePath(paths[node.value].asPtr())});
  }

  void visitStableDefinition(ast::NodeId node, identity::DefinitionKind kind, ast::IdentId name) {
    addStableDefinition(node, kind, name);
    const bool savedStableOwnerChain = stableOwnerChain;
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Definition, node});
    stableOwnerChain = savedStableOwnerChain && identity::isStableDefinitionKind(kind);
    visitChildren(node, OracleDefinitionPlacement::Lexical);
    stableOwnerChain = savedStableOwnerChain;
    parents.removeLast();
  }

  void visitUnstableDefinition(ast::NodeId node) {
    const bool savedStableOwnerChain = stableOwnerChain;
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Definition, node});
    stableOwnerChain = false;
    visitChildren(node, OracleDefinitionPlacement::Lexical);
    stableOwnerChain = savedStableOwnerChain;
    parents.removeLast();
  }

  void visitImplementation(ast::NodeId node) {
    if (selectedModule() && stableOwnerChain && tree.contains(node) && node.value < paths.size()) {
      implementationEntries.add(ImplInventoryEntry{node, currentModule, tree.node(node).range,
                                                   cloneParents(parents.asPtr())});
      stableReferences.add(StableReference{true, node, clonePath(paths[node.value].asPtr())});
    }
    const bool savedStableOwnerChain = stableOwnerChain;
    parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Impl, node});
    visitChildren(node, OracleDefinitionPlacement::Lexical);
    stableOwnerChain = savedStableOwnerChain;
    parents.removeLast();
  }

  void collectModulePatternBindings(ast::NodeId node, identity::DefinitionKind kind) {
    if (!tree.contains(node)) { return; }
    const auto& syntax = tree.node(node);
    switch (syntax.kind) {
      case ast::SyntaxKind::RestPattern: {
        const ast::IdentId name(syntax.payload.words[ast::kRestPatternBindingWord]);
        if (name) { addStableDefinition(node, kind, name); }
        return;
      }
      case ast::SyntaxKind::BindingPattern:
        addStableDefinition(node, kind,
                            ast::IdentId(syntax.payload.words[ast::kBindingPatternNameWord]));
        collectModulePatternBindings(ast::NodeId(syntax.payload.words[ast::kBindingPatternSubWord]),
                                     kind);
        return;
      case ast::SyntaxKind::IdentifierPattern:
        addStableDefinition(node, kind,
                            ast::IdentId(syntax.payload.words[ast::kIdentifierPatternNameWord]));
        return;
      case ast::SyntaxKind::PatternProperty:
        if (syntax.payload.words[ast::kPatternPropertyShortFormWord] != 0) {
          addStableDefinition(node, kind,
                              ast::IdentId(syntax.payload.words[ast::kPatternPropertyNameWord]));
        } else {
          collectModulePatternBindings(
              ast::NodeId(syntax.payload.words[ast::kPatternPropertyPatWord]), kind);
        }
        return;
      case ast::SyntaxKind::TuplePattern: {
        const ast::NodeList values{syntax.payload.words[ast::kTuplePatternPatsFirstWord],
                                   syntax.payload.words[ast::kTuplePatternPatsSizeWord]};
        for (const auto child : tree.list(values)) { collectModulePatternBindings(child, kind); }
        return;
      }
      case ast::SyntaxKind::StructPattern: {
        const ast::NodeList values{syntax.payload.words[ast::kStructPatternFieldsFirstWord],
                                   syntax.payload.words[ast::kStructPatternFieldsSizeWord]};
        for (const auto child : tree.list(values)) { collectModulePatternBindings(child, kind); }
        collectModulePatternBindings(ast::NodeId(syntax.payload.words[ast::kStructPatternRestWord]),
                                     kind);
        return;
      }
      case ast::SyntaxKind::ArrayPattern: {
        const ast::NodeList values{syntax.payload.words[ast::kArrayPatternPatsFirstWord],
                                   syntax.payload.words[ast::kArrayPatternPatsSizeWord]};
        for (const auto child : tree.list(values)) { collectModulePatternBindings(child, kind); }
        collectModulePatternBindings(ast::NodeId(syntax.payload.words[ast::kArrayPatternRestWord]),
                                     kind);
        return;
      }
      case ast::SyntaxKind::EnumPattern: {
        const ast::NodeList values{syntax.payload.words[ast::kEnumPatternArgsFirstWord],
                                   syntax.payload.words[ast::kEnumPatternArgsSizeWord]};
        for (const auto child : tree.list(values)) { collectModulePatternBindings(child, kind); }
        return;
      }
      default:
        return;
    }
  }

  void visitLet(ast::NodeId node, OracleDefinitionPlacement placement) {
    const auto& syntax = tree.node(node);
    if (placement == OracleDefinitionPlacement::ModuleItem) {
      const auto declarationKind =
          static_cast<ast::BindingDeclarationKind>(syntax.payload.words[ast::kLetStmtKindWord]);
      const auto kind = declarationKind == ast::BindingDeclarationKind::Const
                            ? identity::DefinitionKind::Constant
                            : identity::DefinitionKind::Static;
      const ast::NodeId declarations(syntax.payload.words[ast::kLetStmtDeclarationsWord]);
      if (tree.contains(declarations)) {
        const auto& list = tree.node(declarations);
        const ast::NodeList declarators{
            list.payload.words[ast::kVariableDeclaratorListDeclsFirstWord],
            list.payload.words[ast::kVariableDeclaratorListDeclsSizeWord]};
        for (const auto declarator : tree.list(declarators)) {
          if (!tree.contains(declarator)) {
            valid = false;
            badNode = declarator;
            return;
          }
          collectModulePatternBindings(
              ast::NodeId(tree.node(declarator).payload.words[ast::kVariableDeclaratorPatternWord]),
              kind);
        }
      }
    }
    visitChildren(node, OracleDefinitionPlacement::Lexical);
  }

  void visitModule(ast::NodeId node) {
    const auto& syntax = tree.node(node);
    const auto form = static_cast<ast::ModuleDeclarationForm>(
        syntax.payload.words[ast::kModuleDeclarationFormWord]);
    if (form == ast::ModuleDeclarationForm::Alias) {
      addStableDefinition(
          node, identity::DefinitionKind::ModuleAlias,
          ast::IdentId(syntax.payload.words[ast::kModuleDeclarationDeclaredNameWord]));
      visitChildren(node, OracleDefinitionPlacement::ModuleItem);
      return;
    }
    const auto savedModule = currentModule;
    auto savedParents = zc::mv(parents);
    const bool savedStableOwnerChain = stableOwnerChain;
    currentModule = node;
    parents = zc::Vector<StructuralIdentityParent>();
    stableOwnerChain = true;
    visitChildren(node, OracleDefinitionPlacement::ModuleItem);
    stableOwnerChain = savedStableOwnerChain;
    parents = zc::mv(savedParents);
    currentModule = savedModule;
  }

  void visitSourceFile(ast::NodeId node) {
    const auto& syntax = tree.node(node);
    const ast::NodeId module(syntax.payload.words[ast::kSourceFileModuleWord]);
    ast::NodeId sourceModule;
    if (tree.contains(module)) {
      const auto form = static_cast<ast::ModuleDeclarationForm>(
          tree.node(module).payload.words[ast::kModuleDeclarationFormWord]);
      visitNode(module, OracleDefinitionPlacement::ModuleItem);
      if (form == ast::ModuleDeclarationForm::RootDeclaration) { sourceModule = module; }
    }
    const auto savedModule = currentModule;
    currentModule = sourceModule;
    const ast::NodeList statements{syntax.payload.words[ast::kSourceFileStatementsFirstWord],
                                   syntax.payload.words[ast::kSourceFileStatementsSizeWord]};
    for (const auto statement : tree.list(statements)) {
      visitNode(statement, OracleDefinitionPlacement::ModuleItem);
    }
    currentModule = savedModule;
  }

  void visitChildren(ast::NodeId node, OracleDefinitionPlacement placement) {
    ast::visitChildNodeIds(tree, tree.node(node),
                           [this, placement](ast::NodeId child) { visitNode(child, placement); });
  }

  void visitNode(ast::NodeId node, OracleDefinitionPlacement placement) {
    if (!valid || !tree.contains(node)) { return; }
    const auto& syntax = tree.node(node);
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
        visitStableDefinition(node, identity::DefinitionKind::Function,
                              ast::IdentId(syntax.payload.words[ast::kExternDeclNameWord]));
        return;
      case ast::SyntaxKind::ExternVarDecl:
        visitStableDefinition(node, identity::DefinitionKind::Static,
                              ast::IdentId(syntax.payload.words[ast::kExternVarDeclNameWord]));
        return;
      case ast::SyntaxKind::UnitVariant:
        visitStableDefinition(node, identity::DefinitionKind::EnumVariant,
                              ast::IdentId(syntax.payload.words[ast::kUnitVariantNameWord]));
        return;
      case ast::SyntaxKind::TupleVariant:
        visitStableDefinition(node, identity::DefinitionKind::EnumVariant,
                              ast::IdentId(syntax.payload.words[ast::kTupleVariantNameWord]));
        return;
      case ast::SyntaxKind::EnumDeclaration:
        visitStableDefinition(node, identity::DefinitionKind::Enum,
                              ast::IdentId(syntax.payload.words[ast::kEnumDeclarationNameWord]));
        return;
      case ast::SyntaxKind::FunctionDecl:
        visitStableDefinition(node, identity::DefinitionKind::Function,
                              ast::IdentId(syntax.payload.words[ast::kFunctionDeclNameWord]));
        return;
      case ast::SyntaxKind::ClassDecl:
        visitStableDefinition(node, identity::DefinitionKind::Class,
                              ast::IdentId(syntax.payload.words[ast::kClassDeclNameWord]));
        return;
      case ast::SyntaxKind::StructDecl:
        visitStableDefinition(node, identity::DefinitionKind::Struct,
                              ast::IdentId(syntax.payload.words[ast::kStructDeclNameWord]));
        return;
      case ast::SyntaxKind::InterfaceDecl:
        visitStableDefinition(node, identity::DefinitionKind::Interface,
                              ast::IdentId(syntax.payload.words[ast::kInterfaceDeclNameWord]));
        return;
      case ast::SyntaxKind::ErrorDecl:
        visitStableDefinition(node, identity::DefinitionKind::Error,
                              ast::IdentId(syntax.payload.words[ast::kErrorDeclNameWord]));
        return;
      case ast::SyntaxKind::AliasDecl:
        visitStableDefinition(node, identity::DefinitionKind::TypeAlias,
                              ast::IdentId(syntax.payload.words[ast::kAliasDeclNameWord]));
        return;
      case ast::SyntaxKind::MethodDecl:
        visitStableDefinition(node, identity::DefinitionKind::Method,
                              ast::IdentId(syntax.payload.words[ast::kMethodDeclNameWord]));
        return;
      case ast::SyntaxKind::FieldDecl:
        visitStableDefinition(node, identity::DefinitionKind::Field,
                              ast::IdentId(syntax.payload.words[ast::kFieldDeclNameWord]));
        return;
      case ast::SyntaxKind::AssociatedTypeDecl:
        visitStableDefinition(node, identity::DefinitionKind::AssociatedType,
                              ast::IdentId(syntax.payload.words[ast::kAssociatedTypeDeclNameWord]));
        return;
      case ast::SyntaxKind::ConstructorDecl:
        visitStableDefinition(node, identity::DefinitionKind::Constructor,
                              ast::IdentId(syntax.payload.words[ast::kConstructorDeclNameWord]));
        return;
      case ast::SyntaxKind::DestructorDecl:
        visitStableDefinition(node, identity::DefinitionKind::Destructor,
                              ast::IdentId(syntax.payload.words[ast::kDestructorDeclNameWord]));
        return;
      case ast::SyntaxKind::ClassConstDecl:
        visitStableDefinition(node, identity::DefinitionKind::Constant,
                              ast::IdentId(syntax.payload.words[ast::kClassConstDeclNameWord]));
        return;
      case ast::SyntaxKind::StandaloneImplDecl:
      case ast::SyntaxKind::MarkerImpl:
        visitImplementation(node);
        return;
      case ast::SyntaxKind::FunctionParameterDecl:
      case ast::SyntaxKind::GenericTypeParam:
      case ast::SyntaxKind::FunctionExpression:
      case ast::SyntaxKind::LambdaExpression:
        visitUnstableDefinition(node);
        return;
      case ast::SyntaxKind::LetStmt:
        visitLet(node, placement);
        return;
      case ast::SyntaxKind::ImportDeclaration:
      case ast::SyntaxKind::ExportDeclaration:
      case ast::SyntaxKind::ImportSpecifier:
      case ast::SyntaxKind::ExportSpecifier:
        visitChildren(node, placement);
        return;
      default:
        visitChildren(node, OracleDefinitionPlacement::Lexical);
        return;
    }
  }

  void sortReferences() {
    for (size_t index = 1; index < stableReferences.size(); ++index) {
      auto current = zc::mv(stableReferences[index]);
      size_t insertion = index;
      while (insertion != 0 &&
             comparePath(current.path.asPtr(), stableReferences[insertion - 1].path.asPtr()) < 0) {
        stableReferences[insertion] = zc::mv(stableReferences[insertion - 1]);
        --insertion;
      }
      stableReferences[insertion] = zc::mv(current);
    }
  }

  const ast::Tree& tree;
  ast::NodeId selectedModuleNode;
  ast::NodeId currentModule;
  zc::Vector<StructuralIdentityParent> parents;
  bool stableOwnerChain = true;
  bool valid = true;
  ast::NodeId badNode;
  zc::Vector<zc::Vector<uint32_t>> paths;
  zc::Vector<uint8_t> visited;
  zc::Vector<DefinitionInventoryEntry> definitionEntries;
  zc::Vector<ImplInventoryEntry> implementationEntries;
  zc::Vector<StableReference> stableReferences;
};

zc::Maybe<const ProducedDefinitionIdentity&> producedDefinitionAt(
    const StableIdentityCandidateInventory& inventory, ast::NodeId node) {
  const ProducedDefinitionIdentity* result = nullptr;
  for (const auto& entry : inventory.definitions()) {
    if (entry.node != node) { continue; }
    if (result != nullptr) { return zc::none; }
    result = &entry;
  }
  if (result == nullptr) { return zc::none; }
  return *result;
}

zc::Maybe<const ProducedImplIdentity&> producedImplAt(
    const StableIdentityCandidateInventory& inventory, ast::NodeId node) {
  const ProducedImplIdentity* result = nullptr;
  for (const auto& entry : inventory.implementations()) {
    if (entry.node != node) { continue; }
    if (result != nullptr) { return zc::none; }
    result = &entry;
  }
  if (result == nullptr) { return zc::none; }
  return *result;
}

zc::Maybe<const PreAdmissionIdentityCandidate&> candidateAt(
    const StableIdentityCandidateInventory& inventory, const IdentitySyntaxSiteKey& site) {
  const PreAdmissionIdentityCandidate* result = nullptr;
  for (const auto& candidate : inventory.candidates()) {
    if (!candidate.site().sameAs(site)) { continue; }
    if (result != nullptr) { return zc::none; }
    result = &candidate;
  }
  if (result == nullptr) { return zc::none; }
  return *result;
}

bool retainsExactSite(const StableIdentityCandidateInventory& inventory,
                      const IdentitySyntaxSiteKey& expected, const identity::SourceSpan& source) {
  size_t count = 0;
  for (const auto& site : inventory.sites()) {
    if (!site.key().sameAs(expected)) { continue; }
    if (site.range().byteStart() != source.byteStart() ||
        site.range().byteEnd() != source.byteEnd() ||
        !site.range().source().sameAs(source.source())) {
      return false;
    }
    ++count;
  }
  return count == 1;
}

zc::Maybe<identity::OverloadHeaderAuthority> cloneOverload(
    const PreAdmissionIdentityCandidate& candidate) {
  ZC_IF_SOME(value, candidate.overloadHeader()) { return value.clone(); }
  return zc::none;
}

zc::Maybe<BinderDiagnosticCode> redeclarationCode(identity::DefinitionKind kind) {
  using identity::DefinitionKind;
  switch (kind) {
    case DefinitionKind::Function:
    case DefinitionKind::Method:
    case DefinitionKind::Constructor:
    case DefinitionKind::Destructor:
      return BinderDiagnosticCode::RedeclareFunction;
    case DefinitionKind::Class:
      return BinderDiagnosticCode::RedeclareClass;
    case DefinitionKind::Interface:
      return BinderDiagnosticCode::RedeclareInterface;
    case DefinitionKind::Enum:
      return BinderDiagnosticCode::RedeclareEnum;
    case DefinitionKind::TypeAlias:
    case DefinitionKind::AssociatedType:
      return BinderDiagnosticCode::RedeclareTypeAlias;
    case DefinitionKind::Field:
    case DefinitionKind::Constant:
    case DefinitionKind::Static:
      return BinderDiagnosticCode::RedeclareVariable;
    case DefinitionKind::Struct:
    case DefinitionKind::Error:
    case DefinitionKind::EnumVariant:
    case DefinitionKind::ModuleAlias:
      return BinderDiagnosticCode::DuplicateIdentifier;
    case DefinitionKind::Parameter:
    case DefinitionKind::TypeParameter:
    case DefinitionKind::Local:
    case DefinitionKind::PatternBinding:
    case DefinitionKind::Closure:
      return zc::none;
    default:
      ZC_UNREACHABLE;
  }
}

bool sourceOrderLess(const VerifiedStableDefinitionCandidate& left,
                     const VerifiedStableDefinitionCandidate& right) {
  if (!left.site.source().sameAs(right.site.source())) {
    const auto leftSource = left.site.source().encode();
    const auto rightSource = right.site.source().encode();
    return compareBytes(leftSource.asPtr(), rightSource.asPtr()) < 0;
  }
  if (left.source.byteStart() != right.source.byteStart()) {
    return left.source.byteStart() < right.source.byteStart();
  }
  if (left.source.byteEnd() != right.source.byteEnd()) {
    return left.source.byteEnd() < right.source.byteEnd();
  }
  return comparePath(left.site.moduleSyntaxPath(), right.site.moduleSyntaxPath()) < 0;
}

}  // namespace

static StableIdentityCandidateVerification reconstructStableCandidates(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, const StableIdentityCandidateProduction* production) {
  const auto& tree = parsedModule.tree();
  if (!tree.contains(tree.root()) ||
      (moduleNode && (!tree.contains(moduleNode) ||
                      tree.node(moduleNode).kind != ast::SyntaxKind::ModuleDeclaration)) ||
      !parsedModule.source().belongsTo(module.crate())) {
    return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch, moduleNode);
  }

  StableSyntaxOracle syntax(tree, moduleNode);
  if (!syntax.reconstruct()) {
    return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch,
                     syntax.failureNode());
  }
  const CanonicalHeaderSyntaxView headerSyntax(syntax.definitions(), syntax.implementations());
  const StableIdentityCandidateInventory* inventory = nullptr;
  if (production != nullptr && production->is<StableIdentityCandidateInventory>()) {
    inventory = &production->get<StableIdentityCandidateInventory>();
  }
  const StableIdentityCandidateFailure* productionFailure = nullptr;
  if (production != nullptr && production->is<StableIdentityCandidateFailure>()) {
    productionFailure = &production->get<StableIdentityCandidateFailure>();
  }

  zc::Vector<VerifiedDefinitionKey> definitionKeys;
  zc::Vector<VerifiedImplKey> implKeys;
  VerifiedStableIdentityCandidateInventory result;
  size_t verifiedDefinitionCount = 0;
  size_t verifiedImplCount = 0;
  for (const auto& reference : syntax.references()) {
    if (!reference.implementation) {
      auto definition = definitionAt(syntax.definitions(), reference.node);
      if (definition == zc::none) {
        return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch, reference.node);
      }
      ZC_IF_SOME(entry, definition) {
        zc::Maybe<DuplicateGenericParameterSyntax> duplicateGeneric;
        ast::NodeId badGeneric;
        if (!findFirstDuplicateGenericParameter(parsedModule,
                                                stableDefinitionGenericBinder(tree, entry.node),
                                                duplicateGeneric, badGeneric)) {
          return invariant(StableIdentityCandidateInvariantKind::InvalidDefinitionAuthority,
                           badGeneric);
        }
        ZC_IF_SOME(duplicate, duplicateGeneric) {
          zc::Maybe<identity::SourceSpan> previous = zc::mv(duplicate.first);
          zc::Maybe<identity::DeclaredDefinitionName> identifier = zc::mv(duplicate.name);
          return StableIdentityCandidateSourceFailure{
              StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter, duplicate.node,
              zc::mv(duplicate.duplicate), zc::mv(previous), zc::mv(identifier)};
        }
        auto owners =
            buildOwners(entry.parentPath.asPtr(), definitionKeys.asPtr(), implKeys.asPtr());
        auto name = identity::DeclaredDefinitionName::fromSource(tree.ident(entry.declaredName));
        auto nameSpace = identity::definitionNamespaceFor(entry.kind);
        if (owners == zc::none || name == zc::none || nameSpace == zc::none) {
          return invariant(StableIdentityCandidateInvariantKind::InvalidDefinitionAuthority,
                           entry.node);
        }
        zc::Maybe<identity::OverloadHeaderAuthority> overload;
        zc::Maybe<identity::OverloadHeaderDigest> digest;
        if (callableDefinition(entry.kind)) {
          auto header = CanonicalHeaderVerifier::reconstructDefinition(tree, headerSyntax, entry);
          if (!header.is<VerifiedCanonicalDefinitionHeader>()) {
            const auto bad = header.get<CanonicalHeaderVerificationFailure>().node;
            const bool matchingProductionFailure =
                productionFailure != nullptr &&
                productionFailure->kind == StableIdentityCandidateFailureKind::InvalidHeader &&
                productionFailure->headerKind ==
                    CanonicalHeaderSyntaxFailureKind::InvalidConstantExpression &&
                productionFailure->node == bad;
            if ((production == nullptr || matchingProductionFailure) &&
                isNonLiteralFixedArrayLength(tree, entry.node, bad)) {
              auto source = parsedModule.spanFor(tree.node(bad).range);
              if (source == zc::none) {
                return invariant(StableIdentityCandidateInvariantKind::InvalidSyntaxSite, bad);
              }
              ZC_IF_SOME(value, source) {
                return StableIdentityCandidateSourceFailure{
                    StableIdentityCandidateSourceFailureKind::ConstantExpressionNotAllowed, bad,
                    zc::mv(value), zc::none, zc::none};
              }
            }
            return invariant(StableIdentityCandidateInvariantKind::InvalidDefinitionAuthority, bad);
          }
          auto verified = zc::mv(header.get<VerifiedCanonicalDefinitionHeader>());
          digest = verified.authority.digest().clone();
          overload = zc::mv(verified.authority);
        }
        ZC_IF_SOME(ownerValues, owners) {
          ZC_IF_SOME(nameValue, name) {
            ZC_IF_SOME(namespaceValue, nameSpace) {
              auto record = identity::DefinitionIdentityRecord::from(
                  module.clone(), zc::mv(ownerValues), entry.kind, namespaceValue,
                  zc::mv(nameValue), zc::mv(digest));
              if (record == zc::none) {
                return invariant(StableIdentityCandidateInvariantKind::InvalidDefinitionAuthority,
                                 entry.node);
              }
              ZC_IF_SOME(recordValue, record) {
                auto authority = identity::DefinitionIdentityAuthority::from(recordValue.clone(),
                                                                             zc::mv(overload));
                if (authority == zc::none) {
                  return invariant(StableIdentityCandidateInvariantKind::InvalidDefinitionAuthority,
                                   entry.node);
                }
                ZC_IF_SOME(authorityValue, authority) {
                  definitionKeys.add(
                      VerifiedDefinitionKey{entry.node, authorityValue.key().clone()});
                  auto site =
                      IdentitySyntaxSiteKey::from(module.clone(), parsedModule.source().clone(),
                                                  clonePath(reference.path.asPtr()));
                  auto source = parsedModule.spanFor(entry.source);
                  if (site == zc::none || source == zc::none) {
                    return invariant(StableIdentityCandidateInvariantKind::InvalidSyntaxSite,
                                     entry.node);
                  }
                  ZC_IF_SOME(siteValue, site) {
                    ZC_IF_SOME(sourceValue, source) {
                      if (inventory != nullptr) {
                        auto produced = producedDefinitionAt(*inventory, entry.node);
                        auto candidate = candidateAt(*inventory, siteValue);
                        if (produced == zc::none || candidate == zc::none) {
                          return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch,
                                           entry.node);
                        }
                        if (!retainsExactSite(*inventory, siteValue, sourceValue)) {
                          return invariant(StableIdentityCandidateInvariantKind::InvalidSyntaxSite,
                                           entry.node);
                        }
                        ZC_IF_SOME(producedValue, produced) {
                          if (producedValue.key != authorityValue.key()) {
                            return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch,
                                             entry.node);
                          }
                        }
                        ZC_IF_SOME(candidateValue, candidate) {
                          auto candidateRecord = candidateValue.definitionRecord();
                          if (candidateValue.kind() != PreAdmissionIdentityKind::Definition ||
                              candidateRecord == zc::none) {
                            return invariant(
                                StableIdentityCandidateInvariantKind::ProductionMismatch,
                                entry.node);
                          }
                          ZC_IF_SOME(candidateRecordValue, candidateRecord) {
                            auto candidateAuthority = identity::DefinitionIdentityAuthority::from(
                                candidateRecordValue.clone(), cloneOverload(candidateValue));
                            if (candidateAuthority == zc::none) {
                              return invariant(
                                  StableIdentityCandidateInvariantKind::ProductionMismatch,
                                  entry.node);
                            }
                            ZC_IF_SOME(candidateAuthorityValue, candidateAuthority) {
                              if (!authorityValue.sameRecordAs(candidateAuthorityValue)) {
                                return invariant(
                                    StableIdentityCandidateInvariantKind::ProductionMismatch,
                                    entry.node);
                              }
                            }
                          }
                        }
                        ++verifiedDefinitionCount;
                      }
                      result.definitions.add(VerifiedStableDefinitionCandidate{
                          entry.node, zc::mv(authorityValue), zc::mv(siteValue),
                          zc::mv(sourceValue)});
                    }
                  }
                }
              }
            }
          }
        }
      }
      continue;
    }

    auto implementation = implAt(syntax.implementations(), reference.node);
    if (implementation == zc::none) {
      return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch, reference.node);
    }
    ZC_IF_SOME(entry, implementation) {
      zc::Maybe<DuplicateGenericParameterSyntax> duplicateGeneric;
      ast::NodeId badGeneric;
      if (!findFirstDuplicateGenericParameter(parsedModule,
                                              stableImplementationGenericBinder(tree, entry.node),
                                              duplicateGeneric, badGeneric)) {
        return invariant(StableIdentityCandidateInvariantKind::InvalidImplementationAuthority,
                         badGeneric);
      }
      ZC_IF_SOME(duplicate, duplicateGeneric) {
        zc::Maybe<identity::SourceSpan> previous = zc::mv(duplicate.first);
        zc::Maybe<identity::DeclaredDefinitionName> identifier = zc::mv(duplicate.name);
        return StableIdentityCandidateSourceFailure{
            StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter, duplicate.node,
            zc::mv(duplicate.duplicate), zc::mv(previous), zc::mv(identifier)};
      }
      auto owners = buildOwners(entry.parentPath.asPtr(), definitionKeys.asPtr(), implKeys.asPtr());
      auto header = CanonicalHeaderVerifier::reconstructImpl(tree, headerSyntax, entry);
      if (owners == zc::none || !header.is<VerifiedCanonicalImplHeader>()) {
        const auto bad = header.is<CanonicalHeaderVerificationFailure>()
                             ? header.get<CanonicalHeaderVerificationFailure>().node
                             : entry.node;
        return invariant(StableIdentityCandidateInvariantKind::InvalidImplementationAuthority, bad);
      }
      ZC_IF_SOME(ownerValues, owners) {
        auto verified = zc::mv(header.get<VerifiedCanonicalImplHeader>());
        auto record = identity::ImplIdentityRecord::from(module.clone(), zc::mv(ownerValues),
                                                         zc::mv(verified.header));
        auto authority = identity::ImplIdentityAuthority::from(record.clone());
        implKeys.add(VerifiedImplKey{entry.node, authority.key().clone()});
        auto site = IdentitySyntaxSiteKey::from(module.clone(), parsedModule.source().clone(),
                                                clonePath(reference.path.asPtr()));
        auto source = parsedModule.spanFor(entry.source);
        if (site == zc::none || source == zc::none) {
          return invariant(StableIdentityCandidateInvariantKind::InvalidSyntaxSite, entry.node);
        }
        ZC_IF_SOME(siteValue, site) {
          ZC_IF_SOME(sourceValue, source) {
            if (inventory != nullptr) {
              auto produced = producedImplAt(*inventory, entry.node);
              auto candidate = candidateAt(*inventory, siteValue);
              if (produced == zc::none || candidate == zc::none) {
                return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch,
                                 entry.node);
              }
              if (!retainsExactSite(*inventory, siteValue, sourceValue)) {
                return invariant(StableIdentityCandidateInvariantKind::InvalidSyntaxSite,
                                 entry.node);
              }
              ZC_IF_SOME(producedValue, produced) {
                if (producedValue.key != authority.key()) {
                  return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch,
                                   entry.node);
                }
              }
              ZC_IF_SOME(candidateValue, candidate) {
                auto candidateRecord = candidateValue.implRecord();
                if (candidateValue.kind() != PreAdmissionIdentityKind::Implementation ||
                    candidateRecord == zc::none) {
                  return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch,
                                   entry.node);
                }
                ZC_IF_SOME(candidateRecordValue, candidateRecord) {
                  auto candidateAuthority =
                      identity::ImplIdentityAuthority::from(candidateRecordValue.clone());
                  if (!authority.sameRecordAs(candidateAuthority)) {
                    return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch,
                                     entry.node);
                  }
                }
              }
              ++verifiedImplCount;
            }
            result.implementations.add(VerifiedStableImplementationCandidate{
                entry.node, zc::mv(authority), zc::mv(siteValue), zc::mv(sourceValue)});
          }
        }
      }
    }
  }

  if (production == nullptr) { return result; }
  if (inventory == nullptr || productionFailure != nullptr ||
      verifiedDefinitionCount != inventory->definitions().size() ||
      verifiedImplCount != inventory->implementations().size() ||
      inventory->candidates().size() !=
          inventory->definitions().size() + inventory->implementations().size()) {
    return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch, moduleNode);
  }
  return result;
}

StableIdentityCandidateVerification StableIdentityCandidateVerifier::reconstruct(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode) {
  return reconstructStableCandidates(parsedModule, module, moduleNode, nullptr);
}

StableIdentityCandidateVerification StableIdentityCandidateVerifier::verify(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode, const StableIdentityCandidateProduction& production) {
  return reconstructStableCandidates(parsedModule, module, moduleNode, &production);
}

StableDefinitionRedeclarationValidation
StableIdentityCandidateVerifier::findDefinitionRedeclarations(
    zc::ArrayPtr<const VerifiedStableDefinitionCandidate> definitions) {
  if (definitions.size() > UINT32_MAX) {
    return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch, ast::NodeId());
  }
  zc::Vector<uint32_t> order(definitions.size());
  for (size_t index = 0; index < definitions.size(); ++index) {
    order.add(static_cast<uint32_t>(index));
  }
  for (size_t index = 1; index < order.size(); ++index) {
    const uint32_t current = order[index];
    size_t insertion = index;
    while (insertion != 0 &&
           sourceOrderLess(definitions[current], definitions[order[insertion - 1]])) {
      order[insertion] = order[insertion - 1];
      --insertion;
    }
    order[insertion] = current;
  }

  zc::Vector<uint32_t> firstIndices;
  zc::Vector<StableDefinitionRedeclaration> result;
  for (const auto index : order) {
    const auto& candidate = definitions[index];
    uint32_t first = UINT32_MAX;
    for (const auto prior : firstIndices) {
      if (definitions[prior].authority.key() == candidate.authority.key()) {
        first = prior;
        break;
      }
    }
    if (first == UINT32_MAX) {
      firstIndices.add(index);
      continue;
    }
    const auto& authority = definitions[first].authority;
    if (!authority.sameRecordAs(candidate.authority)) {
      return invariant(StableIdentityCandidateInvariantKind::DigestCollision, candidate.node);
    }
    auto diagnostic = redeclarationCode(candidate.authority.record().kind());
    if (diagnostic == zc::none) {
      return invariant(StableIdentityCandidateInvariantKind::ProductionMismatch, candidate.node);
    }
    ZC_IF_SOME(value, diagnostic) {
      result.add(StableDefinitionRedeclaration{first, index, value});
    }
  }
  return result;
}

}  // namespace zomlang::compiler::binder
