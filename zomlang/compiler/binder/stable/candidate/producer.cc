// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/stable/candidate/producer.h"

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/binder/canonical/canonical-definition-header-producer.h"
#include "zomlang/compiler/binder/canonical/canonical-impl-header-producer.h"

namespace zomlang::compiler::binder {

struct StableIdentityCandidateInventory::Impl final {
  zc::Vector<IdentitySyntaxSite> sites;
  zc::Vector<PreAdmissionIdentityCandidate> candidates;
  zc::Vector<ProducedDefinitionIdentity> definitions;
  zc::Vector<ProducedImplIdentity> implementations;
  zc::Vector<ProducedDefinitionIdentitySite> definitionSites;
  zc::Vector<ProducedImplIdentitySite> implementationSites;
};

namespace {

struct StableInventoryReference final {
  bool implementation;
  size_t slot;
  ast::NodeId node;
};

zc::Vector<uint32_t> clonePath(zc::ArrayPtr<const uint32_t> path) {
  zc::Vector<uint32_t> result(path.size());
  result.addAll(path);
  return result;
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

bool callableDefinition(identity::DefinitionKind kind) noexcept {
  return kind == identity::DefinitionKind::Function || kind == identity::DefinitionKind::Method ||
         kind == identity::DefinitionKind::Constructor;
}

ast::NodeId definitionGenericParameters(const ast::Tree& tree, ast::NodeId node) {
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

ast::NodeId implementationGenericParameters(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node)) { return {}; }
  const auto& syntax = tree.node(node);
  if (syntax.kind != ast::SyntaxKind::StandaloneImplDecl) { return {}; }
  return ast::NodeId(syntax.payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
}

class IdentitySyntaxSiteInventoryProducerImpl final {
public:
  IdentitySyntaxSiteInventoryProducerImpl(const CanonicalParsedModule& parsedModule,
                                          const identity::ModuleKey& module, ast::NodeId moduleNode)
      : parsedModule(parsedModule),
        tree(parsedModule.tree()),
        module(module),
        moduleNode(moduleNode),
        inventory(DefinitionInventory::collect(tree)) {
    paths.resize(tree.nodeCount() + 1);
    ordinals.resize(tree.nodeCount() + 1);
    seen.resize(tree.nodeCount() + 1);
    identityRoot.resize(tree.nodeCount() + 1);
    for (auto& value : seen) { value = 0; }
    for (auto& value : identityRoot) { value = 0; }
  }

  zc::Maybe<IdentitySyntaxSiteInventory> produce() {
    if (!tree.contains(tree.root()) || tree.nodeCount() > UINT32_MAX ||
        !parsedModule.source().belongsTo(module.crate()) ||
        (moduleNode && (!tree.contains(moduleNode) ||
                        tree.node(moduleNode).kind != ast::SyntaxKind::ModuleDeclaration))) {
      return zc::none;
    }
    for (const auto& definition : inventory.definitions()) {
      if (selectedDefinition(definition) && tree.contains(definition.node)) {
        identityRoot[definition.node.value] = 1;
      }
    }
    for (const auto& implementation : inventory.impls()) {
      if (implementation.moduleNode == moduleNode && tree.contains(implementation.node)) {
        identityRoot[implementation.node.value] = 1;
      }
    }
    zc::Vector<uint32_t> path;
    uint32_t ordinal = 0;
    if (!collect(tree.root(), false, path, ordinal) || ordinal != tree.nodeCount()) {
      return zc::none;
    }
    return IdentitySyntaxSiteInventory::fromVerified(
        module.clone(), parsedModule.source().clone(), parsedModule.contentDigest(),
        static_cast<uint32_t>(tree.nodeCount()), zc::mv(entries));
  }

private:
  bool selectedDefinition(const DefinitionInventoryEntry& definition) const noexcept {
    return definition.moduleNode == moduleNode || (!definition.moduleNode && !moduleNode) ||
           (!definition.moduleNode && definition.node == moduleNode &&
            definition.kind == identity::DefinitionKind::ModuleAlias);
  }

  bool collect(ast::NodeId node, bool insideIdentity, zc::Vector<uint32_t>& path,
               uint32_t& ordinal) {
    if (!tree.contains(node) || node.value >= seen.size() || seen[node.value] != 0) {
      return false;
    }
    seen[node.value] = 1;
    paths[node.value] = clonePath(path.asPtr());
    ordinals[node.value] = ordinal++;
    insideIdentity = insideIdentity || identityRoot[node.value] != 0;
    if (insideIdentity) {
      auto key = IdentitySyntaxSiteKey::from(module.clone(), parsedModule.source().clone(),
                                             clonePath(path.asPtr()));
      auto span = parsedModule.spanFor(tree.node(node).range);
      if (key == zc::none || span == zc::none) { return false; }
      auto site =
          IdentitySyntaxSite::from(zc::mv(ZC_ASSERT_NONNULL(key)), zc::mv(ZC_ASSERT_NONNULL(span)));
      if (site == zc::none) { return false; }
      entries.add(
          IdentitySyntaxSiteInventoryEntry{ordinals[node.value], zc::mv(ZC_ASSERT_NONNULL(site))});
    }
    uint32_t childIndex = 0;
    bool valid = true;
    ast::visitChildNodeIds(tree, tree.node(node), [&](ast::NodeId child) {
      const uint32_t index = childIndex++;
      if (!valid) { return; }
      path.add(index);
      valid = collect(child, insideIdentity, path, ordinal);
      path.removeLast();
    });
    return valid;
  }

  const CanonicalParsedModule& parsedModule;
  const ast::Tree& tree;
  const identity::ModuleKey& module;
  ast::NodeId moduleNode;
  DefinitionInventory inventory;
  zc::Vector<zc::Vector<uint32_t>> paths;
  zc::Vector<uint32_t> ordinals;
  zc::Vector<uint8_t> seen;
  zc::Vector<uint8_t> identityRoot;
  zc::Vector<IdentitySyntaxSiteInventoryEntry> entries;
};

}  // namespace

zc::Maybe<IdentitySyntaxSiteInventory> IdentitySyntaxSiteInventoryProducer::produce(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode) {
  return IdentitySyntaxSiteInventoryProducerImpl(parsedModule, module, moduleNode).produce();
}

class CandidateProducerImpl final {
public:
  CandidateProducerImpl(const CanonicalParsedModule& parsedModule,
                        const identity::ModuleKey& module, ast::NodeId moduleNode)
      : parsedModule(parsedModule),
        tree(parsedModule.tree()),
        module(module),
        moduleNode(moduleNode),
        inventory(DefinitionInventory::collect(tree)) {
    paths.resize(tree.nodeCount() + 1);
    pathSeen.resize(tree.nodeCount() + 1);
    siteRetained.resize(tree.nodeCount() + 1);
    for (auto& state : pathSeen) { state = 0; }
    for (auto& state : siteRetained) { state = 0; }
    definitionKeys.resize(tree.nodeCount() + 1);
    implKeys.resize(tree.nodeCount() + 1);
  }

