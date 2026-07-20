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

#include "zomlang/compiler/driver/compiler-session.h"

#include "zc/core/encoding.h"
#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/ast/tree.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/binder/binding-diagnostic-adapter.h"
#include "zomlang/compiler/binder/binding-input-diagnostic-adapter.h"
#include "zomlang/compiler/binder/definition-inventory.h"
#include "zomlang/compiler/binder/frozen-definition-inventory.h"
#include "zomlang/compiler/binder/local-identity.h"
#include "zomlang/compiler/binder/module-dependency-requests.h"
#include "zomlang/compiler/binder/module-graph-diagnostic-adapter.h"
#include "zomlang/compiler/binder/stable-identity-candidate-producer.h"
#include "zomlang/compiler/binder/stable-identity-candidate-verifier.h"
#include "zomlang/compiler/checker/body-checker.h"
#include "zomlang/compiler/checker/borrow-interface-diagnostic-adapter.h"
#include "zomlang/compiler/checker/checked-facts-repository.h"
#include "zomlang/compiler/checker/checker-diagnostic-adapter.h"
#include "zomlang/compiler/checker/coherence-facts.h"
#include "zomlang/compiler/checker/cross-module-facts.h"
#include "zomlang/compiler/checker/signature-facts.h"
#include "zomlang/compiler/diagnostics/consoling-diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/diagnostics/diagnostic-materializer.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/active-definition-authority-session.h"
#include "zomlang/compiler/driver/coherence-builder.h"
#include "zomlang/compiler/driver/imported-signature-view-projector.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/module-discovery.h"
#include "zomlang/compiler/driver/module-interface-diagnostic-adapter.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/named-item-query.h"
#include "zomlang/compiler/driver/package/package-diagnostic.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/identity-diagnostic-adapter.h"
#include "zomlang/compiler/parser/parse-source-query.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace driver {
namespace {

namespace source_query = identity::source_query;

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

bool containsDefinitionRecord(const binder::NamedDefinitionInventory& inventory,
                              const identity::DefinitionKey& key,
                              zc::ArrayPtr<const uint8_t> record) {
  for (const auto& entry : inventory.entries()) {
    if (entry.key() == key) { return entry.canonicalRecord() == record; }
  }
  return false;
}

bool containsImplementationKey(const binder::NamedImplementationInventory& inventory,
                               const identity::ImplKey& key) {
  for (const auto& candidate : inventory.keys()) {
    if (candidate == key) { return true; }
  }
  return false;
}

bool sameModulePath(zc::ArrayPtr<const identity::ModulePathSegment> left,
                    zc::ArrayPtr<const identity::ModulePathSegment> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

bool sameRelativePath(const identity::CanonicalRelativePath& left,
                      const identity::CanonicalRelativePath& right) {
  if (left.segments().size() != right.segments().size()) { return false; }
  for (size_t index = 0; index < left.segments().size(); ++index) {
    if (left.segments()[index] != right.segments()[index]) { return false; }
  }
  return true;
}

zc::Vector<identity::ModulePathSegment> cloneModulePath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

zc::Array<uint8_t> encodeStructuralModulePath(
    const identity::CrateKey& crate, zc::ArrayPtr<const identity::ModulePathSegment> path) {
  identity::CanonicalEncoder encoder;
  crate.encode(encoder);
  encoder.encodeSequenceSize(path.size());
  for (const auto& segment : path) { segment.encode(encoder); }
  return encoder.finish();
}

identity::CanonicalRelativePath parentDirectory(const identity::CanonicalRelativePath& path) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  if (path.segments().size() != 0) {
    segments.reserve(path.segments().size() - 1);
    for (size_t index = 0; index + 1 < path.segments().size(); ++index) {
      segments.add(path.segments()[index].clone());
    }
  }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

zc::Array<uint8_t> targetSelectionBytes(const package::RegisteredTargetSelection& selection) {
  identity::CanonicalEncoder encoder;
  selection.encode(encoder);
  return encoder.finish();
}

bool sameTargetSelection(const package::RegisteredTargetSelection& left,
                         const package::RegisteredTargetSelection& right) {
  return sameBytes(targetSelectionBytes(left), targetSelectionBytes(right));
}

bool packageMatchesBase(const identity::PackageKey& package, const identity::PackageBaseKey& base) {
  identity::CanonicalEncoder packageSource;
  identity::CanonicalEncoder baseSource;
  package.source().encode(packageSource);
  base.source().encode(baseSource);
  return packageSource.finish().asPtr() == baseSource.finish().asPtr() &&
         package.name() == base.name() && package.version() == base.version();
}

bool samePackage(const identity::PackageKey& left, const identity::PackageKey& right) {
  return sameBytes(left.encode().asPtr(), right.encode().asPtr());
}

zc::String packageSourceIdentifier(const identity::PackageKey& package,
                                   const identity::CanonicalRelativePath& path) {
  zc::String result = zc::str(package.name(), "/");
  for (size_t index = 0; index < path.segments().size(); ++index) {
    if (index != 0) { result = zc::str(result, "/"); }
    result = zc::str(result, path.segments()[index].text());
  }
  return result;
}

zc::String generatedSourceIdentifier(const identity::PackageKey& package,
                                     const identity::CanonicalRelativePath& path) {
  zc::String result = zc::str(package.name(), "/<generated>");
  for (const auto& segment : path.segments()) { result = zc::str(result, "/", segment.text()); }
  return result;
}

bool graphContainsPackage(const package::ResolutionOutput& graph,
                          const identity::PackageKey& package) {
  const auto expected = package.encode();
  for (const auto& selected : graph.packages()) {
    if (sameBytes(expected, selected.key().encode())) { return true; }
  }
  return false;
}

bool graphAndSnapshotsMatch(const package::ResolutionOutput& graph,
                            zc::ArrayPtr<const package::ResolvedPackageSourceSnapshot> snapshots) {
  if (graph.packages().size() == 0 || snapshots.size() == 0) { return false; }
  for (const auto& selected : graph.packages()) {
    bool found = false;
    for (const auto& snapshot : snapshots) {
      if (packageMatchesBase(selected.key(), snapshot.package())) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  for (const auto& snapshot : snapshots) {
    bool found = false;
    for (const auto& selected : graph.packages()) {
      if (packageMatchesBase(selected.key(), snapshot.package())) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  return true;
}

zc::Maybe<identity::SourceOriginKey> sourceOriginFor(const identity::PackageKey& package,
                                                     const identity::CanonicalRelativePath& path) {
  const auto& packageSource = package.source();
  switch (packageSource.kind()) {
    case identity::PackageSourceKind::LocalPath: {
      zc::Vector<identity::CanonicalPathSegment> segments;
      for (const auto& segment : packageSource.localPath().segments()) {
        segments.add(segment.clone());
      }
      for (const auto& segment : path.segments()) { segments.add(segment.clone()); }
      return identity::SourceOriginKey::localFile(identity::CanonicalWorkspaceRelativePath::from(
          packageSource.localPath().leadingParents(), zc::mv(segments)));
    }
    case identity::PackageSourceKind::Registry:
      return identity::SourceOriginKey::registryFile(package.clone(), path.clone());
    case identity::PackageSourceKind::Vcs:
      return identity::SourceOriginKey::vcsFile(package.clone(), path.clone());
  }
  ZC_UNREACHABLE
}

void emitIdentityFailures(identity::SemanticIdentityRegistrySet& registries,
                          diagnostics::DiagnosticEngine& diagnostics) {
  registries.sortIdentityInvariants();
  const auto groups = identity::groupIdentityInvariants(registries.identityInvariants());
  identity::emitIdentityDiagnosticGroups(diagnostics, groups.asPtr());
}

template <typename Handle>
bool containsIdentityHandle(const zc::Vector<Handle>& handles, Handle candidate) {
  for (const auto handle : handles) {
    if (handle == candidate) { return true; }
  }
  return false;
}

bool containsSyntaxNode(zc::ArrayPtr<const ast::NodeId> nodes, ast::NodeId candidate) {
  for (const auto node : nodes) {
    if (node == candidate) { return true; }
  }
  return false;
}

zc::Maybe<const identity::DefinitionKey&> producedDefinitionKeyAt(
    zc::ArrayPtr<const binder::ProducedDefinitionIdentity> definitions, ast::NodeId node) {
  for (const auto& definition : definitions) {
    if (definition.node == node) { return definition.key; }
  }
  return zc::none;
}

zc::Maybe<const identity::ImplKey&> producedImplKeyAt(
    zc::ArrayPtr<const binder::FrozenImplOccurrenceProjection> implementations, ast::NodeId node) {
  for (const auto& implementation : implementations) {
    if (implementation.node == node) { return implementation.key.implementation(); }
  }
  return zc::none;
}

bool hasAuthoritativeImmediateOwner(zc::ArrayPtr<const binder::StructuralIdentityParent> parents,
                                    zc::ArrayPtr<const ast::NodeId> definitionAuthorities,
                                    zc::ArrayPtr<const ast::NodeId> implAuthorities) {
  if (parents.size() == 0) { return false; }
  const auto& owner = parents.back();
  return owner.kind == binder::StructuralIdentityParentKind::Definition
             ? containsSyntaxNode(definitionAuthorities, owner.node)
             : containsSyntaxNode(implAuthorities, owner.node);
}

zc::Maybe<identity::StableGenericParameterOwnerKey> immediateGenericOwner(
    zc::ArrayPtr<const binder::StructuralIdentityParent> parents,
    zc::ArrayPtr<const binder::ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const binder::FrozenImplOccurrenceProjection> implementations) {
  if (parents.size() == 0) { return zc::none; }
  const auto& owner = parents.back();
  if (owner.kind == binder::StructuralIdentityParentKind::Definition) {
    ZC_IF_SOME(key, producedDefinitionKeyAt(definitions, owner.node)) {
      return identity::StableGenericParameterOwnerKey::definition(key.clone());
    }
    return zc::none;
  }
  ZC_IF_SOME(key, producedImplKeyAt(implementations, owner.node)) {
    return identity::StableGenericParameterOwnerKey::implementation(key.clone());
  }
  return zc::none;
}

zc::Maybe<const identity::DefinitionKey&> immediateDefinitionOwner(
    zc::ArrayPtr<const binder::StructuralIdentityParent> parents,
    zc::ArrayPtr<const binder::ProducedDefinitionIdentity> definitions) {
  if (parents.size() == 0 ||
      parents.back().kind != binder::StructuralIdentityParentKind::Definition) {
    return zc::none;
  }
  return producedDefinitionKeyAt(definitions, parents.back().node);
}

zc::Maybe<binder::StableBodyOwnerKey> projectedBodyOwner(
    zc::ArrayPtr<const binder::StructuralIdentityParent> parents,
    zc::ArrayPtr<const binder::ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const ast::NodeId> definitionAuthorities, const identity::ModuleKey& module) {
  for (size_t remaining = parents.size(); remaining > 0; --remaining) {
    const auto& parent = parents[remaining - 1];
    if (parent.kind != binder::StructuralIdentityParentKind::Definition) { continue; }
    ZC_IF_SOME(key, producedDefinitionKeyAt(definitions, parent.node)) {
      if (!containsSyntaxNode(definitionAuthorities, parent.node)) { return zc::none; }
      return binder::StableBodyOwnerKey::definition(key.clone());
    }
  }
  return binder::StableBodyOwnerKey::module(module.clone());
}

bool findLocalSyntaxPath(const ast::Tree& tree, ast::NodeId current, ast::NodeId target,
                         zc::Vector<uint32_t>& path) {
  if (current == target) { return true; }
  uint32_t childIndex = 0;
  bool found = false;
  ast::visitChildNodeIds(tree, tree.node(current), [&](ast::NodeId child) {
    const uint32_t currentIndex = childIndex++;
    if (found || !tree.contains(child)) { return; }
    path.add(currentIndex);
    if (findLocalSyntaxPath(tree, child, target, path)) {
      found = true;
      return;
    }
    path.removeLast();
  });
  return found;
}

zc::Maybe<binder::LocalSyntaxPath> localSyntaxPath(const ast::Tree& tree, ast::NodeId owner,
                                                   ast::NodeId target) {
  if (!tree.contains(owner) || !tree.contains(target) || owner == target) { return zc::none; }
  zc::Vector<uint32_t> path;
  if (!findLocalSyntaxPath(tree, owner, target, path)) { return zc::none; }
  return binder::LocalSyntaxPath::from(zc::mv(path));
}

zc::Maybe<binder::LocalSyntaxPath> moduleBodySyntaxPath(const ast::Tree& tree, ast::NodeId module,
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
    if (tree.contains(item) && findLocalSyntaxPath(tree, item, target, path)) {
      return binder::LocalSyntaxPath::from(zc::mv(path));
    }
  }
  return zc::none;
}

zc::Maybe<binder::LocalSyntaxPath> projectedBodyPath(
    const ast::Tree& tree, zc::ArrayPtr<const binder::StructuralIdentityParent> parents,
    zc::ArrayPtr<const binder::ProducedDefinitionIdentity> definitions,
    zc::ArrayPtr<const ast::NodeId> definitionAuthorities, ast::NodeId module, ast::NodeId target) {
  for (size_t remaining = parents.size(); remaining > 0; --remaining) {
    const auto& parent = parents[remaining - 1];
    if (parent.kind != binder::StructuralIdentityParentKind::Definition) { continue; }
    if (producedDefinitionKeyAt(definitions, parent.node) == zc::none) { continue; }
    return containsSyntaxNode(definitionAuthorities, parent.node)
               ? localSyntaxPath(tree, parent.node, target)
               : zc::Maybe<binder::LocalSyntaxPath>();
  }
  return moduleBodySyntaxPath(tree, module, target);
}

zc::Maybe<ast::NodeId> immediateAnonymousOwnerNode(
    const binder::DefinitionInventory& inventory,
    zc::ArrayPtr<const binder::StructuralIdentityParent> parents) {
  if (parents.size() == 0 ||
      parents.back().kind != binder::StructuralIdentityParentKind::Definition) {
    return zc::none;
  }
  for (const auto& anonymous : inventory.anonymousEntities()) {
    if (anonymous.node == parents.back().node) { return anonymous.node; }
  }
  return zc::none;
}

bool isReceiverParameter(const binder::DefinitionInventoryEntry& entry,
                         const binder::VerifiedParsedModule& parsedModule) {
  return parsedModule.functionParameterNameSpan(entry.node, ast::SyntaxKind::ThisKeyword) !=
         zc::none;
}

zc::Maybe<identity::DeclaredDefinitionName> declaredInventoryName(
    const binder::DefinitionInventoryEntry& entry,
    const binder::VerifiedParsedModule& parsedModule) {
  if (entry.nameKind != binder::InventoryDefinitionNameKind::Declared) { return zc::none; }
  return identity::DeclaredDefinitionName::fromSource(
      parsedModule.tree().ident(entry.declaredName));
}

zc::Maybe<binder::FrozenDefinitionInventoryInput> materializeFrozenInventoryInput(
    identity::SemanticContextBrand context, identity::ModuleId module,
    const binder::VerifiedParsedModule& parsedModule,
    const binder::StableIdentityCandidateInventory& candidates,
    identity::SemanticIdentityRegistrySet& registries, uint32_t& genericTraversalOrdinal,
    uint32_t& callableTraversalOrdinal) {
  if (!module.belongsTo(context) || registries.context() != context) { return zc::none; }
  auto allocator = binder::ModuleLocalIdentityAllocator::create(context, module);
  if (allocator == zc::none) { return zc::none; }

  binder::FrozenDefinitionInventoryInput input;
  zc::Vector<identity::DefId> definitionHandles;
  zc::Vector<ast::NodeId> definitionAuthorityNodes;
  for (const auto& definition : candidates.definitions()) {
    auto handle = registries.definitions().find(definition.key);
    if (handle == zc::none) { return zc::none; }
    input.definitionCandidates.add(
        binder::ProducedDefinitionIdentity{definition.node, definition.key.clone()});
    ZC_IF_SOME(value, handle) {
      if (!containsIdentityHandle(definitionHandles, value)) {
        definitionHandles.add(value);
        definitionAuthorityNodes.add(definition.node);
      }
    }
  }

  size_t definitionCandidateIndex = 0;
  size_t implIdentityCandidateIndex = 0;
  for (const auto& candidate : candidates.candidates()) {
    ast::NodeId identityNode;
    if (candidate.kind() == binder::PreAdmissionIdentityKind::Definition) {
      if (definitionCandidateIndex >= candidates.definitions().size()) { return zc::none; }
      const auto& produced = candidates.definitions()[definitionCandidateIndex++];
      auto record = candidate.definitionRecord();
      if (record == zc::none) { return zc::none; }
      ZC_IF_SOME(recordValue, record) {
        if (identity::DefinitionKey::compute(recordValue) != produced.key) { return zc::none; }
      }
      identityNode = produced.node;
    } else {
      if (implIdentityCandidateIndex >= candidates.implementations().size()) { return zc::none; }
      const auto& produced = candidates.implementations()[implIdentityCandidateIndex++];
      auto record = candidate.implRecord();
      if (record == zc::none) { return zc::none; }
      ZC_IF_SOME(recordValue, record) {
        if (identity::ImplKey::compute(recordValue) != produced.key) { return zc::none; }
      }
      identityNode = produced.node;
    }
    for (const auto& duplicate : candidate.duplicateBounds()) {
      input.duplicateBounds.add(
          binder::FrozenDuplicateBoundProjection{identityNode, duplicate.clone()});
    }
  }
  if (definitionCandidateIndex != candidates.definitions().size() ||
      implIdentityCandidateIndex != candidates.implementations().size()) {
    return zc::none;
  }

  zc::Vector<identity::ImplId> implHandles;
  zc::Vector<ast::NodeId> implAuthorityNodes;
  size_t implCandidateIndex = 0;
  ZC_IF_SOME(allocatorValue, allocator) {
    for (const auto& implementation : candidates.implementations()) {
      while (implCandidateIndex < candidates.candidates().size() &&
             candidates.candidates()[implCandidateIndex].kind() !=
                 binder::PreAdmissionIdentityKind::Implementation) {
        ++implCandidateIndex;
      }
      if (implCandidateIndex == candidates.candidates().size()) { return zc::none; }
      const auto& candidate = candidates.candidates()[implCandidateIndex++];
      auto record = candidate.implRecord();
      auto occurrence = allocatorValue.allocateImplOccurrence();
      auto authority = registries.impls().find(implementation.key);
      if (record == zc::none || occurrence == zc::none || authority == zc::none) {
        return zc::none;
      }
      ZC_IF_SOME(recordValue, record) {
        if (identity::ImplKey::compute(recordValue) != implementation.key) { return zc::none; }
      }
      ZC_IF_SOME(occurrenceValue, occurrence) {
        input.implOccurrences.add(binder::FrozenImplOccurrenceProjection{
            implementation.node, occurrenceValue,
            binder::ImplSourceOccurrenceKey::from(implementation.key.clone(),
                                                  candidate.site().clone())});
      }
      ZC_IF_SOME(authorityValue, authority) {
        if (!containsIdentityHandle(implHandles, authorityValue)) {
          implHandles.add(authorityValue);
          implAuthorityNodes.add(implementation.node);
        }
      }
    }

    const auto syntax = binder::DefinitionInventory::collect(parsedModule.tree());
    auto activeModuleKey = registries.modules().lookup(module);
    if (activeModuleKey == zc::none || syntax.modules().size() > 1) { return zc::none; }
    const ast::NodeId moduleBodyRoot =
        syntax.modules().size() == 1 ? syntax.modules()[0].node : ast::NodeId();
    for (size_t index = 0; index < syntax.genericParameters().size(); ++index) {
      const auto& parameter = syntax.genericParameters()[index];
      if (!hasAuthoritativeImmediateOwner(parameter.parentPath.asPtr(),
                                          definitionAuthorityNodes.asPtr(),
                                          implAuthorityNodes.asPtr())) {
        continue;
      }
      auto owner =
          immediateGenericOwner(parameter.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                                input.implOccurrences.asPtr());
      if (owner == zc::none) { return zc::none; }
      uint32_t ordinal = 0;
      for (size_t prior = 0; prior < index; ++prior) {
        const auto& preceding = syntax.genericParameters()[prior];
        if (!preceding.parentPath.empty() && !parameter.parentPath.empty() &&
            preceding.parentPath.back().node == parameter.parentPath.back().node &&
            hasAuthoritativeImmediateOwner(preceding.parentPath.asPtr(),
                                           definitionAuthorityNodes.asPtr(),
                                           implAuthorityNodes.asPtr())) {
          ++ordinal;
        }
      }
      ZC_IF_SOME(ownerValue, owner) {
        auto record = identity::GenericParameterIdentityRecord::type(zc::mv(ownerValue), ordinal);
        auto key = identity::GenericParameterKey::compute(record);
        if (registries.collectGenericParameter(record.clone(), genericTraversalOrdinal++) !=
            identity::FrozenRegistryFailure::None) {
          return zc::none;
        }
        input.genericParameters.add(
            binder::FrozenGenericParameterProjection{parameter.node, zc::mv(key)});
      }
    }

    for (size_t index = 0; index < syntax.callableParameters().size(); ++index) {
      const auto& parameter = syntax.callableParameters()[index];
      if (!hasAuthoritativeImmediateOwner(parameter.parentPath.asPtr(),
                                          definitionAuthorityNodes.asPtr(),
                                          implAuthorityNodes.asPtr())) {
        continue;
      }
      auto owner = immediateDefinitionOwner(parameter.parentPath.asPtr(),
                                            input.definitionCandidates.asPtr());
      if (owner == zc::none) { return zc::none; }
      const bool receiver = isReceiverParameter(parameter, parsedModule);
      uint32_t ordinal = 0;
      if (!receiver) {
        for (size_t prior = 0; prior < index; ++prior) {
          const auto& preceding = syntax.callableParameters()[prior];
          if (!preceding.parentPath.empty() && !parameter.parentPath.empty() &&
              preceding.parentPath.back().node == parameter.parentPath.back().node &&
              !isReceiverParameter(preceding, parsedModule) &&
              hasAuthoritativeImmediateOwner(preceding.parentPath.asPtr(),
                                             definitionAuthorityNodes.asPtr(),
                                             implAuthorityNodes.asPtr())) {
            ++ordinal;
          }
        }
      }
      ZC_IF_SOME(ownerValue, owner) {
        auto record = identity::CallableParameterIdentityRecord::from(
            ownerValue.clone(), receiver ? identity::CallableParameterPosition::receiver()
                                         : identity::CallableParameterPosition::ordinary(ordinal));
        auto key = identity::CallableParameterKey::compute(record);
        if (registries.collectCallableParameter(record.clone(), callableTraversalOrdinal++) !=
            identity::FrozenRegistryFailure::None) {
          return zc::none;
        }
        input.callableParameters.add(
            binder::FrozenCallableParameterProjection{parameter.node, zc::mv(key)});
      }
    }

    const auto addOwnerLocalProjection = [&](const binder::DefinitionInventoryEntry& binding,
                                             binder::OwnerLocalBindingNamespace nameSpace,
                                             binder::OwnerLocalBindingKind kind) -> bool {
      auto owner =
          projectedBodyOwner(binding.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                             definitionAuthorityNodes.asPtr(), ZC_ASSERT_NONNULL(activeModuleKey));
      auto path = projectedBodyPath(parsedModule.tree(), binding.parentPath.asPtr(),
                                    input.definitionCandidates.asPtr(),
                                    definitionAuthorityNodes.asPtr(), moduleBodyRoot, binding.node);
      auto name = declaredInventoryName(binding, parsedModule);
      auto bindingId = allocatorValue.allocateOwnerLocalBinding();
      if (owner == zc::none || path == zc::none || name == zc::none || bindingId == zc::none) {
        return false;
      }
      ZC_IF_SOME(ownerValue, owner) {
        ZC_IF_SOME(pathValue, path) {
          ZC_IF_SOME(nameValue, name) {
            auto key = binder::OwnerLocalBindingKey::from(zc::mv(ownerValue), zc::mv(pathValue),
                                                          nameSpace, kind, zc::mv(nameValue));
            if (key == zc::none) { return false; }
            ZC_IF_SOME(bindingIdValue, bindingId) {
              ZC_IF_SOME(keyValue, key) {
                input.ownerLocalBindings.add(binder::FrozenOwnerLocalBindingProjection{
                    binding.node, bindingIdValue, zc::mv(keyValue)});
              }
            }
          }
        }
      }
      return true;
    };

    for (const auto& binding : syntax.ownerLocalBindings()) {
      if (!addOwnerLocalProjection(binding, binder::OwnerLocalBindingNamespace::Value,
                                   binding.kind == identity::DefinitionKind::Local
                                       ? binder::OwnerLocalBindingKind::Local
                                       : binder::OwnerLocalBindingKind::PatternBinding)) {
        return zc::none;
      }
    }
    for (const auto& parameter : syntax.genericParameters()) {
      if (immediateAnonymousOwnerNode(syntax, parameter.parentPath.asPtr()) == zc::none) {
        continue;
      }
      if (!addOwnerLocalProjection(parameter, binder::OwnerLocalBindingNamespace::Type,
                                   binder::OwnerLocalBindingKind::GenericParameter)) {
        return zc::none;
      }
    }
    for (const auto& parameter : syntax.callableParameters()) {
      if (immediateAnonymousOwnerNode(syntax, parameter.parentPath.asPtr()) == zc::none) {
        continue;
      }
      if (isReceiverParameter(parameter, parsedModule) ||
          !addOwnerLocalProjection(parameter, binder::OwnerLocalBindingNamespace::Value,
                                   binder::OwnerLocalBindingKind::CallableParameter)) {
        return zc::none;
      }
    }

    for (const auto& anonymous : syntax.anonymousEntities()) {
      auto owner =
          projectedBodyOwner(anonymous.parentPath.asPtr(), input.definitionCandidates.asPtr(),
                             definitionAuthorityNodes.asPtr(), ZC_ASSERT_NONNULL(activeModuleKey));
      auto path = projectedBodyPath(
          parsedModule.tree(), anonymous.parentPath.asPtr(), input.definitionCandidates.asPtr(),
          definitionAuthorityNodes.asPtr(), moduleBodyRoot, anonymous.node);
      if (owner == zc::none || path == zc::none || anonymous.anonymousRole == zc::none) {
        return zc::none;
      }
      ZC_IF_SOME(ownerValue, owner) {
        ZC_IF_SOME(pathValue, path) {
          ZC_IF_SOME(role, anonymous.anonymousRole) {
            auto key = binder::AnonymousOwnerLocalKey::from(
                zc::mv(ownerValue), zc::mv(pathValue),
                role == binder::AnonymousSyntaxRole::FunctionExpression
                    ? binder::AnonymousOwnerLocalRole::FunctionExpression
                    : binder::AnonymousOwnerLocalRole::Closure);
            if (key == zc::none) { return zc::none; }
            ZC_IF_SOME(keyValue, key) {
              input.anonymousEntities.add(
                  binder::FrozenAnonymousEntityProjection{anonymous.node, zc::mv(keyValue)});
            }
          }
        }
      }
    }
  }
  return input;
}

}  // namespace
// ================================================================================
// VerifiedPackageSessionInput

struct VerifiedPackageSessionInput::Impl final {
  Impl(package::VerifiedPackageCompilationRequest&& request,
       ir::VerifiedTargetSelection&& hostTarget, ir::VerifiedTargetSelection&& target,
       package::ResolutionOutput&& graph, package::VerifiedBuildScriptPlan&& buildScriptPlan,
       zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots) noexcept
      : request(zc::mv(request)),
        hostTarget(zc::mv(hostTarget)),
        target(zc::mv(target)),
        graph(zc::mv(graph)),
        buildScriptPlan(zc::mv(buildScriptPlan)),
        snapshots(zc::mv(snapshots)) {}

  package::VerifiedPackageCompilationRequest request;
  ir::VerifiedTargetSelection hostTarget;
  ir::VerifiedTargetSelection target;
  package::ResolutionOutput graph;
  package::VerifiedBuildScriptPlan buildScriptPlan;
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
};

VerifiedPackageSessionInput::VerifiedPackageSessionInput(
    package::VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, package::ResolutionOutput&& graph,
    package::VerifiedBuildScriptPlan&& buildScriptPlan,
    zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots)
    : impl(zc::heap<Impl>(zc::mv(request), zc::mv(hostTarget), zc::mv(target), zc::mv(graph),
                          zc::mv(buildScriptPlan), zc::mv(snapshots))) {}

VerifiedPackageSessionInput::~VerifiedPackageSessionInput() noexcept(false) = default;
VerifiedPackageSessionInput::VerifiedPackageSessionInput(VerifiedPackageSessionInput&&) noexcept =
    default;
VerifiedPackageSessionInput& VerifiedPackageSessionInput::operator=(
    VerifiedPackageSessionInput&&) noexcept = default;

zc::Maybe<VerifiedPackageSessionInput> VerifiedPackageSessionInput::from(
    package::VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, package::ResolutionOutput&& graph,
    zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots) {
  const auto& requestHost = request.hostTarget();
  const auto& requestTarget = request.target();
  const auto& verifiedHost = hostTarget.packageSelection();
  const auto& verifiedTarget = target.packageSelection();
  if (requestHost.registryRevision() != verifiedHost.registryRevision() ||
      requestTarget.registryRevision() != verifiedTarget.registryRevision() ||
      verifiedHost.registryRevision() != verifiedTarget.registryRevision() ||
      !sameTargetSelection(requestHost, verifiedHost) ||
      !sameTargetSelection(requestTarget, verifiedTarget) ||
      !graphAndSnapshotsMatch(graph, snapshots)) {
    return zc::none;
  }
  for (const auto& root : request.roots()) {
    if (!graphContainsPackage(graph, root.packageKey())) { return zc::none; }
  }
  auto plan = VerifiedPreparatoryCrateGraph::buildPlan(request, graph);
  if (!plan.is<package::VerifiedBuildScriptPlan>()) { return zc::none; }
  auto buildScriptPlan = zc::mv(plan.get<package::VerifiedBuildScriptPlan>());
  return VerifiedPackageSessionInput(zc::mv(request), zc::mv(hostTarget), zc::mv(target),
                                     zc::mv(graph), zc::mv(buildScriptPlan), zc::mv(snapshots));
}

// ================================================================================
// CompilerSession::Impl

struct CompilerSession::Impl {
  Impl(identity::SemanticContextFactory& contextFactory, const basic::LangOptions& opts,
       const basic::CompilerOptions& compOpts)
      : langOpts(opts),
        compilerOpts(compOpts),
        queryScheduler(4),
        queryDatabase(queryScheduler),
        stringPool(zc::heap<basic::StringPool>()),
        sourceManager(zc::heap<source::SourceManager>(*stringPool)),
        diagnosticEngine(zc::heap<diagnostics::DiagnosticEngine>(*sourceManager)) {
    diagnosticEngine->addConsumer(zc::heap<diagnostics::ConsolingDiagnosticConsumer>());
    if (!incremental_binding_query::registerIncrementalBindingQueryAdapter(queryDatabase)) {
      diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                                            zc::str(uint64_t{1}));
      return;
    }
    auto issuedContext = contextFactory.issue();
    if (issuedContext == zc::none) {
      diagnosticEngine->diagnose<diagnostics::DiagID::IdentityBrandExhausted>(source::SourceLoc(),
                                                                              zc::str(uint64_t{1}));
      return;
    }
    ZC_IF_SOME(context, issuedContext) {
      contextBrand = context;
      auto issuedRegistries =
          identity::SemanticIdentityRegistrySet::create(contextFactory, contextBrand);
      if (issuedRegistries == zc::none) {
        diagnosticEngine->diagnose<diagnostics::DiagID::IdentityDuplicateSingletonStore>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return;
      }
      ZC_IF_SOME(registries, issuedRegistries) { identityRegistries = zc::mv(registries); }
      auto issuedFactStoreBrands = contextFactory.issueRegistryBrandIssuer(contextBrand);
      if (issuedFactStoreBrands == zc::none) {
        diagnosticEngine->diagnose<diagnostics::DiagID::IdentityDuplicateSingletonStore>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return;
      }
      ZC_IF_SOME(issuer, issuedFactStoreBrands) { factStoreBrands = zc::mv(issuer); }
      auto issuedTypeStoreToken =
          contextFactory.issueSemanticTypeStoreConstructionToken(contextBrand);
      if (issuedTypeStoreToken == zc::none) {
        diagnosticEngine->diagnose<diagnostics::DiagID::IdentityDuplicateSingletonStore>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return;
      }
      ZC_IF_SOME(token, issuedTypeStoreToken) {
        ZC_IF_SOME(registries, identityRegistries) {
          semanticTypeStore = zc::heap<type::SemanticTypeStore>(zc::mv(token), registries);
        }
      }
    }
  }
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  struct OutputDirective {
    zc::ArrayPtr<zc::byte> name;
    zc::Maybe<zc::Path> dir;

    ZC_DISALLOW_COPY(OutputDirective);
    OutputDirective(OutputDirective&&) noexcept = default;
    OutputDirective(const zc::ArrayPtr<zc::byte> name, zc::Maybe<zc::Path> dir)
        : name(name), dir(zc::mv(dir)) {}
  };

  /// Language options
  const basic::LangOptions& langOpts;
  /// Compiler options
  const basic::CompilerOptions& compilerOpts;
  /// Sole session-owned scheduler for query dependency groups and future frontend work.
  basic::ThreadPool queryScheduler;
  /// Sole session-owned RFC 0017 input, memo, dependency, and flight authority.
  query::QueryDatabase queryDatabase;
  /// Session-owned readiness barrier and exact active-definition input ledger.
  incremental_binding_query::ActiveDefinitionAuthorityProjectionState activeDefinitionAuthority;
  /// Stable module dependency-input keys retained to replace the complete topology root.
  zc::Vector<incremental_binding_query::StableModuleQueryKey> stagedTopologyModules;
  /// Stable selected-source keys retained independently of later topology inputs.
  zc::Vector<incremental_binding_query::StableModuleQueryKey> stagedSelectedModules;
  /// Stable source-input keys retained to replace the complete source snapshot root.
  zc::Vector<source_query::StableSourceQueryKey> stagedSourceSnapshots;
  /// Stable crate keys retained to replace per-crate active source and module roots.
  zc::Vector<incremental_binding_query::StableCrateQueryKey> stagedActiveCrates;
  /// Package-root-set key retained to replace the complete active-crate input root.
  zc::Maybe<incremental_binding_query::PackageRootSetQueryKey> stagedPackageRoots;
  /// Process-unique identity for this semantic compilation.
  identity::SemanticContextBrand contextBrand;
  /// Sole RFC 0011 identity registry family for this session.
  zc::Maybe<identity::SemanticIdentityRegistrySet> identityRegistries;
  /// Sole RFC 0005 canonical semantic type store for this session.
  zc::Own<type::SemanticTypeStore> semanticTypeStore;
  /// Sole context-local issuer for substitution and witness store brands.
  zc::Maybe<identity::RegistryBrandIssuer> factStoreBrands;
  /// Session-owned append-only checked evidence publication root.
  zc::Own<checker::checked::CheckedFactsRepository> checkedFactsRepository;
  /// Session-owned append-only RFC 0013 borrow-evidence publication root.
  zc::Own<borrow_evidence::BorrowEvidenceRepository> borrowEvidenceRepository;
  /// Explicit storage owner for resolver inputs and the retained resolution output.
  zc::MemoryResource packageResolutionMemory;
  /// Workspace-verified package roots and their semantic identities.
  zc::Maybe<package::VerifiedPackageCompilationRequest> packageRequest;
  zc::Maybe<ir::VerifiedTargetSelection> verifiedHostTarget;
  zc::Maybe<ir::VerifiedTargetSelection> verifiedTarget;
  zc::Maybe<package::ResolutionOutput> packageGraph;
  zc::Maybe<VerifiedCrateGraph> crateGraph;
  zc::Vector<VerifiedPreparatoryCrateGraph> preparatoryCrateGraphs;
  zc::Maybe<identity::SemanticContextFingerprint> semanticContextFingerprint;
  zc::Maybe<binder::VerifiedModuleGraph> moduleGraph;
  zc::Vector<package::ResolvedPackageSourceSnapshot> packageSnapshots;
  zc::Maybe<package::VerifiedBuildScriptPlan> buildScriptPlan;
  zc::Maybe<package::VerifiedBuildScriptResultSet> buildScriptResults;
  /// Canonical source identities retained from package admission until source freeze.
  zc::HashMap<source::BufferId, identity::SourceFileKey> pendingSourceIdentities;
  /// Structurally selected non-empty module paths retained until module identity freeze.
  zc::HashMap<source::BufferId, zc::Vector<identity::ModulePathSegment>> pendingModulePaths;
  /// Frozen source handles indexed by the phase-local SourceManager buffer handle.
  zc::HashMap<source::BufferId, identity::SourceFileId> sourceIdentities;
  struct ModuleIdentityBinding final {
    source::BufferId buffer;
    ast::NodeId node;
    identity::ModuleId identity;
  };
  /// Frozen module handles indexed by one tree-local node, with node zero denoting an implicit
  /// root.
  zc::Vector<ModuleIdentityBinding> moduleIdentities;
  struct FrozenInventoryInputBinding final {
    FrozenInventoryInputBinding(source::BufferId buffer, identity::ModuleId module,
                                binder::FrozenDefinitionInventoryInput&& input) noexcept
        : buffer(zc::mv(buffer)), module(module), input(zc::mv(input)) {}
    FrozenInventoryInputBinding(FrozenInventoryInputBinding&&) noexcept = default;
    FrozenInventoryInputBinding& operator=(FrozenInventoryInputBinding&&) noexcept = default;
    ZC_DISALLOW_COPY(FrozenInventoryInputBinding);
    source::BufferId buffer;
    identity::ModuleId module;
    binder::FrozenDefinitionInventoryInput input;
  };
  zc::Vector<FrozenInventoryInputBinding> frozenInventoryInputs;
  struct FrozenInventoryBinding final {
    FrozenInventoryBinding(source::BufferId buffer, identity::ModuleId module,
                           binder::FrozenDefinitionInventoryView&& view) noexcept
        : buffer(zc::mv(buffer)), module(module), view(zc::mv(view)) {}
    FrozenInventoryBinding(FrozenInventoryBinding&&) noexcept = default;
    FrozenInventoryBinding& operator=(FrozenInventoryBinding&&) noexcept = default;
    ZC_DISALLOW_COPY(FrozenInventoryBinding);
    source::BufferId buffer;
    identity::ModuleId module;
    binder::FrozenDefinitionInventoryView view;
  };
  zc::Vector<FrozenInventoryBinding> frozenInventories;
  struct ModuleBodyQueryBinding final {
    ModuleBodyQueryBinding(source::BufferId buffer, identity::ModuleId module,
                           binder::ModuleBodySyntax&& syntax,
                           binder::ModuleBodyProvenance&& provenance) noexcept
        : buffer(zc::mv(buffer)),
          module(module),
          syntax(zc::mv(syntax)),
          provenance(zc::mv(provenance)) {}
    ModuleBodyQueryBinding(ModuleBodyQueryBinding&&) noexcept = default;
    ModuleBodyQueryBinding& operator=(ModuleBodyQueryBinding&&) noexcept = default;
    ZC_DISALLOW_COPY(ModuleBodyQueryBinding);
    source::BufferId buffer;
    identity::ModuleId module;
    binder::ModuleBodySyntax syntax;
    binder::ModuleBodyProvenance provenance;
  };
  /// Verified RFC 0019 module-body query values retained for owner-body demand.
  zc::Vector<ModuleBodyQueryBinding> moduleBodyQueryBindings;
  struct NamedItemQueryBinding final {
    NamedItemQueryBinding(identity::DefinitionKey&& definition, binder::NamedItemSyntax&& syntax,
                          binder::NamedItemProvenance&& provenance) noexcept
        : definition(zc::mv(definition)), syntax(zc::mv(syntax)), provenance(zc::mv(provenance)) {}
    NamedItemQueryBinding(NamedItemQueryBinding&&) noexcept = default;
    NamedItemQueryBinding& operator=(NamedItemQueryBinding&&) noexcept = default;
    ZC_DISALLOW_COPY(NamedItemQueryBinding);
    identity::DefinitionKey definition;
    binder::NamedItemSyntax syntax;
    binder::NamedItemProvenance provenance;
  };
  /// Ready-snapshot named-item values retained as the definition-owner syntax authority.
  zc::Vector<NamedItemQueryBinding> namedItemQueryBindings;
  /// String pool to manage interned strings.
  zc::Own<basic::StringPool> stringPool;
  /// Source manager to manage source files.
  zc::Own<source::SourceManager> sourceManager;
  /// Diagnostic engine to report diagnostics.
  zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;
  /// Canonically ordered parser results admitted against frozen source snapshots.
  zc::Vector<ParsedModuleRecord> parsedModules;
  /// True only after every discovered source publishes a promoted immutable parser result.
  bool verifiedParsedSyntax = false;
  /// Dependency-first verified binder publications for the complete module graph.
  zc::Vector<binder::VerifiedBindingInput> bindingInputs;
  zc::Vector<binder::VerifiedBindingOutput> bindingOutputs;
  zc::Vector<binder::VerifiedBoundModuleInput> boundModules;
  zc::Maybe<checker::signature::VerifiedMarkerShapeInventory> markerShapes;
  zc::Maybe<checker::signature::VerifiedMarkerPolicyRegistry> markerPolicies;
  zc::Vector<checker::signature::VerifiedSignatureFacts> signatureFacts;
  zc::Vector<checker::cross_module::ImportedSignatureView> importedSignatureViews;
  zc::Vector<VerifiedModuleInterface> moduleInterfaces;
  zc::Maybe<checker::coherence::FrozenCoherenceView> coherenceView;
  zc::Vector<checker::checked::CheckedEvidenceLease> checkedEvidence;
  zc::Vector<checker::dispatch::VerifiedDispatchFacts> dispatchFacts;
  zc::Vector<hir::VerifiedHirModule> hirModules;
  zc::Vector<mir::VerifiedBuiltMir> builtMirModules;
  zc::Vector<ir::IrDiagnosticGroup> irFailureGroups;
  zc::Vector<identity::IdentityInvariant> irIdentityInvariantFailures;
  bool verifiedCheckedSources = false;
  /// Closed Checker invariant rejection retained when no complete publication exists.
  zc::Vector<checker::signature::CheckerVerificationFailure> checkerFailures;

  bool demandNamedItemQueries() {
    if (!namedItemQueryBindings.empty()) { return false; }
    zc::Vector<NamedItemQueryBinding> staged(activeDefinitionAuthority.keyLedger().size());
    auto readySnapshot = queryDatabase.snapshot();
    auto readiness =
        readySnapshot.get<incremental_binding_query::ActiveDefinitionAuthorityReadyInput>(
            source_query::CompilationUnitQueryKey::fixed());
    if (readiness.isRuntimeFailure() || readiness.kind() != query::QueryValueKind::Value) {
      return false;
    }
    for (const auto& definition : activeDefinitionAuthority.keyLedger()) {
      auto syntax = readySnapshot.get<incremental_binding_query::NamedItemSyntaxQuery>(definition);
      auto provenance =
          readySnapshot.get<incremental_binding_query::NamedItemProvenanceQuery>(definition);
      if (syntax.isRuntimeFailure() || provenance.isRuntimeFailure() ||
          syntax.kind() != query::QueryValueKind::Value ||
          provenance.kind() != query::QueryValueKind::Value) {
        return false;
      }
      staged.add(NamedItemQueryBinding(definition.clone(), syntax.value().clone(),
                                       provenance.value().clone()));
    }
    namedItemQueryBindings = zc::mv(staged);
    return true;
  }

  bool stageParseSourceInputs() {
    if (packageRequest == zc::none || pendingSourceIdentities.size() == 0) { return false; }
    zc::Maybe<source_query::CanonicalCompilationOptions> compilationOptions;
    ZC_IF_SOME(request, packageRequest) {
      compilationOptions = source_query::CanonicalCompilationOptions::fromVerified(request);
    }
    if (compilationOptions == zc::none) { return false; }

    struct StagedSource final {
      StagedSource(source_query::StableSourceQueryKey&& key,
                   source_query::CanonicalSourceSnapshot&& snapshot) noexcept
          : key(zc::mv(key)), snapshot(zc::mv(snapshot)) {}
      StagedSource(StagedSource&&) noexcept = default;
      StagedSource& operator=(StagedSource&&) noexcept = default;
      ZC_DISALLOW_COPY(StagedSource);
      source_query::StableSourceQueryKey key;
      source_query::CanonicalSourceSnapshot snapshot;
    };

    zc::TreeMap<zc::String, StagedSource> canonicalSources;
    for (const auto& entry : pendingSourceIdentities) {
      auto key = source_query::StableSourceQueryKey::fromVerified(entry.value);
      auto snapshot = identity::ImmutableSourceSnapshot::from(
          entry.value.clone(), zc::heapArray(sourceManager->getEntireTextForBuffer(entry.key)));
      if (key == zc::none || snapshot == zc::none) { return false; }
      auto canonicalSnapshot =
          source_query::CanonicalSourceSnapshot::fromVerified(ZC_ASSERT_NONNULL(snapshot));
      if (canonicalSnapshot == zc::none) { return false; }
      auto sortKey = zc::encodeHex(ZC_ASSERT_NONNULL(key).canonicalSourceBytes());
      if (canonicalSources.find(sortKey) != zc::none) { return false; }
      canonicalSources.insert(zc::mv(sortKey),
                              StagedSource(zc::mv(ZC_ASSERT_NONNULL(key)),
                                           zc::mv(ZC_ASSERT_NONNULL(canonicalSnapshot))));
    }

    auto pending = activeDefinitionAuthority.beginBaseMutation(queryDatabase);
    if (pending == zc::none) { return false; }
    ZC_IF_SOME(transaction, pending) {
      for (const auto& prior : stagedSourceSnapshots) {
        if (!transaction.erase<source_query::SourceSnapshotInput>(prior)) { return false; }
      }
      if (!transaction.set<source_query::CompilationOptionsInput>(
              source_query::CompilationUnitQueryKey::fixed(),
              ZC_ASSERT_NONNULL(compilationOptions))) {
        return false;
      }
      for (const auto& entry : canonicalSources) {
        if (!transaction.set<source_query::SourceSnapshotInput>(entry.value.key,
                                                                entry.value.snapshot)) {
          return false;
        }
      }
      if (transaction.commit() == zc::none) { return false; }
    }
    stagedSourceSnapshots.clear();
    stagedSourceSnapshots.reserve(canonicalSources.size());
    for (const auto& entry : canonicalSources) {
      stagedSourceSnapshots.add(entry.value.key.clone());
    }
    return true;
  }

  bool stageSelectedModuleSourceInputs(identity::SemanticIdentityRegistrySet& registries) {
    namespace incremental = incremental_binding_query;

    struct StagedSelection final {
      StagedSelection(incremental::StableModuleQueryKey&& module,
                      incremental::SelectedModuleSource&& source) noexcept
          : module(zc::mv(module)), source(zc::mv(source)) {}
      StagedSelection(StagedSelection&&) noexcept = default;
      StagedSelection& operator=(StagedSelection&&) noexcept = default;
      ZC_DISALLOW_COPY(StagedSelection);
      incremental::StableModuleQueryKey module;
      incremental::SelectedModuleSource source;
    };

    if (moduleIdentities.size() == 0 || moduleIdentities.size() != parsedModules.size()) {
      return false;
    }
    zc::TreeMap<zc::String, StagedSelection> selections;
    zc::Vector<incremental::SelectedModuleSource> selectedSources(moduleIdentities.size());
    for (const auto& binding : moduleIdentities) {
      auto moduleKey = registries.modules().lookup(binding.identity);
      zc::Maybe<const ParsedModuleRecord&> parsed;
      for (const auto& candidate : parsedModules) {
        if (candidate.buffer() != binding.buffer) { continue; }
        if (parsed != zc::none) { return false; }
        parsed = candidate;
      }
      if (moduleKey == zc::none || parsed == zc::none) { return false; }
      auto queryKey = incremental::StableModuleQueryKey::fromVerified(ZC_ASSERT_NONNULL(moduleKey));
      auto selected = incremental::SelectedModuleSource::fromVerified(
          ZC_ASSERT_NONNULL(moduleKey), ZC_ASSERT_NONNULL(parsed).parsedModule().source());
      if (queryKey == zc::none || selected == zc::none) { return false; }
      auto sortKey = zc::encodeHex(ZC_ASSERT_NONNULL(queryKey).canonicalModuleBytes());
      if (selections.find(sortKey) != zc::none) { return false; }
      selectedSources.add(ZC_ASSERT_NONNULL(selected).clone());
      selections.insert(zc::mv(sortKey), StagedSelection(zc::mv(ZC_ASSERT_NONNULL(queryKey)),
                                                         zc::mv(ZC_ASSERT_NONNULL(selected))));
    }
    if (!incremental::verifySelectedSourceSnapshotClosure(selectedSources.asPtr(),
                                                          stagedSourceSnapshots.asPtr())) {
      return false;
    }

    auto pending = activeDefinitionAuthority.beginBaseMutation(queryDatabase);
    if (pending == zc::none) { return false; }
    ZC_IF_SOME(transaction, pending) {
      for (const auto& prior : stagedSelectedModules) {
        if (!transaction.erase<incremental::SelectedModuleSourceInput>(prior)) { return false; }
      }
      for (const auto& selection : selections) {
        if (!transaction.set<incremental::SelectedModuleSourceInput>(selection.value.module,
                                                                     selection.value.source)) {
          return false;
        }
      }
      if (transaction.commit() == zc::none) { return false; }
    }
    stagedSelectedModules.clear();
    stagedSelectedModules.reserve(selections.size());
    for (const auto& selection : selections) {
      stagedSelectedModules.add(selection.value.module.clone());
    }
    return true;
  }

  bool stageModuleTopologyInputs(const binder::VerifiedModuleGraph& graph,
                                 identity::SemanticIdentityRegistrySet& registries) {
    namespace incremental = incremental_binding_query;

    if (packageRequest == zc::none || packageGraph == zc::none || crateGraph == zc::none) {
      return false;
    }
    zc::Maybe<incremental::PackageRootSetQueryKey> packageRoots;
    zc::Maybe<incremental::CanonicalPackageGraph> packageGraphInput;
    zc::Maybe<incremental::CanonicalCrateSet> activeCrates;
    zc::Maybe<source_query::CanonicalCompilationOptions> compilationOptions;
    ZC_IF_SOME(request, packageRequest) {
      packageRoots = incremental::PackageRootSetQueryKey::fromVerified(request);
      compilationOptions = source_query::CanonicalCompilationOptions::fromVerified(request);
    }
    ZC_IF_SOME(graphValue, crateGraph) {
      zc::Vector<incremental::StableCrateQueryKey> crates(graphValue.crates().size());
      for (const auto& crate : graphValue.crates()) {
        auto projected = incremental::StableCrateQueryKey::fromVerified(crate);
        if (projected == zc::none) { return false; }
        ZC_IF_SOME(value, projected) { crates.add(zc::mv(value)); }
      }
      activeCrates = incremental::CanonicalCrateSet::from(zc::mv(crates));
    }
    ZC_IF_SOME(resolution, packageGraph) {
      ZC_IF_SOME(graphValue, crateGraph) {
        packageGraphInput =
            incremental::CanonicalPackageGraph::fromVerified(resolution, graphValue);
      }
    }
    if (packageRoots == zc::none || packageGraphInput == zc::none || activeCrates == zc::none ||
        compilationOptions == zc::none) {
      return false;
    }

    const auto verifyCompilationOptionsInput = [&] {
      ZC_IF_SOME(request, packageRequest) {
        ZC_IF_SOME(options, compilationOptions) {
          const auto& language = request.languageOptions();
          return options.hostTargetBytes() == targetSelectionBytes(request.hostTarget()).asPtr() &&
                 options.targetBytes() == targetSelectionBytes(request.target()).asPtr() &&
                 options.useUnicode() == language.useUnicode &&
                 options.allowDollarIdentifiers() == language.allowDollarIdentifiers &&
                 options.supportRegexLiterals() == language.supportRegexLiterals;
        }
      }
      return false;
    };
    if (!verifyCompilationOptionsInput()) { return false; }

    const auto verifyActiveCratesInput = [&] {
      ZC_IF_SOME(request, packageRequest) {
        ZC_IF_SOME(rootKey, packageRoots) {
          for (const auto& root : request.roots()) {
            const auto expected = root.packageKey().encode();
            size_t occurrences = 0;
            for (const auto& package : rootKey.packages()) {
              if (package.canonicalPackageBytes() == expected.asPtr()) { ++occurrences; }
            }
            if (occurrences != 1) { return false; }
          }
          for (const auto& package : rootKey.packages()) {
            bool found = false;
            for (const auto& root : request.roots()) {
              if (package.canonicalPackageBytes() == root.packageKey().encode().asPtr()) {
                found = true;
                break;
              }
            }
            if (!found) { return false; }
          }
        }
      }
      ZC_IF_SOME(graphValue, crateGraph) {
        ZC_IF_SOME(crates, activeCrates) {
          if (crates.crates().size() != graphValue.crates().size()) { return false; }
          for (const auto& expected : graphValue.crates()) {
            auto projected = incremental::StableCrateQueryKey::fromVerified(expected);
            if (projected == zc::none) { return false; }
            ZC_IF_SOME(value, projected) {
              if (!crates.contains(value)) { return false; }
            }
          }
          return true;
        }
      }
      return false;
    };
    if (!verifyActiveCratesInput()) { return false; }

    const auto verifyPackageGraphInput = [&] {
      ZC_IF_SOME(resolution, packageGraph) {
        ZC_IF_SOME(graphValue, crateGraph) {
          ZC_IF_SOME(value, packageGraphInput) {
            if (value.resolvedPackages().size() != resolution.packages().size() ||
                value.resolvedPackageEdges().size() != resolution.edges().size() ||
                value.selectedPackageEdges().size() != graphValue.packageEdges().size() ||
                value.crates().size() != graphValue.crates().size() ||
                value.crateEdges().size() != graphValue.edges().size()) {
              return false;
            }
            for (const auto& record : resolution.packages()) {
              const auto expected = record.key().encode();
              size_t occurrences = 0;
              for (const auto& candidate : value.resolvedPackages()) {
                if (candidate.canonicalPackageBytes() == expected.asPtr()) { ++occurrences; }
              }
              if (occurrences != 1) { return false; }
            }
            for (const auto& edge : resolution.edges()) {
              const auto expected = edge.encode();
              size_t occurrences = 0;
              for (const auto& candidate : value.resolvedPackageEdges()) {
                if (candidate.canonicalEdgeBytes() == expected.asPtr()) { ++occurrences; }
              }
              if (occurrences != 1) { return false; }
            }
            for (const auto& edge : graphValue.packageEdges()) {
              const auto expected = edge.encode();
              size_t occurrences = 0;
              for (const auto& candidate : value.selectedPackageEdges()) {
                if (candidate.canonicalEdgeBytes() == expected.asPtr()) { ++occurrences; }
              }
              if (occurrences != 1) { return false; }
            }
            for (const auto& crate : graphValue.crates()) {
              const auto expected = crate.encode();
              size_t occurrences = 0;
              for (const auto& candidate : value.crates()) {
                if (candidate.canonicalCrateBytes() == expected.asPtr()) { ++occurrences; }
              }
              if (occurrences != 1) { return false; }
            }
            for (const auto& edge : graphValue.edges()) {
              const auto expected = edge.encode();
              size_t occurrences = 0;
              for (const auto& candidate : value.crateEdges()) {
                if (candidate.canonicalEdgeBytes() == expected.asPtr()) { ++occurrences; }
              }
              if (occurrences != 1) { return false; }
            }
            ZC_IF_SOME(rootKey, packageRoots) {
              for (const auto& root : rootKey.packages()) {
                size_t occurrences = 0;
                for (const auto& candidate : value.resolvedPackages()) {
                  if (candidate.canonicalPackageBytes() == root.canonicalPackageBytes()) {
                    ++occurrences;
                  }
                }
                if (occurrences != 1) { return false; }
              }
            }
            return true;
          }
        }
      }
      return false;
    };
    if (!verifyPackageGraphInput()) { return false; }

    struct StagedModuleTopologyInput final {
      StagedModuleTopologyInput(incremental::StableModuleQueryKey&& requester,
                                incremental::SelectedModuleSource&& source) noexcept
          : requester(zc::mv(requester)), source(zc::mv(source)) {}
      StagedModuleTopologyInput(StagedModuleTopologyInput&&) noexcept = default;
      StagedModuleTopologyInput& operator=(StagedModuleTopologyInput&&) noexcept = default;
      ZC_DISALLOW_COPY(StagedModuleTopologyInput);

      incremental::StableModuleQueryKey requester;
      incremental::SelectedModuleSource source;
      zc::Vector<incremental::StableModuleQueryKey> dependencies;
    };

    struct StagedCrateMembershipInput final {
      StagedCrateMembershipInput(incremental::StableCrateQueryKey&& crate,
                                 incremental::CanonicalSourceSet&& sources,
                                 incremental::CanonicalModuleSet&& modules) noexcept
          : crate(zc::mv(crate)), sources(zc::mv(sources)), modules(zc::mv(modules)) {}
      StagedCrateMembershipInput(StagedCrateMembershipInput&&) noexcept = default;
      StagedCrateMembershipInput& operator=(StagedCrateMembershipInput&&) noexcept = default;
      ZC_DISALLOW_COPY(StagedCrateMembershipInput);

      incremental::StableCrateQueryKey crate;
      incremental::CanonicalSourceSet sources;
      incremental::CanonicalModuleSet modules;
    };

    struct StagedSourceSnapshotInput final {
      StagedSourceSnapshotInput(source_query::StableSourceQueryKey&& source,
                                source_query::CanonicalSourceSnapshot&& snapshot) noexcept
          : source(zc::mv(source)), snapshot(zc::mv(snapshot)) {}
      StagedSourceSnapshotInput(StagedSourceSnapshotInput&&) noexcept = default;
      StagedSourceSnapshotInput& operator=(StagedSourceSnapshotInput&&) noexcept = default;
      ZC_DISALLOW_COPY(StagedSourceSnapshotInput);

      source_query::StableSourceQueryKey source;
      source_query::CanonicalSourceSnapshot snapshot;
    };

    zc::Vector<StagedSourceSnapshotInput> sourceInputs(registries.sourceSnapshots().size());
    for (const auto& snapshot : registries.sourceSnapshots()) {
      auto source = source_query::StableSourceQueryKey::fromVerified(snapshot.source());
      auto value = source_query::CanonicalSourceSnapshot::fromVerified(snapshot);
      if (source == zc::none || value == zc::none) { return false; }
      ZC_IF_SOME(sourceValue, source) {
        ZC_IF_SOME(snapshotValue, value) {
          sourceInputs.add(StagedSourceSnapshotInput(zc::mv(sourceValue), zc::mv(snapshotValue)));
        }
      }
    }

    zc::Vector<StagedModuleTopologyInput> topology(graph.modules().size());
    zc::TreeMap<zc::String, size_t> topologyByRequester;
    zc::Vector<incremental::StableModuleQueryKey> activeKeys(graph.modules().size());
    for (const auto& module : graph.modules()) {
      auto stable = incremental::StableModuleQueryKey::fromVerified(module);
      auto moduleId = registries.modules().find(module);
      if (stable == zc::none || moduleId == zc::none) { return false; }
      zc::Maybe<incremental::SelectedModuleSource> selectedSource;
      ZC_IF_SOME(handle, moduleId) {
        auto source = graph.sourceFile(handle);
        if (source == zc::none) { return false; }
        ZC_IF_SOME(sourceValue, source) {
          selectedSource = incremental::SelectedModuleSource::fromVerified(module, sourceValue);
        }
      }
      if (selectedSource == zc::none) { return false; }
      auto encoded = zc::encodeHex(module.encode().asPtr());
      if (topologyByRequester.find(encoded) != zc::none) { return false; }
      ZC_IF_SOME(value, stable) {
        ZC_IF_SOME(sourceValue, selectedSource) {
          activeKeys.add(value.clone());
          topologyByRequester.insert(zc::mv(encoded), topology.size());
          topology.add(StagedModuleTopologyInput(zc::mv(value), zc::mv(sourceValue)));
        }
      }
    }
    for (const auto& edge : graph.edges()) {
      auto requesterKey = registries.modules().lookup(edge.request().requester());
      auto dependencyKey = registries.modules().lookup(edge.target());
      if (requesterKey == zc::none || dependencyKey == zc::none) { return false; }
      ZC_IF_SOME(requester, requesterKey) {
        auto requesterIndex = topologyByRequester.find(zc::encodeHex(requester.encode().asPtr()));
        if (requesterIndex == zc::none) { return false; }
        ZC_IF_SOME(dependency, dependencyKey) {
          auto stableDependency = incremental::StableModuleQueryKey::fromVerified(dependency);
          if (stableDependency == zc::none) { return false; }
          ZC_IF_SOME(index, requesterIndex) {
            ZC_IF_SOME(value, stableDependency) { topology[index].dependencies.add(zc::mv(value)); }
          }
        }
      }
    }

    auto active = incremental::CanonicalModuleSet::from(zc::mv(activeKeys));
    if (active == zc::none) { return false; }

    zc::Vector<StagedCrateMembershipInput> crateMemberships;
    ZC_IF_SOME(graphValue, crateGraph) {
      crateMemberships.reserve(graphValue.crates().size());
      for (const auto& crate : graphValue.crates()) {
        auto stableCrate = incremental::StableCrateQueryKey::fromVerified(crate);
        if (stableCrate == zc::none) { return false; }
        zc::Vector<source_query::StableSourceQueryKey> crateSources;
        for (const auto& snapshot : registries.sourceSnapshots()) {
          if (!snapshot.source().belongsTo(crate)) { continue; }
          auto source = source_query::StableSourceQueryKey::fromVerified(snapshot.source());
          if (source == zc::none) { return false; }
          ZC_IF_SOME(value, source) { crateSources.add(zc::mv(value)); }
        }
        zc::Vector<incremental::StableModuleQueryKey> crateModules;
        for (const auto& module : graph.modules()) {
          if (module.crate().encode().asPtr() != crate.encode().asPtr()) { continue; }
          auto projected = incremental::StableModuleQueryKey::fromVerified(module);
          if (projected == zc::none) { return false; }
          ZC_IF_SOME(value, projected) { crateModules.add(zc::mv(value)); }
        }
        auto sources = incremental::CanonicalSourceSet::from(zc::mv(crateSources));
        auto modules = incremental::CanonicalModuleSet::from(zc::mv(crateModules));
        if (sources == zc::none || modules == zc::none ||
            ZC_ASSERT_NONNULL(modules).modules().size() == 0) {
          return false;
        }
        ZC_IF_SOME(crateValue, stableCrate) {
          ZC_IF_SOME(sourceValue, sources) {
            ZC_IF_SOME(moduleValue, modules) {
              crateMemberships.add(StagedCrateMembershipInput(
                  zc::mv(crateValue), zc::mv(sourceValue), zc::mv(moduleValue)));
            }
          }
        }
      }
    }

    const auto verifyCrateMembershipInputs = [&] {
      ZC_IF_SOME(graphValue, crateGraph) {
        if (crateMemberships.size() != graphValue.crates().size()) { return false; }
        for (const auto& crate : graphValue.crates()) {
          auto expectedCrate = incremental::StableCrateQueryKey::fromVerified(crate);
          if (expectedCrate == zc::none) { return false; }
          size_t membershipOccurrences = 0;
          for (const auto& membership : crateMemberships) {
            ZC_IF_SOME(expected, expectedCrate) {
              if (membership.crate != expected) { continue; }
            }
            ++membershipOccurrences;
            size_t expectedSources = 0;
            for (const auto& snapshot : registries.sourceSnapshots()) {
              if (!snapshot.source().belongsTo(crate)) { continue; }
              ++expectedSources;
              auto source = source_query::StableSourceQueryKey::fromVerified(snapshot.source());
              if (source == zc::none) { return false; }
              ZC_IF_SOME(value, source) {
                if (!membership.sources.contains(value)) { return false; }
              }
            }
            if (membership.sources.sources().size() != expectedSources) { return false; }
            size_t expectedModules = 0;
            for (const auto& module : graph.modules()) {
              if (module.crate().encode().asPtr() != crate.encode().asPtr()) { continue; }
              ++expectedModules;
              auto projected = incremental::StableModuleQueryKey::fromVerified(module);
              if (projected == zc::none) { return false; }
              ZC_IF_SOME(value, projected) {
                if (!membership.modules.contains(value)) { return false; }
              }
            }
            if (expectedModules == 0 || membership.modules.modules().size() != expectedModules) {
              return false;
            }
          }
          if (membershipOccurrences != 1) { return false; }
        }
        return true;
      }
      return false;
    };
    if (!verifyCrateMembershipInputs()) { return false; }

    const auto verifySelectedModuleSourceInputs = [&](const incremental::CanonicalModuleSet& set) {
      if (set.modules().size() != graph.modules().size() ||
          topology.size() != graph.modules().size()) {
        return false;
      }
      for (const auto& module : graph.modules()) {
        auto expectedModule = incremental::StableModuleQueryKey::fromVerified(module);
        auto handle = registries.modules().find(module);
        if (expectedModule == zc::none || handle == zc::none) { return false; }
        zc::Maybe<incremental::SelectedModuleSource> expectedSource;
        ZC_IF_SOME(moduleHandle, handle) {
          ZC_IF_SOME(source, graph.sourceFile(moduleHandle)) {
            expectedSource = incremental::SelectedModuleSource::fromVerified(module, source);
          }
        }
        if (expectedSource == zc::none) { return false; }
        size_t occurrences = 0;
        for (const auto& candidate : topology) {
          ZC_IF_SOME(moduleKey, expectedModule) {
            if (candidate.requester != moduleKey) { continue; }
          }
          ++occurrences;
          ZC_IF_SOME(source, expectedSource) {
            if (candidate.source != source) { return false; }
          }
        }
        ZC_IF_SOME(moduleKey, expectedModule) {
          if (occurrences != 1 || !set.contains(moduleKey)) { return false; }
        }
      }
      return true;
    };
    ZC_IF_SOME(activeValue, active) {
      if (!verifySelectedModuleSourceInputs(activeValue)) { return false; }
    }

    const auto verifySourceSnapshotInputs = [&] {
      if (sourceInputs.size() != registries.sourceSnapshots().size()) { return false; }
      for (const auto& expected : registries.sourceSnapshots()) {
        const auto expectedKey = expected.source().encode();
        size_t occurrences = 0;
        for (const auto& candidate : sourceInputs) {
          if (candidate.source.canonicalSourceBytes() != expectedKey.asPtr()) { continue; }
          ++occurrences;
          if (candidate.snapshot.contentDigest() != expected.contentDigest() ||
              candidate.snapshot.bytes() != expected.bytes()) {
            return false;
          }
        }
        if (occurrences != 1) { return false; }
      }
      return true;
    };
    if (!verifySourceSnapshotInputs()) { return false; }

    zc::Vector<incremental::SelectedModuleSource> selectedSources(topology.size());
    for (const auto& input : topology) { selectedSources.add(input.source.clone()); }
    zc::Vector<source_query::StableSourceQueryKey> snapshotSources(sourceInputs.size());
    for (const auto& input : sourceInputs) { snapshotSources.add(input.source.clone()); }
    if (!incremental::verifySelectedSourceSnapshotClosure(selectedSources.asPtr(),
                                                          snapshotSources.asPtr())) {
      return false;
    }

    auto pending = activeDefinitionAuthority.beginBaseMutation(queryDatabase);
    if (pending == zc::none) { return false; }
    ZC_IF_SOME(transaction, pending) {
      for (const auto& prior : stagedTopologyModules) {
        if (!transaction.erase<incremental::ModuleDependenciesInput>(prior)) { return false; }
      }
      for (const auto& prior : stagedSelectedModules) {
        if (!transaction.erase<incremental::SelectedModuleSourceInput>(prior)) { return false; }
      }
      for (const auto& prior : stagedSourceSnapshots) {
        if (!transaction.erase<source_query::SourceSnapshotInput>(prior)) { return false; }
      }
      for (const auto& prior : stagedActiveCrates) {
        if (!transaction.erase<incremental::ActiveSourcesInput>(prior) ||
            !transaction.erase<incremental::ActiveModulesInput>(prior)) {
          return false;
        }
      }
      ZC_IF_SOME(prior, stagedPackageRoots) {
        ZC_IF_SOME(current, packageRoots) {
          if (prior != current) {
            if (!transaction.erase<incremental::PackageGraphInput>(prior) ||
                !transaction.erase<incremental::ActiveCratesInput>(prior)) {
              return false;
            }
          }
        }
      }
      ZC_IF_SOME(activeValue, active) {
        ZC_IF_SOME(options, compilationOptions) {
          if (!transaction.set<source_query::CompilationOptionsInput>(
                  source_query::CompilationUnitQueryKey::fixed(), options)) {
            return false;
          }
        }
        ZC_IF_SOME(rootKey, packageRoots) {
          ZC_IF_SOME(graphInput, packageGraphInput) {
            if (!transaction.set<incremental::PackageGraphInput>(rootKey, graphInput)) {
              return false;
            }
          }
          ZC_IF_SOME(crates, activeCrates) {
            if (!transaction.set<incremental::ActiveCratesInput>(rootKey, crates)) { return false; }
          }
        }
        for (const auto& membership : crateMemberships) {
          if (!transaction.set<incremental::ActiveSourcesInput>(membership.crate,
                                                                membership.sources) ||
              !transaction.set<incremental::ActiveModulesInput>(membership.crate,
                                                                membership.modules)) {
            return false;
          }
        }
        for (auto& input : topology) {
          if (!transaction.set<incremental::SelectedModuleSourceInput>(input.requester,
                                                                       input.source)) {
            return false;
          }
          auto canonicalDependencies =
              incremental::CanonicalModuleSet::from(zc::mv(input.dependencies));
          if (canonicalDependencies == zc::none) { return false; }
          ZC_IF_SOME(dependencyValue, canonicalDependencies) {
            auto dependencyInput =
                incremental::ModuleDependencySet::present(zc::mv(dependencyValue));
            if (!transaction.set<incremental::ModuleDependenciesInput>(input.requester,
                                                                       dependencyInput)) {
              return false;
            }
          }
        }
        for (const auto& input : sourceInputs) {
          if (!transaction.set<source_query::SourceSnapshotInput>(input.source, input.snapshot)) {
            return false;
          }
        }

        if (transaction.commit() == zc::none) { return false; }
        stagedTopologyModules.clear();
        stagedTopologyModules.reserve(activeValue.modules().size());
        for (const auto& module : activeValue.modules()) {
          stagedTopologyModules.add(module.clone());
        }
        stagedSelectedModules.clear();
        stagedSelectedModules.reserve(activeValue.modules().size());
        for (const auto& module : activeValue.modules()) {
          stagedSelectedModules.add(module.clone());
        }
        stagedSourceSnapshots.clear();
        stagedSourceSnapshots.reserve(sourceInputs.size());
        for (const auto& input : sourceInputs) { stagedSourceSnapshots.add(input.source.clone()); }
        stagedActiveCrates.clear();
        stagedActiveCrates.reserve(crateMemberships.size());
        for (const auto& membership : crateMemberships) {
          stagedActiveCrates.add(membership.crate.clone());
        }
        ZC_IF_SOME(rootKey, packageRoots) { stagedPackageRoots = rootKey.clone(); }
        return true;
      }
    }
    return false;
  }

  zc::Maybe<source::BufferId> registerVerifiedSource(
      const identity::CrateKey& crate, const identity::CanonicalRelativePath& sourcePath,
      zc::ArrayPtr<const identity::ModulePathSegment> modulePath, bool& added) {
    added = false;
    if (modulePath.size() == 0 || identityRegistries == zc::none || packageRequest == zc::none) {
      return zc::none;
    }

    zc::Maybe<const package::ResolvedPackageSourceSnapshot&> selectedSnapshot;
    for (const auto& candidate : packageSnapshots) {
      if (!packageMatchesBase(crate.package(), candidate.package())) { continue; }
      if (selectedSnapshot != zc::none) { return zc::none; }
      selectedSnapshot = candidate;
    }
    if (selectedSnapshot == zc::none) { return zc::none; }

    auto origin = sourceOriginFor(crate.package(), sourcePath);
    if (origin == zc::none) { return zc::none; }
    ZC_IF_SOME(originValue, origin) {
      auto sourceKey = identity::SourceFileKey::from(crate.clone(), zc::mv(originValue));
      ZC_IF_SOME(snapshot, selectedSnapshot) {
        auto bytes = snapshot.snapshot().readVerifiedFile(sourcePath);
        if (!bytes.is<zc::Array<zc::byte>>()) { return zc::none; }
        return registerSource(zc::mv(sourceKey), modulePath,
                              zc::mv(bytes.get<zc::Array<zc::byte>>()),
                              packageSourceIdentifier(crate.package(), sourcePath), added);
      }
    }
    return zc::none;
  }

  zc::Maybe<source::BufferId> registerGeneratedSource(
      const identity::CrateKey& crate, const package::VerifiedBuildScriptResult& result,
      identity::BuildScriptProducerKey producer, const identity::CanonicalRelativePath& sourcePath,
      zc::ArrayPtr<const identity::ModulePathSegment> modulePath, bool& added) {
    added = false;
    if (modulePath.size() == 0 || result.output().producerKey().digest() != producer.digest()) {
      return zc::none;
    }
    size_t matches = 0;
    for (const auto& file : result.run().outputs().files()) {
      if (!sameRelativePath(file.path(), sourcePath)) { continue; }
      ++matches;
    }
    if (matches != 1) { return zc::none; }
    auto bytes = result.run().outputSnapshot().readVerifiedFile(sourcePath);
    if (!bytes.is<zc::Array<zc::byte>>()) { return zc::none; }
    auto sourceKey = identity::SourceFileKey::from(
        crate.clone(), identity::SourceOriginKey::generatedFile(producer, sourcePath.clone()));
    return registerSource(zc::mv(sourceKey), modulePath, zc::mv(bytes.get<zc::Array<zc::byte>>()),
                          generatedSourceIdentifier(crate.package(), sourcePath), added);
  }

  zc::Maybe<source::BufferId> registerSource(
      identity::SourceFileKey&& sourceKey,
      zc::ArrayPtr<const identity::ModulePathSegment> modulePath, zc::Array<zc::byte>&& bytes,
      zc::String&& identifier, bool& added) {
    const auto encoded = sourceKey.encode();
    for (const auto& existing : pendingSourceIdentities) {
      if (!sameBytes(existing.value.encode().asPtr(), encoded.asPtr())) { continue; }
      auto existingPath = pendingModulePaths.find(existing.key);
      if (existingPath == zc::none) { return zc::none; }
      ZC_IF_SOME(pathValue, existingPath) {
        if (!sameModulePath(pathValue.asPtr(), modulePath)) { return zc::none; }
      }
      return existing.key;
    }

    const auto buffer = sourceManager->addNewSourceBuffer(zc::mv(bytes), identifier);
    pendingSourceIdentities.upsert(buffer, zc::mv(sourceKey));
    pendingModulePaths.upsert(buffer, cloneModulePath(modulePath));
    added = true;
    return buffer;
  }

  zc::Maybe<const package::FinalizedCompilationRoot&> compilationRoot(
      const identity::CrateKey& crate) const {
    if (crateGraph == zc::none) { return zc::none; }
    zc::Maybe<const package::FinalizedCompilationRoot&> selected;
    ZC_IF_SOME(graph, crateGraph) {
      const auto expected = crate.encode();
      for (const auto& root : graph.roots()) {
        if (!sameBytes(root.crateKey().encode().asPtr(), expected.asPtr())) { continue; }
        if (selected != zc::none) { return zc::none; }
        selected = root;
      }
    }
    return selected;
  }

  zc::Maybe<const package::ResolvedPackageSourceSnapshot&> packageSnapshot(
      const identity::PackageKey& package) const {
    zc::Maybe<const package::ResolvedPackageSourceSnapshot&> selected;
    for (const auto& candidate : packageSnapshots) {
      if (!packageMatchesBase(package, candidate.package())) { continue; }
      if (selected != zc::none) { return zc::none; }
      selected = candidate;
    }
    return selected;
  }

  bool discoverModuleSourceCandidate(const identity::CrateKey& crate,
                                     zc::ArrayPtr<const identity::ModulePathSegment> selectedPath,
                                     bool& addedAny) {
    if (selectedPath.size() == 0) { return false; }
    auto root = compilationRoot(crate);
    auto snapshot = packageSnapshot(crate.package());
    if (root == zc::none || snapshot == zc::none) { return false; }
    ZC_IF_SOME(rootValue, root) {
      if (selectedPath[0].text() != rootValue.crateKey().targetName()) { return true; }
      if (selectedPath.size() == 1) { return true; }
      ZC_IF_SOME(snapshotValue, snapshot) {
        const auto searchRoot = parentDirectory(rootValue.sourcePath());
        const auto moduleSuffix = selectedPath.slice(1, selectedPath.size());
        auto discovered =
            discoverModuleSource(snapshotValue.snapshot().record(), searchRoot, moduleSuffix);
        if (discovered.is<InvalidModuleSourceRequest>()) { return false; }
        const auto registerPath = [&](const identity::CanonicalRelativePath& path) {
          bool added = false;
          auto registered = registerVerifiedSource(crate, path, selectedPath, added);
          if (registered == zc::none) { return false; }
          addedAny = addedAny || added;
          return true;
        };
        if (discovered.is<ResolvedModuleSource>()) {
          if (!registerPath(discovered.get<ResolvedModuleSource>().path())) { return false; }
        }
        if (discovered.is<AmbiguousModuleSource>()) {
          for (const auto& path : discovered.get<AmbiguousModuleSource>().paths()) {
            if (!registerPath(path)) { return false; }
          }
        }

        ZC_IF_SOME(results, buildScriptResults) {
          zc::Vector<identity::CanonicalPathSegment> noSegments;
          const auto generatedRoot = identity::CanonicalRelativePath::from(zc::mv(noSegments));
          for (size_t index = 0; index < results.results().size(); ++index) {
            const auto& planKey = results.planKeys()[index];
            const auto& result = results.results()[index];
            if (!samePackage(crate.package(), planKey.package())) { continue; }
            auto generated =
                discoverModuleSource(result.run().outputs(), generatedRoot, moduleSuffix);
            if (generated.is<InvalidModuleSourceRequest>()) { return false; }
            const auto registerGenerated = [&](const identity::CanonicalRelativePath& path) {
              bool added = false;
              auto registered = registerGeneratedSource(crate, result, planKey.producerKey(), path,
                                                        selectedPath, added);
              if (registered == zc::none) { return false; }
              addedAny = addedAny || added;
              return true;
            };
            if (generated.is<ResolvedModuleSource>() &&
                !registerGenerated(generated.get<ResolvedModuleSource>().path())) {
              return false;
            }
            if (generated.is<AmbiguousModuleSource>()) {
              for (const auto& path : generated.get<AmbiguousModuleSource>().paths()) {
                if (!registerGenerated(path)) { return false; }
              }
            }
          }
        }
        return true;
      }
    }
    return false;
  }

  bool discoverDependencies(const source::BufferId& requesterBuffer,
                            zc::ArrayPtr<const StructuralModuleDependencyRequest> requests,
                            bool& addedAny) {
    if (crateGraph == zc::none) { return false; }
    auto requesterSource = pendingSourceIdentities.find(requesterBuffer);
    auto requesterPath = pendingModulePaths.find(requesterBuffer);
    if (requesterSource == zc::none || requesterPath == zc::none) { return false; }

    struct DiscoveryTarget final {
      DiscoveryTarget(identity::CrateKey&& crate,
                      zc::Vector<identity::ModulePathSegment>&& path) noexcept
          : crate(zc::mv(crate)), path(zc::mv(path)) {}
      DiscoveryTarget(DiscoveryTarget&&) noexcept = default;
      DiscoveryTarget& operator=(DiscoveryTarget&&) noexcept = default;
      ZC_DISALLOW_COPY(DiscoveryTarget);
      identity::CrateKey crate;
      zc::Vector<identity::ModulePathSegment> path;
    };

    ZC_IF_SOME(sourceValue, requesterSource) {
      ZC_IF_SOME(pathValue, requesterPath) {
        const auto& requesterCrate = sourceValue.crate();
        for (const auto& request : requests) {
          if (request.normalizedPath().size() == 0) { return false; }
          zc::TreeMap<zc::String, DiscoveryTarget> targets;
          const auto addTarget = [&](identity::CrateKey&& crate,
                                     zc::Vector<identity::ModulePathSegment>&& path) {
            if (path.size() == 0) { return false; }
            auto key = zc::encodeHex(encodeStructuralModulePath(crate, path.asPtr()).asPtr());
            if (targets.find(key) == zc::none) {
              targets.insert(zc::mv(key), DiscoveryTarget(zc::mv(crate), zc::mv(path)));
            }
            return true;
          };

          for (size_t prefixSize = pathValue.size(); prefixSize != 0; --prefixSize) {
            zc::Vector<identity::ModulePathSegment> candidate(prefixSize +
                                                              request.normalizedPath().size());
            for (size_t index = 0; index < prefixSize; ++index) {
              candidate.add(pathValue[index].clone());
            }
            for (const auto& segment : request.normalizedPath()) { candidate.add(segment.clone()); }
            if (!addTarget(requesterCrate.clone(), zc::mv(candidate))) { return false; }
          }
          if (!addTarget(requesterCrate.clone(), cloneModulePath(request.normalizedPath()))) {
            return false;
          }

          ZC_IF_SOME(graph, crateGraph) {
            for (const auto& edge : graph.edges()) {
              if (!sameBytes(edge.consumer().encode().asPtr(), requesterCrate.encode().asPtr()) ||
                  request.normalizedPath()[0].text() != edge.packageEdge().alias()) {
                continue;
              }
              zc::Vector<identity::ModulePathSegment> providerPath;
              auto rootSegment =
                  identity::ModulePathSegment::fromCanonical(edge.provider().targetName());
              if (rootSegment == zc::none) { return false; }
              ZC_IF_SOME(value, rootSegment) { providerPath.add(zc::mv(value)); }
              for (size_t index = 1; index < request.normalizedPath().size(); ++index) {
                providerPath.add(request.normalizedPath()[index].clone());
              }
              if (!addTarget(edge.provider().clone(), zc::mv(providerPath))) { return false; }
            }
          }

          for (const auto& entry : targets) {
            if (!discoverModuleSourceCandidate(entry.value.crate, entry.value.path.asPtr(),
                                               addedAny)) {
              return false;
            }
          }
        }
      }
    }
    return true;
  }

  bool freezePackageAndCrateIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none) { return false; }
    if (crateGraph == zc::none) {
      package::PackageDiagnosticAdapter::emitBuildScriptIssue(
          *diagnosticEngine, package::BuildScriptIssue::BuildResultIntegrityViolation);
      return false;
    }

    bool failed = false;
    ZC_IF_SOME(registries, identityRegistries) {
      uint32_t traversalOrdinal = 0;
      ZC_IF_SOME(graph, crateGraph) {
        zc::Vector<zc::Array<uint8_t>> collectedPackageKeys;
        for (const auto& crate : graph.crates()) {
          auto packageKey = crate.package().clone();
          auto encoded = packageKey.encode();
          bool alreadyCollected = false;
          for (const auto& prior : collectedPackageKeys) {
            if (sameBytes(prior.asPtr(), encoded.asPtr())) {
              alreadyCollected = true;
              break;
            }
          }
          if (alreadyCollected) { continue; }
          collectedPackageKeys.add(zc::mv(encoded));
          failed = registries.collectPackage(zc::mv(packageKey), traversalOrdinal++) !=
                       identity::FrozenRegistryFailure::None ||
                   failed;
        }
        failed = registries.freezePackages() != identity::FrozenRegistryFailure::None || failed;

        traversalOrdinal = 0;
        for (const auto& crate : graph.crates()) {
          failed = registries.collectCrate(crate.clone(), traversalOrdinal++) !=
                       identity::FrozenRegistryFailure::None ||
                   failed;
        }
      }
      failed = registries.freezeCrates() != identity::FrozenRegistryFailure::None || failed;
      if (failed) { emitIdentityFailures(registries, *diagnosticEngine); }
    }
    return !failed;
  }

  bool freezeSourceIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none || pendingSourceIdentities.size() == 0 ||
        pendingSourceIdentities.size() != sourceManager->getManagedBufferIds().size() ||
        sourceIdentities.size() != 0) {
      return false;
    }

