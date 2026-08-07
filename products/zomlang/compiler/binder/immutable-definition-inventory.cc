// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/binder/immutable-definition-inventory.h"

namespace zomlang::compiler::binder {
namespace {

template <typename Handle>
struct NodeHandleEntry final {
  ast::NodeId node;
  Handle handle;
};

struct ImplementationOccurrenceEntry final {
  ast::NodeId node;
  ImplOccurrenceId occurrence;
  identity::ImplId authority;
};

bool sameOwner(const StableOwnerBodyQueryKey& left, const StableOwnerBodyQueryKey& right) {
  return left.encodeCanonical().asPtr() == right.encodeCanonical().asPtr();
}

bool exactOwnerCoverage(const MaterializedModuleSkeletonIdentities& identities,
                        zc::ArrayPtr<const BoundOwnerBody> ownerBodies) {
  const auto expected = identities.stableWitness().bodyOwners().values();
  if (expected.size() != ownerBodies.size()) { return false; }
  for (size_t index = 0; index < expected.size(); ++index) {
    if (!sameOwner(expected[index], ownerBodies[index].owner())) { return false; }
  }
  return true;
}

zc::Maybe<ast::NodeId> declarationNode(const DefinitionSite& site) {
  const auto& value = site.value();
  if (!value.is<DeclarationDefinitionSite>() || !value.get<DeclarationDefinitionSite>().node) {
    return zc::none;
  }
  return value.get<DeclarationDefinitionSite>().node;
}

bool sameDefinitionSite(const DefinitionSite& left, const DefinitionSite& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<DeclarationDefinitionSite>()) {
    return rightValue.is<DeclarationDefinitionSite>() &&
           leftValue.get<DeclarationDefinitionSite>().node ==
               rightValue.get<DeclarationDefinitionSite>().node;
  }
  if (!rightValue.is<PatternBindingSite>()) { return false; }
  const auto& leftPattern = leftValue.get<PatternBindingSite>();
  const auto& rightPattern = rightValue.get<PatternBindingSite>();
  return leftPattern.introducer == rightPattern.introducer &&
         leftPattern.patternPath.asPtr() == rightPattern.patternPath.asPtr();
}

bool sameSourceSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().encode().asPtr() == right.source().encode().asPtr() &&
         left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

template <typename Entry, typename Handle>
zc::Maybe<const Entry&> findByHandle(zc::ArrayPtr<const Entry> entries, Handle handle) noexcept {
  for (const auto& entry : entries) {
    if (entry.handle() == handle) { return entry; }
  }
  return zc::none;
}

template <typename Entry>
bool hasNode(zc::ArrayPtr<Entry> entries, ast::NodeId node) {
  for (const auto& entry : entries) {
    if (entry.node == node) { return true; }
  }
  return false;
}

template <typename Entry>
zc::Vector<Entry> cloneEntries(zc::ArrayPtr<const Entry> entries) {
  zc::Vector<Entry> result(entries.size());
  result.addAll(entries);
  return result;
}

template <typename Handle>
zc::Maybe<Handle> handleAt(zc::ArrayPtr<const NodeHandleEntry<Handle>> entries,
                           ast::NodeId node) noexcept {
  for (const auto& entry : entries) {
    if (entry.node == node) { return entry.handle; }
  }
  return zc::none;
}

template <typename Entry>
bool sameEntries(zc::ArrayPtr<const Entry> left, zc::ArrayPtr<const Entry> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].handle() != right[index].handle() || left[index].key() != right[index].key() ||
        left[index].record().encode().asPtr() != right[index].record().encode().asPtr()) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct ImmutableDefinitionInventory::Impl final {
  Impl(MaterializedModuleSkeletonIdentities&& identities, zc::Vector<BoundOwnerBody>&& ownerBodies,
       zc::Vector<NodeHandleEntry<identity::DefId>>&& definitionNodes,
       zc::Vector<NodeHandleEntry<identity::GenericParameterId>>&& genericParameterNodes,
       zc::Vector<NodeHandleEntry<identity::CallableParameterId>>&& callableParameterNodes,
       zc::Vector<NodeHandleEntry<OwnerLocalBindingId>>&& ownerLocalBindingNodes,
       zc::Vector<MaterializedDefinitionInventoryEntry>&& definitions,
       zc::Vector<MaterializedGenericParameterInventoryEntry>&& genericParameters,
       zc::Vector<MaterializedCallableParameterInventoryEntry>&& callableParameters,
       zc::Vector<MaterializedOwnerLocalBindingInventoryEntry>&& ownerLocalBindings,
       zc::Vector<MaterializedAnonymousEntityEntry>&& anonymousEntities,
       zc::Vector<MaterializedImplAuthorityInventoryEntry>&& implAuthorities,
       zc::Vector<MaterializedImplOccurrenceInventoryEntry>&& impls,
       zc::Vector<ImplementationOccurrenceEntry>&& implementations) noexcept
      : identities(zc::mv(identities)),
        ownerBodies(zc::mv(ownerBodies)),
        definitionNodes(zc::mv(definitionNodes)),
        genericParameterNodes(zc::mv(genericParameterNodes)),
        callableParameterNodes(zc::mv(callableParameterNodes)),
        ownerLocalBindingNodes(zc::mv(ownerLocalBindingNodes)),
        definitions(zc::mv(definitions)),
        genericParameters(zc::mv(genericParameters)),
        callableParameters(zc::mv(callableParameters)),
        ownerLocalBindings(zc::mv(ownerLocalBindings)),
        anonymousEntities(zc::mv(anonymousEntities)),
        implAuthorities(zc::mv(implAuthorities)),
        impls(zc::mv(impls)),
        implementations(zc::mv(implementations)) {}

  MaterializedModuleSkeletonIdentities identities;
  zc::Vector<BoundOwnerBody> ownerBodies;
  zc::Vector<NodeHandleEntry<identity::DefId>> definitionNodes;
  zc::Vector<NodeHandleEntry<identity::GenericParameterId>> genericParameterNodes;
  zc::Vector<NodeHandleEntry<identity::CallableParameterId>> callableParameterNodes;
  zc::Vector<NodeHandleEntry<OwnerLocalBindingId>> ownerLocalBindingNodes;
  zc::Vector<MaterializedDefinitionInventoryEntry> definitions;
  zc::Vector<MaterializedGenericParameterInventoryEntry> genericParameters;
  zc::Vector<MaterializedCallableParameterInventoryEntry> callableParameters;
  zc::Vector<MaterializedOwnerLocalBindingInventoryEntry> ownerLocalBindings;
  zc::Vector<MaterializedAnonymousEntityEntry> anonymousEntities;
  zc::Vector<MaterializedImplAuthorityInventoryEntry> implAuthorities;
  zc::Vector<MaterializedImplOccurrenceInventoryEntry> impls;
  zc::Vector<ImplementationOccurrenceEntry> implementations;
};

