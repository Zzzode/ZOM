// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core-library-query-verifier.h"

#include "zc/core/encoding.h"
#include "zc/core/map.h"

namespace zomlang::compiler::driver::core_library_query {

bool CoreLibraryQueryVerifier::verifyModuleGraph(
    query::QueryContext& context, const ContextualCoreCrateKey& key,
    const query::TypedQueryResult<CoreModuleGraphRecord>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto singleton =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(key.crate());
  auto stableCrate = incremental_binding_query::StableCrateQueryKey::fromVerified(key.crate());
  if (distribution.isRuntimeFailure() || distribution.kind() != query::QueryValueKind::Value ||
      singleton == zc::none || stableCrate == zc::none) {
    return false;
  }
  auto activeCrates =
      context.get<incremental_binding_query::ActiveCratesQuery>(ZC_ASSERT_NONNULL(singleton));
  auto activeSources =
      context.get<incremental_binding_query::ActiveSourcesQuery>(ZC_ASSERT_NONNULL(stableCrate));
  auto activeModules = context.get<module_graph_query::ActiveModulesQuery>(key.crate());
  auto graph = context.get<module_graph_query::ModuleGraphQuery>(ZC_ASSERT_NONNULL(singleton));
  if (activeCrates.isRuntimeFailure() || activeSources.isRuntimeFailure() ||
      activeModules.isRuntimeFailure() || graph.isRuntimeFailure()) {
    return false;
  }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == graph.semanticFailureBytes();
  }
  if (result.kind() != query::QueryValueKind::Value ||
      activeCrates.kind() != query::QueryValueKind::Value ||
      activeSources.kind() != query::QueryValueKind::Value ||
      activeModules.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value || activeCrates.value().crates().size() != 1 ||
      activeCrates.value().crates()[0].canonicalCrateBytes() != key.crate().encode().asPtr() ||
      activeSources.value().sources().size() != distribution.value().record().files().size()) {
    return false;
  }
  for (const auto& file : distribution.value().record().files()) {
    auto source = identity::SourceFileKey::from(
        key.crate().clone(), identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                                                 file.path().clone()));
    auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
    if (stable == zc::none || !activeSources.value().contains(ZC_ASSERT_NONNULL(stable))) {
      return false;
    }
  }

  zc::TreeMap<zc::String, identity::ModuleKey> modulesByKey;
  for (const auto& module : activeModules.value().modules()) {
    if (module.crate().encode().asPtr() != key.crate().encode().asPtr()) { return false; }
    auto encoded = zc::encodeHex(module.encode().asPtr());
    if (modulesByKey.find(encoded) != zc::none) { return false; }
    modulesByKey.insert(zc::mv(encoded), module.clone());
  }
  if (modulesByKey.size() != graph.value().modules().size()) { return false; }

  zc::Vector<identity::ModuleKey> modules(modulesByKey.size());
  zc::Vector<module_graph_query::ModuleDependencyEdgeKey> edges;
  for (const auto& entry : modulesByKey) {
    bool graphMember = false;
    for (const auto& graphModule : graph.value().modules()) {
      if (graphModule.encode().asPtr() == entry.value.encode().asPtr()) {
        graphMember = true;
        break;
      }
    }
    if (!graphMember) { return false; }
    auto dependencies = context.get<module_graph_query::ModuleDependenciesQuery>(entry.value);
    if (dependencies.isRuntimeFailure()) { return false; }
    if (dependencies.kind() == query::QueryValueKind::SemanticFailure) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() == dependencies.semanticFailureBytes();
    }
    if (dependencies.kind() != query::QueryValueKind::Value) { return false; }
    for (const auto& dependency : dependencies.value().dependencies()) {
      auto edge = module_graph_query::ModuleDependencyEdgeKey::from(entry.value.clone(),
                                                                    dependency.clone());
      if (edge == zc::none) { return false; }
      edges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
    }
    modules.add(entry.value.clone());
  }
  auto coreContext = identity::CoreSemanticContextFingerprint::compute(key.crate());
  if (coreContext == zc::none) { return false; }
  auto expected = CoreModuleGraphRecord::from(
      key.crate().clone(), zc::mv(ZC_ASSERT_NONNULL(coreContext)), zc::mv(modules), zc::mv(edges));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
                                     result.value().encodeCanonical().asPtr();
}

}  // namespace zomlang::compiler::driver::core_library_query