    bool failed = false;
    ZC_IF_SOME(registries, identityRegistries) {
      if (!registries.packages().isFrozen() || !registries.crates().isFrozen() ||
          registries.sourceFiles().isFrozen()) {
        return false;
      }
      zc::TreeMap<zc::String, source::BufferId> canonicalSources;
      for (const auto& entry : pendingSourceIdentities) {
        auto sortKey = zc::encodeHex(entry.value.encode().asPtr());
        if (canonicalSources.find(sortKey) != zc::none) { return false; }
        canonicalSources.insert(zc::mv(sortKey), entry.key);
      }

      uint32_t traversalOrdinal = 0;
      for (const auto& entry : canonicalSources) {
        auto pending = pendingSourceIdentities.find(entry.value);
        if (pending == zc::none) {
          failed = true;
          continue;
        }
        ZC_IF_SOME(sourceKey, pending) {
          auto snapshot = identity::ImmutableSourceSnapshot::from(
              sourceKey.clone(), zc::heapArray(sourceManager->getEntireTextForBuffer(entry.value)));
          if (snapshot == zc::none) {
            failed = true;
            continue;
          }
          ZC_IF_SOME(value, snapshot) {
            failed = registries.collectSourceFile(zc::mv(value), traversalOrdinal++) !=
                         identity::FrozenRegistryFailure::None ||
                     failed;
          }
        }
      }
      failed = registries.freezeSourceFiles() != identity::FrozenRegistryFailure::None || failed;
      if (!failed) {
        for (const auto& entry : pendingSourceIdentities) {
          auto source = registries.sourceFiles().find(entry.value);
          ZC_IF_SOME(value, source) {
            sourceIdentities.upsert(entry.key, value);
          } else {
            failed = true;
          }
        }
      }
      if (failed) { emitIdentityFailures(registries, *diagnosticEngine); }
    }
    return !failed;
  }

  bool freezeSemanticContextFingerprint() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none || crateGraph == zc::none ||
        semanticContextFingerprint != zc::none) {
      return false;
    }
    ZC_IF_SOME(registries, identityRegistries) {
      ZC_IF_SOME(crates, crateGraph) {
        auto fingerprint = identity::SemanticContextFingerprint::compute(
            registries, crates.packageEdges(), crates.edges());
        ZC_IF_SOME(value, fingerprint) {
          semanticContextFingerprint = zc::mv(value);
          return true;
        }
      }
    }
    return false;
  }

  zc::Maybe<identity::SourceFileId> sourceIdentity(const source::BufferId& buffer) const {
    ZC_IF_SOME(value, sourceIdentities.find(buffer)) { return value; }
    return zc::none;
  }

  zc::Maybe<identity::ModuleId> moduleIdentity(const source::BufferId& buffer,
                                               ast::NodeId node) const {
    for (const auto& binding : moduleIdentities) {
      if (binding.buffer == buffer && binding.node == node) { return binding.identity; }
    }
    return zc::none;
  }

  bool freezeModuleIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none) { return false; }

    struct PendingModule final {
      source::BufferId buffer;
      ast::NodeId node;
      identity::ModuleKey key;

      PendingModule(source::BufferId buffer, ast::NodeId node, identity::ModuleKey&& key)
          : buffer(zc::mv(buffer)), node(node), key(zc::mv(key)) {}
      PendingModule(PendingModule&&) noexcept = default;
      PendingModule& operator=(PendingModule&&) noexcept = default;
      ZC_DISALLOW_COPY(PendingModule);
    };

    bool failed = false;
    zc::Vector<PendingModule> pending;
    ZC_IF_SOME(registries, identityRegistries) {
      uint32_t traversalOrdinal = 0;
      for (const auto& parsedRecord : parsedModules) {
        const auto& buffer = parsedRecord.buffer();
        const auto& tree = parsedRecord.parsedModule().tree();
        auto inventoryValue = binder::DefinitionInventory::collect(tree);
        auto sourceId = sourceIdentity(buffer);
        auto selectedPath = pendingModulePaths.find(buffer);
        if (sourceId == zc::none || selectedPath == zc::none ||
            inventoryValue.modules().size() > 1) {
          failed = true;
          continue;
        }
        ZC_IF_SOME(sourceHandle, sourceId) {
          auto sourceKey = registries.sourceFiles().lookup(sourceHandle);
          if (sourceKey == zc::none || registries.sourceSnapshot(sourceHandle) == zc::none) {
            failed = true;
            continue;
          }
          ZC_IF_SOME(sourceValue, sourceKey) {
            ZC_IF_SOME(pathValue, selectedPath) {
              if (pathValue.size() == 0) {
                failed = true;
                continue;
              }
              ast::NodeId moduleNode;
              if (inventoryValue.modules().size() == 1) {
                const auto& module = inventoryValue.modules()[0];
                auto declaredName =
                    identity::ModulePathSegment::fromSource(tree.ident(module.declaredName));
                if (module.parentModuleNode || declaredName == zc::none) {
                  failed = true;
                  continue;
                }
                bool nameMatches = false;
                ZC_IF_SOME(value, declaredName) { nameMatches = value == pathValue.back(); }
                if (!nameMatches) {
                  auto declarationSpan = parsedRecord.parsedModule().spanFor(module.source);
                  if (declarationSpan == zc::none) {
                    failed = true;
                    continue;
                  }
                  auto declarationStart =
                      parsedRecord.parsedModule().sourceLocFor(ZC_ASSERT_NONNULL(declarationSpan));
                  if (declarationStart == zc::none) {
                    failed = true;
                    continue;
                  }
                  ZC_IF_SOME(value, declaredName) {
                    const auto start = ZC_ASSERT_NONNULL(declarationStart);
                    auto diagnostic =
                        diagnosticEngine
                            ->diagnose<diagnostics::DiagID::ModuleDeclarationNameMismatch>(
                                start, zc::str(value.text()), zc::str(pathValue.back().text()));
                    diagnostic.addRange(source::CharSourceRange::getCharRange(
                        start, start.getAdvancedLoc(module.source.getLength())));
                    diagnostic.emit();
                  }
                  failed = true;
                  continue;
                }
                moduleNode = module.node;
              }
              zc::Vector<identity::ModulePathSegment> keyPath(pathValue.size());
              for (const auto& segment : pathValue) { keyPath.add(segment.clone()); }
              auto key = identity::ModuleKey::from(sourceValue.crate().clone(), zc::mv(keyPath));
              ZC_IF_SOME(value, key) {
                auto retained = value.clone();
                failed = registries.collectModule(zc::mv(value), traversalOrdinal++) !=
                             identity::FrozenRegistryFailure::None ||
                         failed;
                pending.add(PendingModule(buffer, moduleNode, zc::mv(retained)));
              } else {
                failed = true;
              }
            }
          }
        }
      }
      failed = registries.freezeModules() != identity::FrozenRegistryFailure::None || failed;
      if (!failed) {
        for (const auto& module : pending) {
          auto handle = registries.modules().find(module.key);
          ZC_IF_SOME(value, handle) {
            moduleIdentities.add(ModuleIdentityBinding{module.buffer, module.node, value});
          } else {
            failed = true;
          }
        }
      }
      if (failed) { emitIdentityFailures(registries, *diagnosticEngine); }
    }
    return !failed;
  }

  bool freezeDefinitionAndImplIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none || !frozenInventoryInputs.empty() ||
        !moduleBodyQueryBindings.empty()) {
      return false;
    }

    struct CandidateBinding final {
      CandidateBinding(size_t parsedIndex, source::BufferId buffer, identity::ModuleId module,
                       binder::StableIdentityCandidateInventory&& inventory,
                       binder::NamedDefinitionInventory&& definitions,
                       binder::NamedImplementationInventory&& implementations,
                       binder::ModuleBodySyntax&& bodySyntax,
                       binder::ModuleBodyProvenance&& bodyProvenance) noexcept
          : parsedIndex(parsedIndex),
            buffer(zc::mv(buffer)),
            module(module),
            inventory(zc::mv(inventory)),
            definitions(zc::mv(definitions)),
            implementations(zc::mv(implementations)),
            bodySyntax(zc::mv(bodySyntax)),
            bodyProvenance(zc::mv(bodyProvenance)) {}
      CandidateBinding(CandidateBinding&&) noexcept = default;
      CandidateBinding& operator=(CandidateBinding&&) noexcept = default;
      ZC_DISALLOW_COPY(CandidateBinding);
      size_t parsedIndex;
      source::BufferId buffer;
      identity::ModuleId module;
      binder::StableIdentityCandidateInventory inventory;
      binder::NamedDefinitionInventory definitions;
      binder::NamedImplementationInventory implementations;
      binder::ModuleBodySyntax bodySyntax;
      binder::ModuleBodyProvenance bodyProvenance;
    };

    bool failed = false;
    zc::Vector<CandidateBinding> candidateBindings;
    zc::Vector<binder::VerifiedStableDefinitionCandidate> verifiedDefinitions;
    zc::Vector<size_t> verifiedDefinitionParsedIndices;
    auto inventorySnapshot = queryDatabase.snapshot();
    ZC_IF_SOME(registries, identityRegistries) {
      uint32_t stableTraversalOrdinal = 0;
      for (size_t parsedIndex = 0; parsedIndex < parsedModules.size(); ++parsedIndex) {
        const auto& parsed = parsedModules[parsedIndex];
        const auto syntax = binder::DefinitionInventory::collect(parsed.parsedModule().tree());
        if (syntax.modules().size() > 1) {
          failed = true;
          break;
        }
        const ast::NodeId moduleNode =
            syntax.modules().size() == 1 ? syntax.modules()[0].node : ast::NodeId();
        auto module = moduleIdentity(parsed.buffer(), moduleNode);
        if (module == zc::none) {
          failed = true;
          break;
        }
        ZC_IF_SOME(moduleValue, module) {
          auto moduleKey = registries.modules().lookup(moduleValue);
          if (moduleKey == zc::none) {
            failed = true;
            break;
          }
          ZC_IF_SOME(moduleKeyValue, moduleKey) {
            auto produced = binder::StableIdentityCandidateProducer::produce(
                parsed.parsedModule().syntax(), moduleKeyValue, moduleNode);
            auto verification = binder::StableIdentityCandidateVerifier::verify(
                parsed.parsedModule().syntax(), moduleKeyValue, moduleNode, produced);
            if (verification.is<binder::StableIdentityCandidateSourceFailure>()) {
              const auto& sourceFailure =
                  verification.get<binder::StableIdentityCandidateSourceFailure>();
              auto location = parsed.parsedModule().sourceLocFor(sourceFailure.source);
              if (location == zc::none) {
                failed = true;
                break;
              }
              ZC_IF_SOME(value, location) {
                switch (sourceFailure.kind) {
                  case binder::StableIdentityCandidateSourceFailureKind::
                      ConstantExpressionNotAllowed:
                    diagnosticEngine->diagnose<diagnostics::DiagID::ConstantExpressionNotAllowed>(
                        value);
                    break;
                  case binder::StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter:
                    if (sourceFailure.previous == zc::none ||
                        sourceFailure.identifier == zc::none) {
                      failed = true;
                      break;
                    }
                    ZC_IF_SOME(previousSource, sourceFailure.previous) {
                      auto previous = parsed.parsedModule().sourceLocFor(previousSource);
                      if (previous == zc::none) {
                        failed = true;
                        break;
                      }
                      ZC_IF_SOME(previousValue, previous) {
                        ZC_IF_SOME(identifier, sourceFailure.identifier) {
                          if (!binder::BindingDiagnosticAdapter::emitRedeclaration(
                                  *diagnosticEngine,
                                  binder::BinderDiagnosticCode::DuplicateIdentifier, value,
                                  previousValue,
                                  binder::VerifiedIdentifierArgument::from(identifier))) {
                            failed = true;
                          }
                        }
                      }
                    }
                    break;
                }
              }
              failed = true;
              break;
            }
            if (!verification.is<binder::VerifiedStableIdentityCandidateInventory>() ||
                !produced.is<binder::StableIdentityCandidateInventory>()) {
              failed = true;
              break;
            }
            auto verified =
                zc::mv(verification.get<binder::VerifiedStableIdentityCandidateInventory>());
            auto queryKey =
                incremental_binding_query::StableModuleQueryKey::fromVerified(moduleKeyValue);
            if (queryKey == zc::none) {
              failed = true;
              break;
            }
            auto definitions =
                inventorySnapshot.get<incremental_binding_query::NamedDefinitionInventoryQuery>(
                    ZC_ASSERT_NONNULL(queryKey));
            auto implementations =
                inventorySnapshot.get<incremental_binding_query::NamedImplementationInventoryQuery>(
                    ZC_ASSERT_NONNULL(queryKey));
            auto definitionSites =
                inventorySnapshot.get<incremental_binding_query::RevisionLocalDefinitionSitesQuery>(
                    ZC_ASSERT_NONNULL(queryKey));
            auto implementationSites =
                inventorySnapshot
                    .get<incremental_binding_query::RevisionLocalImplementationSitesQuery>(
                        ZC_ASSERT_NONNULL(queryKey));
            auto bodySyntax =
                inventorySnapshot.get<incremental_binding_query::ModuleBodySyntaxQuery>(
                    ZC_ASSERT_NONNULL(queryKey));
            auto bodyProvenance =
                inventorySnapshot.get<incremental_binding_query::ModuleBodyProvenanceQuery>(
                    ZC_ASSERT_NONNULL(queryKey));
            if (definitions.isRuntimeFailure() || implementations.isRuntimeFailure() ||
                definitionSites.isRuntimeFailure() || implementationSites.isRuntimeFailure() ||
                bodySyntax.isRuntimeFailure() || bodyProvenance.isRuntimeFailure() ||
                definitions.kind() != query::QueryValueKind::Value ||
                implementations.kind() != query::QueryValueKind::Value ||
                definitionSites.kind() != query::QueryValueKind::Value ||
                implementationSites.kind() != query::QueryValueKind::Value ||
                bodySyntax.kind() != query::QueryValueKind::Value ||
                bodyProvenance.kind() != query::QueryValueKind::Value) {
              failed = true;
              break;
            }
            zc::Vector<identity::DefinitionIdentityAuthority> definitionAuthorities(
                verified.definitions.size());
            for (const auto& definition : verified.definitions) {
              definitionAuthorities.add(definition.authority.clone());
            }
            zc::Vector<identity::ImplIdentityAuthority> implementationAuthorities(
                verified.implementations.size());
            for (const auto& implementation : verified.implementations) {
              implementationAuthorities.add(implementation.authority.clone());
            }
            auto expectedDefinitions = binder::NamedDefinitionInventory::fromVerified(
                moduleKeyValue, definitionAuthorities.asPtr());
            auto expectedImplementations = binder::NamedImplementationInventory::fromVerified(
                moduleKeyValue, implementationAuthorities.asPtr());
            if (expectedDefinitions == zc::none || expectedImplementations == zc::none ||
                !ZC_ASSERT_NONNULL(expectedDefinitions).sameAs(definitions.value()) ||
                !ZC_ASSERT_NONNULL(expectedImplementations).sameAs(implementations.value())) {
              failed = true;
              break;
            }
            for (auto& definition : verified.definitions) {
              verifiedDefinitions.add(zc::mv(definition));
              verifiedDefinitionParsedIndices.add(parsedIndex);
            }
            auto inventory = zc::mv(produced.get<binder::StableIdentityCandidateInventory>());
            candidateBindings.add(
                CandidateBinding(parsedIndex, parsed.buffer(), moduleValue, zc::mv(inventory),
                                 definitions.value().clone(), implementations.value().clone(),
                                 bodySyntax.value().clone(), bodyProvenance.value().clone()));
          }
        }
      }

      if (!failed) {
        auto validation = binder::StableIdentityCandidateVerifier::findDefinitionRedeclarations(
            verifiedDefinitions.asPtr());
        if (!validation.is<zc::Vector<binder::StableDefinitionRedeclaration>>()) {
          failed = true;
        } else {
          auto redeclarations =
              zc::mv(validation.get<zc::Vector<binder::StableDefinitionRedeclaration>>());
          for (const auto& redeclaration : redeclarations) {
            if (redeclaration.first >= verifiedDefinitions.size() ||
                redeclaration.duplicate >= verifiedDefinitions.size()) {
              failed = true;
              break;
            }
            const auto& first = verifiedDefinitions[redeclaration.first];
            const auto& duplicate = verifiedDefinitions[redeclaration.duplicate];
            const auto firstParsedIndex = verifiedDefinitionParsedIndices[redeclaration.first];
            const auto duplicateParsedIndex =
                verifiedDefinitionParsedIndices[redeclaration.duplicate];
            auto previous =
                parsedModules[firstParsedIndex].parsedModule().sourceLocFor(first.source);
            auto primary =
                parsedModules[duplicateParsedIndex].parsedModule().sourceLocFor(duplicate.source);
            auto name = identity::DeclaredDefinitionName::fromCanonical(
                duplicate.authority.record().name());
            if (previous == zc::none || primary == zc::none || name == zc::none) {
              failed = true;
              break;
            }
            ZC_IF_SOME(previousValue, previous) {
              ZC_IF_SOME(primaryValue, primary) {
                ZC_IF_SOME(nameValue, name) {
                  if (!binder::BindingDiagnosticAdapter::emitRedeclaration(
                          *diagnosticEngine, redeclaration.diagnostic, primaryValue, previousValue,
                          binder::VerifiedIdentifierArgument::from(nameValue))) {
                    failed = true;
                    break;
                  }
                }
              }
            }
          }
          if (!redeclarations.empty()) { failed = true; }
        }
      }

      if (!failed) {
        for (const auto& binding : candidateBindings) {
          for (const auto& candidate : binding.inventory.candidates()) {
            if (candidate.kind() == binder::PreAdmissionIdentityKind::Definition) {
              auto record = candidate.definitionRecord();
              if (record == zc::none) {
                failed = true;
                break;
              }
              zc::Maybe<identity::OverloadHeaderAuthority> overload;
              ZC_IF_SOME(value, candidate.overloadHeader()) { overload = value.clone(); }
              ZC_IF_SOME(value, record) {
                auto key = identity::DefinitionKey::compute(value);
                auto encodedRecord = value.encode();
                if (!containsDefinitionRecord(binding.definitions, key, encodedRecord.asPtr())) {
                  failed = true;
                  break;
                }
                if (registries.collectDefinition(value.clone(), zc::mv(overload),
                                                 stableTraversalOrdinal++) !=
                    identity::FrozenRegistryFailure::None) {
                  failed = true;
                  break;
                }
              }
              continue;
            }
            auto record = candidate.implRecord();
            if (record == zc::none) {
              failed = true;
              break;
            }
            ZC_IF_SOME(value, record) {
              auto key = identity::ImplKey::compute(value);
              if (!containsImplementationKey(binding.implementations, key)) {
                failed = true;
                break;
              }
              if (registries.collectImpl(value.clone(), stableTraversalOrdinal++) !=
                  identity::FrozenRegistryFailure::None) {
                failed = true;
                break;
              }
            }
          }
          if (failed) { break; }
        }
      }

      if (!failed && registries.freezeStableIdentities() != identity::FrozenRegistryFailure::None) {
        failed = true;
      }

      uint32_t genericTraversalOrdinal = 0;
      uint32_t callableTraversalOrdinal = 0;
      if (!failed) {
        for (const auto& binding : candidateBindings) {
          auto input = materializeFrozenInventoryInput(
              contextBrand, binding.module, parsedModules[binding.parsedIndex].parsedModule(),
              binding.inventory, registries, genericTraversalOrdinal, callableTraversalOrdinal);
          if (input == zc::none) {
            failed = true;
            break;
          }
          ZC_IF_SOME(value, input) {
            frozenInventoryInputs.add(
                FrozenInventoryInputBinding(binding.buffer, binding.module, zc::mv(value)));
          }
        }
      }
      if (!failed &&
          registries.freezeGenericParameters() != identity::FrozenRegistryFailure::None) {
        failed = true;
      }
      if (!failed &&
          registries.freezeCallableParameters() != identity::FrozenRegistryFailure::None) {
        failed = true;
      }
      if (!failed) {
        moduleBodyQueryBindings.reserve(candidateBindings.size());
        for (auto& binding : candidateBindings) {
          moduleBodyQueryBindings.add(ModuleBodyQueryBinding(binding.buffer, binding.module,
                                                             zc::mv(binding.bodySyntax),
                                                             zc::mv(binding.bodyProvenance)));
        }
      }
      if (failed) {
        emitIdentityFailures(registries, *diagnosticEngine);
        if (!diagnosticEngine->hasErrors()) {
          diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
        }
      }
    }
    return !failed;
  }

  bool freezeDefinitionInventoryViews() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none || frozenInventories.size() != 0 ||
        frozenInventoryInputs.size() != parsedModules.size()) {
      return false;
    }

    zc::TreeMap<zc::String, FrozenInventoryBinding> canonical;
    ZC_IF_SOME(registries, identityRegistries) {
      for (const auto& parsed : parsedModules) {
        zc::Maybe<size_t> selected;
        for (size_t index = 0; index < frozenInventoryInputs.size(); ++index) {
          const auto& binding = frozenInventoryInputs[index];
          if (binding.buffer != parsed.buffer()) { continue; }
          if (selected != zc::none) { return false; }
          selected = index;
        }
        if (selected == zc::none) { return false; }
        ZC_IF_SOME(inputIndex, selected) {
          auto& inputBinding = frozenInventoryInputs[inputIndex];
          const auto module = inputBinding.module;
          auto moduleKey = registries.modules().lookup(module);
          if (moduleKey == zc::none) { return false; }
          auto result = binder::FrozenDefinitionInventoryVerifier::verifySingleModule(
              contextBrand, module, parsed.parsedModule(), registries, zc::mv(inputBinding.input));
          if (!result.is<binder::FrozenDefinitionInventoryView>()) {
            diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
                source::SourceLoc(),
                zc::str(result.get<binder::FrozenInventoryInvariantFact>().occurrence));
            return false;
          }
          ZC_IF_SOME(key, moduleKey) {
            auto sortKey = zc::encodeHex(key.encode().asPtr());
            if (canonical.find(sortKey) != zc::none) { return false; }
            canonical.insert(zc::mv(sortKey),
                             FrozenInventoryBinding(
                                 parsed.buffer(), module,
                                 zc::mv(result.get<binder::FrozenDefinitionInventoryView>())));
          }
        }
      }
    }
    for (auto& entry : canonical) { frozenInventories.add(zc::mv(entry.value)); }
    return frozenInventories.size() == parsedModules.size();
  }

  bool freezeModuleGraph() {
    if (packageRequest == zc::none) { return true; }
    if (identityRegistries == zc::none || crateGraph == zc::none ||
        semanticContextFingerprint == zc::none || moduleGraph != zc::none) {
      return false;
    }
    const auto rejectInvariant = [&]() {
      diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                                            zc::str(uint64_t{1}));
      return false;
    };
    ZC_IF_SOME(registries, identityRegistries) {
      if (moduleIdentities.size() == 0 || moduleIdentities.size() != registries.modules().size()) {
        return rejectInvariant();
      }

      zc::TreeMap<zc::String, size_t> canonicalBindings;
      zc::TreeMap<zc::String, size_t> canonicalCrates;
      for (size_t index = 0; index < moduleIdentities.size(); ++index) {
        const auto& binding = moduleIdentities[index];
        auto key = registries.modules().lookup(binding.identity);
        if (key == zc::none) { return rejectInvariant(); }
        ZC_IF_SOME(value, key) {
          auto moduleSortKey = zc::encodeHex(value.encode().asPtr());
          if (canonicalBindings.find(moduleSortKey) != zc::none) { return rejectInvariant(); }
          canonicalBindings.insert(zc::mv(moduleSortKey), index);

          auto crateSortKey = zc::encodeHex(value.crate().encode().asPtr());
          if (canonicalCrates.find(crateSortKey) == zc::none) {
            canonicalCrates.insert(zc::mv(crateSortKey), index);
          }
        }
      }
      if (canonicalBindings.size() != moduleIdentities.size() || canonicalCrates.size() == 0) {
        return rejectInvariant();
      }

      zc::Vector<binder::ModuleSearchRoot> searchRoots(canonicalCrates.size());
      for (const auto& entry : canonicalCrates) {
        const auto& binding = moduleIdentities[entry.value];
        auto key = registries.modules().lookup(binding.identity);
        if (key == zc::none) { return rejectInvariant(); }
        ZC_IF_SOME(value, key) {
          const auto& crate = value.crate();
          const auto& package = crate.package();
          auto root = compilationRoot(crate);
          if (root == zc::none) { return rejectInvariant(); }
          ZC_IF_SOME(rootValue, root) {
            const auto packageRelativeRoot = parentDirectory(rootValue.sourcePath());
            switch (package.source().kind()) {
              case identity::PackageSourceKind::LocalPath: {
                zc::Vector<identity::CanonicalPathSegment> workspaceSegments;
                for (const auto& segment : package.source().localPath().segments()) {
                  workspaceSegments.add(segment.clone());
                }
                for (const auto& segment : packageRelativeRoot.segments()) {
                  workspaceSegments.add(segment.clone());
                }
                searchRoots.add(binder::ModuleSearchRoot::workspace(
                    crate.clone(),
                    identity::CanonicalWorkspaceRelativePath::from(
                        package.source().localPath().leadingParents(), zc::mv(workspaceSegments))));
                break;
              }
              case identity::PackageSourceKind::Registry:
              case identity::PackageSourceKind::Vcs:
                searchRoots.add(binder::ModuleSearchRoot::package(crate.clone(), package.clone(),
                                                                  packageRelativeRoot.clone()));
                break;
            }
            ZC_IF_SOME(results, buildScriptResults) {
              for (size_t index = 0; index < results.results().size(); ++index) {
                const auto& planKey = results.planKeys()[index];
                const auto& result = results.results()[index];
                if (!samePackage(package, planKey.package())) { continue; }
                zc::Vector<identity::CanonicalPathSegment> noSegments;
                searchRoots.add(binder::ModuleSearchRoot::generated(
                    crate.clone(), result.output().producerKey(),
                    identity::CanonicalRelativePath::from(zc::mv(noSegments))));
              }
            }
          }
        }
      }

      zc::Vector<binder::ModuleGraphModule> modules(canonicalBindings.size());
      zc::Vector<binder::ParsedModuleGraphInput> parsedInputs(canonicalBindings.size());
      zc::Vector<binder::StructuralModuleCatalogEntry> catalog(canonicalBindings.size());
      zc::Vector<binder::RequesterModuleAncestryCandidate> requesterAncestry(
          canonicalBindings.size());
      for (const auto& entry : canonicalBindings) {
        const auto& binding = moduleIdentities[entry.value];
        auto key = registries.modules().lookup(binding.identity);
        if (key == zc::none) { return rejectInvariant(); }
        zc::Maybe<size_t> parsedRecordIndex;
        for (size_t index = 0; index < parsedModules.size(); ++index) {
          if (parsedModules[index].buffer() != binding.buffer) { continue; }
          if (parsedRecordIndex != zc::none) { return rejectInvariant(); }
          parsedRecordIndex = index;
        }
        if (parsedRecordIndex == zc::none) { return rejectInvariant(); }
        size_t selectedParsedRecordIndex = 0;
        ZC_IF_SOME(value, parsedRecordIndex) { selectedParsedRecordIndex = value; }
        const auto& parsedRecord = parsedModules[selectedParsedRecordIndex];

        ZC_IF_SOME(value, key) {
          modules.add(binder::ModuleGraphModule(value.clone(), binding.identity));
          parsedInputs.add(
              binder::ParsedModuleGraphInput{binding.identity, parsedRecord.parsedModule()});
          catalog.add(binder::StructuralModuleCatalogEntry(
              value.clone(), binding.identity,
              parsedModules[selectedParsedRecordIndex].parsedModule().source().clone()));

          zc::Vector<identity::ModuleKey> ancestry;
          ancestry.add(value.clone());
          zc::Vector<identity::ModulePathSegment> currentPath = cloneModulePath(value.path());
          while (currentPath.size() > 1) {
            currentPath.removeLast();
            zc::Maybe<identity::ModuleKey> parent;
            for (const auto& candidate : canonicalBindings) {
              auto candidateKey =
                  registries.modules().lookup(moduleIdentities[candidate.value].identity);
              if (candidateKey == zc::none) { return rejectInvariant(); }
              ZC_IF_SOME(candidateValue, candidateKey) {
                if (candidateValue.crate().encode().asPtr() != value.crate().encode().asPtr() ||
                    !sameModulePath(candidateValue.path(), currentPath.asPtr())) {
                  continue;
                }
                if (parent != zc::none) { return rejectInvariant(); }
                parent = candidateValue.clone();
              }
            }
            if (parent == zc::none) {
              parent = identity::ModuleKey::from(value.crate().clone(),
                                                 cloneModulePath(currentPath.asPtr()));
            }
            if (parent == zc::none) { return rejectInvariant(); }
            ZC_IF_SOME(parentValue, parent) { ancestry.add(zc::mv(parentValue)); }
          }
          requesterAncestry.add(
              binder::RequesterModuleAncestryCandidate(value.clone(), zc::mv(ancestry)));
        }
      }

      zc::Vector<binder::ModuleSourceSnapshotRevision> sourceSnapshots(
          registries.sourceSnapshots().size());
      for (const auto& snapshot : registries.sourceSnapshots()) {
        sourceSnapshots.add(binder::ModuleSourceSnapshotRevision(snapshot.source().clone(),
                                                                 snapshot.contentDigest()));
      }
      zc::Vector<binder::GeneratedModuleSourceRevision> generatedSourceRevisions;
      ZC_IF_SOME(results, buildScriptResults) {
        generatedSourceRevisions.reserve(results.results().size());
        for (const auto& result : results.results()) {
          generatedSourceRevisions.add(binder::GeneratedModuleSourceRevision(
              result.output().producerKey(), result.run().outputs().digest()));
        }
      }
      zc::Vector<binder::ModuleDependencyAliasRoot> dependencyAliasRoots;
      ZC_IF_SOME(resolvedCrates, crateGraph) {
        for (const auto& edge : resolvedCrates.edges()) {
          zc::Maybe<identity::ModuleKey> providerRoot;
          const auto providerBytes = edge.provider().encode();
          for (const auto& binding : moduleIdentities) {
            auto candidate = registries.modules().lookup(binding.identity);
            if (candidate == zc::none) { return rejectInvariant(); }
            ZC_IF_SOME(candidateValue, candidate) {
              if (candidateValue.crate().encode().asPtr() != providerBytes.asPtr() ||
                  candidateValue.path().size() != 1 ||
                  candidateValue.path()[0].text() != edge.provider().targetName()) {
                continue;
              }
              if (providerRoot != zc::none) { return rejectInvariant(); }
              providerRoot = candidateValue.clone();
            }
          }
          auto alias = identity::DependencyAlias::fromCanonical(edge.packageEdge().alias());
          if (alias == zc::none) { return rejectInvariant(); }
          if (providerRoot == zc::none) { return rejectInvariant(); }
          ZC_IF_SOME(rootValue, providerRoot) {
            ZC_IF_SOME(aliasValue, alias) {
              dependencyAliasRoots.add(binder::ModuleDependencyAliasRoot(
                  edge.consumer().clone(), zc::mv(aliasValue), zc::mv(rootValue)));
            }
          }
        }
      }
      auto resolverResult = binder::StructuralModuleResolver::freeze(
          contextBrand, registries,
          binder::ModuleResolutionEnvironmentRecord(
              zc::mv(searchRoots), zc::mv(sourceSnapshots), zc::mv(generatedSourceRevisions),
              zc::mv(dependencyAliasRoots), zc::mv(requesterAncestry)),
          zc::mv(catalog));
      if (!resolverResult.is<binder::StructuralModuleResolver>()) {
        const auto& failure = resolverResult.get<binder::ModuleResolutionInvariantFact>();
        diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(failure.occurrence));
        return false;
      }
      auto resolver = zc::mv(resolverResult.get<binder::StructuralModuleResolver>());
      zc::Vector<binder::ModuleDependencyRequest> requests;
      for (const auto& parsed : parsedInputs) {
        auto derived = binder::ModuleDependencyRequestDeriver::derive(
            parsed.module, parsed.parsedModule, resolver.environmentRevision(), resolver);
        if (!derived.is<zc::Vector<binder::ModuleDependencyRequest>>()) {
          const auto& failure = derived.get<binder::ModuleResolutionInvariantFact>();
          diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(failure.occurrence));
          return false;
        }
        auto derivedRequests = zc::mv(derived.get<zc::Vector<binder::ModuleDependencyRequest>>());
        for (auto& request : derivedRequests) { requests.add(zc::mv(request)); }
      }
      auto pendingResolutionInputs = activeDefinitionAuthority.beginBaseMutation(queryDatabase);
      if (pendingResolutionInputs == zc::none) { return rejectInvariant(); }
      ZC_IF_SOME(transaction, pendingResolutionInputs) {
        if (!incremental_module_resolution_query::stageModuleResolutionQueryInputs(
                transaction, resolver, requests.asPtr()) ||
            transaction.commit() == zc::none) {
          return rejectInvariant();
        }
      }
      auto resolutionSnapshot = queryDatabase.snapshot();
      zc::Vector<binder::ModulePathResolution> resolutions(requests.size());
      for (auto& request : requests) {
        auto candidates =
            resolutionSnapshot.get<incremental_module_resolution_query::ResolveModuleRequestQuery>(
                request.key());
        if (candidates.isRuntimeFailure() || candidates.kind() != query::QueryValueKind::Value) {
          return rejectInvariant();
        }
        auto resolved = resolver.materializeQueryResolution(zc::mv(request), candidates.value());
        if (!resolved.is<binder::ModulePathResolution>()) {
          const auto& failure = resolved.get<binder::ModuleResolutionInvariantFact>();
          diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(failure.occurrence));
          return false;
        }
        resolutions.add(zc::mv(resolved.get<binder::ModulePathResolution>()));
      }
      const zc::ArrayPtr<const binder::ModuleDependencyRequest> noPreludes;
      ZC_IF_SOME(graph, crateGraph) {
        ZC_IF_SOME(fingerprint, semanticContextFingerprint) {
          auto result = binder::ModuleGraphVerifier::verify(binder::ModuleGraphCandidate{
              contextBrand, fingerprint, registries, graph.packageEdges(), graph.edges(), resolver,
              modules.asPtr(), parsedInputs.asPtr(), noPreludes, resolutions.asPtr()});
          if (result.is<binder::VerifiedModuleGraph>()) {
            moduleGraph = zc::mv(result.get<binder::VerifiedModuleGraph>());
            return true;
          }
          if (result.is<binder::ModuleGraphSourceRejected>()) {
            const auto& rejected = result.get<binder::ModuleGraphSourceRejected>();
            const auto parsedFor = [&](identity::ModuleId requester)
                -> zc::Maybe<const binder::VerifiedParsedModule&> {
              zc::Maybe<const binder::VerifiedParsedModule&> found;
              for (const auto& parsed : parsedInputs) {
                if (parsed.module != requester) { continue; }
                if (found != zc::none) { return zc::none; }
                found = parsed.parsedModule;
              }
              return found;
            };
            for (const auto& failure : rejected.failures()) {
              auto parsed = parsedFor(failure.request().requester());
              if (parsed == zc::none) { return rejectInvariant(); }
              ZC_IF_SOME(parsedValue, parsed) {
                if (!binder::canEmitModuleGraphSourceFailure(parsedValue, failure)) {
                  return rejectInvariant();
                }
              }
            }
            for (const auto& failure : rejected.failures()) {
              auto parsed = parsedFor(failure.request().requester());
              if (parsed == zc::none) { return rejectInvariant(); }
              ZC_IF_SOME(parsedValue, parsed) {
                if (!binder::emitModuleGraphSourceFailure(*diagnosticEngine, parsedValue,
                                                          failure)) {
                  return rejectInvariant();
                }
              }
            }
            return false;
          }
          binder::emitModuleGraphInvariant(*diagnosticEngine,
                                           result.get<binder::ModuleGraphInvariantFact>());
        }
      }
    }
    return false;
  }
};

