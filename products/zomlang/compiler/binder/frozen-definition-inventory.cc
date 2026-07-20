// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/frozen-definition-inventory.h"

#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/canonical-header-verifier.h"
#include "zomlang/compiler/binder/definition-inventory.h"

namespace zomlang::compiler::binder {
namespace {

constexpr uint32_t kMissing = UINT32_MAX;

FrozenInventoryInvariantFact failure(FrozenInventoryInvariantKind kind) { return {kind, 1}; }

template <typename Left, typename Right>
bool sameEncoding(const Left& left, const Right& right) {
  const auto leftBytes = left.encode();
  const auto rightBytes = right.encode();
  return leftBytes.asPtr() == rightBytes.asPtr();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return sameEncoding(left, right);
}

bool sameSource(const identity::SourceFileKey& left, const identity::SourceFileKey& right) {
  return sameEncoding(left, right);
}

bool callableDefinition(identity::DefinitionKind kind) noexcept {
  return kind == identity::DefinitionKind::Function || kind == identity::DefinitionKind::Method ||
         kind == identity::DefinitionKind::Constructor;
}

zc::Maybe<identity::DeclaredDefinitionName> declaredName(const DefinitionInventoryEntry& entry,
                                                         const VerifiedParsedModule& parsedModule) {
  if (entry.nameKind != InventoryDefinitionNameKind::Declared) { return zc::none; }
  return identity::DeclaredDefinitionName::fromSource(
      parsedModule.tree().ident(entry.declaredName));
}

template <typename Entry>
zc::Maybe<const Entry&> entryAt(zc::ArrayPtr<const Entry> entries, ast::NodeId node) {
  for (const auto& entry : entries) {
    if (entry.node == node) { return entry; }
  }
  return zc::none;
}

zc::Maybe<const identity::DefinitionKey&> definitionKeyAt(
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions, ast::NodeId node) {
  for (const auto& definition : definitions) {
    if (definition.node == node) { return definition.key; }
  }
  return zc::none;
}

bool containsNode(zc::ArrayPtr<const ast::NodeId> nodes, ast::NodeId candidate);

zc::Maybe<const identity::ImplKey&> implKeyAt(
    zc::ArrayPtr<const FrozenImplOccurrenceProjection> occurrences, ast::NodeId node) {
  for (const auto& occurrence : occurrences) {
    if (occurrence.node == node) { return occurrence.key.implementation(); }
  }
  return zc::none;
}

zc::Maybe<zc::Vector<identity::EnclosingStableOwnerKey>> stableOwnerChain(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const FrozenImplOccurrenceProjection> occurrences) {
  zc::Vector<identity::EnclosingStableOwnerKey> owners(parents.size());
  for (const auto& parent : parents) {
    if (parent.kind == StructuralIdentityParentKind::Definition) {
      auto key = definitionKeyAt(definitions, parent.node);
      if (key == zc::none) { return zc::none; }
      ZC_IF_SOME(value, key) {
        owners.add(identity::EnclosingStableOwnerKey::definition(value.clone()));
      }
      continue;
    }
    auto key = implKeyAt(occurrences, parent.node);
    if (key == zc::none) { return zc::none; }
    ZC_IF_SOME(value, key) {
      owners.add(identity::EnclosingStableOwnerKey::implementation(value.clone()));
    }
  }
  return zc::mv(owners);
}

zc::Maybe<identity::StableGenericParameterOwnerKey> immediateStableOwner(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const FrozenImplOccurrenceProjection> occurrences) {
  if (parents.size() == 0) { return zc::none; }
  const auto& parent = parents.back();
  if (parent.kind == StructuralIdentityParentKind::Definition) {
    ZC_IF_SOME(key, definitionKeyAt(definitions, parent.node)) {
      return identity::StableGenericParameterOwnerKey::definition(key.clone());
    }
    return zc::none;
  }
  ZC_IF_SOME(key, implKeyAt(occurrences, parent.node)) {
    return identity::StableGenericParameterOwnerKey::implementation(key.clone());
  }
  return zc::none;
}

zc::Maybe<const identity::DefinitionKey&> immediateDefinitionOwner(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions) {
  if (parents.size() == 0 || parents.back().kind != StructuralIdentityParentKind::Definition) {
    return zc::none;
  }
  return definitionKeyAt(definitions, parents.back().node);
}

zc::Maybe<StableBodyOwnerKey> stableBodyOwner(
    zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const ast::NodeId> definitionAuthorities, const identity::ModuleKey& module) {
  for (size_t remaining = parents.size(); remaining > 0; --remaining) {
    const auto& parent = parents[remaining - 1];
    if (parent.kind != StructuralIdentityParentKind::Definition) { continue; }
    ZC_IF_SOME(key, definitionKeyAt(definitions, parent.node)) {
      if (!containsNode(definitionAuthorities, parent.node)) { return zc::none; }
      return StableBodyOwnerKey::definition(key.clone());
    }
  }
  return StableBodyOwnerKey::module(module.clone());
}

zc::Maybe<ast::NodeId> immediateAnonymousOwnerNode(
    const DefinitionInventory& inventory, zc::ArrayPtr<const StructuralIdentityParent> parents) {
  if (parents.size() == 0 || parents.back().kind != StructuralIdentityParentKind::Definition ||
      entryAt(inventory.anonymousEntities(), parents.back().node) == zc::none) {
    return zc::none;
  }
  return parents.back().node;
}

bool containsNodeInList(const ast::Tree& tree, ast::NodeId listNode, ast::NodeId candidate,
                        ast::SyntaxKind expectedListKind, uint32_t firstWord, uint32_t sizeWord) {
  if (!tree.contains(listNode) || tree.node(listNode).kind != expectedListKind) { return false; }
  const auto& list = tree.node(listNode);
  const ast::NodeList nodes{list.payload.words[firstWord], list.payload.words[sizeWord]};
  if (!tree.contains(nodes)) { return false; }
  for (const auto node : tree.list(nodes)) {
    if (node == candidate) { return true; }
  }
  return false;
}

bool anonymousOwnsGenericParameter(const ast::Tree& tree, ast::NodeId anonymous,
                                   ast::NodeId parameter) {
  if (!tree.contains(anonymous) ||
      tree.node(anonymous).kind != ast::SyntaxKind::FunctionExpression) {
    return false;
  }
  const ast::NodeId list(
      tree.node(anonymous).payload.words[ast::kFunctionExpressionTypeParamsIdWord]);
  return containsNodeInList(tree, list, parameter, ast::SyntaxKind::GenericParams,
                            ast::kGenericParamsParamsFirstWord, ast::kGenericParamsParamsSizeWord);
}

bool anonymousOwnsCallableParameter(const ast::Tree& tree, ast::NodeId anonymous,
                                    ast::NodeId parameter) {
  if (!tree.contains(anonymous)) { return false; }
  ast::NodeId list;
  const auto& callable = tree.node(anonymous);
  if (callable.kind == ast::SyntaxKind::FunctionExpression) {
    list = ast::NodeId(callable.payload.words[ast::kFunctionExpressionParamsIdWord]);
  } else if (callable.kind == ast::SyntaxKind::LambdaExpression) {
    list = ast::NodeId(callable.payload.words[ast::kLambdaExpressionParamsIdWord]);
  } else {
    return false;
  }
  return containsNodeInList(tree, list, parameter, ast::SyntaxKind::FunctionParameterList,
                            ast::kFunctionParameterListParamsFirstWord,
                            ast::kFunctionParameterListParamsSizeWord);
}

bool findSyntaxPath(const ast::Tree& tree, ast::NodeId current, ast::NodeId target,
                    zc::Vector<uint32_t>& path) {
  if (current == target) { return true; }
  uint32_t childIndex = 0;
  bool found = false;
  ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
    const uint32_t thisIndex = childIndex++;
    if (found || !tree.contains(child)) { return; }
    path.add(thisIndex);
    if (findSyntaxPath(tree, child, target, path)) {
      found = true;
      return;
    }
    path.removeLast();
  });
  return found;
}