  StableIdentityCandidateProduction produce() {
    if (!tree.contains(tree.root()) ||
        (moduleNode && (!tree.contains(moduleNode) ||
                        tree.node(moduleNode).kind != ast::SyntaxKind::ModuleDeclaration)) ||
        !parsedModule.source().belongsTo(module.crate())) {
      return reject(StableIdentityCandidateFailureKind::InvalidModule, moduleNode);
    }
    zc::Vector<uint32_t> rootPath;
    if (!collectPaths(tree.root(), rootPath)) {
      return reject(StableIdentityCandidateFailureKind::DuplicateNode, duplicateNode);
    }
    collectReferences();
    sortReferences();
    for (const auto& reference : references) {
      auto failure =
          reference.implementation ? produceImpl(reference) : produceDefinition(reference);
      ZC_IF_SOME(value, failure) { return value; }
    }
    auto output = zc::heap<StableIdentityCandidateInventory::Impl>();
    output->sites = zc::mv(sites);
    output->candidates = zc::mv(candidates);
    output->definitions = zc::mv(producedDefinitions);
    output->implementations = zc::mv(producedImpls);
    output->definitionSites = zc::mv(producedDefinitionSites);
    output->implementationSites = zc::mv(producedImplSites);
    return StableIdentityCandidateInventory(zc::mv(output));
  }

private:
  StableIdentityCandidateFailure reject(
      StableIdentityCandidateFailureKind kind, ast::NodeId node,
      CanonicalHeaderSyntaxFailureKind headerKind =
          CanonicalHeaderSyntaxFailureKind::InvalidTypeSyntax) const noexcept {
    return StableIdentityCandidateFailure{kind, node, headerKind};
  }