// ================================================================================
// CompilerSession

ParsedModuleRecord::ParsedModuleRecord(const source::BufferId& buffer,
                                       binder::VerifiedParsedModule&& parsedModule) noexcept
    : bufferValue(buffer), parsedModuleValue(zc::mv(parsedModule)) {}

const source::BufferId& ParsedModuleRecord::buffer() const noexcept { return bufferValue; }

const binder::VerifiedParsedModule& ParsedModuleRecord::parsedModule() const noexcept {
  return parsedModuleValue;
}

CompilerSession::CompilerSession(identity::SemanticContextFactory& contextFactory,
                                 const basic::LangOptions& langOpts,
                                 const basic::CompilerOptions& compilerOpts)
    : impl(zc::heap<Impl>(contextFactory, langOpts, compilerOpts)) {}
CompilerSession::~CompilerSession() noexcept(false) = default;

zc::Maybe<source::BufferId> CompilerSession::addVerifiedPackageRoot(
    const package::FinalizedCompilationRoot& root) {
  if (impl->packageRequest == zc::none || impl->identityRegistries == zc::none ||
      impl->crateGraph == zc::none) {
    return zc::none;
  }
  bool admittedRoot = false;
  ZC_IF_SOME(graph, impl->crateGraph) {
    for (const auto& candidate : graph.roots()) {
      if (!sameBytes(candidate.packageKey().encode().asPtr(), root.packageKey().encode().asPtr()) ||
          !sameBytes(candidate.crateKey().encode().asPtr(), root.crateKey().encode().asPtr()) ||
          !sameRelativePath(candidate.sourcePath(), root.sourcePath())) {
        continue;
      }
      if (admittedRoot) { return zc::none; }
      admittedRoot = true;
    }
  }
  if (!admittedRoot) { return zc::none; }

  auto rootModule = identity::ModulePathSegment::fromCanonical(root.crateKey().targetName());
  if (rootModule == zc::none) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> selectedPath;
  ZC_IF_SOME(segment, rootModule) { selectedPath.add(zc::mv(segment)); }
  bool added = false;
  auto registered =
      impl->registerVerifiedSource(root.crateKey(), root.sourcePath(), selectedPath.asPtr(), added);
  if (!added) { return zc::none; }
  return registered;
}

