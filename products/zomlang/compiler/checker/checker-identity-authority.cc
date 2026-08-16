// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/checker/checker-identity-authority.h"

#include "zomlang/compiler/binder/identity/identity-pre-admission.h"

namespace zomlang::compiler::checker {
namespace {

template <typename Entry, typename Handle, typename Entries>
zc::Maybe<const Entry&> findUnique(Entries entries, Handle handle) {
  zc::Maybe<const Entry&> result;
  for (const auto& entry : entries) {
    if (entry.handle() != handle) { continue; }
    if (result != zc::none) { return zc::none; }
    result = entry;
  }
  return result;
}

template <typename Entry, typename Key, typename Entries>
zc::Maybe<const Entry&> findUniqueByKey(Entries entries, const Key& key) {
  zc::Maybe<const Entry&> result;
  for (const auto& entry : entries) {
    if (entry.key().encode().asPtr() != key.encode().asPtr()) { continue; }
    if (result != zc::none) { return zc::none; }
    result = entry;
  }
  return result;
}

template <typename Entry, typename Handle, typename Lookup>
zc::Maybe<const Entry&> findInModules(
    zc::ArrayPtr<const CheckerIdentityAuthority::BoundModuleView> modules, Handle handle,
    Lookup lookup) {
  zc::Maybe<const Entry&> result;
  for (const auto& module : modules) {
    auto entry = lookup(module.definitions(), handle);
    if (entry == zc::none) { continue; }
    if (result != zc::none) { return zc::none; }
    ZC_IF_SOME(value, entry) { result = value; }
  }
  return result;
}

template <typename Entry, typename Key, typename Lookup>
zc::Maybe<const Entry&> findInModulesByKey(
    zc::ArrayPtr<const CheckerIdentityAuthority::BoundModuleView> modules, const Key& key,
    Lookup lookup) {
  zc::Maybe<const Entry&> result;
  for (const auto& module : modules) {
    for (const auto& entry : lookup(module.definitions().identities())) {
      if (entry.key().encode().asPtr() != key.encode().asPtr()) { continue; }
      if (result != zc::none) { return zc::none; }
      result = entry;
    }
  }
  return result;
}

bool hasMatchingGraphMembership(const CheckerIdentityAuthority::BoundModuleView& module,
                                const driver::module_graph_query::MaterializedModuleGraph& graph) {
  size_t compilationUnitMatches = 0;
  size_t crateMatches = 0;
  size_t sourceMatches = 0;
  size_t moduleMatches = 0;
  bool sourceKeyMatches = false;
  bool moduleKeyMatches = false;
  const auto& bound = module.boundModuleLease().capability();
  for (const auto& entry : graph.units()) {
    if (entry.handle() == module.compilationUnit()) { ++compilationUnitMatches; }
  }
  for (const auto& entry : graph.crates()) {
    if (entry.handle() == module.crate()) { ++crateMatches; }
  }
  for (const auto& entry : graph.sources()) {
    if (entry.handle() != module.sourceFile()) { continue; }
    ++sourceMatches;
    sourceKeyMatches = entry.key().encode().asPtr() == bound.source().encode().asPtr();
  }
  for (const auto& entry : graph.modules()) {
    if (entry.handle() != module.module()) { continue; }
    ++moduleMatches;
    moduleKeyMatches = entry.key().encode().asPtr() == bound.module().encode().asPtr();
  }
  return compilationUnitMatches == 1 && crateMatches == 1 && sourceMatches == 1 &&
         moduleMatches == 1 && sourceKeyMatches && moduleKeyMatches;
}

bool hasMatchingIdentityAdmission(const CheckerIdentityAuthority::BoundModuleView& module) {
  const auto& bound = module.boundModuleLease().capability();
  const auto& skeleton = bound.skeletonLease().capability();
  const auto& admission = skeleton.identityAdmissionLease().capability();
  return skeleton.identityAdmissionLease().revision() == bound.revision() &&
         admission.module().encode().asPtr() == bound.module().encode().asPtr() &&
         admission.source().encode().asPtr() == bound.source().encode().asPtr();
}

}  // namespace

struct CheckerIdentityAuthority::Impl final {
  Impl(GraphLease&& graph, zc::Vector<BoundModuleView>&& modules) noexcept
      : graph(zc::mv(graph)), modules(zc::mv(modules)) {}