  bool collectPaths(ast::NodeId node, zc::Vector<uint32_t>& path) {
    if (!tree.contains(node) || node.value >= pathSeen.size()) { return false; }
    if (pathSeen[node.value] != 0) {
      duplicateNode = node;
      return false;
    }
    pathSeen[node.value] = 1;
    paths[node.value] = clonePath(path.asPtr());
    const auto& syntax = tree.node(node);
    uint32_t childIndex = 0;
    bool valid = true;
    ast::visitChildNodeIds(tree, syntax, [&](ast::NodeId child) {
      const uint32_t index = childIndex++;
      if (!valid) { return; }
      path.add(index);
      valid = collectPaths(child, path);
      path.removeLast();
    });
    return valid;
  }

  bool selectedDefinition(const DefinitionInventoryEntry& definition) const noexcept {
    return definition.moduleNode == moduleNode || (!definition.moduleNode && !moduleNode) ||
           (!definition.moduleNode && definition.node == moduleNode &&
            definition.kind == identity::DefinitionKind::ModuleAlias);
  }

  bool selectedImplementation(const ImplInventoryEntry& implementation) const noexcept {
    return implementation.moduleNode == moduleNode || (!implementation.moduleNode && !moduleNode);
  }

  bool stableOwnerChain(zc::ArrayPtr<const StructuralIdentityParent> parents) const {
    for (const auto& parent : parents) {
      if (parent.kind == StructuralIdentityParentKind::Definition) {
        auto entry = definition(parent.node);
        if (entry == zc::none) { return false; }
        ZC_IF_SOME(value, entry) {
          if (!stableOwnerChain(value.parentPath.asPtr())) { return false; }
        }
      } else {
        auto entry = implementation(parent.node);
        if (entry == zc::none) { return false; }
        ZC_IF_SOME(value, entry) {
          if (!stableOwnerChain(value.parentPath.asPtr())) { return false; }
        }
      }
    }
    return true;
  }

  void collectReferences() {
    for (size_t index = 0; index < inventory.definitions().size(); ++index) {
      const auto& definition = inventory.definitions()[index];
      if (selectedDefinition(definition) && stableOwnerChain(definition.parentPath.asPtr())) {
        references.add(StableInventoryReference{false, index, definition.node});
      }
    }
    for (size_t index = 0; index < inventory.impls().size(); ++index) {
      const auto& implementation = inventory.impls()[index];
      if (selectedImplementation(implementation) &&
          stableOwnerChain(implementation.parentPath.asPtr())) {
        references.add(StableInventoryReference{true, index, implementation.node});
      }
    }
  }

  bool referenceLessThan(const StableInventoryReference& left,
                         const StableInventoryReference& right) const {
    return comparePath(paths[left.node.value].asPtr(), paths[right.node.value].asPtr()) < 0;
  }

  void sortReferences() {
    for (size_t index = 1; index < references.size(); ++index) {
      size_t insertion = index;
      while (insertion != 0 &&
             referenceLessThan(references[insertion], references[insertion - 1])) {
        auto displaced = zc::mv(references[insertion - 1]);
        references[insertion - 1] = zc::mv(references[insertion]);
        references[insertion] = zc::mv(displaced);
        --insertion;
      }
    }
  }

  zc::Maybe<const DefinitionInventoryEntry&> definition(ast::NodeId node) const {
    for (const auto& entry : inventory.definitions()) {
      if (entry.node == node && selectedDefinition(entry)) { return entry; }
    }
    return zc::none;
  }

