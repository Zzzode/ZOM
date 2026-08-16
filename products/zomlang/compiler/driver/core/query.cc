// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core/query.h"

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/binder/surface/module-body-syntax.h"
#include "zomlang/compiler/binder/graph/module-skeleton-query.h"
#include "zomlang/compiler/binder/stable/stable-binding-codec.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/driver/core/verifier.h"
#include "zomlang/compiler/driver/core/marker-authority.h"
#include "zomlang/compiler/driver/core/role-seed-failure.h"
#include "zomlang/compiler/driver/core/signature.h"
#include "zomlang/compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/query/module-graph/module-graph-query-input.h"
#include "zomlang/compiler/driver/query/binding/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/query/binding/named-item-query.h"
#include "zomlang/compiler/driver/package/canonical-package-compilation-request.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::driver::core_library_query {
namespace {

constexpr zc::StringPtr kCoreDistributionDomain = "zom.query.core-distribution"_zc;
constexpr zc::StringPtr kCoreDistributionValueDomain = "zom.query.core-distribution-value"_zc;
constexpr zc::StringPtr kCoreModuleGraphValueDomain = "zom.query.core-module-graph-value"_zc;
constexpr zc::StringPtr kCoreModuleGraphRevisionDomain = "zom.core-module-graph"_zc;
constexpr zc::StringPtr kCoreRoleSeedValueDomain = "zom.query.core-role-seed-value"_zc;
constexpr zc::StringPtr kCoreRoleSeedRevisionDomain = "zom.core-role-seed"_zc;
constexpr zc::StringPtr kVerifiedCoreRoleSeedWitnessDomain =
    "zom.query.verified-core-role-seed-witness"_zc;
constexpr zc::StringPtr kCoreBootstrapInterfaceValueDomain =
    "zom.query.core-bootstrap-module-interface-value"_zc;
constexpr zc::StringPtr kCoreBootstrapInterfaceRevisionDomain =
    "zom.core-bootstrap-module-interface"_zc;
constexpr zc::StringPtr kCoreExportSurfaceValueDomain = "zom.query.core-export-surface-value"_zc;
constexpr zc::StringPtr kCoreExportSurfaceRevisionDomain = "zom.core-export-surface"_zc;
constexpr zc::StringPtr kCorePreludeSurfaceValueDomain = "zom.query.core-prelude-surface-value"_zc;
constexpr zc::StringPtr kCorePreludeSurfaceRevisionDomain = "zom.core-prelude-surface"_zc;
constexpr zc::StringPtr kCoreRoleAuthorityValueDomain = "zom.query.core-role-authority-value"_zc;
constexpr zc::StringPtr kCoreRoleAuthorityRevisionDomain = "zom.core-role-authority"_zc;
constexpr zc::StringPtr kVerifiedCoreBootstrapInterfaceWitnessDomain =
    "zom.query.verified-core-bootstrap-module-interface-witness"_zc;
constexpr zc::StringPtr kCoreDistributionTransactionDomain =
    "zom.query.input-transaction.core-distribution"_zc;
constexpr size_t kMaximumCoreDistributionKeyBytes = 256;
constexpr size_t kMaximumCoreDistributionValueBytes = 512 * 1024;
constexpr size_t kMaximumContextRootBytes = 64 * 1024 * 1024;
constexpr size_t kMaximumCrateOrModuleKeyBytes = 64 * 1024;
constexpr size_t kMaximumCoreModules = 4096;
constexpr size_t kMaximumCoreEdges = 1024 * 1024;
constexpr size_t kMaximumCoreGraphBytes = 128 * 1024 * 1024;
constexpr size_t kMaximumCoreRoleSeedBytes = 1024 * 1024;
constexpr size_t kMaximumCoreBootstrapInterfaceBytes = 1024 * 1024;
constexpr size_t kMaximumCoreExportSurfaceBytes = 1024 * 1024;
constexpr size_t kMaximumCorePreludeSurfaceBytes = 1024 * 1024;
constexpr size_t kMaximumCoreRoleAuthorityBytes = 1024 * 1024;

zc::Array<uint8_t> frame(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> payload) {
  auto result = zc::heapArray<uint8_t>(domain.size() + 1 + payload.size());
  size_t cursor = 0;
  for (const auto byte : domain.asBytes()) { result[cursor++] = byte; }
  result[cursor++] = 0;
  for (const auto byte : payload) { result[cursor++] = byte; }
  return result;
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> unframe(zc::StringPtr domain,
                                               zc::ArrayPtr<const uint8_t> bytes,
                                               size_t maximumBytes) {
  const size_t prefixSize = domain.size() + 1;
  if (bytes.size() <= prefixSize || bytes.size() > maximumBytes ||
      bytes.slice(0, domain.size()) != domain.asBytes() || bytes[domain.size()] != 0) {
    return zc::none;
  }
  return bytes.slice(prefixSize, bytes.size());
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

template <typename T, typename Bytes>
bool canonicalizePayloadValues(zc::Vector<T>& values, Bytes bytes) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           compareCanonicalBytes(bytes(current), bytes(values[insertion - 1])) < 0) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
  for (size_t index = 1; index < values.size(); ++index) {
    if (bytes(values[index - 1]).asPtr() == bytes(values[index]).asPtr()) { return false; }
  }
  return true;
}

zc::Maybe<identity::CrateKey> decodePayloadCrate(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumCrateOrModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(crate).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(crate));
}

bool contextContainsCoreCrate(
    const incremental_binding_query::CompilationRootSetQueryKey& contextRoots,
    const identity::CrateKey& crate) {
  if (crate.unit().kind() != identity::CompilationUnitKind::Toolchain ||
      crate.unit().toolchain().component() != identity::ToolchainComponent::Core ||
      crate.targetKind() != identity::CrateTargetKind::Library || crate.targetName() != "core"_zc ||
      crate.compilation().hasBuildScriptProducer() ||
      crate.semanticOptions().editionYear() != 2026) {
    return false;
  }
  const auto crateBytes = crate.encode();
  size_t occurrences = 0;
  for (const auto& root : contextRoots.roots()) {
    if (root.kind() == incremental_binding_query::CompilationRootKind::ToolchainCore &&
        root.toolchainCore().canonicalCrateBytes() == crateBytes.asPtr()) {
      ++occurrences;
    }
  }
  return occurrences == 1;
}

bool samePath(const identity::CanonicalRelativePath& left,
              const identity::CanonicalRelativePath& right) {
  if (left.segments().size() != right.segments().size()) { return false; }
  for (size_t index = 0; index < left.segments().size(); ++index) {
    if (left.segments()[index].text() != right.segments()[index].text()) { return false; }
  }
  return true;
}

bool verifiedDistributionMatchesAccepted(
    const source::core::VerifiedCoreDistribution& distribution,
    const source::core::CoreDistributionInputRecord& accepted) {
  auto candidate = source::core::CoreDistributionInputRecord::from(
      distribution.record().clone(), distribution.distributionDigest(),
      distribution.policyTemplate().clone());
  return candidate != zc::none &&
         ZC_ASSERT_NONNULL(candidate).encode().asPtr() == accepted.encode().asPtr();
}

zc::Array<uint8_t> encodeCoreGraphPayload(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& coreContext,
    zc::Maybe<const CoreModuleGraphRevision&> revision,
    zc::ArrayPtr<const identity::ModuleKey> modules,
    zc::ArrayPtr<const module_graph_query::ModuleDependencyEdgeKey> edges) {
  identity::CanonicalEncoder encoder;
  const auto coreBytes = core.encode();
  encoder.encodeByteString(coreBytes.asPtr());
  encoder.encodeDigest(coreContext.digest());
  ZC_IF_SOME(value, revision) { encoder.encodeDigest(value.digest()); }
  encoder.encodeSequenceSize(modules.size());
  for (const auto& module : modules) {
    const auto bytes = module.encode();
    encoder.encodeByteString(bytes.asPtr());
  }
  encoder.encodeSequenceSize(edges.size());
  for (const auto& edge : edges) {
    const auto bytes = edge.encodeCanonical();
    encoder.encodeByteString(bytes.asPtr());
  }
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeCoreGraphRevision(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& coreContext,
    zc::ArrayPtr<const identity::ModuleKey> modules,
    zc::ArrayPtr<const module_graph_query::ModuleDependencyEdgeKey> edges) {
  zc::Maybe<const CoreModuleGraphRevision&> noRevision;
  const auto payload = encodeCoreGraphPayload(core, coreContext, noRevision, modules, edges);
  const auto preimage = frame(kCoreModuleGraphRevisionDomain, payload.asPtr());
  return identity::sha256(preimage.asPtr());
}

bool isCoreRole(source::core::CoreSemanticRole role) noexcept {
  switch (role) {
    case source::core::CoreSemanticRole::Copy:
    case source::core::CoreSemanticRole::Linear:
      return true;
  }
  return false;
}

bool isCoreMarkerModule(const identity::ModuleKey& module, const identity::CrateKey& core) {
  return module.crate().encode().asPtr() == core.encode().asPtr() && module.path().size() == 2 &&
         module.path()[0].text() == "core"_zc && module.path()[1].text() == "marker"_zc;
}

zc::Array<uint8_t> encodeCoreRoleSeedPayload(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& coreContext,
    const identity::Sha256Digest& distribution, const identity::ModuleKey& markerModule,
    zc::Maybe<const CoreRoleSeedRevision&> revision, zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  identity::CanonicalEncoder encoder;
  const auto coreBytes = core.encode();
  const auto markerBytes = markerModule.encode();
  encoder.encodeByteString(coreBytes.asPtr());
  encoder.encodeDigest(coreContext.digest());
  encoder.encodeDigest(distribution);
  encoder.encodeByteString(markerBytes.asPtr());
  ZC_IF_SOME(value, revision) { encoder.encodeDigest(value.digest()); }
  encoder.encodeSequenceSize(roles.size());
  for (const auto& role : roles) {
    encoder.encodeUint8(static_cast<uint8_t>(role.role));
    role.definition.encode(encoder);
  }
  return encoder.finish();
}

zc::Array<uint8_t> encodeCoreRoleSeedRevisionPayload(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& coreContext,
    const identity::ModuleKey& markerModule, zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  identity::CanonicalEncoder encoder;
  const auto coreBytes = core.encode();
  const auto markerBytes = markerModule.encode();
  encoder.encodeByteString(coreBytes.asPtr());
  encoder.encodeDigest(coreContext.digest());
  encoder.encodeByteString(markerBytes.asPtr());
  encoder.encodeSequenceSize(roles.size());
  for (const auto& role : roles) {
    encoder.encodeUint8(static_cast<uint8_t>(role.role));
    role.definition.encode(encoder);
  }
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeCoreRoleSeedRevision(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& coreContext,
    const identity::ModuleKey& markerModule, zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  const auto payload = encodeCoreRoleSeedRevisionPayload(core, coreContext, markerModule, roles);
  const auto preimage = frame(kCoreRoleSeedRevisionDomain, payload.asPtr());
  return identity::sha256(preimage.asPtr());
}

bool roleEntriesAreCanonical(zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  if (roles.size() != 2) { return false; }
  for (size_t index = 0; index < roles.size(); ++index) {
    if (!isCoreRole(roles[index].role) ||
        (index != 0 &&
         static_cast<uint8_t>(roles[index - 1].role) >= static_cast<uint8_t>(roles[index].role))) {
      return false;
    }
  }
  return roles[0].role == source::core::CoreSemanticRole::Copy &&
         roles[1].role == source::core::CoreSemanticRole::Linear;
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

zc::Maybe<identity::ModuleKey> markerModuleFromGraph(const CoreModuleGraphRecord& graph) {
  zc::Maybe<identity::ModuleKey> marker;
  for (const auto& module : graph.modules()) {
    if (!isCoreMarkerModule(module, graph.core())) { continue; }
    if (marker != zc::none) { return zc::none; }
    marker = module.clone();
  }
  return marker;
}

zc::Maybe<CoreRoleSeedFailure> roleSeedInventoryFailure(
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

zc::Maybe<zc::Vector<CoreRoleSeedEntry>> seedEntriesFromInventory(
    const source::core::CoreDistributionInputRecord& distribution,
    const identity::ModuleKey& markerModule, const binder::NamedDefinitionInventory& inventory) {
  zc::Vector<CoreRoleSeedEntry> roles(distribution.record().roles().size());
  for (const auto& templateRole : distribution.record().roles()) {
    zc::Maybe<const binder::NamedDefinitionInventoryEntry&> selected;
    for (const auto& entry : inventory.entries()) {
      if (!matchesRoleTemplate(templateRole, markerModule, entry)) { continue; }
      if (selected != zc::none) { return zc::none; }
      selected = entry;
    }
    if (selected == zc::none) { return zc::none; }
    roles.add(CoreRoleSeedEntry{templateRole.role(), ZC_ASSERT_NONNULL(selected).key().clone()});
  }
  if (!roleEntriesAreCanonical(roles.asPtr())) { return zc::none; }
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

query::TypedQueryResult<CoreRoleSeedRecord> roleSeedRejected(
    CoreRoleSeedFailureKind kind, zc::Maybe<source::core::CoreSemanticRole> role) {
  auto failure = CoreRoleSeedFailure::from(kind, role);
  if (failure == zc::none) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<CoreRoleSeedRecord>::semanticFailure(
      ZC_ASSERT_NONNULL(failure).encodeCanonical());
}

bool materializedRolesAreCanonical(identity::SemanticContextBrand context,
                                   zc::ArrayPtr<const MaterializedCoreRoleSeedEntry> roles) {
  if (roles.size() != 2) { return false; }
  for (size_t index = 0; index < roles.size(); ++index) {
    if (!isCoreRole(roles[index].role) || !roles[index].definition.belongsTo(context) ||
        (index != 0 &&
         static_cast<uint8_t>(roles[index - 1].role) >= static_cast<uint8_t>(roles[index].role))) {
      return false;
    }
  }
  return roles[0].role == source::core::CoreSemanticRole::Copy &&
         roles[1].role == source::core::CoreSemanticRole::Linear;
}

bool materializedRolesMatchBound(const module_graph_query::VerifiedBoundModule& bound,
                                 zc::ArrayPtr<const MaterializedCoreRoleSeedEntry> roles) {
  for (const auto& role : roles) {
    zc::Maybe<const binder::MaterializedDefinitionInventoryEntry&> definition;
    for (const auto& entry : bound.definitions().definitions()) {
      if (entry.key != role.key) { continue; }
      if (definition != zc::none) { return false; }
      definition = entry;
    }
    if (definition == zc::none || ZC_ASSERT_NONNULL(definition).definition != role.definition ||
        identity::DefinitionKey::compute(ZC_ASSERT_NONNULL(definition).record) != role.key ||
        !roleDefinitionIsPublic(bound, role.key)) {
      return false;
    }
  }
  return true;
}

bool sameModuleSequence(zc::ArrayPtr<const identity::ModuleKey> left,
                        zc::ArrayPtr<const identity::ModuleKey> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].encode().asPtr() != right[index].encode().asPtr()) { return false; }
  }
  return true;
}

query::TypedQueryResult<CoreModuleGraphRecord> provideCoreModuleGraph(
    query::QueryContext& context, const ContextualCoreCrateKey& key) {
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  if (distribution.isRuntimeFailure()) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        distribution.runtimeFailure());
  }
  if (distribution.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto singleton =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(key.crate());
  auto stableCrate = incremental_binding_query::StableCrateQueryKey::fromVerified(key.crate());
  if (singleton == zc::none || stableCrate == zc::none) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto activeCrates =
      context.get<incremental_binding_query::ActiveCratesQuery>(ZC_ASSERT_NONNULL(singleton));
  auto activeSources =
      context.get<incremental_binding_query::ActiveSourcesQuery>(ZC_ASSERT_NONNULL(stableCrate));
  auto activeModules = context.get<module_graph_query::ActiveModulesQuery>(key.crate());
  auto graph = context.get<module_graph_query::ModuleGraphQuery>(ZC_ASSERT_NONNULL(singleton));
  if (activeCrates.isRuntimeFailure() || activeSources.isRuntimeFailure() ||
      activeModules.isRuntimeFailure() || graph.isRuntimeFailure()) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        activeCrates.isRuntimeFailure()    ? activeCrates.runtimeFailure()
        : activeSources.isRuntimeFailure() ? activeSources.runtimeFailure()
        : activeModules.isRuntimeFailure() ? activeModules.runtimeFailure()
                                           : graph.runtimeFailure());
  }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::semanticFailure(
        zc::heapArray<uint8_t>(graph.semanticFailureBytes()));
  }
  if (activeCrates.kind() != query::QueryValueKind::Value ||
      activeSources.kind() != query::QueryValueKind::Value ||
      activeModules.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value || activeCrates.value().crates().size() != 1 ||
      activeCrates.value().crates()[0].canonicalCrateBytes() != key.crate().encode().asPtr() ||
      !sameModuleSequence(activeModules.value().modules(), graph.value().modules())) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  if (activeSources.value().sources().size() != distribution.value().record().files().size()) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  for (const auto& file : distribution.value().record().files()) {
    auto source = identity::SourceFileKey::from(
        key.crate().clone(), identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                                                 file.path().clone()));
    auto stable = identity::source_query::StableSourceQueryKey::fromVerified(source);
    if (stable == zc::none || !activeSources.value().contains(ZC_ASSERT_NONNULL(stable))) {
      return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
  }

  zc::Vector<identity::ModuleKey> modules(graph.value().modules().size());
  zc::Vector<module_graph_query::ModuleDependencyEdgeKey> edges;
  for (const auto& module : graph.value().modules()) {
    if (module.crate().encode().asPtr() != key.crate().encode().asPtr()) {
      return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    auto dependencies = context.get<module_graph_query::ModuleDependenciesQuery>(module);
    if (dependencies.isRuntimeFailure()) {
      return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
          dependencies.runtimeFailure());
    }
    if (dependencies.kind() == query::QueryValueKind::SemanticFailure) {
      return query::TypedQueryResult<CoreModuleGraphRecord>::semanticFailure(
          zc::heapArray<uint8_t>(dependencies.semanticFailureBytes()));
    }
    if (dependencies.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    for (const auto& dependency : dependencies.value().dependencies()) {
      auto edge =
          module_graph_query::ModuleDependencyEdgeKey::from(module.clone(), dependency.clone());
      if (edge == zc::none) {
        return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      edges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
    }
    modules.add(module.clone());
  }
  auto coreContext = identity::CoreSemanticContextFingerprint::compute(key.crate());
  if (coreContext == zc::none) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto record = CoreModuleGraphRecord::from(
      key.crate().clone(), zc::mv(ZC_ASSERT_NONNULL(coreContext)), zc::mv(modules), zc::mv(edges));
  if (record == zc::none) {
    return query::TypedQueryResult<CoreModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<CoreModuleGraphRecord>::value(zc::mv(ZC_ASSERT_NONNULL(record)));
}

query::TypedQueryResult<CoreRoleSeedRecord> provideCoreRoleSeed(query::QueryContext& context,
                                                                const ContextualCoreCrateKey& key) {
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  if (distribution.isRuntimeFailure() || graph.isRuntimeFailure()) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        distribution.isRuntimeFailure() ? distribution.runtimeFailure() : graph.runtimeFailure());
  }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::semanticFailure(
        zc::heapArray<uint8_t>(graph.semanticFailureBytes()));
  }
  if (distribution.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value ||
      graph.value().core().encode().asPtr() != key.crate().encode().asPtr()) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto marker = markerModuleFromGraph(graph.value());
  if (marker == zc::none) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto contextualMarker = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), ZC_ASSERT_NONNULL(marker).clone());
  auto bound =
      context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(contextualMarker));
  if (!bound.isPublished() || bound.lease().capability().module().encode().asPtr() !=
                                  ZC_ASSERT_NONNULL(marker).encode().asPtr()) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        bound.isRuntimeRejected() ? bound.runtimeFailure()
                                  : query::QueryRuntimeFailure::ProviderRejected);
  }
  auto stableMarker =
      incremental_binding_query::StableModuleQueryKey::fromVerified(ZC_ASSERT_NONNULL(marker));
  if (stableMarker == zc::none) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto definitions = context.get<incremental_binding_query::NamedDefinitionInventoryQuery>(
      zc::mv(ZC_ASSERT_NONNULL(stableMarker)));
  if (definitions.isRuntimeFailure()) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        definitions.runtimeFailure());
  }
  if (definitions.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::semanticFailure(
        zc::heapArray<uint8_t>(definitions.semanticFailureBytes()));
  }
  if (definitions.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto inventoryFailure = roleSeedInventoryFailure(distribution.value(), ZC_ASSERT_NONNULL(marker),
                                                   definitions.value());
  if (inventoryFailure != zc::none) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::semanticFailure(
        ZC_ASSERT_NONNULL(inventoryFailure).encodeCanonical());
  }
  auto roles = seedEntriesFromInventory(distribution.value(), ZC_ASSERT_NONNULL(marker),
                                        definitions.value());
  if (roles == zc::none) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  for (const auto& entry : definitions.value().entries()) {
    auto role = roleForDefinition(ZC_ASSERT_NONNULL(roles).asPtr(), entry.key());
    if (role == zc::none) {
      return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    auto definition = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(), binder::StableDefinitionQueryKey::from(
                                        ZC_ASSERT_NONNULL(marker).clone(), entry.key().clone()));
    auto syntax = context.get<incremental_binding_query::NamedItemSyntaxQuery>(zc::mv(definition));
    if (syntax.isRuntimeFailure()) {
      return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(syntax.runtimeFailure());
    }
    if (syntax.kind() == query::QueryValueKind::SemanticFailure) {
      return query::TypedQueryResult<CoreRoleSeedRecord>::semanticFailure(
          zc::heapArray<uint8_t>(syntax.semanticFailureBytes()));
    }
    if (syntax.kind() != query::QueryValueKind::Value ||
        syntax.value().owningModule().encode().asPtr() !=
            ZC_ASSERT_NONNULL(marker).encode().asPtr()) {
      return roleSeedRejected(CoreRoleSeedFailureKind::WrongRoleModule, role);
    }
    if (!core::isInitialMarkerInterface(syntax.value())) {
      return roleSeedRejected(CoreRoleSeedFailureKind::WrongRoleKind, role);
    }
  }
  for (const auto& role : ZC_ASSERT_NONNULL(roles)) {
    if (!roleDefinitionIsPublic(bound.lease().capability(), role.definition)) {
      return roleSeedRejected(CoreRoleSeedFailureKind::WrongRoleVisibility, role.role);
    }
  }
  auto seed = CoreRoleSeedRecord::from(
      key.crate().clone(), graph.value().coreContext().clone(), distribution.value().digest(),
      zc::mv(ZC_ASSERT_NONNULL(marker)), zc::mv(ZC_ASSERT_NONNULL(roles)));
  if (seed == zc::none) {
    return query::TypedQueryResult<CoreRoleSeedRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<CoreRoleSeedRecord>::value(zc::mv(ZC_ASSERT_NONNULL(seed)));
}

using CoreRoleSeedMaterialization = zc::OneOf<VerifiedCoreRoleSeed, query::QueryRuntimeFailure>;

