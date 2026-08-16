// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core/verifier.h"

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/binder/surface/module-body-syntax.h"
#include "zomlang/compiler/binder/graph/module-skeleton-query.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/driver/core/role-seed-failure.h"
#include "zomlang/compiler/driver/core/signature.h"
#include "zomlang/compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/query/binding/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/query/binding/named-item-query.h"

namespace zomlang::compiler::driver::core_library_query {
namespace {

bool isMarkerModule(const identity::ModuleKey& module, const identity::CrateKey& core) {
  return module.crate().encode().asPtr() == core.encode().asPtr() && module.path().size() == 2 &&
         module.path()[0].text() == "core"_zc && module.path()[1].text() == "marker"_zc;
}

bool matchesRoleTemplate(const source::core::CoreRoleIdentityTemplate& role,
                         const identity::ModuleKey& markerModule,
                         const binder::NamedDefinitionInventoryEntry& entry) {
  const auto& record = entry.record();
  if (role.module().size() != markerModule.path().size() ||
      record.module().encode().asPtr() != markerModule.encode().asPtr() ||
      record.owners().size() != role.owners().size() || record.kind() != role.kind() ||
      record.nameSpace() != role.nameSpace() || record.name() != role.declaredName() ||
      role.overloadHeader() != zc::none || record.overloadHeader() != zc::none) {
    return false;
  }
  for (size_t index = 0; index < role.module().size(); ++index) {
    if (role.module()[index].text() != markerModule.path()[index].text()) { return false; }
  }
  return true;
}

zc::Maybe<identity::ModuleKey> findMarkerModule(const CoreModuleGraphRecord& graph) {
  zc::Maybe<identity::ModuleKey> marker;
  for (const auto& module : graph.modules()) {
    if (!isMarkerModule(module, graph.core())) { continue; }
    if (marker != zc::none) { return zc::none; }
    marker = module.clone();
  }
  return marker;
}

zc::Maybe<CoreRoleSeedFailure> inventoryFailure(
    const source::core::CoreDistributionInputRecord& distribution,
    const identity::ModuleKey& markerModule, const binder::NamedDefinitionInventory& inventory) {
  for (const auto& templateRole : distribution.record().roles()) {
    size_t matches = 0;
    for (const auto& entry : inventory.entries()) {
      if (matchesRoleTemplate(templateRole, markerModule, entry)) { ++matches; }
    }
    if (matches == 0) {
      return CoreRoleSeedFailure::from(CoreRoleSeedFailureKind::MissingRequiredRole,
                                       templateRole.role());
    }
    if (matches != 1) {
      return CoreRoleSeedFailure::from(CoreRoleSeedFailureKind::DuplicateRole, templateRole.role());
    }
  }
  return zc::none;
}

zc::Maybe<zc::Vector<CoreRoleSeedEntry>> reconstructRoles(
    const source::core::CoreDistributionInputRecord& distribution,
    const identity::ModuleKey& markerModule, const binder::NamedDefinitionInventory& inventory) {
  zc::Vector<CoreRoleSeedEntry> roles(distribution.record().roles().size());
  for (const auto& templateRole : distribution.record().roles()) {
    zc::Maybe<const binder::NamedDefinitionInventoryEntry&> matching;
    for (const auto& entry : inventory.entries()) {
      if (!matchesRoleTemplate(templateRole, markerModule, entry)) { continue; }
      if (matching != zc::none) { return zc::none; }
      matching = entry;
    }
    if (matching == zc::none) { return zc::none; }
    roles.add(CoreRoleSeedEntry{templateRole.role(), ZC_ASSERT_NONNULL(matching).key().clone()});
  }
  return roles;
}

bool roleDefinitionIsPublic(const module_graph_query::VerifiedBoundModule& bound,
                            const identity::DefinitionKey& key) {
  zc::Maybe<identity::DefId> definition;
  for (const auto& entry : bound.definitions().definitions()) {
    if (entry.key != key) { continue; }
    if (definition != zc::none) { return false; }
    definition = entry.definition;
  }
  if (definition == zc::none) { return false; }
  bool exported = false;
  for (const auto& entry : bound.bindingSurface().exports()) {
    const auto& target = entry.canonicalTarget.value();
    if (!target.is<binder::DefinitionBindingTarget>() ||
        target.get<binder::DefinitionBindingTarget>().definition != ZC_ASSERT_NONNULL(definition)) {
      continue;
    }
    if (!entry.visibility.value().is<binder::ExternalVisibility>() || exported) { return false; }
    exported = true;
  }
  return exported;
}

zc::Maybe<source::core::CoreSemanticRole> roleForDefinition(
    zc::ArrayPtr<const CoreRoleSeedEntry> roles, const identity::DefinitionKey& definition) {
  zc::Maybe<source::core::CoreSemanticRole> selected;
  for (const auto& role : roles) {
    if (role.definition != definition) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = role.role;
  }
  return selected;
}

bool matchesRoleSeedFailure(const query::TypedQueryResult<CoreRoleSeedRecord>& result,
                            CoreRoleSeedFailureKind kind,
                            zc::Maybe<source::core::CoreSemanticRole> role) {
  auto failure = CoreRoleSeedFailure::from(kind, role);
  return failure != zc::none && result.kind() == query::QueryValueKind::SemanticFailure &&
         result.semanticFailureBytes() == ZC_ASSERT_NONNULL(failure).encodeCanonical().asPtr();
}

zc::Maybe<CoreBootstrapModuleSurface> bootstrapSurface(const identity::ModuleKey& module,
                                                       const identity::CrateKey& core) {
  if (module.crate().encode().asPtr() != core.encode().asPtr() || module.path().size() == 0 ||
      module.path()[0].text() != "core"_zc) {
    return zc::none;
  }
  if (module.path().size() == 1) { return CoreBootstrapModuleSurface::Root; }
  if (module.path().size() != 2) { return zc::none; }
  if (module.path()[1].text() == "marker"_zc) { return CoreBootstrapModuleSurface::Marker; }
  if (module.path()[1].text() == "prelude"_zc) { return CoreBootstrapModuleSurface::Prelude; }
  return zc::none;
}

zc::Vector<binder::StableExportedBinding> cloneFinalBindings(
    zc::ArrayPtr<const binder::StableExportedBinding> bindings) {
  zc::Vector<binder::StableExportedBinding> cloned(bindings.size());
  for (const auto& binding : bindings) { cloned.add(binding.clone()); }
  return cloned;
}

zc::Maybe<binder::MemberVisibility> cloneVisibility(
    const zc::Maybe<binder::MemberVisibility>& visibility) {
  ZC_IF_SOME(value, visibility) { return value; }
  return zc::none;
}

zc::Maybe<CoreBindingSurfaceRevision> finalBindingSurfaceRevision(
    const identity::ModuleKey& module, const identity::CoreSemanticContextFingerprint& context,
    zc::ArrayPtr<const binder::StableExportedBinding> visibleBindings,
    zc::ArrayPtr<const binder::StableExportedBinding> exportedBindings) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(module.encode().asPtr());
  encoder.encodeDigest(context.digest());
  encoder.encodeSequenceSize(visibleBindings.size());
  for (const auto& binding : visibleBindings) {
    encoder.encodeByteString(
        binder::StableBindingCodec<binder::StableExportedBinding>::encode(binding).asPtr());
  }
  encoder.encodeSequenceSize(exportedBindings.size());
  for (const auto& binding : exportedBindings) {
    encoder.encodeByteString(
        binder::StableBindingCodec<binder::StableExportedBinding>::encode(binding).asPtr());
  }
  constexpr auto domain = "zom.core-binding-surface"_zc;
  auto payload = encoder.finish();
  zc::Vector<uint8_t> preimage(domain.size() + 1 + payload.size());
  preimage.addAll(domain.asBytes());
  preimage.add(0);
  preimage.addAll(payload.asPtr());
  auto digest = identity::sha256(preimage.asPtr());
  if (digest == zc::none) { return zc::none; }
  return CoreBindingSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(digest));
}