zc::ArrayPtr<const ParsedModuleRecord> CompilerSession::getParsedModules() const noexcept {
  return impl->parsedModules.asPtr();
}

bool CompilerSession::hasVerifiedParsedSyntax() const noexcept {
  return impl->verifiedParsedSyntax;
}

zc::ArrayPtr<const binder::VerifiedBindingOutput> CompilerSession::getVerifiedBindingOutputs()
    const noexcept {
  return impl->bindingOutputs;
}

zc::ArrayPtr<const binder::VerifiedBoundModuleInput> CompilerSession::getVerifiedBoundModules()
    const noexcept {
  return impl->boundModules;
}

zc::ArrayPtr<const checker::signature::CheckerVerificationFailure>
CompilerSession::getCheckerInvariantFailures() const noexcept {
  return impl->checkerFailures;
}

zc::ArrayPtr<const checker::signature::VerifiedSignatureFacts>
CompilerSession::getVerifiedSignatureFacts() const noexcept {
  return impl->signatureFacts;
}

zc::ArrayPtr<const checker::cross_module::ImportedSignatureView>
CompilerSession::getImportedSignatureViews() const noexcept {
  return impl->importedSignatureViews;
}

zc::ArrayPtr<const VerifiedModuleInterface> CompilerSession::getVerifiedModuleInterfaces()
    const noexcept {
  return impl->moduleInterfaces;
}