  zc::Maybe<const ImplInventoryEntry&> implementation(ast::NodeId node) const {
    for (const auto& entry : inventory.impls()) {
      if (entry.node == node && selectedImplementation(entry)) { return entry; }
    }
    return zc::none;
  }

  zc::Maybe<identity::DefinitionKey> definitionKey(ast::NodeId node) const {
    if (node.value >= definitionKeys.size()) { return zc::none; }
    ZC_IF_SOME(value, definitionKeys[node.value]) { return value.clone(); }
    return zc::none;
  }

  zc::Maybe<identity::ImplKey> implKey(ast::NodeId node) const {
    if (node.value >= implKeys.size()) { return zc::none; }
    ZC_IF_SOME(value, implKeys[node.value]) { return value.clone(); }
    return zc::none;
  }

  zc::Maybe<StableIdentityCandidateFailure> buildOwnerContext(
      zc::ArrayPtr<const StructuralIdentityParent> parents,
      zc::Vector<identity::EnclosingStableOwnerKey>& owners,
      zc::Vector<CanonicalGenericBinderFrame>& binders, ast::NodeId current) const {
    for (const auto& parent : parents) {
      if (parent.kind == StructuralIdentityParentKind::Definition) {
        auto key = definitionKey(parent.node);
        auto entry = definition(parent.node);
        if (key == zc::none || entry == zc::none) {
          return reject(StableIdentityCandidateFailureKind::InvalidOwner, current);
        }
        ZC_IF_SOME(value, key) {
          owners.add(identity::EnclosingStableOwnerKey::definition(zc::mv(value)));
        }
      } else {
        auto key = implKey(parent.node);
        auto entry = implementation(parent.node);
        if (key == zc::none || entry == zc::none) {
          return reject(StableIdentityCandidateFailureKind::InvalidOwner, current);
        }
        ZC_IF_SOME(value, key) {
          owners.add(identity::EnclosingStableOwnerKey::implementation(zc::mv(value)));
        }
      }
    }
    for (size_t reverse = parents.size(); reverse != 0; --reverse) {
      const auto& parent = parents[reverse - 1];
      binders.add(
          CanonicalGenericBinderFrame{parent.kind == StructuralIdentityParentKind::Definition
                                          ? definitionGenericParameters(tree, parent.node)
                                          : implementationGenericParameters(tree, parent.node)});
    }
    return zc::none;
  }

  zc::Maybe<IdentitySyntaxSiteKey> siteKey(ast::NodeId node) const {
    if (!tree.contains(node) || node.value >= paths.size() || pathSeen[node.value] == 0) {
      return zc::none;
    }
    return IdentitySyntaxSiteKey::from(module.clone(), parsedModule.source().clone(),
                                       clonePath(paths[node.value].asPtr()));
  }

  zc::Maybe<StableIdentityCandidateFailure> retainSite(ast::NodeId node, source::SourceRange range,
                                                       IdentitySyntaxSiteKey& key) {
    if (!tree.contains(node) || node.value >= siteRetained.size()) {
      return reject(StableIdentityCandidateFailureKind::InvalidSite, node);
    }
    if (siteRetained[node.value] != 0) { return zc::none; }
    auto span = parsedModule.spanFor(range);
    if (span == zc::none) { return reject(StableIdentityCandidateFailureKind::InvalidSite, node); }
    ZC_IF_SOME(value, span) {
      auto site = IdentitySyntaxSite::from(key.clone(), zc::mv(value));
      if (site == zc::none) {
        return reject(StableIdentityCandidateFailureKind::InvalidSite, node);
      }
      ZC_IF_SOME(admitted, site) {
        sites.add(zc::mv(admitted));
        siteRetained[node.value] = 1;
      }
    }
    return zc::none;
  }