int compareCanonicalBytes(zc::ArrayPtr<const uint8_t> left,
                          zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

bool finalBindingsAreCanonical(zc::ArrayPtr<const binder::StableExportedBinding> bindings) {
  zc::Maybe<zc::Array<uint8_t>> previous;
  for (const auto& binding : bindings) {
    if (!binding.exported()) { return false; }
    auto encoded = binder::StableBindingCodec<binder::StableExportedBinding>::encode(binding);
    ZC_IF_SOME(last, previous) {
      if (compareCanonicalBytes(last.asPtr(), encoded.asPtr()) >= 0) { return false; }
    }
    previous = zc::mv(encoded);
  }
  return true;
}

bool sameBindingName(const binder::BindingNameKey& left, const binder::BindingNameKey& right) {
  return binder::StableBindingCodec<binder::BindingNameKey>::encode(left).asPtr() ==
         binder::StableBindingCodec<binder::BindingNameKey>::encode(right).asPtr();
}

bool finalSignaturesAreCanonical(
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> signatures) {
  zc::Maybe<zc::Array<uint8_t>> previous;
  for (const auto& signature : signatures) {
    auto encoded = signature.definition().encode();
    ZC_IF_SOME(last, previous) {
      if (compareCanonicalBytes(last.asPtr(), encoded.asPtr()) >= 0) { return false; }
    }
    previous = zc::mv(encoded);
  }
  return true;
}

zc::Maybe<zc::Vector<CoreFinalSignatureRoot>> finalSignatureRoots(
    const identity::ModuleKey& sourceModule, const CoreBindingSurfaceRevision& bindingSurface,
    zc::ArrayPtr<const binder::StableExportedBinding> bindings,
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> lookupDefinitions) {
  zc::Vector<CoreFinalSignatureRoot> roots;
  for (const auto& signature : lookupDefinitions) {
    zc::Maybe<const binder::StableExportedBinding&> binding;
    for (const auto& candidate : bindings) {
      const auto& target = candidate.canonicalTarget().value();
      if (!target.is<binder::StableDefinitionBindingTarget>() ||
          target.get<binder::StableDefinitionBindingTarget>().definition.definition() !=
              signature.definition()) {
        continue;
      }
      if (binding != zc::none) { return zc::none; }
      binding = candidate;
    }
    if (binding == zc::none) { return zc::none; }
    ZC_IF_SOME(value, binding) {
      roots.add(CoreFinalSignatureRoot{
          signature.definition().clone(), signature.definition().clone(),
          cloneVisibility(value.visibility()), sourceModule.clone(), bindingSurface.clone()});
    }
  }
  for (size_t index = 1; index < roots.size(); ++index) {
    if (compareCanonicalBytes(roots[index - 1].binding.encode().asPtr(),
                              roots[index].binding.encode().asPtr()) >= 0) {
      return zc::none;
    }
  }
  return roots;
}

zc::Maybe<zc::Vector<core::TypeFreeInterfaceSignatureRecord>> finalSignatures(
    CoreBootstrapModuleSurface surface, const core::VerifiedCoreSignatureFacts& facts,
    const VerifiedCoreRoleSeed& seed) {
  if (surface != CoreBootstrapModuleSurface::Marker) {
    return facts.facts().size() == 0
               ? zc::Maybe<zc::Vector<core::TypeFreeInterfaceSignatureRecord>>(
                     zc::Vector<core::TypeFreeInterfaceSignatureRecord>())
               : zc::none;
  }
  if (facts.facts().size() != seed.roles().size()) { return zc::none; }
  zc::TreeMap<zc::String, size_t> order;
  zc::Vector<core::TypeFreeInterfaceSignatureRecord> values(facts.facts().size());
  for (const auto& fact : facts.facts()) {
    zc::Maybe<const MaterializedCoreRoleSeedEntry&> role;
    for (const auto& candidate : seed.roles()) {
      if (candidate.role != fact.role) { continue; }
      if (role != zc::none) { return zc::none; }
      role = candidate;
    }
    if (role == zc::none || fact.signature.definition != ZC_ASSERT_NONNULL(role).definition) {
      return zc::none;
    }
    auto signature =
        core::TypeFreeInterfaceSignatureRecord::decodeCanonical(fact.canonical.asPtr());
    if (signature == zc::none ||
        ZC_ASSERT_NONNULL(signature).definition() != ZC_ASSERT_NONNULL(role).key) {
      return zc::none;
    }
    auto key = zc::encodeHex(ZC_ASSERT_NONNULL(signature).definition().encode().asPtr());
    if (order.find(key) != zc::none) { return zc::none; }
    values.add(zc::mv(ZC_ASSERT_NONNULL(signature)));
    order.insert(zc::mv(key), values.size() - 1);
  }
  zc::Vector<core::TypeFreeInterfaceSignatureRecord> sorted(values.size());
  for (const auto& entry : order) { sorted.add(values[entry.value].clone()); }
  return finalSignaturesAreCanonical(sorted.asPtr())
             ? zc::Maybe<zc::Vector<core::TypeFreeInterfaceSignatureRecord>>(zc::mv(sorted))
             : zc::none;
}

zc::Maybe<zc::Vector<binder::StableExportedBinding>> finalBindings(
    query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context,
    const identity::ModuleKey& module) {
  auto names = context.get<binder::ModuleExportNames>(module.clone());
  if (names.isRuntimeFailure() || names.kind() != query::QueryValueKind::Value) { return zc::none; }
  zc::Vector<binder::StableExportedBinding> bindings(names.value().values().size());
  for (const auto& name : names.value().values()) {
    auto binding = context.get<binder::ExportedBinding>(
        binder::StableExportedBindingQueryKey::from(module.clone(), name.clone()));
    if (binding.isRuntimeFailure() || binding.kind() != query::QueryValueKind::Value ||
        !sameBindingName(binding.value().name(), name) || !binding.value().exported()) {
      return zc::none;
    }
    bindings.add(binding.value().clone());
  }
  return finalBindingsAreCanonical(bindings.asPtr())
             ? zc::Maybe<zc::Vector<binder::StableExportedBinding>>(zc::mv(bindings))
             : zc::none;
}

bool graphIncludesModuleExactlyOnce(const CoreModuleGraphRecord& graph,
                                    const identity::ModuleKey& module) {
  size_t matches = 0;
  for (const auto& candidate : graph.modules()) {
    if (candidate.encode().asPtr() == module.encode().asPtr()) { ++matches; }
  }
  return matches == 1;
}

zc::Vector<CoreRoleSeedEntry> stableRoles(const VerifiedCoreRoleSeed& seed) {
  zc::Vector<CoreRoleSeedEntry> result(seed.roles().size());
  for (const auto& role : seed.roles()) {
    result.add(CoreRoleSeedEntry{role.role, role.key.clone()});
  }
  return result;
}

zc::Maybe<checker::CheckerIdentityAuthority> materializeCheckerIdentities(
    query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
    const incremental_binding_query::CompilationRootSetQueryKey& roots) {
  auto graph =
      context.getCapability<module_graph_query::MaterializeModuleGraphQuery>(roots.clone());
  if (!graph.isPublished()) { return zc::none; }
  zc::Vector<checker::CheckerIdentityAuthority::BoundModuleView> views;
  for (const auto& module : graph.lease().capability().modules()) {
    auto key =
        incremental_binding_query::ContextualModuleKey::from(roots.clone(), module.key().clone());
    auto bound = context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(key));
    if (!bound.isPublished()) { return zc::none; }
    auto view = module_graph_query::CheckerBoundModuleView::from(zc::mv(bound).takeLease());
    if (view == zc::none) { return zc::none; }
    views.add(zc::mv(ZC_ASSERT_NONNULL(view)));
  }
  return checker::CheckerIdentityAuthority::from(zc::mv(views));
}