CoreRoleSeedMaterialization materializeCoreRoleSeed(
    query::CapabilityQueryContext<MaterializeCoreRoleSeedQuery>& context,
    const ContextualCoreCrateKey& key) {
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  auto seed = context.get<CoreRoleSeedQuery>(key.clone());
  if (distribution.isRuntimeFailure() || graph.isRuntimeFailure() || seed.isRuntimeFailure() ||
      distribution.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value || seed.kind() != query::QueryValueKind::Value ||
      graph.value().core().encode().asPtr() != key.crate().encode().asPtr() ||
      seed.value().core().encode().asPtr() != key.crate().encode().asPtr() ||
      seed.value().coreContext().digest() != graph.value().coreContext().digest() ||
      seed.value().distribution() != distribution.value().digest()) {
    return query::QueryRuntimeFailure::ProviderRejected;
  }
  auto singleton =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(key.crate());
  if (singleton == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto activeCrates =
      context.get<incremental_binding_query::ActiveCratesQuery>(ZC_ASSERT_NONNULL(singleton));
  auto activeModules = context.get<module_graph_query::ActiveModulesQuery>(key.crate());
  if (activeCrates.isRuntimeFailure() || activeModules.isRuntimeFailure() ||
      activeCrates.kind() != query::QueryValueKind::Value ||
      activeModules.kind() != query::QueryValueKind::Value ||
      activeCrates.value().crates().size() != 1 ||
      activeCrates.value().crates()[0].canonicalCrateBytes() != key.crate().encode().asPtr() ||
      !sameModuleSequence(activeModules.value().modules(), graph.value().modules())) {
    return query::QueryRuntimeFailure::ProviderRejected;
  }
  auto crate = context.materializeActive<identity::CrateKey,
                                         incremental_binding_query::ActiveCrateMembershipQuery>(
      incremental_binding_query::ContextualCrateKey::from(key.contextRoots().clone(),
                                                          key.crate().clone()),
      key.crate());
  if (crate.isRuntimeFailure() || crate.kind() != query::QueryValueKind::Value) {
    return crate.isRuntimeFailure() ? crate.runtimeFailure()
                                    : query::QueryRuntimeFailure::ProviderRejected;
  }
  auto contextualMarker = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), seed.value().markerModule().clone());
  auto bound =
      context.getCapability<module_graph_query::VerifyBoundModuleQuery>(contextualMarker.clone());
  auto skeleton = context.getCapability<module_graph_query::MaterializeModuleSkeletonQuery>(
      zc::mv(contextualMarker));
  if (!bound.isPublished() || !skeleton.isPublished() ||
      bound.lease().capability().contextRoots() != key.contextRoots() ||
      bound.lease().capability().module().encode().asPtr() !=
          seed.value().markerModule().encode().asPtr() ||
      bound.lease().capability().skeletonLease().stableWitness() !=
          skeleton.lease().stableWitness()) {
    return query::QueryRuntimeFailure::ProviderRejected;
  }
  auto module = context.materializeActive<identity::ModuleKey,
                                          incremental_binding_query::ActiveModuleMembershipQuery>(
      incremental_binding_query::ContextualModuleKey::from(key.contextRoots().clone(),
                                                           seed.value().markerModule().clone()),
      seed.value().markerModule());
  if (module.isRuntimeFailure() || module.kind() != query::QueryValueKind::Value) {
    return module.isRuntimeFailure() ? module.runtimeFailure()
                                     : query::QueryRuntimeFailure::ProviderRejected;
  }
  if (module.value() !=
      bound.lease().capability().skeletonLease().capability().identities().module()) {
    return query::QueryRuntimeFailure::ProviderRejected;
  }
  auto stableMarker = incremental_binding_query::StableModuleQueryKey::fromVerified(
      seed.value().markerModule().clone());
  if (stableMarker == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto definitions = context.get<incremental_binding_query::NamedDefinitionInventoryQuery>(
      zc::mv(ZC_ASSERT_NONNULL(stableMarker)));
  if (definitions.isRuntimeFailure() || definitions.kind() != query::QueryValueKind::Value) {
    return definitions.isRuntimeFailure() ? definitions.runtimeFailure()
                                          : query::QueryRuntimeFailure::ProviderRejected;
  }
  zc::Vector<MaterializedCoreRoleSeedEntry> roles(seed.value().roles().size());
  for (const auto& selected : seed.value().roles()) {
    zc::Maybe<const binder::NamedDefinitionInventoryEntry&> definition;
    for (const auto& entry : definitions.value().entries()) {
      if (entry.key() != selected.definition) { continue; }
      if (definition != zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
      definition = entry;
    }
    if (definition == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
    auto definitionKey = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(seed.value().markerModule().clone(),
                                               selected.definition.clone()));
    auto syntax =
        context.get<incremental_binding_query::NamedItemSyntaxQuery>(definitionKey.clone());
    auto handle =
        context.materializeActive<identity::DefinitionKey,
                                  incremental_binding_query::ActiveDefinitionMembershipQuery>(
            zc::mv(definitionKey), ZC_ASSERT_NONNULL(definition).record());
    if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value ||
        syntax.value().owningModule().encode().asPtr() !=
            seed.value().markerModule().encode().asPtr() ||
        !core::isInitialMarkerInterface(syntax.value()) ||
        !roleDefinitionIsPublic(bound.lease().capability(), selected.definition) ||
        handle.isRuntimeFailure() || handle.kind() != query::QueryValueKind::Value) {
      return handle.isRuntimeFailure()   ? handle.runtimeFailure()
             : syntax.isRuntimeFailure() ? syntax.runtimeFailure()
                                         : query::QueryRuntimeFailure::ProviderRejected;
    }
    roles.add(
        MaterializedCoreRoleSeedEntry{selected.role, selected.definition.clone(), handle.value()});
  }
  auto candidate = VerifiedCoreRoleSeed::from(
      bound.lease().capability().context(), bound.lease().capability().fingerprint().clone(),
      seed.value().coreContext().clone(), seed.value().distribution(), crate.value(),
      module.value(), zc::mv(roles), seed.value().revision().clone(), zc::mv(bound).takeLease());
  if (candidate == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  return zc::mv(ZC_ASSERT_NONNULL(candidate));
}

}  // namespace

ContextualCoreCrateKey::ContextualCoreCrateKey(
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    identity::CrateKey&& crate) noexcept
    : contextRootsField(zc::mv(contextRoots)), crateField(zc::mv(crate)) {}

zc::Maybe<ContextualCoreCrateKey> ContextualCoreCrateKey::from(
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    identity::CrateKey&& crate) {
  if (!contextContainsCoreCrate(contextRoots, crate)) { return zc::none; }
  return ContextualCoreCrateKey(zc::mv(contextRoots), zc::mv(crate));
}

zc::Maybe<ContextualCoreCrateKey> ContextualCoreCrateKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto rootsBytes = decoder.decodeByteString(kMaximumContextRootBytes);
  auto crateBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  if (rootsBytes == zc::none || crateBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto roots = incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(rootsBytes).asPtr());
  identity::CanonicalDecoder crateDecoder(ZC_ASSERT_NONNULL(crateBytes).asPtr());
  auto crate = identity::CrateKey::decodeCanonical(crateDecoder);
  if (roots == zc::none || crate == zc::none || !crateDecoder.finished() ||
      ZC_ASSERT_NONNULL(crate).encode().asPtr() != ZC_ASSERT_NONNULL(crateBytes).asPtr()) {
    return zc::none;
  }
  return from(zc::mv(ZC_ASSERT_NONNULL(roots)), zc::mv(ZC_ASSERT_NONNULL(crate)));
}

ContextualCoreCrateKey ContextualCoreCrateKey::clone() const {
  return ContextualCoreCrateKey(contextRootsField.clone(), crateField.clone());
}

const incremental_binding_query::CompilationRootSetQueryKey& ContextualCoreCrateKey::contextRoots()
    const noexcept {
  return contextRootsField;
}

const identity::CrateKey& ContextualCoreCrateKey::crate() const noexcept { return crateField; }

zc::Array<uint8_t> ContextualCoreCrateKey::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  const auto roots = contextRootsField.encodeCanonical();
  const auto crate = crateField.encode();
  encoder.encodeByteString(roots.asPtr());
  encoder.encodeByteString(crate.asPtr());
  return encoder.finish();
}

bool ContextualCoreCrateKey::operator==(const ContextualCoreCrateKey& other) const noexcept {
  return contextRootsField == other.contextRootsField &&
         crateField.encode().asPtr() == other.crateField.encode().asPtr();
}

ContextualCoreModuleKey::ContextualCoreModuleKey(
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    identity::ModuleKey&& module) noexcept
    : contextRootsField(zc::mv(contextRoots)), moduleField(zc::mv(module)) {}

zc::Maybe<ContextualCoreModuleKey> ContextualCoreModuleKey::from(
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    identity::ModuleKey&& module) {
  if (!contextContainsCoreCrate(contextRoots, module.crate())) { return zc::none; }
  return ContextualCoreModuleKey(zc::mv(contextRoots), zc::mv(module));
}

zc::Maybe<ContextualCoreModuleKey> ContextualCoreModuleKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto rootsBytes = decoder.decodeByteString(kMaximumContextRootBytes);
  auto moduleBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  if (rootsBytes == zc::none || moduleBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto roots = incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(rootsBytes).asPtr());
  identity::CanonicalDecoder moduleDecoder(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
  auto module = identity::ModuleKey::decodeCanonical(moduleDecoder);
  if (roots == zc::none || module == zc::none || !moduleDecoder.finished() ||
      ZC_ASSERT_NONNULL(module).encode().asPtr() != ZC_ASSERT_NONNULL(moduleBytes).asPtr()) {
    return zc::none;
  }
  return from(zc::mv(ZC_ASSERT_NONNULL(roots)), zc::mv(ZC_ASSERT_NONNULL(module)));
}

ContextualCoreModuleKey ContextualCoreModuleKey::clone() const {
  return ContextualCoreModuleKey(contextRootsField.clone(), moduleField.clone());
}

const incremental_binding_query::CompilationRootSetQueryKey& ContextualCoreModuleKey::contextRoots()
    const noexcept {
  return contextRootsField;
}

const identity::ModuleKey& ContextualCoreModuleKey::module() const noexcept { return moduleField; }

zc::Array<uint8_t> ContextualCoreModuleKey::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  const auto roots = contextRootsField.encodeCanonical();
  const auto module = moduleField.encode();
  encoder.encodeByteString(roots.asPtr());
  encoder.encodeByteString(module.asPtr());
  return encoder.finish();
}

bool ContextualCoreModuleKey::operator==(const ContextualCoreModuleKey& other) const noexcept {
  return contextRootsField == other.contextRootsField &&
         moduleField.encode().asPtr() == other.moduleField.encode().asPtr();
}

zc::Array<uint8_t> CoreDistributionInput::encodeKey(const Key& key) {
  const auto payload = key.encode();
  return frame(kCoreDistributionDomain, payload.asPtr());
}

zc::Maybe<CoreDistributionInput::Key> CoreDistributionInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCoreDistributionDomain, bytes, kMaximumCoreDistributionKeyBytes);
  if (payload == zc::none) { return zc::none; }
  auto decoded = identity::ToolchainUnitKey::decode(ZC_ASSERT_NONNULL(payload));
  if (decoded == zc::none ||
      ZC_ASSERT_NONNULL(decoded).component() != identity::ToolchainComponent::Core ||
      encodeKey(ZC_ASSERT_NONNULL(decoded)).asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(decoded);
}

zc::Array<uint8_t> CoreDistributionInput::encodeValue(const Value& value) {
  const auto payload = value.encode();
  return frame(kCoreDistributionValueDomain, payload.asPtr());
}

zc::Maybe<CoreDistributionInput::Value> CoreDistributionInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCoreDistributionValueDomain, bytes, kMaximumCoreDistributionValueBytes);
  if (payload == zc::none) { return zc::none; }
  auto decoded =
      source::core::CoreDistributionInputRecord::decodeCanonical(ZC_ASSERT_NONNULL(payload));
  if (decoded == zc::none || encodeValue(ZC_ASSERT_NONNULL(decoded)).asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(decoded);
}

CoreModuleGraphRevision::CoreModuleGraphRevision(const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}

CoreModuleGraphRevision CoreModuleGraphRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CoreModuleGraphRevision(digest);
}

CoreModuleGraphRevision CoreModuleGraphRevision::clone() const noexcept {
  return CoreModuleGraphRevision(digestValue);
}

const identity::Sha256Digest& CoreModuleGraphRevision::digest() const noexcept {
  return digestValue;
}

bool CoreModuleGraphRevision::operator==(const CoreModuleGraphRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

struct CoreModuleGraphRecord::Impl final {
  Impl(identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
       CoreModuleGraphRevision revision, zc::Vector<identity::ModuleKey>&& modules,
       zc::Vector<module_graph_query::ModuleDependencyEdgeKey>&& edges) noexcept
      : core(zc::mv(core)),
        coreContext(zc::mv(coreContext)),
        revision(revision),
        modules(zc::mv(modules)),
        edges(zc::mv(edges)) {}

  identity::CrateKey core;
  identity::CoreSemanticContextFingerprint coreContext;
  CoreModuleGraphRevision revision;
  zc::Vector<identity::ModuleKey> modules;
  zc::Vector<module_graph_query::ModuleDependencyEdgeKey> edges;
};

CoreModuleGraphRecord::CoreModuleGraphRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}

CoreModuleGraphRecord::~CoreModuleGraphRecord() noexcept(false) = default;
CoreModuleGraphRecord::CoreModuleGraphRecord(CoreModuleGraphRecord&&) noexcept = default;
CoreModuleGraphRecord& CoreModuleGraphRecord::operator=(CoreModuleGraphRecord&&) noexcept = default;

zc::Maybe<CoreModuleGraphRecord> CoreModuleGraphRecord::from(
    identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
    zc::Vector<identity::ModuleKey>&& modules,
    zc::Vector<module_graph_query::ModuleDependencyEdgeKey>&& edges) {
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(core);
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != coreContext.digest()) {
    return zc::none;
  }
  auto canonicalGraph = module_graph_query::ModuleGraphRecord::from(zc::mv(modules), zc::mv(edges));
  if (canonicalGraph == zc::none ||
      ZC_ASSERT_NONNULL(canonicalGraph).modules().size() > kMaximumCoreModules ||
      ZC_ASSERT_NONNULL(canonicalGraph).edges().size() > kMaximumCoreEdges) {
    return zc::none;
  }
  zc::Vector<identity::ModuleKey> canonicalModules(
      ZC_ASSERT_NONNULL(canonicalGraph).modules().size());
  for (const auto& module : ZC_ASSERT_NONNULL(canonicalGraph).modules()) {
    if (module.crate().encode().asPtr() != core.encode().asPtr()) { return zc::none; }
    canonicalModules.add(module.clone());
  }
  zc::Vector<module_graph_query::ModuleDependencyEdgeKey> canonicalEdges(
      ZC_ASSERT_NONNULL(canonicalGraph).edges().size());
  for (const auto& edge : ZC_ASSERT_NONNULL(canonicalGraph).edges()) {
    canonicalEdges.add(edge.clone());
  }
  auto digest =
      computeCoreGraphRevision(core, coreContext, canonicalModules.asPtr(), canonicalEdges.asPtr());
  if (digest == zc::none) { return zc::none; }
  CoreModuleGraphRecord record(zc::heap<Impl>(zc::mv(core), zc::mv(coreContext),
                                              CoreModuleGraphRevision(ZC_ASSERT_NONNULL(digest)),
                                              zc::mv(canonicalModules), zc::mv(canonicalEdges)));
  if (record.encodeCanonical().size() > kMaximumCoreGraphBytes) { return zc::none; }
  return zc::mv(record);
}

zc::Maybe<CoreModuleGraphRecord> CoreModuleGraphRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCoreModuleGraphValueDomain, bytes, kMaximumCoreGraphBytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto coreBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto coreContextDigest = decoder.decodeDigest();
  auto revisionDigest = decoder.decodeDigest();
  auto moduleCount = decoder.decodeSequenceSize(kMaximumCoreModules);
  if (coreBytes == zc::none || coreContextDigest == zc::none || revisionDigest == zc::none ||
      moduleCount == zc::none || ZC_ASSERT_NONNULL(moduleCount) == 0) {
    return zc::none;
  }
  identity::CanonicalDecoder coreDecoder(ZC_ASSERT_NONNULL(coreBytes).asPtr());
  auto core = identity::CrateKey::decodeCanonical(coreDecoder);
  if (core == zc::none || !coreDecoder.finished() ||
      ZC_ASSERT_NONNULL(core).encode().asPtr() != ZC_ASSERT_NONNULL(coreBytes).asPtr()) {
    return zc::none;
  }
  auto coreContext = identity::CoreSemanticContextFingerprint::compute(ZC_ASSERT_NONNULL(core));
  if (coreContext == zc::none ||
      ZC_ASSERT_NONNULL(coreContext).digest() != ZC_ASSERT_NONNULL(coreContextDigest)) {
    return zc::none;
  }
  zc::Vector<identity::ModuleKey> modules(static_cast<size_t>(ZC_ASSERT_NONNULL(moduleCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(moduleCount); ++index) {
    auto moduleBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
    if (moduleBytes == zc::none) { return zc::none; }
    identity::CanonicalDecoder moduleDecoder(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
    auto module = identity::ModuleKey::decodeCanonical(moduleDecoder);
    if (module == zc::none || !moduleDecoder.finished() ||
        ZC_ASSERT_NONNULL(module).encode().asPtr() != ZC_ASSERT_NONNULL(moduleBytes).asPtr()) {
      return zc::none;
    }
    modules.add(zc::mv(ZC_ASSERT_NONNULL(module)));
  }
  auto edgeCount = decoder.decodeSequenceSize(kMaximumCoreEdges);
  if (edgeCount == zc::none) { return zc::none; }
  zc::Vector<module_graph_query::ModuleDependencyEdgeKey> edges(
      static_cast<size_t>(ZC_ASSERT_NONNULL(edgeCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(edgeCount); ++index) {
    auto edgeBytes = decoder.decodeByteString(2 * kMaximumCrateOrModuleKeyBytes + 64);
    if (edgeBytes == zc::none) { return zc::none; }
    auto edge = module_graph_query::ModuleDependencyEdgeKey::decodeCanonical(
        ZC_ASSERT_NONNULL(edgeBytes).asPtr());
    if (edge == zc::none) { return zc::none; }
    edges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto record = from(zc::mv(ZC_ASSERT_NONNULL(core)), zc::mv(ZC_ASSERT_NONNULL(coreContext)),
                     zc::mv(modules), zc::mv(edges));
  if (record == zc::none ||
      ZC_ASSERT_NONNULL(record).revision().digest() != ZC_ASSERT_NONNULL(revisionDigest) ||
      ZC_ASSERT_NONNULL(record).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(record));
}

CoreModuleGraphRecord CoreModuleGraphRecord::clone() const {
  zc::Vector<identity::ModuleKey> modules(impl->modules.size());
  for (const auto& module : impl->modules) { modules.add(module.clone()); }
  zc::Vector<module_graph_query::ModuleDependencyEdgeKey> edges(impl->edges.size());
  for (const auto& edge : impl->edges) { edges.add(edge.clone()); }
  return CoreModuleGraphRecord(zc::heap<Impl>(impl->core.clone(), impl->coreContext.clone(),
                                              impl->revision.clone(), zc::mv(modules),
                                              zc::mv(edges)));
}

const identity::CrateKey& CoreModuleGraphRecord::core() const noexcept { return impl->core; }

const identity::CoreSemanticContextFingerprint& CoreModuleGraphRecord::coreContext()
    const noexcept {
  return impl->coreContext;
}

const CoreModuleGraphRevision& CoreModuleGraphRecord::revision() const noexcept {
  return impl->revision;
}

zc::ArrayPtr<const identity::ModuleKey> CoreModuleGraphRecord::modules() const noexcept {
  return impl->modules.asPtr();
}

zc::ArrayPtr<const module_graph_query::ModuleDependencyEdgeKey> CoreModuleGraphRecord::edges()
    const noexcept {
  return impl->edges.asPtr();
}

zc::Array<uint8_t> CoreModuleGraphRecord::encodeCanonical() const {
  zc::Maybe<const CoreModuleGraphRevision&> revision(impl->revision);
  const auto payload = encodeCoreGraphPayload(impl->core, impl->coreContext, zc::mv(revision),
                                              impl->modules.asPtr(), impl->edges.asPtr());
  return frame(kCoreModuleGraphValueDomain, payload.asPtr());
}

zc::Array<uint8_t> CoreModuleGraphQuery::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<CoreModuleGraphQuery::Key> CoreModuleGraphQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreCrateKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> CoreModuleGraphQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<CoreModuleGraphQuery::Value> CoreModuleGraphQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CoreModuleGraphRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<CoreModuleGraphQuery::Value> CoreModuleGraphQuery::provide(
    query::QueryContext& context, const Key& key) {
  return provideCoreModuleGraph(context, key);
}

bool CoreModuleGraphQuery::verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result) {
  return CoreLibraryQueryVerifier::verifyModuleGraph(context, key, result);
}

CoreRoleSeedEntry CoreRoleSeedEntry::clone() const { return {role, definition.clone()}; }

CoreRoleSeedRevision::CoreRoleSeedRevision(const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}

CoreRoleSeedRevision CoreRoleSeedRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CoreRoleSeedRevision(digest);
}

CoreRoleSeedRevision CoreRoleSeedRevision::clone() const noexcept {
  return CoreRoleSeedRevision(digestValue);
}

const identity::Sha256Digest& CoreRoleSeedRevision::digest() const noexcept { return digestValue; }

bool CoreRoleSeedRevision::operator==(const CoreRoleSeedRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

struct CoreRoleSeedRecord::Impl final {
  Impl(identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
       const identity::Sha256Digest& distribution, identity::ModuleKey&& markerModule,
       CoreRoleSeedRevision revision, zc::Vector<CoreRoleSeedEntry>&& roles) noexcept
      : core(zc::mv(core)),
        coreContext(zc::mv(coreContext)),
        distribution(distribution),
        markerModule(zc::mv(markerModule)),
        revision(revision),
        roles(zc::mv(roles)) {}

  identity::CrateKey core;
  identity::CoreSemanticContextFingerprint coreContext;
  identity::Sha256Digest distribution;
  identity::ModuleKey markerModule;
  CoreRoleSeedRevision revision;
  zc::Vector<CoreRoleSeedEntry> roles;
};

CoreRoleSeedRecord::CoreRoleSeedRecord(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
CoreRoleSeedRecord::~CoreRoleSeedRecord() noexcept(false) = default;
CoreRoleSeedRecord::CoreRoleSeedRecord(CoreRoleSeedRecord&&) noexcept = default;
CoreRoleSeedRecord& CoreRoleSeedRecord::operator=(CoreRoleSeedRecord&&) noexcept = default;

zc::Maybe<CoreRoleSeedRecord> CoreRoleSeedRecord::from(
    identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
    const identity::Sha256Digest& distribution, identity::ModuleKey&& markerModule,
    zc::Vector<CoreRoleSeedEntry>&& roles) {
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(core);
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != coreContext.digest() ||
      !isCoreMarkerModule(markerModule, core) || !roleEntriesAreCanonical(roles.asPtr())) {
    return zc::none;
  }
  auto digest = computeCoreRoleSeedRevision(core, coreContext, markerModule, roles.asPtr());
  if (digest == zc::none) { return zc::none; }
  CoreRoleSeedRecord record(
      zc::heap<Impl>(zc::mv(core), zc::mv(coreContext), distribution, zc::mv(markerModule),
                     CoreRoleSeedRevision(ZC_ASSERT_NONNULL(digest)), zc::mv(roles)));
  if (record.encodeCanonical().size() > kMaximumCoreRoleSeedBytes) { return zc::none; }
  return zc::mv(record);
}

zc::Maybe<CoreRoleSeedRecord> CoreRoleSeedRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCoreRoleSeedValueDomain, bytes, kMaximumCoreRoleSeedBytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto coreBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto coreContext = decoder.decodeDigest();
  auto distribution = decoder.decodeDigest();
  auto markerBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto revision = decoder.decodeDigest();
  auto count = decoder.decodeSequenceSize(2);
  if (coreBytes == zc::none || coreContext == zc::none || distribution == zc::none ||
      markerBytes == zc::none || revision == zc::none || count == zc::none) {
    return zc::none;
  }
  auto core = decodePayloadCrate(ZC_ASSERT_NONNULL(coreBytes));
  identity::CanonicalDecoder markerDecoder(ZC_ASSERT_NONNULL(markerBytes));
  auto marker = identity::ModuleKey::decodeCanonical(markerDecoder);
  if (core == zc::none || marker == zc::none || !markerDecoder.finished()) { return zc::none; }
  zc::Vector<CoreRoleSeedEntry> roles(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto role = decoder.decodeUint8();
    auto definitionDigest = decoder.decodeDigest();
    if (role == zc::none || definitionDigest == zc::none ||
        !isCoreRole(static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)))) {
      return zc::none;
    }
    auto definition =
        identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(definitionDigest).bytes());
    if (definition == zc::none) { return zc::none; }
    roles.add(
        CoreRoleSeedEntry{static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)),
                          zc::mv(ZC_ASSERT_NONNULL(definition))});
  }
  if (!decoder.finished()) { return zc::none; }
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(ZC_ASSERT_NONNULL(core));
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != ZC_ASSERT_NONNULL(coreContext)) {
    return zc::none;
  }
  auto record =
      from(zc::mv(ZC_ASSERT_NONNULL(core)), zc::mv(ZC_ASSERT_NONNULL(expectedContext)),
           ZC_ASSERT_NONNULL(distribution), zc::mv(ZC_ASSERT_NONNULL(marker)), zc::mv(roles));
  if (record == zc::none ||
      ZC_ASSERT_NONNULL(record).revision().digest() != ZC_ASSERT_NONNULL(revision) ||
      ZC_ASSERT_NONNULL(record).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(record));
}

CoreRoleSeedRecord CoreRoleSeedRecord::clone() const {
  zc::Vector<CoreRoleSeedEntry> roles(impl->roles.size());
  for (const auto& role : impl->roles) { roles.add(role.clone()); }
  return CoreRoleSeedRecord(zc::heap<Impl>(impl->core.clone(), impl->coreContext.clone(),
                                           impl->distribution, impl->markerModule.clone(),
                                           impl->revision.clone(), zc::mv(roles)));
}

const identity::CrateKey& CoreRoleSeedRecord::core() const noexcept { return impl->core; }

const identity::CoreSemanticContextFingerprint& CoreRoleSeedRecord::coreContext() const noexcept {
  return impl->coreContext;
}

const identity::Sha256Digest& CoreRoleSeedRecord::distribution() const noexcept {
  return impl->distribution;
}

const identity::ModuleKey& CoreRoleSeedRecord::markerModule() const noexcept {
  return impl->markerModule;
}

const CoreRoleSeedRevision& CoreRoleSeedRecord::revision() const noexcept { return impl->revision; }

zc::ArrayPtr<const CoreRoleSeedEntry> CoreRoleSeedRecord::roles() const noexcept {
  return impl->roles.asPtr();
}

zc::Array<uint8_t> CoreRoleSeedRecord::encodeCanonical() const {
  zc::Maybe<const CoreRoleSeedRevision&> revision(impl->revision);
  const auto payload =
      encodeCoreRoleSeedPayload(impl->core, impl->coreContext, impl->distribution,
                                impl->markerModule, zc::mv(revision), impl->roles.asPtr());
  return frame(kCoreRoleSeedValueDomain, payload.asPtr());
}

zc::Array<uint8_t> CoreRoleSeedQuery::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<CoreRoleSeedQuery::Key> CoreRoleSeedQuery::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreCrateKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> CoreRoleSeedQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<CoreRoleSeedQuery::Value> CoreRoleSeedQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CoreRoleSeedRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<CoreRoleSeedQuery::Value> CoreRoleSeedQuery::provide(
    query::QueryContext& context, const Key& key) {
  return provideCoreRoleSeed(context, key);
}

