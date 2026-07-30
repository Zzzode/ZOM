// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core-library-query-provider.h"

#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zomlang/compiler/driver/core-library-query-verifier.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/package/canonical-package-compilation-request.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-snapshot.h"

namespace zomlang::compiler::driver::core_library_query {
namespace {

constexpr zc::StringPtr kCoreDistributionDomain = "zom.query.core-distribution"_zc;
constexpr zc::StringPtr kCoreDistributionValueDomain = "zom.query.core-distribution-value"_zc;
constexpr zc::StringPtr kCoreModuleGraphValueDomain = "zom.query.core-module-graph-value"_zc;
constexpr zc::StringPtr kCoreModuleGraphRevisionDomain = "zom.core-module-graph"_zc;
constexpr zc::StringPtr kCoreDistributionTransactionDomain =
    "zom.query.input-transaction.core-distribution"_zc;
constexpr size_t kMaximumCoreDistributionKeyBytes = 256;
constexpr size_t kMaximumCoreDistributionValueBytes = 512 * 1024;
constexpr size_t kMaximumContextRootBytes = 64 * 1024 * 1024;
constexpr size_t kMaximumCrateOrModuleKeyBytes = 64 * 1024;
constexpr size_t kMaximumCoreModules = 4096;
constexpr size_t kMaximumCoreEdges = 1024 * 1024;
constexpr size_t kMaximumCoreGraphBytes = 128 * 1024 * 1024;

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
  return database.registerDescriptor<CoreModuleGraphQuery>().isRegistered();
}

}  // namespace zomlang::compiler::driver::core_library_query