  zc::Maybe<StableIdentityCandidateFailure> buildDuplicateBounds(
      zc::ArrayPtr<const CanonicalBoundSyntaxOccurrence> occurrences,
      zc::Vector<DuplicateBoundOccurrence>& duplicates) {
    struct FirstOccurrence final {
      zc::Array<uint8_t> obligation;
      ast::NodeId node;
    };

    zc::Vector<size_t> order(occurrences.size());
    for (size_t index = 0; index < occurrences.size(); ++index) {
      if (!tree.contains(occurrences[index].node) ||
          occurrences[index].node.value >= paths.size() ||
          pathSeen[occurrences[index].node.value] == 0) {
        return reject(StableIdentityCandidateFailureKind::InvalidSite, occurrences[index].node);
      }
      order.add(index);
    }
    for (size_t index = 1; index < order.size(); ++index) {
      const size_t current = order[index];
      size_t insertion = index;
      while (insertion != 0 &&
             comparePath(paths[occurrences[current].node.value].asPtr(),
                         paths[occurrences[order[insertion - 1]].node.value].asPtr()) < 0) {
        order[insertion] = order[insertion - 1];
        --insertion;
      }
      order[insertion] = current;
    }

    zc::Vector<FirstOccurrence> firstOccurrences;
    for (const auto index : order) {
      const auto& occurrence = occurrences[index];
      auto occurrenceSite = siteKey(occurrence.node);
      if (occurrenceSite == zc::none) {
        return reject(StableIdentityCandidateFailureKind::InvalidSite, occurrence.node);
      }
      auto encoded = occurrence.obligation.encode();
      const FirstOccurrence* first = nullptr;
      for (const auto& candidate : firstOccurrences) {
        if (candidate.obligation.asPtr() == encoded.asPtr()) {
          first = &candidate;
          break;
        }
      }
      if (first == nullptr) {
        firstOccurrences.add(FirstOccurrence{zc::mv(encoded), occurrence.node});
        continue;
      }

      auto firstSite = siteKey(first->node);
      if (firstSite == zc::none) {
        return reject(StableIdentityCandidateFailureKind::InvalidSite, first->node);
      }
      ZC_IF_SOME(firstSiteValue, firstSite) {
        ZC_IF_SOME(failure, retainSite(first->node, tree.node(first->node).range, firstSiteValue)) {
          return failure;
        }
        ZC_IF_SOME(occurrenceSiteValue, occurrenceSite) {
          ZC_IF_SOME(failure, retainSite(occurrence.node, tree.node(occurrence.node).range,
                                         occurrenceSiteValue)) {
            return failure;
          }
          auto duplicate = DuplicateBoundOccurrence::from(
              occurrence.obligation.clone(), firstSiteValue.clone(), occurrenceSiteValue.clone());
          if (duplicate == zc::none) {
            return reject(StableIdentityCandidateFailureKind::InvalidRecord, occurrence.node);
          }
          ZC_IF_SOME(value, duplicate) { duplicates.add(zc::mv(value)); }
        }
      }
    }
    return zc::none;
  }