zc::Maybe<const checker::coherence::FrozenCoherenceView&> CompilerSession::getFrozenCoherenceView()
    const noexcept {
  ZC_IF_SOME(view, impl->coherenceView) { return view; }
  return zc::none;
}

zc::Maybe<checker::marker::MarkerProofResult> CompilerSession::proveMarker(
    identity::ModuleId requester, identity::DefId marker, identity::SemanticTypeId subject) {
  if (impl->identityRegistries == zc::none || impl->semanticTypeStore.get() == nullptr ||
      impl->markerPolicies == zc::none || impl->coherenceView == zc::none ||
      impl->signatureFacts.size() != impl->importedSignatureViews.size()) {
    return zc::none;
  }
  size_t requesterIndex = impl->signatureFacts.size();
  for (size_t index = 0; index < impl->signatureFacts.size(); ++index) {
    if (impl->signatureFacts[index].module() == requester) {
      requesterIndex = index;
      break;
    }
  }
  if (requesterIndex == impl->signatureFacts.size()) return zc::none;
  ZC_IF_SOME(registries, impl->identityRegistries) {
    ZC_IF_SOME(policies, impl->markerPolicies) {
      ZC_IF_SOME(coherence, impl->coherenceView) {
        if (requesterIndex >= impl->boundModules.size()) return zc::none;
        const auto& bound = impl->boundModules[requesterIndex];
        auto inventoryResult = checker::body::BodyFactRequirementInventoryBuilder::build(bound);
        if (!inventoryResult.is<checker::body::VerifiedBodyFactRequirementInventory>()) {
          return zc::none;
        }
        auto inventory =
            zc::mv(inventoryResult).get<checker::body::VerifiedBodyFactRequirementInventory>();
        auto crateKey = registries.crates().lookup(bound.crate());
        if (crateKey == zc::none) return zc::none;
        ZC_IF_SOME(crate, crateKey) {
          checker::body::BodyCheckingInput bodyInput{bound,
                                                     impl->signatureFacts[requesterIndex],
                                                     impl->importedSignatureViews[requesterIndex],
                                                     coherence,
                                                     registries,
                                                     *impl->semanticTypeStore,
                                                     inventory,
                                                     crate.semanticOptions()};
          auto input = checker::marker::MarkerProofInput::from(bodyInput, policies);
          if (input == zc::none) return zc::none;
          ZC_IF_SOME(value, input) {
            checker::marker::MarkerProofEngine engine(zc::mv(value));
            return engine.prove(marker, subject);
          }
        }
      }
    }
  }
  return zc::none;
}