zc::Maybe<core::VerifiedCoreImportedSignatureView> materializeCoreImportedSignatures(
    query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
    const ContextualCoreModuleKey& key,
    const query::QueryCapabilityLease<const module_graph_query::VerifiedBoundModule>& bound) {
  auto graphKey =
      ContextualCoreCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  if (graphKey == zc::none) { return zc::none; }
  auto graph = context.get<CoreModuleGraphQuery>(zc::mv(ZC_ASSERT_NONNULL(graphKey)));
  if (graph.isRuntimeFailure() || graph.kind() != query::QueryValueKind::Value) { return zc::none; }
  auto surface = bootstrapSurface(key.module(), graph.value().core());
  if (surface == zc::none) { return zc::none; }
  zc::Vector<core::VerifiedCoreImportedSignatureView::BootstrapInterfaceLease> sources;
  if (ZC_ASSERT_NONNULL(surface) == CoreBootstrapModuleSurface::Prelude) {
    zc::Maybe<identity::ModuleKey> marker;
    for (const auto& module : graph.value().modules()) {
      if (bootstrapSurface(module, graph.value().core()) != CoreBootstrapModuleSurface::Marker) {
        continue;
      }
      if (marker != zc::none) { return zc::none; }
      marker = module.clone();
    }
    if (marker == zc::none) { return zc::none; }
    auto markerKey = ContextualCoreModuleKey::from(key.contextRoots().clone(),
                                                   zc::mv(ZC_ASSERT_NONNULL(marker)));
    if (markerKey == zc::none) { return zc::none; }
    auto interface = context.getCapability<MaterializeCoreBootstrapModuleInterfaceQuery>(
        zc::mv(ZC_ASSERT_NONNULL(markerKey)));
    if (!interface.isPublished()) { return zc::none; }
    sources.add(zc::mv(interface).takeLease());
  }
  auto requester = module_graph_query::CheckerBoundModuleView::from(bound.retain());
  if (requester == zc::none) { return zc::none; }
  return core::VerifiedCoreImportedSignatureView::from(
      ZC_ASSERT_NONNULL(requester), graph.value().coreContext(), zc::mv(sources));
}