  zc::Maybe<StableIdentityCandidateFailure> produceDefinition(
      const StableInventoryReference& reference) {
    const auto& entry = inventory.definitions()[reference.slot];
    if (entry.nameKind != InventoryDefinitionNameKind::Declared || !entry.declaredName) {
      return reject(StableIdentityCandidateFailureKind::InvalidRecord, entry.node);
    }
    auto name = identity::DeclaredDefinitionName::fromSource(tree.ident(entry.declaredName));
    auto nameSpace = identity::definitionNamespaceFor(entry.kind);
    auto key = siteKey(entry.node);
    if (name == zc::none || nameSpace == zc::none || key == zc::none) {
      return reject(StableIdentityCandidateFailureKind::InvalidRecord, entry.node);
    }
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Vector<CanonicalGenericBinderFrame> binders;
    ZC_IF_SOME(failure, buildOwnerContext(entry.parentPath.asPtr(), owners, binders, entry.node)) {
      return failure;
    }
    zc::Maybe<identity::OverloadHeaderAuthority> overload;
    zc::Maybe<identity::OverloadHeaderDigest> overloadDigest;
    zc::Vector<CanonicalBoundSyntaxOccurrence> boundOccurrences;
    if (callableDefinition(entry.kind)) {
      auto produced =
          CanonicalDefinitionHeaderProducer::produceWithProvenance(tree, entry, binders.asPtr());
      if (!produced.is<CanonicalDefinitionHeaderProvenance>()) {
        const auto& headerFailure = produced.get<CanonicalHeaderSyntaxFailure>();
        return reject(StableIdentityCandidateFailureKind::InvalidHeader, headerFailure.node,
                      headerFailure.kind);
      }
      auto provenance = zc::mv(produced.get<CanonicalDefinitionHeaderProvenance>());
      overloadDigest = provenance.authority.digest().clone();
      overload = zc::mv(provenance.authority);
      boundOccurrences = zc::mv(provenance.boundOccurrences);
    }
    ZC_IF_SOME(nameValue, name) {
      ZC_IF_SOME(namespaceValue, nameSpace) {
        auto record = identity::DefinitionIdentityRecord::from(
            module.clone(), zc::mv(owners), entry.kind, namespaceValue, zc::mv(nameValue),
            zc::mv(overloadDigest));
        if (record == zc::none) {
          return reject(StableIdentityCandidateFailureKind::InvalidRecord, entry.node);
        }
        ZC_IF_SOME(siteValue, key) {
          ZC_IF_SOME(failure, retainSite(entry.node, entry.source, siteValue)) { return failure; }
          ZC_IF_SOME(recordValue, record) {
            auto recordKey = identity::DefinitionKey::compute(recordValue);
            zc::Vector<DuplicateBoundOccurrence> duplicates;
            ZC_IF_SOME(failure, buildDuplicateBounds(boundOccurrences.asPtr(), duplicates)) {
              return failure;
            }
            auto candidate = PreAdmissionIdentityCandidate::definition(
                recordValue.clone(), zc::mv(overload), siteValue.clone(), zc::mv(duplicates));
            if (candidate == zc::none || definitionKeys[entry.node.value] != zc::none) {
              return reject(StableIdentityCandidateFailureKind::DuplicateNode, entry.node);
            }
            auto source = parsedModule.spanFor(entry.source);
            if (source == zc::none) {
              return reject(StableIdentityCandidateFailureKind::InvalidSite, entry.node);
            }
            definitionKeys[entry.node.value] = recordKey.clone();
            producedDefinitions.add(ProducedDefinitionIdentity{entry.node, recordKey.clone()});
            producedDefinitionSites.add(
                ProducedDefinitionIdentitySite{entry.node, zc::mv(recordKey), siteValue.clone(),
                                               zc::mv(ZC_ASSERT_NONNULL(source))});
            ZC_IF_SOME(candidateValue, candidate) { candidates.add(zc::mv(candidateValue)); }
          }
        }
      }
    }
    return zc::none;
  }

  zc::Maybe<StableIdentityCandidateFailure> produceImpl(const StableInventoryReference& reference) {
    const auto& entry = inventory.impls()[reference.slot];
    auto key = siteKey(entry.node);
    if (key == zc::none) {
      return reject(StableIdentityCandidateFailureKind::InvalidSite, entry.node);
    }
    zc::Vector<identity::EnclosingStableOwnerKey> owners;
    zc::Vector<CanonicalGenericBinderFrame> binders;
    ZC_IF_SOME(failure, buildOwnerContext(entry.parentPath.asPtr(), owners, binders, entry.node)) {
      return failure;
    }
    auto produced =
        CanonicalImplHeaderProducer::produceWithProvenance(tree, entry, binders.asPtr());
    if (!produced.is<CanonicalImplHeaderProvenance>()) {
      const auto& headerFailure = produced.get<CanonicalHeaderSyntaxFailure>();
      return reject(StableIdentityCandidateFailureKind::InvalidHeader, headerFailure.node,
                    headerFailure.kind);
    }
    auto provenance = zc::mv(produced.get<CanonicalImplHeaderProvenance>());
    ZC_IF_SOME(siteValue, key) {
      ZC_IF_SOME(failure, retainSite(entry.node, entry.source, siteValue)) { return failure; }
      auto record = identity::ImplIdentityRecord::from(module.clone(), zc::mv(owners),
                                                       zc::mv(provenance.header));
      auto recordKey = identity::ImplKey::compute(record);
      zc::Vector<DuplicateBoundOccurrence> duplicates;
      ZC_IF_SOME(failure, buildDuplicateBounds(provenance.boundOccurrences.asPtr(), duplicates)) {
        return failure;
      }
      zc::Maybe<identity::OverloadHeaderAuthority> noOverload;
      auto candidate = PreAdmissionIdentityCandidate::implementation(
          record.clone(), zc::mv(noOverload), siteValue.clone(), zc::mv(duplicates));
      if (candidate == zc::none || implKeys[entry.node.value] != zc::none) {
        return reject(StableIdentityCandidateFailureKind::DuplicateNode, entry.node);
      }
      auto source = parsedModule.spanFor(entry.source);
      if (source == zc::none) {
        return reject(StableIdentityCandidateFailureKind::InvalidSite, entry.node);
      }
      implKeys[entry.node.value] = recordKey.clone();
      producedImpls.add(ProducedImplIdentity{entry.node, recordKey.clone()});
      producedImplSites.add(ProducedImplIdentitySite{
          entry.node, zc::mv(recordKey), siteValue.clone(), zc::mv(ZC_ASSERT_NONNULL(source))});
      ZC_IF_SOME(candidateValue, candidate) { candidates.add(zc::mv(candidateValue)); }
    }
    return zc::none;
  }