zc::Maybe<LocalSyntaxPath> localPath(const ast::Tree& tree, ast::NodeId owner, ast::NodeId target) {
  if (!tree.contains(owner) || !tree.contains(target) || owner == target) { return zc::none; }
  zc::Vector<uint32_t> path;
  if (!findSyntaxPath(tree, owner, target, path)) { return zc::none; }
  return LocalSyntaxPath::from(zc::mv(path));
}

zc::Maybe<LocalSyntaxPath> moduleBodyPath(const ast::Tree& tree, ast::NodeId module,
                                          ast::NodeId target) {
  if (!tree.contains(target) || !tree.contains(tree.root()) ||
      tree.node(tree.root()).kind != ast::SyntaxKind::SourceFile) {
    return zc::none;
  }

  const auto& sourceFile = tree.node(tree.root());
  ast::NodeList bodyItems;
  if (!module) {
    if (ast::NodeId(sourceFile.payload.words[ast::kSourceFileModuleWord])) { return zc::none; }
    bodyItems = ast::NodeList{sourceFile.payload.words[ast::kSourceFileStatementsFirstWord],
                              sourceFile.payload.words[ast::kSourceFileStatementsSizeWord]};
  } else {
    if (!tree.contains(module) || tree.node(module).kind != ast::SyntaxKind::ModuleDeclaration) {
      return zc::none;
    }
    const auto& declaration = tree.node(module);
    const auto form = static_cast<ast::ModuleDeclarationForm>(
        declaration.payload.words[ast::kModuleDeclarationFormWord]);
    if (form == ast::ModuleDeclarationForm::RootDeclaration) {
      if (ast::NodeId(sourceFile.payload.words[ast::kSourceFileModuleWord]) != module) {
        return zc::none;
      }
      bodyItems = ast::NodeList{sourceFile.payload.words[ast::kSourceFileStatementsFirstWord],
                                sourceFile.payload.words[ast::kSourceFileStatementsSizeWord]};
    } else if (form == ast::ModuleDeclarationForm::InlineRoot) {
      bodyItems =
          ast::NodeList{declaration.payload.words[ast::kModuleDeclarationInlineItemsFirstWord],
                        declaration.payload.words[ast::kModuleDeclarationInlineItemsSizeWord]};
    } else {
      return zc::none;
    }
  }
  if (!tree.contains(bodyItems)) { return zc::none; }

  uint32_t ordinal = 0;
  for (const auto item : tree.list(bodyItems)) {
    zc::Vector<uint32_t> path;
    path.add(ordinal++);
    if (tree.contains(item) && findSyntaxPath(tree, item, target, path)) {
      return LocalSyntaxPath::from(zc::mv(path));
    }
  }
  return zc::none;
}

zc::Maybe<LocalSyntaxPath> stableBodyPath(
    const ast::Tree& tree, zc::ArrayPtr<const StructuralIdentityParent> parents,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const ast::NodeId> definitionAuthorities, ast::NodeId module, ast::NodeId target) {
  for (size_t remaining = parents.size(); remaining > 0; --remaining) {
    const auto& parent = parents[remaining - 1];
    if (parent.kind != StructuralIdentityParentKind::Definition) { continue; }
    if (definitionKeyAt(definitions, parent.node) == zc::none) { continue; }
    return containsNode(definitionAuthorities, parent.node) ? localPath(tree, parent.node, target)
                                                            : zc::Maybe<LocalSyntaxPath>();
  }
  return moduleBodyPath(tree, module, target);
}

zc::Maybe<IdentitySyntaxSiteKey> moduleSiteKey(const VerifiedParsedModule& parsedModule,
                                               const identity::ModuleKey& module,
                                               ast::NodeId target) {
  zc::Vector<uint32_t> path;
  if (!findSyntaxPath(parsedModule.tree(), parsedModule.tree().root(), target, path)) {
    return zc::none;
  }
  return IdentitySyntaxSiteKey::from(module.clone(), parsedModule.source().clone(), zc::mv(path));
}

int compareSyntaxPath(zc::ArrayPtr<const uint32_t> left,
                      zc::ArrayPtr<const uint32_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

bool appendDuplicateBoundOracle(const VerifiedParsedModule& parsedModule,
                                const identity::ModuleKey& module, ast::NodeId identityNode,
                                zc::ArrayPtr<const CanonicalBoundSyntaxOccurrence> occurrences,
                                zc::Vector<FrozenDuplicateBoundProjection>& result) {
  struct FirstOccurrence final {
    zc::Array<uint8_t> obligation;
    IdentitySyntaxSiteKey site;
  };

  zc::Vector<zc::Vector<uint32_t>> paths(occurrences.size());
  zc::Vector<size_t> order(occurrences.size());
  for (size_t index = 0; index < occurrences.size(); ++index) {
    zc::Vector<uint32_t> path;
    if (!findSyntaxPath(parsedModule.tree(), parsedModule.tree().root(), occurrences[index].node,
                        path)) {
      return false;
    }
    paths.add(zc::mv(path));
    order.add(index);
  }
  for (size_t index = 1; index < order.size(); ++index) {
    const size_t current = order[index];
    size_t insertion = index;
    while (insertion != 0 &&
           compareSyntaxPath(paths[current].asPtr(), paths[order[insertion - 1]].asPtr()) < 0) {
      order[insertion] = order[insertion - 1];
      --insertion;
    }
    order[insertion] = current;
  }

  zc::Vector<FirstOccurrence> firstOccurrences;
  for (const auto index : order) {
    const auto& occurrence = occurrences[index];
    auto site = moduleSiteKey(parsedModule, module, occurrence.node);
    if (site == zc::none) { return false; }
    auto obligation = occurrence.obligation.encode();
    const FirstOccurrence* first = nullptr;
    for (const auto& candidate : firstOccurrences) {
      if (candidate.obligation.asPtr() == obligation.asPtr()) {
        first = &candidate;
        break;
      }
    }
    ZC_IF_SOME(siteValue, site) {
      if (first == nullptr) {
        firstOccurrences.add(FirstOccurrence{zc::mv(obligation), siteValue.clone()});
        continue;
      }
      auto duplicate = DuplicateBoundOccurrence::from(occurrence.obligation.clone(),
                                                      first->site.clone(), siteValue.clone());
      if (duplicate == zc::none) { return false; }
      ZC_IF_SOME(value, duplicate) {
        result.add(FrozenDuplicateBoundProjection{identityNode, zc::mv(value)});
      }
    }
  }
  return true;
}

void sortDuplicateBounds(zc::Vector<FrozenDuplicateBoundProjection>& projections) {
  for (size_t index = 1; index < projections.size(); ++index) {
    auto current = zc::mv(projections[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           compareSyntaxPath(current.occurrence.duplicate().moduleSyntaxPath(),
                             projections[insertion - 1].occurrence.duplicate().moduleSyntaxPath()) <
               0) {
      projections[insertion] = zc::mv(projections[insertion - 1]);
      --insertion;
    }
    projections[insertion] = zc::mv(current);
  }
}

bool sameDuplicateBounds(zc::ArrayPtr<const FrozenDuplicateBoundProjection> left,
                         zc::ArrayPtr<const FrozenDuplicateBoundProjection> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].identity != right[index].identity ||
        !sameEncoding(left[index].occurrence, right[index].occurrence)) {
      return false;
    }
  }
  return true;
}