  GraphLease graph;
  zc::Vector<BoundModuleView> modules;
};

CheckerIdentityAuthority::CheckerIdentityAuthority(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
CheckerIdentityAuthority::~CheckerIdentityAuthority() noexcept(false) = default;
CheckerIdentityAuthority::CheckerIdentityAuthority(CheckerIdentityAuthority&&) noexcept = default;
CheckerIdentityAuthority& CheckerIdentityAuthority::operator=(CheckerIdentityAuthority&&) noexcept =
    default;

zc::Maybe<CheckerIdentityAuthority> CheckerIdentityAuthority::from(
    zc::Vector<BoundModuleView>&& modules) {
  if (modules.empty()) { return zc::none; }
  auto graph = modules[0].boundModuleLease().capability().graphLease().retain();
  const auto& graphValue = graph.capability();
  if (!graphValue.context().isValid() || graph.revision() != graphValue.revision() ||
      modules.size() != graphValue.modules().size()) {
    return zc::none;
  }
  for (size_t index = 0; index < graphValue.modules().size(); ++index) {
    if (modules[index].module() != graphValue.modules()[index].handle()) { return zc::none; }
  }
  for (const auto& module : modules) {
    const auto& bound = module.boundModuleLease().capability();
    if (module.semanticContext() != graphValue.context() ||
        module.semanticFingerprint().digest() != graphValue.witness().fingerprint().digest() ||
        bound.graphLease().stableWitness() != graph.stableWitness() ||
        bound.revision() != graphValue.revision() ||
        !hasMatchingGraphMembership(module, graphValue) || !hasMatchingIdentityAdmission(module)) {
      return zc::none;
    }
  }
  return CheckerIdentityAuthority(zc::heap<Impl>(zc::mv(graph), zc::mv(modules)));
}

CheckerIdentityAuthority CheckerIdentityAuthority::clone() const {
  zc::Vector<BoundModuleView> modules(impl->modules.size());
  for (const auto& module : impl->modules) { modules.add(module.retain()); }
  ZC_IF_SOME(value, from(zc::mv(modules))) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::SemanticContextBrand CheckerIdentityAuthority::semanticContext() const noexcept {
  return impl->graph.capability().context();
}

query::DatabaseRevision CheckerIdentityAuthority::revision() const noexcept {
  return impl->graph.capability().revision();
}

const identity::ContextFingerprint& CheckerIdentityAuthority::fingerprint() const noexcept {
  return impl->graph.capability().witness().fingerprint();
}

const CheckerIdentityAuthority::GraphLease& CheckerIdentityAuthority::graphLease() const noexcept {
  return impl->graph;
}

zc::ArrayPtr<const CheckerIdentityAuthority::BoundModuleView> CheckerIdentityAuthority::modules()
    const noexcept {
  return impl->modules.asPtr();
}

zc::Maybe<const CheckerIdentityAuthority::BoundModuleView&> CheckerIdentityAuthority::boundModule(
    identity::ModuleId handle) const noexcept {
  zc::Maybe<const BoundModuleView&> result;
  for (const auto& module : impl->modules) {
    if (module.module() != handle) { continue; }
    if (result != zc::none) { return zc::none; }
    result = module;
  }
  return result;
}

zc::Maybe<const driver::module_graph_query::MaterializedCompilationUnitEntry&>
CheckerIdentityAuthority::compilationUnit(identity::CompilationUnitId handle) const noexcept {
  return findUnique<driver::module_graph_query::MaterializedCompilationUnitEntry>(
      impl->graph.capability().units(), handle);
}

zc::Maybe<const driver::module_graph_query::MaterializedCompilationUnitEntry&>
CheckerIdentityAuthority::compilationUnit(
    const identity::CompilationUnitIdentity& key) const noexcept {
  return findUniqueByKey<driver::module_graph_query::MaterializedCompilationUnitEntry>(
      impl->graph.capability().units(), key);
}

zc::Maybe<const driver::module_graph_query::MaterializedCrateEntry&>
CheckerIdentityAuthority::crate(identity::CrateId handle) const noexcept {
  return findUnique<driver::module_graph_query::MaterializedCrateEntry>(
      impl->graph.capability().crates(), handle);
}

zc::Maybe<const driver::module_graph_query::MaterializedCrateEntry&>
CheckerIdentityAuthority::crate(const identity::CrateKey& key) const noexcept {
  return findUniqueByKey<driver::module_graph_query::MaterializedCrateEntry>(
      impl->graph.capability().crates(), key);
}

zc::Maybe<const driver::module_graph_query::MaterializedSourceEntry&>
CheckerIdentityAuthority::sourceFile(identity::SourceFileId handle) const noexcept {
  return findUnique<driver::module_graph_query::MaterializedSourceEntry>(
      impl->graph.capability().sources(), handle);
}

zc::Maybe<const driver::module_graph_query::MaterializedSourceEntry&>
CheckerIdentityAuthority::sourceFile(const identity::SourceFileKey& key) const noexcept {
  zc::Maybe<const driver::module_graph_query::MaterializedSourceEntry&> result;
  for (const auto& entry : impl->graph.capability().sources()) {
    if (entry.key().encode().asPtr() != key.encode().asPtr()) { continue; }
    if (result != zc::none) { return zc::none; }
    result = entry;
  }
  return result;
}

zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&>
CheckerIdentityAuthority::module(identity::ModuleId handle) const noexcept {
  return findUnique<driver::module_graph_query::MaterializedModuleEntry>(
      impl->graph.capability().modules(), handle);
}

zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&>
CheckerIdentityAuthority::module(const identity::ModuleKey& key) const noexcept {
  return findUniqueByKey<driver::module_graph_query::MaterializedModuleEntry>(
      impl->graph.capability().modules(), key);
}

zc::Maybe<const binder::MaterializedDefinitionIdentityEntry&> CheckerIdentityAuthority::definition(
    identity::DefId handle) const noexcept {
  return findInModules<binder::MaterializedDefinitionIdentityEntry>(
      modules(), handle,
      [](const binder::ImmutableDefinitionInventory& definitions, identity::DefId target) {
        return definitions.definition(target);
      });
}

zc::Maybe<const binder::MaterializedDefinitionIdentityEntry&> CheckerIdentityAuthority::definition(
    const identity::DefinitionKey& key) const noexcept {
  return findInModulesByKey<binder::MaterializedDefinitionIdentityEntry>(
      modules(), key, [](const binder::MaterializedModuleSkeletonIdentities& identities) {
        return identities.definitions();
      });
}

zc::Maybe<const identity::DefinitionIdentityAuthority&>
CheckerIdentityAuthority::definitionAuthority(identity::DefId handle) const noexcept {
  auto materialized = definition(handle);
  if (materialized == zc::none) { return zc::none; }
  ZC_IF_SOME(entry, materialized) {
    zc::Maybe<const identity::DefinitionIdentityAuthority&> result;
    for (const auto& module : modules()) {
      const auto& skeleton = module.boundModuleLease().capability().skeletonLease().capability();
      const auto& admission = skeleton.identityAdmissionLease().capability();
      for (const auto& candidate : admission.definitions()) {
        if (candidate.authority.key() != entry.key() ||
            candidate.authority.record().encode().asPtr() != entry.record().encode().asPtr()) {
          continue;
        }
        if (!candidate.authority.verify() || result != zc::none) { return zc::none; }
        result = candidate.authority;
      }
    }
    return result;
  }
  return zc::none;
}

zc::Maybe<const binder::MaterializedImplementationIdentityEntry&>
CheckerIdentityAuthority::implementation(identity::ImplId handle) const noexcept {
  return findInModules<binder::MaterializedImplementationIdentityEntry>(
      modules(), handle,
      [](const binder::ImmutableDefinitionInventory& definitions, identity::ImplId target) {
        return definitions.implementation(target);
      });
}

zc::Maybe<const binder::MaterializedImplementationIdentityEntry&>
CheckerIdentityAuthority::implementation(const identity::ImplKey& key) const noexcept {
  return findInModulesByKey<binder::MaterializedImplementationIdentityEntry>(
      modules(), key, [](const binder::MaterializedModuleSkeletonIdentities& identities) {
        return identities.implementations();
      });
}

zc::Maybe<const binder::MaterializedGenericParameterIdentityEntry&>
CheckerIdentityAuthority::genericParameter(identity::GenericParameterId handle) const noexcept {
  return findInModules<binder::MaterializedGenericParameterIdentityEntry>(
      modules(), handle,
      [](const binder::ImmutableDefinitionInventory& definitions,
         identity::GenericParameterId target) { return definitions.genericParameter(target); });
}

zc::Maybe<const binder::MaterializedGenericParameterIdentityEntry&>
CheckerIdentityAuthority::genericParameter(
    const identity::GenericParameterKey& key) const noexcept {
  return findInModulesByKey<binder::MaterializedGenericParameterIdentityEntry>(
      modules(), key, [](const binder::MaterializedModuleSkeletonIdentities& identities) {
        return identities.genericParameters();
      });
}

zc::Maybe<const binder::MaterializedCallableParameterIdentityEntry&>
CheckerIdentityAuthority::callableParameter(identity::CallableParameterId handle) const noexcept {
  return findInModules<binder::MaterializedCallableParameterIdentityEntry>(
      modules(), handle,
      [](const binder::ImmutableDefinitionInventory& definitions,
         identity::CallableParameterId target) { return definitions.callableParameter(target); });
}

zc::Maybe<const binder::MaterializedCallableParameterIdentityEntry&>
CheckerIdentityAuthority::callableParameter(
    const identity::CallableParameterKey& key) const noexcept {
  return findInModulesByKey<binder::MaterializedCallableParameterIdentityEntry>(
      modules(), key, [](const binder::MaterializedModuleSkeletonIdentities& identities) {
        return identities.callableParameters();
      });
}

}  // namespace zomlang::compiler::checker