bool sameRoles(zc::ArrayPtr<const CoreRoleSeedEntry> left,
               zc::ArrayPtr<const CoreRoleSeedEntry> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].role != right[index].role ||
        left[index].definition != right[index].definition) {
      return false;
    }
  }
  return true;
}

zc::Maybe<identity::ModuleKey> findInitialModule(const CoreModuleGraphRecord& graph,
                                                 CoreBootstrapModuleSurface surface) {
  zc::Maybe<identity::ModuleKey> selected;
  for (const auto& module : graph.modules()) {
    auto candidate = bootstrapSurface(module, graph.core());
    if (candidate == zc::none || ZC_ASSERT_NONNULL(candidate) != surface) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = module.clone();
  }
  return selected;
}

bool hasExactPreludeDependency(const CoreModuleGraphRecord& graph,
                               const identity::ModuleKey& prelude,
                               const identity::ModuleKey& marker) {
  size_t matches = 0;
  for (const auto& edge : graph.edges()) {
    if (edge.requester().encode().asPtr() == prelude.encode().asPtr() &&
        edge.dependency().encode().asPtr() == marker.encode().asPtr()) {
      ++matches;
    }
  }
  return matches == 1;
}

}  // namespace

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

bool CoreLibraryQueryVerifier::verifyRoleSeed(
    query::QueryContext& context, const ContextualCoreCrateKey& key,
    const query::TypedQueryResult<CoreRoleSeedRecord>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  if (distribution.isRuntimeFailure() || graph.isRuntimeFailure()) { return false; }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == graph.semanticFailureBytes();
  }
  if (distribution.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value ||
      graph.value().core().encode().asPtr() != key.crate().encode().asPtr()) {
    return false;
  }
  auto marker = findMarkerModule(graph.value());
  if (marker == zc::none) { return false; }
  auto contextualMarker = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), ZC_ASSERT_NONNULL(marker).clone());
  auto bound =
      context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(contextualMarker));
  if (!bound.isPublished() || bound.lease().capability().module().encode().asPtr() !=
                                  ZC_ASSERT_NONNULL(marker).encode().asPtr()) {
    return false;
  }
  auto stableMarker =
      incremental_binding_query::StableModuleQueryKey::fromVerified(ZC_ASSERT_NONNULL(marker));
  if (stableMarker == zc::none) { return false; }
  auto definitions = context.get<incremental_binding_query::NamedDefinitionInventoryQuery>(
      zc::mv(ZC_ASSERT_NONNULL(stableMarker)));
  if (definitions.isRuntimeFailure()) { return false; }
  if (definitions.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == definitions.semanticFailureBytes();
  }
  if (definitions.kind() != query::QueryValueKind::Value) { return false; }
  auto failure =
      inventoryFailure(distribution.value(), ZC_ASSERT_NONNULL(marker), definitions.value());
  if (failure != zc::none) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == ZC_ASSERT_NONNULL(failure).encodeCanonical().asPtr();
  }
  auto roles =
      reconstructRoles(distribution.value(), ZC_ASSERT_NONNULL(marker), definitions.value());
  if (roles == zc::none) { return false; }
  for (const auto& entry : definitions.value().entries()) {
    auto role = roleForDefinition(ZC_ASSERT_NONNULL(roles).asPtr(), entry.key());
    if (role == zc::none) { return false; }
    auto definition = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(), binder::StableDefinitionQueryKey::from(
                                        ZC_ASSERT_NONNULL(marker).clone(), entry.key().clone()));
    auto syntax = context.get<incremental_binding_query::NamedItemSyntaxQuery>(zc::mv(definition));
    if (syntax.isRuntimeFailure()) { return false; }
    if (syntax.kind() == query::QueryValueKind::SemanticFailure) {
      return result.kind() == query::QueryValueKind::SemanticFailure &&
             result.semanticFailureBytes() == syntax.semanticFailureBytes();
    }
    if (syntax.kind() != query::QueryValueKind::Value ||
        syntax.value().owningModule().encode().asPtr() !=
            ZC_ASSERT_NONNULL(marker).encode().asPtr()) {
      return matchesRoleSeedFailure(result, CoreRoleSeedFailureKind::WrongRoleModule, role);
    }
    if (!core::isInitialMarkerInterface(syntax.value())) {
      return matchesRoleSeedFailure(result, CoreRoleSeedFailureKind::WrongRoleKind, role);
    }
  }
  if (result.kind() != query::QueryValueKind::Value) { return false; }
  for (const auto& role : ZC_ASSERT_NONNULL(roles)) {
    if (!roleDefinitionIsPublic(bound.lease().capability(), role.definition)) {
      return matchesRoleSeedFailure(result, CoreRoleSeedFailureKind::WrongRoleVisibility,
                                    role.role);
    }
  }
  auto expected = CoreRoleSeedRecord::from(
      key.crate().clone(), graph.value().coreContext().clone(), distribution.value().digest(),
      zc::mv(ZC_ASSERT_NONNULL(marker)), zc::mv(ZC_ASSERT_NONNULL(roles)));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
                                     result.value().encodeCanonical().asPtr();
}