zc::Maybe<identity::DefinitionIdentityAuthority> reconstructDefinitionAuthority(
    const VerifiedParsedModule& parsedModule, const DefinitionInventory& inventory,
    const identity::ModuleKey& module, const DefinitionInventoryEntry& syntax,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const FrozenImplOccurrenceProjection> implementations,
    zc::Vector<FrozenDuplicateBoundProjection>& duplicateBounds) {
  auto name = declaredName(syntax, parsedModule);
  auto nameSpace = identity::definitionNamespaceFor(syntax.kind);
  auto owners = stableOwnerChain(syntax.parentPath.asPtr(), definitions, implementations);
  if (name == zc::none || nameSpace == zc::none || owners == zc::none) { return zc::none; }

  zc::Maybe<identity::OverloadHeaderAuthority> overload;
  zc::Maybe<identity::OverloadHeaderDigest> digest;
  if (callableDefinition(syntax.kind)) {
    const CanonicalHeaderSyntaxView syntaxView(inventory.definitions(), inventory.impls());
    auto reconstructed =
        CanonicalHeaderVerifier::reconstructDefinition(parsedModule.tree(), syntaxView, syntax);
    if (!reconstructed.is<VerifiedCanonicalDefinitionHeader>()) { return zc::none; }
    auto verified = zc::mv(reconstructed.get<VerifiedCanonicalDefinitionHeader>());
    if (!verified.authority.verify() ||
        !appendDuplicateBoundOracle(parsedModule, module, syntax.node,
                                    verified.boundOccurrences.asPtr(), duplicateBounds)) {
      return zc::none;
    }
    digest = verified.authority.digest().clone();
    overload = zc::mv(verified.authority);
  }

  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(namespaceValue, nameSpace) {
      ZC_IF_SOME(ownerValues, owners) {
        auto record = identity::DefinitionIdentityRecord::from(module.clone(), zc::mv(ownerValues),
                                                               syntax.kind, namespaceValue,
                                                               zc::mv(nameValue), zc::mv(digest));
        if (record == zc::none) { return zc::none; }
        ZC_IF_SOME(recordValue, record) {
          return identity::DefinitionIdentityAuthority::from(zc::mv(recordValue), zc::mv(overload));
        }
      }
    }
  }
  return zc::none;
}

zc::Maybe<identity::ImplIdentityAuthority> reconstructImplAuthority(
    const VerifiedParsedModule& parsedModule, const DefinitionInventory& inventory,
    const identity::ModuleKey& module, const ImplInventoryEntry& syntax,
    zc::ArrayPtr<const ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const FrozenImplOccurrenceProjection> implementations,
    zc::Vector<FrozenDuplicateBoundProjection>& duplicateBounds) {
  auto owners = stableOwnerChain(syntax.parentPath.asPtr(), definitions, implementations);
  if (owners == zc::none) { return zc::none; }
  const CanonicalHeaderSyntaxView syntaxView(inventory.definitions(), inventory.impls());
  auto reconstructed =
      CanonicalHeaderVerifier::reconstructImpl(parsedModule.tree(), syntaxView, syntax);
  if (!reconstructed.is<VerifiedCanonicalImplHeader>()) { return zc::none; }
  auto verified = zc::mv(reconstructed.get<VerifiedCanonicalImplHeader>());
  if (!appendDuplicateBoundOracle(parsedModule, module, syntax.node,
                                  verified.boundOccurrences.asPtr(), duplicateBounds)) {
    return zc::none;
  }
  ZC_IF_SOME(ownerValues, owners) {
    return identity::ImplIdentityAuthority::from(identity::ImplIdentityRecord::from(
        module.clone(), zc::mv(ownerValues), zc::mv(verified.header)));
  }
  return zc::none;
}

bool allRegistriesFrozen(const identity::SemanticIdentityRegistrySet& registries) {
  return registries.packages().isFrozen() && registries.crates().isFrozen() &&
         registries.sourceFiles().isFrozen() && registries.modules().isFrozen() &&
         registries.definitions().isFrozen() && registries.impls().isFrozen() &&
         registries.genericParameters().isFrozen() && registries.callableParameters().isFrozen();
}

bool definitionBelongsToModule(const identity::DefinitionRegistry& definitions,
                               const identity::DefinitionKey& key,
                               const identity::ModuleKey& module) {
  ZC_IF_SOME(handle, definitions.find(key)) {
    ZC_IF_SOME(record, definitions.lookupRecord(handle)) {
      return sameModule(record.module(), module);
    }
  }
  return false;
}

bool implBelongsToModule(const identity::ImplRegistry& impls, const identity::ImplKey& key,
                         const identity::ModuleKey& module) {
  ZC_IF_SOME(handle, impls.find(key)) {
    ZC_IF_SOME(record, impls.lookupRecord(handle)) { return sameModule(record.module(), module); }
  }
  return false;
}

bool genericParameterBelongsToModule(const identity::SemanticIdentityRegistrySet& registries,
                                     const identity::GenericParameterIdentityRecord& record,
                                     const identity::ModuleKey& module) {
  if (record.owner().kind() == identity::StableGenericParameterOwnerKind::Definition) {
    ZC_IF_SOME(key, record.owner().definitionKey()) {
      return definitionBelongsToModule(registries.definitions(), key, module);
    }
    return false;
  }
  ZC_IF_SOME(key, record.owner().implKey()) {
    return implBelongsToModule(registries.impls(), key, module);
  }
  return false;
}

bool callableParameterBelongsToModule(const identity::SemanticIdentityRegistrySet& registries,
                                      const identity::CallableParameterIdentityRecord& record,
                                      const identity::ModuleKey& module) {
  return definitionBelongsToModule(registries.definitions(), record.owner(), module);
}

size_t definitionCensus(const identity::DefinitionRegistry& registry,
                        const identity::ModuleKey& module) {
  size_t result = 0;
  for (size_t index = 0; index < registry.size(); ++index) {
    ZC_IF_SOME(record, registry.recordAt(index)) {
      if (sameModule(record.module(), module)) { ++result; }
    }
  }
  return result;
}

size_t implCensus(const identity::ImplRegistry& registry, const identity::ModuleKey& module) {
  size_t result = 0;
  for (size_t index = 0; index < registry.size(); ++index) {
    ZC_IF_SOME(record, registry.recordAt(index)) {
      if (sameModule(record.module(), module)) { ++result; }
    }
  }
  return result;
}

size_t genericParameterCensus(const identity::SemanticIdentityRegistrySet& registries,
                              const identity::ModuleKey& module) {
  size_t result = 0;
  for (size_t index = 0; index < registries.genericParameters().size(); ++index) {
    ZC_IF_SOME(record, registries.genericParameters().recordAt(index)) {
      if (genericParameterBelongsToModule(registries, record, module)) { ++result; }
    }
  }
  return result;
}

size_t callableParameterCensus(const identity::SemanticIdentityRegistrySet& registries,
                               const identity::ModuleKey& module) {
  size_t result = 0;
  for (size_t index = 0; index < registries.callableParameters().size(); ++index) {
    ZC_IF_SOME(record, registries.callableParameters().recordAt(index)) {
      if (callableParameterBelongsToModule(registries, record, module)) { ++result; }
    }
  }
  return result;
}

template <typename Handle>
bool containsHandle(const zc::Vector<Handle>& handles, Handle candidate) {
  for (const auto handle : handles) {
    if (handle == candidate) { return true; }
  }
  return false;
}

bool isReceiverParameter(const DefinitionInventoryEntry& entry,
                         const VerifiedParsedModule& parsedModule) {
  return parsedModule.tree().contains(entry.node) &&
         parsedModule.tree().node(entry.node).kind == ast::SyntaxKind::FunctionParameterDecl &&
         parsedModule.functionParameterNameSpan(entry.node, ast::SyntaxKind::ThisKeyword) !=
             zc::none;
}