bool CoreRoleSeedQuery::verify(query::QueryContext& context, const Key& key,
                               const query::TypedQueryResult<Value>& result) {
  return CoreLibraryQueryVerifier::verifyRoleSeed(context, key, result);
}

MaterializedCoreRoleSeedEntry MaterializedCoreRoleSeedEntry::clone() const {
  return {role, key.clone(), definition};
}

struct VerifiedCoreRoleSeed::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       identity::CoreSemanticContextFingerprint&& coreContext,
       const identity::Sha256Digest& distribution, identity::CrateId crate,
       identity::ModuleId markerModule, zc::Vector<MaterializedCoreRoleSeedEntry>&& roles,
       CoreRoleSeedRevision revision, BoundModuleLease&& markerBoundModule) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        coreContext(zc::mv(coreContext)),
        distribution(distribution),
        crate(crate),
        markerModule(markerModule),
        roles(zc::mv(roles)),
        revision(revision),
        markerBoundModule(zc::mv(markerBoundModule)) {}

  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  identity::CoreSemanticContextFingerprint coreContext;
  identity::Sha256Digest distribution;
  identity::CrateId crate;
  identity::ModuleId markerModule;
  zc::Vector<MaterializedCoreRoleSeedEntry> roles;
  CoreRoleSeedRevision revision;
  BoundModuleLease markerBoundModule;
};

VerifiedCoreRoleSeed::VerifiedCoreRoleSeed(zc::Own<Impl>&& value) noexcept : impl(zc::mv(value)) {}
VerifiedCoreRoleSeed::~VerifiedCoreRoleSeed() noexcept(false) = default;
VerifiedCoreRoleSeed::VerifiedCoreRoleSeed(VerifiedCoreRoleSeed&&) noexcept = default;
VerifiedCoreRoleSeed& VerifiedCoreRoleSeed::operator=(VerifiedCoreRoleSeed&&) noexcept = default;

zc::Maybe<VerifiedCoreRoleSeed> VerifiedCoreRoleSeed::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    identity::CoreSemanticContextFingerprint&& coreContext,
    const identity::Sha256Digest& distribution, identity::CrateId crate,
    identity::ModuleId markerModule, zc::Vector<MaterializedCoreRoleSeedEntry>&& roles,
    CoreRoleSeedRevision revision, BoundModuleLease&& markerBoundModule) {
  const auto& bound = markerBoundModule.capability();
  if (!context.isValid() || !crate.belongsTo(context) || !markerModule.belongsTo(context) ||
      markerBoundModule.revision() != bound.revision() || bound.context() != context ||
      bound.fingerprint().digest() != fingerprint.digest() || bound.crate() != crate ||
      bound.skeletonLease().capability().identities().module() != markerModule ||
      !materializedRolesAreCanonical(context, roles.asPtr()) ||
      !materializedRolesMatchBound(bound, roles.asPtr())) {
    return zc::none;
  }
  return VerifiedCoreRoleSeed(zc::heap<Impl>(context, zc::mv(fingerprint), zc::mv(coreContext),
                                             distribution, crate, markerModule, zc::mv(roles),
                                             revision.clone(), zc::mv(markerBoundModule)));
}

VerifiedCoreRoleSeed VerifiedCoreRoleSeed::clone() const {
  zc::Vector<MaterializedCoreRoleSeedEntry> roles(impl->roles.size());
  for (const auto& role : impl->roles) { roles.add(role.clone()); }
  return VerifiedCoreRoleSeed(
      zc::heap<Impl>(impl->context, impl->fingerprint.clone(), impl->coreContext.clone(),
                     impl->distribution, impl->crate, impl->markerModule, zc::mv(roles),
                     impl->revision.clone(), impl->markerBoundModule.retain()));
}

identity::SemanticContextBrand VerifiedCoreRoleSeed::context() const noexcept {
  return impl->context;
}

const identity::ContextFingerprint& VerifiedCoreRoleSeed::fingerprint() const noexcept {
  return impl->fingerprint;
}

const identity::CoreSemanticContextFingerprint& VerifiedCoreRoleSeed::coreContext() const noexcept {
  return impl->coreContext;
}

const identity::Sha256Digest& VerifiedCoreRoleSeed::distribution() const noexcept {
  return impl->distribution;
}

identity::CrateId VerifiedCoreRoleSeed::crate() const noexcept { return impl->crate; }

identity::ModuleId VerifiedCoreRoleSeed::markerModule() const noexcept {
  return impl->markerModule;
}

zc::ArrayPtr<const MaterializedCoreRoleSeedEntry> VerifiedCoreRoleSeed::roles() const noexcept {
  return impl->roles.asPtr();
}

const CoreRoleSeedRevision& VerifiedCoreRoleSeed::revision() const noexcept {
  return impl->revision;
}

const VerifiedCoreRoleSeed::BoundModuleLease& VerifiedCoreRoleSeed::markerBoundModuleLease()
    const noexcept {
  return impl->markerBoundModule;
}

zc::Array<uint8_t> VerifiedCoreRoleSeed::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(markerBoundModuleLease().stableWitness());
  encoder.encodeByteString(fingerprint().digest().bytes());
  coreContext().encode(encoder);
  encoder.encodeDigest(distribution());
  encoder.encodeSequenceSize(roles().size());
  for (const auto& role : roles()) {
    encoder.encodeUint8(static_cast<uint8_t>(role.role));
    role.key.encode(encoder);
  }
  encoder.encodeDigest(revision().digest());
  return frame(kVerifiedCoreRoleSeedWitnessDomain, encoder.finish().asPtr());
}

zc::Array<uint8_t> MaterializeCoreRoleSeedQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<MaterializeCoreRoleSeedQuery::Key> MaterializeCoreRoleSeedQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreCrateKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<MaterializeCoreRoleSeedQuery> MaterializeCoreRoleSeedQuery::provide(
    query::CapabilityQueryContext<MaterializeCoreRoleSeedQuery>& context, const Key& key) {
  auto materialized = materializeCoreRoleSeed(context, key);
  if (materialized.is<query::QueryRuntimeFailure>()) {
    return query::CapabilityProviderResult<MaterializeCoreRoleSeedQuery>::runtimeRejected(
        materialized.get<query::QueryRuntimeFailure>());
  }
  auto candidate = zc::heap<Capability>(zc::mv(materialized).get<Capability>());
  auto witness =
      query::CapabilityCandidateContract<MaterializeCoreRoleSeedQuery>::encode(*candidate);
  return query::CapabilityProviderResult<MaterializeCoreRoleSeedQuery>::candidate(zc::mv(candidate),
                                                                                  zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> MaterializeCoreRoleSeedQuery::verify(
    query::CapabilityQueryContext<MaterializeCoreRoleSeedQuery>& context, const Key& key,
    const Capability& candidate) {
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  auto seed = context.get<CoreRoleSeedQuery>(key.clone());
  if (distribution.isRuntimeFailure() || graph.isRuntimeFailure() || seed.isRuntimeFailure() ||
      distribution.kind() != query::QueryValueKind::Value ||
      graph.kind() != query::QueryValueKind::Value || seed.kind() != query::QueryValueKind::Value ||
      graph.value().core().encode().asPtr() != key.crate().encode().asPtr() ||
      seed.value().core().encode().asPtr() != key.crate().encode().asPtr() ||
      seed.value().coreContext().digest() != graph.value().coreContext().digest() ||
      seed.value().distribution() != distribution.value().digest()) {
    return zc::none;
  }
  auto singleton =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(key.crate());
  if (singleton == zc::none) { return zc::none; }
  auto activeCrates =
      context.get<incremental_binding_query::ActiveCratesQuery>(ZC_ASSERT_NONNULL(singleton));
  auto activeModules = context.get<module_graph_query::ActiveModulesQuery>(key.crate());
  if (activeCrates.isRuntimeFailure() || activeModules.isRuntimeFailure() ||
      activeCrates.kind() != query::QueryValueKind::Value ||
      activeModules.kind() != query::QueryValueKind::Value ||
      activeCrates.value().crates().size() != 1 ||
      activeCrates.value().crates()[0].canonicalCrateBytes() != key.crate().encode().asPtr() ||
      !sameModuleSequence(activeModules.value().modules(), graph.value().modules())) {
    return zc::none;
  }
  auto crate = context.materializeActive<identity::CrateKey,
                                         incremental_binding_query::ActiveCrateMembershipQuery>(
      incremental_binding_query::ContextualCrateKey::from(key.contextRoots().clone(),
                                                          key.crate().clone()),
      key.crate());
  auto contextualMarker = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), seed.value().markerModule().clone());
  auto bound =
      context.getCapability<module_graph_query::VerifyBoundModuleQuery>(contextualMarker.clone());
  auto skeleton = context.getCapability<module_graph_query::MaterializeModuleSkeletonQuery>(
      zc::mv(contextualMarker));
  auto module = context.materializeActive<identity::ModuleKey,
                                          incremental_binding_query::ActiveModuleMembershipQuery>(
      incremental_binding_query::ContextualModuleKey::from(key.contextRoots().clone(),
                                                           seed.value().markerModule().clone()),
      seed.value().markerModule());
  if (crate.isRuntimeFailure() || crate.kind() != query::QueryValueKind::Value ||
      !bound.isPublished() || !skeleton.isPublished() || module.isRuntimeFailure() ||
      module.kind() != query::QueryValueKind::Value ||
      bound.lease().capability().contextRoots() != key.contextRoots() ||
      bound.lease().capability().module().encode().asPtr() !=
          seed.value().markerModule().encode().asPtr() ||
      bound.lease().capability().skeletonLease().stableWitness() !=
          skeleton.lease().stableWitness() ||
      module.value() !=
          bound.lease().capability().skeletonLease().capability().identities().module() ||
      candidate.context() != bound.lease().capability().context() ||
      candidate.fingerprint().digest() != bound.lease().capability().fingerprint().digest() ||
      candidate.coreContext().digest() != seed.value().coreContext().digest() ||
      candidate.distribution() != seed.value().distribution() ||
      candidate.crate() != crate.value() || candidate.markerModule() != module.value() ||
      candidate.revision().digest() != seed.value().revision().digest() ||
      candidate.markerBoundModuleLease().stableWitness() != bound.lease().stableWitness() ||
      !materializedRolesAreCanonical(candidate.context(), candidate.roles()) ||
      !materializedRolesMatchBound(bound.lease().capability(), candidate.roles())) {
    return zc::none;
  }
  auto stableMarker = incremental_binding_query::StableModuleQueryKey::fromVerified(
      seed.value().markerModule().clone());
  if (stableMarker == zc::none || candidate.roles().size() != seed.value().roles().size()) {
    return zc::none;
  }
  auto definitions = context.get<incremental_binding_query::NamedDefinitionInventoryQuery>(
      zc::mv(ZC_ASSERT_NONNULL(stableMarker)));
  if (definitions.isRuntimeFailure() || definitions.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  for (size_t index = 0; index < seed.value().roles().size(); ++index) {
    const auto& selected = seed.value().roles()[index];
    zc::Maybe<const binder::NamedDefinitionInventoryEntry&> definition;
    for (const auto& entry : definitions.value().entries()) {
      if (entry.key() != selected.definition) { continue; }
      if (definition != zc::none) { return zc::none; }
      definition = entry;
    }
    if (definition == zc::none) { return zc::none; }
    auto definitionKey = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(),
        binder::StableDefinitionQueryKey::from(seed.value().markerModule().clone(),
                                               selected.definition.clone()));
    auto syntax =
        context.get<incremental_binding_query::NamedItemSyntaxQuery>(definitionKey.clone());
    auto handle =
        context.materializeActive<identity::DefinitionKey,
                                  incremental_binding_query::ActiveDefinitionMembershipQuery>(
            zc::mv(definitionKey), ZC_ASSERT_NONNULL(definition).record());
    if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value ||
        syntax.value().owningModule().encode().asPtr() !=
            seed.value().markerModule().encode().asPtr() ||
        !core::isInitialMarkerInterface(syntax.value()) || handle.isRuntimeFailure() ||
        handle.kind() != query::QueryValueKind::Value ||
        candidate.roles()[index].role != selected.role ||
        candidate.roles()[index].key != selected.definition ||
        candidate.roles()[index].definition != handle.value()) {
      return zc::none;
    }
  }
  return candidate.encodeCanonical();
}