bool CoreLibraryQueryVerifier::verifyBootstrapModuleInterfaceRecord(
    query::QueryContext& context, const ContextualCoreModuleKey& key,
    const query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto coreKey =
      ContextualCoreCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  if (coreKey == zc::none) { return false; }
  auto graph = context.get<CoreModuleGraphQuery>(ZC_ASSERT_NONNULL(coreKey).clone());
  if (graph.isRuntimeFailure()) { return false; }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == graph.semanticFailureBytes();
  }
  auto seed =
      context.getCapability<MaterializeCoreRoleSeedQuery>(zc::mv(ZC_ASSERT_NONNULL(coreKey)));
  auto boundKey = incremental_binding_query::ContextualModuleKey::from(key.contextRoots().clone(),
                                                                       key.module().clone());
  auto bound = context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(boundKey));
  if (result.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value || !seed.isPublished() || !bound.isPublished()) {
    return false;
  }
  auto surface = bootstrapSurface(key.module(), graph.value().core());
  if (surface == zc::none || !graphIncludesModuleExactlyOnce(graph.value(), key.module()) ||
      bound.lease().capability().contextRoots() != key.contextRoots() ||
      bound.lease().capability().module().encode().asPtr() != key.module().encode().asPtr() ||
      bound.lease().capability().crate() != seed.lease().capability().crate() ||
      bound.lease().capability().context() != seed.lease().capability().context() ||
      bound.lease().capability().fingerprint().digest() !=
          seed.lease().capability().fingerprint().digest() ||
      seed.lease().capability().coreContext().digest() != graph.value().coreContext().digest() ||
      !core::matchesInitialSurface(ZC_ASSERT_NONNULL(surface), bound.lease().capability(),
                                   seed.lease().capability())) {
    return false;
  }
  auto roles = stableRoles(seed.lease().capability());
  auto expected = CoreBootstrapModuleInterfaceRecord::from(
      graph.value().core().clone(), graph.value().coreContext().clone(),
      graph.value().revision().clone(), key.module().clone(),
      bound.lease().capability().bindingSurface().revision(),
      seed.lease().capability().revision().clone(), ZC_ASSERT_NONNULL(surface), zc::mv(roles));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
                                     result.value().encodeCanonical().asPtr();
}

bool CoreLibraryQueryVerifier::verifyExportSurface(
    query::QueryContext& context, const ContextualCoreModuleKey& key,
    const query::TypedQueryResult<CoreExportSurfaceRecord>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto interface = context.get<CoreBootstrapModuleInterfaceQuery>(key);
  if (interface.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value ||
      interface.kind() != query::QueryValueKind::Value ||
      interface.value().module().encode().asPtr() != key.module().encode().asPtr()) {
    return false;
  }
  zc::Vector<CoreRoleSeedEntry> defined;
  zc::Vector<CoreRoleSeedEntry> reexported;
  switch (interface.value().surface()) {
    case CoreBootstrapModuleSurface::Root:
      break;
    case CoreBootstrapModuleSurface::Marker:
      for (const auto& role : interface.value().roles()) { defined.add(role.clone()); }
      break;
    case CoreBootstrapModuleSurface::Prelude:
      for (const auto& role : interface.value().roles()) { reexported.add(role.clone()); }
      break;
  }
  auto expected = CoreExportSurfaceRecord::from(
      interface.value().core().clone(), interface.value().coreContext().clone(),
      interface.value().graphRevision().clone(), interface.value().module().clone(),
      interface.value().revision().clone(), zc::mv(defined), zc::mv(reexported));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
                                     result.value().encodeCanonical().asPtr();
}