  const CanonicalParsedModule& parsedModule;
  const ast::Tree& tree;
  const identity::ModuleKey& module;
  ast::NodeId moduleNode;
  DefinitionInventory inventory;
  zc::Vector<zc::Vector<uint32_t>> paths;
  zc::Vector<uint8_t> pathSeen;
  zc::Vector<uint8_t> siteRetained;
  ast::NodeId duplicateNode;
  zc::Vector<StableInventoryReference> references;
  zc::Vector<zc::Maybe<identity::DefinitionKey>> definitionKeys;
  zc::Vector<zc::Maybe<identity::ImplKey>> implKeys;
  zc::Vector<IdentitySyntaxSite> sites;
  zc::Vector<PreAdmissionIdentityCandidate> candidates;
  zc::Vector<ProducedDefinitionIdentity> producedDefinitions;
  zc::Vector<ProducedImplIdentity> producedImpls;
  zc::Vector<ProducedDefinitionIdentitySite> producedDefinitionSites;
  zc::Vector<ProducedImplIdentitySite> producedImplSites;
};

StableIdentityCandidateInventory::StableIdentityCandidateInventory(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
StableIdentityCandidateInventory::~StableIdentityCandidateInventory() noexcept(false) = default;
StableIdentityCandidateInventory::StableIdentityCandidateInventory(
    StableIdentityCandidateInventory&&) noexcept = default;
StableIdentityCandidateInventory& StableIdentityCandidateInventory::operator=(
    StableIdentityCandidateInventory&&) noexcept = default;

zc::ArrayPtr<const IdentitySyntaxSite> StableIdentityCandidateInventory::sites() const noexcept {
  return impl->sites.asPtr();
}
zc::ArrayPtr<const PreAdmissionIdentityCandidate> StableIdentityCandidateInventory::candidates()
    const noexcept {
  return impl->candidates.asPtr();
}
zc::ArrayPtr<const ProducedDefinitionIdentity> StableIdentityCandidateInventory::definitions()
    const noexcept {
  return impl->definitions.asPtr();
}
zc::ArrayPtr<const ProducedImplIdentity> StableIdentityCandidateInventory::implementations()
    const noexcept {
  return impl->implementations.asPtr();
}
zc::ArrayPtr<const ProducedDefinitionIdentitySite>
StableIdentityCandidateInventory::definitionSites() const noexcept {
  return impl->definitionSites.asPtr();
}
zc::ArrayPtr<const ProducedImplIdentitySite> StableIdentityCandidateInventory::implementationSites()
    const noexcept {
  return impl->implementationSites.asPtr();
}

StableIdentityCandidateProduction CandidateProducer::produce(
    const CanonicalParsedModule& parsedModule, const identity::ModuleKey& module,
    ast::NodeId moduleNode) {
  return CandidateProducerImpl(parsedModule, module, moduleNode).produce();
}

}  // namespace zomlang::compiler::binder