namespace {

zc::Maybe<CoreBootstrapModuleSurface> initialCoreSurface(const identity::ModuleKey& module,
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

bool graphContainsModuleExactlyOnce(const CoreModuleGraphRecord& graph,
                                    const identity::ModuleKey& module) {
  size_t matches = 0;
  for (const auto& candidate : graph.modules()) {
    if (candidate.encode().asPtr() == module.encode().asPtr()) { ++matches; }
  }
  return matches == 1;
}

zc::Vector<CoreRoleSeedEntry> stableRolesFromMaterialized(
    zc::ArrayPtr<const MaterializedCoreRoleSeedEntry> roles) {
  zc::Vector<CoreRoleSeedEntry> result(roles.size());
  for (const auto& role : roles) { result.add(CoreRoleSeedEntry{role.role, role.key.clone()}); }
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
  auto surface = initialCoreSurface(key.module(), graph.value().core());
  if (surface == zc::none) { return zc::none; }
  zc::Vector<core::VerifiedCoreImportedSignatureView::BootstrapInterfaceLease> sources;
  if (ZC_ASSERT_NONNULL(surface) == CoreBootstrapModuleSurface::Prelude) {
    zc::Maybe<identity::ModuleKey> marker;
    for (const auto& module : graph.value().modules()) {
      if (initialCoreSurface(module, graph.value().core()) != CoreBootstrapModuleSurface::Marker) {
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

bool sameRoleEntries(zc::ArrayPtr<const CoreRoleSeedEntry> left,
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

zc::Maybe<CoreBootstrapModuleInterfaceRecord> buildBootstrapRecord(
    const ContextualCoreModuleKey& key, const CoreModuleGraphRecord& graph,
    const module_graph_query::VerifiedBoundModule& bound, const VerifiedCoreRoleSeed& seed) {
  auto surface = initialCoreSurface(key.module(), graph.core());
  if (surface == zc::none || !graphContainsModuleExactlyOnce(graph, key.module()) ||
      bound.contextRoots() != key.contextRoots() ||
      bound.module().encode().asPtr() != key.module().encode().asPtr() ||
      bound.crate() != seed.crate() || bound.context() != seed.context() ||
      bound.fingerprint().digest() != seed.fingerprint().digest() ||
      seed.coreContext().digest() != graph.coreContext().digest() ||
      !core::matchesInitialSurface(ZC_ASSERT_NONNULL(surface), bound, seed)) {
    return zc::none;
  }
  auto roles = stableRolesFromMaterialized(seed.roles());
  return CoreBootstrapModuleInterfaceRecord::from(
      graph.core().clone(), graph.coreContext().clone(), graph.revision().clone(),
      key.module().clone(), bound.bindingSurface().revision(), seed.revision().clone(),
      ZC_ASSERT_NONNULL(surface), zc::mv(roles));
}

query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord> provideCoreBootstrapModuleInterface(
    query::QueryContext& context, const ContextualCoreModuleKey& key) {
  auto coreKey =
      ContextualCoreCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  if (coreKey == zc::none) {
    return query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto graph = context.get<CoreModuleGraphQuery>(ZC_ASSERT_NONNULL(coreKey).clone());
  if (graph.isRuntimeFailure()) {
    return query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>::runtimeFailure(
        graph.runtimeFailure());
  }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>::semanticFailure(
        zc::heapArray<uint8_t>(graph.semanticFailureBytes()));
  }
  auto seed =
      context.getCapability<MaterializeCoreRoleSeedQuery>(zc::mv(ZC_ASSERT_NONNULL(coreKey)));
  auto boundKey = incremental_binding_query::ContextualModuleKey::from(key.contextRoots().clone(),
                                                                       key.module().clone());
  auto bound = context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(boundKey));
  if (graph.kind() != query::QueryValueKind::Value || !seed.isPublished() || !bound.isPublished()) {
    return query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>::runtimeFailure(
        seed.isRuntimeRejected()    ? seed.runtimeFailure()
        : bound.isRuntimeRejected() ? bound.runtimeFailure()
                                    : query::QueryRuntimeFailure::ProviderRejected);
  }
  auto record = buildBootstrapRecord(key, graph.value(), bound.lease().capability(),
                                     seed.lease().capability());
  if (record == zc::none) {
    return query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<CoreBootstrapModuleInterfaceRecord>::value(
      zc::mv(ZC_ASSERT_NONNULL(record)));
}

using CoreBootstrapInterfaceMaterialization =
    zc::OneOf<VerifiedCoreBootstrapModuleInterface, query::QueryRuntimeFailure>;

CoreBootstrapInterfaceMaterialization materializeCoreBootstrapInterface(
    query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
    const ContextualCoreModuleKey& key) {
  auto coreKey =
      ContextualCoreCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  if (coreKey == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto record = context.get<CoreBootstrapModuleInterfaceQuery>(key);
  auto seed =
      context.getCapability<MaterializeCoreRoleSeedQuery>(zc::mv(ZC_ASSERT_NONNULL(coreKey)));
  auto boundKey = incremental_binding_query::ContextualModuleKey::from(key.contextRoots().clone(),
                                                                       key.module().clone());
  auto bound = context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(boundKey));
  if (record.isRuntimeFailure() || record.kind() != query::QueryValueKind::Value ||
      !seed.isPublished() || !bound.isPublished()) {
    return record.isRuntimeFailure()   ? record.runtimeFailure()
           : seed.isRuntimeRejected()  ? seed.runtimeFailure()
           : bound.isRuntimeRejected() ? bound.runtimeFailure()
                                       : query::QueryRuntimeFailure::ProviderRejected;
  }
  auto identities = materializeCheckerIdentities(context, key.contextRoots());
  if (identities == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto surface = initialCoreSurface(key.module(), record.value().core());
  if (surface == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto signatures = core::VerifiedCoreSignatureFacts::from(
      ZC_ASSERT_NONNULL(surface), bound.lease().capability(), seed.lease().capability(),
      ZC_ASSERT_NONNULL(identities));
  if (signatures == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto imported = materializeCoreImportedSignatures(context, key, bound.lease());
  if (imported == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto candidate = VerifiedCoreBootstrapModuleInterface::from(
      bound.lease().capability().context(), bound.lease().capability().fingerprint().clone(),
      record.value().clone(), zc::mv(ZC_ASSERT_NONNULL(signatures)),
      zc::mv(ZC_ASSERT_NONNULL(imported)), zc::mv(bound).takeLease(), zc::mv(seed).takeLease());
  if (candidate == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  return zc::mv(ZC_ASSERT_NONNULL(candidate));
}

zc::Array<uint8_t> encodeBootstrapRecordPayload(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const CoreModuleGraphRevision& graphRevision, const identity::ModuleKey& module,
    const binder::ExportSurfaceRevision& bindingSurface, const CoreRoleSeedRevision& roleSeed,
    CoreBootstrapModuleSurface surface,
    zc::Maybe<const CoreBootstrapModuleInterfaceRevision&> revision,
    zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(core.encode().asPtr());
  encoder.encodeDigest(context.digest());
  encoder.encodeDigest(graphRevision.digest());
  encoder.encodeByteString(module.encode().asPtr());
  encoder.encodeDigest(bindingSurface.digest());
  encoder.encodeDigest(roleSeed.digest());
  encoder.encodeUint8(static_cast<uint8_t>(surface));
  ZC_IF_SOME(value, revision) { encoder.encodeDigest(value.digest()); }
  encoder.encodeSequenceSize(roles.size());
  for (const auto& role : roles) {
    encoder.encodeUint8(static_cast<uint8_t>(role.role));
    role.definition.encode(encoder);
  }
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeBootstrapRecordRevision(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const CoreModuleGraphRevision& graphRevision, const identity::ModuleKey& module,
    const binder::ExportSurfaceRevision& bindingSurface, const CoreRoleSeedRevision& roleSeed,
    CoreBootstrapModuleSurface surface, zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  zc::Maybe<const CoreBootstrapModuleInterfaceRevision&> noRevision;
  auto payload = encodeBootstrapRecordPayload(core, context, graphRevision, module, bindingSurface,
                                              roleSeed, surface, noRevision, roles);
  auto preimage = frame(kCoreBootstrapInterfaceRevisionDomain, payload.asPtr());
  return identity::sha256(preimage.asPtr());
}

}  // namespace

CoreBootstrapModuleInterfaceRevision::CoreBootstrapModuleInterfaceRevision(
    const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}

CoreBootstrapModuleInterfaceRevision CoreBootstrapModuleInterfaceRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CoreBootstrapModuleInterfaceRevision(digest);
}

CoreBootstrapModuleInterfaceRevision CoreBootstrapModuleInterfaceRevision::clone() const noexcept {
  return CoreBootstrapModuleInterfaceRevision(digestValue);
}

const identity::Sha256Digest& CoreBootstrapModuleInterfaceRevision::digest() const noexcept {
  return digestValue;
}

bool CoreBootstrapModuleInterfaceRevision::operator==(
    const CoreBootstrapModuleInterfaceRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

struct CoreBootstrapModuleInterfaceRecord::Impl final {
  Impl(identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
       CoreModuleGraphRevision graphRevision, identity::ModuleKey&& module,
       binder::ExportSurfaceRevision bindingSurface, CoreRoleSeedRevision roleSeed,
       CoreBootstrapModuleSurface surface, CoreBootstrapModuleInterfaceRevision revision,
       zc::Vector<CoreRoleSeedEntry>&& roles) noexcept
      : core(zc::mv(core)),
        context(zc::mv(context)),
        graphRevision(zc::mv(graphRevision)),
        module(zc::mv(module)),
        bindingSurface(zc::mv(bindingSurface)),
        roleSeed(zc::mv(roleSeed)),
        surface(surface),
        revision(zc::mv(revision)),
        roles(zc::mv(roles)) {}

  identity::CrateKey core;
  identity::CoreSemanticContextFingerprint context;
  CoreModuleGraphRevision graphRevision;
  identity::ModuleKey module;
  binder::ExportSurfaceRevision bindingSurface;
  CoreRoleSeedRevision roleSeed;
  CoreBootstrapModuleSurface surface;
  CoreBootstrapModuleInterfaceRevision revision;
  zc::Vector<CoreRoleSeedEntry> roles;
};

CoreBootstrapModuleInterfaceRecord::CoreBootstrapModuleInterfaceRecord(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreBootstrapModuleInterfaceRecord::~CoreBootstrapModuleInterfaceRecord() noexcept(false) = default;
CoreBootstrapModuleInterfaceRecord::CoreBootstrapModuleInterfaceRecord(
    CoreBootstrapModuleInterfaceRecord&&) noexcept = default;
CoreBootstrapModuleInterfaceRecord& CoreBootstrapModuleInterfaceRecord::operator=(
    CoreBootstrapModuleInterfaceRecord&&) noexcept = default;

zc::Maybe<CoreBootstrapModuleInterfaceRecord> CoreBootstrapModuleInterfaceRecord::from(
    identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
    CoreModuleGraphRevision graphRevision, identity::ModuleKey&& module,
    const binder::ExportSurfaceRevision& bindingSurface, CoreRoleSeedRevision roleSeed,
    CoreBootstrapModuleSurface surface, zc::Vector<CoreRoleSeedEntry>&& roles) {
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(core);
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != context.digest() ||
      module.crate().encode().asPtr() != core.encode().asPtr() ||
      initialCoreSurface(module, core) != surface || !roleEntriesAreCanonical(roles.asPtr())) {
    return zc::none;
  }
  auto digest = computeBootstrapRecordRevision(core, context, graphRevision, module, bindingSurface,
                                               roleSeed, surface, roles.asPtr());
  if (digest == zc::none) { return zc::none; }
  auto record = CoreBootstrapModuleInterfaceRecord(zc::heap<Impl>(
      zc::mv(core), zc::mv(context), zc::mv(graphRevision), zc::mv(module), bindingSurface,
      zc::mv(roleSeed), surface, CoreBootstrapModuleInterfaceRevision(ZC_ASSERT_NONNULL(digest)),
      zc::mv(roles)));
  if (record.encodeCanonical().size() > kMaximumCoreBootstrapInterfaceBytes) { return zc::none; }
  return zc::mv(record);
}

zc::Maybe<CoreBootstrapModuleInterfaceRecord> CoreBootstrapModuleInterfaceRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload =
      unframe(kCoreBootstrapInterfaceValueDomain, bytes, kMaximumCoreBootstrapInterfaceBytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto coreBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto context = decoder.decodeDigest();
  auto graphRevision = decoder.decodeDigest();
  auto moduleBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto bindingSurface = decoder.decodeDigest();
  auto roleSeed = decoder.decodeDigest();
  auto surface = decoder.decodeUint8();
  auto revision = decoder.decodeDigest();
  auto count = decoder.decodeSequenceSize(2);
  if (coreBytes == zc::none || context == zc::none || graphRevision == zc::none ||
      moduleBytes == zc::none || bindingSurface == zc::none || roleSeed == zc::none ||
      surface == zc::none || revision == zc::none || count == zc::none) {
    return zc::none;
  }
  auto core = decodePayloadCrate(ZC_ASSERT_NONNULL(coreBytes));
  identity::CanonicalDecoder moduleDecoder(ZC_ASSERT_NONNULL(moduleBytes));
  auto module = identity::ModuleKey::decodeCanonical(moduleDecoder);
  if (core == zc::none || module == zc::none || !moduleDecoder.finished()) { return zc::none; }
  zc::Vector<CoreRoleSeedEntry> roles(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto role = decoder.decodeUint8();
    auto definition = decoder.decodeDigest();
    if (role == zc::none || definition == zc::none ||
        !isCoreRole(static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)))) {
      return zc::none;
    }
    auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(definition).bytes());
    if (key == zc::none) { return zc::none; }
    roles.add(
        CoreRoleSeedEntry{static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)),
                          zc::mv(ZC_ASSERT_NONNULL(key))});
  }
  if (!decoder.finished()) { return zc::none; }
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(ZC_ASSERT_NONNULL(core));
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != ZC_ASSERT_NONNULL(context)) {
    return zc::none;
  }
  auto record =
      from(zc::mv(ZC_ASSERT_NONNULL(core)), zc::mv(ZC_ASSERT_NONNULL(expectedContext)),
           CoreModuleGraphRevision::fromDigest(ZC_ASSERT_NONNULL(graphRevision)),
           zc::mv(ZC_ASSERT_NONNULL(module)),
           binder::ExportSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(bindingSurface)),
           CoreRoleSeedRevision::fromDigest(ZC_ASSERT_NONNULL(roleSeed)),
           static_cast<CoreBootstrapModuleSurface>(ZC_ASSERT_NONNULL(surface)), zc::mv(roles));
  if (record == zc::none ||
      ZC_ASSERT_NONNULL(record).revision().digest() != ZC_ASSERT_NONNULL(revision) ||
      ZC_ASSERT_NONNULL(record).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(record));
}

CoreBootstrapModuleInterfaceRecord CoreBootstrapModuleInterfaceRecord::clone() const {
  zc::Vector<CoreRoleSeedEntry> roles(impl->roles.size());
  for (const auto& role : impl->roles) { roles.add(role.clone()); }
  return CoreBootstrapModuleInterfaceRecord(
      zc::heap<Impl>(impl->core.clone(), impl->context.clone(), impl->graphRevision.clone(),
                     impl->module.clone(), impl->bindingSurface, impl->roleSeed.clone(),
                     impl->surface, impl->revision.clone(), zc::mv(roles)));
}

const identity::CrateKey& CoreBootstrapModuleInterfaceRecord::core() const noexcept {
  return impl->core;
}
const identity::CoreSemanticContextFingerprint& CoreBootstrapModuleInterfaceRecord::coreContext()
    const noexcept {
  return impl->context;
}
const CoreModuleGraphRevision& CoreBootstrapModuleInterfaceRecord::graphRevision() const noexcept {
  return impl->graphRevision;
}
const identity::ModuleKey& CoreBootstrapModuleInterfaceRecord::module() const noexcept {
  return impl->module;
}
const binder::ExportSurfaceRevision& CoreBootstrapModuleInterfaceRecord::bindingSurfaceRevision()
    const noexcept {
  return impl->bindingSurface;
}
const CoreRoleSeedRevision& CoreBootstrapModuleInterfaceRecord::roleSeedRevision() const noexcept {
  return impl->roleSeed;
}
CoreBootstrapModuleSurface CoreBootstrapModuleInterfaceRecord::surface() const noexcept {
  return impl->surface;
}
zc::ArrayPtr<const CoreRoleSeedEntry> CoreBootstrapModuleInterfaceRecord::roles() const noexcept {
  return impl->roles.asPtr();
}
const CoreBootstrapModuleInterfaceRevision& CoreBootstrapModuleInterfaceRecord::revision()
    const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> CoreBootstrapModuleInterfaceRecord::encodeCanonical() const {
  zc::Maybe<const CoreBootstrapModuleInterfaceRevision&> revision(impl->revision);
  auto payload = encodeBootstrapRecordPayload(impl->core, impl->context, impl->graphRevision,
                                              impl->module, impl->bindingSurface, impl->roleSeed,
                                              impl->surface, revision, impl->roles.asPtr());
  return frame(kCoreBootstrapInterfaceValueDomain, payload.asPtr());
}

namespace {

bool exportRoleSetsMatch(CoreBootstrapModuleSurface surface,
                         zc::ArrayPtr<const CoreRoleSeedEntry> defined,
                         zc::ArrayPtr<const CoreRoleSeedEntry> reexported) {
  switch (surface) {
    case CoreBootstrapModuleSurface::Root:
      return defined.size() == 0 && reexported.size() == 0;
    case CoreBootstrapModuleSurface::Marker:
      return roleEntriesAreCanonical(defined) && reexported.size() == 0;
    case CoreBootstrapModuleSurface::Prelude:
      return defined.size() == 0 && roleEntriesAreCanonical(reexported);
  }
  return false;
}

zc::Array<uint8_t> encodeCoreExportSurfacePayload(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const CoreModuleGraphRevision& graphRevision, const identity::ModuleKey& module,
    const CoreBootstrapModuleInterfaceRevision& interfaceRevision,
    zc::Maybe<const CoreExportSurfaceRevision&> revision,
    zc::ArrayPtr<const CoreRoleSeedEntry> defined,
    zc::ArrayPtr<const CoreRoleSeedEntry> reexported) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(core.encode().asPtr());
  encoder.encodeDigest(context.digest());
  encoder.encodeDigest(graphRevision.digest());
  encoder.encodeByteString(module.encode().asPtr());
  encoder.encodeDigest(interfaceRevision.digest());
  ZC_IF_SOME(value, revision) { encoder.encodeDigest(value.digest()); }
  const auto encodeRoles = [&](zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
    encoder.encodeSequenceSize(roles.size());
    for (const auto& role : roles) {
      encoder.encodeUint8(static_cast<uint8_t>(role.role));
      role.definition.encode(encoder);
    }
  };
  encodeRoles(defined);
  encodeRoles(reexported);
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeCoreExportSurfaceRevision(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const CoreModuleGraphRevision& graphRevision, const identity::ModuleKey& module,
    const CoreBootstrapModuleInterfaceRevision& interfaceRevision,
    zc::ArrayPtr<const CoreRoleSeedEntry> defined,
    zc::ArrayPtr<const CoreRoleSeedEntry> reexported) {
  zc::Maybe<const CoreExportSurfaceRevision&> noRevision;
  auto payload = encodeCoreExportSurfacePayload(core, context, graphRevision, module,
                                                interfaceRevision, noRevision, defined, reexported);
  auto preimage = frame(kCoreExportSurfaceRevisionDomain, payload.asPtr());
  return identity::sha256(preimage.asPtr());
}

zc::Maybe<CoreExportSurfaceRecord> buildCoreExportSurfaceRecord(
    const ContextualCoreModuleKey& key, const CoreBootstrapModuleInterfaceRecord& interface) {
  if (interface.module().encode().asPtr() != key.module().encode().asPtr()) { return zc::none; }
  zc::Vector<CoreRoleSeedEntry> defined;
  zc::Vector<CoreRoleSeedEntry> reexported;
  switch (interface.surface()) {
    case CoreBootstrapModuleSurface::Root:
      break;
    case CoreBootstrapModuleSurface::Marker:
      for (const auto& role : interface.roles()) { defined.add(role.clone()); }
      break;
    case CoreBootstrapModuleSurface::Prelude:
      for (const auto& role : interface.roles()) { reexported.add(role.clone()); }
      break;
  }
  return CoreExportSurfaceRecord::from(interface.core().clone(), interface.coreContext().clone(),
                                       interface.graphRevision().clone(),
                                       interface.module().clone(), interface.revision().clone(),
                                       zc::mv(defined), zc::mv(reexported));
}

query::TypedQueryResult<CoreExportSurfaceRecord> provideCoreExportSurface(
    query::QueryContext& context, const ContextualCoreModuleKey& key) {
  auto interface = context.get<CoreBootstrapModuleInterfaceQuery>(key);
  if (interface.isRuntimeFailure()) {
    return query::TypedQueryResult<CoreExportSurfaceRecord>::runtimeFailure(
        interface.runtimeFailure());
  }
  if (interface.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<CoreExportSurfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto record = buildCoreExportSurfaceRecord(key, interface.value());
  if (record == zc::none) {
    return query::TypedQueryResult<CoreExportSurfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<CoreExportSurfaceRecord>::value(zc::mv(ZC_ASSERT_NONNULL(record)));
}

}  // namespace

CoreExportSurfaceRevision::CoreExportSurfaceRevision(const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}

CoreExportSurfaceRevision CoreExportSurfaceRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CoreExportSurfaceRevision(digest);
}

CoreExportSurfaceRevision CoreExportSurfaceRevision::clone() const noexcept {
  return CoreExportSurfaceRevision(digestValue);
}

const identity::Sha256Digest& CoreExportSurfaceRevision::digest() const noexcept {
  return digestValue;
}

bool CoreExportSurfaceRevision::operator==(const CoreExportSurfaceRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

struct CoreExportSurfaceRecord::Impl final {
  Impl(identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
       CoreModuleGraphRevision graphRevision, identity::ModuleKey&& module,
       CoreBootstrapModuleInterfaceRevision interfaceRevision, CoreExportSurfaceRevision revision,
       zc::Vector<CoreRoleSeedEntry>&& defined, zc::Vector<CoreRoleSeedEntry>&& reexported) noexcept
      : core(zc::mv(core)),
        context(zc::mv(context)),
        graphRevision(zc::mv(graphRevision)),
        module(zc::mv(module)),
        interfaceRevision(zc::mv(interfaceRevision)),
        revision(zc::mv(revision)),
        defined(zc::mv(defined)),
        reexported(zc::mv(reexported)) {}

  identity::CrateKey core;
  identity::CoreSemanticContextFingerprint context;
  CoreModuleGraphRevision graphRevision;
  identity::ModuleKey module;
  CoreBootstrapModuleInterfaceRevision interfaceRevision;
  CoreExportSurfaceRevision revision;
  zc::Vector<CoreRoleSeedEntry> defined;
  zc::Vector<CoreRoleSeedEntry> reexported;
};

CoreExportSurfaceRecord::CoreExportSurfaceRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreExportSurfaceRecord::~CoreExportSurfaceRecord() noexcept(false) = default;
CoreExportSurfaceRecord::CoreExportSurfaceRecord(CoreExportSurfaceRecord&&) noexcept = default;
CoreExportSurfaceRecord& CoreExportSurfaceRecord::operator=(CoreExportSurfaceRecord&&) noexcept =
    default;

zc::Maybe<CoreExportSurfaceRecord> CoreExportSurfaceRecord::from(
    identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
    CoreModuleGraphRevision graphRevision, identity::ModuleKey&& module,
    CoreBootstrapModuleInterfaceRevision interfaceRevision, zc::Vector<CoreRoleSeedEntry>&& defined,
    zc::Vector<CoreRoleSeedEntry>&& reexported) {
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(core);
  auto surface = initialCoreSurface(module, core);
  if (expectedContext == zc::none || surface == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != context.digest() ||
      !exportRoleSetsMatch(ZC_ASSERT_NONNULL(surface), defined.asPtr(), reexported.asPtr())) {
    return zc::none;
  }
  auto digest = computeCoreExportSurfaceRevision(
      core, context, graphRevision, module, interfaceRevision, defined.asPtr(), reexported.asPtr());
  if (digest == zc::none) { return zc::none; }
  auto record = CoreExportSurfaceRecord(zc::heap<Impl>(
      zc::mv(core), zc::mv(context), zc::mv(graphRevision), zc::mv(module),
      zc::mv(interfaceRevision), CoreExportSurfaceRevision(ZC_ASSERT_NONNULL(digest)),
      zc::mv(defined), zc::mv(reexported)));
  if (record.encodeCanonical().size() > kMaximumCoreExportSurfaceBytes) { return zc::none; }
  return zc::mv(record);
}

zc::Maybe<CoreExportSurfaceRecord> CoreExportSurfaceRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCoreExportSurfaceValueDomain, bytes, kMaximumCoreExportSurfaceBytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto coreBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto context = decoder.decodeDigest();
  auto graphRevision = decoder.decodeDigest();
  auto moduleBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto interfaceRevision = decoder.decodeDigest();
  auto revision = decoder.decodeDigest();
  const auto decodeRoles = [&]() -> zc::Maybe<zc::Vector<CoreRoleSeedEntry>> {
    auto count = decoder.decodeSequenceSize(2);
    if (count == zc::none) { return zc::none; }
    zc::Vector<CoreRoleSeedEntry> roles(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
    for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
      auto role = decoder.decodeUint8();
      auto definition = decoder.decodeDigest();
      if (role == zc::none || definition == zc::none ||
          !isCoreRole(static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)))) {
        return zc::none;
      }
      auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(definition).bytes());
      if (key == zc::none) { return zc::none; }
      roles.add(
          CoreRoleSeedEntry{static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)),
                            zc::mv(ZC_ASSERT_NONNULL(key))});
    }
    return roles;
  };
  if (coreBytes == zc::none || context == zc::none || graphRevision == zc::none ||
      moduleBytes == zc::none || interfaceRevision == zc::none || revision == zc::none) {
    return zc::none;
  }
  auto core = decodePayloadCrate(ZC_ASSERT_NONNULL(coreBytes));
  identity::CanonicalDecoder moduleDecoder(ZC_ASSERT_NONNULL(moduleBytes));
  auto module = identity::ModuleKey::decodeCanonical(moduleDecoder);
  auto defined = decodeRoles();
  auto reexported = decodeRoles();
  if (core == zc::none || module == zc::none || !moduleDecoder.finished() || defined == zc::none ||
      reexported == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(ZC_ASSERT_NONNULL(core));
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != ZC_ASSERT_NONNULL(context)) {
    return zc::none;
  }
  auto record =
      from(zc::mv(ZC_ASSERT_NONNULL(core)), zc::mv(ZC_ASSERT_NONNULL(expectedContext)),
           CoreModuleGraphRevision::fromDigest(ZC_ASSERT_NONNULL(graphRevision)),
           zc::mv(ZC_ASSERT_NONNULL(module)),
           CoreBootstrapModuleInterfaceRevision::fromDigest(ZC_ASSERT_NONNULL(interfaceRevision)),
           zc::mv(ZC_ASSERT_NONNULL(defined)), zc::mv(ZC_ASSERT_NONNULL(reexported)));
  if (record == zc::none ||
      ZC_ASSERT_NONNULL(record).revision().digest() != ZC_ASSERT_NONNULL(revision) ||
      ZC_ASSERT_NONNULL(record).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(record));
}

CoreExportSurfaceRecord CoreExportSurfaceRecord::clone() const {
  zc::Vector<CoreRoleSeedEntry> defined(impl->defined.size());
  zc::Vector<CoreRoleSeedEntry> reexported(impl->reexported.size());
  for (const auto& role : impl->defined) { defined.add(role.clone()); }
  for (const auto& role : impl->reexported) { reexported.add(role.clone()); }
  return CoreExportSurfaceRecord(
      zc::heap<Impl>(impl->core.clone(), impl->context.clone(), impl->graphRevision.clone(),
                     impl->module.clone(), impl->interfaceRevision.clone(), impl->revision.clone(),
                     zc::mv(defined), zc::mv(reexported)));
}

const identity::CrateKey& CoreExportSurfaceRecord::core() const noexcept { return impl->core; }
const identity::CoreSemanticContextFingerprint& CoreExportSurfaceRecord::coreContext()
    const noexcept {
  return impl->context;
}
const CoreModuleGraphRevision& CoreExportSurfaceRecord::graphRevision() const noexcept {
  return impl->graphRevision;
}
const identity::ModuleKey& CoreExportSurfaceRecord::module() const noexcept { return impl->module; }
const CoreBootstrapModuleInterfaceRevision& CoreExportSurfaceRecord::interfaceRevision()
    const noexcept {
  return impl->interfaceRevision;
}
zc::ArrayPtr<const CoreRoleSeedEntry> CoreExportSurfaceRecord::definedRoles() const noexcept {
  return impl->defined.asPtr();
}
zc::ArrayPtr<const CoreRoleSeedEntry> CoreExportSurfaceRecord::reexportedRoles() const noexcept {
  return impl->reexported.asPtr();
}
const CoreExportSurfaceRevision& CoreExportSurfaceRecord::revision() const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> CoreExportSurfaceRecord::encodeCanonical() const {
  zc::Maybe<const CoreExportSurfaceRevision&> revision(impl->revision);
  auto payload = encodeCoreExportSurfacePayload(impl->core, impl->context, impl->graphRevision,
                                                impl->module, impl->interfaceRevision, revision,
                                                impl->defined.asPtr(), impl->reexported.asPtr());
  return frame(kCoreExportSurfaceValueDomain, payload.asPtr());
}

zc::Array<uint8_t> CoreExportSurfaceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<CoreExportSurfaceQuery::Key> CoreExportSurfaceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreModuleKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> CoreExportSurfaceQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<CoreExportSurfaceQuery::Value> CoreExportSurfaceQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CoreExportSurfaceRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<CoreExportSurfaceQuery::Value> CoreExportSurfaceQuery::provide(
    query::QueryContext& context, const Key& key) {
  return provideCoreExportSurface(context, key);
}

bool CoreExportSurfaceQuery::verify(query::QueryContext& context, const Key& key,
                                    const query::TypedQueryResult<Value>& result) {
  return CoreLibraryQueryVerifier::verifyExportSurface(context, key, result);
}

namespace {

zc::Maybe<identity::ModuleKey> findInitialCoreModule(const CoreModuleGraphRecord& graph,
                                                     CoreBootstrapModuleSurface surface) {
  zc::Maybe<identity::ModuleKey> selected;
  for (const auto& module : graph.modules()) {
    auto candidate = initialCoreSurface(module, graph.core());
    if (candidate == zc::none || ZC_ASSERT_NONNULL(candidate) != surface) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = module.clone();
  }
  return selected;
}

bool graphHasExactPreludeDependency(const CoreModuleGraphRecord& graph,
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

zc::Array<uint8_t> encodeCorePreludeSurfacePayload(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const CoreModuleGraphRevision& graphRevision, const identity::ModuleKey& marker,
    const identity::ModuleKey& prelude, const CoreExportSurfaceRevision& markerExport,
    const CoreExportSurfaceRevision& preludeExport,
    zc::Maybe<const CorePreludeSurfaceRevision&> revision,
    zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(core.encode().asPtr());
  encoder.encodeDigest(context.digest());
  encoder.encodeDigest(graphRevision.digest());
  encoder.encodeByteString(marker.encode().asPtr());
  encoder.encodeByteString(prelude.encode().asPtr());
  encoder.encodeDigest(markerExport.digest());
  encoder.encodeDigest(preludeExport.digest());
  ZC_IF_SOME(value, revision) { encoder.encodeDigest(value.digest()); }
  encoder.encodeSequenceSize(roles.size());
  for (const auto& role : roles) {
    encoder.encodeUint8(static_cast<uint8_t>(role.role));
    role.definition.encode(encoder);
  }
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeCorePreludeSurfaceRevision(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const CoreModuleGraphRevision& graphRevision, const identity::ModuleKey& marker,
    const identity::ModuleKey& prelude, const CoreExportSurfaceRevision& markerExport,
    const CoreExportSurfaceRevision& preludeExport, zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  zc::Maybe<const CorePreludeSurfaceRevision&> noRevision;
  auto payload = encodeCorePreludeSurfacePayload(core, context, graphRevision, marker, prelude,
                                                 markerExport, preludeExport, noRevision, roles);
  auto preimage = frame(kCorePreludeSurfaceRevisionDomain, payload.asPtr());
  return identity::sha256(preimage.asPtr());
}

query::TypedQueryResult<CorePreludeSurfaceRecord> provideCorePreludeSurface(
    query::QueryContext& context, const ContextualCoreCrateKey& key) {
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  if (graph.isRuntimeFailure()) {
    return query::TypedQueryResult<CorePreludeSurfaceRecord>::runtimeFailure(
        graph.runtimeFailure());
  }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<CorePreludeSurfaceRecord>::semanticFailure(
        zc::heapArray<uint8_t>(graph.semanticFailureBytes()));
  }
  if (graph.kind() != query::QueryValueKind::Value ||
      graph.value().core().encode().asPtr() != key.crate().encode().asPtr()) {
    return query::TypedQueryResult<CorePreludeSurfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto marker = findInitialCoreModule(graph.value(), CoreBootstrapModuleSurface::Marker);
  auto prelude = findInitialCoreModule(graph.value(), CoreBootstrapModuleSurface::Prelude);
  if (marker == zc::none || prelude == zc::none ||
      !graphHasExactPreludeDependency(graph.value(), ZC_ASSERT_NONNULL(prelude),
                                      ZC_ASSERT_NONNULL(marker))) {
    return query::TypedQueryResult<CorePreludeSurfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto markerKey =
      ContextualCoreModuleKey::from(key.contextRoots().clone(), ZC_ASSERT_NONNULL(marker).clone());
  auto preludeKey =
      ContextualCoreModuleKey::from(key.contextRoots().clone(), ZC_ASSERT_NONNULL(prelude).clone());
  if (markerKey == zc::none || preludeKey == zc::none) {
    return query::TypedQueryResult<CorePreludeSurfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto markerExport = context.get<CoreExportSurfaceQuery>(zc::mv(ZC_ASSERT_NONNULL(markerKey)));
  auto preludeExport = context.get<CoreExportSurfaceQuery>(zc::mv(ZC_ASSERT_NONNULL(preludeKey)));
  if (markerExport.isRuntimeFailure() || preludeExport.isRuntimeFailure() ||
      markerExport.kind() != query::QueryValueKind::Value ||
      preludeExport.kind() != query::QueryValueKind::Value ||
      markerExport.value().definedRoles().size() != 2 ||
      preludeExport.value().reexportedRoles().size() != 2 ||
      markerExport.value().reexportedRoles().size() != 0 ||
      preludeExport.value().definedRoles().size() != 0 ||
      !sameRoleEntries(markerExport.value().definedRoles(),
                       preludeExport.value().reexportedRoles())) {
    return query::TypedQueryResult<CorePreludeSurfaceRecord>::runtimeFailure(
        markerExport.isRuntimeFailure()    ? markerExport.runtimeFailure()
        : preludeExport.isRuntimeFailure() ? preludeExport.runtimeFailure()
                                           : query::QueryRuntimeFailure::ProviderRejected);
  }
  zc::Vector<CoreRoleSeedEntry> roles(markerExport.value().definedRoles().size());
  for (const auto& role : markerExport.value().definedRoles()) { roles.add(role.clone()); }
  auto record = CorePreludeSurfaceRecord::from(
      graph.value().core().clone(), graph.value().coreContext().clone(),
      graph.value().revision().clone(), ZC_ASSERT_NONNULL(marker).clone(),
      ZC_ASSERT_NONNULL(prelude).clone(), markerExport.value().revision().clone(),
      preludeExport.value().revision().clone(), zc::mv(roles));
  if (record == zc::none) {
    return query::TypedQueryResult<CorePreludeSurfaceRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<CorePreludeSurfaceRecord>::value(
      zc::mv(ZC_ASSERT_NONNULL(record)));
}

}  // namespace

CorePreludeSurfaceRevision::CorePreludeSurfaceRevision(
    const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}

CorePreludeSurfaceRevision CorePreludeSurfaceRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CorePreludeSurfaceRevision(digest);
}

CorePreludeSurfaceRevision CorePreludeSurfaceRevision::clone() const noexcept {
  return CorePreludeSurfaceRevision(digestValue);
}

const identity::Sha256Digest& CorePreludeSurfaceRevision::digest() const noexcept {
  return digestValue;
}

bool CorePreludeSurfaceRevision::operator==(
    const CorePreludeSurfaceRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

struct CorePreludeSurfaceRecord::Impl final {
  Impl(identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
       CoreModuleGraphRevision graphRevision, identity::ModuleKey&& marker,
       identity::ModuleKey&& prelude, CoreExportSurfaceRevision markerExport,
       CoreExportSurfaceRevision preludeExport, CorePreludeSurfaceRevision revision,
       zc::Vector<CoreRoleSeedEntry>&& roles) noexcept
      : core(zc::mv(core)),
        context(zc::mv(context)),
        graphRevision(zc::mv(graphRevision)),
        marker(zc::mv(marker)),
        prelude(zc::mv(prelude)),
        markerExport(zc::mv(markerExport)),
        preludeExport(zc::mv(preludeExport)),
        revision(zc::mv(revision)),
        roles(zc::mv(roles)) {}

  identity::CrateKey core;
  identity::CoreSemanticContextFingerprint context;
  CoreModuleGraphRevision graphRevision;
  identity::ModuleKey marker;
  identity::ModuleKey prelude;
  CoreExportSurfaceRevision markerExport;
  CoreExportSurfaceRevision preludeExport;
  CorePreludeSurfaceRevision revision;
  zc::Vector<CoreRoleSeedEntry> roles;
};

CorePreludeSurfaceRecord::CorePreludeSurfaceRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CorePreludeSurfaceRecord::~CorePreludeSurfaceRecord() noexcept(false) = default;
CorePreludeSurfaceRecord::CorePreludeSurfaceRecord(CorePreludeSurfaceRecord&&) noexcept = default;
CorePreludeSurfaceRecord& CorePreludeSurfaceRecord::operator=(CorePreludeSurfaceRecord&&) noexcept =
    default;

zc::Maybe<CorePreludeSurfaceRecord> CorePreludeSurfaceRecord::from(
    identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
    CoreModuleGraphRevision graphRevision, identity::ModuleKey&& marker,
    identity::ModuleKey&& prelude, CoreExportSurfaceRevision markerExport,
    CoreExportSurfaceRevision preludeExport, zc::Vector<CoreRoleSeedEntry>&& roles) {
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(core);
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != context.digest() ||
      initialCoreSurface(marker, core) != CoreBootstrapModuleSurface::Marker ||
      initialCoreSurface(prelude, core) != CoreBootstrapModuleSurface::Prelude ||
      !roleEntriesAreCanonical(roles.asPtr())) {
    return zc::none;
  }
  auto digest = computeCorePreludeSurfaceRevision(core, context, graphRevision, marker, prelude,
                                                  markerExport, preludeExport, roles.asPtr());
  if (digest == zc::none) { return zc::none; }
  auto record = CorePreludeSurfaceRecord(
      zc::heap<Impl>(zc::mv(core), zc::mv(context), zc::mv(graphRevision), zc::mv(marker),
                     zc::mv(prelude), zc::mv(markerExport), zc::mv(preludeExport),
                     CorePreludeSurfaceRevision(ZC_ASSERT_NONNULL(digest)), zc::mv(roles)));
  if (record.encodeCanonical().size() > kMaximumCorePreludeSurfaceBytes) { return zc::none; }
  return zc::mv(record);
}

zc::Maybe<CorePreludeSurfaceRecord> CorePreludeSurfaceRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCorePreludeSurfaceValueDomain, bytes, kMaximumCorePreludeSurfaceBytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto coreBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto context = decoder.decodeDigest();
  auto graphRevision = decoder.decodeDigest();
  auto markerBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto preludeBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto markerExport = decoder.decodeDigest();
  auto preludeExport = decoder.decodeDigest();
  auto revision = decoder.decodeDigest();
  auto count = decoder.decodeSequenceSize(2);
  if (coreBytes == zc::none || context == zc::none || graphRevision == zc::none ||
      markerBytes == zc::none || preludeBytes == zc::none || markerExport == zc::none ||
      preludeExport == zc::none || revision == zc::none || count == zc::none) {
    return zc::none;
  }
  auto core = decodePayloadCrate(ZC_ASSERT_NONNULL(coreBytes));
  identity::CanonicalDecoder markerDecoder(ZC_ASSERT_NONNULL(markerBytes));
  identity::CanonicalDecoder preludeDecoder(ZC_ASSERT_NONNULL(preludeBytes));
  auto marker = identity::ModuleKey::decodeCanonical(markerDecoder);
  auto prelude = identity::ModuleKey::decodeCanonical(preludeDecoder);
  zc::Vector<CoreRoleSeedEntry> roles(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto role = decoder.decodeUint8();
    auto definition = decoder.decodeDigest();
    if (role == zc::none || definition == zc::none ||
        !isCoreRole(static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)))) {
      return zc::none;
    }
    auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(definition).bytes());
    if (key == zc::none) { return zc::none; }
    roles.add(
        CoreRoleSeedEntry{static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)),
                          zc::mv(ZC_ASSERT_NONNULL(key))});
  }
  if (core == zc::none || marker == zc::none || prelude == zc::none || !markerDecoder.finished() ||
      !preludeDecoder.finished() || !decoder.finished()) {
    return zc::none;
  }
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(ZC_ASSERT_NONNULL(core));
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != ZC_ASSERT_NONNULL(context)) {
    return zc::none;
  }
  auto record =
      from(zc::mv(ZC_ASSERT_NONNULL(core)), zc::mv(ZC_ASSERT_NONNULL(expectedContext)),
           CoreModuleGraphRevision::fromDigest(ZC_ASSERT_NONNULL(graphRevision)),
           zc::mv(ZC_ASSERT_NONNULL(marker)), zc::mv(ZC_ASSERT_NONNULL(prelude)),
           CoreExportSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(markerExport)),
           CoreExportSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(preludeExport)), zc::mv(roles));
  if (record == zc::none ||
      ZC_ASSERT_NONNULL(record).revision().digest() != ZC_ASSERT_NONNULL(revision) ||
      ZC_ASSERT_NONNULL(record).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(record));
}