bool CoreLibraryQueryVerifier::verifyPreludeSurface(
    query::QueryContext& context, const ContextualCoreCrateKey& key,
    const query::TypedQueryResult<CorePreludeSurfaceRecord>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  if (graph.isRuntimeFailure()) { return false; }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return result.kind() == query::QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == graph.semanticFailureBytes();
  }
  if (result.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value ||
      graph.value().core().encode().asPtr() != key.crate().encode().asPtr()) {
    return false;
  }
  auto marker = findInitialModule(graph.value(), CoreBootstrapModuleSurface::Marker);
  auto prelude = findInitialModule(graph.value(), CoreBootstrapModuleSurface::Prelude);
  if (marker == zc::none || prelude == zc::none ||
      !hasExactPreludeDependency(graph.value(), ZC_ASSERT_NONNULL(prelude),
                                 ZC_ASSERT_NONNULL(marker))) {
    return false;
  }
  auto markerKey =
      ContextualCoreModuleKey::from(key.contextRoots().clone(), ZC_ASSERT_NONNULL(marker).clone());
  auto preludeKey =
      ContextualCoreModuleKey::from(key.contextRoots().clone(), ZC_ASSERT_NONNULL(prelude).clone());
  if (markerKey == zc::none || preludeKey == zc::none) { return false; }
  auto markerExport = context.get<CoreExportSurfaceQuery>(zc::mv(ZC_ASSERT_NONNULL(markerKey)));
  auto preludeExport = context.get<CoreExportSurfaceQuery>(zc::mv(ZC_ASSERT_NONNULL(preludeKey)));
  if (markerExport.isRuntimeFailure() || preludeExport.isRuntimeFailure() ||
      markerExport.kind() != query::QueryValueKind::Value ||
      preludeExport.kind() != query::QueryValueKind::Value ||
      markerExport.value().definedRoles().size() != 2 ||
      preludeExport.value().reexportedRoles().size() != 2 ||
      markerExport.value().reexportedRoles().size() != 0 ||
      preludeExport.value().definedRoles().size() != 0 ||
      !sameRoles(markerExport.value().definedRoles(), preludeExport.value().reexportedRoles())) {
    return false;
  }
  zc::Vector<CoreRoleSeedEntry> roles(markerExport.value().definedRoles().size());
  for (const auto& role : markerExport.value().definedRoles()) { roles.add(role.clone()); }
  auto expected = CorePreludeSurfaceRecord::from(
      graph.value().core().clone(), graph.value().coreContext().clone(),
      graph.value().revision().clone(), ZC_ASSERT_NONNULL(marker).clone(),
      ZC_ASSERT_NONNULL(prelude).clone(), markerExport.value().revision().clone(),
      preludeExport.value().revision().clone(), zc::mv(roles));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
                                     result.value().encodeCanonical().asPtr();
}

bool CoreLibraryQueryVerifier::verifyRoleAuthority(
    query::QueryContext& context, const ContextualCoreCrateKey& key,
    const query::TypedQueryResult<CoreRoleAuthorityRecord>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto seed = context.getCapability<MaterializeCoreRoleSeedQuery>(key.clone());
  auto prelude = context.get<CorePreludeSurfaceQuery>(key.clone());
  if (distribution.isRuntimeFailure() || prelude.isRuntimeFailure() ||
      result.kind() != query::QueryValueKind::Value ||
      distribution.kind() != query::QueryValueKind::Value || !seed.isPublished() ||
      prelude.kind() != query::QueryValueKind::Value ||
      prelude.value().core().encode().asPtr() != key.crate().encode().asPtr() ||
      seed.lease().capability().coreContext().digest() != prelude.value().coreContext().digest() ||
      seed.lease().capability().distribution() != distribution.value().digest()) {
    return false;
  }
  auto roles = stableRoles(seed.lease().capability());
  if (!sameRoles(roles.asPtr(), prelude.value().roles())) { return false; }
  auto expected =
      CoreRoleAuthorityRecord::from(key.crate().clone(), prelude.value().coreContext().clone(),
                                    distribution.value().policyTemplate().revision(),
                                    seed.lease().capability().revision().clone(),
                                    prelude.value().revision().clone(), zc::mv(roles));
  return expected != zc::none && ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
                                     result.value().encodeCanonical().asPtr();
}

zc::Maybe<zc::Array<uint8_t>> CoreLibraryQueryVerifier::verifyCoreAuthority(
    query::CapabilityQueryContext<MaterializeCoreAuthorityQuery>& context,
    const ContextualCoreCrateKey& key, const VerifiedCoreAuthorityBundle& candidate) {
  auto record = context.get<CoreRoleAuthorityQuery>(key.clone());
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto seed = context.getCapability<MaterializeCoreRoleSeedQuery>(key.clone());
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  if (record.isRuntimeFailure() || distribution.isRuntimeFailure() || graph.isRuntimeFailure() ||
      record.kind() != query::QueryValueKind::Value ||
      distribution.kind() != query::QueryValueKind::Value || !seed.isPublished() ||
      graph.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto preludeModule = findInitialModule(graph.value(), CoreBootstrapModuleSurface::Prelude);
  if (preludeModule == zc::none) { return zc::none; }
  auto authorityPrelude = ZC_ASSERT_NONNULL(preludeModule).clone();
  auto preludeKey = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), zc::mv(ZC_ASSERT_NONNULL(preludeModule)));
  auto prelude =
      context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(preludeKey));
  if (!prelude.isPublished()) { return zc::none; }
  auto expected = VerifiedCoreAuthorityBundle::from(
      seed.lease().capability().context(), seed.lease().capability().fingerprint().clone(),
      record.value().clone(), distribution.value().policyTemplate().clone(),
      zc::mv(authorityPrelude), zc::mv(seed).takeLease(), zc::mv(prelude).takeLease());
  if (expected == zc::none || candidate.encodeCanonical().asPtr() !=
                                  ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr()) {
    return zc::none;
  }
  return candidate.encodeCanonical();
}