bool hasCompleteStableOwnerChain(const DefinitionInventory& inventory,
                                 zc::ArrayPtr<const StructuralIdentityParent> parents) {
  for (const auto& parent : parents) {
    if (parent.kind == StructuralIdentityParentKind::Definition) {
      if (entryAt(inventory.definitions(), parent.node) == zc::none) { return false; }
    } else if (entryAt(inventory.impls(), parent.node) == zc::none) {
      return false;
    }
  }
  return true;
}

bool containsNode(zc::ArrayPtr<const ast::NodeId> nodes, ast::NodeId candidate) {
  for (const auto node : nodes) {
    if (node == candidate) { return true; }
  }
  return false;
}

bool hasAuthoritativeImmediateOwner(zc::ArrayPtr<const StructuralIdentityParent> parents,
                                    zc::ArrayPtr<const ast::NodeId> definitionAuthorities,
                                    zc::ArrayPtr<const ast::NodeId> implAuthorities) {
  if (parents.size() == 0) { return false; }
  const auto& parent = parents.back();
  return parent.kind == StructuralIdentityParentKind::Definition
             ? containsNode(definitionAuthorities, parent.node)
             : containsNode(implAuthorities, parent.node);
}

}  // namespace

struct FrozenDefinitionInventoryView::Impl final {
  using CreateResult = zc::OneOf<zc::Own<Impl>, FrozenInventoryInvariantKind>;

  static CreateResult create(identity::SemanticContextBrand context, identity::ModuleId module,
                             ast::NodeId moduleNode, const ast::Tree& tree,
                             zc::Vector<FrozenDefinitionEntry>&& definitions,
                             zc::Vector<FrozenGenericParameterEntry>&& genericParameters,
                             zc::Vector<FrozenCallableParameterEntry>&& callableParameters,
                             zc::Vector<FrozenOwnerLocalBindingEntry>&& ownerLocalBindings,
                             zc::Vector<FrozenAnonymousEntityEntry>&& anonymousEntities,
                             zc::Vector<FrozenImplAuthorityEntry>&& implAuthorities,
                             zc::Vector<FrozenImplOccurrenceEntry>&& implOccurrences) {
    if (!context.isValid() || !module.belongsTo(context) || !tree.contains(moduleNode) ||
        tree.nodeCount() >= static_cast<size_t>(UINT32_MAX)) {
      return FrozenInventoryInvariantKind::InputMismatch;
    }
    zc::Vector<uint8_t> occupied;
    occupied.resize(tree.nodeCount() + 1);
    for (auto& value : occupied) { value = 0; }
    const auto makeSlots = [&](auto& entries, zc::Vector<uint32_t>& slots) -> bool {
      if (entries.size() > static_cast<size_t>(UINT32_MAX)) { return false; }
      slots.resize(tree.nodeCount() + 1);
      for (auto& slot : slots) { slot = kMissing; }
      for (size_t index = 0; index < entries.size(); ++index) {
        const ast::NodeId node = entries[index].node;
        if (!tree.contains(node) || node.value >= slots.size() || occupied[node.value] != 0) {
          return false;
        }
        occupied[node.value] = 1;
        slots[node.value] = static_cast<uint32_t>(index);
      }
      return true;
    };

    zc::Vector<uint32_t> definitionSlots;
    zc::Vector<uint32_t> genericParameterSlots;
    zc::Vector<uint32_t> callableParameterSlots;
    zc::Vector<uint32_t> ownerLocalBindingSlots;
    zc::Vector<uint32_t> anonymousEntitySlots;
    zc::Vector<uint32_t> implOccurrenceSlots;
    if (!makeSlots(definitions, definitionSlots) ||
        !makeSlots(genericParameters, genericParameterSlots) ||
        !makeSlots(callableParameters, callableParameterSlots) ||
        !makeSlots(ownerLocalBindings, ownerLocalBindingSlots) ||
        !makeSlots(anonymousEntities, anonymousEntitySlots) ||
        !makeSlots(implOccurrences, implOccurrenceSlots) || occupied[moduleNode.value] != 0) {
      return FrozenInventoryInvariantKind::InvalidDefinitionSite;
    }
    return zc::heap<Impl>(
        context, module, moduleNode, zc::mv(definitions), zc::mv(genericParameters),
        zc::mv(callableParameters), zc::mv(ownerLocalBindings), zc::mv(anonymousEntities),
        zc::mv(implAuthorities), zc::mv(implOccurrences), zc::mv(definitionSlots),
        zc::mv(genericParameterSlots), zc::mv(callableParameterSlots),
        zc::mv(ownerLocalBindingSlots), zc::mv(anonymousEntitySlots), zc::mv(implOccurrenceSlots));
  }

  Impl(identity::SemanticContextBrand context, identity::ModuleId module, ast::NodeId moduleNode,
       zc::Vector<FrozenDefinitionEntry>&& definitions,
       zc::Vector<FrozenGenericParameterEntry>&& genericParameters,
       zc::Vector<FrozenCallableParameterEntry>&& callableParameters,
       zc::Vector<FrozenOwnerLocalBindingEntry>&& ownerLocalBindings,
       zc::Vector<FrozenAnonymousEntityEntry>&& anonymousEntities,
       zc::Vector<FrozenImplAuthorityEntry>&& implAuthorities,
       zc::Vector<FrozenImplOccurrenceEntry>&& implOccurrences,
       zc::Vector<uint32_t>&& definitionSlots, zc::Vector<uint32_t>&& genericParameterSlots,
       zc::Vector<uint32_t>&& callableParameterSlots, zc::Vector<uint32_t>&& ownerLocalBindingSlots,
       zc::Vector<uint32_t>&& anonymousEntitySlots, zc::Vector<uint32_t>&& implOccurrenceSlots)
      : context(context),
        module(module),
        moduleNode(moduleNode),
        definitions(zc::mv(definitions)),
        genericParameters(zc::mv(genericParameters)),
        callableParameters(zc::mv(callableParameters)),
        ownerLocalBindings(zc::mv(ownerLocalBindings)),
        anonymousEntities(zc::mv(anonymousEntities)),
        implAuthorities(zc::mv(implAuthorities)),
        implOccurrences(zc::mv(implOccurrences)),
        definitionSlots(zc::mv(definitionSlots)),
        genericParameterSlots(zc::mv(genericParameterSlots)),
        callableParameterSlots(zc::mv(callableParameterSlots)),
        ownerLocalBindingSlots(zc::mv(ownerLocalBindingSlots)),
        anonymousEntitySlots(zc::mv(anonymousEntitySlots)),
        implOccurrenceSlots(zc::mv(implOccurrenceSlots)) {}

  identity::SemanticContextBrand context;
  identity::ModuleId module;
  ast::NodeId moduleNode;
  zc::Vector<FrozenDefinitionEntry> definitions;
  zc::Vector<FrozenGenericParameterEntry> genericParameters;
  zc::Vector<FrozenCallableParameterEntry> callableParameters;
  zc::Vector<FrozenOwnerLocalBindingEntry> ownerLocalBindings;
  zc::Vector<FrozenAnonymousEntityEntry> anonymousEntities;
  zc::Vector<FrozenImplAuthorityEntry> implAuthorities;
  zc::Vector<FrozenImplOccurrenceEntry> implOccurrences;
  zc::Vector<uint32_t> definitionSlots;
  zc::Vector<uint32_t> genericParameterSlots;
  zc::Vector<uint32_t> callableParameterSlots;
  zc::Vector<uint32_t> ownerLocalBindingSlots;
  zc::Vector<uint32_t> anonymousEntitySlots;
  zc::Vector<uint32_t> implOccurrenceSlots;
};