CorePreludeSurfaceRecord CorePreludeSurfaceRecord::clone() const {
  zc::Vector<CoreRoleSeedEntry> roles(impl->roles.size());
  for (const auto& role : impl->roles) { roles.add(role.clone()); }
  return CorePreludeSurfaceRecord(
      zc::heap<Impl>(impl->core.clone(), impl->context.clone(), impl->graphRevision.clone(),
                     impl->marker.clone(), impl->prelude.clone(), impl->markerExport.clone(),
                     impl->preludeExport.clone(), impl->revision.clone(), zc::mv(roles)));
}

const identity::CrateKey& CorePreludeSurfaceRecord::core() const noexcept { return impl->core; }
const identity::CoreSemanticContextFingerprint& CorePreludeSurfaceRecord::coreContext()
    const noexcept {
  return impl->context;
}
const CoreModuleGraphRevision& CorePreludeSurfaceRecord::graphRevision() const noexcept {
  return impl->graphRevision;
}
const identity::ModuleKey& CorePreludeSurfaceRecord::markerModule() const noexcept {
  return impl->marker;
}
const identity::ModuleKey& CorePreludeSurfaceRecord::preludeModule() const noexcept {
  return impl->prelude;
}
const CoreExportSurfaceRevision& CorePreludeSurfaceRecord::markerExportRevision() const noexcept {
  return impl->markerExport;
}
const CoreExportSurfaceRevision& CorePreludeSurfaceRecord::preludeExportRevision() const noexcept {
  return impl->preludeExport;
}
zc::ArrayPtr<const CoreRoleSeedEntry> CorePreludeSurfaceRecord::roles() const noexcept {
  return impl->roles.asPtr();
}
const CorePreludeSurfaceRevision& CorePreludeSurfaceRecord::revision() const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> CorePreludeSurfaceRecord::encodeCanonical() const {
  zc::Maybe<const CorePreludeSurfaceRevision&> revision(impl->revision);
  auto payload = encodeCorePreludeSurfacePayload(
      impl->core, impl->context, impl->graphRevision, impl->marker, impl->prelude,
      impl->markerExport, impl->preludeExport, revision, impl->roles.asPtr());
  return frame(kCorePreludeSurfaceValueDomain, payload.asPtr());
}

namespace {

zc::Array<uint8_t> encodeCoreRoleAuthorityPayload(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const identity::Sha256Digest& policyTemplate, const CoreRoleSeedRevision& roleSeed,
    const CorePreludeSurfaceRevision& prelude, zc::Maybe<const CoreRoleAuthorityRevision&> revision,
    zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(core.encode().asPtr());
  encoder.encodeDigest(context.digest());
  encoder.encodeDigest(policyTemplate);
  encoder.encodeDigest(roleSeed.digest());
  encoder.encodeDigest(prelude.digest());
  ZC_IF_SOME(value, revision) { encoder.encodeDigest(value.digest()); }
  encoder.encodeSequenceSize(roles.size());
  for (const auto& role : roles) {
    encoder.encodeUint8(static_cast<uint8_t>(role.role));
    role.definition.encode(encoder);
  }
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeCoreRoleAuthorityRevision(
    const identity::CrateKey& core, const identity::CoreSemanticContextFingerprint& context,
    const identity::Sha256Digest& policyTemplate, const CoreRoleSeedRevision& roleSeed,
    const CorePreludeSurfaceRevision& prelude, zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  zc::Maybe<const CoreRoleAuthorityRevision&> noRevision;
  auto payload = encodeCoreRoleAuthorityPayload(core, context, policyTemplate, roleSeed, prelude,
                                                noRevision, roles);
  return identity::sha256(frame(kCoreRoleAuthorityRevisionDomain, payload.asPtr()).asPtr());
}

}  // namespace

CoreRoleAuthorityRevision::CoreRoleAuthorityRevision(const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}

CoreRoleAuthorityRevision CoreRoleAuthorityRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CoreRoleAuthorityRevision(digest);
}

CoreRoleAuthorityRevision CoreRoleAuthorityRevision::clone() const noexcept {
  return CoreRoleAuthorityRevision(digestValue);
}

const identity::Sha256Digest& CoreRoleAuthorityRevision::digest() const noexcept {
  return digestValue;
}

bool CoreRoleAuthorityRevision::operator==(const CoreRoleAuthorityRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

struct CoreRoleAuthorityRecord::Impl final {
  Impl(identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
       const identity::Sha256Digest& policyTemplate, CoreRoleSeedRevision roleSeed,
       CorePreludeSurfaceRevision prelude, CoreRoleAuthorityRevision revision,
       zc::Vector<CoreRoleSeedEntry>&& roles) noexcept
      : core(zc::mv(core)),
        context(zc::mv(context)),
        policyTemplate(policyTemplate),
        roleSeed(zc::mv(roleSeed)),
        prelude(zc::mv(prelude)),
        revision(zc::mv(revision)),
        roles(zc::mv(roles)) {}

  identity::CrateKey core;
  identity::CoreSemanticContextFingerprint context;
  identity::Sha256Digest policyTemplate;
  CoreRoleSeedRevision roleSeed;
  CorePreludeSurfaceRevision prelude;
  CoreRoleAuthorityRevision revision;
  zc::Vector<CoreRoleSeedEntry> roles;
};

CoreRoleAuthorityRecord::CoreRoleAuthorityRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreRoleAuthorityRecord::~CoreRoleAuthorityRecord() noexcept(false) = default;
CoreRoleAuthorityRecord::CoreRoleAuthorityRecord(CoreRoleAuthorityRecord&&) noexcept = default;
CoreRoleAuthorityRecord& CoreRoleAuthorityRecord::operator=(CoreRoleAuthorityRecord&&) noexcept =
    default;

zc::Maybe<CoreRoleAuthorityRecord> CoreRoleAuthorityRecord::from(
    identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& context,
    const identity::Sha256Digest& policyTemplate, CoreRoleSeedRevision roleSeed,
    CorePreludeSurfaceRevision prelude, zc::Vector<CoreRoleSeedEntry>&& roles) {
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(core);
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != context.digest() || roles.size() != 2 ||
      roles[0].role != source::core::CoreSemanticRole::Copy ||
      roles[1].role != source::core::CoreSemanticRole::Linear) {
    return zc::none;
  }
  auto revision = computeCoreRoleAuthorityRevision(core, context, policyTemplate, roleSeed, prelude,
                                                   roles.asPtr());
  if (revision == zc::none) { return zc::none; }
  return CoreRoleAuthorityRecord(zc::heap<Impl>(
      zc::mv(core), zc::mv(context), policyTemplate, zc::mv(roleSeed), zc::mv(prelude),
      CoreRoleAuthorityRevision::fromDigest(ZC_ASSERT_NONNULL(revision)), zc::mv(roles)));
}

zc::Maybe<CoreRoleAuthorityRecord> CoreRoleAuthorityRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCoreRoleAuthorityValueDomain, bytes, kMaximumCoreRoleAuthorityBytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto coreBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
  auto context = decoder.decodeDigest();
  auto policyTemplate = decoder.decodeDigest();
  auto roleSeed = decoder.decodeDigest();
  auto prelude = decoder.decodeDigest();
  auto revision = decoder.decodeDigest();
  auto count = decoder.decodeSequenceSize(2);
  if (coreBytes == zc::none || context == zc::none || policyTemplate == zc::none ||
      roleSeed == zc::none || prelude == zc::none || revision == zc::none || count == zc::none) {
    return zc::none;
  }
  auto core = decodePayloadCrate(ZC_ASSERT_NONNULL(coreBytes));
  zc::Vector<CoreRoleSeedEntry> roles(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto role = decoder.decodeUint8();
    auto definition = decoder.decodeDigest();
    if (role == zc::none || definition == zc::none ||
        !isCoreRole(static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)))) {
      return zc::none;
    }
    auto key = identity::DefinitionKey::fromBytes(ZC_ASSERT_NONNULL(definition).bytes());
    if (key == zc::none) { return zc::none; }
    roles.add(
        CoreRoleSeedEntry{static_cast<source::core::CoreSemanticRole>(ZC_ASSERT_NONNULL(role)),
                          zc::mv(ZC_ASSERT_NONNULL(key))});
  }
  if (core == zc::none || !decoder.finished()) { return zc::none; }
  auto coreContext = identity::CoreSemanticContextFingerprint::compute(ZC_ASSERT_NONNULL(core));
  if (coreContext == zc::none ||
      ZC_ASSERT_NONNULL(coreContext).digest() != ZC_ASSERT_NONNULL(context)) {
    return zc::none;
  }
  auto record =
      from(zc::mv(ZC_ASSERT_NONNULL(core)), zc::mv(ZC_ASSERT_NONNULL(coreContext)),
           ZC_ASSERT_NONNULL(policyTemplate),
           CoreRoleSeedRevision::fromDigest(ZC_ASSERT_NONNULL(roleSeed)),
           CorePreludeSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(prelude)), zc::mv(roles));
  if (record == zc::none ||
      ZC_ASSERT_NONNULL(record).revision().digest() != ZC_ASSERT_NONNULL(revision) ||
      ZC_ASSERT_NONNULL(record).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(record));
}

CoreRoleAuthorityRecord CoreRoleAuthorityRecord::clone() const {
  zc::Vector<CoreRoleSeedEntry> roles(impl->roles.size());
  for (const auto& role : impl->roles) { roles.add(role.clone()); }
  return CoreRoleAuthorityRecord(zc::heap<Impl>(
      impl->core.clone(), impl->context.clone(), impl->policyTemplate, impl->roleSeed.clone(),
      impl->prelude.clone(), impl->revision.clone(), zc::mv(roles)));
}

const identity::CrateKey& CoreRoleAuthorityRecord::core() const noexcept { return impl->core; }
const identity::CoreSemanticContextFingerprint& CoreRoleAuthorityRecord::coreContext()
    const noexcept {
  return impl->context;
}
const identity::Sha256Digest& CoreRoleAuthorityRecord::policyTemplateRevision() const noexcept {
  return impl->policyTemplate;
}
const CoreRoleSeedRevision& CoreRoleAuthorityRecord::roleSeedRevision() const noexcept {
  return impl->roleSeed;
}
const CorePreludeSurfaceRevision& CoreRoleAuthorityRecord::preludeRevision() const noexcept {
  return impl->prelude;
}
zc::ArrayPtr<const CoreRoleSeedEntry> CoreRoleAuthorityRecord::roles() const noexcept {
  return impl->roles.asPtr();
}
const CoreRoleAuthorityRevision& CoreRoleAuthorityRecord::revision() const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> CoreRoleAuthorityRecord::encodeCanonical() const {
  zc::Maybe<const CoreRoleAuthorityRevision&> revisionValue(impl->revision);
  return frame(kCoreRoleAuthorityValueDomain,
               encodeCoreRoleAuthorityPayload(impl->core, impl->context, impl->policyTemplate,
                                              impl->roleSeed, impl->prelude, zc::mv(revisionValue),
                                              impl->roles.asPtr())
                   .asPtr());
}

zc::Array<uint8_t> CorePreludeSurfaceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<CorePreludeSurfaceQuery::Key> CorePreludeSurfaceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreCrateKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> CorePreludeSurfaceQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<CorePreludeSurfaceQuery::Value> CorePreludeSurfaceQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CorePreludeSurfaceRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<CorePreludeSurfaceQuery::Value> CorePreludeSurfaceQuery::provide(
    query::QueryContext& context, const Key& key) {
  return provideCorePreludeSurface(context, key);
}

bool CorePreludeSurfaceQuery::verify(query::QueryContext& context, const Key& key,
                                     const query::TypedQueryResult<Value>& result) {
  return CoreLibraryQueryVerifier::verifyPreludeSurface(context, key, result);
}

zc::Array<uint8_t> CoreRoleAuthorityQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<CoreRoleAuthorityQuery::Key> CoreRoleAuthorityQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreCrateKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> CoreRoleAuthorityQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<CoreRoleAuthorityQuery::Value> CoreRoleAuthorityQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CoreRoleAuthorityRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<CoreRoleAuthorityQuery::Value> CoreRoleAuthorityQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto seed = context.getCapability<MaterializeCoreRoleSeedQuery>(key.clone());
  auto prelude = context.get<CorePreludeSurfaceQuery>(key.clone());
  if (distribution.isRuntimeFailure() || prelude.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        distribution.isRuntimeFailure() ? distribution.runtimeFailure() : prelude.runtimeFailure());
  }
  if (distribution.kind() != query::QueryValueKind::Value || !seed.isPublished() ||
      prelude.kind() != query::QueryValueKind::Value ||
      prelude.value().core().encode().asPtr() != key.crate().encode().asPtr() ||
      seed.lease().capability().coreContext().digest() != prelude.value().coreContext().digest() ||
      seed.lease().capability().distribution() != distribution.value().digest() ||
      !sameRoleEntries(stableRolesFromMaterialized(seed.lease().capability().roles()).asPtr(),
                       prelude.value().roles())) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto record = CoreRoleAuthorityRecord::from(
      key.crate().clone(), prelude.value().coreContext().clone(),
      distribution.value().policyTemplate().revision(),
      seed.lease().capability().revision().clone(), prelude.value().revision().clone(),
      stableRolesFromMaterialized(seed.lease().capability().roles()));
  if (record == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(record)));
}

bool CoreRoleAuthorityQuery::verify(query::QueryContext& context, const Key& key,
                                    const query::TypedQueryResult<Value>& result) {
  return CoreLibraryQueryVerifier::verifyRoleAuthority(context, key, result);
}

struct VerifiedCoreBootstrapModuleInterface::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       CoreBootstrapModuleInterfaceRecord&& record, core::VerifiedCoreSignatureFacts&& signatures,
       core::VerifiedCoreImportedSignatureView&& importedSignatures, BoundModuleLease&& bound,
       RoleSeedLease&& seed)
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        record(zc::mv(record)),
        signatures(zc::mv(signatures)),
        importedSignatures(zc::mv(importedSignatures)),
        bound(zc::mv(bound)),
        seed(zc::mv(seed)) {}

  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  CoreBootstrapModuleInterfaceRecord record;
  core::VerifiedCoreSignatureFacts signatures;
  core::VerifiedCoreImportedSignatureView importedSignatures;
  BoundModuleLease bound;
  RoleSeedLease seed;
};

VerifiedCoreBootstrapModuleInterface::VerifiedCoreBootstrapModuleInterface(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreBootstrapModuleInterface::~VerifiedCoreBootstrapModuleInterface() noexcept(false) =
    default;
VerifiedCoreBootstrapModuleInterface::VerifiedCoreBootstrapModuleInterface(
    VerifiedCoreBootstrapModuleInterface&&) noexcept = default;
VerifiedCoreBootstrapModuleInterface& VerifiedCoreBootstrapModuleInterface::operator=(
    VerifiedCoreBootstrapModuleInterface&&) noexcept = default;

zc::Maybe<VerifiedCoreBootstrapModuleInterface> VerifiedCoreBootstrapModuleInterface::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    CoreBootstrapModuleInterfaceRecord&& record, core::VerifiedCoreSignatureFacts&& signatures,
    core::VerifiedCoreImportedSignatureView&& importedSignatures, BoundModuleLease&& bound,
    RoleSeedLease&& seed) {
  const auto& module = bound.capability();
  const auto& roles = seed.capability();
  if (module.context() != context || module.fingerprint().digest() != fingerprint.digest() ||
      module.module().encode().asPtr() != record.module().encode().asPtr() ||
      module.module().crate().encode().asPtr() != record.core().encode().asPtr() ||
      roles.context() != context || roles.fingerprint().digest() != fingerprint.digest() ||
      roles.coreContext().digest() != record.coreContext().digest() ||
      roles.revision().digest() != record.roleSeedRevision().digest() ||
      !sameRoleEntries(record.roles(), stableRolesFromMaterialized(roles.roles()).asPtr()) ||
      signatures.surface() != record.surface() ||
      importedSignatures.requester() != module.definitions().module()) {
    return zc::none;
  }
  return VerifiedCoreBootstrapModuleInterface(
      zc::heap<Impl>(context, zc::mv(fingerprint), zc::mv(record), zc::mv(signatures),
                     zc::mv(importedSignatures), zc::mv(bound), zc::mv(seed)));
}

identity::SemanticContextBrand VerifiedCoreBootstrapModuleInterface::context() const noexcept {
  return impl->context;
}
const identity::ContextFingerprint& VerifiedCoreBootstrapModuleInterface::fingerprint()
    const noexcept {
  return impl->fingerprint;
}
const CoreBootstrapModuleInterfaceRecord& VerifiedCoreBootstrapModuleInterface::record()
    const noexcept {
  return impl->record;
}
const core::VerifiedCoreSignatureFacts& VerifiedCoreBootstrapModuleInterface::signatures()
    const noexcept {
  return impl->signatures;
}
const core::VerifiedCoreImportedSignatureView&
VerifiedCoreBootstrapModuleInterface::importedSignatures() const noexcept {
  return impl->importedSignatures;
}
const VerifiedCoreBootstrapModuleInterface::BoundModuleLease&
VerifiedCoreBootstrapModuleInterface::boundModuleLease() const noexcept {
  return impl->bound;
}
const VerifiedCoreBootstrapModuleInterface::RoleSeedLease&
VerifiedCoreBootstrapModuleInterface::roleSeedLease() const noexcept {
  return impl->seed;
}
zc::Array<uint8_t> VerifiedCoreBootstrapModuleInterface::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(boundModuleLease().stableWitness());
  encoder.encodeByteString(roleSeedLease().stableWitness());
  encoder.encodeDigest(fingerprint().digest());
  encoder.encodeByteString(record().encodeCanonical().asPtr());
  encoder.encodeByteString(signatures().encodeCanonical().asPtr());
  encoder.encodeByteString(importedSignatures().encodeCanonical().asPtr());
  return frame(kVerifiedCoreBootstrapInterfaceWitnessDomain, encoder.finish().asPtr());
}

namespace {

zc::Maybe<identity::DefId> authorityRole(const VerifiedCoreRoleSeed& seed,
                                         source::core::CoreSemanticRole role) {
  zc::Maybe<identity::DefId> selected;
  for (const auto& entry : seed.roles()) {
    if (entry.role != role) { continue; }
    if (selected != zc::none) { return zc::none; }
    selected = entry.definition;
  }
  return selected;
}

zc::Maybe<zc::Vector<core::CoreMarkerRole>> materializedMarkerRoles(
    const VerifiedCoreRoleSeed& seed) {
  if (seed.roles().size() != 2 || seed.roles()[0].role != source::core::CoreSemanticRole::Copy ||
      seed.roles()[1].role != source::core::CoreSemanticRole::Linear) {
    return zc::none;
  }
  zc::Vector<core::CoreMarkerRole> roles(seed.roles().size());
  for (const auto& role : seed.roles()) {
    if (!role.definition.isValid() || !role.definition.belongsTo(seed.context())) {
      return zc::none;
    }
    roles.add(core::CoreMarkerRole{role.role, role.key.clone(), role.definition});
  }
  return roles;
}

bool policyTemplateIsCanonical(const source::core::CoreStandardMarkerPolicyTemplate& templateValue,
                               zc::ArrayPtr<const CoreRoleSeedEntry> roles) {
  if (templateValue.entries().size() != 1 ||
      templateValue.entries()[0].role != source::core::CoreSemanticRole::Copy ||
      !roleEntriesAreCanonical(roles)) {
    return false;
  }
  for (const auto& reference : templateValue.entries()[0].policy.referenceRules()) {
    auto required = reference.rule.requiredRole();
    if (reference.rule.kind() == source::core::CoreMarkerReferenceTemplateRuleKind::Unconditional) {
      if (required != zc::none) { return false; }
      continue;
    }
    if (reference.rule.kind() != source::core::CoreMarkerReferenceTemplateRuleKind::Requires ||
        required == zc::none) {
      return false;
    }
    size_t matches = 0;
    for (const auto& role : roles) {
      if (role.role == ZC_ASSERT_NONNULL(required)) { ++matches; }
    }
    if (matches != 1) { return false; }
  }
  return templateValue.encode().asPtr() == templateValue.clone().encode().asPtr();
}

}  // namespace

struct VerifiedCoreAuthorityBundle::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       CoreRoleAuthorityRecord&& record, core::VerifiedCoreMarkerShapeInventory&& shapes,
       core::VerifiedCoreMarkerPolicyRegistry&& policies,
       core::VerifiedCoreStandardMarkerAuthority&& authority, identity::ModuleId prelude,
       RoleSeedLease&& roleSeed, PreludeBoundModuleLease&& preludeBoundModule) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        record(zc::mv(record)),
        shapes(zc::mv(shapes)),
        policies(zc::mv(policies)),
        authority(zc::mv(authority)),
        prelude(prelude),
        roleSeed(zc::mv(roleSeed)),
        preludeBoundModule(zc::mv(preludeBoundModule)) {}

  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  CoreRoleAuthorityRecord record;
  core::VerifiedCoreMarkerShapeInventory shapes;
  core::VerifiedCoreMarkerPolicyRegistry policies;
  core::VerifiedCoreStandardMarkerAuthority authority;
  identity::ModuleId prelude;
  RoleSeedLease roleSeed;
  PreludeBoundModuleLease preludeBoundModule;
};