zc::Maybe<zc::Array<uint8_t>> CoreLibraryQueryVerifier::verifyFinalCoreModuleInterface(
    query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context,
    const ContextualCoreModuleKey& key, const VerifiedCoreModuleInterface& candidate) {
  auto coreKey =
      ContextualCoreCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  if (coreKey == zc::none) { return zc::none; }
  auto bootstrap = context.getCapability<MaterializeCoreBootstrapModuleInterfaceQuery>(key.clone());
  auto authority =
      context.getCapability<MaterializeCoreAuthorityQuery>(ZC_ASSERT_NONNULL(coreKey).clone());
  auto seedRecord = context.get<CoreRoleSeedQuery>(ZC_ASSERT_NONNULL(coreKey).clone());
  if (!bootstrap.isPublished() || !authority.isPublished() || seedRecord.isRuntimeFailure() ||
      seedRecord.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  const auto& bootstrapValue = bootstrap.lease().capability();
  const auto& authorityValue = authority.lease().capability();
  const auto& bound = bootstrapValue.boundModuleLease().capability();
  const auto& seed = bootstrapValue.roleSeedLease().capability();
  if (bootstrapValue.record().module().encode().asPtr() != key.module().encode().asPtr() ||
      authorityValue.context() != bootstrapValue.context() ||
      authorityValue.fingerprint().digest() != bootstrapValue.fingerprint().digest() ||
      authorityValue.record().coreContext().digest() !=
          bootstrapValue.record().coreContext().digest() ||
      authorityValue.record().roleSeedRevision().digest() != seed.revision().digest() ||
      seedRecord.value().revision().digest() != seed.revision().digest() ||
      !core::matchesInitialSurface(bootstrapValue.record().surface(), bound, seed)) {
    return zc::none;
  }
  zc::Vector<core::CoreMarkerShapeEntry> roles;
  if (bootstrapValue.record().surface() == CoreBootstrapModuleSurface::Marker) {
    if (authorityValue.shapes().shapes().size() != 2 ||
        authorityValue.shapes().shapes()[0].role != source::core::CoreSemanticRole::Copy ||
        authorityValue.shapes().shapes()[1].role != source::core::CoreSemanticRole::Linear ||
        authorityValue.shapes().shapes()[0].shape !=
            checker::signature::InterfaceMarkerShape::ClosedMarker ||
        authorityValue.shapes().shapes()[1].shape !=
            checker::signature::InterfaceMarkerShape::ClosedMarker) {
      return zc::none;
    }
    roles = zc::Vector<core::CoreMarkerShapeEntry>(authorityValue.shapes().shapes().size());
    for (const auto& role : authorityValue.shapes().shapes()) { roles.add(role.clone()); }
  }
  auto bindings = finalBindings(context, key.module());
  if (bindings == zc::none) { return zc::none; }
  auto lookupDefinitions =
      finalSignatures(bootstrapValue.record().surface(), bootstrapValue.signatures(), seed);
  if (lookupDefinitions == zc::none) { return zc::none; }
  identity::ModuleKey signatureSource = key.module().clone();
  zc::Maybe<CoreBindingSurfaceRevision> sourceSurface;
  if (bootstrapValue.record().surface() == CoreBootstrapModuleSurface::Prelude) {
    auto markerKey = ContextualCoreModuleKey::from(key.contextRoots().clone(),
                                                   seedRecord.value().markerModule().clone());
    if (markerKey == zc::none) { return zc::none; }
    auto marker = context.getCapability<FinalizeCoreModuleInterfaceQuery>(
        zc::mv(ZC_ASSERT_NONNULL(markerKey)));
    if (!marker.isPublished() || marker.lease().capability().record().module().encode().asPtr() !=
                                     seedRecord.value().markerModule().encode().asPtr()) {
      return zc::none;
    }
    auto signatures = marker.lease().capability().record().lookupDefinitions();
    zc::Vector<core::TypeFreeInterfaceSignatureRecord> cloned(signatures.size());
    for (const auto& signature : signatures) { cloned.add(signature.clone()); }
    lookupDefinitions = zc::mv(cloned);
    signatureSource = marker.lease().capability().record().module().clone();
    sourceSurface = marker.lease().capability().record().bindingSurfaceRevision().clone();
  }
  auto visibleBindings = cloneFinalBindings(ZC_ASSERT_NONNULL(bindings).asPtr());
  zc::Vector<core::TypeFreeInterfaceSignatureRecord> supportDefinitions;
  auto bindingSurface =
      finalBindingSurfaceRevision(key.module(), bootstrapValue.record().coreContext(),
                                  visibleBindings.asPtr(), ZC_ASSERT_NONNULL(bindings).asPtr());
  if (bindingSurface == zc::none) { return zc::none; }
  CoreBindingSurfaceRevision signatureSurface = ZC_ASSERT_NONNULL(bindingSurface).clone();
  ZC_IF_SOME(value, sourceSurface) { signatureSurface = value.clone(); }
  auto signatureRoots =
      finalSignatureRoots(signatureSource, signatureSurface, ZC_ASSERT_NONNULL(bindings).asPtr(),
                          ZC_ASSERT_NONNULL(lookupDefinitions).asPtr());
  if (signatureRoots == zc::none) { return zc::none; }
  auto record = CoreModuleInterfaceRecord::from(
      key.module().clone(), bootstrapValue.record().coreContext().clone(), zc::mv(visibleBindings),
      zc::mv(ZC_ASSERT_NONNULL(bindings)), zc::mv(ZC_ASSERT_NONNULL(lookupDefinitions)),
      zc::mv(supportDefinitions), zc::mv(ZC_ASSERT_NONNULL(signatureRoots)),
      zc::Vector<CoreCanonicalModuleTarget>(), zc::mv(roles),
      authorityValue.authority().revision().clone());
  if (record == zc::none) { return zc::none; }
  auto expected = VerifiedCoreModuleInterface::from(
      bootstrapValue.context(), bootstrapValue.fingerprint().clone(), bound.definitions().module(),
      zc::mv(ZC_ASSERT_NONNULL(record)));
  if (expected == zc::none || candidate.encodeCanonical().asPtr() !=
                                  ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr()) {
    return zc::none;
  }
  return candidate.encodeCanonical();
}

zc::Maybe<zc::Array<uint8_t>> CoreLibraryQueryVerifier::verifyBootstrapModuleInterface(
    query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
    const ContextualCoreModuleKey& key, const VerifiedCoreBootstrapModuleInterface& candidate) {
  auto coreKey =
      ContextualCoreCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  if (coreKey == zc::none) { return zc::none; }
  auto graph = context.get<CoreModuleGraphQuery>(ZC_ASSERT_NONNULL(coreKey).clone());
  auto seed =
      context.getCapability<MaterializeCoreRoleSeedQuery>(zc::mv(ZC_ASSERT_NONNULL(coreKey)));
  auto boundKey = incremental_binding_query::ContextualModuleKey::from(key.contextRoots().clone(),
                                                                       key.module().clone());
  auto bound = context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(boundKey));
  if (graph.isRuntimeFailure() || graph.kind() != query::QueryValueKind::Value ||
      !seed.isPublished() || !bound.isPublished()) {
    return zc::none;
  }
  auto surface = bootstrapSurface(key.module(), graph.value().core());
  if (surface == zc::none || !graphIncludesModuleExactlyOnce(graph.value(), key.module()) ||
      bound.lease().capability().contextRoots() != key.contextRoots() ||
      bound.lease().capability().module().encode().asPtr() != key.module().encode().asPtr() ||
      bound.lease().capability().crate() != seed.lease().capability().crate() ||
      bound.lease().capability().context() != seed.lease().capability().context() ||
      bound.lease().capability().fingerprint().digest() !=
          seed.lease().capability().fingerprint().digest() ||
      seed.lease().capability().coreContext().digest() != graph.value().coreContext().digest() ||
      !core::matchesInitialSurface(ZC_ASSERT_NONNULL(surface), bound.lease().capability(),
                                   seed.lease().capability())) {
    return zc::none;
  }
  auto roles = stableRoles(seed.lease().capability());
  auto expected = CoreBootstrapModuleInterfaceRecord::from(
      graph.value().core().clone(), graph.value().coreContext().clone(),
      graph.value().revision().clone(), key.module().clone(),
      bound.lease().capability().bindingSurface().revision(),
      seed.lease().capability().revision().clone(), ZC_ASSERT_NONNULL(surface), zc::mv(roles));
  auto identities = materializeCheckerIdentities(context, key.contextRoots());
  if (identities == zc::none) { return zc::none; }
  auto signatures = core::VerifiedCoreSignatureFacts::from(
      ZC_ASSERT_NONNULL(surface), bound.lease().capability(), seed.lease().capability(),
      ZC_ASSERT_NONNULL(identities));
  auto imported = materializeCoreImportedSignatures(context, key, bound.lease());
  if (expected == zc::none || candidate.context() != bound.lease().capability().context() ||
      candidate.fingerprint().digest() != bound.lease().capability().fingerprint().digest() ||
      candidate.boundModuleLease().stableWitness() != bound.lease().stableWitness() ||
      candidate.roleSeedLease().stableWitness() != seed.lease().stableWitness() ||
      signatures == zc::none ||
      candidate.signatures().encodeCanonical().asPtr() !=
          ZC_ASSERT_NONNULL(signatures).encodeCanonical().asPtr() ||
      imported == zc::none ||
      candidate.importedSignatures().encodeCanonical().asPtr() !=
          ZC_ASSERT_NONNULL(imported).encodeCanonical().asPtr() ||
      !sameRoles(candidate.record().roles(), ZC_ASSERT_NONNULL(expected).roles()) ||
      candidate.record().encodeCanonical().asPtr() !=
          ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr()) {
    return zc::none;
  }
  return candidate.encodeCanonical();
}

}  // namespace zomlang::compiler::driver::core_library_query