zc::Maybe<const checker::checked::CheckedFactsRepository&>
CompilerSession::getCheckedFactsRepository() const noexcept {
  if (impl->checkedFactsRepository.get() == nullptr) { return zc::none; }
  return *impl->checkedFactsRepository;
}

zc::ArrayPtr<const checker::checked::CheckedEvidenceLease>
CompilerSession::getCheckedEvidenceLeases() const noexcept {
  return impl->checkedEvidence;
}

zc::ArrayPtr<const checker::dispatch::VerifiedDispatchFacts>
CompilerSession::getVerifiedDispatchFacts() const noexcept {
  return impl->dispatchFacts;
}

zc::Maybe<const borrow_evidence::BorrowEvidenceRepository&>
CompilerSession::getBorrowEvidenceRepository() const noexcept {
  if (impl->borrowEvidenceRepository.get() == nullptr) { return zc::none; }
  return *impl->borrowEvidenceRepository;
}

zc::ArrayPtr<const hir::VerifiedHirModule> CompilerSession::getVerifiedHirModules() const noexcept {
  return impl->hirModules;
}

zc::ArrayPtr<const mir::VerifiedBuiltMir> CompilerSession::getVerifiedBuiltMirModules()
    const noexcept {
  return impl->builtMirModules;
}

zc::ArrayPtr<const ir::IrDiagnosticGroup> CompilerSession::getIrFailureGroups() const noexcept {
  return impl->irFailureGroups;
}

zc::ArrayPtr<const identity::IdentityInvariant> CompilerSession::getIrIdentityInvariantFailures()
    const noexcept {
  return impl->irIdentityInvariantFailures;
}

const diagnostics::DiagnosticEngine& CompilerSession::getDiagnosticEngine() const {
  return *impl->diagnosticEngine;
}

diagnostics::DiagnosticEngine& CompilerSession::getDiagnosticEngine() {
  return *impl->diagnosticEngine;
}

bool CompilerSession::parseSources() {
  if (impl->diagnosticEngine->hasErrors() || !impl->parsedModules.empty()) { return false; }
  if (impl->packageRequest == zc::none) { return true; }
  if (!impl->freezePackageAndCrateIdentities()) { return false; }

  if (impl->crateGraph == zc::none || impl->pendingSourceIdentities.size() == 0) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
        source::SourceLoc(), zc::str(uint64_t{1}));
    return false;
  }
  ZC_IF_SOME(graph, impl->crateGraph) {
    for (const auto& root : graph.roots()) {
      bool found = false;
      for (const auto& source : impl->pendingSourceIdentities) {
        if (sameBytes(source.value.crate().encode().asPtr(), root.crateKey().encode().asPtr())) {
          found = true;
          break;
        }
      }
      if (!found) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
    }
  }

  zc::Vector<source::BufferId> processed;
  while (true) {
    zc::TreeMap<zc::String, source::BufferId> worklist;
    for (const auto& source : impl->pendingSourceIdentities) {
      bool alreadyProcessed = false;
      for (const auto& buffer : processed) {
        if (buffer == source.key) {
          alreadyProcessed = true;
          break;
        }
      }
      if (alreadyProcessed) { continue; }
      auto key = zc::encodeHex(source.value.encode().asPtr());
      if (worklist.find(key) != zc::none) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
      worklist.insert(zc::mv(key), source.key);
    }
    if (worklist.size() == 0) { break; }
    if (!impl->stageParseSourceInputs()) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
      return false;
    }
    auto parseSnapshot = impl->queryDatabase.snapshot();

    for (const auto& entry : worklist) {
      auto sourceKey = impl->pendingSourceIdentities.find(entry.value);
      if (sourceKey == zc::none) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
      ZC_IF_SOME(sourceValue, sourceKey) {
        auto queryKey = source_query::StableSourceQueryKey::fromVerified(sourceValue);
        if (queryKey == zc::none) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        auto parsed = parseSnapshot.get<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(queryKey));
        if (parsed.isRuntimeFailure()) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        if (parsed.kind() == query::QueryValueKind::SemanticFailure) {
          auto rejected = parser::ParseRejected::decodeCanonical(parsed.semanticFailureBytes());
          if (rejected == zc::none) {
            impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
                source::SourceLoc(), zc::str(uint64_t{1}));
            return false;
          }
          diagnostics::materializeDiagnosticFacts(ZC_ASSERT_NONNULL(rejected).facts(),
                                                  *impl->sourceManager, entry.value,
                                                  *impl->diagnosticEngine);
          return false;
        }
        if (parsed.kind() != query::QueryValueKind::Value) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        auto requests = extractStructuralModuleDependencyRequests(parsed.value().tree());
        if (!requests.is<zc::Vector<StructuralModuleDependencyRequest>>()) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        bool addedAny = false;
        if (!impl->discoverDependencies(
                entry.value, requests.get<zc::Vector<StructuralModuleDependencyRequest>>(),
                addedAny)) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        processed.add(entry.value);
      }
    }
  }

  if (processed.size() != impl->pendingSourceIdentities.size() || !impl->freezeSourceIdentities() ||
      impl->identityRegistries == zc::none) {
    if (!impl->diagnosticEngine->hasErrors()) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
    }
    return false;
  }

  zc::TreeMap<zc::String, source::BufferId> canonicalOrder;
  for (const auto& candidate : impl->pendingSourceIdentities) {
    auto key = zc::encodeHex(candidate.value.encode().asPtr());
    if (canonicalOrder.find(key) != zc::none) { return false; }
    canonicalOrder.insert(zc::mv(key), candidate.key);
  }
  auto parseSnapshot = impl->queryDatabase.snapshot();
  ZC_IF_SOME(registries, impl->identityRegistries) {
    for (const auto& entry : canonicalOrder) {
      auto sourceKey = impl->pendingSourceIdentities.find(entry.value);
      if (sourceKey == zc::none) { return false; }
      auto queryKey =
          source_query::StableSourceQueryKey::fromVerified(ZC_ASSERT_NONNULL(sourceKey));
      if (queryKey == zc::none) { return false; }
      auto parsed = parseSnapshot.get<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(queryKey));
      if (parsed.isRuntimeFailure() || parsed.kind() != query::QueryValueKind::Value) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
      diagnostics::materializeDiagnosticFacts(parsed.value().facts(), *impl->sourceManager,
                                              entry.value, *impl->diagnosticEngine);
      auto verified = binder::ParsedModuleVerifier::verifyQueryResult(
          impl->contextBrand, registries, ZC_ASSERT_NONNULL(sourceKey), *impl->sourceManager,
          entry.value, parsed.value().clone());
      if (!verified.is<binder::VerifiedParsedModule>()) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
      impl->parsedModules.add(
          ParsedModuleRecord(entry.value, zc::mv(verified.get<binder::VerifiedParsedModule>())));
    }
  }
  impl->verifiedParsedSyntax = true;

  if (impl->diagnosticEngine->hasErrors() || !impl->freezeModuleIdentities() ||
      !impl->stageSelectedModuleSourceInputs(ZC_ASSERT_NONNULL(impl->identityRegistries)) ||
      !impl->freezeSemanticContextFingerprint() || !impl->freezeDefinitionAndImplIdentities() ||
      !impl->freezeDefinitionInventoryViews() || !impl->freezeModuleGraph()) {
    return false;
  }
  return !impl->diagnosticEngine->hasErrors();
}