FrozenDefinitionInventoryView::FrozenDefinitionInventoryView(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
FrozenDefinitionInventoryView::~FrozenDefinitionInventoryView() noexcept(false) = default;
FrozenDefinitionInventoryView::FrozenDefinitionInventoryView(
    FrozenDefinitionInventoryView&&) noexcept = default;
FrozenDefinitionInventoryView& FrozenDefinitionInventoryView::operator=(
    FrozenDefinitionInventoryView&&) noexcept = default;

identity::SemanticContextBrand FrozenDefinitionInventoryView::semanticContext() const noexcept {
  return impl->context;
}
identity::ModuleId FrozenDefinitionInventoryView::module() const noexcept { return impl->module; }
ast::NodeId FrozenDefinitionInventoryView::moduleNode() const noexcept { return impl->moduleNode; }
zc::ArrayPtr<const FrozenDefinitionEntry> FrozenDefinitionInventoryView::definitions()
    const noexcept {
  return impl->definitions.asPtr();
}
zc::ArrayPtr<const FrozenGenericParameterEntry> FrozenDefinitionInventoryView::genericParameters()
    const noexcept {
  return impl->genericParameters.asPtr();
}
zc::ArrayPtr<const FrozenCallableParameterEntry> FrozenDefinitionInventoryView::callableParameters()
    const noexcept {
  return impl->callableParameters.asPtr();
}
zc::ArrayPtr<const FrozenOwnerLocalBindingEntry> FrozenDefinitionInventoryView::ownerLocalBindings()
    const noexcept {
  return impl->ownerLocalBindings.asPtr();
}
zc::ArrayPtr<const FrozenAnonymousEntityEntry> FrozenDefinitionInventoryView::anonymousEntities()
    const noexcept {
  return impl->anonymousEntities.asPtr();
}
zc::ArrayPtr<const FrozenImplAuthorityEntry> FrozenDefinitionInventoryView::implAuthorities()
    const noexcept {
  return impl->implAuthorities.asPtr();
}
zc::ArrayPtr<const FrozenImplOccurrenceEntry> FrozenDefinitionInventoryView::impls()
    const noexcept {
  return impl->implOccurrences.asPtr();
}

zc::Maybe<const identity::DefinitionKey&> FrozenDefinitionInventoryView::definitionKey(
    identity::DefId definition) const {
  for (const auto& entry : impl->definitions) {
    if (entry.definition == definition) { return entry.key; }
  }
  return zc::none;
}
zc::Maybe<const identity::DefinitionIdentityRecord&>
FrozenDefinitionInventoryView::definitionRecord(identity::DefId definition) const {
  for (const auto& entry : impl->definitions) {
    if (entry.definition == definition) { return entry.record; }
  }
  return zc::none;
}
zc::Maybe<const identity::GenericParameterKey&> FrozenDefinitionInventoryView::genericParameterKey(
    identity::GenericParameterId parameter) const {
  for (const auto& entry : impl->genericParameters) {
    if (entry.parameter == parameter) { return entry.key; }
  }
  return zc::none;
}
zc::Maybe<const identity::GenericParameterIdentityRecord&>
FrozenDefinitionInventoryView::genericParameterRecord(
    identity::GenericParameterId parameter) const {
  for (const auto& entry : impl->genericParameters) {
    if (entry.parameter == parameter) { return entry.record; }
  }
  return zc::none;
}
zc::Maybe<const identity::CallableParameterKey&>
FrozenDefinitionInventoryView::callableParameterKey(identity::CallableParameterId parameter) const {
  for (const auto& entry : impl->callableParameters) {
    if (entry.parameter == parameter) { return entry.key; }
  }
  return zc::none;
}
zc::Maybe<const identity::CallableParameterIdentityRecord&>
FrozenDefinitionInventoryView::callableParameterRecord(
    identity::CallableParameterId parameter) const {
  for (const auto& entry : impl->callableParameters) {
    if (entry.parameter == parameter) { return entry.record; }
  }
  return zc::none;
}
zc::Maybe<const identity::ImplKey&> FrozenDefinitionInventoryView::implKey(
    identity::ImplId implementation) const {
  for (const auto& entry : impl->implAuthorities) {
    if (entry.implementation == implementation) { return entry.key; }
  }
  return zc::none;
}
zc::Maybe<const identity::ImplIdentityRecord&> FrozenDefinitionInventoryView::implRecord(
    identity::ImplId implementation) const {
  for (const auto& entry : impl->implAuthorities) {
    if (entry.implementation == implementation) { return entry.record; }
  }
  return zc::none;
}

template <typename Entry, typename Value, typename Project>
zc::Maybe<Value> valueAt(ast::NodeId node, zc::ArrayPtr<const Entry> entries,
                         zc::ArrayPtr<const uint32_t> slots, Project&& project) {
  if (!node || node.value >= slots.size()) { return zc::none; }
  const uint32_t slot = slots[node.value];
  if (slot == kMissing || slot >= entries.size() || entries[slot].node != node) { return zc::none; }
  return project(entries[slot]);
}

zc::Maybe<identity::DefId> FrozenDefinitionInventoryView::definitionAt(ast::NodeId node) const {
  return valueAt<FrozenDefinitionEntry, identity::DefId>(
      node, impl->definitions.asPtr(), impl->definitionSlots.asPtr(),
      [](const FrozenDefinitionEntry& entry) { return entry.definition; });
}

zc::Maybe<identity::GenericParameterId> FrozenDefinitionInventoryView::genericParameterAt(
    ast::NodeId node) const {
  return valueAt<FrozenGenericParameterEntry, identity::GenericParameterId>(
      node, impl->genericParameters.asPtr(), impl->genericParameterSlots.asPtr(),
      [](const FrozenGenericParameterEntry& entry) { return entry.parameter; });
}

zc::Maybe<identity::CallableParameterId> FrozenDefinitionInventoryView::callableParameterAt(
    ast::NodeId node) const {
  return valueAt<FrozenCallableParameterEntry, identity::CallableParameterId>(
      node, impl->callableParameters.asPtr(), impl->callableParameterSlots.asPtr(),
      [](const FrozenCallableParameterEntry& entry) { return entry.parameter; });
}

zc::Maybe<OwnerLocalBindingId> FrozenDefinitionInventoryView::ownerLocalBindingAt(
    ast::NodeId node) const {
  return valueAt<FrozenOwnerLocalBindingEntry, OwnerLocalBindingId>(
      node, impl->ownerLocalBindings.asPtr(), impl->ownerLocalBindingSlots.asPtr(),
      [](const FrozenOwnerLocalBindingEntry& entry) { return entry.binding; });
}

zc::Maybe<const FrozenAnonymousEntityEntry&> FrozenDefinitionInventoryView::anonymousEntityAt(
    ast::NodeId node) const {
  if (!node || node.value >= impl->anonymousEntitySlots.size()) { return zc::none; }
  const uint32_t slot = impl->anonymousEntitySlots[node.value];
  if (slot == kMissing || slot >= impl->anonymousEntities.size() ||
      impl->anonymousEntities[slot].node != node) {
    return zc::none;
  }
  return impl->anonymousEntities[slot];
}

zc::Maybe<ImplOccurrenceId> FrozenDefinitionInventoryView::implAt(ast::NodeId node) const {
  return valueAt<FrozenImplOccurrenceEntry, ImplOccurrenceId>(
      node, impl->implOccurrences.asPtr(), impl->implOccurrenceSlots.asPtr(),
      [](const FrozenImplOccurrenceEntry& entry) { return entry.occurrence; });
}

zc::Maybe<identity::ImplId> FrozenDefinitionInventoryView::implAuthority(
    ImplOccurrenceId occurrence) const {
  for (const auto& entry : impl->implOccurrences) {
    if (entry.occurrence == occurrence) { return entry.authority; }
  }
  return zc::none;
}

FrozenDefinitionInventoryResult FrozenDefinitionInventoryVerifier::verifySingleModule(
    identity::SemanticContextBrand context, identity::ModuleId module,
    const VerifiedParsedModule& parsedModule,
    const identity::SemanticIdentityRegistrySet& registries,
    FrozenDefinitionInventoryInput&& input) {
  if (!context.isValid() || !module.belongsTo(context) || registries.context() != context ||
      !parsedModule.sourceFile().belongsTo(context) || !allRegistriesFrozen(registries)) {
    return failure(FrozenInventoryInvariantKind::InputMismatch);
  }
  auto moduleKey = registries.modules().lookup(module);
  auto sourceKey = registries.sourceFiles().lookup(parsedModule.sourceFile());
  if (moduleKey == zc::none || sourceKey == zc::none) {
    return failure(FrozenInventoryInvariantKind::InputMismatch);
  }

  const auto inventory = DefinitionInventory::collect(parsedModule.tree());
  if (inventory.modules().size() > 1) {
    return failure(FrozenInventoryInvariantKind::IncompleteInventory);
  }

  zc::Vector<size_t> eligibleDefinitionIndices;
  for (size_t index = 0; index < inventory.definitions().size(); ++index) {
    if (hasCompleteStableOwnerChain(inventory, inventory.definitions()[index].parentPath.asPtr())) {
      eligibleDefinitionIndices.add(index);
    }
  }
  zc::Vector<size_t> eligibleImplIndices;
  for (size_t index = 0; index < inventory.impls().size(); ++index) {
    if (hasCompleteStableOwnerChain(inventory, inventory.impls()[index].parentPath.asPtr())) {
      eligibleImplIndices.add(index);
    }
  }
  if (input.definitionCandidates.size() != eligibleDefinitionIndices.size() ||
      input.implOccurrences.size() != eligibleImplIndices.size()) {
    return failure(FrozenInventoryInvariantKind::IncompleteInventory);
  }

  ZC_IF_SOME(moduleValue, moduleKey) {
    ZC_IF_SOME(sourceValue, sourceKey) {
      if (!sameSource(parsedModule.source(), sourceValue) ||
          !sourceValue.belongsTo(moduleValue.crate())) {
        return failure(FrozenInventoryInvariantKind::InputMismatch);
      }
      const ast::NodeId moduleNode = inventory.modules().size() == 1 ? inventory.modules()[0].node
                                                                     : parsedModule.tree().root();
      const ast::NodeId syntaxModuleNode =
          inventory.modules().size() == 1 ? moduleNode : ast::NodeId();
      if (!parsedModule.tree().contains(moduleNode)) {
        return failure(FrozenInventoryInvariantKind::InvalidDefinitionSite);
      }
      ZC_IF_SOME(localAllocatorValue, ModuleLocalIdentityAllocator::create(context, module)) {
        auto localAllocator = zc::mv(localAllocatorValue);
        zc::Vector<FrozenDefinitionEntry> definitions;
        zc::Vector<identity::DefId> definitionHandles;
        zc::Vector<ast::NodeId> definitionAuthorityNodes;
        zc::Vector<FrozenDuplicateBoundProjection> expectedDuplicateBounds;

        for (size_t index = 0; index < eligibleDefinitionIndices.size(); ++index) {
          const auto& syntax = inventory.definitions()[eligibleDefinitionIndices[index]];
          const auto& projection = input.definitionCandidates[index];
          if (syntax.node != projection.node || syntax.moduleNode != syntaxModuleNode) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionSite);
          }
          auto expected = reconstructDefinitionAuthority(
              parsedModule, inventory, moduleValue, syntax, input.definitionCandidates.asPtr(),
              input.implOccurrences.asPtr(), expectedDuplicateBounds);
          if (expected == zc::none) {
            return failure(FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
          }
          ZC_IF_SOME(expectedValue, expected) {
            if (expectedValue.key() != projection.key) {
              return failure(FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
            }
            auto handle = registries.definitions().find(projection.key);
            if (handle == zc::none) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
            ZC_IF_SOME(definition, handle) {
              auto authority = registries.definitions().lookupAuthority(definition);
              if (authority == zc::none) {
                return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
              }
              ZC_IF_SOME(authorityValue, authority) {
                if (!authorityValue.verify() || !authorityValue.sameRecordAs(expectedValue)) {
                  return failure(FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
                }
              }
            }
            auto span = parsedModule.spanFor(syntax.source);
            auto name = declaredName(syntax, parsedModule);
            if (span == zc::none || name == zc::none) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
            ZC_IF_SOME(definition, handle) {
              if (!containsHandle(definitionHandles, definition)) {
                definitionHandles.add(definition);
                definitionAuthorityNodes.add(syntax.node);
                ZC_IF_SOME(spanValue, span) {
                  definitions.add(FrozenDefinitionEntry{
                      syntax.node, syntax.site.clone(), definition, projection.key.clone(),
                      expectedValue.record().clone(), zc::mv(name), zc::mv(spanValue)});
                }
              }
            }
          }
        }
        if (definitionHandles.size() != definitionCensus(registries.definitions(), moduleValue)) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        zc::Vector<ast::NodeId> implAuthorityNodes;
        zc::Vector<identity::ImplId> projectedImplHandles;
        for (size_t index = 0; index < eligibleImplIndices.size(); ++index) {
          const auto& syntax = inventory.impls()[eligibleImplIndices[index]];
          const auto& projection = input.implOccurrences[index];
          auto expected = reconstructImplAuthority(
              parsedModule, inventory, moduleValue, syntax, input.definitionCandidates.asPtr(),
              input.implOccurrences.asPtr(), expectedDuplicateBounds);
          if (expected == zc::none) {
            return failure(FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
          }
          ZC_IF_SOME(expectedValue, expected) {
            if (expectedValue.key() != projection.key.implementation()) {
              return failure(FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
            }
          }
          auto authority = registries.impls().find(projection.key.implementation());
          if (syntax.node != projection.node || authority == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(authorityValue, authority) {
            auto retained = registries.impls().lookupAuthority(authorityValue);
            if (retained == zc::none) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
            ZC_IF_SOME(retainedValue, retained) {
              ZC_IF_SOME(expectedValue, expected) {
                if (!retainedValue.verify() || !retainedValue.sameRecordAs(expectedValue)) {
                  return failure(FrozenInventoryInvariantKind::CanonicalHeaderMismatch);
                }
              }
            }
            if (!containsHandle(projectedImplHandles, authorityValue)) {
              projectedImplHandles.add(authorityValue);
              implAuthorityNodes.add(syntax.node);
            }
          }
        }

        sortDuplicateBounds(expectedDuplicateBounds);
        if (!sameDuplicateBounds(expectedDuplicateBounds.asPtr(), input.duplicateBounds.asPtr())) {
          return failure(FrozenInventoryInvariantKind::DuplicateBoundMismatch);
        }

        zc::Vector<size_t> genericParameterIndices;
        for (size_t index = 0; index < inventory.genericParameters().size(); ++index) {
          if (hasAuthoritativeImmediateOwner(
                  inventory.genericParameters()[index].parentPath.asPtr(),
                  definitionAuthorityNodes.asPtr(), implAuthorityNodes.asPtr())) {
            genericParameterIndices.add(index);
          }
        }
        if (input.genericParameters.size() != genericParameterIndices.size()) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        zc::Vector<FrozenGenericParameterEntry> genericParameters;
        for (size_t index = 0; index < genericParameterIndices.size(); ++index) {
          const size_t syntaxIndex = genericParameterIndices[index];
          const auto& syntax = inventory.genericParameters()[syntaxIndex];
          const auto& projection = input.genericParameters[index];
          if (syntax.node != projection.node || syntax.moduleNode != syntaxModuleNode) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionSite);
          }
          auto owner =
              immediateStableOwner(syntax.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                                   input.implOccurrences.asPtr());
          if (owner == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          uint32_t ordinal = 0;
          for (size_t prior = 0; prior < syntaxIndex; ++prior) {
            const auto& candidate = inventory.genericParameters()[prior];
            if (!candidate.parentPath.empty() && !syntax.parentPath.empty() &&
                candidate.parentPath.back().node == syntax.parentPath.back().node &&
                hasAuthoritativeImmediateOwner(candidate.parentPath.asPtr(),
                                               definitionAuthorityNodes.asPtr(),
                                               implAuthorityNodes.asPtr())) {
              ++ordinal;
            }
          }
          ZC_IF_SOME(ownerValue, owner) {
            auto expectedRecord =
                identity::GenericParameterIdentityRecord::type(zc::mv(ownerValue), ordinal);
            if (identity::GenericParameterKey::compute(expectedRecord) != projection.key) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          auto parameter = registries.genericParameters().find(projection.key);
          auto name = declaredName(syntax, parsedModule);
          auto span = parsedModule.spanFor(syntax.source);
          if (parameter == zc::none || name == zc::none || span == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(parameterValue, parameter) {
            ZC_IF_SOME(record, registries.genericParameters().lookupRecord(parameterValue)) {
              if (!genericParameterBelongsToModule(registries, record, moduleValue)) {
                return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
              }
              ZC_IF_SOME(nameValue, name) {
                ZC_IF_SOME(spanValue, span) {
                  genericParameters.add(FrozenGenericParameterEntry{
                      syntax.node, syntax.site.clone(), parameterValue, projection.key.clone(),
                      record.clone(), zc::mv(nameValue), zc::mv(spanValue)});
                }
              }
            } else {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
        }
        if (genericParameters.size() != genericParameterCensus(registries, moduleValue)) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        zc::Vector<size_t> callableParameterIndices;
        for (size_t index = 0; index < inventory.callableParameters().size(); ++index) {
          if (hasAuthoritativeImmediateOwner(
                  inventory.callableParameters()[index].parentPath.asPtr(),
                  definitionAuthorityNodes.asPtr(), implAuthorityNodes.asPtr())) {
            callableParameterIndices.add(index);
          }
        }
        if (input.callableParameters.size() != callableParameterIndices.size()) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        zc::Vector<FrozenCallableParameterEntry> callableParameters;
        for (size_t index = 0; index < callableParameterIndices.size(); ++index) {
          const size_t syntaxIndex = callableParameterIndices[index];
          const auto& syntax = inventory.callableParameters()[syntaxIndex];
          const auto& projection = input.callableParameters[index];
          if (syntax.node != projection.node || syntax.moduleNode != syntaxModuleNode) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionSite);
          }
          auto owner = immediateDefinitionOwner(syntax.parentPath.asPtr(),
                                                input.definitionCandidates.asPtr());
          if (owner == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          const bool receiver = isReceiverParameter(syntax, parsedModule);
          uint32_t ordinal = 0;
          if (!receiver) {
            for (size_t prior = 0; prior < syntaxIndex; ++prior) {
              const auto& candidate = inventory.callableParameters()[prior];
              if (!candidate.parentPath.empty() && !syntax.parentPath.empty() &&
                  candidate.parentPath.back().node == syntax.parentPath.back().node &&
                  !isReceiverParameter(candidate, parsedModule) &&
                  hasAuthoritativeImmediateOwner(candidate.parentPath.asPtr(),
                                                 definitionAuthorityNodes.asPtr(),
                                                 implAuthorityNodes.asPtr())) {
                ++ordinal;
              }
            }
          }
          ZC_IF_SOME(ownerValue, owner) {
            auto expectedRecord = identity::CallableParameterIdentityRecord::from(
                ownerValue.clone(), receiver
                                        ? identity::CallableParameterPosition::receiver()
                                        : identity::CallableParameterPosition::ordinary(ordinal));
            if (identity::CallableParameterKey::compute(expectedRecord) != projection.key) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          auto parameter = registries.callableParameters().find(projection.key);
          zc::Maybe<identity::DeclaredDefinitionName> name;
          if (!receiver) { name = declaredName(syntax, parsedModule); }
          auto span = parsedModule.spanFor(syntax.source);
          if (parameter == zc::none || span == zc::none || (!receiver && name == zc::none)) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(parameterValue, parameter) {
            ZC_IF_SOME(record, registries.callableParameters().lookupRecord(parameterValue)) {
              if (!callableParameterBelongsToModule(registries, record, moduleValue)) {
                return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
              }
              ZC_IF_SOME(spanValue, span) {
                callableParameters.add(FrozenCallableParameterEntry{
                    syntax.node, syntax.site.clone(), parameterValue, projection.key.clone(),
                    record.clone(), zc::mv(name), zc::mv(spanValue)});
              }
            } else {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
        }
        if (callableParameters.size() != callableParameterCensus(registries, moduleValue)) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        enum class OwnerLocalSyntaxDomain : uint8_t {
          Binding,
          AnonymousGenericParameter,
          AnonymousCallableParameter
        };
        struct OwnerLocalSyntaxReference final {
          OwnerLocalSyntaxDomain domain;
          size_t index;
        };

        zc::Vector<OwnerLocalSyntaxReference> ownerLocalSyntax;
        for (size_t index = 0; index < inventory.ownerLocalBindings().size(); ++index) {
          ownerLocalSyntax.add(OwnerLocalSyntaxReference{OwnerLocalSyntaxDomain::Binding, index});
        }
        for (size_t index = 0; index < inventory.genericParameters().size(); ++index) {
          if (immediateAnonymousOwnerNode(
                  inventory, inventory.genericParameters()[index].parentPath.asPtr()) != zc::none) {
            ownerLocalSyntax.add(OwnerLocalSyntaxReference{
                OwnerLocalSyntaxDomain::AnonymousGenericParameter, index});
          }
        }
        for (size_t index = 0; index < inventory.callableParameters().size(); ++index) {
          if (immediateAnonymousOwnerNode(
                  inventory, inventory.callableParameters()[index].parentPath.asPtr()) !=
              zc::none) {
            ownerLocalSyntax.add(OwnerLocalSyntaxReference{
                OwnerLocalSyntaxDomain::AnonymousCallableParameter, index});
          }
        }
        if (input.ownerLocalBindings.size() != ownerLocalSyntax.size()) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        zc::Vector<FrozenOwnerLocalBindingEntry> ownerLocalBindings;
        for (size_t index = 0; index < ownerLocalSyntax.size(); ++index) {
          const auto reference = ownerLocalSyntax[index];
          const auto& syntax =
              reference.domain == OwnerLocalSyntaxDomain::Binding
                  ? inventory.ownerLocalBindings()[reference.index]
                  : (reference.domain == OwnerLocalSyntaxDomain::AnonymousGenericParameter
                         ? inventory.genericParameters()[reference.index]
                         : inventory.callableParameters()[reference.index]);
          const auto& projection = input.ownerLocalBindings[index];
          auto expectedId = localAllocator.allocateOwnerLocalBinding();
          auto owner =
              stableBodyOwner(syntax.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                              definitionAuthorityNodes.asPtr(), moduleValue);
          auto path = stableBodyPath(
              parsedModule.tree(), syntax.parentPath.asPtr(), input.definitionCandidates.asPtr(),
              definitionAuthorityNodes.asPtr(), syntaxModuleNode, syntax.node);
          auto name = declaredName(syntax, parsedModule);
          auto span = parsedModule.spanFor(syntax.source);
          if (syntax.node != projection.node || syntax.moduleNode != syntaxModuleNode ||
              expectedId == zc::none || owner == zc::none || path == zc::none || name == zc::none ||
              span == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(expectedIdValue, expectedId) {
            if (expectedIdValue != projection.binding) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(ownerValue, owner) {
            if (projection.key.owner() != ownerValue) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          OwnerLocalBindingKind expectedKind = OwnerLocalBindingKind::PatternBinding;
          OwnerLocalBindingNamespace expectedNamespace = OwnerLocalBindingNamespace::Value;
          if (reference.domain == OwnerLocalSyntaxDomain::AnonymousGenericParameter) {
            expectedKind = OwnerLocalBindingKind::GenericParameter;
            expectedNamespace = OwnerLocalBindingNamespace::Type;
          } else if (reference.domain == OwnerLocalSyntaxDomain::AnonymousCallableParameter) {
            expectedKind = OwnerLocalBindingKind::CallableParameter;
          } else if (syntax.kind == identity::DefinitionKind::Local) {
            expectedKind = OwnerLocalBindingKind::Local;
          }
          if (projection.key.kind() != expectedKind ||
              projection.key.nameSpace() != expectedNamespace) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(nameValue, name) {
            if (projection.key.name() != nameValue) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(pathValue, path) {
            if (projection.key.path() != pathValue) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          if (reference.domain != OwnerLocalSyntaxDomain::Binding) {
            auto anonymousNode = immediateAnonymousOwnerNode(inventory, syntax.parentPath.asPtr());
            if (anonymousNode == zc::none) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
            ZC_IF_SOME(anonymousNodeValue, anonymousNode) {
              const bool belongsToAnonymousList =
                  reference.domain == OwnerLocalSyntaxDomain::AnonymousGenericParameter
                      ? anonymousOwnsGenericParameter(parsedModule.tree(), anonymousNodeValue,
                                                      syntax.node)
                      : anonymousOwnsCallableParameter(parsedModule.tree(), anonymousNodeValue,
                                                       syntax.node);
              if (!belongsToAnonymousList ||
                  (reference.domain == OwnerLocalSyntaxDomain::AnonymousCallableParameter &&
                   isReceiverParameter(syntax, parsedModule))) {
                return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
              }
              zc::Maybe<const FrozenAnonymousEntityProjection&> anonymousProjection;
              for (const auto& candidate : input.anonymousEntities) {
                if (candidate.node == anonymousNodeValue) {
                  anonymousProjection = candidate;
                  break;
                }
              }
              if (anonymousProjection == zc::none) {
                return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
              }
              ZC_IF_SOME(anonymousValue, anonymousProjection) {
                if (anonymousValue.key.owner() != projection.key.owner()) {
                  return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
                }
                auto expectedAnonymousPath = stableBodyPath(
                    parsedModule.tree(), syntax.parentPath.asPtr(),
                    input.definitionCandidates.asPtr(), definitionAuthorityNodes.asPtr(),
                    syntaxModuleNode, anonymousNodeValue);
                if (expectedAnonymousPath == zc::none) {
                  return failure(FrozenInventoryInvariantKind::InvalidDefinitionSite);
                }
                ZC_IF_SOME(pathValue, expectedAnonymousPath) {
                  if (anonymousValue.key.path() != pathValue) {
                    return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
                  }
                }
              }
            }
          }
          ZC_IF_SOME(spanValue, span) {
            ownerLocalBindings.add(
                FrozenOwnerLocalBindingEntry{syntax.node, syntax.site.clone(), projection.binding,
                                             projection.key.clone(), zc::mv(spanValue)});
          }
        }

        if (input.anonymousEntities.size() != inventory.anonymousEntities().size()) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        zc::Vector<FrozenAnonymousEntityEntry> anonymousEntities;
        for (size_t index = 0; index < inventory.anonymousEntities().size(); ++index) {
          const auto& syntax = inventory.anonymousEntities()[index];
          const auto& projection = input.anonymousEntities[index];
          auto owner =
              stableBodyOwner(syntax.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                              definitionAuthorityNodes.asPtr(), moduleValue);
          auto path = stableBodyPath(
              parsedModule.tree(), syntax.parentPath.asPtr(), input.definitionCandidates.asPtr(),
              definitionAuthorityNodes.asPtr(), syntaxModuleNode, syntax.node);
          auto span = parsedModule.spanFor(syntax.source);
          if (syntax.node != projection.node || syntax.moduleNode != syntaxModuleNode ||
              owner == zc::none || path == zc::none || span == zc::none ||
              syntax.anonymousRole == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(ownerValue, owner) {
            if (projection.key.owner() != ownerValue) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(role, syntax.anonymousRole) {
            const auto expectedRole = role == AnonymousSyntaxRole::FunctionExpression
                                          ? AnonymousOwnerLocalRole::FunctionExpression
                                          : AnonymousOwnerLocalRole::Closure;
            if (projection.key.role() != expectedRole) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(pathValue, path) {
            if (projection.key.path() != pathValue) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(spanValue, span) {
            anonymousEntities.add(FrozenAnonymousEntityEntry{
                syntax.node, syntax.site.clone(), projection.key.clone(), zc::mv(spanValue)});
          }
        }

        zc::Vector<FrozenImplAuthorityEntry> implAuthorities;
        zc::Vector<identity::ImplId> implHandles;
        zc::Vector<FrozenImplOccurrenceEntry> implOccurrences;
        for (size_t index = 0; index < eligibleImplIndices.size(); ++index) {
          const auto& syntax = inventory.impls()[eligibleImplIndices[index]];
          auto& projection = input.implOccurrences[index];
          auto expectedId = localAllocator.allocateImplOccurrence();
          auto expectedSite = moduleSiteKey(parsedModule, moduleValue, syntax.node);
          auto authority = registries.impls().find(projection.key.implementation());
          auto span = parsedModule.spanFor(syntax.source);
          if (syntax.node != projection.node || syntax.moduleNode != syntaxModuleNode ||
              expectedId == zc::none || expectedSite == zc::none || authority == zc::none ||
              span == zc::none) {
            return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
          }
          ZC_IF_SOME(expectedIdValue, expectedId) {
            if (expectedIdValue != projection.occurrence) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(siteValue, expectedSite) {
            if (!projection.key.site().sameAs(siteValue)) {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
          ZC_IF_SOME(authorityValue, authority) {
            ZC_IF_SOME(record, registries.impls().lookupRecord(authorityValue)) {
              if (!sameModule(record.module(), moduleValue)) {
                return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
              }
              if (!containsHandle(implHandles, authorityValue)) {
                implHandles.add(authorityValue);
                implAuthorities.add(FrozenImplAuthorityEntry{
                    authorityValue, projection.key.implementation().clone(), record.clone()});
              }
              ZC_IF_SOME(spanValue, span) {
                implOccurrences.add(
                    FrozenImplOccurrenceEntry{projection.occurrence, zc::mv(projection.key),
                                              authorityValue, syntax.node, zc::mv(spanValue)});
              }
            } else {
              return failure(FrozenInventoryInvariantKind::InvalidDefinitionIdentity);
            }
          }
        }
        if (implHandles.size() != implCensus(registries.impls(), moduleValue)) {
          return failure(FrozenInventoryInvariantKind::IncompleteInventory);
        }

        auto result = FrozenDefinitionInventoryView::Impl::create(
            context, module, moduleNode, parsedModule.tree(), zc::mv(definitions),
            zc::mv(genericParameters), zc::mv(callableParameters), zc::mv(ownerLocalBindings),
            zc::mv(anonymousEntities), zc::mv(implAuthorities), zc::mv(implOccurrences));
        if (result.is<FrozenInventoryInvariantKind>()) {
          return failure(result.get<FrozenInventoryInvariantKind>());
        }
        return FrozenDefinitionInventoryView(
            zc::mv(result.get<zc::Own<FrozenDefinitionInventoryView::Impl>>()));
      }
      return failure(FrozenInventoryInvariantKind::InputMismatch);
    }
  }
  return failure(FrozenInventoryInvariantKind::InputMismatch);
}

}  // namespace zomlang::compiler::binder