VerifiedCoreAuthorityBundle::VerifiedCoreAuthorityBundle(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreAuthorityBundle::~VerifiedCoreAuthorityBundle() noexcept(false) = default;
VerifiedCoreAuthorityBundle::VerifiedCoreAuthorityBundle(VerifiedCoreAuthorityBundle&&) noexcept =
    default;
VerifiedCoreAuthorityBundle& VerifiedCoreAuthorityBundle::operator=(
    VerifiedCoreAuthorityBundle&&) noexcept = default;

zc::Maybe<VerifiedCoreAuthorityBundle> VerifiedCoreAuthorityBundle::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    CoreRoleAuthorityRecord&& record,
    source::core::CoreStandardMarkerPolicyTemplate&& policyTemplate,
    identity::ModuleKey&& preludeKey, RoleSeedLease&& roleSeed, PreludeBoundModuleLease&& prelude) {
  const auto& seed = roleSeed.capability();
  const auto& preludeBoundModule = prelude.capability();
  auto copy = authorityRole(seed, source::core::CoreSemanticRole::Copy);
  auto linear = authorityRole(seed, source::core::CoreSemanticRole::Linear);
  auto roles = materializedMarkerRoles(seed);
  if (!context.isValid() || roleSeed.revision() != seed.markerBoundModuleLease().revision() ||
      seed.context() != context || preludeBoundModule.context() != context ||
      seed.fingerprint().digest() != fingerprint.digest() ||
      preludeBoundModule.fingerprint().digest() != fingerprint.digest() ||
      record.coreContext().digest() != seed.coreContext().digest() ||
      record.policyTemplateRevision() != policyTemplate.revision() ||
      record.roleSeedRevision().digest() != seed.revision().digest() ||
      !sameRoleEntries(record.roles(), stableRolesFromMaterialized(seed.roles()).asPtr()) ||
      !policyTemplateIsCanonical(policyTemplate, record.roles()) || copy == zc::none ||
      linear == zc::none || roles == zc::none ||
      preludeBoundModule.module().encode().asPtr() != preludeKey.encode().asPtr() ||
      ZC_ASSERT_NONNULL(copy) == ZC_ASSERT_NONNULL(linear)) {
    return zc::none;
  }
  zc::Vector<core::CoreMarkerShapeEntry> shapeEntries(ZC_ASSERT_NONNULL(roles).size());
  for (const auto& role : ZC_ASSERT_NONNULL(roles)) {
    shapeEntries.add(
        core::CoreMarkerShapeEntry{role.role, role.definition.clone(),
                                   checker::signature::InterfaceMarkerShape::ClosedMarker});
  }
  auto shapes = core::VerifiedCoreMarkerShapeInventory::from(
      context, fingerprint.clone(), record.coreContext().clone(), seed.distribution(),
      seed.revision().digest(), zc::mv(shapeEntries));
  if (shapes == zc::none) { return zc::none; }
  auto resolvedPolicy = core::CoreResolvedMarkerPolicy::from(policyTemplate.entries()[0].policy,
                                                             ZC_ASSERT_NONNULL(roles).asPtr());
  if (resolvedPolicy == zc::none) { return zc::none; }
  zc::Vector<core::CoreMarkerPolicyEntry> policyEntries(1);
  policyEntries.add(core::CoreMarkerPolicyEntry{source::core::CoreSemanticRole::Copy,
                                                ZC_ASSERT_NONNULL(roles)[0].definition.clone(),
                                                zc::mv(ZC_ASSERT_NONNULL(resolvedPolicy))});
  auto policies = core::VerifiedCoreMarkerPolicyRegistry::from(
      context, fingerprint.clone(), record.coreContext().clone(), seed.distribution(),
      seed.revision().digest(), policyTemplate.revision(), ZC_ASSERT_NONNULL(shapes),
      zc::mv(policyEntries));
  if (policies == zc::none) { return zc::none; }
  auto authority = core::VerifiedCoreStandardMarkerAuthority::from(
      context, fingerprint.clone(), record.coreContext().clone(), policyTemplate.revision(),
      ZC_ASSERT_NONNULL(shapes), ZC_ASSERT_NONNULL(policies), zc::mv(preludeKey),
      zc::mv(ZC_ASSERT_NONNULL(roles)));
  if (authority == zc::none) { return zc::none; }
  return VerifiedCoreAuthorityBundle(zc::heap<Impl>(
      context, zc::mv(fingerprint), zc::mv(record), zc::mv(ZC_ASSERT_NONNULL(shapes)),
      zc::mv(ZC_ASSERT_NONNULL(policies)), zc::mv(ZC_ASSERT_NONNULL(authority)),
      preludeBoundModule.definitions().module(), zc::mv(roleSeed), zc::mv(prelude)));
}

identity::SemanticContextBrand VerifiedCoreAuthorityBundle::context() const noexcept {
  return impl->context;
}
const identity::ContextFingerprint& VerifiedCoreAuthorityBundle::fingerprint()
    const noexcept {
  return impl->fingerprint;
}
const CoreRoleAuthorityRecord& VerifiedCoreAuthorityBundle::record() const noexcept {
  return impl->record;
}
const core::VerifiedCoreMarkerShapeInventory& VerifiedCoreAuthorityBundle::shapes() const noexcept {
  return impl->shapes;
}
const core::VerifiedCoreMarkerPolicyRegistry& VerifiedCoreAuthorityBundle::policies()
    const noexcept {
  return impl->policies;
}
const core::VerifiedCoreStandardMarkerAuthority& VerifiedCoreAuthorityBundle::authority()
    const noexcept {
  return impl->authority;
}
identity::ModuleId VerifiedCoreAuthorityBundle::preludeModule() const noexcept {
  return impl->prelude;
}
identity::DefId VerifiedCoreAuthorityBundle::copy() const noexcept { return authority().copy(); }
identity::DefId VerifiedCoreAuthorityBundle::linear() const noexcept {
  return authority().linear();
}
const VerifiedCoreAuthorityBundle::RoleSeedLease& VerifiedCoreAuthorityBundle::roleSeedLease()
    const noexcept {
  return impl->roleSeed;
}
const VerifiedCoreAuthorityBundle::PreludeBoundModuleLease&
VerifiedCoreAuthorityBundle::preludeBoundModuleLease() const noexcept {
  return impl->preludeBoundModule;
}
zc::Array<uint8_t> VerifiedCoreAuthorityBundle::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(impl->roleSeed.stableWitness());
  encoder.encodeByteString(impl->preludeBoundModule.stableWitness());
  encoder.encodeDigest(fingerprint().digest());
  encoder.encodeByteString(record().encodeCanonical().asPtr());
  encoder.encodeByteString(shapes().encodeCanonical().asPtr());
  encoder.encodeByteString(policies().encodeCanonical().asPtr());
  encoder.encodeByteString(authority().encodeCanonical().asPtr());
  return frame("zom.query.verified-core-authority-witness"_zc, encoder.finish().asPtr());
}

namespace {

constexpr zc::StringPtr kCoreModuleInterfaceRevisionDomain = "zom.core-module-interface"_zc;
constexpr zc::StringPtr kCoreBindingSurfaceRevisionDomain = "zom.core-binding-surface"_zc;
constexpr zc::StringPtr kCoreModuleInterfaceValueDomain =
    "zom.query.core-module-interface-value"_zc;
constexpr zc::StringPtr kVerifiedCoreModuleInterfaceWitnessDomain =
    "zom.query.verified-core-module-interface-witness"_zc;

bool finalRolesAreCanonical(zc::ArrayPtr<const core::CoreMarkerShapeEntry> roles) {
  if (roles.size() == 0) { return true; }
  return roles.size() == 2 && roles[0].role == source::core::CoreSemanticRole::Copy &&
         roles[1].role == source::core::CoreSemanticRole::Linear &&
         roles[0].definition != roles[1].definition &&
         roles[0].shape == checker::signature::InterfaceMarkerShape::ClosedMarker &&
         roles[1].shape == checker::signature::InterfaceMarkerShape::ClosedMarker;
}

zc::Array<uint8_t> encodeFinalRole(const core::CoreMarkerShapeEntry& role) {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(role.role));
  role.definition.encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(role.shape));
  return encoder.finish();
}

zc::Vector<binder::StableExportedBinding> cloneFinalBindings(
    zc::ArrayPtr<const binder::StableExportedBinding> bindings) {
  zc::Vector<binder::StableExportedBinding> cloned(bindings.size());
  for (const auto& binding : bindings) { cloned.add(binding.clone()); }
  return cloned;
}

zc::Vector<core::TypeFreeInterfaceSignatureRecord> cloneFinalSignatures(
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> signatures) {
  zc::Vector<core::TypeFreeInterfaceSignatureRecord> cloned(signatures.size());
  for (const auto& signature : signatures) { cloned.add(signature.clone()); }
  return cloned;
}

zc::Maybe<binder::MemberVisibility> cloneVisibility(
    const zc::Maybe<binder::MemberVisibility>& visibility) {
  ZC_IF_SOME(value, visibility) { return value; }
  return zc::none;
}

zc::Array<uint8_t> encodeFinalSignatureRoot(const CoreFinalSignatureRoot& root) {
  identity::CanonicalEncoder encoder;
  root.binding.encode(encoder);
  root.canonicalDefinition.encode(encoder);
  ZC_IF_SOME(visibility, root.visibility) {
    encoder.encodeSome();
    encoder.encodeUint8(static_cast<uint8_t>(visibility));
  } else {
    encoder.encodeNone();
  }
  encoder.encodeByteString(root.sourceModule.encode().asPtr());
  encoder.encodeDigest(root.bindingSurfaceRevision.digest());
  return encoder.finish();
}

zc::Array<uint8_t> encodeFinalModuleTarget(const CoreCanonicalModuleTarget& target) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(
      binder::StableBindingCodec<binder::BindingNameKey>::encode(target.name).asPtr());
  encoder.encodeByteString(target.module.encode().asPtr());
  encoder.encodeDigest(target.surfaceRevision.digest());
  return encoder.finish();
}

zc::Vector<CoreFinalSignatureRoot> cloneFinalSignatureRoots(
    zc::ArrayPtr<const CoreFinalSignatureRoot> roots) {
  zc::Vector<CoreFinalSignatureRoot> cloned(roots.size());
  for (const auto& root : roots) { cloned.add(root.clone()); }
  return cloned;
}