bool CompilerSession::bindSources() {
  if (impl->diagnosticEngine->hasErrors() || !impl->bindingInputs.empty() ||
      !impl->bindingOutputs.empty() || !impl->boundModules.empty()) {
    return false;
  }
  if (impl->packageRequest == zc::none) { return true; }
  if (impl->identityRegistries == zc::none || impl->moduleGraph == zc::none ||
      impl->parsedModules.empty() || impl->frozenInventories.size() != impl->parsedModules.size()) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
        source::SourceLoc(), zc::str(uint64_t{1}));
    return false;
  }

  ZC_IF_SOME(registries, impl->identityRegistries) {
    ZC_IF_SOME(graph, impl->moduleGraph) {
      namespace incremental = incremental_binding_query;

      if (!impl->stageModuleTopologyInputs(graph, registries)) {
        binder::emitModuleGraphInvariant(
            *impl->diagnosticEngine,
            binder::ModuleGraphInvariantFact{binder::ModuleGraphInvariantKind::InvalidEdge,
                                             zc::none, zc::Vector<uint32_t>(), 1});
        return false;
      }
      if (impl->stagedPackageRoots == zc::none) {
        binder::emitModuleGraphInvariant(
            *impl->diagnosticEngine,
            binder::ModuleGraphInvariantFact{binder::ModuleGraphInvariantKind::InvalidEdge,
                                             zc::none, zc::Vector<uint32_t>(), 1});
        return false;
      }
      auto snapshot = impl->queryDatabase.snapshot();
      auto bindingOrder = snapshot.get<incremental::ModuleBindingOrderQuery>(
          ZC_ASSERT_NONNULL(impl->stagedPackageRoots));
      if (bindingOrder.isRuntimeFailure() || bindingOrder.kind() != query::QueryValueKind::Value ||
          bindingOrder.value().modules().size() != graph.modules().size()) {
        binder::emitModuleGraphInvariant(
            *impl->diagnosticEngine,
            binder::ModuleGraphInvariantFact{binder::ModuleGraphInvariantKind::InvalidEdge,
                                             zc::none, zc::Vector<uint32_t>(), 1});
        return false;
      }
      if (!impl->activeDefinitionAuthority.refresh(impl->queryDatabase,
                                                   ZC_ASSERT_NONNULL(impl->stagedPackageRoots)) ||
          !impl->demandNamedItemQueries()) {
        binder::emitModuleGraphInvariant(
            *impl->diagnosticEngine,
            binder::ModuleGraphInvariantFact{binder::ModuleGraphInvariantKind::InvalidEdge,
                                             zc::none, zc::Vector<uint32_t>(), 1});
        return false;
      }

      impl->bindingInputs.reserve(graph.modules().size());
      impl->bindingOutputs.reserve(graph.modules().size());
      impl->boundModules.reserve(graph.modules().size());
      zc::TreeMap<zc::String, identity::ModuleId> modulesByKey;
      for (const auto& moduleKey : graph.modules()) {
        auto module = registries.modules().find(moduleKey);
        if (module == zc::none) { return false; }
        auto encoded = zc::encodeHex(moduleKey.encode().asPtr());
        if (modulesByKey.find(encoded) != zc::none) { return false; }
        ZC_IF_SOME(value, module) { modulesByKey.insert(zc::mv(encoded), value); }
      }

      zc::TreeMap<zc::String, zc::Vector<size_t>> dependencyEdgesByRequester;
      for (size_t edgeIndex = 0; edgeIndex < graph.edges().size(); ++edgeIndex) {
        auto requesterKey =
            registries.modules().lookup(graph.edges()[edgeIndex].request().requester());
        if (requesterKey == zc::none) { return false; }
        ZC_IF_SOME(key, requesterKey) {
          auto encoded = zc::encodeHex(key.encode().asPtr());
          ZC_IF_SOME(indices, dependencyEdgesByRequester.find(encoded)) {
            indices.add(edgeIndex);
          } else {
            zc::Vector<size_t> indices;
            indices.add(edgeIndex);
            dependencyEdgesByRequester.insert(zc::mv(encoded), zc::mv(indices));
          }
        }
      }

      zc::HashMap<source::BufferId, size_t> parsedByBuffer;
      for (size_t index = 0; index < impl->parsedModules.size(); ++index) {
        const auto buffer = impl->parsedModules[index].buffer();
        if (parsedByBuffer.find(buffer) != zc::none) { return false; }
        parsedByBuffer.insert(buffer, index);
      }
      zc::TreeMap<zc::String, size_t> parsedByModule;
      for (const auto& binding : impl->moduleIdentities) {
        auto parsedIndex = parsedByBuffer.find(binding.buffer);
        auto moduleKey = registries.modules().lookup(binding.identity);
        if (parsedIndex == zc::none || moduleKey == zc::none) { return false; }
        ZC_IF_SOME(key, moduleKey) {
          auto encoded = zc::encodeHex(key.encode().asPtr());
          if (parsedByModule.find(encoded) != zc::none) { return false; }
          ZC_IF_SOME(index, parsedIndex) { parsedByModule.insert(zc::mv(encoded), index); }
        }
      }
      zc::TreeMap<zc::String, size_t> inventoryByModule;
      for (size_t index = 0; index < impl->frozenInventories.size(); ++index) {
        auto moduleKey = registries.modules().lookup(impl->frozenInventories[index].module);
        if (moduleKey == zc::none) { return false; }
        ZC_IF_SOME(key, moduleKey) {
          auto encoded = zc::encodeHex(key.encode().asPtr());
          if (inventoryByModule.find(encoded) != zc::none) { return false; }
          inventoryByModule.insert(zc::mv(encoded), index);
        }
      }

      zc::TreeMap<zc::String, size_t> bindingOutputsByModule;
      for (const auto& stableModule : bindingOrder.value().modules()) {
        auto encodedModule = zc::encodeHex(stableModule.canonicalModuleBytes());
        auto selected = modulesByKey.find(encodedModule);
        if (selected == zc::none) { return false; }
        ZC_IF_SOME(module, selected) {
          auto moduleKey = registries.modules().lookup(module);
          if (moduleKey == zc::none) { return false; }
          zc::Maybe<const ParsedModuleRecord&> parsed;
          zc::Maybe<const Impl::FrozenInventoryBinding&> inventory;
          ZC_IF_SOME(index, parsedByModule.find(encodedModule)) {
            if (index >= impl->parsedModules.size()) { return false; }
            parsed = impl->parsedModules[index];
          }
          ZC_IF_SOME(index, inventoryByModule.find(encodedModule)) {
            if (index >= impl->frozenInventories.size()) { return false; }
            inventory = impl->frozenInventories[index];
          }
          if (parsed == zc::none || inventory == zc::none) { return false; }

          auto graphView = graph.view(module);
          if (graphView == zc::none) { return false; }
          zc::TreeMap<zc::String, size_t> dependencyOutputIndices;
          zc::Maybe<size_t> preludeOutputIndex;
          ZC_IF_SOME(edgeIndices, dependencyEdgesByRequester.find(encodedModule)) {
            for (const auto edgeIndex : edgeIndices) {
              if (edgeIndex >= graph.edges().size()) { return false; }
              const auto& edge = graph.edges()[edgeIndex];
              auto targetKey = registries.modules().lookup(edge.target());
              if (targetKey == zc::none) { return false; }
              ZC_IF_SOME(value, targetKey) {
                auto encodedTarget = zc::encodeHex(value.encode().asPtr());
                auto outputIndex = bindingOutputsByModule.find(encodedTarget);
                if (outputIndex == zc::none) { return false; }
                ZC_IF_SOME(index, outputIndex) {
                  if (edge.request().kind() == identity::ModuleDependencyKind::Prelude) {
                    if (preludeOutputIndex != zc::none) { return false; }
                    preludeOutputIndex = index;
                  } else if (dependencyOutputIndices.find(encodedTarget) == zc::none) {
                    dependencyOutputIndices.insert(zc::mv(encodedTarget), index);
                  }
                }
              }
            }
          }

          zc::Vector<binder::DependencyExportSurface> dependencySurfaces(
              dependencyOutputIndices.size());
          for (const auto& entry : dependencyOutputIndices) {
            const auto& surface = impl->bindingOutputs[entry.value].surface;
            dependencySurfaces.add(
                binder::DependencyExportSurface{surface.sourceModule(), surface});
          }
          zc::Maybe<const binder::VerifiedExportSurface&> preludeSurface;
          ZC_IF_SOME(index, preludeOutputIndex) {
            preludeSurface = impl->bindingOutputs[index].surface;
          }

          ZC_IF_SOME(key, moduleKey) {
            auto package = registries.packages().find(key.crate().package());
            auto crate = registries.crates().find(key.crate());
            if (package == zc::none || crate == zc::none) { return false; }
            ZC_IF_SOME(packageValue, package) {
              ZC_IF_SOME(crateValue, crate) {
                ZC_IF_SOME(view, graphView) {
                  ZC_IF_SOME(parsedValue, parsed) {
                    ZC_IF_SOME(inventoryValue, inventory) {
                      auto input =
                          binder::BindingInputVerifier::verify(binder::BindingInputCandidate{
                              impl->contextBrand, packageValue, crateValue, module, registries,
                              view, parsedValue.parsedModule(), inventoryValue.view,
                              dependencySurfaces.asPtr(), preludeSurface});
                      if (input.is<binder::BindingInputSourceRejected>()) {
                        for (const auto& failure :
                             input.get<binder::BindingInputSourceRejected>().failures()) {
                          if (!binder::emitBindingInputSourceFailure(
                                  *impl->diagnosticEngine, parsedValue.parsedModule(), failure)) {
                            return false;
                          }
                        }
                        return false;
                      }
                      if (input.is<binder::ModuleGraphInvariantFact>()) {
                        binder::emitModuleGraphInvariant(
                            *impl->diagnosticEngine, input.get<binder::ModuleGraphInvariantFact>());
                        return false;
                      }

                      auto result = binder::runBinding(input.get<binder::VerifiedBindingInput>(),
                                                       *impl->diagnosticEngine);
                      if (result.is<binder::VerifiedBindingOutput>()) {
                        impl->bindingInputs.add(zc::mv(input.get<binder::VerifiedBindingInput>()));
                        impl->bindingOutputs.add(
                            zc::mv(result.get<binder::VerifiedBindingOutput>()));
                        auto bound = binder::VerifiedBoundModuleInput::from(
                            impl->bindingInputs.back(), impl->bindingOutputs.back());
                        if (bound == zc::none ||
                            bindingOutputsByModule.find(encodedModule) != zc::none) {
                          return false;
                        }
                        ZC_IF_SOME(value, bound) { impl->boundModules.add(zc::mv(value)); }
                        bindingOutputsByModule.insert(zc::mv(encodedModule),
                                                      impl->bindingOutputs.size() - 1);
                        continue;
                      }
                      if (result.is<binder::InvariantRejected>()) {
                        identity::IdentityInvariantCollector identityFailures;
                        for (const auto& failure :
                             result.get<binder::InvariantRejected>().failures()) {
                          if (failure.value.is<binder::BinderInvariantFact>()) {
                            binder::emitBinderInvariant(
                                *impl->diagnosticEngine,
                                failure.value.get<binder::BinderInvariantFact>());
                          } else {
                            identityFailures.add(
                                failure.value.get<identity::IdentityInvariant>().clone());
                          }
                        }
                        identityFailures.sort();
                        auto groups = identity::groupIdentityInvariants(identityFailures.facts());
                        identity::emitIdentityDiagnosticGroups(*impl->diagnosticEngine,
                                                               groups.asPtr());
                      }
                      return false;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return !impl->diagnosticEngine->hasErrors();
}

bool CompilerSession::checkSources() {
  if (impl->diagnosticEngine->hasErrors() || !impl->checkerFailures.empty() ||
      !impl->irFailureGroups.empty() || !impl->irIdentityInvariantFailures.empty()) {
    return false;
  }
  if (impl->packageRequest == zc::none) { return true; }
  if (impl->identityRegistries == zc::none || impl->semanticContextFingerprint == zc::none ||
      impl->moduleGraph == zc::none || impl->semanticTypeStore.get() == nullptr ||
      impl->factStoreBrands == zc::none ||
      impl->bindingInputs.size() != impl->bindingOutputs.size() ||
      impl->bindingOutputs.size() != impl->parsedModules.size() ||
      impl->boundModules.size() != impl->bindingOutputs.size()) {
    return false;
  }
  if (impl->bindingOutputs.empty()) { return true; }
  if (impl->verifiedCheckedSources) { return true; }
  if (!impl->signatureFacts.empty() || !impl->importedSignatureViews.empty() ||
      !impl->moduleInterfaces.empty() || impl->coherenceView != zc::none ||
      impl->checkedFactsRepository.get() != nullptr || !impl->checkedEvidence.empty() ||
      !impl->dispatchFacts.empty() || impl->borrowEvidenceRepository.get() != nullptr ||
      !impl->hirModules.empty() || !impl->builtMirModules.empty()) {
    return false;
  }

  const auto parsedFor =
      [&](identity::ModuleId module) -> zc::Maybe<const binder::VerifiedParsedModule&> {
    for (const auto& bound : impl->boundModules) {
      if (bound.module() == module) { return bound.parsedModule(); }
    }
    return zc::none;
  };
  const auto locationFor = [&](const binder::VerifiedParsedModule& parsed,
                               const identity::SourceSpan& span) {
    ZC_IF_SOME(location, parsed.sourceLocFor(span)) { return location; }
    return source::SourceLoc();
  };
  const auto rejectChecker =
      [&](identity::ModuleId module,
          zc::Vector<checker::signature::CheckerVerificationFailure>&& failures) {
        for (auto& failure : failures) { impl->checkerFailures.add(zc::mv(failure)); }
        ZC_IF_SOME(parsed, parsedFor(module)) {
          checker::emitCheckerVerificationFailures(*impl->diagnosticEngine, parsed,
                                                   impl->checkerFailures.asPtr());
        }
        return false;
      };
  const auto rejectOne = [&](identity::ModuleId module,
                             checker::signature::CheckerInvariantKind kind,
                             checker::signature::CheckerInvariantStage stage, uint32_t ordinal) {
    zc::Vector<checker::signature::CheckerVerificationFailure> failures;
    failures.add(
        checker::signature::CheckerVerificationFailure(checker::signature::CheckerInvariantFact{
            kind, stage, module, zc::none, zc::none, zc::none, zc::Vector<uint32_t>(), zc::none,
            zc::none, ordinal}));
    return rejectChecker(module, zc::mv(failures));
  };
  const auto rejectDispatch = [&](identity::ModuleId module,
                                  checker::dispatch::DispatchFactsInvariantRejected&& rejected) {
    ZC_IF_SOME(parsed, parsedFor(module)) {
      checker::emitDispatchVerificationFailures(*impl->diagnosticEngine, parsed,
                                                rejected.failures.asPtr());
    }
    return false;
  };
  const auto emitSignatureSource = [&](const binder::VerifiedParsedModule& parsed,
                                       const checker::signature::SignatureFactsSourceRejected&
                                           rejected,
                                       const identity::SemanticIdentityRegistrySet& registries) {
    for (const auto& failure : rejected.failures) {
      const auto id = static_cast<diagnostics::DiagID>(failure.diagnostic);
      const auto location = locationFor(parsed, failure.primarySpan);
      if (failure.diagnostic ==
          checker::signature::SignatureSourceDiagnostic::BodyLiteralOutOfRange) {
        if (failure.arguments.size() != 2 ||
            !failure.arguments[0].variant().is<checker::signature::SignatureLiteralDisplayArg>() ||
            !failure.arguments[1]
                 .variant()
                 .is<checker::signature::SignaturePrimitiveTypeDisplayArg>()) {
          return false;
        }
        auto literal = checker::checked::CheckerDisplayArgument(checker::checked::LiteralDisplayArg{
            failure.arguments[0]
                .variant()
                .get<checker::signature::SignatureLiteralDisplayArg>()
                .literal.clone()});
        auto primitive =
            checker::checked::CheckerDisplayArgument(checker::checked::PrimitiveTypeDisplayArg{
                failure.arguments[1]
                    .variant()
                    .get<checker::signature::SignaturePrimitiveTypeDisplayArg>()
                    .kind});
        impl->diagnosticEngine->emit(diagnostics::Diagnostic(
            id, location,
            checker::renderCheckerDisplayArgument(literal, registries, *impl->semanticTypeStore),
            checker::renderCheckerDisplayArgument(primitive, registries,
                                                  *impl->semanticTypeStore)));
      } else if (failure.diagnostic ==
                     checker::signature::SignatureSourceDiagnostic::ConflictingImpl ||
                 failure.diagnostic == checker::signature::SignatureSourceDiagnostic::OrphanImpl) {
        impl->diagnosticEngine->emit(
            diagnostics::Diagnostic(id, location, "interface"_zc, "type"_zc));
      } else {
        impl->diagnosticEngine->emit(diagnostics::Diagnostic(id, location));
      }
    }
    for (const auto& advisory : rejected.advisories) {
      impl->diagnosticEngine->emit(
          diagnostics::Diagnostic(advisory.diagnostic, locationFor(parsed, advisory.primarySpan)));
    }
    return true;
  };
  const auto rejectIrIdentity = [&](const ir::SortedIdentityInvariantFacts& failures) {
    ir::emitIrIdentityInvariantFailures(*impl->diagnosticEngine, failures);
    for (const auto& failure : failures.facts()) {
      impl->irIdentityInvariantFailures.add(failure.clone());
    }
    return false;
  };
  const auto rejectIrCapability = [&](const ir::SortedCapabilityFailureFacts& failures) {
    auto groups = ir::groupIrCapabilityFailures(failures);
    ir::emitIrDiagnosticGroups(*impl->diagnosticEngine, groups.asPtr());
    impl->irFailureGroups = zc::mv(groups);
    return false;
  };
  const auto rejectIrInvariant = [&](const ir::SortedIrInvariantFailureFacts& failures) {
    auto groups = ir::groupIrInvariantFailures(failures);
    ir::emitIrDiagnosticGroups(*impl->diagnosticEngine, groups.asPtr());
    impl->irFailureGroups = zc::mv(groups);
    return false;
  };

  zc::Maybe<checker::signature::VerifiedMarkerShapeInventory> stagedMarkerShapes;
  zc::Maybe<checker::signature::VerifiedMarkerPolicyRegistry> stagedMarkerPolicies;
  zc::Vector<checker::signature::VerifiedSignatureFacts> stagedSignatureFacts;
  zc::Vector<checker::cross_module::ImportedSignatureView> stagedImportedSignatureViews;
  zc::Vector<VerifiedModuleInterface> stagedModuleInterfaces;
  zc::Maybe<checker::coherence::FrozenCoherenceView> stagedCoherenceView;
  auto stagedCheckedFactsRepository =
      zc::heap<checker::checked::CheckedFactsRepository>(impl->contextBrand);
  zc::Vector<checker::checked::CheckedEvidenceLease> stagedCheckedEvidence;
  zc::Vector<checker::dispatch::VerifiedDispatchFacts> stagedDispatchFacts;
  zc::Own<borrow_evidence::BorrowEvidenceRepository> stagedBorrowEvidenceRepository;
  zc::Vector<hir::VerifiedHirModule> stagedHirModules;
  zc::Vector<mir::VerifiedBuiltMir> stagedBuiltMirModules;

  ZC_IF_SOME(registries, impl->identityRegistries) {
    ZC_IF_SOME(fingerprint, impl->semanticContextFingerprint) {
      zc::Vector<checker::signature::MarkerShapeModuleInput> markerInputs(
          impl->boundModules.size());
      for (const auto& bound : impl->boundModules) {
        markerInputs.add(checker::signature::MarkerShapeModuleInput{bound});
      }
      auto shapeResult = checker::signature::MarkerShapeInventoryBuilder::build(
          impl->contextBrand, fingerprint, markerInputs.asPtr(), registries);
      if (!shapeResult.is<checker::signature::VerifiedMarkerShapeInventory>()) {
        auto rejected =
            zc::mv(shapeResult).get<checker::signature::SignatureFactsInvariantRejected>();
        return rejectChecker(impl->boundModules[0].module(), zc::mv(rejected.failures));
      }
      stagedMarkerShapes =
          zc::mv(shapeResult).get<checker::signature::VerifiedMarkerShapeInventory>();

      zc::Vector<identity::ModuleId> authorizedPreludeModules;
      ZC_IF_SOME(graph, impl->moduleGraph) {
        for (const auto& edge : graph.edges()) {
          if (edge.request().kind() != identity::ModuleDependencyKind::Prelude) { continue; }
          bool duplicate = false;
          for (const auto module : authorizedPreludeModules) {
            if (module == edge.target()) {
              duplicate = true;
              break;
            }
          }
          if (!duplicate) { authorizedPreludeModules.add(edge.target()); }
        }
      }
      auto markerConfiguration = checker::signature::MarkerPolicyConfiguration::explicitOnly();
      ZC_IF_SOME(shapes, stagedMarkerShapes) {
        auto policyResult = checker::signature::MarkerPolicyRegistryBuilder::build(
            markerConfiguration, shapes, authorizedPreludeModules.asPtr(), registries);
        if (!policyResult.is<checker::signature::VerifiedMarkerPolicyRegistry>()) {
          auto rejected =
              zc::mv(policyResult).get<checker::signature::SignatureFactsInvariantRejected>();
          return rejectChecker(impl->boundModules[0].module(), zc::mv(rejected.failures));
        }
        stagedMarkerPolicies =
            zc::mv(policyResult).get<checker::signature::VerifiedMarkerPolicyRegistry>();
      }

      for (const auto& bound : impl->boundModules) {
        ZC_IF_SOME(shapes, stagedMarkerShapes) {
          ZC_IF_SOME(policies, stagedMarkerPolicies) {
            auto signatureResult = checker::signature::SignatureFactsBuilder::build(
                checker::signature::SignatureFactsBuildInput{
                    bound, registries, *impl->semanticTypeStore, shapes, policies});
            if (signatureResult.is<checker::signature::SignatureFactsSourceRejected>()) {
              if (!emitSignatureSource(
                      bound.parsedModule(),
                      signatureResult.get<checker::signature::SignatureFactsSourceRejected>(),
                      registries)) {
                return rejectOne(bound.module(),
                                 checker::signature::CheckerInvariantKind::InvalidFact,
                                 checker::signature::CheckerInvariantStage::Signature, 0);
              }
              return false;
            }
            if (signatureResult.is<checker::signature::SignatureFactsInvariantRejected>()) {
              auto rejected = zc::mv(signatureResult)
                                  .get<checker::signature::SignatureFactsInvariantRejected>();
              return rejectChecker(bound.module(), zc::mv(rejected.failures));
            }
            stagedSignatureFacts.add(
                zc::mv(signatureResult).get<checker::signature::VerifiedSignatureFacts>());

            auto imported = ImportedSignatureViewProjector::build(
                bound, stagedModuleInterfaces.asPtr(), registries, *impl->semanticTypeStore);
            if (imported == zc::none) {
              return rejectOne(bound.module(),
                               checker::signature::CheckerInvariantKind::ViewMismatch,
                               checker::signature::CheckerInvariantStage::Signature, 0);
            }
            ZC_IF_SOME(view, imported) { stagedImportedSignatureViews.add(zc::mv(view)); }
            const auto& signatures = stagedSignatureFacts.back();
            const auto& importedView = stagedImportedSignatureViews.back();
            auto borrowResult = checker::borrow::BorrowInterfaceBuilder::build(
                checker::borrow::BorrowInterfaceBuildInput{
                    impl->contextBrand, fingerprint, bound.module(), signatures.revision(),
                    importedView.revision(), signatures.signatures(),
                    zc::ArrayPtr<const checker::signature::SemanticSignature>(), registries,
                    *impl->semanticTypeStore});
            if (borrowResult.is<checker::borrow::BorrowInterfaceSourceRejected>()) {
              checker::borrow::emitBorrowSignatureFailures(
                  *impl->diagnosticEngine, bound.parsedModule(),
                  borrowResult.get<checker::borrow::BorrowInterfaceSourceRejected>()
                      .failures.asPtr());
              return false;
            }
            if (borrowResult.is<checker::borrow::BorrowInterfaceInvariantRejected>()) {
              auto rejected =
                  zc::mv(borrowResult).get<checker::borrow::BorrowInterfaceInvariantRejected>();
              return rejectChecker(bound.module(), zc::mv(rejected.failures));
            }
            auto interfaceResult = ModuleInterfaceVerifier::build(ModuleInterfaceBuildInput{
                bound, signatures, importedView, policies,
                zc::mv(borrowResult).get<checker::borrow::VerifiedBorrowInterfaceSurface>(),
                registries, *impl->semanticTypeStore});
            if (!interfaceResult.is<VerifiedModuleInterface>()) {
              auto rejected = zc::mv(interfaceResult).get<ModuleInterfaceInvariantRejected>();
              emitModuleInterfaceInvariantFacts(*impl->diagnosticEngine, bound.parsedModule(),
                                                rejected.failures.asPtr());
              return false;
            }
            stagedModuleInterfaces.add(zc::mv(interfaceResult).get<VerifiedModuleInterface>());
          }
        }
      }

      if (stagedSignatureFacts.size() != impl->boundModules.size() ||
          stagedImportedSignatureViews.size() != impl->boundModules.size() ||
          stagedModuleInterfaces.size() != impl->boundModules.size()) {
        return rejectOne(impl->boundModules[0].module(),
                         checker::signature::CheckerInvariantKind::MissingRequiredFact,
                         checker::signature::CheckerInvariantStage::Signature, 0);
      }

      const auto buildCoherence = [&]() -> checker::coherence::CoherenceBuildResult {
        ZC_IF_SOME(policies, stagedMarkerPolicies) {
          return CoherenceBuilder::build(
              CoherenceBuildInput{impl->contextBrand, fingerprint, policies,
                                  stagedModuleInterfaces.asPtr(), registries});
        }
        ZC_UNREACHABLE
      };
      auto coherenceResult = buildCoherence();
      if (coherenceResult.is<checker::coherence::CoherenceSourceRejected>()) {
        const auto& rejected = coherenceResult.get<checker::coherence::CoherenceSourceRejected>();
        for (const auto& failure : rejected.failures) {
          auto implementation = registries.impls().lookupRecord(failure.primaryImpl);
          if (implementation == zc::none) {
            return rejectOne(impl->boundModules[0].module(),
                             checker::signature::CheckerInvariantKind::InvalidFact,
                             checker::signature::CheckerInvariantStage::Coherence, 0);
          }
          ZC_IF_SOME(record, implementation) {
            auto moduleId = registries.modules().find(record.module());
            if (moduleId == zc::none) {
              return rejectOne(impl->boundModules[0].module(),
                               checker::signature::CheckerInvariantKind::InvalidFact,
                               checker::signature::CheckerInvariantStage::Coherence, 0);
            }
            ZC_IF_SOME(module, moduleId) {
              ZC_IF_SOME(parsed, parsedFor(module)) {
                checker::emitCoherenceSourceFailure(*impl->diagnosticEngine, parsed, registries,
                                                    *impl->semanticTypeStore, failure);
              }
            }
          }
        }
        return false;
      }
      if (coherenceResult.is<checker::coherence::CoherenceInvariantRejected>()) {
        auto rejected =
            zc::mv(coherenceResult).get<checker::coherence::CoherenceInvariantRejected>();
        return rejectChecker(impl->boundModules[0].module(), zc::mv(rejected.failures));
      }
      auto frozenCoherence = zc::mv(coherenceResult).get<checker::coherence::CoherenceFrozen>();
      for (const auto& advisory : frozenCoherence.advisories) {
        ZC_IF_SOME(parsed, parsedFor(impl->boundModules[0].module())) {
          impl->diagnosticEngine->emit(diagnostics::Diagnostic(
              advisory.diagnostic, locationFor(parsed, advisory.primarySpan)));
        }
      }
      stagedCoherenceView = zc::mv(frozenCoherence.view);

      ZC_IF_SOME(coherence, stagedCoherenceView) {
        ZC_IF_SOME(factStoreBrands, impl->factStoreBrands) {
          checker::body::BodyChecker bodyChecker;
          for (size_t index = 0; index < impl->boundModules.size(); ++index) {
            const auto& bound = impl->boundModules[index];
            auto inventoryResult = checker::body::BodyFactRequirementInventoryBuilder::build(bound);
            if (!inventoryResult.is<checker::body::VerifiedBodyFactRequirementInventory>()) {
              auto rejected =
                  zc::mv(inventoryResult).get<checker::checked::CheckedFactsInvariantRejected>();
              return rejectChecker(bound.module(), zc::mv(rejected.failures));
            }
            auto inventory =
                zc::mv(inventoryResult).get<checker::body::VerifiedBodyFactRequirementInventory>();
            auto dispatchInventoryResult =
                checker::dispatch::DispatchSiteInventoryBuilder::build(bound, inventory);
            if (!dispatchInventoryResult.is<checker::dispatch::VerifiedDispatchSiteInventory>()) {
              auto rejected = zc::mv(dispatchInventoryResult)
                                  .get<checker::dispatch::DispatchFactsInvariantRejected>();
              return rejectDispatch(bound.module(), zc::mv(rejected));
            }
            auto dispatchInventory = zc::mv(dispatchInventoryResult)
                                         .get<checker::dispatch::VerifiedDispatchSiteInventory>();
            auto crateKey = registries.crates().lookup(bound.crate());
            if (crateKey == zc::none) {
              return rejectOne(bound.module(),
                               checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                               checker::signature::CheckerInvariantStage::Body, 0);
            }
            ZC_IF_SOME(crate, crateKey) {
              auto bodyResult = bodyChecker.check(
                  checker::body::BodyCheckingInput{bound, stagedSignatureFacts[index],
                                                   stagedImportedSignatureViews[index], coherence,
                                                   registries, *impl->semanticTypeStore, inventory,
                                                   crate.semanticOptions()},
                  factStoreBrands);
              if (bodyResult.is<checker::checked::CheckedFactsSourceRejected>()) {
                auto rejected =
                    zc::mv(bodyResult).get<checker::checked::CheckedFactsSourceRejected>();
                zc::Vector<identity::DefId> importedDefinitions;
                for (const auto& importedModule : stagedImportedSignatureViews[index].modules()) {
                  for (const auto& definition : importedModule.lookupDefinitions()) {
                    bool duplicate = false;
                    for (const auto existing : importedDefinitions) {
                      if (existing == definition.definition) {
                        duplicate = true;
                        break;
                      }
                    }
                    if (!duplicate) { importedDefinitions.add(definition.definition); }
                  }
                }
                zc::Vector<identity::ImplId> coherentImpls(coherence.implHeads().size());
                for (const auto& implementation : coherence.implHeads()) {
                  coherentImpls.add(implementation.impl);
                }
                checker::checked::CheckedFactsVerificationInput rejectionInput{
                    impl->contextBrand,
                    fingerprint,
                    bound.module(),
                    bound.parsedModule().source(),
                    bound.parsedModule().contentDigest(),
                    bound.parsedModule().receipt(),
                    stagedSignatureFacts[index].revision(),
                    stagedImportedSignatureViews[index].revision(),
                    coherence.revision(),
                    crate.semanticOptions(),
                    inventory.nodeRequirements(),
                    inventory.definitionRequirements(),
                    inventory.captureRequirements(),
                    importedDefinitions.asPtr(),
                    coherentImpls.asPtr(),
                    rejected.failures.asPtr(),
                    bound.definitions().ownerLocalBindings(),
                    bound.definitions().anonymousEntities(),
                    registries,
                    *impl->semanticTypeStore};
                auto verifiedRejection =
                    checker::checked::CheckedFactsSourceRejectionVerifier::verify(zc::mv(rejected),
                                                                                  rejectionInput);
                if (verifiedRejection.is<checker::checked::CheckedFactsInvariantRejected>()) {
                  auto invariant = zc::mv(verifiedRejection)
                                       .get<checker::checked::CheckedFactsInvariantRejected>();
                  return rejectChecker(bound.module(), zc::mv(invariant.failures));
                }
                const auto& verified =
                    verifiedRejection.get<checker::checked::CheckedFactsSourceRejected>();
                checker::emitCheckedFactsSourceFailures(
                    *impl->diagnosticEngine, bound.parsedModule(), registries,
                    *impl->semanticTypeStore, verified.failures.asPtr());
                return false;
              }
              if (bodyResult.is<checker::checked::CheckedFactsInvariantRejected>()) {
                auto rejected =
                    zc::mv(bodyResult).get<checker::checked::CheckedFactsInvariantRejected>();
                return rejectChecker(bound.module(), zc::mv(rejected.failures));
              }
              auto candidate = zc::mv(bodyResult).get<checker::checked::CheckedFactsCandidate>();
              zc::Vector<identity::DefId> importedDefinitions;
              for (const auto& module : stagedImportedSignatureViews[index].modules()) {
                for (const auto& definition : module.lookupDefinitions()) {
                  bool duplicate = false;
                  for (const auto existing : importedDefinitions) {
                    if (existing == definition.definition) {
                      duplicate = true;
                      break;
                    }
                  }
                  if (!duplicate) { importedDefinitions.add(definition.definition); }
                }
              }
              zc::Vector<identity::ImplId> coherentImpls(coherence.implHeads().size());
              for (const auto& implementation : coherence.implHeads()) {
                coherentImpls.add(implementation.impl);
              }
              checker::checked::CheckedFactsVerificationInput verificationInput{
                  impl->contextBrand,
                  fingerprint,
                  bound.module(),
                  bound.parsedModule().source(),
                  bound.parsedModule().contentDigest(),
                  bound.parsedModule().receipt(),
                  stagedSignatureFacts[index].revision(),
                  stagedImportedSignatureViews[index].revision(),
                  coherence.revision(),
                  crate.semanticOptions(),
                  inventory.nodeRequirements(),
                  inventory.definitionRequirements(),
                  inventory.captureRequirements(),
                  importedDefinitions.asPtr(),
                  coherentImpls.asPtr(),
                  candidate.sourceFailures.asPtr(),
                  bound.definitions().ownerLocalBindings(),
                  bound.definitions().anonymousEntities(),
                  registries,
                  *impl->semanticTypeStore};
              auto verified = checker::checked::CheckedFactsVerifier::verify(zc::mv(candidate),
                                                                             verificationInput);
              if (verified.is<checker::checked::CheckedFactsSourceRejected>()) {
                const auto& rejected = verified.get<checker::checked::CheckedFactsSourceRejected>();
                checker::emitCheckedFactsSourceFailures(
                    *impl->diagnosticEngine, bound.parsedModule(), registries,
                    *impl->semanticTypeStore, rejected.failures.asPtr());
                return false;
              }
              if (verified.is<checker::checked::CheckedFactsInvariantRejected>()) {
                auto rejected =
                    zc::mv(verified).get<checker::checked::CheckedFactsInvariantRejected>();
                return rejectChecker(bound.module(), zc::mv(rejected.failures));
              }
              auto adoption = stagedCheckedFactsRepository->adopt(
                  zc::mv(verified).get<checker::checked::VerifiedCheckedFacts>());
              if (!adoption.is<checker::checked::CheckedEvidenceLease>()) {
                return rejectOne(bound.module(),
                                 checker::signature::CheckerInvariantKind::InvalidFact,
                                 checker::signature::CheckerInvariantStage::Verification, 0);
              }
              auto lease = zc::mv(adoption).get<checker::checked::CheckedEvidenceLease>();
              auto adoptedFacts = stagedCheckedFactsRepository->lookup(lease);
              if (adoptedFacts == zc::none) {
                return rejectOne(bound.module(),
                                 checker::signature::CheckerInvariantKind::InvalidFact,
                                 checker::signature::CheckerInvariantStage::Verification, 0);
              }
              ZC_IF_SOME(facts, adoptedFacts) {
                auto dispatchBuild = checker::dispatch::DispatchFactsBuilder::build(
                    dispatchInventory, fingerprint, lease, facts, registries,
                    *impl->semanticTypeStore);
                if (!dispatchBuild.is<checker::dispatch::DispatchFactsCandidate>()) {
                  auto rejected = zc::mv(dispatchBuild)
                                      .get<checker::dispatch::DispatchFactsInvariantRejected>();
                  return rejectDispatch(bound.module(), zc::mv(rejected));
                }
                auto dispatchVerification = checker::dispatch::DispatchFactsVerifier::verify(
                    zc::mv(dispatchBuild).get<checker::dispatch::DispatchFactsCandidate>(),
                    checker::dispatch::DispatchFactsVerificationInput{
                        fingerprint, bound.module(), bound.parsedModule().source(),
                        dispatchInventory.requirements(), dispatchInventory.nodeProjections(),
                        lease, facts, registries, *impl->semanticTypeStore});
                if (!dispatchVerification.is<checker::dispatch::VerifiedDispatchFacts>()) {
                  auto rejected = zc::mv(dispatchVerification)
                                      .get<checker::dispatch::DispatchFactsInvariantRejected>();
                  return rejectDispatch(bound.module(), zc::mv(rejected));
                }
                stagedDispatchFacts.add(
                    zc::mv(dispatchVerification).get<checker::dispatch::VerifiedDispatchFacts>());
              }
              stagedCheckedEvidence.add(zc::mv(lease));
            }
          }
        }
      }
    }
  }

  if (stagedCheckedEvidence.size() != impl->boundModules.size() ||
      stagedDispatchFacts.size() != impl->boundModules.size() ||
      impl->diagnosticEngine->hasErrors()) {
    return false;
  }
  if (impl->boundModules.size() > UINT32_MAX) {
    return rejectOne(impl->boundModules[0].module(),
                     checker::signature::CheckerInvariantKind::InvalidFact,
                     checker::signature::CheckerInvariantStage::Verification, 0);
  }
  ZC_IF_SOME(issuer, impl->factStoreBrands) {
    auto repositoryBrand = issuer.issue();
    if (repositoryBrand == zc::none) {
      return rejectOne(impl->boundModules[0].module(),
                       checker::signature::CheckerInvariantKind::InferenceLifecycle,
                       checker::signature::CheckerInvariantStage::Verification, 0);
    }
    ZC_IF_SOME(brand, repositoryBrand) {
      auto repository = borrow_evidence::BorrowEvidenceRepository::create(
          impl->contextBrand, brand, static_cast<uint32_t>(impl->boundModules.size()));
      if (repository == zc::none) {
        return rejectOne(impl->boundModules[0].module(),
                         checker::signature::CheckerInvariantKind::InferenceLifecycle,
                         checker::signature::CheckerInvariantStage::Verification, 0);
      }
      ZC_IF_SOME(value, repository) {
        stagedBorrowEvidenceRepository =
            zc::heap<borrow_evidence::BorrowEvidenceRepository>(zc::mv(value));
      }
    }
  }
  if (stagedBorrowEvidenceRepository.get() == nullptr) {
    return rejectOne(impl->boundModules[0].module(),
                     checker::signature::CheckerInvariantKind::InferenceLifecycle,
                     checker::signature::CheckerInvariantStage::Verification, 0);
  }
  ZC_IF_SOME(registries, impl->identityRegistries) {
    for (size_t index = 0; index < impl->boundModules.size(); ++index) {
      auto checkedModule = hir::CheckedModuleBuilder::build(hir::CheckedModuleBuildInput{
          impl->boundModules[index], stagedSignatureFacts[index], stagedModuleInterfaces[index],
          stagedImportedSignatureViews[index], stagedModuleInterfaces.asPtr(),
          stagedCheckedEvidence[index], *stagedCheckedFactsRepository, stagedDispatchFacts[index],
          *stagedBorrowEvidenceRepository, registries, *impl->semanticTypeStore});
      if (checkedModule.isCapabilityRejected()) {
        return rejectIrCapability(checkedModule.capabilityFailures());
      }
      if (checkedModule.isIdentityInvariantRejected()) {
        return rejectIrIdentity(checkedModule.identityFailures());
      }
      if (checkedModule.isIrInvariantRejected()) {
        return rejectIrInvariant(checkedModule.invariantFailures());
      }

      auto hirCandidate = hir::HirBuilder::build(zc::mv(checkedModule).takeVerified());
      if (hirCandidate.isCapabilityRejected()) {
        return rejectIrCapability(hirCandidate.capabilityFailures());
      }
      if (hirCandidate.isIdentityInvariantRejected()) {
        return rejectIrIdentity(hirCandidate.identityFailures());
      }
      if (hirCandidate.isIrInvariantRejected()) {
        return rejectIrInvariant(hirCandidate.invariantFailures());
      }

      auto verifiedHir = hir::HirVerifier::verify(zc::mv(hirCandidate).takeVerified());
      if (verifiedHir.isCapabilityRejected()) {
        return rejectIrCapability(verifiedHir.capabilityFailures());
      }
      if (verifiedHir.isIdentityInvariantRejected()) {
        return rejectIrIdentity(verifiedHir.identityFailures());
      }
      if (verifiedHir.isIrInvariantRejected()) {
        return rejectIrInvariant(verifiedHir.invariantFailures());
      }
      stagedHirModules.add(zc::mv(verifiedHir).takeVerified());

      auto mirCandidate = mir::BuiltMirBuilder::build(stagedHirModules[index]);
      if (mirCandidate.isCapabilityRejected()) {
        return rejectIrCapability(mirCandidate.capabilityFailures());
      }
      if (mirCandidate.isIdentityInvariantRejected()) {
        return rejectIrIdentity(mirCandidate.identityFailures());
      }
      if (mirCandidate.isIrInvariantRejected()) {
        return rejectIrInvariant(mirCandidate.invariantFailures());
      }

      auto verifiedMir = mir::BuiltMirVerifier::verify(zc::mv(mirCandidate).takeVerified());
      if (verifiedMir.isCapabilityRejected()) {
        return rejectIrCapability(verifiedMir.capabilityFailures());
      }
      if (verifiedMir.isIdentityInvariantRejected()) {
        return rejectIrIdentity(verifiedMir.identityFailures());
      }
      if (verifiedMir.isIrInvariantRejected()) {
        return rejectIrInvariant(verifiedMir.invariantFailures());
      }
      stagedBuiltMirModules.add(zc::mv(verifiedMir).takeVerified());
    }
  }
  if (stagedHirModules.size() != impl->boundModules.size() ||
      stagedBuiltMirModules.size() != impl->boundModules.size() ||
      impl->diagnosticEngine->hasErrors()) {
    return false;
  }
  impl->markerShapes = zc::mv(stagedMarkerShapes);
  impl->markerPolicies = zc::mv(stagedMarkerPolicies);
  impl->signatureFacts = zc::mv(stagedSignatureFacts);
  impl->importedSignatureViews = zc::mv(stagedImportedSignatureViews);
  impl->moduleInterfaces = zc::mv(stagedModuleInterfaces);
  impl->coherenceView = zc::mv(stagedCoherenceView);
  impl->checkedFactsRepository = zc::mv(stagedCheckedFactsRepository);
  impl->checkedEvidence = zc::mv(stagedCheckedEvidence);
  impl->dispatchFacts = zc::mv(stagedDispatchFacts);
  impl->borrowEvidenceRepository = zc::mv(stagedBorrowEvidenceRepository);
  impl->hirModules = zc::mv(stagedHirModules);
  impl->builtMirModules = zc::mv(stagedBuiltMirModules);
  impl->verifiedCheckedSources = true;
  return true;
}

basic::StringPool& CompilerSession::getStringPool() { return *impl->stringPool; }

const basic::StringPool& CompilerSession::getStringPool() const { return *impl->stringPool; }

const basic::CompilerOptions& CompilerSession::getCompilerOptions() const {
  return impl->compilerOpts;
}

const source::SourceManager& CompilerSession::getSourceManager() const {
  return *impl->sourceManager;
}

identity::SemanticContextBrand CompilerSession::getSemanticContextBrand() const noexcept {
  return impl->contextBrand;
}

zc::Maybe<const identity::SemanticIdentityRegistrySet&> CompilerSession::getIdentityRegistries()
    const noexcept {
  ZC_IF_SOME(registries, impl->identityRegistries) { return registries; }
  return zc::none;
}

zc::Maybe<const type::SemanticTypeStore&> CompilerSession::getSemanticTypeStore() const noexcept {
  if (impl->semanticTypeStore == nullptr) { return zc::none; }
  return *impl->semanticTypeStore;
}

zc::MemoryResource& CompilerSession::getPackageResolutionMemoryResource() noexcept {
  return impl->packageResolutionMemory;
}

bool CompilerSession::installVerifiedPackageInput(VerifiedPackageSessionInput&& input) {
  if (input.impl.get() == nullptr || impl->packageRequest != zc::none ||
      impl->verifiedHostTarget != zc::none || impl->verifiedTarget != zc::none ||
      impl->packageGraph != zc::none || impl->buildScriptPlan != zc::none ||
      impl->packageSnapshots.size() != 0 ||
      impl->sourceManager->getManagedBufferIds().size() != 0) {
    return false;
  }

  zc::Maybe<VerifiedCrateGraph> crateGraph;
  auto graphResult = VerifiedCrateGraph::buildFinal(input.impl->request, input.impl->graph,
                                                    input.impl->buildScriptPlan);
  if (!graphResult.is<VerifiedCrateGraph>()) { return false; }
  crateGraph = zc::mv(graphResult.get<VerifiedCrateGraph>());

  impl->packageRequest = zc::mv(input.impl->request);
  impl->verifiedHostTarget = zc::mv(input.impl->hostTarget);
  impl->verifiedTarget = zc::mv(input.impl->target);
  impl->packageGraph = zc::mv(input.impl->graph);
  impl->buildScriptPlan = zc::mv(input.impl->buildScriptPlan);
  ZC_IF_SOME(graph, crateGraph) { impl->crateGraph = zc::mv(graph); }
  impl->packageSnapshots = zc::mv(input.impl->snapshots);
  return true;
}

zc::Maybe<const package::VerifiedPackageCompilationRequest&>
CompilerSession::getPackageCompilationRequest() const noexcept {
  ZC_IF_SOME(request, impl->packageRequest) { return request; }
  return zc::none;
}

zc::ArrayPtr<const package::FinalizedCompilationRoot>
CompilerSession::getFinalizedCompilationRoots() const noexcept {
  ZC_IF_SOME(graph, impl->crateGraph) { return graph.roots(); }
  return zc::ArrayPtr<const package::FinalizedCompilationRoot>();
}

zc::Maybe<const VerifiedCrateGraph&> CompilerSession::getVerifiedCrateGraph() const noexcept {
  ZC_IF_SOME(graph, impl->crateGraph) { return graph; }
  return zc::none;
}

zc::Maybe<const identity::SemanticContextFingerprint&>
CompilerSession::getSemanticContextFingerprint() const noexcept {
  ZC_IF_SOME(fingerprint, impl->semanticContextFingerprint) { return fingerprint; }
  return zc::none;
}

zc::Maybe<const binder::VerifiedModuleGraph&> CompilerSession::getVerifiedModuleGraph()
    const noexcept {
  ZC_IF_SOME(graph, impl->moduleGraph) { return graph; }
  return zc::none;
}

zc::ArrayPtr<const VerifiedPreparatoryCrateGraph>
CompilerSession::getVerifiedPreparatoryCrateGraphs() const noexcept {
  return impl->preparatoryCrateGraphs;
}

zc::Maybe<const ir::VerifiedTargetSelection&> CompilerSession::getVerifiedHostTarget()
    const noexcept {
  ZC_IF_SOME(target, impl->verifiedHostTarget) { return target; }
  return zc::none;
}

zc::Maybe<const ir::VerifiedTargetSelection&> CompilerSession::getVerifiedTarget() const noexcept {
  ZC_IF_SOME(target, impl->verifiedTarget) { return target; }
  return zc::none;
}

zc::Maybe<const package::ResolutionOutput&> CompilerSession::getResolvedPackageGraph()
    const noexcept {
  ZC_IF_SOME(graph, impl->packageGraph) { return graph; }
  return zc::none;
}

zc::ArrayPtr<const package::ResolvedPackageSourceSnapshot>
CompilerSession::getResolvedPackageSnapshots() const noexcept {
  return impl->packageSnapshots;
}

zc::Maybe<package::MaterializationIssue> CompilerSession::finishResolvedPackageSnapshots() {
  zc::Maybe<package::MaterializationIssue> firstIssue;
  for (auto& snapshot : impl->packageSnapshots) {
    ZC_IF_SOME(issue, snapshot.finish()) {
      if (firstIssue == zc::none) { firstIssue = issue; }
    }
  }
  impl->packageSnapshots.clear();
  return firstIssue;
}

zc::Maybe<package::BuildScriptIssue> CompilerSession::executeBuildScripts(
    package::BuildScriptPlanExecutor& executor) {
  if (impl->packageRequest == zc::none || impl->verifiedHostTarget == zc::none ||
      impl->verifiedTarget == zc::none || impl->packageGraph == zc::none ||
      impl->packageSnapshots.size() == 0 || impl->buildScriptPlan == zc::none ||
      impl->crateGraph == zc::none || impl->buildScriptResults != zc::none) {
    return package::BuildScriptIssue::BuildResultIntegrityViolation;
  }

  ZC_IF_SOME(plan, impl->buildScriptPlan) {
    zc::Vector<package::VerifiedBuildScriptResult> completed(plan.nodes().size());
    zc::Vector<VerifiedPreparatoryCrateGraph> preparatoryGraphs(plan.nodes().size());
    for (const auto& node : plan.nodes()) {
      bool packageFound = false;
      ZC_IF_SOME(graph, impl->packageGraph) {
        for (const auto& selected : graph.packages()) {
          if (selected.key().encode().asPtr() ==
              node.key().preparatory().package().encode().asPtr()) {
            packageFound = true;
            break;
          }
        }
      }
      if (!packageFound ||
          node.key().preparatory().targetName() != node.contract().target().name()) {
        return package::BuildScriptIssue::BuildResultIntegrityViolation;
      }
    }
    for (const auto nodeIndex : plan.executionOrder()) {
      const auto& node = plan.nodes()[nodeIndex];
      ZC_IF_SOME(request, impl->packageRequest) {
        ZC_IF_SOME(resolution, impl->packageGraph) {
          auto graph =
              VerifiedPreparatoryCrateGraph::build(request, node, resolution, plan, completed);
          if (!graph.is<VerifiedPreparatoryCrateGraph>()) {
            return package::BuildScriptIssue::BuildResultIntegrityViolation;
          }
          auto executed =
              executor.execute(node, graph.get<VerifiedPreparatoryCrateGraph>(), completed);
          if (executed.is<package::BuildScriptIssue>()) {
            return executed.get<package::BuildScriptIssue>();
          }
          auto result = zc::mv(executed.get<package::VerifiedBuildScriptResult>());
          if (result.output().producerKey().digest() !=
              node.key().preparatory().producerKey().digest()) {
            return package::BuildScriptIssue::BuildResultIntegrityViolation;
          }
          preparatoryGraphs.add(zc::mv(graph.get<VerifiedPreparatoryCrateGraph>()));
          completed.add(zc::mv(result));
        }
      }
    }

    zc::Vector<identity::PreparatoryBuildScriptKey> planKeys(plan.nodes().size());
    for (const auto& node : plan.nodes()) { planKeys.add(node.key().preparatory().clone()); }
    auto results = package::VerifiedBuildScriptResultSet::from(zc::mv(planKeys), zc::mv(completed));
    if (results.is<package::BuildResultIntegrityViolation>()) {
      return package::BuildScriptIssue::BuildResultIntegrityViolation;
    }
    auto& value = results.get<package::VerifiedBuildScriptResultSet>();
    impl->preparatoryCrateGraphs = zc::mv(preparatoryGraphs);
    impl->buildScriptResults = zc::mv(value);
    return zc::none;
  }
  return package::BuildScriptIssue::BuildResultIntegrityViolation;
}

zc::Maybe<const package::VerifiedBuildScriptPlan&> CompilerSession::getBuildScriptPlan()
    const noexcept {
  ZC_IF_SOME(plan, impl->buildScriptPlan) { return plan; }
  return zc::none;
}

zc::Maybe<const package::VerifiedBuildScriptResultSet&> CompilerSession::getBuildScriptResults()
    const noexcept {
  ZC_IF_SOME(results, impl->buildScriptResults) { return results; }
  return zc::none;
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