ImmutableDefinitionInventory::ImmutableDefinitionInventory(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
ImmutableDefinitionInventory::~ImmutableDefinitionInventory() noexcept(false) = default;
ImmutableDefinitionInventory::ImmutableDefinitionInventory(
    ImmutableDefinitionInventory&&) noexcept = default;
ImmutableDefinitionInventory& ImmutableDefinitionInventory::operator=(
    ImmutableDefinitionInventory&&) noexcept = default;

zc::Maybe<ImmutableDefinitionInventory> ImmutableDefinitionInventory::from(
    MaterializedModuleSkeletonIdentities&& identities, zc::Vector<BoundOwnerBody>&& ownerBodies,
    zc::ArrayPtr<const DefinitionFact> definitions,
    zc::ArrayPtr<const GenericParameterFact> genericParameters,
    zc::ArrayPtr<const CallableParameterFact> callableParameters,
    zc::ArrayPtr<const ImplBindingFact> implementations,
    zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings,
    zc::ArrayPtr<const AnonymousEntityFact> anonymousEntities) {
  if (!identities.context().isValid() || identities.revision().value() == 0 ||
      !identities.module().belongsTo(identities.context()) ||
      !exactOwnerCoverage(identities, ownerBodies.asPtr()) ||
      definitions.size() != identities.definitions().size() ||
      genericParameters.size() != identities.genericParameters().size() ||
      callableParameters.size() != identities.callableParameters().size()) {
    return zc::none;
  }

  zc::Vector<NodeHandleEntry<identity::DefId>> definitionNodes(definitions.size());
  zc::Vector<MaterializedDefinitionInventoryEntry> definitionEntries(definitions.size());
  for (const auto& fact : definitions) {
    const auto node = declarationNode(fact.site);
    const auto identity = findByHandle(identities.definitions(), fact.identity);
    if (node == zc::none || identity == zc::none ||
        hasNode(definitionNodes.asPtr(), ZC_ASSERT_NONNULL(node))) {
      return zc::none;
    }
    definitionNodes.add(NodeHandleEntry<identity::DefId>{ZC_ASSERT_NONNULL(node), fact.identity});
    zc::Maybe<identity::DeclaredDefinitionName> bindingName = fact.name.clone();
    definitionEntries.add(MaterializedDefinitionInventoryEntry{
        ZC_ASSERT_NONNULL(node), fact.site.clone(), fact.identity,
        ZC_ASSERT_NONNULL(identity).key().clone(), ZC_ASSERT_NONNULL(identity).record().clone(),
        zc::mv(bindingName), fact.source.clone()});
  }

  zc::Vector<NodeHandleEntry<identity::GenericParameterId>> genericParameterNodes(
      genericParameters.size());
  zc::Vector<MaterializedGenericParameterInventoryEntry> genericParameterEntries(
      genericParameters.size());
  for (const auto& fact : genericParameters) {
    const auto node = declarationNode(fact.site);
    const auto identity = findByHandle(identities.genericParameters(), fact.identity);
    if (node == zc::none || identity == zc::none ||
        hasNode(genericParameterNodes.asPtr(), ZC_ASSERT_NONNULL(node))) {
      return zc::none;
    }
    genericParameterNodes.add(
        NodeHandleEntry<identity::GenericParameterId>{ZC_ASSERT_NONNULL(node), fact.identity});
    genericParameterEntries.add(MaterializedGenericParameterInventoryEntry{
        ZC_ASSERT_NONNULL(node), fact.site.clone(), fact.identity,
        ZC_ASSERT_NONNULL(identity).key().clone(), ZC_ASSERT_NONNULL(identity).record().clone(),
        fact.name.clone(), fact.source.clone()});
  }

  zc::Vector<NodeHandleEntry<identity::CallableParameterId>> callableParameterNodes(
      callableParameters.size());
  zc::Vector<MaterializedCallableParameterInventoryEntry> callableParameterEntries(
      callableParameters.size());
  for (const auto& fact : callableParameters) {
    const auto node = declarationNode(fact.site);
    const auto identity = findByHandle(identities.callableParameters(), fact.identity);
    if (node == zc::none || identity == zc::none ||
        hasNode(callableParameterNodes.asPtr(), ZC_ASSERT_NONNULL(node))) {
      return zc::none;
    }
    callableParameterNodes.add(
        NodeHandleEntry<identity::CallableParameterId>{ZC_ASSERT_NONNULL(node), fact.identity});
    zc::Maybe<identity::DeclaredDefinitionName> bindingName;
    ZC_IF_SOME(value, fact.name) { bindingName = value.clone(); }
    callableParameterEntries.add(MaterializedCallableParameterInventoryEntry{
        ZC_ASSERT_NONNULL(node), fact.site.clone(), fact.identity,
        ZC_ASSERT_NONNULL(identity).key().clone(), ZC_ASSERT_NONNULL(identity).record().clone(),
        zc::mv(bindingName), fact.source.clone()});
  }

  size_t expectedOwnerLocals = 0;
  size_t expectedAnonymous = 0;
  for (const auto& body : ownerBodies) {
    expectedOwnerLocals += body.bindings().values().size();
    expectedAnonymous +=
        body.closures().values().size() + body.explicitClosureCaptures().values().size();
  }
  if (ownerLocalBindings.size() != expectedOwnerLocals ||
      anonymousEntities.size() != expectedAnonymous) {
    return zc::none;
  }

  zc::Vector<NodeHandleEntry<OwnerLocalBindingId>> ownerLocalBindingNodes(
      ownerLocalBindings.size());
  zc::Vector<MaterializedOwnerLocalBindingInventoryEntry> ownerLocalEntries(
      ownerLocalBindings.size());
  size_t ownerLocalIndex = 0;
  for (const auto& body : ownerBodies) {
    for (const auto& stable : body.bindings().values()) {
      if (ownerLocalIndex >= ownerLocalBindings.size()) { return zc::none; }
      const auto& fact = ownerLocalBindings[ownerLocalIndex++];
    if (!fact.node || !fact.identity.belongsTo(identities.context()) ||
        !fact.identity.belongsTo(identities.module()) ||
        hasNode(ownerLocalBindingNodes.asPtr(), fact.node) || fact.kind != stable.kind() ||
        fact.name != stable.name() || fact.nameSpace != stable.nameSpace() ||
        fact.activation != stable.activation()) {
      return zc::none;
    }
    ownerLocalBindingNodes.add(NodeHandleEntry<OwnerLocalBindingId>{fact.node, fact.identity});
      ownerLocalEntries.add(MaterializedOwnerLocalBindingInventoryEntry{
          fact.node, fact.site.clone(), fact.identity, stable.key().clone(), fact.source.clone()});
    }
  }
  if (ownerLocalIndex != ownerLocalBindings.size()) { return zc::none; }

  zc::Vector<MaterializedAnonymousEntityEntry> anonymousEntries(anonymousEntities.size());
  size_t anonymousIndex = 0;
  for (const auto& body : ownerBodies) {
    for (const auto& stable : body.closures().values()) {
      if (anonymousIndex >= anonymousEntities.size() ||
          anonymousEntities[anonymousIndex].key != stable.closure()) {
        return zc::none;
      }
      ++anonymousIndex;
    }
    for (const auto& stable : body.explicitClosureCaptures().values()) {
      if (anonymousIndex >= anonymousEntities.size() ||
          anonymousEntities[anonymousIndex].key != stable.closure()) {
        return zc::none;
      }
      ++anonymousIndex;
    }
  }
  if (anonymousIndex != anonymousEntities.size()) { return zc::none; }
  for (const auto& fact : anonymousEntities) {
    const auto node = declarationNode(fact.site);
    if (!fact.node || node == zc::none || ZC_ASSERT_NONNULL(node) != fact.node ||
        !fact.identity.belongsTo(identities.context()) ||
        !fact.identity.belongsTo(identities.module())) {
      return zc::none;
    }
    for (const auto& entry : anonymousEntries) {
      if (entry.node == fact.node && entry.key.role() == fact.key.role()) { return zc::none; }
    }
    anonymousEntries.add(MaterializedAnonymousEntityEntry{
        fact.node, fact.site.clone(), fact.identity, fact.key.clone(), fact.source.clone()});
  }

  zc::Vector<ImplementationOccurrenceEntry> implementationEntries(implementations.size());
  zc::Vector<MaterializedImplAuthorityInventoryEntry> implAuthorityEntries(
      identities.implementations().size());
  for (const auto& identity : identities.implementations()) {
    implAuthorityEntries.add(MaterializedImplAuthorityInventoryEntry{
        identity.handle(), identity.key().clone(), identity.record().clone()});
  }
  const auto stableImplementations = identities.stableWitness().implementationOccurrences().values();
  if (implementations.size() != stableImplementations.size()) { return zc::none; }
  zc::Vector<MaterializedImplOccurrenceInventoryEntry> implEntries(implementations.size());
  for (size_t index = 0; index < implementations.size(); ++index) {
    const auto& fact = implementations[index];
    const auto& stable = stableImplementations[index];
    if (!fact.node || !fact.occurrence.belongsTo(identities.context()) ||
        !fact.occurrence.belongsTo(identities.module()) ||
        findByHandle(identities.implementations(), fact.authority) == zc::none) {
      return zc::none;
    }
    for (const auto& entry : implementationEntries) {
      if (entry.node == fact.node || entry.occurrence == fact.occurrence) { return zc::none; }
    }
    implementationEntries.add(
        ImplementationOccurrenceEntry{fact.node, fact.occurrence, fact.authority});
    implEntries.add(MaterializedImplOccurrenceInventoryEntry{
        fact.occurrence, stable.occurrence().occurrence().clone(), fact.authority, fact.node,
        fact.source.clone()});
  }

  return ImmutableDefinitionInventory(zc::heap<Impl>(
      zc::mv(identities), zc::mv(ownerBodies), zc::mv(definitionNodes),
      zc::mv(genericParameterNodes), zc::mv(callableParameterNodes), zc::mv(ownerLocalBindingNodes),
      zc::mv(definitionEntries), zc::mv(genericParameterEntries),
      zc::mv(callableParameterEntries), zc::mv(ownerLocalEntries), zc::mv(anonymousEntries),
      zc::mv(implAuthorityEntries), zc::mv(implEntries), zc::mv(implementationEntries)));
}

ImmutableDefinitionInventory ImmutableDefinitionInventory::clone() const {
  zc::Vector<BoundOwnerBody> ownerBodies;
  for (const auto& ownerBody : impl->ownerBodies) { ownerBodies.add(ownerBody.clone()); }
  zc::Vector<MaterializedDefinitionInventoryEntry> definitions;
  for (const auto& entry : impl->definitions) {
    zc::Maybe<identity::DeclaredDefinitionName> bindingName;
    ZC_IF_SOME(value, entry.bindingName) { bindingName = value.clone(); }
    definitions.add(MaterializedDefinitionInventoryEntry{
        entry.node, entry.site.clone(), entry.definition, entry.key.clone(), entry.record.clone(),
        zc::mv(bindingName), entry.source.clone()});
  }
  zc::Vector<MaterializedGenericParameterInventoryEntry> genericParameters;
  for (const auto& entry : impl->genericParameters) {
    genericParameters.add(MaterializedGenericParameterInventoryEntry{
        entry.node, entry.site.clone(), entry.parameter, entry.key.clone(), entry.record.clone(),
        entry.bindingName.clone(), entry.source.clone()});
  }
  zc::Vector<MaterializedCallableParameterInventoryEntry> callableParameters;
  for (const auto& entry : impl->callableParameters) {
    zc::Maybe<identity::DeclaredDefinitionName> bindingName;
    ZC_IF_SOME(value, entry.bindingName) { bindingName = value.clone(); }
    callableParameters.add(MaterializedCallableParameterInventoryEntry{
        entry.node, entry.site.clone(), entry.parameter, entry.key.clone(), entry.record.clone(),
        zc::mv(bindingName), entry.source.clone()});
  }
  zc::Vector<MaterializedOwnerLocalBindingInventoryEntry> ownerLocalBindings;
  for (const auto& entry : impl->ownerLocalBindings) {
    ownerLocalBindings.add(MaterializedOwnerLocalBindingInventoryEntry{
        entry.node, entry.site.clone(), entry.binding, entry.key.clone(), entry.source.clone()});
  }
  zc::Vector<MaterializedAnonymousEntityEntry> anonymousEntities;
  for (const auto& entry : impl->anonymousEntities) {
    anonymousEntities.add(MaterializedAnonymousEntityEntry{
        entry.node, entry.site.clone(), entry.entity, entry.key.clone(), entry.source.clone()});
  }
  zc::Vector<MaterializedImplAuthorityInventoryEntry> implAuthorities;
  for (const auto& entry : impl->implAuthorities) {
    implAuthorities.add(MaterializedImplAuthorityInventoryEntry{
        entry.implementation, entry.key.clone(), entry.record.clone()});
  }
  zc::Vector<MaterializedImplOccurrenceInventoryEntry> impls;
  for (const auto& entry : impl->impls) {
    impls.add(MaterializedImplOccurrenceInventoryEntry{
        entry.occurrence, entry.key.clone(), entry.authority, entry.node, entry.source.clone()});
  }
  auto definitionNodes = cloneEntries(impl->definitionNodes.asPtr());
  auto genericParameterNodes = cloneEntries(impl->genericParameterNodes.asPtr());
  auto callableParameterNodes = cloneEntries(impl->callableParameterNodes.asPtr());
  auto ownerLocalBindingNodes = cloneEntries(impl->ownerLocalBindingNodes.asPtr());
  auto implementations = cloneEntries(impl->implementations.asPtr());
  return ImmutableDefinitionInventory(zc::heap<Impl>(
      impl->identities.clone(), zc::mv(ownerBodies), zc::mv(definitionNodes),
      zc::mv(genericParameterNodes), zc::mv(callableParameterNodes), zc::mv(ownerLocalBindingNodes),
      zc::mv(definitions), zc::mv(genericParameters), zc::mv(callableParameters),
      zc::mv(ownerLocalBindings), zc::mv(anonymousEntities), zc::mv(implAuthorities),
      zc::mv(impls), zc::mv(implementations)));
}

identity::SemanticContextBrand ImmutableDefinitionInventory::semanticContext() const noexcept {
  return impl->identities.context();
}

query::DatabaseRevision ImmutableDefinitionInventory::revision() const noexcept {
  return impl->identities.revision();
}

const identity::SemanticContextFingerprint& ImmutableDefinitionInventory::fingerprint()
    const noexcept {
  return impl->identities.fingerprint();
}

identity::ModuleId ImmutableDefinitionInventory::module() const noexcept {
  return impl->identities.module();
}

const MaterializedModuleSkeletonIdentities& ImmutableDefinitionInventory::identities()
    const noexcept {
  return impl->identities;
}

zc::ArrayPtr<const BoundOwnerBody> ImmutableDefinitionInventory::ownerBodies() const noexcept {
  return impl->ownerBodies.asPtr();
}

zc::ArrayPtr<const MaterializedDefinitionInventoryEntry>
ImmutableDefinitionInventory::definitions() const noexcept {
  return impl->definitions.asPtr();
}

zc::ArrayPtr<const MaterializedGenericParameterInventoryEntry>
ImmutableDefinitionInventory::genericParameters() const noexcept {
  return impl->genericParameters.asPtr();
}

zc::ArrayPtr<const MaterializedCallableParameterInventoryEntry>
ImmutableDefinitionInventory::callableParameters() const noexcept {
  return impl->callableParameters.asPtr();
}

zc::ArrayPtr<const MaterializedOwnerLocalBindingInventoryEntry>
ImmutableDefinitionInventory::ownerLocalBindings() const noexcept {
  return impl->ownerLocalBindings.asPtr();
}

zc::ArrayPtr<const MaterializedImplAuthorityInventoryEntry>
ImmutableDefinitionInventory::implAuthorities() const noexcept {
  return impl->implAuthorities.asPtr();
}

zc::ArrayPtr<const MaterializedImplOccurrenceInventoryEntry>
ImmutableDefinitionInventory::impls() const noexcept {
  return impl->impls.asPtr();
}

zc::Maybe<const MaterializedDefinitionIdentityEntry&> ImmutableDefinitionInventory::definition(
    identity::DefId handle) const noexcept {
  return findByHandle(identities().definitions(), handle);
}

zc::Maybe<const MaterializedImplementationIdentityEntry&>
ImmutableDefinitionInventory::implementation(identity::ImplId handle) const noexcept {
  return findByHandle(identities().implementations(), handle);
}

zc::Maybe<const MaterializedGenericParameterIdentityEntry&>
ImmutableDefinitionInventory::genericParameter(identity::GenericParameterId handle) const noexcept {
  return findByHandle(identities().genericParameters(), handle);
}

zc::Maybe<const MaterializedCallableParameterIdentityEntry&>
ImmutableDefinitionInventory::callableParameter(
    identity::CallableParameterId handle) const noexcept {
  return findByHandle(identities().callableParameters(), handle);
}

zc::Maybe<identity::DefId> ImmutableDefinitionInventory::definitionAt(
    ast::NodeId node) const noexcept {
  return handleAt(impl->definitionNodes.asPtr(), node);
}

zc::Maybe<identity::GenericParameterId> ImmutableDefinitionInventory::genericParameterAt(
    ast::NodeId node) const noexcept {
  return handleAt(impl->genericParameterNodes.asPtr(), node);
}

zc::Maybe<identity::CallableParameterId> ImmutableDefinitionInventory::callableParameterAt(
    ast::NodeId node) const noexcept {
  return handleAt(impl->callableParameterNodes.asPtr(), node);
}

zc::Maybe<OwnerLocalBindingId> ImmutableDefinitionInventory::ownerLocalBindingAt(
    ast::NodeId node) const noexcept {
  return handleAt(impl->ownerLocalBindingNodes.asPtr(), node);
}

zc::Maybe<const MaterializedAnonymousEntityEntry&> ImmutableDefinitionInventory::anonymousEntityAt(
    ast::NodeId node, AnonymousOwnerLocalRole role) const noexcept {
  for (const auto& entry : impl->anonymousEntities) {
    if (entry.node == node && entry.key.role() == role) { return entry; }
  }
  return zc::none;
}

zc::Maybe<ImplOccurrenceId> ImmutableDefinitionInventory::implementationAt(
    ast::NodeId node) const noexcept {
  for (const auto& entry : impl->implementations) {
    if (entry.node == node) { return entry.occurrence; }
  }
  return zc::none;
}

zc::Maybe<identity::ImplId> ImmutableDefinitionInventory::implementationAuthority(
    ImplOccurrenceId occurrence) const noexcept {
  for (const auto& entry : impl->implementations) {
    if (entry.occurrence == occurrence) { return entry.authority; }
  }
  return zc::none;
}

zc::ArrayPtr<const MaterializedAnonymousEntityEntry>
ImmutableDefinitionInventory::anonymousEntities() const noexcept {
  return impl->anonymousEntities.asPtr();
}

bool ImmutableDefinitionInventory::matches(
    const MaterializedModuleSkeletonIdentities& source,
    zc::ArrayPtr<const BoundOwnerBody> sourceOwnerBodies,
    zc::ArrayPtr<const DefinitionFact> definitions,
    zc::ArrayPtr<const GenericParameterFact> genericParameters,
    zc::ArrayPtr<const CallableParameterFact> callableParameters,
    zc::ArrayPtr<const ImplBindingFact> implementations,
    zc::ArrayPtr<const OwnerLocalBindingFact> ownerLocalBindings,
    zc::ArrayPtr<const AnonymousEntityFact> anonymousEntities) const {
  if (semanticContext() != source.context() || revision() != source.revision() ||
      fingerprint().digest() != source.fingerprint().digest() || module() != source.module() ||
      !sameEntries(identities().definitions(), source.definitions()) ||
      !sameEntries(identities().implementations(), source.implementations()) ||
      !sameEntries(identities().genericParameters(), source.genericParameters()) ||
      !sameEntries(identities().callableParameters(), source.callableParameters()) ||
      ownerBodies().size() != sourceOwnerBodies.size() ||
      impl->definitionNodes.size() != definitions.size() ||
      impl->genericParameterNodes.size() != genericParameters.size() ||
      impl->callableParameterNodes.size() != callableParameters.size() ||
      impl->ownerLocalBindingNodes.size() != ownerLocalBindings.size() ||
      impl->anonymousEntities.size() != anonymousEntities.size() ||
      impl->implementations.size() != implementations.size()) {
    return false;
  }
  for (size_t index = 0; index < ownerBodies().size(); ++index) {
    if (!(ownerBodies()[index] == sourceOwnerBodies[index])) { return false; }
  }
  for (const auto& fact : definitions) {
    const auto node = declarationNode(fact.site);
    if (node == zc::none || definitionAt(ZC_ASSERT_NONNULL(node)) != fact.identity) {
      return false;
    }
  }
  for (const auto& fact : genericParameters) {
    const auto node = declarationNode(fact.site);
    if (node == zc::none || genericParameterAt(ZC_ASSERT_NONNULL(node)) != fact.identity) {
      return false;
    }
  }
  for (const auto& fact : callableParameters) {
    const auto node = declarationNode(fact.site);
    if (node == zc::none || callableParameterAt(ZC_ASSERT_NONNULL(node)) != fact.identity) {
      return false;
    }
  }
  for (const auto& fact : ownerLocalBindings) {
    if (ownerLocalBindingAt(fact.node) != fact.identity) { return false; }
  }
  for (const auto& fact : anonymousEntities) {
    auto entry = anonymousEntityAt(fact.node, fact.key.role());
    if (entry == zc::none || ZC_ASSERT_NONNULL(entry).entity != fact.identity ||
        ZC_ASSERT_NONNULL(entry).key != fact.key ||
        !sameDefinitionSite(ZC_ASSERT_NONNULL(entry).site, fact.site) ||
        !sameSourceSpan(ZC_ASSERT_NONNULL(entry).source, fact.source)) {
      return false;
    }
  }
  for (const auto& fact : implementations) {
    if (implementationAt(fact.node) != fact.occurrence ||
        implementationAuthority(fact.occurrence) != fact.authority) {
      return false;
    }
  }
  return true;
}

}  // namespace zomlang::compiler::binder