zc::Vector<CoreCanonicalModuleTarget> cloneFinalModuleTargets(
    zc::ArrayPtr<const CoreCanonicalModuleTarget> targets) {
  zc::Vector<CoreCanonicalModuleTarget> cloned(targets.size());
  for (const auto& target : targets) { cloned.add(target.clone()); }
  return cloned;
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

bool finalSignatureRootsAreCanonical(zc::ArrayPtr<const CoreFinalSignatureRoot> roots) {
  zc::Maybe<zc::Array<uint8_t>> previous;
  for (const auto& root : roots) {
    if (root.binding.encode().asPtr() != root.canonicalDefinition.encode().asPtr()) {
      return false;
    }
    auto encoded = encodeFinalSignatureRoot(root);
    ZC_IF_SOME(last, previous) {
      if (compareCanonicalBytes(last.asPtr(), encoded.asPtr()) >= 0) { return false; }
    }
    previous = zc::mv(encoded);
  }
  return true;
}

bool finalSignatureRootsMatchDefinitions(
    zc::ArrayPtr<const CoreFinalSignatureRoot> roots,
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> lookupDefinitions,
    const identity::ModuleKey& module, const CoreBindingSurfaceRevision& bindingSurface) {
  for (const auto& root : roots) {
    bool definitionFound = false;
    for (const auto& signature : lookupDefinitions) {
      if (root.canonicalDefinition == signature.definition()) {
        definitionFound = true;
        break;
      }
    }
    if (!definitionFound ||
        root.sourceModule.crate().encode().asPtr() != module.crate().encode().asPtr() ||
        (root.sourceModule.encode().asPtr() == module.encode().asPtr() &&
         root.bindingSurfaceRevision != bindingSurface)) {
      return false;
    }
  }
  return true;
}

bool finalModuleTargetsAreCanonical(zc::ArrayPtr<const CoreCanonicalModuleTarget> targets) {
  zc::Maybe<zc::Array<uint8_t>> previous;
  for (const auto& target : targets) {
    auto encoded = encodeFinalModuleTarget(target);
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
  return finalSignatureRootsAreCanonical(roots.asPtr())
             ? zc::Maybe<zc::Vector<CoreFinalSignatureRoot>>(zc::mv(roots))
             : zc::none;
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

zc::Array<uint8_t> encodeCoreModuleInterfacePayload(
    const identity::ModuleKey& module, const identity::CoreSemanticContextFingerprint& context,
    const CoreBindingSurfaceRevision& bindingSurface,
    zc::ArrayPtr<const binder::StableExportedBinding> visibleBindings,
    zc::ArrayPtr<const binder::StableExportedBinding> exportedBindings,
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> lookupDefinitions,
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> supportDefinitions,
    zc::ArrayPtr<const CoreFinalSignatureRoot> signatureRoots,
    zc::ArrayPtr<const CoreCanonicalModuleTarget> moduleTargets,
    zc::ArrayPtr<const core::CoreMarkerShapeEntry> roles,
    const core::CoreStandardMarkerAuthorityRevision& authority,
    zc::Maybe<const CoreModuleInterfaceRevision&> revision) {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(module.encode().asPtr());
  encoder.encodeDigest(context.digest());
  encoder.encodeDigest(bindingSurface.digest());
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
  encoder.encodeSequenceSize(lookupDefinitions.size());
  for (const auto& definition : lookupDefinitions) {
    encoder.encodeByteString(definition.encodeCanonical().asPtr());
  }
  encoder.encodeSequenceSize(supportDefinitions.size());
  for (const auto& definition : supportDefinitions) {
    encoder.encodeByteString(definition.encodeCanonical().asPtr());
  }
  encoder.encodeSequenceSize(signatureRoots.size());
  for (const auto& root : signatureRoots) {
    encoder.encodeByteString(encodeFinalSignatureRoot(root).asPtr());
  }
  encoder.encodeSequenceSize(moduleTargets.size());
  for (const auto& target : moduleTargets) {
    encoder.encodeByteString(encodeFinalModuleTarget(target).asPtr());
  }
  encoder.encodeSequenceSize(roles.size());
  for (const auto& role : roles) { encoder.encodeByteString(encodeFinalRole(role).asPtr()); }
  encoder.encodeDigest(authority.digest());
  ZC_IF_SOME(value, revision) { encoder.encodeDigest(value.digest()); }
  return encoder.finish();
}

zc::Maybe<identity::Sha256Digest> computeCoreBindingSurfaceRevision(
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
  return identity::sha256(
      frame(kCoreBindingSurfaceRevisionDomain, encoder.finish().asPtr()).asPtr());
}

zc::Maybe<identity::Sha256Digest> computeCoreModuleInterfaceRevision(
    const identity::ModuleKey& module, const identity::CoreSemanticContextFingerprint& context,
    const CoreBindingSurfaceRevision& bindingSurface,
    zc::ArrayPtr<const binder::StableExportedBinding> visibleBindings,
    zc::ArrayPtr<const binder::StableExportedBinding> exportedBindings,
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> lookupDefinitions,
    zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> supportDefinitions,
    zc::ArrayPtr<const CoreFinalSignatureRoot> signatureRoots,
    zc::ArrayPtr<const CoreCanonicalModuleTarget> moduleTargets,
    zc::ArrayPtr<const core::CoreMarkerShapeEntry> roles,
    const core::CoreStandardMarkerAuthorityRevision& authority) {
  zc::Maybe<const CoreModuleInterfaceRevision&> noRevision;
  auto payload = encodeCoreModuleInterfacePayload(
      module, context, bindingSurface, visibleBindings, exportedBindings, lookupDefinitions,
      supportDefinitions, signatureRoots, moduleTargets, roles, authority, noRevision);
  return identity::sha256(frame(kCoreModuleInterfaceRevisionDomain, payload.asPtr()).asPtr());
}

zc::Maybe<zc::Vector<core::CoreMarkerShapeEntry>> finalRoles(
    CoreBootstrapModuleSurface surface, const VerifiedCoreAuthorityBundle& authority) {
  zc::Vector<core::CoreMarkerShapeEntry> roles;
  if (surface != CoreBootstrapModuleSurface::Marker) { return roles; }
  if (!finalRolesAreCanonical(authority.shapes().shapes())) { return zc::none; }
  roles = zc::Vector<core::CoreMarkerShapeEntry>(authority.shapes().shapes().size());
  for (const auto& role : authority.shapes().shapes()) { roles.add(role.clone()); }
  return roles;
}

zc::OneOf<VerifiedCoreModuleInterface, query::QueryRuntimeFailure> finalizeCoreModuleInterface(
    query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context,
    const ContextualCoreModuleKey& key) {
  auto coreKey =
      ContextualCoreCrateKey::from(key.contextRoots().clone(), key.module().crate().clone());
  if (coreKey == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto bootstrap = context.getCapability<MaterializeCoreBootstrapModuleInterfaceQuery>(key.clone());
  auto authority =
      context.getCapability<MaterializeCoreAuthorityQuery>(ZC_ASSERT_NONNULL(coreKey).clone());
  auto seedRecord = context.get<CoreRoleSeedQuery>(ZC_ASSERT_NONNULL(coreKey).clone());
  if (!bootstrap.isPublished() || !authority.isPublished() || seedRecord.isRuntimeFailure() ||
      seedRecord.kind() != query::QueryValueKind::Value) {
    return bootstrap.isRuntimeRejected()   ? bootstrap.runtimeFailure()
           : authority.isRuntimeRejected() ? authority.runtimeFailure()
                                           : query::QueryRuntimeFailure::ProviderRejected;
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
    return query::QueryRuntimeFailure::ProviderRejected;
  }
  auto roles = finalRoles(bootstrapValue.record().surface(), authorityValue);
  if (roles == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto bindings = finalBindings(context, key.module());
  if (bindings == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto lookupDefinitions =
      finalSignatures(bootstrapValue.record().surface(), bootstrapValue.signatures(), seed);
  if (lookupDefinitions == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  identity::ModuleKey signatureSource = key.module().clone();
  zc::Maybe<CoreBindingSurfaceRevision> sourceSurface;
  if (bootstrapValue.record().surface() == CoreBootstrapModuleSurface::Prelude) {
    auto markerKey = ContextualCoreModuleKey::from(key.contextRoots().clone(),
                                                   seedRecord.value().markerModule().clone());
    if (markerKey == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
    auto marker = context.getCapability<FinalizeCoreModuleInterfaceQuery>(
        zc::mv(ZC_ASSERT_NONNULL(markerKey)));
    if (!marker.isPublished() || marker.lease().capability().record().module().encode().asPtr() !=
                                     seedRecord.value().markerModule().encode().asPtr()) {
      return query::QueryRuntimeFailure::ProviderRejected;
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
  auto bindingSurface = computeCoreBindingSurfaceRevision(
      key.module(), bootstrapValue.record().coreContext(), visibleBindings.asPtr(),
      ZC_ASSERT_NONNULL(bindings).asPtr());
  if (bindingSurface == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  CoreBindingSurfaceRevision signatureSurface =
      CoreBindingSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(bindingSurface));
  ZC_IF_SOME(value, sourceSurface) { signatureSurface = value.clone(); }
  auto signatureRoots =
      finalSignatureRoots(signatureSource, signatureSurface, ZC_ASSERT_NONNULL(bindings).asPtr(),
                          ZC_ASSERT_NONNULL(lookupDefinitions).asPtr());
  if (signatureRoots == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  zc::Vector<CoreCanonicalModuleTarget> moduleTargets;
  auto record = CoreModuleInterfaceRecord::from(
      key.module().clone(), bootstrapValue.record().coreContext().clone(), zc::mv(visibleBindings),
      zc::mv(ZC_ASSERT_NONNULL(bindings)), zc::mv(ZC_ASSERT_NONNULL(lookupDefinitions)),
      zc::mv(supportDefinitions), zc::mv(ZC_ASSERT_NONNULL(signatureRoots)), zc::mv(moduleTargets),
      zc::mv(ZC_ASSERT_NONNULL(roles)), authorityValue.authority().revision().clone());
  if (record == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  auto result = VerifiedCoreModuleInterface::from(
      bootstrapValue.context(), bootstrapValue.fingerprint().clone(), bound.definitions().module(),
      zc::mv(ZC_ASSERT_NONNULL(record)));
  if (result == zc::none) { return query::QueryRuntimeFailure::ProviderRejected; }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

}  // namespace

CoreModuleInterfaceRevision::CoreModuleInterfaceRevision(
    const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}
CoreModuleInterfaceRevision CoreModuleInterfaceRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CoreModuleInterfaceRevision(digest);
}
CoreModuleInterfaceRevision CoreModuleInterfaceRevision::clone() const noexcept {
  return CoreModuleInterfaceRevision(digestValue);
}
const identity::Sha256Digest& CoreModuleInterfaceRevision::digest() const noexcept {
  return digestValue;
}
bool CoreModuleInterfaceRevision::operator==(
    const CoreModuleInterfaceRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

CoreBindingSurfaceRevision::CoreBindingSurfaceRevision(
    const identity::Sha256Digest& digest) noexcept
    : digestValue(digest) {}
CoreBindingSurfaceRevision CoreBindingSurfaceRevision::fromDigest(
    const identity::Sha256Digest& digest) noexcept {
  return CoreBindingSurfaceRevision(digest);
}
CoreBindingSurfaceRevision CoreBindingSurfaceRevision::clone() const noexcept {
  return CoreBindingSurfaceRevision(digestValue);
}
const identity::Sha256Digest& CoreBindingSurfaceRevision::digest() const noexcept {
  return digestValue;
}
bool CoreBindingSurfaceRevision::operator==(
    const CoreBindingSurfaceRevision& other) const noexcept {
  return digestValue == other.digestValue;
}

CoreFinalSignatureRoot CoreFinalSignatureRoot::clone() const {
  return CoreFinalSignatureRoot{binding.clone(), canonicalDefinition.clone(),
                                cloneVisibility(visibility), sourceModule.clone(),
                                bindingSurfaceRevision.clone()};
}

CoreCanonicalModuleTarget CoreCanonicalModuleTarget::clone() const {
  return CoreCanonicalModuleTarget{name.clone(), module.clone(), surfaceRevision.clone()};
}

struct CoreModuleInterfaceRecord::Impl final {
  Impl(identity::ModuleKey&& module, identity::CoreSemanticContextFingerprint&& context,
       CoreBindingSurfaceRevision bindingSurface,
       zc::Vector<binder::StableExportedBinding>&& visibleBindings,
       zc::Vector<binder::StableExportedBinding>&& exportedBindings,
       zc::Vector<core::TypeFreeInterfaceSignatureRecord>&& lookupDefinitions,
       zc::Vector<core::TypeFreeInterfaceSignatureRecord>&& supportDefinitions,
       zc::Vector<CoreFinalSignatureRoot>&& signatureRoots,
       zc::Vector<CoreCanonicalModuleTarget>&& moduleTargets,
       zc::Vector<core::CoreMarkerShapeEntry>&& roles,
       core::CoreStandardMarkerAuthorityRevision authority, CoreModuleInterfaceRevision revision)
      : module(zc::mv(module)),
        context(zc::mv(context)),
        bindingSurface(zc::mv(bindingSurface)),
        visibleBindings(zc::mv(visibleBindings)),
        exportedBindings(zc::mv(exportedBindings)),
        lookupDefinitions(zc::mv(lookupDefinitions)),
        supportDefinitions(zc::mv(supportDefinitions)),
        signatureRoots(zc::mv(signatureRoots)),
        moduleTargets(zc::mv(moduleTargets)),
        roles(zc::mv(roles)),
        authority(zc::mv(authority)),
        revision(zc::mv(revision)) {}
  identity::ModuleKey module;
  identity::CoreSemanticContextFingerprint context;
  CoreBindingSurfaceRevision bindingSurface;
  zc::Vector<binder::StableExportedBinding> visibleBindings;
  zc::Vector<binder::StableExportedBinding> exportedBindings;
  zc::Vector<core::TypeFreeInterfaceSignatureRecord> lookupDefinitions;
  zc::Vector<core::TypeFreeInterfaceSignatureRecord> supportDefinitions;
  zc::Vector<CoreFinalSignatureRoot> signatureRoots;
  zc::Vector<CoreCanonicalModuleTarget> moduleTargets;
  zc::Vector<core::CoreMarkerShapeEntry> roles;
  core::CoreStandardMarkerAuthorityRevision authority;
  CoreModuleInterfaceRevision revision;
};

CoreModuleInterfaceRecord::CoreModuleInterfaceRecord(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
CoreModuleInterfaceRecord::~CoreModuleInterfaceRecord() noexcept(false) = default;
CoreModuleInterfaceRecord::CoreModuleInterfaceRecord(CoreModuleInterfaceRecord&&) noexcept =
    default;
CoreModuleInterfaceRecord& CoreModuleInterfaceRecord::operator=(
    CoreModuleInterfaceRecord&&) noexcept = default;

zc::Maybe<CoreModuleInterfaceRecord> CoreModuleInterfaceRecord::from(
    identity::ModuleKey&& module, identity::CoreSemanticContextFingerprint&& context,
    zc::Vector<binder::StableExportedBinding>&& visibleBindings,
    zc::Vector<binder::StableExportedBinding>&& exportedBindings,
    zc::Vector<core::TypeFreeInterfaceSignatureRecord>&& lookupDefinitions,
    zc::Vector<core::TypeFreeInterfaceSignatureRecord>&& supportDefinitions,
    zc::Vector<CoreFinalSignatureRoot>&& signatureRoots,
    zc::Vector<CoreCanonicalModuleTarget>&& moduleTargets,
    zc::Vector<core::CoreMarkerShapeEntry>&& roles,
    core::CoreStandardMarkerAuthorityRevision authority) {
  auto expectedContext = identity::CoreSemanticContextFingerprint::compute(module.crate());
  if (expectedContext == zc::none ||
      ZC_ASSERT_NONNULL(expectedContext).digest() != context.digest() ||
      !finalBindingsAreCanonical(visibleBindings.asPtr()) ||
      !finalBindingsAreCanonical(exportedBindings.asPtr()) ||
      !finalSignaturesAreCanonical(lookupDefinitions.asPtr()) ||
      !finalSignaturesAreCanonical(supportDefinitions.asPtr()) ||
      !finalSignatureRootsAreCanonical(signatureRoots.asPtr()) ||
      !finalModuleTargetsAreCanonical(moduleTargets.asPtr()) ||
      !finalRolesAreCanonical(roles.asPtr())) {
    return zc::none;
  }
  auto bindingSurface = computeCoreBindingSurfaceRevision(module, context, visibleBindings.asPtr(),
                                                          exportedBindings.asPtr());
  if (bindingSurface == zc::none) { return zc::none; }
  auto finalBindingSurface =
      CoreBindingSurfaceRevision::fromDigest(ZC_ASSERT_NONNULL(bindingSurface));
  if (!finalSignatureRootsMatchDefinitions(signatureRoots.asPtr(), lookupDefinitions.asPtr(),
                                           module, finalBindingSurface)) {
    return zc::none;
  }
  auto revision = computeCoreModuleInterfaceRevision(
      module, context, finalBindingSurface, visibleBindings.asPtr(), exportedBindings.asPtr(),
      lookupDefinitions.asPtr(), supportDefinitions.asPtr(), signatureRoots.asPtr(),
      moduleTargets.asPtr(), roles.asPtr(), authority);
  if (revision == zc::none) { return zc::none; }
  return CoreModuleInterfaceRecord(zc::heap<Impl>(
      zc::mv(module), zc::mv(context), zc::mv(finalBindingSurface), zc::mv(visibleBindings),
      zc::mv(exportedBindings), zc::mv(lookupDefinitions), zc::mv(supportDefinitions),
      zc::mv(signatureRoots), zc::mv(moduleTargets), zc::mv(roles), zc::mv(authority),
      CoreModuleInterfaceRevision::fromDigest(ZC_ASSERT_NONNULL(revision))));
}

CoreModuleInterfaceRecord CoreModuleInterfaceRecord::clone() const {
  auto visibleBindings = cloneFinalBindings(impl->visibleBindings.asPtr());
  auto exportedBindings = cloneFinalBindings(impl->exportedBindings.asPtr());
  auto lookupDefinitions = cloneFinalSignatures(impl->lookupDefinitions.asPtr());
  auto supportDefinitions = cloneFinalSignatures(impl->supportDefinitions.asPtr());
  auto signatureRoots = cloneFinalSignatureRoots(impl->signatureRoots.asPtr());
  auto moduleTargets = cloneFinalModuleTargets(impl->moduleTargets.asPtr());
  zc::Vector<core::CoreMarkerShapeEntry> roles(impl->roles.size());
  for (const auto& role : impl->roles) { roles.add(role.clone()); }
  return CoreModuleInterfaceRecord(
      zc::heap<Impl>(impl->module.clone(), impl->context.clone(), impl->bindingSurface.clone(),
                     zc::mv(visibleBindings), zc::mv(exportedBindings), zc::mv(lookupDefinitions),
                     zc::mv(supportDefinitions), zc::mv(signatureRoots), zc::mv(moduleTargets),
                     zc::mv(roles), impl->authority.clone(), impl->revision.clone()));
}
const identity::ModuleKey& CoreModuleInterfaceRecord::module() const noexcept {
  return impl->module;
}
const identity::CoreSemanticContextFingerprint& CoreModuleInterfaceRecord::coreContext()
    const noexcept {
  return impl->context;
}
const CoreBindingSurfaceRevision& CoreModuleInterfaceRecord::bindingSurfaceRevision()
    const noexcept {
  return impl->bindingSurface;
}
zc::ArrayPtr<const binder::StableExportedBinding> CoreModuleInterfaceRecord::visibleBindings()
    const noexcept {
  return impl->visibleBindings.asPtr();
}
zc::ArrayPtr<const binder::StableExportedBinding> CoreModuleInterfaceRecord::exportedBindings()
    const noexcept {
  return impl->exportedBindings.asPtr();
}
zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord>
CoreModuleInterfaceRecord::lookupDefinitions() const noexcept {
  return impl->lookupDefinitions.asPtr();
}
zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord>
CoreModuleInterfaceRecord::supportDefinitions() const noexcept {
  return impl->supportDefinitions.asPtr();
}
zc::ArrayPtr<const CoreFinalSignatureRoot> CoreModuleInterfaceRecord::signatureRoots()
    const noexcept {
  return impl->signatureRoots.asPtr();
}
zc::ArrayPtr<const CoreCanonicalModuleTarget> CoreModuleInterfaceRecord::moduleTargets()
    const noexcept {
  return impl->moduleTargets.asPtr();
}
zc::ArrayPtr<const core::CoreMarkerShapeEntry> CoreModuleInterfaceRecord::definedRoles()
    const noexcept {
  return impl->roles.asPtr();
}
const core::CoreStandardMarkerAuthorityRevision& CoreModuleInterfaceRecord::authorityRevision()
    const noexcept {
  return impl->authority;
}
const CoreModuleInterfaceRevision& CoreModuleInterfaceRecord::revision() const noexcept {
  return impl->revision;
}
zc::Array<uint8_t> CoreModuleInterfaceRecord::encodeCanonical() const {
  zc::Maybe<const CoreModuleInterfaceRevision&> revisionValue(impl->revision);
  auto payload = encodeCoreModuleInterfacePayload(
      module(), coreContext(), bindingSurfaceRevision(), visibleBindings(), exportedBindings(),
      lookupDefinitions(), supportDefinitions(), signatureRoots(), moduleTargets(), definedRoles(),
      authorityRevision(), revisionValue);
  return frame(kCoreModuleInterfaceValueDomain, payload.asPtr());
}

struct VerifiedCoreModuleInterface::Impl final {
  Impl(identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
       identity::ModuleId module, CoreModuleInterfaceRecord&& record) noexcept
      : context(context),
        fingerprint(zc::mv(fingerprint)),
        module(module),
        record(zc::mv(record)) {}
  identity::SemanticContextBrand context;
  identity::ContextFingerprint fingerprint;
  identity::ModuleId module;
  CoreModuleInterfaceRecord record;
};

VerifiedCoreModuleInterface::VerifiedCoreModuleInterface(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreModuleInterface::~VerifiedCoreModuleInterface() noexcept(false) = default;
VerifiedCoreModuleInterface::VerifiedCoreModuleInterface(VerifiedCoreModuleInterface&&) noexcept =
    default;
VerifiedCoreModuleInterface& VerifiedCoreModuleInterface::operator=(
    VerifiedCoreModuleInterface&&) noexcept = default;
zc::Maybe<VerifiedCoreModuleInterface> VerifiedCoreModuleInterface::from(
    identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
    identity::ModuleId module, CoreModuleInterfaceRecord&& record) {
  if (!context.isValid() || !module.belongsTo(context) ||
      !finalRolesAreCanonical(record.definedRoles())) {
    return zc::none;
  }
  return VerifiedCoreModuleInterface(
      zc::heap<Impl>(context, zc::mv(fingerprint), module, zc::mv(record)));
}
identity::SemanticContextBrand VerifiedCoreModuleInterface::context() const noexcept {
  return impl->context;
}
const identity::ContextFingerprint& VerifiedCoreModuleInterface::fingerprint()
    const noexcept {
  return impl->fingerprint;
}
identity::ModuleId VerifiedCoreModuleInterface::module() const noexcept { return impl->module; }
const CoreModuleInterfaceRecord& VerifiedCoreModuleInterface::record() const noexcept {
  return impl->record;
}
zc::Array<uint8_t> VerifiedCoreModuleInterface::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(fingerprint().digest());
  encoder.encodeByteString(record().module().encode().asPtr());
  encoder.encodeByteString(record().encodeCanonical().asPtr());
  return frame(kVerifiedCoreModuleInterfaceWitnessDomain, encoder.finish().asPtr());
}

zc::Array<uint8_t> CoreBootstrapModuleInterfaceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<CoreBootstrapModuleInterfaceQuery::Key> CoreBootstrapModuleInterfaceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreModuleKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> CoreBootstrapModuleInterfaceQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<CoreBootstrapModuleInterfaceQuery::Value> CoreBootstrapModuleInterfaceQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CoreBootstrapModuleInterfaceRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<CoreBootstrapModuleInterfaceQuery::Value>
CoreBootstrapModuleInterfaceQuery::provide(query::QueryContext& context, const Key& key) {
  return provideCoreBootstrapModuleInterface(context, key);
}

bool CoreBootstrapModuleInterfaceQuery::verify(query::QueryContext& context, const Key& key,
                                               const query::TypedQueryResult<Value>& result) {
  return CoreLibraryQueryVerifier::verifyBootstrapModuleInterfaceRecord(context, key, result);
}

zc::Array<uint8_t> MaterializeCoreBootstrapModuleInterfaceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}
zc::Maybe<MaterializeCoreBootstrapModuleInterfaceQuery::Key>
MaterializeCoreBootstrapModuleInterfaceQuery::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreModuleKey::decodeCanonical(bytes);
}
query::CapabilityProviderResult<MaterializeCoreBootstrapModuleInterfaceQuery>
MaterializeCoreBootstrapModuleInterfaceQuery::provide(
    query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
    const Key& key) {
  auto materialized = materializeCoreBootstrapInterface(context, key);
  if (materialized.is<query::QueryRuntimeFailure>()) {
    return query::CapabilityProviderResult<MaterializeCoreBootstrapModuleInterfaceQuery>::
        runtimeRejected(materialized.get<query::QueryRuntimeFailure>());
  }
  auto candidate = zc::heap<Capability>(zc::mv(materialized).get<Capability>());
  auto witness =
      query::CapabilityCandidateContract<MaterializeCoreBootstrapModuleInterfaceQuery>::encode(
          *candidate);
  return query::CapabilityProviderResult<MaterializeCoreBootstrapModuleInterfaceQuery>::candidate(
      zc::mv(candidate), zc::mv(witness));
}
zc::Maybe<zc::Array<uint8_t>> MaterializeCoreBootstrapModuleInterfaceQuery::verify(
    query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
    const Key& key, const Capability& candidate) {
  return CoreLibraryQueryVerifier::verifyBootstrapModuleInterface(context, key, candidate);
}

zc::Array<uint8_t> MaterializeCoreAuthorityQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<MaterializeCoreAuthorityQuery::Key> MaterializeCoreAuthorityQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreCrateKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<MaterializeCoreAuthorityQuery>
MaterializeCoreAuthorityQuery::provide(
    query::CapabilityQueryContext<MaterializeCoreAuthorityQuery>& context, const Key& key) {
  auto record = context.get<CoreRoleAuthorityQuery>(key.clone());
  auto distribution = context.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto seed = context.getCapability<MaterializeCoreRoleSeedQuery>(key.clone());
  auto graph = context.get<CoreModuleGraphQuery>(key.clone());
  if (record.isRuntimeFailure() || distribution.isRuntimeFailure() || graph.isRuntimeFailure() ||
      record.kind() != query::QueryValueKind::Value ||
      distribution.kind() != query::QueryValueKind::Value || !seed.isPublished() ||
      graph.kind() != query::QueryValueKind::Value) {
    const auto failure = record.isRuntimeFailure()         ? record.runtimeFailure()
                         : distribution.isRuntimeFailure() ? distribution.runtimeFailure()
                         : graph.isRuntimeFailure()        ? graph.runtimeFailure()
                         : seed.isRuntimeRejected()        ? seed.runtimeFailure()
                                                    : query::QueryRuntimeFailure::ProviderRejected;
    return query::CapabilityProviderResult<MaterializeCoreAuthorityQuery>::runtimeRejected(failure);
  }
  auto preludeModule = findInitialCoreModule(graph.value(), CoreBootstrapModuleSurface::Prelude);
  if (preludeModule == zc::none) {
    return query::CapabilityProviderResult<MaterializeCoreAuthorityQuery>::runtimeRejected(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto authorityPrelude = ZC_ASSERT_NONNULL(preludeModule).clone();
  auto preludeKey = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), zc::mv(ZC_ASSERT_NONNULL(preludeModule)));
  auto prelude =
      context.getCapability<module_graph_query::VerifyBoundModuleQuery>(zc::mv(preludeKey));
  if (!prelude.isPublished()) {
    return query::CapabilityProviderResult<MaterializeCoreAuthorityQuery>::runtimeRejected(
        prelude.isRuntimeRejected() ? prelude.runtimeFailure()
                                    : query::QueryRuntimeFailure::ProviderRejected);
  }
  auto candidate = VerifiedCoreAuthorityBundle::from(
      seed.lease().capability().context(), seed.lease().capability().fingerprint().clone(),
      record.value().clone(), distribution.value().policyTemplate().clone(),
      zc::mv(authorityPrelude), zc::mv(seed).takeLease(), zc::mv(prelude).takeLease());
  if (candidate == zc::none) {
    return query::CapabilityProviderResult<MaterializeCoreAuthorityQuery>::runtimeRejected(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto owned = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  auto witness = query::CapabilityCandidateContract<MaterializeCoreAuthorityQuery>::encode(*owned);
  return query::CapabilityProviderResult<MaterializeCoreAuthorityQuery>::candidate(zc::mv(owned),
                                                                                   zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> MaterializeCoreAuthorityQuery::verify(
    query::CapabilityQueryContext<MaterializeCoreAuthorityQuery>& context, const Key& key,
    const Capability& candidate) {
  return CoreLibraryQueryVerifier::verifyCoreAuthority(context, key, candidate);
}

zc::Array<uint8_t> FinalizeCoreModuleInterfaceQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<FinalizeCoreModuleInterfaceQuery::Key> FinalizeCoreModuleInterfaceQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ContextualCoreModuleKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<FinalizeCoreModuleInterfaceQuery>
FinalizeCoreModuleInterfaceQuery::provide(
    query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context, const Key& key) {
  auto finalized = finalizeCoreModuleInterface(context, key);
  if (finalized.is<query::QueryRuntimeFailure>()) {
    return query::CapabilityProviderResult<FinalizeCoreModuleInterfaceQuery>::runtimeRejected(
        finalized.get<query::QueryRuntimeFailure>());
  }
  auto candidate = zc::heap<Capability>(zc::mv(finalized).get<Capability>());
  auto witness =
      query::CapabilityCandidateContract<FinalizeCoreModuleInterfaceQuery>::encode(*candidate);
  return query::CapabilityProviderResult<FinalizeCoreModuleInterfaceQuery>::candidate(
      zc::mv(candidate), zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> FinalizeCoreModuleInterfaceQuery::verify(
    query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context, const Key& key,
    const Capability& candidate) {
  return CoreLibraryQueryVerifier::verifyFinalCoreModuleInterface(context, key, candidate);
}

struct VerifiedCoreProjectionInput::Impl final {
  struct StagedSource final {
    StagedSource(identity::source_query::StableSourceQueryKey&& key,
                 identity::source_query::CanonicalSourceSnapshot&& snapshot) noexcept
        : key(zc::mv(key)), snapshot(zc::mv(snapshot)) {}
    StagedSource(StagedSource&&) noexcept = default;
    StagedSource& operator=(StagedSource&&) noexcept = default;
    ZC_DISALLOW_COPY(StagedSource);

    identity::source_query::StableSourceQueryKey key;
    identity::source_query::CanonicalSourceSnapshot snapshot;
  };

  Impl(identity::CrateKey&& crate, source::core::AdmittedCoreSourceCatalog&& catalog,
       incremental_module_resolution_query::CanonicalModuleSearchRoots&& searchRoots,
       zc::Vector<StagedSource>&& sources) noexcept
      : crate(zc::mv(crate)),
        catalog(zc::mv(catalog)),
        searchRoots(zc::mv(searchRoots)),
        sources(zc::mv(sources)) {}

  identity::CrateKey crate;
  source::core::AdmittedCoreSourceCatalog catalog;
  incremental_module_resolution_query::CanonicalModuleSearchRoots searchRoots;
  zc::Vector<StagedSource> sources;
};

VerifiedCoreProjectionInput::VerifiedCoreProjectionInput(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreProjectionInput::~VerifiedCoreProjectionInput() noexcept(false) = default;
VerifiedCoreProjectionInput::VerifiedCoreProjectionInput(VerifiedCoreProjectionInput&&) noexcept =
    default;
VerifiedCoreProjectionInput& VerifiedCoreProjectionInput::operator=(
    VerifiedCoreProjectionInput&&) noexcept = default;
const identity::CrateKey& VerifiedCoreProjectionInput::crate() const noexcept {
  return impl->crate;
}
const source::core::AdmittedCoreSourceCatalog& VerifiedCoreProjectionInput::catalog()
    const noexcept {
  return impl->catalog;
}

const incremental_module_resolution_query::CanonicalModuleSearchRoots&
VerifiedCoreProjectionInput::searchRoots() const noexcept {
  return impl->searchRoots;
}

struct VerifiedCoreDistributionInputPayload::Impl final {
  Impl(incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
       source::core::CoreDistributionRecord&& distributionRecord,
       const identity::Sha256Digest& distributionDigest,
       source::core::CoreStandardMarkerPolicyTemplate&& policyTemplate,
       zc::Vector<ProjectedCoreSourceEntry>&& projectedCoreSources,
       zc::Vector<module_graph_query::CompilationOptionsEntry>&& compilationOptions,
       zc::Vector<module_graph_query::ModuleSearchRootsEntry>&& moduleSearchRoots,
       zc::Vector<identity::CrateKey>&& projectedCoreInventory,
       module_graph_query::CompleteCompilationContextAuthority&& contextAuthority) noexcept
      : contextRoots(zc::mv(contextRoots)),
        distributionRecord(zc::mv(distributionRecord)),
        distributionDigest(distributionDigest),
        policyTemplate(zc::mv(policyTemplate)),
        projectedCoreSources(zc::mv(projectedCoreSources)),
        compilationOptions(zc::mv(compilationOptions)),
        moduleSearchRoots(zc::mv(moduleSearchRoots)),
        projectedCoreInventory(zc::mv(projectedCoreInventory)),
        contextAuthority(zc::mv(contextAuthority)) {}

  incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  source::core::CoreDistributionRecord distributionRecord;
  identity::Sha256Digest distributionDigest;
  source::core::CoreStandardMarkerPolicyTemplate policyTemplate;
  zc::Vector<ProjectedCoreSourceEntry> projectedCoreSources;
  zc::Vector<module_graph_query::CompilationOptionsEntry> compilationOptions;
  zc::Vector<module_graph_query::ModuleSearchRootsEntry> moduleSearchRoots;
  zc::Vector<identity::CrateKey> projectedCoreInventory;
  module_graph_query::CompleteCompilationContextAuthority contextAuthority;
};

VerifiedCoreDistributionInputPayload::VerifiedCoreDistributionInputPayload(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedCoreDistributionInputPayload::~VerifiedCoreDistributionInputPayload() noexcept(false) =
    default;
VerifiedCoreDistributionInputPayload::VerifiedCoreDistributionInputPayload(
    VerifiedCoreDistributionInputPayload&&) noexcept = default;
VerifiedCoreDistributionInputPayload& VerifiedCoreDistributionInputPayload::operator=(
    VerifiedCoreDistributionInputPayload&&) noexcept = default;

zc::Maybe<VerifiedCoreDistributionInputPayload> VerifiedCoreDistributionInputPayload::from(
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    source::core::CoreDistributionRecord&& distributionRecord,
    const identity::Sha256Digest& distributionDigest,
    source::core::CoreStandardMarkerPolicyTemplate&& policyTemplate,
    zc::Vector<ProjectedCoreSourceEntry>&& projectedCoreSources,
    zc::Vector<module_graph_query::CompilationOptionsEntry>&& compilationOptions,
    zc::Vector<module_graph_query::ModuleSearchRootsEntry>&& moduleSearchRoots,
    zc::Vector<identity::CrateKey>&& projectedCoreInventory,
    module_graph_query::CompleteCompilationContextAuthority&& contextAuthority) {
  auto computedDigest = source::core::computeCoreDistributionDigest(distributionRecord);
  if (projectedCoreSources.empty() || compilationOptions.empty() || moduleSearchRoots.empty() ||
      projectedCoreInventory.empty() || computedDigest == zc::none ||
      ZC_ASSERT_NONNULL(computedDigest) != distributionDigest ||
      contextRoots != contextAuthority.contextRoots() ||
      distributionRecord.encode().asPtr() !=
          contextAuthority.coreDistributionRecord().encode().asPtr() ||
      distributionDigest != contextAuthority.coreDistributionDigest() ||
      !canonicalizePayloadValues(projectedCoreSources,
                                 [](const ProjectedCoreSourceEntry& value) {
                                   return zc::heapArray<uint8_t>(
                                       value.key().canonicalSourceBytes());
                                 }) ||
      !canonicalizePayloadValues(compilationOptions,
                                 [](const module_graph_query::CompilationOptionsEntry& value) {
                                   return value.key().encode();
                                 }) ||
      !canonicalizePayloadValues(moduleSearchRoots,
                                 [](const module_graph_query::ModuleSearchRootsEntry& value) {
                                   return value.key().encode();
                                 }) ||
      !canonicalizePayloadValues(projectedCoreInventory,
                                 [](const identity::CrateKey& value) { return value.encode(); })) {
    return zc::none;
  }
  if (compilationOptions.size() != contextAuthority.compilationOptions().size() ||
      moduleSearchRoots.size() != contextAuthority.moduleSearchRoots().size() ||
      projectedCoreInventory.size() != contextAuthority.projectedCoreCrates().size()) {
    return zc::none;
  }
  for (size_t index = 0; index < compilationOptions.size(); ++index) {
    if (compilationOptions[index].key().encode().asPtr() !=
            contextAuthority.compilationOptions()[index].key().encode().asPtr() ||
        compilationOptions[index].value().encodeCanonical().asPtr() !=
            contextAuthority.compilationOptions()[index].value().encodeCanonical().asPtr()) {
      return zc::none;
    }
  }
  for (size_t index = 0; index < moduleSearchRoots.size(); ++index) {
    if (moduleSearchRoots[index].key().encode().asPtr() !=
            contextAuthority.moduleSearchRoots()[index].key().encode().asPtr() ||
        moduleSearchRoots[index].value().encode().asPtr() !=
            contextAuthority.moduleSearchRoots()[index].value().encode().asPtr()) {
      return zc::none;
    }
  }
  for (size_t index = 0; index < projectedCoreInventory.size(); ++index) {
    if (projectedCoreInventory[index].encode().asPtr() !=
        contextAuthority.projectedCoreCrates()[index].encode().asPtr()) {
      return zc::none;
    }
  }
  return VerifiedCoreDistributionInputPayload(zc::heap<Impl>(
      zc::mv(contextRoots), zc::mv(distributionRecord), distributionDigest, zc::mv(policyTemplate),
      zc::mv(projectedCoreSources), zc::mv(compilationOptions), zc::mv(moduleSearchRoots),
      zc::mv(projectedCoreInventory), zc::mv(contextAuthority)));
}

VerifiedCoreDistributionInputPayload VerifiedCoreDistributionInputPayload::clone() const {
  zc::Vector<ProjectedCoreSourceEntry> sources(impl->projectedCoreSources.size());
  for (const auto& value : impl->projectedCoreSources) { sources.add(value.clone()); }
  zc::Vector<module_graph_query::CompilationOptionsEntry> options(impl->compilationOptions.size());
  for (const auto& value : impl->compilationOptions) { options.add(value.clone()); }
  zc::Vector<module_graph_query::ModuleSearchRootsEntry> roots(impl->moduleSearchRoots.size());
  for (const auto& value : impl->moduleSearchRoots) { roots.add(value.clone()); }
  zc::Vector<identity::CrateKey> inventory(impl->projectedCoreInventory.size());
  for (const auto& value : impl->projectedCoreInventory) { inventory.add(value.clone()); }
  return VerifiedCoreDistributionInputPayload(zc::heap<Impl>(
      impl->contextRoots.clone(), impl->distributionRecord.clone(), impl->distributionDigest,
      impl->policyTemplate.clone(), zc::mv(sources), zc::mv(options), zc::mv(roots),
      zc::mv(inventory), impl->contextAuthority.clone()));
}

const incremental_binding_query::CompilationRootSetQueryKey&
VerifiedCoreDistributionInputPayload::contextRoots() const noexcept {
  return impl->contextRoots;
}
const source::core::CoreDistributionRecord&
VerifiedCoreDistributionInputPayload::distributionRecord() const noexcept {
  return impl->distributionRecord;
}
const identity::Sha256Digest& VerifiedCoreDistributionInputPayload::distributionDigest()
    const noexcept {
  return impl->distributionDigest;
}
const source::core::CoreStandardMarkerPolicyTemplate&
VerifiedCoreDistributionInputPayload::policyTemplate() const noexcept {
  return impl->policyTemplate;
}
zc::ArrayPtr<const ProjectedCoreSourceEntry>
VerifiedCoreDistributionInputPayload::projectedCoreSources() const noexcept {
  return impl->projectedCoreSources.asPtr();
}
zc::ArrayPtr<const module_graph_query::CompilationOptionsEntry>
VerifiedCoreDistributionInputPayload::compilationOptions() const noexcept {
  return impl->compilationOptions.asPtr();
}
zc::ArrayPtr<const module_graph_query::ModuleSearchRootsEntry>
VerifiedCoreDistributionInputPayload::moduleSearchRoots() const noexcept {
  return impl->moduleSearchRoots.asPtr();
}
zc::ArrayPtr<const identity::CrateKey>
VerifiedCoreDistributionInputPayload::projectedCoreInventory() const noexcept {
  return impl->projectedCoreInventory.asPtr();
}
const module_graph_query::CompleteCompilationContextAuthority&
VerifiedCoreDistributionInputPayload::contextAuthority() const noexcept {
  return impl->contextAuthority;
}

zc::Array<uint8_t> VerifiedCoreDistributionInputPayload::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(impl->contextRoots.encodeCanonical().asPtr());
  encoder.encodeByteString(impl->distributionRecord.encode().asPtr());
  encoder.encodeDigest(impl->distributionDigest);
  encoder.encodeByteString(impl->policyTemplate.encode().asPtr());
  encoder.encodeSequenceSize(impl->projectedCoreSources.size());
  for (const auto& value : impl->projectedCoreSources) {
    encoder.encodeByteString(value.key().canonicalSourceBytes());
    encoder.encodeByteString(value.value().encodeCanonical().asPtr());
  }
  encoder.encodeSequenceSize(impl->compilationOptions.size());
  for (const auto& value : impl->compilationOptions) {
    encoder.encodeByteString(value.key().encode().asPtr());
    encoder.encodeByteString(value.value().encodeCanonical().asPtr());
  }
  encoder.encodeSequenceSize(impl->moduleSearchRoots.size());
  for (const auto& value : impl->moduleSearchRoots) {
    encoder.encodeByteString(value.key().encode().asPtr());
    encoder.encodeByteString(value.value().encode().asPtr());
  }
  encoder.encodeSequenceSize(impl->projectedCoreInventory.size());
  for (const auto& value : impl->projectedCoreInventory) {
    encoder.encodeByteString(value.encode().asPtr());
  }
  encoder.encodeByteString(impl->contextAuthority.encodeCanonical().asPtr());
  return frame(kCoreDistributionTransactionDomain, encoder.finish().asPtr());
}

bool VerifiedCoreDistributionInputPayload::operator==(
    const VerifiedCoreDistributionInputPayload& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

zc::Maybe<VerifiedCoreDistributionInputPayload>
VerifiedCoreDistributionInputPayload::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kCoreDistributionTransactionDomain, bytes, kMaximumCoreGraphBytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto contextBytes = decoder.decodeByteString(kMaximumContextRootBytes);
  auto distributionBytes = decoder.decodeByteString(kMaximumCoreDistributionValueBytes);
  auto digest = decoder.decodeDigest();
  auto policyBytes = decoder.decodeByteString(kMaximumCoreDistributionValueBytes);
  if (contextBytes == zc::none || distributionBytes == zc::none || digest == zc::none ||
      policyBytes == zc::none) {
    return zc::none;
  }
  auto context = incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(contextBytes).asPtr());
  auto distribution = source::core::CoreDistributionRecord::decodeCanonical(
      ZC_ASSERT_NONNULL(distributionBytes).asPtr());
  auto policy = source::core::CoreStandardMarkerPolicyTemplate::decodeCanonical(
      ZC_ASSERT_NONNULL(policyBytes).asPtr());
  if (context == zc::none || distribution == zc::none || policy == zc::none) { return zc::none; }
  auto sourceCount = decoder.decodeSequenceSize(kMaximumCoreEdges);
  if (sourceCount == zc::none) { return zc::none; }
  zc::Vector<ProjectedCoreSourceEntry> sources;
  sources.reserve(ZC_ASSERT_NONNULL(sourceCount));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(sourceCount); ++index) {
    auto keyBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
    auto valueBytes = decoder.decodeByteString(kMaximumCoreDistributionValueBytes);
    if (keyBytes == zc::none || valueBytes == zc::none) { return zc::none; }
    auto key = identity::source_query::StableSourceQueryKey::decodeBounded(
        ZC_ASSERT_NONNULL(keyBytes).asPtr());
    auto value = identity::source_query::CanonicalSourceSnapshot::decodeCanonical(
        ZC_ASSERT_NONNULL(valueBytes).asPtr());
    if (key == zc::none || value == zc::none) { return zc::none; }
    sources.add(ProjectedCoreSourceEntry::from(zc::mv(ZC_ASSERT_NONNULL(key)),
                                               zc::mv(ZC_ASSERT_NONNULL(value))));
  }
  auto optionsCount = decoder.decodeSequenceSize(kMaximumCoreEdges);
  if (optionsCount == zc::none) { return zc::none; }
  zc::Vector<module_graph_query::CompilationOptionsEntry> options;
  options.reserve(ZC_ASSERT_NONNULL(optionsCount));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(optionsCount); ++index) {
    auto keyBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
    auto valueBytes = decoder.decodeByteString(kMaximumCoreDistributionValueBytes);
    if (keyBytes == zc::none || valueBytes == zc::none) { return zc::none; }
    auto key = decodePayloadCrate(ZC_ASSERT_NONNULL(keyBytes).asPtr());
    auto value = identity::source_query::CompilationOptionsInput::decodeValue(
        ZC_ASSERT_NONNULL(valueBytes).asPtr());
    if (key == zc::none || value == zc::none) { return zc::none; }
    options.add(module_graph_query::CompilationOptionsEntry::from(
        zc::mv(ZC_ASSERT_NONNULL(key)), zc::mv(ZC_ASSERT_NONNULL(value))));
  }
  auto rootsCount = decoder.decodeSequenceSize(kMaximumCoreEdges);
  if (rootsCount == zc::none) { return zc::none; }
  zc::Vector<module_graph_query::ModuleSearchRootsEntry> roots;
  roots.reserve(ZC_ASSERT_NONNULL(rootsCount));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(rootsCount); ++index) {
    auto keyBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
    auto valueBytes = decoder.decodeByteString(kMaximumCoreDistributionValueBytes);
    if (keyBytes == zc::none || valueBytes == zc::none) { return zc::none; }
    auto key = decodePayloadCrate(ZC_ASSERT_NONNULL(keyBytes).asPtr());
    auto value = incremental_module_resolution_query::ModuleSearchRootsInput::decodeValue(
        ZC_ASSERT_NONNULL(valueBytes).asPtr());
    if (key == zc::none || value == zc::none) { return zc::none; }
    roots.add(module_graph_query::ModuleSearchRootsEntry::from(zc::mv(ZC_ASSERT_NONNULL(key)),
                                                               zc::mv(ZC_ASSERT_NONNULL(value))));
  }
  auto inventoryCount = decoder.decodeSequenceSize(kMaximumCoreEdges);
  if (inventoryCount == zc::none) { return zc::none; }
  zc::Vector<identity::CrateKey> inventory;
  inventory.reserve(ZC_ASSERT_NONNULL(inventoryCount));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(inventoryCount); ++index) {
    auto crateBytes = decoder.decodeByteString(kMaximumCrateOrModuleKeyBytes);
    if (crateBytes == zc::none) { return zc::none; }
    auto crate = decodePayloadCrate(ZC_ASSERT_NONNULL(crateBytes).asPtr());
    if (crate == zc::none) { return zc::none; }
    inventory.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  auto authorityBytes = decoder.decodeByteString(kMaximumCoreGraphBytes);
  if (authorityBytes == zc::none || !decoder.finished()) { return zc::none; }
  auto authority = module_graph_query::CompleteCompilationContextAuthority::decodeCanonical(
      ZC_ASSERT_NONNULL(authorityBytes).asPtr());
  if (authority == zc::none) { return zc::none; }
  auto result =
      from(zc::mv(ZC_ASSERT_NONNULL(context)), zc::mv(ZC_ASSERT_NONNULL(distribution)),
           ZC_ASSERT_NONNULL(digest), zc::mv(ZC_ASSERT_NONNULL(policy)), zc::mv(sources),
           zc::mv(options), zc::mv(roots), zc::mv(inventory), zc::mv(ZC_ASSERT_NONNULL(authority)));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

bool VerifiedCoreDistributionInputVerifier::verify(
    const VerifiedCoreDistributionInputPayload& candidate,
    const source::core::VerifiedCoreDistribution& distribution,
    const package::VerifiedPackageCompilationRequest& packageRequest,
    const identity::source_query::CanonicalCompilationOptions& compilationOptions,
    zc::ArrayPtr<const identity::CrateKey> completeConsumerInventory) {
  auto encoded = candidate.encodeCanonical();
  auto decoded = VerifiedCoreDistributionInputPayload::decodeCanonical(encoded.asPtr());
  if (decoded == zc::none || ZC_ASSERT_NONNULL(decoded) != candidate ||
      candidate.distributionRecord().encode().asPtr() != distribution.record().encode().asPtr() ||
      candidate.distributionDigest() != distribution.distributionDigest() ||
      candidate.policyTemplate().encode().asPtr() !=
          distribution.policyTemplate().encode().asPtr() ||
      !package::CanonicalPackageCompilationRequestProjectionVerifier::verify(
          candidate.contextAuthority().packageRequest(), packageRequest)) {
    return false;
  }
  zc::Vector<identity::CrateKey> projectedCoreCrates;
  for (const auto& consumer : completeConsumerInventory) {
    if (!compilationOptions.matchesCrate(consumer)) { return false; }
    auto projected = identity::projectToolchainCoreCrate(consumer);
    if (projected == zc::none) { return false; }
    bool present = false;
    for (const auto& candidateCore : projectedCoreCrates) {
      if (candidateCore.encode().asPtr() == ZC_ASSERT_NONNULL(projected).encode().asPtr()) {
        present = true;
        break;
      }
    }
    if (!present) { projectedCoreCrates.add(zc::mv(ZC_ASSERT_NONNULL(projected))); }
  }
  if (!canonicalizePayloadValues(projectedCoreCrates,
                                 [](const identity::CrateKey& value) { return value.encode(); }) ||
      projectedCoreCrates.size() != candidate.projectedCoreInventory().size()) {
    return false;
  }
  for (size_t index = 0; index < projectedCoreCrates.size(); ++index) {
    if (projectedCoreCrates[index].encode().asPtr() !=
        candidate.projectedCoreInventory()[index].encode().asPtr()) {
      return false;
    }
  }
  if (candidate.projectedCoreSources().size() !=
      projectedCoreCrates.size() * distribution.snapshots().size()) {
    return false;
  }
  size_t matchedSources = 0;
  for (const auto& core : projectedCoreCrates) {
    for (const auto& snapshot : distribution.snapshots()) {
      auto sourceKey = identity::SourceFileKey::from(
          core.clone(), identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                                            snapshot.path().clone()));
      auto immutable = identity::ImmutableSourceSnapshot::from(
          sourceKey.clone(), zc::heapArray<uint8_t>(snapshot.bytes()));
      if (immutable == zc::none ||
          ZC_ASSERT_NONNULL(immutable).contentDigest() != snapshot.contentDigest()) {
        return false;
      }
      auto stable = identity::source_query::StableSourceQueryKey::fromVerified(sourceKey);
      auto canonical = identity::source_query::CanonicalSourceSnapshot::fromVerified(
          ZC_ASSERT_NONNULL(immutable));
      if (stable == zc::none || canonical == zc::none) { return false; }
      size_t occurrences = 0;
      for (const auto& entry : candidate.projectedCoreSources()) {
        if (entry.key() != ZC_ASSERT_NONNULL(stable)) { continue; }
        if (entry.value() != ZC_ASSERT_NONNULL(canonical)) { return false; }
        ++occurrences;
      }
      if (occurrences != 1) { return false; }
      ++matchedSources;
    }
  }
  if (matchedSources != candidate.projectedCoreSources().size() ||
      candidate.compilationOptions().size() !=
          candidate.contextAuthority().compilationOptions().size() ||
      candidate.moduleSearchRoots().size() !=
          candidate.contextAuthority().moduleSearchRoots().size()) {
    return false;
  }
  for (const auto& entry : candidate.compilationOptions()) {
    if (!entry.value().matchesCrate(entry.key()) ||
        entry.value().encodeCanonical().asPtr() != compilationOptions.encodeCanonical().asPtr()) {
      return false;
    }
  }
  return true;
}

struct VerifiedCoreDistributionInputTransaction::Impl final {
  Impl(query::DatabaseRevision expectedPreviousRevision,
       source::core::CoreDistributionInputRecord&& distribution,
       VerifiedCoreDistributionInputPayload&& payload,
       binder::CanonicalInputPayloadDigest&& payloadDigest,
       zc::Vector<VerifiedCoreProjectionInput>&& projections) noexcept
      : expectedPreviousRevision(expectedPreviousRevision),
        distribution(zc::mv(distribution)),
        payload(zc::mv(payload)),
        payloadDigest(zc::mv(payloadDigest)),
        projections(zc::mv(projections)) {}

  query::DatabaseRevision expectedPreviousRevision;
  source::core::CoreDistributionInputRecord distribution;
  VerifiedCoreDistributionInputPayload payload;
  binder::CanonicalInputPayloadDigest payloadDigest;
  zc::Vector<VerifiedCoreProjectionInput> projections;
  bool committed = false;
};

VerifiedCoreDistributionInputTransaction::VerifiedCoreDistributionInputTransaction(
    zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreDistributionInputTransaction::~VerifiedCoreDistributionInputTransaction() noexcept(
    false) = default;
VerifiedCoreDistributionInputTransaction::VerifiedCoreDistributionInputTransaction(
    VerifiedCoreDistributionInputTransaction&&) noexcept = default;
VerifiedCoreDistributionInputTransaction& VerifiedCoreDistributionInputTransaction::operator=(
    VerifiedCoreDistributionInputTransaction&&) noexcept = default;

zc::Maybe<VerifiedCoreDistributionInputTransaction>
VerifiedCoreDistributionInputTransaction::prepare(
    query::DatabaseRevision expectedPreviousRevision,
    const source::core::VerifiedCoreDistribution& distribution,
    const package::VerifiedPackageCompilationRequest& packageRequest,
    module_graph_query::CompleteCompilationContextAuthority&& contextAuthority,
    const identity::source_query::CanonicalCompilationOptions& compilationOptions,
    zc::ArrayPtr<const identity::CrateKey> completeConsumerInventory) {
  auto accepted = source::core::initialCoreDistributionInput();
  if (accepted == zc::none || completeConsumerInventory.size() == 0 ||
      !package::CanonicalPackageCompilationRequestProjectionVerifier::verify(
          contextAuthority.packageRequest(), packageRequest) ||
      !verifiedDistributionMatchesAccepted(distribution, ZC_ASSERT_NONNULL(accepted)) ||
      contextAuthority.coreDistributionRecord().encode().asPtr() !=
          distribution.record().encode().asPtr() ||
      contextAuthority.coreDistributionDigest() != distribution.distributionDigest() ||
      distribution.record().editionYear() != 2026 ||
      distribution.snapshots().size() != distribution.record().files().size()) {
    return zc::none;
  }

  zc::TreeMap<zc::String, identity::CrateKey> uniqueProjections;
  for (const auto& consumer : completeConsumerInventory) {
    if (!compilationOptions.matchesCrate(consumer)) { return zc::none; }
    auto projected = identity::projectToolchainCoreCrate(consumer);
    if (projected == zc::none) { return zc::none; }
    auto sortKey = zc::encodeHex(ZC_ASSERT_NONNULL(projected).encode().asPtr());
    if (uniqueProjections.find(sortKey) == zc::none) {
      uniqueProjections.insert(zc::mv(sortKey), zc::mv(ZC_ASSERT_NONNULL(projected)));
    }
  }

  zc::Vector<VerifiedCoreProjectionInput> projections(uniqueProjections.size());
  for (const auto& projection : uniqueProjections) {
    auto catalogResult =
        source::core::CoreSourceCatalogAdmission::admit(distribution, projection.value);
    if (!catalogResult.is<source::core::AdmittedCoreSourceCatalog>()) { return zc::none; }
    auto catalog = zc::mv(catalogResult.get<source::core::AdmittedCoreSourceCatalog>());

    auto root = binder::ModuleSearchRoot::toolchainCore(projection.value.clone(),
                                                        distribution.distributionDigest());
    if (root == zc::none) { return zc::none; }
    zc::Vector<binder::ModuleSearchRoot> environment;
    environment.add(zc::mv(ZC_ASSERT_NONNULL(root)));
    auto searchRoots =
        incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
            projection.value, environment.asPtr());
    if (searchRoots == zc::none) { return zc::none; }

    zc::Vector<VerifiedCoreProjectionInput::Impl::StagedSource> sources(
        distribution.snapshots().size());
    for (size_t index = 0; index < distribution.snapshots().size(); ++index) {
      const auto& admitted = distribution.snapshots()[index];
      const auto& declared = distribution.record().files()[index];
      if (!samePath(admitted.path(), declared.path()) ||
          admitted.contentDigest() != declared.digest()) {
        return zc::none;
      }
      auto sourceKey = identity::SourceFileKey::from(
          projection.value.clone(),
          identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                              admitted.path().clone()));
      auto immutable = identity::ImmutableSourceSnapshot::from(
          sourceKey.clone(), zc::heapArray<uint8_t>(admitted.bytes()));
      if (immutable == zc::none ||
          ZC_ASSERT_NONNULL(immutable).contentDigest() != admitted.contentDigest()) {
        return zc::none;
      }
      auto stable = identity::source_query::StableSourceQueryKey::fromVerified(sourceKey);
      auto snapshot = identity::source_query::CanonicalSourceSnapshot::fromVerified(
          ZC_ASSERT_NONNULL(immutable));
      if (stable == zc::none || snapshot == zc::none) { return zc::none; }
      sources.add(VerifiedCoreProjectionInput::Impl::StagedSource(
          zc::mv(ZC_ASSERT_NONNULL(stable)), zc::mv(ZC_ASSERT_NONNULL(snapshot))));
    }
    projections.add(VerifiedCoreProjectionInput(zc::heap<VerifiedCoreProjectionInput::Impl>(
        projection.value.clone(), zc::mv(catalog), zc::mv(ZC_ASSERT_NONNULL(searchRoots)),
        zc::mv(sources))));
  }
  if (projections.empty()) { return zc::none; }

  zc::Vector<ProjectedCoreSourceEntry> projectedSources(projections.size() *
                                                        distribution.snapshots().size());
  for (const auto& projection : projections) {
    for (const auto& source : projection.impl->sources) {
      projectedSources.add(
          ProjectedCoreSourceEntry::from(source.key.clone(), source.snapshot.clone()));
    }
  }
  zc::Vector<module_graph_query::CompilationOptionsEntry> options(
      contextAuthority.compilationOptions().size());
  for (const auto& entry : contextAuthority.compilationOptions()) { options.add(entry.clone()); }
  zc::Vector<module_graph_query::ModuleSearchRootsEntry> roots(
      contextAuthority.moduleSearchRoots().size());
  for (const auto& entry : contextAuthority.moduleSearchRoots()) { roots.add(entry.clone()); }
  zc::Vector<identity::CrateKey> projectedInventory(projections.size());
  for (const auto& projection : projections) {
    projectedInventory.add(projection.impl->crate.clone());
  }
  auto payload = VerifiedCoreDistributionInputPayload::from(
      contextAuthority.contextRoots().clone(), distribution.record().clone(),
      distribution.distributionDigest(), distribution.policyTemplate().clone(),
      zc::mv(projectedSources), zc::mv(options), zc::mv(roots), zc::mv(projectedInventory),
      zc::mv(contextAuthority));
  if (payload == zc::none || !VerifiedCoreDistributionInputVerifier::verify(
                                 ZC_ASSERT_NONNULL(payload), distribution, packageRequest,
                                 compilationOptions, completeConsumerInventory)) {
    return zc::none;
  }
  auto payloadBytes = ZC_ASSERT_NONNULL(payload).encodeCanonical();
  auto payloadDigest = module_graph_query::computeCanonicalInputPayloadDigest(
      kCoreDistributionTransactionDomain, payloadBytes.asPtr());
  if (payloadDigest == zc::none) { return zc::none; }
  return VerifiedCoreDistributionInputTransaction(
      zc::heap<Impl>(expectedPreviousRevision, zc::mv(ZC_ASSERT_NONNULL(accepted)),
                     zc::mv(ZC_ASSERT_NONNULL(payload)), zc::mv(ZC_ASSERT_NONNULL(payloadDigest)),
                     zc::mv(projections)));
}

zc::ArrayPtr<const VerifiedCoreProjectionInput>
VerifiedCoreDistributionInputTransaction::projections() const noexcept {
  if (impl.get() == nullptr) { return {}; }
  return impl->projections.asPtr();
}

const source::core::CoreDistributionInputRecord&
VerifiedCoreDistributionInputTransaction::distribution() const noexcept {
  return impl->distribution;
}

const VerifiedCoreDistributionInputPayload& VerifiedCoreDistributionInputTransaction::payload()
    const noexcept {
  return impl->payload;
}

query::InputCommitResult VerifiedCoreDistributionInputTransaction::commit(
    query::QueryDatabase& database) {
  if (impl.get() == nullptr || impl->committed) {
    return query::InputCommitResult::rejected(query::InputTransactionFailure::TransactionClosed);
  }
  const auto coreUnit = identity::ToolchainUnitKey::core();
  auto pending = database.beginInputTransaction(impl->expectedPreviousRevision);
  if (!pending.isOpened()) { return query::InputCommitResult::rejected(pending.failure()); }
  auto transaction = zc::mv(pending).takeTransaction();
  auto distributionMutation = transaction.set<CoreDistributionInput>(coreUnit, impl->distribution);
  if (!distributionMutation.isApplied()) {
    transaction.abandon();
    return query::InputCommitResult::rejected(distributionMutation.failure());
  }
  for (const auto& entry : impl->payload.compilationOptions()) {
    auto optionsMutation = transaction.set<identity::source_query::CompilationOptionsInput>(
        entry.key(), entry.value());
    if (!optionsMutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(optionsMutation.failure());
    }
  }
  for (const auto& entry : impl->payload.moduleSearchRoots()) {
    auto rootsMutation =
        transaction.set<incremental_module_resolution_query::ModuleSearchRootsInput>(entry.key(),
                                                                                     entry.value());
    if (!rootsMutation.isApplied()) {
      transaction.abandon();
      return query::InputCommitResult::rejected(rootsMutation.failure());
    }
  }
  for (const auto& projection : impl->projections) {
    for (const auto& source : projection.impl->sources) {
      auto sourceMutation =
          transaction.set<identity::source_query::SourceSnapshotInput>(source.key, source.snapshot);
      if (!sourceMutation.isApplied()) {
        transaction.abandon();
        return query::InputCommitResult::rejected(sourceMutation.failure());
      }
    }
  }
  auto authorityMutation =
      transaction.set<module_graph_query::CompleteCompilationContextAuthorityInput>(
          impl->payload.contextRoots(), impl->payload.contextAuthority());
  if (!authorityMutation.isApplied()) {
    transaction.abandon();
    return query::InputCommitResult::rejected(authorityMutation.failure());
  }
  auto witnessMutation =
      transaction.set<module_graph_query::CoreDistributionTransactionWitnessInput>(
          impl->payload.contextRoots(), impl->payloadDigest);
  if (!witnessMutation.isApplied()) {
    transaction.abandon();
    return query::InputCommitResult::rejected(witnessMutation.failure());
  }
  auto result = transaction.commit();
  if (!result.isCommitted()) { return result; }
  impl->committed = true;
  return result;
}

bool registerCoreLibraryQueryProvider(query::QueryDatabase& database) {
  auto distribution = database.registerDescriptor<CoreDistributionInput>();
  if (!distribution.isRegistered()) { return false; }
  if (!database.registerDescriptor<CoreModuleGraphQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<CoreRoleSeedQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<CoreBootstrapModuleInterfaceQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<CoreExportSurfaceQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<CorePreludeSurfaceQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<CoreRoleAuthorityQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<MaterializeCoreRoleSeedQuery>().isRegistered()) { return false; }
  if (!database.registerDescriptor<MaterializeCoreBootstrapModuleInterfaceQuery>().isRegistered()) {
    return false;
  }
  if (!database.registerDescriptor<MaterializeCoreAuthorityQuery>().isRegistered()) {
    return false;
  }
  return database.registerDescriptor<FinalizeCoreModuleInterfaceQuery>().isRegistered();
}

}  // namespace zomlang::compiler::driver::core_library_query

namespace zomlang::compiler::query {

StableWitnessBytes
CapabilityCandidateContract<driver::core_library_query::MaterializeCoreRoleSeedQuery>::encode(
    const Descriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<CapabilityCandidateContract<
    driver::core_library_query::MaterializeCoreRoleSeedQuery>::Descriptor::Capability>>
CapabilityCandidateContract<driver::core_library_query::MaterializeCoreRoleSeedQuery>::decode(
    zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

StableWitnessBytes
CapabilityCandidateContract<driver::core_library_query::MaterializeCoreAuthorityQuery>::encode(
    const Descriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<CapabilityCandidateContract<
    driver::core_library_query::MaterializeCoreAuthorityQuery>::Descriptor::Capability>>
CapabilityCandidateContract<driver::core_library_query::MaterializeCoreAuthorityQuery>::decode(
    zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

StableWitnessBytes
CapabilityCandidateContract<driver::core_library_query::FinalizeCoreModuleInterfaceQuery>::encode(
    const Descriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<CapabilityCandidateContract<
    driver::core_library_query::FinalizeCoreModuleInterfaceQuery>::Descriptor::Capability>>
CapabilityCandidateContract<driver::core_library_query::FinalizeCoreModuleInterfaceQuery>::decode(
    zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

StableWitnessBytes CapabilityCandidateContract<
    driver::core_library_query::MaterializeCoreBootstrapModuleInterfaceQuery>::
    encode(const Descriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<CapabilityCandidateContract<
    driver::core_library_query::MaterializeCoreBootstrapModuleInterfaceQuery>::Descriptor::
                      Capability>>
CapabilityCandidateContract<
    driver::core_library_query::MaterializeCoreBootstrapModuleInterfaceQuery>::
    decode(zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

}  // namespace zomlang::compiler::query
