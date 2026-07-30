// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/materialized-module-graph-query.h"

#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable-binding-diagnostic-fact.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/module-dependency-provenance-query.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/parser/parse-source-query.h"

namespace zomlang::compiler {
namespace {

constexpr zc::StringPtr kDependencyWitnessDomain = "zom.materialized-module-dependency-witness"_zc;
constexpr zc::StringPtr kGraphWitnessDomain = "zom.materialized-module-graph-witness"_zc;
constexpr zc::StringPtr kGraphRevisionDomain = "zom.module-dependency-graph"_zc;
constexpr zc::StringPtr kSemanticContextDomain = "zom.semantic-context"_zc;
constexpr uint64_t kMaximumModuleKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumRequestBytes = 1024 * 1024;
constexpr uint64_t kMaximumRequestEdges = 1024 * 1024;
constexpr uint64_t kMaximumWitnessBytes = 128 * 1024 * 1024;

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  if (left.size() > right.size()) { return 1; }
  return 0;
}

zc::Array<uint8_t> frame(zc::StringPtr domain, zc::ArrayPtr<const uint8_t> payload) {
  auto result = zc::heapArray<uint8_t>(domain.size() + 1 + payload.size());
  size_t cursor = 0;
  for (const auto byte : domain.asBytes()) { result[cursor++] = byte; }
  result[cursor++] = 0;
  for (const auto byte : payload) { result[cursor++] = byte; }
  return result;
}

zc::Maybe<zc::ArrayPtr<const uint8_t>> unframe(zc::StringPtr domain,
                                               zc::ArrayPtr<const uint8_t> bytes) {
  const size_t prefixSize = domain.size() + 1;
  if (bytes.size() <= prefixSize || bytes.size() > kMaximumWitnessBytes ||
      bytes.slice(0, domain.size()) != domain.asBytes() || bytes[domain.size()] != 0) {
    return zc::none;
  }
  return bytes.slice(prefixSize, bytes.size());
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool containsModule(zc::ArrayPtr<const identity::ModuleKey> modules,
                    const identity::ModuleKey& target) {
  for (const auto& module : modules) {
    if (sameModule(module, target)) { return true; }
  }
  return false;
}

zc::Maybe<identity::ModuleKey> decodeModule(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::ModuleKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(value).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(value));
}

zc::Maybe<identity::SourceFileKey> decodeSource(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::SourceFileKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(value).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(value));
}

zc::Maybe<identity::PackageDependencyEdgeKey> decodePackageEdge(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::PackageDependencyEdgeKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(value).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(value));
}

zc::Maybe<identity::CrateDependencyEdgeKey> decodeCrateEdge(zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::CrateDependencyEdgeKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished() ||
      ZC_ASSERT_NONNULL(value).encode().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(value));
}

zc::Maybe<identity::ModuleResolutionKey> decodeRequest(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumRequestBytes) { return zc::none; }
  auto value = identity::ModuleResolutionKey::decodeCanonical(bytes);
  if (value == zc::none || ZC_ASSERT_NONNULL(value).encode().asPtr() != bytes) { return zc::none; }
  return zc::mv(ZC_ASSERT_NONNULL(value));
}

template <typename Value, typename Bytes>
void canonicalSort(zc::Vector<Value>& values, Bytes bytes) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && compareBytes(bytes(current), bytes(values[insertion - 1])) < 0) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

void appendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    const uint32_t shift = 56 - index * 8;
    bytes.add(static_cast<uint8_t>((value >> shift) & 0xffu));
  }
}

bool stableEdgesMatchGraph(
    const driver::module_graph_query::ModuleGraphRecord& graph,
    zc::ArrayPtr<const driver::module_graph_query::StableMaterializedDependencyWitness> edges) {
  for (const auto& edge : edges) {
    if (!sameModule(edge.requester(), edge.request().requester()) ||
        !containsModule(graph.modules(), edge.requester()) ||
        !containsModule(graph.modules(), edge.dependency())) {
      return false;
    }
    bool projected = false;
    for (const auto& graphEdge : graph.edges()) {
      if (sameModule(graphEdge.requester(), edge.requester()) &&
          sameModule(graphEdge.dependency(), edge.dependency())) {
        projected = true;
        break;
      }
    }
    if (!projected) { return false; }
  }
  for (const auto& graphEdge : graph.edges()) {
    bool represented = false;
    for (const auto& edge : edges) {
      if (sameModule(graphEdge.requester(), edge.requester()) &&
          sameModule(graphEdge.dependency(), edge.dependency())) {
        represented = true;
        break;
      }
    }
    if (!represented) { return false; }
  }
  return true;
}

template <typename Entry>
bool entriesAreCanonical(zc::ArrayPtr<const Entry> entries,
                         identity::SemanticContextBrand context) {
  for (size_t index = 0; index < entries.size(); ++index) {
    const auto keyBytes = entries[index].key().encode();
    const auto recordBytes = entries[index].record().encode();
    if (!entries[index].handle().belongsTo(context) || keyBytes.asPtr() != recordBytes.asPtr()) {
      return false;
    }
    if (index != 0) {
      const auto priorBytes = entries[index - 1].key().encode();
      if (compareBytes(priorBytes.asPtr(), keyBytes.asPtr()) >= 0) { return false; }
    }
    for (size_t priorIndex = 0; priorIndex < index; ++priorIndex) {
      if (entries[priorIndex].handle() == entries[index].handle()) { return false; }
    }
  }
  return true;
}

template <typename Entry, typename Key>
bool containsEntryKey(zc::ArrayPtr<const Entry> entries, const Key& key) {
  const auto expected = key.encode();
  for (const auto& entry : entries) {
    if (entry.key().encode().asPtr() == expected.asPtr()) { return true; }
  }
  return false;
}

bool rootsMatchMaterializedEntries(
    const driver::incremental_binding_query::CompilationRootSetQueryKey& contextRoots,
    zc::ArrayPtr<const driver::module_graph_query::MaterializedCompilationUnitEntry> units,
    zc::ArrayPtr<const driver::module_graph_query::MaterializedCrateEntry> crates) {
  using driver::incremental_binding_query::CompilationRootKind;
  for (const auto& root : contextRoots.roots()) {
    size_t matches = 0;
    if (root.kind() == CompilationRootKind::UserPackage) {
      for (const auto& unit : units) {
        if (unit.key().kind() == identity::CompilationUnitKind::UserPackage &&
            unit.key().userPackage().encode().asPtr() ==
                root.userPackage().canonicalPackageBytes()) {
          ++matches;
        }
      }
    } else {
      for (const auto& crate : crates) {
        if (crate.key().encode().asPtr() == root.toolchainCore().canonicalCrateBytes()) {
          ++matches;
        }
      }
    }
    if (matches != 1) { return false; }
  }
  for (const auto& crate : crates) {
    if (crate.key().unit().kind() != identity::CompilationUnitKind::Toolchain) { continue; }
    size_t matches = 0;
    for (const auto& root : contextRoots.roots()) {
      if (root.kind() == CompilationRootKind::ToolchainCore &&
          root.toolchainCore().canonicalCrateBytes() == crate.key().encode().asPtr()) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
  }
  return true;
}

zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&> moduleEntryFor(
    zc::ArrayPtr<const driver::module_graph_query::MaterializedModuleEntry> modules,
    identity::ModuleId handle) {
  zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&> result;
  for (const auto& module : modules) {
    if (module.handle() != handle) { continue; }
    if (result != zc::none) { return zc::none; }
    result = module;
  }
  return result;
}

zc::Maybe<identity::Sha256Digest> computeGraphRevision(
    const identity::SemanticContextFingerprint& fingerprint,
    const driver::module_graph_query::ModuleGraphRecord& graph,
    zc::ArrayPtr<const driver::module_graph_query::StableMaterializedDependencyWitness> edges) {
  zc::Vector<uint8_t> preimage;
  for (const auto byte : kGraphRevisionDomain.asBytes()) { preimage.add(byte); }
  preimage.add(0);
  preimage.addAll(fingerprint.digest().bytes());
  const auto graphBytes = graph.encodeCanonical();
  appendUint64(preimage, graphBytes.size());
  preimage.addAll(graphBytes.asPtr());
  appendUint64(preimage, edges.size());
  for (size_t index = 0; index < edges.size(); ++index) {
    const auto bytes = edges[index].encodeCanonical();
    if (index != 0) {
      const auto priorBytes = edges[index - 1].encodeCanonical();
      if (compareBytes(priorBytes.asPtr(), bytes.asPtr()) >= 0) { return zc::none; }
    }
    appendUint64(preimage, bytes.size());
    preimage.addAll(bytes.asPtr());
  }
  return identity::sha256(preimage.asPtr());
}

struct ProviderAcquisition final {
  ProviderAcquisition(driver::incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
                      zc::Vector<identity::CompilationUnitIdentity>&& units,
                      zc::Vector<identity::ToolchainSemanticContextInput>&& toolchainInputs,
                      zc::Vector<identity::PackageDependencyEdgeKey>&& packageEdges,
                      zc::Vector<identity::CrateKey>&& crates,
                      zc::Vector<identity::CrateDependencyEdgeKey>&& crateEdges,
                      zc::Vector<identity::SourceFileKey>&& sources,
                      zc::Vector<identity::ModuleKey>&& modules,
                      driver::module_graph_query::ModuleGraphRecord&& graph,
                      driver::module_graph_query::ModuleGraphSccRecord&& scc) noexcept
      : contextRoots(zc::mv(contextRoots)),
        units(zc::mv(units)),
        toolchainInputs(zc::mv(toolchainInputs)),
        packageEdges(zc::mv(packageEdges)),
        crates(zc::mv(crates)),
        crateEdges(zc::mv(crateEdges)),
        sources(zc::mv(sources)),
        modules(zc::mv(modules)),
        graph(zc::mv(graph)),
        scc(zc::mv(scc)) {}

  ProviderAcquisition clone() const {
    zc::Vector<identity::CompilationUnitIdentity> clonedUnits(units.size());
    for (const auto& unit : units) { clonedUnits.add(unit.clone()); }
    zc::Vector<identity::ToolchainSemanticContextInput> clonedToolchainInputs(
        toolchainInputs.size());
    for (const auto& input : toolchainInputs) { clonedToolchainInputs.add(input.clone()); }
    zc::Vector<identity::PackageDependencyEdgeKey> clonedPackageEdges(packageEdges.size());
    for (const auto& edge : packageEdges) { clonedPackageEdges.add(edge.clone()); }
    zc::Vector<identity::CrateKey> clonedCrates(crates.size());
    for (const auto& crate : crates) { clonedCrates.add(crate.clone()); }
    zc::Vector<identity::CrateDependencyEdgeKey> clonedCrateEdges(crateEdges.size());
    for (const auto& edge : crateEdges) { clonedCrateEdges.add(edge.clone()); }
    zc::Vector<identity::SourceFileKey> clonedSources(sources.size());
    for (const auto& source : sources) { clonedSources.add(source.clone()); }
    zc::Vector<identity::ModuleKey> clonedModules(modules.size());
    for (const auto& module : modules) { clonedModules.add(module.clone()); }
    return ProviderAcquisition(
        contextRoots.clone(), zc::mv(clonedUnits), zc::mv(clonedToolchainInputs),
        zc::mv(clonedPackageEdges), zc::mv(clonedCrates), zc::mv(clonedCrateEdges),
        zc::mv(clonedSources), zc::mv(clonedModules), graph.clone(), scc.clone());
  }

  driver::incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  zc::Vector<identity::CompilationUnitIdentity> units;
  zc::Vector<identity::ToolchainSemanticContextInput> toolchainInputs;
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<identity::ModuleKey> modules;
  driver::module_graph_query::ModuleGraphRecord graph;
  driver::module_graph_query::ModuleGraphSccRecord scc;
};

struct ProviderSourceContent final {
  ProviderSourceContent(identity::SourceFileKey&& source,
                        const identity::Sha256Digest& digest) noexcept
      : source(zc::mv(source)), digest(digest) {}

  zc::Array<uint8_t> encode() const {
    identity::CanonicalEncoder encoder;
    source.encode(encoder);
    encoder.encodeDigest(digest);
    return encoder.finish();
  }

  identity::SourceFileKey source;
  identity::Sha256Digest digest;
};

struct ResolvedProviderAcquisition final {
  ResolvedProviderAcquisition(
      ProviderAcquisition&& acquisition, identity::SemanticContextFingerprint&& fingerprint,
      zc::Vector<driver::module_graph_query::StableMaterializedDependencyWitness>&&
          requestEdges) noexcept
      : acquisition(zc::mv(acquisition)),
        fingerprint(zc::mv(fingerprint)),
        requestEdges(zc::mv(requestEdges)) {}

  ProviderAcquisition acquisition;
  identity::SemanticContextFingerprint fingerprint;
  zc::Vector<driver::module_graph_query::StableMaterializedDependencyWitness> requestEdges;
};

struct MaterializedProviderAcquisition final {
  MaterializedProviderAcquisition(
      ResolvedProviderAcquisition&& resolved, identity::SemanticContextBrand context,
      zc::Vector<driver::module_graph_query::MaterializedCompilationUnitEntry>&& units,
      zc::Vector<driver::module_graph_query::MaterializedCrateEntry>&& crates,
      zc::Vector<driver::module_graph_query::MaterializedSourceEntry>&& sources,
      zc::Vector<driver::module_graph_query::MaterializedModuleEntry>&& modules) noexcept
      : resolved(zc::mv(resolved)),
        context(context),
        units(zc::mv(units)),
        crates(zc::mv(crates)),
        sources(zc::mv(sources)),
        modules(zc::mv(modules)) {}

  ResolvedProviderAcquisition resolved;
  identity::SemanticContextBrand context;
  zc::Vector<driver::module_graph_query::MaterializedCompilationUnitEntry> units;
  zc::Vector<driver::module_graph_query::MaterializedCrateEntry> crates;
  zc::Vector<driver::module_graph_query::MaterializedSourceEntry> sources;
  zc::Vector<driver::module_graph_query::MaterializedModuleEntry> modules;
};

template <typename Descriptor>
query::TypedQueryResult<typename Descriptor::Value> requireValue(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    const typename Descriptor::Key& key) {
  auto result = context.template get<Descriptor>(key);
  if (result.isRuntimeFailure()) {
    return query::TypedQueryResult<typename Descriptor::Value>::runtimeFailure(
        result.runtimeFailure());
  }
  if (result.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<typename Descriptor::Value>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<typename Descriptor::Value>::value(result.value().clone());
}

template <typename Entry>
zc::Maybe<const Entry&> inputEntryFor(zc::ArrayPtr<const Entry> entries,
                                      const identity::CrateKey& crate) {
  zc::Maybe<const Entry&> result;
  const auto expected = crate.encode();
  for (const auto& entry : entries) {
    if (entry.key().encode().asPtr() != expected.asPtr()) { continue; }
    if (result != zc::none) { return zc::none; }
    result = entry;
  }
  return result;
}

template <typename Descriptor, typename Value>
bool sameInputValue(const Value& left, const Value& right) {
  return Descriptor::encodeValue(left).asPtr() == Descriptor::encodeValue(right).asPtr();
}

query::TypedQueryResult<ProviderAcquisition> acquireProviderContext(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    const driver::incremental_binding_query::CompilationRootSetQueryKey& key) {
  using namespace driver;
  auto authority =
      requireValue<module_graph_query::CompleteCompilationContextAuthorityInput>(context, key);
  if (authority.isRuntimeFailure()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(authority.runtimeFailure());
  }
  if (authority.value().contextRoots().encodeCanonical().asPtr() != key.encodeCanonical().asPtr()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  auto packageGraph = requireValue<incremental_binding_query::PackageGraphInput>(
      context, authority.value().packageRootSet());
  if (packageGraph.isRuntimeFailure()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        packageGraph.runtimeFailure());
  }
  if (packageGraph.value() != authority.value().packageGraph()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  zc::Vector<identity::CrateKey> crates(authority.value().completeCrates().size());
  zc::Vector<identity::CompilationUnitIdentity> units;
  for (const auto& crate : authority.value().completeCrates()) {
    crates.add(crate.clone());
    bool present = false;
    for (const auto& unit : units) {
      if (unit.encode().asPtr() == crate.unit().encode().asPtr()) {
        present = true;
        break;
      }
    }
    if (!present) { units.add(crate.unit().clone()); }
  }
  canonicalSort(units, [](const identity::CompilationUnitIdentity& unit) { return unit.encode(); });

  if (authority.value().projectedCoreCrates().size() == 0) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& projectedCore = authority.value().projectedCoreCrates()[0];
  if (projectedCore.unit().kind() != identity::CompilationUnitKind::Toolchain ||
      projectedCore.unit().toolchain().component() != identity::ToolchainComponent::Core) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  for (const auto& crate : authority.value().projectedCoreCrates()) {
    if (crate.unit().encode().asPtr() != projectedCore.unit().encode().asPtr()) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
  }
  auto core = requireValue<core_library_query::CoreDistributionInput>(
      context, projectedCore.unit().toolchain());
  if (core.isRuntimeFailure()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(core.runtimeFailure());
  }
  if (core.value().record().encode().asPtr() !=
          authority.value().coreDistributionRecord().encode().asPtr() ||
      core.value().digest() != authority.value().coreDistributionDigest()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Vector<identity::ToolchainSemanticContextInput> toolchainInputs;
  toolchainInputs.add(identity::ToolchainSemanticContextInput::from(
      projectedCore.unit().toolchain(), core.value().digest(),
      core.value().policyTemplate().revision()));

  for (const auto& crate : crates) {
    auto expectedOptions = inputEntryFor(authority.value().compilationOptions(), crate);
    auto expectedRoots = inputEntryFor(authority.value().moduleSearchRoots(), crate);
    if (expectedOptions == zc::none || expectedRoots == zc::none) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto options = requireValue<identity::source_query::CompilationOptionsInput>(context, crate);
    auto roots =
        requireValue<incremental_module_resolution_query::ModuleSearchRootsInput>(context, crate);
    if (options.isRuntimeFailure() || roots.isRuntimeFailure()) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          options.isRuntimeFailure() ? options.runtimeFailure() : roots.runtimeFailure());
    }
    if (!sameInputValue<identity::source_query::CompilationOptionsInput>(
            options.value(), ZC_ASSERT_NONNULL(expectedOptions).value()) ||
        !sameInputValue<incremental_module_resolution_query::ModuleSearchRootsInput>(
            roots.value(), ZC_ASSERT_NONNULL(expectedRoots).value())) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
  }

  auto activeCrates = requireValue<incremental_binding_query::ActiveCratesQuery>(context, key);
  if (activeCrates.isRuntimeFailure() || activeCrates.value().crates().size() != crates.size()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        activeCrates.isRuntimeFailure() ? activeCrates.runtimeFailure()
                                        : query::QueryRuntimeFailure::InvariantViolation);
  }
  for (size_t index = 0; index < crates.size(); ++index) {
    if (activeCrates.value().crates()[index].canonicalCrateBytes() !=
        crates[index].encode().asPtr()) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
  }

  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<identity::ModuleKey> modules;
  for (const auto& crate : crates) {
    auto stableCrate = incremental_binding_query::StableCrateQueryKey::fromVerified(crate);
    if (stableCrate == zc::none) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto activeSources = requireValue<incremental_binding_query::ActiveSourcesQuery>(
        context, ZC_ASSERT_NONNULL(stableCrate));
    auto activeModules = requireValue<module_graph_query::ActiveModulesQuery>(context, crate);
    if (activeSources.isRuntimeFailure() || activeModules.isRuntimeFailure()) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          activeSources.isRuntimeFailure() ? activeSources.runtimeFailure()
                                           : activeModules.runtimeFailure());
    }
    for (const auto& sourceKey : activeSources.value().sources()) {
      auto source = decodeSource(sourceKey.canonicalSourceBytes());
      if (source == zc::none || !ZC_ASSERT_NONNULL(source).belongsTo(crate)) {
        return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      sources.add(zc::mv(ZC_ASSERT_NONNULL(source)));
    }
    for (const auto& module : activeModules.value().modules()) {
      if (module.crate().encode().asPtr() != crate.encode().asPtr()) {
        return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      modules.add(module.clone());
    }
  }
  canonicalSort(sources, [](const identity::SourceFileKey& source) { return source.encode(); });
  canonicalSort(modules, [](const identity::ModuleKey& module) { return module.encode(); });

  auto graph = requireValue<module_graph_query::ModuleGraphQuery>(context, key);
  auto scc = requireValue<module_graph_query::ModuleGraphSccQuery>(context, key);
  if (graph.isRuntimeFailure() || scc.isRuntimeFailure() ||
      graph.value().modules().size() != modules.size()) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        graph.isRuntimeFailure()
            ? graph.runtimeFailure()
            : (scc.isRuntimeFailure() ? scc.runtimeFailure()
                                      : query::QueryRuntimeFailure::InvariantViolation));
  }
  for (size_t index = 0; index < modules.size(); ++index) {
    if (!sameModule(graph.value().modules()[index], modules[index])) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
  }
  zc::Vector<module_graph_query::ModuleGraphSccComponent> components(
      scc.value().components().size());
  for (const auto& component : scc.value().components()) { components.add(component.clone()); }
  auto verifiedScc =
      module_graph_query::ModuleGraphSccRecord::fromVerified(graph.value(), zc::mv(components));
  if (verifiedScc == zc::none ||
      ZC_ASSERT_NONNULL(verifiedScc).encodeCanonical().asPtr() !=
          scc.value().encodeCanonical().asPtr() ||
      scc.value().hasCycle(graph.value())) {
    return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }

  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  for (const auto& stable : packageGraph.value().selectedPackageEdges()) {
    auto edge = decodePackageEdge(stable.canonicalEdgeBytes());
    if (edge == zc::none) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    packageEdges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
  }
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  for (const auto& stable : packageGraph.value().crateEdges()) {
    auto edge = decodeCrateEdge(stable.canonicalEdgeBytes());
    if (edge == zc::none) {
      return query::TypedQueryResult<ProviderAcquisition>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    crateEdges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
  }
  return query::TypedQueryResult<ProviderAcquisition>::value(
      ProviderAcquisition(key.clone(), zc::mv(units), zc::mv(toolchainInputs), zc::mv(packageEdges),
                          zc::mv(crates), zc::mv(crateEdges), zc::mv(sources), zc::mv(modules),
                          graph.value().clone(), scc.value().clone()));
}

template <typename Value>
bool appendFingerprintSequence(zc::Vector<uint8_t>& bytes, zc::ArrayPtr<const Value> values) {
  zc::Vector<zc::Array<uint8_t>> encoded(values.size());
  for (const auto& value : values) { encoded.add(value.encode()); }
  canonicalSort(encoded, [](const zc::Array<uint8_t>& value) { return value.asPtr(); });
  for (size_t index = 1; index < encoded.size(); ++index) {
    if (encoded[index - 1].asPtr() == encoded[index].asPtr()) { return false; }
  }
  appendUint64(bytes, encoded.size());
  for (const auto& value : encoded) { bytes.addAll(value.asPtr()); }
  return true;
}

zc::Maybe<identity::SemanticContextFingerprint> computeProviderFingerprint(
    const ProviderAcquisition& acquisition,
    zc::ArrayPtr<const ProviderSourceContent> sourceContents) {
  for (const auto& unit : acquisition.units) {
    if (unit.kind() != identity::CompilationUnitKind::Toolchain) { continue; }
    size_t matches = 0;
    for (const auto& input : acquisition.toolchainInputs) {
      if (input.toolchain().component() == unit.toolchain().component()) { ++matches; }
    }
    if (matches != 1) { return zc::none; }
  }
  for (const auto& input : acquisition.toolchainInputs) {
    size_t matches = 0;
    for (const auto& unit : acquisition.units) {
      if (unit.kind() == identity::CompilationUnitKind::Toolchain &&
          unit.toolchain().component() == input.toolchain().component()) {
        ++matches;
      }
    }
    if (matches != 1) { return zc::none; }
  }
  zc::Vector<uint8_t> bytes;
  bytes.addAll(kSemanticContextDomain.asBytes());
  bytes.add(0);
  if (!appendFingerprintSequence(bytes, acquisition.units.asPtr()) ||
      !appendFingerprintSequence(bytes, acquisition.toolchainInputs.asPtr()) ||
      !appendFingerprintSequence(bytes, acquisition.packageEdges.asPtr()) ||
      !appendFingerprintSequence(bytes, acquisition.crates.asPtr()) ||
      !appendFingerprintSequence(bytes, acquisition.crateEdges.asPtr()) ||
      !appendFingerprintSequence(bytes, sourceContents) ||
      !appendFingerprintSequence(bytes, acquisition.modules.asPtr())) {
    return zc::none;
  }
  auto digest = identity::sha256(bytes.asPtr());
  if (digest == zc::none) { return zc::none; }
  return identity::SemanticContextFingerprint::fromCanonicalDigest(ZC_ASSERT_NONNULL(digest));
}

using MaterializerProviderResult =
    query::CapabilityProviderResult<driver::module_graph_query::MaterializeModuleGraphQuery>;
using ProviderModuleAcquisitionResult =
    zc::OneOf<ResolvedProviderAcquisition, MaterializerProviderResult>;

template <typename SourceDescriptor>
MaterializerProviderResult forwardProviderSourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  using TargetContract =
      query::CapabilityFailureContract<driver::module_graph_query::MaterializeModuleGraphQuery,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto decoded = TargetContract::decode(SourceContract::encode(source.diagnostics()).asPtr());
  if (decoded == zc::none) {
    return MaterializerProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return MaterializerProviderResult::sourceRejected<diagnostics::DiagnosticFact>(
      zc::mv(ZC_ASSERT_NONNULL(decoded)));
}

template <typename SourceDescriptor>
MaterializerProviderResult forwardProviderKeyRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  return MaterializerProviderResult::keyRejected<binder::BinderKeyFailure>(
      source.keyFailure().clone());
}

zc::Maybe<const ProviderSourceContent&> sourceContentFor(
    zc::ArrayPtr<const ProviderSourceContent> contents, const identity::SourceFileKey& source) {
  zc::Maybe<const ProviderSourceContent&> result;
  const auto expected = source.encode();
  for (const auto& content : contents) {
    if (content.source.encode().asPtr() != expected.asPtr()) { continue; }
    if (result != zc::none) { return zc::none; }
    result = content;
  }
  return result;
}

ProviderModuleAcquisitionResult acquireProviderModules(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    ProviderAcquisition&& acquisition) {
  using namespace driver;
  zc::Vector<identity::SourceFileKey> moduleSources(acquisition.modules.size());
  zc::Vector<module_graph_query::ModuleDependencyRequestSetRecord> requestSets(
      acquisition.modules.size());
  zc::Vector<module_graph_query::StableMaterializedDependencyWitness> requestEdges;

  for (const auto& module : acquisition.modules) {
    auto selected = requireValue<module_graph_query::SelectedModuleSourceQuery>(context, module);
    if (selected.isRuntimeFailure()) {
      return MaterializerProviderResult::runtimeRejected(selected.runtimeFailure());
    }
    auto sites = requireValue<module_graph_query::ModuleDependencySitesQuery>(context, module);
    if (sites.isRuntimeFailure()) {
      return MaterializerProviderResult::runtimeRejected(sites.runtimeFailure());
    }
    auto requests =
        requireValue<module_graph_query::ModuleDependencyRequestsQuery>(context, module);
    if (requests.isRuntimeFailure()) {
      return MaterializerProviderResult::runtimeRejected(requests.runtimeFailure());
    }
    if (!selected.value().belongsTo(module.crate()) ||
        !sameModule(sites.value().module(), module) ||
        sites.value().source().encode().asPtr() != selected.value().encode().asPtr()) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }

    zc::Maybe<identity::ModuleKey> preludeTarget;
    for (const auto& request : requests.value().requests()) {
      if (!sameModule(request.requester(), module)) {
        return MaterializerProviderResult::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      auto resolution =
          requireValue<incremental_module_resolution_query::ResolveModuleRequestQuery>(context,
                                                                                       request);
      if (resolution.isRuntimeFailure()) {
        return MaterializerProviderResult::runtimeRejected(resolution.runtimeFailure());
      }
      if (resolution.value().candidates().size() != 1 ||
          !containsModule(acquisition.modules.asPtr(), resolution.value().candidates()[0])) {
        return MaterializerProviderResult::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      const auto& dependency = resolution.value().candidates()[0];
      auto edge = module_graph_query::StableMaterializedDependencyWitness::from(
          module.clone(), request.clone(), dependency.clone());
      if (edge == zc::none) {
        return MaterializerProviderResult::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
      requestEdges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
      if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
        if (preludeTarget != zc::none) {
          return MaterializerProviderResult::runtimeRejected(
              query::QueryRuntimeFailure::InvariantViolation);
        }
        preludeTarget = dependency.clone();
      }
    }

    ZC_IF_SOME(expectedPrelude, preludeTarget) {
      auto configured = requireValue<incremental_module_resolution_query::ConfiguredPreludeInput>(
          context, module.crate());
      if (configured.isRuntimeFailure()) {
        return MaterializerProviderResult::runtimeRejected(configured.runtimeFailure());
      }
      auto actualPrelude = configured.value().target();
      if (actualPrelude == zc::none ||
          !sameModule(ZC_ASSERT_NONNULL(actualPrelude), expectedPrelude)) {
        return MaterializerProviderResult::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
    }
    moduleSources.add(selected.value().clone());
    requestSets.add(requests.value().clone());
  }

  zc::Vector<identity::SourceFileKey> selectedSources(moduleSources.size());
  for (const auto& source : moduleSources) { selectedSources.add(source.clone()); }
  canonicalSort(selectedSources,
                [](const identity::SourceFileKey& source) { return source.encode(); });
  zc::Vector<identity::SourceFileKey> uniqueSources(selectedSources.size());
  for (const auto& source : selectedSources) {
    if (uniqueSources.size() == 0 ||
        uniqueSources.back().encode().asPtr() != source.encode().asPtr()) {
      uniqueSources.add(source.clone());
    }
  }
  if (uniqueSources.size() != acquisition.sources.size()) {
    return MaterializerProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  for (size_t index = 0; index < uniqueSources.size(); ++index) {
    if (uniqueSources[index].encode().asPtr() != acquisition.sources[index].encode().asPtr()) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
  }

  zc::Vector<ProviderSourceContent> sourceContents(uniqueSources.size());
  for (const auto& source : uniqueSources) {
    auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(source);
    if (sourceKey == zc::none) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto parse = context.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
    if (parse.isRuntimeRejected()) {
      return MaterializerProviderResult::runtimeRejected(parse.runtimeFailure());
    }
    if (parse.isSourceRejected()) { return forwardProviderSourceRejection(parse); }
    if (!parse.isPublished() ||
        parse.lease().capability().canonicalSourceKey() != source.encode().asPtr()) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    sourceContents.add(
        ProviderSourceContent(source.clone(), parse.lease().capability().contentDigest()));
  }

  for (size_t index = 0; index < acquisition.modules.size(); ++index) {
    const auto& module = acquisition.modules[index];
    auto provenance =
        context.getCapability<module_graph_query::ModuleDependencyProvenanceQuery>(module);
    if (provenance.isRuntimeRejected()) {
      return MaterializerProviderResult::runtimeRejected(provenance.runtimeFailure());
    }
    if (provenance.isKeyRejected()) { return forwardProviderKeyRejection(provenance); }
    if (provenance.isSourceRejected()) { return forwardProviderSourceRejection(provenance); }
    if (!provenance.isPublished()) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    const auto& candidate = provenance.lease().capability();
    auto content = sourceContentFor(sourceContents.asPtr(), moduleSources[index]);
    if (content == zc::none || !sameModule(candidate.module(), module) ||
        candidate.source().encode().asPtr() != moduleSources[index].encode().asPtr() ||
        candidate.sourceDigest() != ZC_ASSERT_NONNULL(content).digest ||
        candidate.entries().size() != requestSets[index].requests().size()) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    for (size_t requestIndex = 0; requestIndex < candidate.entries().size(); ++requestIndex) {
      if (candidate.entries()[requestIndex].request().encode().asPtr() !=
          requestSets[index].requests()[requestIndex].encode().asPtr()) {
        return MaterializerProviderResult::runtimeRejected(
            query::QueryRuntimeFailure::InvariantViolation);
      }
    }
  }

  auto fingerprint = computeProviderFingerprint(acquisition, sourceContents.asPtr());
  if (fingerprint == zc::none) {
    return MaterializerProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  canonicalSort(requestEdges,
                [](const module_graph_query::StableMaterializedDependencyWitness& edge) {
                  return edge.encodeCanonical();
                });
  return ResolvedProviderAcquisition(zc::mv(acquisition), zc::mv(ZC_ASSERT_NONNULL(fingerprint)),
                                     zc::mv(requestEdges));
}

using MaterializedProviderAcquisitionResult =
    zc::OneOf<MaterializedProviderAcquisition, MaterializerProviderResult>;

template <typename GlobalKey, typename MembershipDescriptor, typename Lookup>
query::TypedQueryResult<driver::module_graph_query::MaterializedIdentityEntry<
    GlobalKey, GlobalKey, typename query::ActiveMaterialization<GlobalKey>::Handle>>
materializeProviderIdentity(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    const driver::module_graph_query::ModuleGraphIdentityMaterializationResources& resources,
    const driver::incremental_binding_query::CompilationRootSetQueryKey& contextRoots,
    const GlobalKey& key, const typename MembershipDescriptor::Record& expectedAuthority,
    Lookup lookup) {
  using MembershipKey = typename MembershipDescriptor::Key;
  using Handle = typename query::ActiveMaterialization<GlobalKey>::Handle;
  using Entry = driver::module_graph_query::MaterializedIdentityEntry<GlobalKey, GlobalKey, Handle>;
  auto membershipKey = MembershipKey::from(contextRoots.clone(), key.clone());
  auto handle = context.template materializeActive<GlobalKey, MembershipDescriptor>(
      membershipKey, expectedAuthority);
  if (handle.isRuntimeFailure()) {
    return query::TypedQueryResult<Entry>::runtimeFailure(handle.runtimeFailure());
  }
  if (handle.kind() != query::QueryValueKind::Value ||
      !handle.value().belongsTo(resources.semanticContext())) {
    return query::TypedQueryResult<Entry>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto reverse = lookup(handle.value());
  if (reverse == zc::none || ZC_ASSERT_NONNULL(reverse).handle() != handle.value() ||
      ZC_ASSERT_NONNULL(reverse).key().encode().asPtr() != key.encode().asPtr() ||
      ZC_ASSERT_NONNULL(reverse).record().encode().asPtr() != key.encode().asPtr()) {
    return query::TypedQueryResult<Entry>::runtimeFailure(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Entry>::value(
      Entry::fromVerified(ZC_ASSERT_NONNULL(reverse).key().clone(),
                          ZC_ASSERT_NONNULL(reverse).record().clone(), handle.value()));
}

MaterializedProviderAcquisitionResult materializeProviderIdentities(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    ResolvedProviderAcquisition&& resolved) {
  using namespace driver;
  using Resource = module_graph_query::ModuleGraphIdentityMaterializationResources;
  auto resources = context.template semanticContextResources<Resource>();
  if (resources == zc::none) {
    return MaterializerProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& resource = ZC_ASSERT_NONNULL(resources);
  zc::Vector<module_graph_query::MaterializedCompilationUnitEntry> units(
      resolved.acquisition.units.size());
  zc::Vector<module_graph_query::MaterializedCrateEntry> crates(resolved.acquisition.crates.size());
  zc::Vector<module_graph_query::MaterializedSourceEntry> sources(
      resolved.acquisition.sources.size());
  zc::Vector<module_graph_query::MaterializedModuleEntry> modules(
      resolved.acquisition.modules.size());

  for (const auto& unit : resolved.acquisition.units) {
    zc::Vector<identity::CrateKey> activeCrates;
    for (const auto& crate : resolved.acquisition.crates) {
      if (crate.unit().encode().asPtr() == unit.encode().asPtr()) {
        activeCrates.add(crate.clone());
      }
    }
    auto authority = incremental_binding_query::ActiveCompilationUnitMembership::from(
        unit.clone(), zc::mv(activeCrates));
    if (authority == zc::none) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    auto entry = materializeProviderIdentity<
        identity::CompilationUnitIdentity,
        incremental_binding_query::ActiveCompilationUnitMembershipQuery>(
        context, resource, resolved.acquisition.contextRoots, unit, ZC_ASSERT_NONNULL(authority),
        [&](identity::CompilationUnitId handle) { return resource.compilationUnit(handle); });
    if (entry.isRuntimeFailure()) {
      return MaterializerProviderResult::runtimeRejected(entry.runtimeFailure());
    }
    units.add(entry.value().clone());
  }

  for (const auto& crate : resolved.acquisition.crates) {
    auto entry = materializeProviderIdentity<identity::CrateKey,
                                             incremental_binding_query::ActiveCrateMembershipQuery>(
        context, resource, resolved.acquisition.contextRoots, crate, crate,
        [&](identity::CrateId handle) { return resource.crate(handle); });
    if (entry.isRuntimeFailure()) {
      return MaterializerProviderResult::runtimeRejected(entry.runtimeFailure());
    }
    crates.add(entry.value().clone());
  }

  for (const auto& source : resolved.acquisition.sources) {
    auto entry =
        materializeProviderIdentity<identity::SourceFileKey,
                                    incremental_binding_query::ActiveSourceMembershipQuery>(
            context, resource, resolved.acquisition.contextRoots, source, source,
            [&](identity::SourceFileId handle) { return resource.sourceFile(handle); });
    if (entry.isRuntimeFailure()) {
      return MaterializerProviderResult::runtimeRejected(entry.runtimeFailure());
    }
    sources.add(entry.value().clone());
  }

  for (const auto& module : resolved.acquisition.modules) {
    auto entry =
        materializeProviderIdentity<identity::ModuleKey,
                                    incremental_binding_query::ActiveModuleMembershipQuery>(
            context, resource, resolved.acquisition.contextRoots, module, module,
            [&](identity::ModuleId handle) { return resource.module(handle); });
    if (entry.isRuntimeFailure()) {
      return MaterializerProviderResult::runtimeRejected(entry.runtimeFailure());
    }
    modules.add(entry.value().clone());
  }

  return MaterializedProviderAcquisition(zc::mv(resolved), resource.semanticContext(),
                                         zc::mv(units), zc::mv(crates), zc::mv(sources),
                                         zc::mv(modules));
}

zc::Maybe<identity::ModuleId> materializedModuleHandle(
    zc::ArrayPtr<const driver::module_graph_query::MaterializedModuleEntry> modules,
    const identity::ModuleKey& key) {
  zc::Maybe<identity::ModuleId> result;
  const auto expected = key.encode();
  for (const auto& module : modules) {
    if (module.key().encode().asPtr() != expected.asPtr()) { continue; }
    if (result != zc::none) { return zc::none; }
    result = module.handle();
  }
  return result;
}

MaterializerProviderResult publishProviderCandidate(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    MaterializedProviderAcquisition&& materialized) {
  using namespace driver::module_graph_query;
  auto revisionDigest = computeGraphRevision(materialized.resolved.fingerprint,
                                             materialized.resolved.acquisition.graph,
                                             materialized.resolved.requestEdges.asPtr());
  if (revisionDigest == zc::none) {
    return MaterializerProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Vector<MaterializedModuleDependencyEdge> handleEdges(
      materialized.resolved.requestEdges.size());
  for (const auto& edge : materialized.resolved.requestEdges) {
    auto requester = materializedModuleHandle(materialized.modules.asPtr(), edge.requester());
    auto dependency = materializedModuleHandle(materialized.modules.asPtr(), edge.dependency());
    if (requester == zc::none || dependency == zc::none) {
      return MaterializerProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    handleEdges.add(MaterializedModuleDependencyEdge(
        ZC_ASSERT_NONNULL(requester), edge.request().clone(), ZC_ASSERT_NONNULL(dependency)));
  }
  auto witness = MaterializedModuleGraphWitness::from(
      zc::mv(materialized.resolved.acquisition.contextRoots),
      zc::mv(materialized.resolved.fingerprint), zc::mv(materialized.resolved.acquisition.graph),
      zc::mv(materialized.resolved.acquisition.scc), zc::mv(materialized.resolved.requestEdges),
      binder::ModuleGraphRevision::fromCanonicalDigest(ZC_ASSERT_NONNULL(revisionDigest)));
  if (witness == zc::none) {
    return MaterializerProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = MaterializedModuleGraph::from(
      materialized.context, context.snapshotRevision(), zc::mv(ZC_ASSERT_NONNULL(witness)),
      zc::mv(materialized.units), zc::mv(materialized.crates), zc::mv(materialized.sources),
      zc::mv(materialized.modules), zc::mv(handleEdges));
  if (candidate == zc::none) {
    return MaterializerProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto owned = zc::heap<MaterializedModuleGraph>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  auto stableWitness =
      query::CapabilityCandidateContract<MaterializeModuleGraphQuery>::encode(*owned);
  return MaterializerProviderResult::candidate(zc::mv(owned), zc::mv(stableWitness));
}

struct VerifierSourceContent final {
  VerifierSourceContent(identity::SourceFileKey&& source,
                        const identity::Sha256Digest& digest) noexcept
      : source(zc::mv(source)), digest(digest) {}

  zc::Array<uint8_t> encode() const {
    identity::CanonicalEncoder encoder;
    source.encode(encoder);
    encoder.encodeDigest(digest);
    return encoder.finish();
  }

  identity::SourceFileKey source;
  identity::Sha256Digest digest;
};

int verifierCompareBytes(zc::ArrayPtr<const uint8_t> left,
                         zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return -1; }
    if (left[index] > right[index]) { return 1; }
  }
  if (left.size() < right.size()) { return -1; }
  return left.size() == right.size() ? 0 : 1;
}

void verifierAppendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    const uint32_t shift = 56 - index * 8;
    bytes.add(static_cast<uint8_t>((value >> shift) & 0xffu));
  }
}

template <typename Value, typename Bytes>
void verifierSort(zc::Vector<Value>& values, Bytes bytes) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 &&
           verifierCompareBytes(bytes(current), bytes(values[insertion - 1])) < 0) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

template <typename Entry>
zc::Maybe<const Entry&> verifierInputEntryFor(zc::ArrayPtr<const Entry> entries,
                                              const identity::CrateKey& crate) {
  zc::Maybe<const Entry&> result;
  const auto expected = crate.encode();
  for (const auto& entry : entries) {
    if (entry.key().encode().asPtr() != expected.asPtr()) { continue; }
    if (result != zc::none) { return zc::none; }
    result = entry;
  }
  return result;
}

template <typename Descriptor, typename Value>
bool verifierInputValuesMatch(const Value& left, const Value& right) {
  const auto leftBytes = Descriptor::encodeValue(left);
  const auto rightBytes = Descriptor::encodeValue(right);
  return verifierCompareBytes(leftBytes.asPtr(), rightBytes.asPtr()) == 0;
}

struct VerifierContextAcquisition final {
  VerifierContextAcquisition(zc::Vector<identity::CompilationUnitIdentity>&& units,
                             zc::Vector<identity::ToolchainSemanticContextInput>&& toolchainInputs,
                             zc::Vector<identity::PackageDependencyEdgeKey>&& packageEdges,
                             zc::Vector<identity::CrateKey>&& crates,
                             zc::Vector<identity::CrateDependencyEdgeKey>&& crateEdges,
                             zc::Vector<identity::SourceFileKey>&& sources,
                             zc::Vector<identity::ModuleKey>&& modules,
                             driver::module_graph_query::ModuleGraphRecord&& graph,
                             driver::module_graph_query::ModuleGraphSccRecord&& scc) noexcept
      : units(zc::mv(units)),
        toolchainInputs(zc::mv(toolchainInputs)),
        packageEdges(zc::mv(packageEdges)),
        crates(zc::mv(crates)),
        crateEdges(zc::mv(crateEdges)),
        sources(zc::mv(sources)),
        modules(zc::mv(modules)),
        graph(zc::mv(graph)),
        scc(zc::mv(scc)) {}

  zc::Vector<identity::CompilationUnitIdentity> units;
  zc::Vector<identity::ToolchainSemanticContextInput> toolchainInputs;
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  zc::Vector<identity::CrateKey> crates;
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<identity::ModuleKey> modules;
  driver::module_graph_query::ModuleGraphRecord graph;
  driver::module_graph_query::ModuleGraphSccRecord scc;
};

zc::Maybe<VerifierContextAcquisition> acquireVerifierContext(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    const driver::incremental_binding_query::CompilationRootSetQueryKey& key) {
  using namespace driver;
  auto authority = context.get<module_graph_query::CompleteCompilationContextAuthorityInput>(key);
  if (authority.isRuntimeFailure() || authority.kind() != query::QueryValueKind::Value ||
      authority.value().contextRoots().encodeCanonical().asPtr() != key.encodeCanonical().asPtr()) {
    return zc::none;
  }
  auto packageGraph =
      context.get<incremental_binding_query::PackageGraphInput>(authority.value().packageRootSet());
  if (packageGraph.isRuntimeFailure() || packageGraph.kind() != query::QueryValueKind::Value ||
      packageGraph.value() != authority.value().packageGraph()) {
    return zc::none;
  }

  zc::Vector<identity::CrateKey> crates(authority.value().completeCrates().size());
  for (const auto& crate : authority.value().completeCrates()) { crates.add(crate.clone()); }
  verifierSort(crates, [](const identity::CrateKey& crate) { return crate.encode(); });
  for (size_t index = 0; index < crates.size(); ++index) {
    if (crates[index].encode().asPtr() !=
            authority.value().completeCrates()[index].encode().asPtr() ||
        (index != 0 && crates[index - 1].encode().asPtr() == crates[index].encode().asPtr())) {
      return zc::none;
    }
  }
  zc::Vector<identity::CompilationUnitIdentity> units;
  for (const auto& crate : crates) {
    bool present = false;
    for (const auto& unit : units) {
      if (unit.encode().asPtr() == crate.unit().encode().asPtr()) {
        present = true;
        break;
      }
    }
    if (!present) { units.add(crate.unit().clone()); }
  }
  verifierSort(units, [](const identity::CompilationUnitIdentity& unit) { return unit.encode(); });

  if (authority.value().projectedCoreCrates().size() == 0) { return zc::none; }
  const auto& projectedCore = authority.value().projectedCoreCrates()[0];
  if (projectedCore.unit().kind() != identity::CompilationUnitKind::Toolchain ||
      projectedCore.unit().toolchain().component() != identity::ToolchainComponent::Core) {
    return zc::none;
  }
  for (const auto& crate : authority.value().projectedCoreCrates()) {
    if (crate.unit().encode().asPtr() != projectedCore.unit().encode().asPtr()) { return zc::none; }
  }
  auto core =
      context.get<core_library_query::CoreDistributionInput>(projectedCore.unit().toolchain());
  if (core.isRuntimeFailure() || core.kind() != query::QueryValueKind::Value ||
      core.value().record().encode().asPtr() !=
          authority.value().coreDistributionRecord().encode().asPtr() ||
      core.value().digest() != authority.value().coreDistributionDigest()) {
    return zc::none;
  }
  zc::Vector<identity::ToolchainSemanticContextInput> toolchainInputs;
  toolchainInputs.add(identity::ToolchainSemanticContextInput::from(
      projectedCore.unit().toolchain(), core.value().digest(),
      core.value().policyTemplate().revision()));

  for (const auto& crate : crates) {
    auto expectedOptions = verifierInputEntryFor(authority.value().compilationOptions(), crate);
    auto expectedRoots = verifierInputEntryFor(authority.value().moduleSearchRoots(), crate);
    if (expectedOptions == zc::none || expectedRoots == zc::none) { return zc::none; }
    auto options = context.get<identity::source_query::CompilationOptionsInput>(crate);
    if (options.isRuntimeFailure() || options.kind() != query::QueryValueKind::Value ||
        !verifierInputValuesMatch<identity::source_query::CompilationOptionsInput>(
            options.value(), ZC_ASSERT_NONNULL(expectedOptions).value())) {
      return zc::none;
    }
    auto roots = context.get<incremental_module_resolution_query::ModuleSearchRootsInput>(crate);
    if (roots.isRuntimeFailure() || roots.kind() != query::QueryValueKind::Value ||
        !verifierInputValuesMatch<incremental_module_resolution_query::ModuleSearchRootsInput>(
            roots.value(), ZC_ASSERT_NONNULL(expectedRoots).value())) {
      return zc::none;
    }
  }
  zc::Vector<identity::PackageDependencyEdgeKey> packageEdges;
  for (const auto& stable : packageGraph.value().selectedPackageEdges()) {
    auto edge = decodePackageEdge(stable.canonicalEdgeBytes());
    if (edge == zc::none) { return zc::none; }
    packageEdges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
  }
  zc::Vector<identity::CrateDependencyEdgeKey> crateEdges;
  for (const auto& stable : packageGraph.value().crateEdges()) {
    auto edge = decodeCrateEdge(stable.canonicalEdgeBytes());
    if (edge == zc::none) { return zc::none; }
    crateEdges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
  }

  auto activeCrates = context.get<incremental_binding_query::ActiveCratesQuery>(key);
  if (activeCrates.isRuntimeFailure() || activeCrates.kind() != query::QueryValueKind::Value ||
      activeCrates.value().crates().size() != crates.size()) {
    return zc::none;
  }
  for (size_t index = 0; index < crates.size(); ++index) {
    if (activeCrates.value().crates()[index].canonicalCrateBytes() !=
        crates[index].encode().asPtr()) {
      return zc::none;
    }
  }
  zc::Vector<identity::SourceFileKey> sources;
  zc::Vector<identity::ModuleKey> modules;
  for (const auto& crate : crates) {
    auto stableCrate = incremental_binding_query::StableCrateQueryKey::fromVerified(crate);
    if (stableCrate == zc::none) { return zc::none; }
    auto activeSources =
        context.get<incremental_binding_query::ActiveSourcesQuery>(ZC_ASSERT_NONNULL(stableCrate));
    if (activeSources.isRuntimeFailure() || activeSources.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    for (const auto& sourceKey : activeSources.value().sources()) {
      auto source = decodeSource(sourceKey.canonicalSourceBytes());
      if (source == zc::none || !ZC_ASSERT_NONNULL(source).belongsTo(crate)) { return zc::none; }
      sources.add(zc::mv(ZC_ASSERT_NONNULL(source)));
    }
    auto activeModules = context.get<module_graph_query::ActiveModulesQuery>(crate);
    if (activeModules.isRuntimeFailure() || activeModules.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    for (const auto& module : activeModules.value().modules()) {
      if (module.crate().encode().asPtr() != crate.encode().asPtr()) { return zc::none; }
      modules.add(module.clone());
    }
  }
  verifierSort(sources, [](const identity::SourceFileKey& source) { return source.encode(); });
  verifierSort(modules, [](const identity::ModuleKey& module) { return module.encode(); });

  auto graph = context.get<module_graph_query::ModuleGraphQuery>(key);
  if (graph.isRuntimeFailure() || graph.kind() != query::QueryValueKind::Value ||
      graph.value().modules().size() != modules.size()) {
    return zc::none;
  }
  for (size_t index = 0; index < modules.size(); ++index) {
    if (graph.value().modules()[index].encode().asPtr() != modules[index].encode().asPtr()) {
      return zc::none;
    }
  }
  auto scc = context.get<module_graph_query::ModuleGraphSccQuery>(key);
  if (scc.isRuntimeFailure() || scc.kind() != query::QueryValueKind::Value) { return zc::none; }
  zc::Vector<module_graph_query::ModuleGraphSccComponent> components(
      scc.value().components().size());
  for (const auto& component : scc.value().components()) { components.add(component.clone()); }
  auto rebuiltScc =
      module_graph_query::ModuleGraphSccRecord::fromVerified(graph.value(), zc::mv(components));
  if (rebuiltScc == zc::none ||
      ZC_ASSERT_NONNULL(rebuiltScc).encodeCanonical().asPtr() !=
          scc.value().encodeCanonical().asPtr() ||
      scc.value().hasCycle(graph.value())) {
    return zc::none;
  }
  return VerifierContextAcquisition(zc::mv(units), zc::mv(toolchainInputs), zc::mv(packageEdges),
                                    zc::mv(crates), zc::mv(crateEdges), zc::mv(sources),
                                    zc::mv(modules), graph.value().clone(), scc.value().clone());
}

template <typename Value>
bool verifierAppendSequence(zc::Vector<uint8_t>& bytes, zc::ArrayPtr<const Value> values) {
  zc::Vector<zc::Array<uint8_t>> encoded(values.size());
  for (const auto& value : values) { encoded.add(value.encode()); }
  verifierSort(encoded, [](const zc::Array<uint8_t>& value) { return value.asPtr(); });
  for (size_t index = 1; index < encoded.size(); ++index) {
    if (encoded[index - 1].asPtr() == encoded[index].asPtr()) { return false; }
  }
  verifierAppendUint64(bytes, encoded.size());
  for (const auto& value : encoded) { bytes.addAll(value.asPtr()); }
  return true;
}

zc::Maybe<identity::SemanticContextFingerprint> computeVerifierFingerprint(
    zc::ArrayPtr<const identity::CompilationUnitIdentity> units,
    zc::ArrayPtr<const identity::ToolchainSemanticContextInput> toolchainInputs,
    zc::ArrayPtr<const identity::PackageDependencyEdgeKey> packageEdges,
    zc::ArrayPtr<const identity::CrateKey> crates,
    zc::ArrayPtr<const identity::CrateDependencyEdgeKey> crateEdges,
    zc::ArrayPtr<const VerifierSourceContent> sourceContents,
    zc::ArrayPtr<const identity::ModuleKey> modules) {
  for (const auto& unit : units) {
    if (unit.kind() != identity::CompilationUnitKind::Toolchain) { continue; }
    size_t matches = 0;
    for (const auto& input : toolchainInputs) {
      if (input.toolchain().component() == unit.toolchain().component()) { ++matches; }
    }
    if (matches != 1) { return zc::none; }
  }
  for (const auto& input : toolchainInputs) {
    size_t matches = 0;
    for (const auto& unit : units) {
      if (unit.kind() == identity::CompilationUnitKind::Toolchain &&
          unit.toolchain().component() == input.toolchain().component()) {
        ++matches;
      }
    }
    if (matches != 1) { return zc::none; }
  }
  zc::Vector<uint8_t> bytes;
  bytes.addAll(kSemanticContextDomain.asBytes());
  bytes.add(0);
  if (!verifierAppendSequence(bytes, units) || !verifierAppendSequence(bytes, toolchainInputs) ||
      !verifierAppendSequence(bytes, packageEdges) || !verifierAppendSequence(bytes, crates) ||
      !verifierAppendSequence(bytes, crateEdges) ||
      !verifierAppendSequence(bytes, sourceContents) || !verifierAppendSequence(bytes, modules)) {
    return zc::none;
  }
  auto digest = identity::sha256(bytes.asPtr());
  if (digest == zc::none) { return zc::none; }
  return identity::SemanticContextFingerprint::fromCanonicalDigest(ZC_ASSERT_NONNULL(digest));
}

struct VerifierModuleAcquisition final {
  VerifierModuleAcquisition(
      VerifierContextAcquisition&& acquisition, identity::SemanticContextFingerprint&& fingerprint,
      zc::Vector<driver::module_graph_query::StableMaterializedDependencyWitness>&&
          stableEdges) noexcept
      : acquisition(zc::mv(acquisition)),
        fingerprint(zc::mv(fingerprint)),
        stableEdges(zc::mv(stableEdges)) {}

  VerifierContextAcquisition acquisition;
  identity::SemanticContextFingerprint fingerprint;
  zc::Vector<driver::module_graph_query::StableMaterializedDependencyWitness> stableEdges;
};

zc::Maybe<VerifierModuleAcquisition> acquireVerifierModules(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    VerifierContextAcquisition&& acquisition) {
  using namespace driver::module_graph_query;
  zc::Vector<identity::SourceFileKey> moduleSources(acquisition.modules.size());
  zc::Vector<ModuleDependencyRequestSetRecord> requestSets(acquisition.modules.size());
  zc::Vector<StableMaterializedDependencyWitness> stableEdges;
  for (const auto& module : acquisition.modules) {
    auto selected = context.get<SelectedModuleSourceQuery>(module);
    if (selected.isRuntimeFailure() || selected.kind() != query::QueryValueKind::Value ||
        !selected.value().belongsTo(module.crate())) {
      return zc::none;
    }
    auto sites = context.get<ModuleDependencySitesQuery>(module);
    if (sites.isRuntimeFailure() || sites.kind() != query::QueryValueKind::Value ||
        sites.value().module().encode().asPtr() != module.encode().asPtr() ||
        sites.value().source().encode().asPtr() != selected.value().encode().asPtr()) {
      return zc::none;
    }
    auto requests = context.get<ModuleDependencyRequestsQuery>(module);
    if (requests.isRuntimeFailure() || requests.kind() != query::QueryValueKind::Value) {
      return zc::none;
    }
    zc::Maybe<identity::ModuleKey> preludeTarget;
    for (const auto& request : requests.value().requests()) {
      if (request.requester().encode().asPtr() != module.encode().asPtr()) { return zc::none; }
      auto resolution =
          context.get<driver::incremental_module_resolution_query::ResolveModuleRequestQuery>(
              request);
      if (resolution.isRuntimeFailure() || resolution.kind() != query::QueryValueKind::Value ||
          resolution.value().candidates().size() != 1) {
        return zc::none;
      }
      bool reached = false;
      for (const auto& reachedModule : acquisition.modules) {
        if (reachedModule.encode().asPtr() == resolution.value().candidates()[0].encode().asPtr()) {
          reached = true;
          break;
        }
      }
      auto edge = StableMaterializedDependencyWitness::from(
          module.clone(), request.clone(), resolution.value().candidates()[0].clone());
      if (!reached || edge == zc::none) { return zc::none; }
      stableEdges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
      if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
        if (preludeTarget != zc::none) { return zc::none; }
        preludeTarget = resolution.value().candidates()[0].clone();
      }
    }
    ZC_IF_SOME(expectedPrelude, preludeTarget) {
      auto configured =
          context.get<driver::incremental_module_resolution_query::ConfiguredPreludeInput>(
              module.crate());
      if (configured.isRuntimeFailure() || configured.kind() != query::QueryValueKind::Value ||
          configured.value().target() == zc::none ||
          ZC_ASSERT_NONNULL(configured.value().target()).encode().asPtr() !=
              expectedPrelude.encode().asPtr()) {
        return zc::none;
      }
    }
    moduleSources.add(selected.value().clone());
    requestSets.add(requests.value().clone());
  }
  verifierSort(stableEdges, [](const StableMaterializedDependencyWitness& edge) {
    return edge.encodeCanonical();
  });
  for (size_t index = 1; index < stableEdges.size(); ++index) {
    if (stableEdges[index - 1] == stableEdges[index]) { return zc::none; }
  }

  zc::Vector<identity::SourceFileKey> orderedSources(moduleSources.size());
  for (const auto& source : moduleSources) { orderedSources.add(source.clone()); }
  verifierSort(orderedSources,
               [](const identity::SourceFileKey& source) { return source.encode(); });
  zc::Vector<identity::SourceFileKey> selectedSources(orderedSources.size());
  for (const auto& source : orderedSources) {
    if (selectedSources.size() == 0 ||
        selectedSources.back().encode().asPtr() != source.encode().asPtr()) {
      selectedSources.add(source.clone());
    }
  }
  if (selectedSources.size() != acquisition.sources.size()) { return zc::none; }
  zc::Vector<VerifierSourceContent> sourceContents(acquisition.sources.size());
  for (size_t index = 0; index < acquisition.sources.size(); ++index) {
    if (selectedSources[index].encode().asPtr() != acquisition.sources[index].encode().asPtr()) {
      return zc::none;
    }
    auto sourceKey =
        identity::source_query::StableSourceQueryKey::fromVerified(acquisition.sources[index]);
    if (sourceKey == zc::none) { return zc::none; }
    auto parse = context.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
    if (!parse.isPublished() || parse.lease().capability().canonicalSourceKey() !=
                                    acquisition.sources[index].encode().asPtr()) {
      return zc::none;
    }
    sourceContents.add(VerifierSourceContent(acquisition.sources[index].clone(),
                                             parse.lease().capability().contentDigest()));
  }

  for (size_t index = 0; index < acquisition.modules.size(); ++index) {
    auto provenance =
        context.getCapability<ModuleDependencyProvenanceQuery>(acquisition.modules[index]);
    if (!provenance.isPublished()) { return zc::none; }
    const auto& value = provenance.lease().capability();
    zc::Maybe<const VerifierSourceContent&> content;
    for (const auto& source : sourceContents) {
      if (source.source.encode().asPtr() != value.source().encode().asPtr()) { continue; }
      if (content != zc::none) { return zc::none; }
      content = source;
    }
    if (content == zc::none ||
        value.module().encode().asPtr() != acquisition.modules[index].encode().asPtr() ||
        value.source().encode().asPtr() != moduleSources[index].encode().asPtr() ||
        value.sourceDigest() != ZC_ASSERT_NONNULL(content).digest ||
        value.entries().size() != requestSets[index].requests().size()) {
      return zc::none;
    }
    for (size_t requestIndex = 0; requestIndex < value.entries().size(); ++requestIndex) {
      if (value.entries()[requestIndex].request().encode().asPtr() !=
          requestSets[index].requests()[requestIndex].encode().asPtr()) {
        return zc::none;
      }
    }
  }
  auto fingerprint = computeVerifierFingerprint(
      acquisition.units.asPtr(), acquisition.toolchainInputs.asPtr(),
      acquisition.packageEdges.asPtr(), acquisition.crates.asPtr(), acquisition.crateEdges.asPtr(),
      sourceContents.asPtr(), acquisition.modules.asPtr());
  if (fingerprint == zc::none) { return zc::none; }
  return VerifierModuleAcquisition(zc::mv(acquisition), zc::mv(ZC_ASSERT_NONNULL(fingerprint)),
                                   zc::mv(stableEdges));
}

template <typename SourceHandler, typename KeyHandler>
query::CapabilityRejectionCheck verifyMaterializerRejection(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    const driver::incremental_binding_query::CompilationRootSetQueryKey& key,
    SourceHandler sourceHandler, KeyHandler keyHandler) {
  using namespace driver::module_graph_query;
  auto contextState = acquireVerifierContext(context, key);
  if (contextState == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
  auto acquisition = zc::mv(ZC_ASSERT_NONNULL(contextState));
  zc::Vector<identity::SourceFileKey> moduleSources(acquisition.modules.size());
  zc::Vector<ModuleDependencyRequestSetRecord> requestSets(acquisition.modules.size());

  for (const auto& module : acquisition.modules) {
    auto selected = context.get<SelectedModuleSourceQuery>(module);
    if (selected.isRuntimeFailure() || selected.kind() != query::QueryValueKind::Value) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    auto sites = context.get<ModuleDependencySitesQuery>(module);
    if (sites.isRuntimeFailure() || sites.kind() != query::QueryValueKind::Value) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    auto requests = context.get<ModuleDependencyRequestsQuery>(module);
    if (requests.isRuntimeFailure() || requests.kind() != query::QueryValueKind::Value ||
        !selected.value().belongsTo(module.crate()) ||
        sites.value().module().encode().asPtr() != module.encode().asPtr() ||
        sites.value().source().encode().asPtr() != selected.value().encode().asPtr()) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    zc::Maybe<identity::ModuleKey> preludeTarget;
    for (const auto& request : requests.value().requests()) {
      if (request.requester().encode().asPtr() != module.encode().asPtr()) {
        return query::CapabilityRejectionCheck::Rejected;
      }
      auto resolution =
          context.get<driver::incremental_module_resolution_query::ResolveModuleRequestQuery>(
              request);
      if (resolution.isRuntimeFailure() || resolution.kind() != query::QueryValueKind::Value ||
          resolution.value().candidates().size() != 1) {
        return query::CapabilityRejectionCheck::Rejected;
      }
      bool reached = false;
      for (const auto& reachedModule : acquisition.modules) {
        if (reachedModule.encode().asPtr() == resolution.value().candidates()[0].encode().asPtr()) {
          reached = true;
          break;
        }
      }
      auto edge = StableMaterializedDependencyWitness::from(
          module.clone(), request.clone(), resolution.value().candidates()[0].clone());
      if (!reached || edge == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
      if (request.dependencyKind() == identity::ModuleDependencyKind::Prelude) {
        if (preludeTarget != zc::none) { return query::CapabilityRejectionCheck::Rejected; }
        preludeTarget = resolution.value().candidates()[0].clone();
      }
    }
    ZC_IF_SOME(expectedPrelude, preludeTarget) {
      auto configured =
          context.get<driver::incremental_module_resolution_query::ConfiguredPreludeInput>(
              module.crate());
      if (configured.isRuntimeFailure() || configured.kind() != query::QueryValueKind::Value ||
          configured.value().target() == zc::none ||
          ZC_ASSERT_NONNULL(configured.value().target()).encode().asPtr() !=
              expectedPrelude.encode().asPtr()) {
        return query::CapabilityRejectionCheck::Rejected;
      }
    }
    moduleSources.add(selected.value().clone());
    requestSets.add(requests.value().clone());
  }

  zc::Vector<identity::SourceFileKey> selectedSources(moduleSources.size());
  for (const auto& source : moduleSources) { selectedSources.add(source.clone()); }
  verifierSort(selectedSources,
               [](const identity::SourceFileKey& source) { return source.encode(); });
  zc::Vector<identity::SourceFileKey> uniqueSources(selectedSources.size());
  for (const auto& source : selectedSources) {
    if (uniqueSources.size() == 0 ||
        uniqueSources.back().encode().asPtr() != source.encode().asPtr()) {
      uniqueSources.add(source.clone());
    }
  }
  if (uniqueSources.size() != acquisition.sources.size()) {
    return query::CapabilityRejectionCheck::Rejected;
  }

  zc::Vector<VerifierSourceContent> sourceContents(uniqueSources.size());
  for (size_t index = 0; index < uniqueSources.size(); ++index) {
    if (uniqueSources[index].encode().asPtr() != acquisition.sources[index].encode().asPtr()) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    auto sourceKey =
        identity::source_query::StableSourceQueryKey::fromVerified(uniqueSources[index]);
    if (sourceKey == zc::none) { return query::CapabilityRejectionCheck::Rejected; }
    auto parse = context.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(sourceKey));
    if (parse.isRuntimeRejected()) { return query::CapabilityRejectionCheck::Rejected; }
    if (parse.isSourceRejected()) { return sourceHandler(parse.diagnostics().values()); }
    if (!parse.isPublished() ||
        parse.lease().capability().canonicalSourceKey() != uniqueSources[index].encode().asPtr()) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    sourceContents.add(VerifierSourceContent(uniqueSources[index].clone(),
                                             parse.lease().capability().contentDigest()));
  }

  for (size_t index = 0; index < acquisition.modules.size(); ++index) {
    auto provenance =
        context.getCapability<ModuleDependencyProvenanceQuery>(acquisition.modules[index]);
    if (provenance.isRuntimeRejected()) { return query::CapabilityRejectionCheck::Rejected; }
    if (provenance.isKeyRejected()) { return keyHandler(provenance.keyFailure()); }
    if (provenance.isSourceRejected()) { return sourceHandler(provenance.diagnostics().values()); }
    if (!provenance.isPublished()) { return query::CapabilityRejectionCheck::Rejected; }
    const auto& value = provenance.lease().capability();
    zc::Maybe<const VerifierSourceContent&> content;
    for (const auto& source : sourceContents) {
      if (source.source.encode().asPtr() != value.source().encode().asPtr()) { continue; }
      if (content != zc::none) { return query::CapabilityRejectionCheck::Rejected; }
      content = source;
    }
    if (content == zc::none ||
        value.module().encode().asPtr() != acquisition.modules[index].encode().asPtr() ||
        value.source().encode().asPtr() != moduleSources[index].encode().asPtr() ||
        value.sourceDigest() != ZC_ASSERT_NONNULL(content).digest ||
        value.entries().size() != requestSets[index].requests().size()) {
      return query::CapabilityRejectionCheck::Rejected;
    }
    for (size_t requestIndex = 0; requestIndex < value.entries().size(); ++requestIndex) {
      if (value.entries()[requestIndex].request().encode().asPtr() !=
          requestSets[index].requests()[requestIndex].encode().asPtr()) {
        return query::CapabilityRejectionCheck::Rejected;
      }
    }
  }
  return query::CapabilityRejectionCheck::Rejected;
}

zc::Maybe<identity::Sha256Digest> computeVerifierGraphRevision(
    const identity::SemanticContextFingerprint& fingerprint,
    const driver::module_graph_query::ModuleGraphRecord& graph,
    zc::ArrayPtr<const driver::module_graph_query::StableMaterializedDependencyWitness> edges) {
  zc::Vector<uint8_t> bytes;
  bytes.addAll(kGraphRevisionDomain.asBytes());
  bytes.add(0);
  bytes.addAll(fingerprint.digest().bytes());
  const auto graphBytes = graph.encodeCanonical();
  verifierAppendUint64(bytes, graphBytes.size());
  bytes.addAll(graphBytes.asPtr());
  verifierAppendUint64(bytes, edges.size());
  for (size_t index = 0; index < edges.size(); ++index) {
    const auto edgeBytes = edges[index].encodeCanonical();
    if (index != 0 &&
        verifierCompareBytes(edges[index - 1].encodeCanonical().asPtr(), edgeBytes.asPtr()) >= 0) {
      return zc::none;
    }
    verifierAppendUint64(bytes, edgeBytes.size());
    bytes.addAll(edgeBytes.asPtr());
  }
  return identity::sha256(bytes.asPtr());
}

bool verifierEdgesMatchGraph(
    const driver::module_graph_query::ModuleGraphRecord& graph,
    zc::ArrayPtr<const driver::module_graph_query::StableMaterializedDependencyWitness> edges) {
  for (const auto& edge : edges) {
    bool found = false;
    for (const auto& graphEdge : graph.edges()) {
      if (edge.requester().encode().asPtr() == graphEdge.requester().encode().asPtr() &&
          edge.dependency().encode().asPtr() == graphEdge.dependency().encode().asPtr()) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  for (const auto& graphEdge : graph.edges()) {
    bool found = false;
    for (const auto& edge : edges) {
      if (edge.requester().encode().asPtr() == graphEdge.requester().encode().asPtr() &&
          edge.dependency().encode().asPtr() == graphEdge.dependency().encode().asPtr()) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  return true;
}

template <typename GlobalKey, typename MembershipDescriptor, typename CandidateEntry,
          typename Lookup>
bool verifyMaterializedIdentity(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    const driver::module_graph_query::ModuleGraphIdentityMaterializationResources& resources,
    const driver::incremental_binding_query::CompilationRootSetQueryKey& contextRoots,
    const GlobalKey& key, const typename MembershipDescriptor::Record& expectedAuthority,
    const CandidateEntry& candidate, Lookup lookup) {
  using MembershipKey = typename MembershipDescriptor::Key;
  auto membershipKey = MembershipKey::from(contextRoots.clone(), key.clone());
  auto handle = context.template materializeActive<GlobalKey, MembershipDescriptor>(
      membershipKey, expectedAuthority);
  if (handle.isRuntimeFailure() || handle.kind() != query::QueryValueKind::Value ||
      handle.value() != candidate.handle() ||
      !candidate.handle().belongsTo(resources.semanticContext()) ||
      candidate.key().encode().asPtr() != key.encode().asPtr() ||
      candidate.record().encode().asPtr() != key.encode().asPtr()) {
    return false;
  }
  auto reverse = lookup(candidate.handle());
  return reverse != zc::none && ZC_ASSERT_NONNULL(reverse).handle() == candidate.handle() &&
         ZC_ASSERT_NONNULL(reverse).key().encode().asPtr() == key.encode().asPtr() &&
         ZC_ASSERT_NONNULL(reverse).record().encode().asPtr() == key.encode().asPtr();
}

zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&> verifierModuleForHandle(
    zc::ArrayPtr<const driver::module_graph_query::MaterializedModuleEntry> modules,
    identity::ModuleId handle) {
  zc::Maybe<const driver::module_graph_query::MaterializedModuleEntry&> result;
  for (const auto& module : modules) {
    if (module.handle() != handle) { continue; }
    if (result != zc::none) { return zc::none; }
    result = module;
  }
  return result;
}

zc::Maybe<zc::Array<uint8_t>> verifyMaterializedCandidate(
    query::CapabilityQueryContext<driver::module_graph_query::MaterializeModuleGraphQuery>& context,
    const driver::incremental_binding_query::CompilationRootSetQueryKey& key,
    const driver::module_graph_query::MaterializedModuleGraph& candidate,
    VerifierModuleAcquisition&& state) {
  using namespace driver;
  using Resource = module_graph_query::ModuleGraphIdentityMaterializationResources;
  const auto& acquisition = state.acquisition;
  auto resources = context.template semanticContextResources<Resource>();
  if (resources == zc::none || candidate.units().size() != acquisition.units.size() ||
      candidate.crates().size() != acquisition.crates.size() ||
      candidate.sources().size() != acquisition.sources.size() ||
      candidate.modules().size() != acquisition.modules.size()) {
    return zc::none;
  }
  const auto& resource = ZC_ASSERT_NONNULL(resources);

  for (size_t index = 0; index < acquisition.units.size(); ++index) {
    zc::Vector<identity::CrateKey> activeCrates;
    for (const auto& crate : acquisition.crates) {
      if (crate.unit().encode().asPtr() == acquisition.units[index].encode().asPtr()) {
        activeCrates.add(crate.clone());
      }
    }
    auto authority = incremental_binding_query::ActiveCompilationUnitMembership::from(
        acquisition.units[index].clone(), zc::mv(activeCrates));
    if (authority == zc::none ||
        !verifyMaterializedIdentity<
            identity::CompilationUnitIdentity,
            incremental_binding_query::ActiveCompilationUnitMembershipQuery>(
            context, resource, key, acquisition.units[index], ZC_ASSERT_NONNULL(authority),
            candidate.units()[index],
            [&](identity::CompilationUnitId handle) { return resource.compilationUnit(handle); })) {
      return zc::none;
    }
  }
  for (size_t index = 0; index < acquisition.crates.size(); ++index) {
    if (!verifyMaterializedIdentity<identity::CrateKey,
                                    incremental_binding_query::ActiveCrateMembershipQuery>(
            context, resource, key, acquisition.crates[index], acquisition.crates[index],
            candidate.crates()[index],
            [&](identity::CrateId handle) { return resource.crate(handle); })) {
      return zc::none;
    }
  }
  for (size_t index = 0; index < acquisition.sources.size(); ++index) {
    if (!verifyMaterializedIdentity<identity::SourceFileKey,
                                    incremental_binding_query::ActiveSourceMembershipQuery>(
            context, resource, key, acquisition.sources[index], acquisition.sources[index],
            candidate.sources()[index],
            [&](identity::SourceFileId handle) { return resource.sourceFile(handle); })) {
      return zc::none;
    }
  }
  for (size_t index = 0; index < acquisition.modules.size(); ++index) {
    if (!verifyMaterializedIdentity<identity::ModuleKey,
                                    incremental_binding_query::ActiveModuleMembershipQuery>(
            context, resource, key, acquisition.modules[index], acquisition.modules[index],
            candidate.modules()[index],
            [&](identity::ModuleId handle) { return resource.module(handle); })) {
      return zc::none;
    }
  }

  if (candidate.context() != resource.semanticContext() ||
      candidate.revision() != context.snapshotRevision() ||
      candidate.witness().contextRoots().encodeCanonical().asPtr() !=
          key.encodeCanonical().asPtr() ||
      candidate.witness().fingerprint().digest() != state.fingerprint.digest() ||
      candidate.witness().graph().encodeCanonical().asPtr() !=
          acquisition.graph.encodeCanonical().asPtr() ||
      candidate.witness().scc().encodeCanonical().asPtr() !=
          acquisition.scc.encodeCanonical().asPtr() ||
      candidate.witness().requestEdges().size() != state.stableEdges.size() ||
      candidate.requestEdges().size() != state.stableEdges.size() ||
      !verifierEdgesMatchGraph(acquisition.graph, state.stableEdges.asPtr())) {
    return zc::none;
  }
  for (size_t index = 0; index < state.stableEdges.size(); ++index) {
    if (!(candidate.witness().requestEdges()[index] == state.stableEdges[index])) {
      return zc::none;
    }
    const auto& handleEdge = candidate.requestEdges()[index];
    auto requester = verifierModuleForHandle(candidate.modules(), handleEdge.requester());
    auto dependency = verifierModuleForHandle(candidate.modules(), handleEdge.dependency());
    if (requester == zc::none || dependency == zc::none ||
        ZC_ASSERT_NONNULL(requester).key().encode().asPtr() !=
            state.stableEdges[index].requester().encode().asPtr() ||
        ZC_ASSERT_NONNULL(dependency).key().encode().asPtr() !=
            state.stableEdges[index].dependency().encode().asPtr() ||
        handleEdge.request().encode().asPtr() !=
            state.stableEdges[index].request().encode().asPtr()) {
      return zc::none;
    }
  }
  auto revision =
      computeVerifierGraphRevision(state.fingerprint, acquisition.graph, state.stableEdges.asPtr());
  if (revision == zc::none ||
      ZC_ASSERT_NONNULL(revision) != candidate.witness().graphRevision().digest()) {
    return zc::none;
  }
  return candidate.witness().encodeCanonical();
}

template <typename Handle>
query::TypedQueryResult<Handle> mapIdentityInternResult(
    identity::IdentityInternResult<Handle>&& result) {
  if (result.template is<Handle>()) {
    return query::TypedQueryResult<Handle>::value(result.template get<Handle>());
  }
  switch (result.template get<identity::IdentityInternerFailure>()) {
    case identity::IdentityInternerFailure::AllocationFailure:
    case identity::IdentityInternerFailure::SlotOverflow:
      return query::TypedQueryResult<Handle>::runtimeFailure(
          query::QueryRuntimeFailure::AllocationFailure);
    case identity::IdentityInternerFailure::ForeignBrand:
    case identity::IdentityInternerFailure::MalformedRecord:
    case identity::IdentityInternerFailure::CanonicalCollision:
      return query::TypedQueryResult<Handle>::runtimeFailure(
          query::QueryRuntimeFailure::InvariantViolation);
  }
  return query::TypedQueryResult<Handle>::runtimeFailure(
      query::QueryRuntimeFailure::InvariantViolation);
}

bool sameUnit(const identity::CompilationUnitIdentity& left,
              const identity::CompilationUnitIdentity& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

}  // namespace
}  // namespace zomlang::compiler

namespace zomlang::compiler::driver::module_graph_query {

struct StableMaterializedDependencyWitness::Impl final {
  Impl(identity::ModuleKey&& requester, identity::ModuleResolutionKey&& request,
       identity::ModuleKey&& dependency) noexcept
      : requester(zc::mv(requester)), request(zc::mv(request)), dependency(zc::mv(dependency)) {}

  identity::ModuleKey requester;
  identity::ModuleResolutionKey request;
  identity::ModuleKey dependency;
};

StableMaterializedDependencyWitness::StableMaterializedDependencyWitness(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

StableMaterializedDependencyWitness::~StableMaterializedDependencyWitness() noexcept(false) =
    default;
StableMaterializedDependencyWitness::StableMaterializedDependencyWitness(
    StableMaterializedDependencyWitness&&) noexcept = default;
StableMaterializedDependencyWitness& StableMaterializedDependencyWitness::operator=(
    StableMaterializedDependencyWitness&&) noexcept = default;

StableMaterializedDependencyWitness StableMaterializedDependencyWitness::clone() const {
  return StableMaterializedDependencyWitness(
      zc::heap<Impl>(impl->requester.clone(), impl->request.clone(), impl->dependency.clone()));
}

const identity::ModuleKey& StableMaterializedDependencyWitness::requester() const noexcept {
  return impl->requester;
}

const identity::ModuleResolutionKey& StableMaterializedDependencyWitness::request() const noexcept {
  return impl->request;
}

const identity::ModuleKey& StableMaterializedDependencyWitness::dependency() const noexcept {
  return impl->dependency;
}

zc::Maybe<StableMaterializedDependencyWitness> StableMaterializedDependencyWitness::from(
    identity::ModuleKey&& requester, identity::ModuleResolutionKey&& request,
    identity::ModuleKey&& dependency) {
  if (!sameModule(requester, request.requester())) { return zc::none; }
  StableMaterializedDependencyWitness result(
      zc::heap<Impl>(zc::mv(requester), zc::mv(request), zc::mv(dependency)));
  if (result.encodeCanonical().size() > kMaximumWitnessBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<StableMaterializedDependencyWitness> StableMaterializedDependencyWitness::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kDependencyWitnessDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto requesterBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  auto requestBytes = decoder.decodeByteString(kMaximumRequestBytes);
  auto dependencyBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  if (requesterBytes == zc::none || requestBytes == zc::none || dependencyBytes == zc::none ||
      !decoder.finished()) {
    return zc::none;
  }
  auto requester = decodeModule(ZC_ASSERT_NONNULL(requesterBytes).asPtr());
  auto request = decodeRequest(ZC_ASSERT_NONNULL(requestBytes).asPtr());
  auto dependency = decodeModule(ZC_ASSERT_NONNULL(dependencyBytes).asPtr());
  if (requester == zc::none || request == zc::none || dependency == zc::none) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(requester)), zc::mv(ZC_ASSERT_NONNULL(request)),
                     zc::mv(ZC_ASSERT_NONNULL(dependency)));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

zc::Array<uint8_t> StableMaterializedDependencyWitness::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  const auto requesterBytes = impl->requester.encode();
  const auto requestBytes = impl->request.encode();
  const auto dependencyBytes = impl->dependency.encode();
  payload.encodeByteString(requesterBytes.asPtr());
  payload.encodeByteString(requestBytes.asPtr());
  payload.encodeByteString(dependencyBytes.asPtr());
  return frame(kDependencyWitnessDomain, payload.finish().asPtr());
}

bool StableMaterializedDependencyWitness::operator==(
    const StableMaterializedDependencyWitness& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

template <typename Key, typename Record, typename Handle>
struct MaterializedIdentityEntry<Key, Record, Handle>::Impl final {
  Impl(Key&& key, Record&& record, Handle handle) noexcept
      : key(zc::mv(key)), record(zc::mv(record)), handle(handle) {}

  Key key;
  Record record;
  Handle handle;
};

template <typename Key, typename Record, typename Handle>
MaterializedIdentityEntry<Key, Record, Handle>::MaterializedIdentityEntry(
    zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

template <typename Key, typename Record, typename Handle>
MaterializedIdentityEntry<Key, Record, Handle>::~MaterializedIdentityEntry() noexcept(false) =
    default;

template <typename Key, typename Record, typename Handle>
MaterializedIdentityEntry<Key, Record, Handle>::MaterializedIdentityEntry(
    MaterializedIdentityEntry&&) noexcept = default;

template <typename Key, typename Record, typename Handle>
MaterializedIdentityEntry<Key, Record, Handle>&
MaterializedIdentityEntry<Key, Record, Handle>::operator=(MaterializedIdentityEntry&&) noexcept =
    default;

template <typename Key, typename Record, typename Handle>
MaterializedIdentityEntry<Key, Record, Handle>
MaterializedIdentityEntry<Key, Record, Handle>::fromVerified(Key&& key, Record&& record,
                                                             Handle handle) noexcept {
  return MaterializedIdentityEntry(zc::heap<Impl>(zc::mv(key), zc::mv(record), handle));
}

template <typename Key, typename Record, typename Handle>
MaterializedIdentityEntry<Key, Record, Handle>
MaterializedIdentityEntry<Key, Record, Handle>::clone() const {
  return MaterializedIdentityEntry(
      zc::heap<Impl>(impl->key.clone(), impl->record.clone(), impl->handle));
}

template <typename Key, typename Record, typename Handle>
const Key& MaterializedIdentityEntry<Key, Record, Handle>::key() const noexcept {
  return impl->key;
}

template <typename Key, typename Record, typename Handle>
const Record& MaterializedIdentityEntry<Key, Record, Handle>::record() const noexcept {
  return impl->record;
}

template <typename Key, typename Record, typename Handle>
Handle MaterializedIdentityEntry<Key, Record, Handle>::handle() const noexcept {
  return impl->handle;
}

template class MaterializedIdentityEntry<identity::CompilationUnitIdentity,
                                         identity::CompilationUnitIdentity,
                                         identity::CompilationUnitId>;
template class MaterializedIdentityEntry<identity::CrateKey, identity::CrateKey, identity::CrateId>;
template class MaterializedIdentityEntry<identity::SourceFileKey, identity::SourceFileKey,
                                         identity::SourceFileId>;
template class MaterializedIdentityEntry<identity::ModuleKey, identity::ModuleKey,
                                         identity::ModuleId>;

struct MaterializedModuleDependencyEdge::Impl final {
  Impl(identity::ModuleId requester, identity::ModuleResolutionKey&& request,
       identity::ModuleId dependency) noexcept
      : requester(requester), request(zc::mv(request)), dependency(dependency) {}

  identity::ModuleId requester;
  identity::ModuleResolutionKey request;
  identity::ModuleId dependency;
};

MaterializedModuleDependencyEdge::MaterializedModuleDependencyEdge(
    identity::ModuleId requester, identity::ModuleResolutionKey&& request,
    identity::ModuleId dependency) noexcept
    : impl(zc::heap<Impl>(requester, zc::mv(request), dependency)) {}

MaterializedModuleDependencyEdge::MaterializedModuleDependencyEdge(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

MaterializedModuleDependencyEdge::~MaterializedModuleDependencyEdge() noexcept(false) = default;
MaterializedModuleDependencyEdge::MaterializedModuleDependencyEdge(
    MaterializedModuleDependencyEdge&&) noexcept = default;
MaterializedModuleDependencyEdge& MaterializedModuleDependencyEdge::operator=(
    MaterializedModuleDependencyEdge&&) noexcept = default;

MaterializedModuleDependencyEdge MaterializedModuleDependencyEdge::clone() const {
  return MaterializedModuleDependencyEdge(
      zc::heap<Impl>(impl->requester, impl->request.clone(), impl->dependency));
}

identity::ModuleId MaterializedModuleDependencyEdge::requester() const noexcept {
  return impl->requester;
}

const identity::ModuleResolutionKey& MaterializedModuleDependencyEdge::request() const noexcept {
  return impl->request;
}

identity::ModuleId MaterializedModuleDependencyEdge::dependency() const noexcept {
  return impl->dependency;
}

struct MaterializedModuleGraphWitness::Impl final {
  Impl(incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
       identity::SemanticContextFingerprint&& fingerprint, ModuleGraphRecord&& graph,
       ModuleGraphSccRecord&& scc, zc::Vector<StableMaterializedDependencyWitness>&& requestEdges,
       binder::ModuleGraphRevision&& graphRevision) noexcept
      : contextRoots(zc::mv(contextRoots)),
        fingerprint(zc::mv(fingerprint)),
        graph(zc::mv(graph)),
        scc(zc::mv(scc)),
        requestEdges(zc::mv(requestEdges)),
        graphRevision(zc::mv(graphRevision)) {}

  incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  identity::SemanticContextFingerprint fingerprint;
  ModuleGraphRecord graph;
  ModuleGraphSccRecord scc;
  zc::Vector<StableMaterializedDependencyWitness> requestEdges;
  binder::ModuleGraphRevision graphRevision;
};

MaterializedModuleGraphWitness::MaterializedModuleGraphWitness(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

MaterializedModuleGraphWitness::~MaterializedModuleGraphWitness() noexcept(false) = default;
MaterializedModuleGraphWitness::MaterializedModuleGraphWitness(
    MaterializedModuleGraphWitness&&) noexcept = default;
MaterializedModuleGraphWitness& MaterializedModuleGraphWitness::operator=(
    MaterializedModuleGraphWitness&&) noexcept = default;

MaterializedModuleGraphWitness MaterializedModuleGraphWitness::clone() const {
  zc::Vector<StableMaterializedDependencyWitness> requestEdges;
  for (const auto& edge : impl->requestEdges) requestEdges.add(edge.clone());
  return MaterializedModuleGraphWitness(zc::heap<Impl>(
      impl->contextRoots.clone(), impl->fingerprint.clone(), impl->graph.clone(), impl->scc.clone(),
      zc::mv(requestEdges), binder::ModuleGraphRevision(impl->graphRevision)));
}

const incremental_binding_query::CompilationRootSetQueryKey&
MaterializedModuleGraphWitness::contextRoots() const noexcept {
  return impl->contextRoots;
}

const identity::SemanticContextFingerprint& MaterializedModuleGraphWitness::fingerprint()
    const noexcept {
  return impl->fingerprint;
}

const ModuleGraphRecord& MaterializedModuleGraphWitness::graph() const noexcept {
  return impl->graph;
}

const ModuleGraphSccRecord& MaterializedModuleGraphWitness::scc() const noexcept {
  return impl->scc;
}

zc::ArrayPtr<const StableMaterializedDependencyWitness>
MaterializedModuleGraphWitness::requestEdges() const noexcept {
  return impl->requestEdges.asPtr();
}

const binder::ModuleGraphRevision& MaterializedModuleGraphWitness::graphRevision() const noexcept {
  return impl->graphRevision;
}

zc::Maybe<MaterializedModuleGraphWitness> MaterializedModuleGraphWitness::from(
    incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
    identity::SemanticContextFingerprint&& fingerprint, ModuleGraphRecord&& graph,
    ModuleGraphSccRecord&& scc, zc::Vector<StableMaterializedDependencyWitness>&& requestEdges,
    binder::ModuleGraphRevision&& graphRevision) {
  if (requestEdges.size() > kMaximumRequestEdges) { return zc::none; }
  canonicalSort(requestEdges, [](const StableMaterializedDependencyWitness& edge) {
    return edge.encodeCanonical();
  });
  for (size_t index = 1; index < requestEdges.size(); ++index) {
    if (requestEdges[index - 1] == requestEdges[index]) { return zc::none; }
  }
  zc::Vector<ModuleGraphSccComponent> components(scc.components().size());
  for (const auto& component : scc.components()) { components.add(component.clone()); }
  auto verifiedScc = ModuleGraphSccRecord::fromVerified(graph, zc::mv(components));
  if (verifiedScc == zc::none ||
      ZC_ASSERT_NONNULL(verifiedScc).encodeCanonical().asPtr() != scc.encodeCanonical().asPtr() ||
      scc.hasCycle(graph) || !stableEdgesMatchGraph(graph, requestEdges.asPtr())) {
    return zc::none;
  }
  auto revision = computeGraphRevision(fingerprint, graph, requestEdges.asPtr());
  if (revision == zc::none || ZC_ASSERT_NONNULL(revision) != graphRevision.digest()) {
    return zc::none;
  }
  MaterializedModuleGraphWitness result(
      zc::heap<Impl>(zc::mv(contextRoots), zc::mv(fingerprint), zc::mv(graph), zc::mv(scc),
                     zc::mv(requestEdges), zc::mv(graphRevision)));
  if (result.encodeCanonical().size() > kMaximumWitnessBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<MaterializedModuleGraphWitness> MaterializedModuleGraphWitness::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kGraphWitnessDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto rootsBytes = decoder.decodeByteString(kMaximumWitnessBytes);
  auto fingerprintDigest = decoder.decodeDigest();
  auto graphBytes = decoder.decodeByteString(kMaximumWitnessBytes);
  auto sccBytes = decoder.decodeByteString(kMaximumWitnessBytes);
  auto edgeCount = decoder.decodeSequenceSize(kMaximumRequestEdges);
  if (rootsBytes == zc::none || fingerprintDigest == zc::none || graphBytes == zc::none ||
      sccBytes == zc::none || edgeCount == zc::none) {
    return zc::none;
  }
  zc::Vector<StableMaterializedDependencyWitness> edges(
      static_cast<size_t>(ZC_ASSERT_NONNULL(edgeCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(edgeCount); ++index) {
    auto edgeBytes = decoder.decodeByteString(kMaximumWitnessBytes);
    if (edgeBytes == zc::none) { return zc::none; }
    auto edge =
        StableMaterializedDependencyWitness::decodeCanonical(ZC_ASSERT_NONNULL(edgeBytes).asPtr());
    if (edge == zc::none) { return zc::none; }
    edges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
  }
  auto revisionDigest = decoder.decodeDigest();
  if (revisionDigest == zc::none || !decoder.finished()) { return zc::none; }
  auto roots = incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(
      ZC_ASSERT_NONNULL(rootsBytes).asPtr());
  auto graph = ModuleGraphRecord::decodeCanonical(ZC_ASSERT_NONNULL(graphBytes).asPtr());
  auto scc = ModuleGraphSccRecord::decodeCanonical(ZC_ASSERT_NONNULL(sccBytes).asPtr());
  if (roots == zc::none || graph == zc::none || scc == zc::none) { return zc::none; }
  auto result =
      from(zc::mv(ZC_ASSERT_NONNULL(roots)),
           identity::SemanticContextFingerprint::fromCanonicalDigest(
               ZC_ASSERT_NONNULL(fingerprintDigest)),
           zc::mv(ZC_ASSERT_NONNULL(graph)), zc::mv(ZC_ASSERT_NONNULL(scc)), zc::mv(edges),
           binder::ModuleGraphRevision::fromCanonicalDigest(ZC_ASSERT_NONNULL(revisionDigest)));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

zc::Array<uint8_t> MaterializedModuleGraphWitness::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  const auto rootsBytes = impl->contextRoots.encodeCanonical();
  const auto graphBytes = impl->graph.encodeCanonical();
  const auto sccBytes = impl->scc.encodeCanonical();
  payload.encodeByteString(rootsBytes.asPtr());
  payload.encodeDigest(impl->fingerprint.digest());
  payload.encodeByteString(graphBytes.asPtr());
  payload.encodeByteString(sccBytes.asPtr());
  payload.encodeSequenceSize(impl->requestEdges.size());
  for (const auto& edge : impl->requestEdges) {
    const auto edgeBytes = edge.encodeCanonical();
    payload.encodeByteString(edgeBytes.asPtr());
  }
  payload.encodeDigest(impl->graphRevision.digest());
  return frame(kGraphWitnessDomain, payload.finish().asPtr());
}

bool MaterializedModuleGraphWitness::sameAs(const MaterializedModuleGraphWitness& other) const {
  return encodeCanonical().asPtr() == other.encodeCanonical().asPtr();
}

struct MaterializedModuleGraph::Impl final {
  Impl(identity::SemanticContextBrand context, query::DatabaseRevision revision,
       MaterializedModuleGraphWitness&& witness,
       zc::Vector<MaterializedCompilationUnitEntry>&& units,
       zc::Vector<MaterializedCrateEntry>&& crates, zc::Vector<MaterializedSourceEntry>&& sources,
       zc::Vector<MaterializedModuleEntry>&& modules,
       zc::Vector<MaterializedModuleDependencyEdge>&& requestEdges) noexcept
      : context(context),
        revision(revision),
        witness(zc::mv(witness)),
        units(zc::mv(units)),
        crates(zc::mv(crates)),
        sources(zc::mv(sources)),
        modules(zc::mv(modules)),
        requestEdges(zc::mv(requestEdges)) {}

  identity::SemanticContextBrand context;
  query::DatabaseRevision revision;
  MaterializedModuleGraphWitness witness;
  zc::Vector<MaterializedCompilationUnitEntry> units;
  zc::Vector<MaterializedCrateEntry> crates;
  zc::Vector<MaterializedSourceEntry> sources;
  zc::Vector<MaterializedModuleEntry> modules;
  zc::Vector<MaterializedModuleDependencyEdge> requestEdges;
};

MaterializedModuleGraph::MaterializedModuleGraph(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}

MaterializedModuleGraph::~MaterializedModuleGraph() noexcept(false) = default;
MaterializedModuleGraph::MaterializedModuleGraph(MaterializedModuleGraph&&) noexcept = default;
MaterializedModuleGraph& MaterializedModuleGraph::operator=(MaterializedModuleGraph&&) noexcept =
    default;

zc::Maybe<MaterializedModuleGraph> MaterializedModuleGraph::from(
    identity::SemanticContextBrand context, query::DatabaseRevision revision,
    MaterializedModuleGraphWitness&& witness, zc::Vector<MaterializedCompilationUnitEntry>&& units,
    zc::Vector<MaterializedCrateEntry>&& crates, zc::Vector<MaterializedSourceEntry>&& sources,
    zc::Vector<MaterializedModuleEntry>&& modules,
    zc::Vector<MaterializedModuleDependencyEdge>&& requestEdges) {
  if (!context.isValid() || revision.value() == 0 ||
      !entriesAreCanonical<MaterializedCompilationUnitEntry>(units.asPtr(), context) ||
      !entriesAreCanonical<MaterializedCrateEntry>(crates.asPtr(), context) ||
      !entriesAreCanonical<MaterializedSourceEntry>(sources.asPtr(), context) ||
      !entriesAreCanonical<MaterializedModuleEntry>(modules.asPtr(), context) ||
      !rootsMatchMaterializedEntries(witness.contextRoots(), units.asPtr(), crates.asPtr()) ||
      modules.size() != witness.graph().modules().size() ||
      requestEdges.size() != witness.requestEdges().size()) {
    return zc::none;
  }
  for (size_t index = 0; index < modules.size(); ++index) {
    if (!sameModule(modules[index].key(), witness.graph().modules()[index]) ||
        !containsEntryKey<MaterializedCrateEntry>(crates.asPtr(), modules[index].key().crate())) {
      return zc::none;
    }
  }
  for (const auto& crate : crates) {
    if (!containsEntryKey<MaterializedCompilationUnitEntry>(units.asPtr(), crate.key().unit())) {
      return zc::none;
    }
  }
  for (const auto& source : sources) {
    if (!containsEntryKey<MaterializedCrateEntry>(crates.asPtr(), source.key().crate())) {
      return zc::none;
    }
  }
  for (size_t index = 0; index < requestEdges.size(); ++index) {
    const auto& edge = requestEdges[index];
    if (!edge.requester().belongsTo(context) || !edge.dependency().belongsTo(context)) {
      return zc::none;
    }
    auto requester = moduleEntryFor(modules.asPtr(), edge.requester());
    auto dependency = moduleEntryFor(modules.asPtr(), edge.dependency());
    const auto& stable = witness.requestEdges()[index];
    if (requester == zc::none || dependency == zc::none ||
        !sameModule(ZC_ASSERT_NONNULL(requester).key(), stable.requester()) ||
        !sameModule(ZC_ASSERT_NONNULL(dependency).key(), stable.dependency()) ||
        edge.request().encode().asPtr() != stable.request().encode().asPtr()) {
      return zc::none;
    }
  }
  return MaterializedModuleGraph(zc::heap<Impl>(context, revision, zc::mv(witness), zc::mv(units),
                                                zc::mv(crates), zc::mv(sources), zc::mv(modules),
                                                zc::mv(requestEdges)));
}

MaterializedModuleGraph MaterializedModuleGraph::clone() const {
  zc::Vector<MaterializedCompilationUnitEntry> units;
  for (const auto& entry : impl->units) units.add(entry.clone());
  zc::Vector<MaterializedCrateEntry> crates;
  for (const auto& entry : impl->crates) crates.add(entry.clone());
  zc::Vector<MaterializedSourceEntry> sources;
  for (const auto& entry : impl->sources) sources.add(entry.clone());
  zc::Vector<MaterializedModuleEntry> modules;
  for (const auto& entry : impl->modules) modules.add(entry.clone());
  zc::Vector<MaterializedModuleDependencyEdge> requestEdges;
  for (const auto& edge : impl->requestEdges) requestEdges.add(edge.clone());
  return MaterializedModuleGraph(
      zc::heap<Impl>(impl->context, impl->revision, impl->witness.clone(), zc::mv(units),
                     zc::mv(crates), zc::mv(sources), zc::mv(modules), zc::mv(requestEdges)));
}

identity::SemanticContextBrand MaterializedModuleGraph::context() const noexcept {
  return impl->context;
}

query::DatabaseRevision MaterializedModuleGraph::revision() const noexcept {
  return impl->revision;
}

const MaterializedModuleGraphWitness& MaterializedModuleGraph::witness() const noexcept {
  return impl->witness;
}

zc::ArrayPtr<const MaterializedCompilationUnitEntry> MaterializedModuleGraph::units()
    const noexcept {
  return impl->units.asPtr();
}

zc::ArrayPtr<const MaterializedCrateEntry> MaterializedModuleGraph::crates() const noexcept {
  return impl->crates.asPtr();
}

zc::ArrayPtr<const MaterializedSourceEntry> MaterializedModuleGraph::sources() const noexcept {
  return impl->sources.asPtr();
}

zc::ArrayPtr<const MaterializedModuleEntry> MaterializedModuleGraph::modules() const noexcept {
  return impl->modules.asPtr();
}

zc::ArrayPtr<const MaterializedModuleDependencyEdge> MaterializedModuleGraph::requestEdges()
    const noexcept {
  return impl->requestEdges.asPtr();
}

zc::Array<uint8_t> MaterializeModuleGraphQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<MaterializeModuleGraphQuery::Key> MaterializeModuleGraphQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<MaterializeModuleGraphQuery> MaterializeModuleGraphQuery::provide(
    query::CapabilityQueryContext<MaterializeModuleGraphQuery>& context, const Key& key) {
  auto acquisition = acquireProviderContext(context, key);
  if (acquisition.isRuntimeFailure()) {
    return query::CapabilityProviderResult<MaterializeModuleGraphQuery>::runtimeRejected(
        acquisition.runtimeFailure());
  }
  auto resolved = acquireProviderModules(context, acquisition.value().clone());
  if (resolved.is<MaterializerProviderResult>()) {
    return zc::mv(resolved).get<MaterializerProviderResult>();
  }
  auto materialized =
      materializeProviderIdentities(context, zc::mv(resolved).get<ResolvedProviderAcquisition>());
  if (materialized.is<MaterializerProviderResult>()) {
    return zc::mv(materialized).get<MaterializerProviderResult>();
  }
  return publishProviderCandidate(context,
                                  zc::mv(materialized).get<MaterializedProviderAcquisition>());
}

zc::Maybe<zc::Array<uint8_t>> MaterializeModuleGraphQuery::verify(
    query::CapabilityQueryContext<MaterializeModuleGraphQuery>& context, const Key& key,
    const Capability& candidate) {
  auto contextState = acquireVerifierContext(context, key);
  if (contextState == zc::none) { return zc::none; }
  auto moduleState = acquireVerifierModules(context, zc::mv(ZC_ASSERT_NONNULL(contextState)));
  if (moduleState == zc::none) { return zc::none; }
  return verifyMaterializedCandidate(context, key, candidate,
                                     zc::mv(ZC_ASSERT_NONNULL(moduleState)));
}

}  // namespace zomlang::compiler::driver::module_graph_query

namespace zomlang::compiler::query {

using MaterializeModuleGraphDescriptor = driver::module_graph_query::MaterializeModuleGraphQuery;

StableWitnessBytes CapabilityCandidateContract<MaterializeModuleGraphDescriptor>::encode(
    const MaterializeModuleGraphDescriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.witness().encodeCanonical());
}

zc::Maybe<zc::Own<MaterializeModuleGraphDescriptor::Capability>>
CapabilityCandidateContract<MaterializeModuleGraphDescriptor>::decode(zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

zc::Array<uint8_t> CapabilityFailureContract<
    MaterializeModuleGraphDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<MaterializeModuleGraphDescriptor,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<
    MaterializeModuleGraphDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);
  if (facts == zc::none) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

CapabilityRejectionCheck CapabilityFailureContract<MaterializeModuleGraphDescriptor,
                                                   SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<MaterializeModuleGraphDescriptor>& context,
           const MaterializeModuleGraphDescriptor::Key& key, const Sequence& diagnostics) {
  return verifyMaterializerRejection(
      context, key,
      [&](zc::ArrayPtr<const diagnostics::DiagnosticFact> actual) {
        auto actualBytes = binder::encodeStableBindingDiagnosticFacts(actual);
        auto expectedBytes = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
        return actualBytes != zc::none && expectedBytes != zc::none &&
                       ZC_ASSERT_NONNULL(actualBytes).asPtr() ==
                           ZC_ASSERT_NONNULL(expectedBytes).asPtr()
                   ? CapabilityRejectionCheck::Verified
                   : CapabilityRejectionCheck::Rejected;
      },
      [&](const binder::BinderKeyFailure&) { return CapabilityRejectionCheck::Rejected; });
}

zc::Array<uint8_t> CapabilityFailureContract<
    MaterializeModuleGraphDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::encode(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> CapabilityFailureContract<
    MaterializeModuleGraphDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

CapabilityRejectionCheck CapabilityFailureContract<MaterializeModuleGraphDescriptor,
                                                   KeyRejection<binder::BinderKeyFailure>>::
    verify(CapabilityQueryContext<MaterializeModuleGraphDescriptor>& context,
           const MaterializeModuleGraphDescriptor::Key& key,
           const binder::BinderKeyFailure& failure) {
  return verifyMaterializerRejection(
      context, key,
      [&](zc::ArrayPtr<const diagnostics::DiagnosticFact>) {
        return CapabilityRejectionCheck::Rejected;
      },
      [&](const binder::BinderKeyFailure& actual) {
        return actual == failure ? CapabilityRejectionCheck::Verified
                                 : CapabilityRejectionCheck::Rejected;
      });
}

TypedQueryResult<identity::CompilationUnitId>
ActiveMaterialization<identity::CompilationUnitIdentity>::materialize(
    const Resource& resources, const identity::CompilationUnitIdentity& key, const Record& record) {
  if (!sameUnit(key, record.unit())) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  const auto context = resources.semanticContext();
  if (!context.isValid()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  return mapIdentityInternResult(resources.internCompilationUnit(context, key));
}

TypedQueryResult<identity::CrateId> ActiveMaterialization<identity::CrateKey>::materialize(
    const Resource& resources, const identity::CrateKey& key, const Record& record) {
  if (key.encode().asPtr() != record.encode().asPtr()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  const auto context = resources.semanticContext();
  if (!context.isValid()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  return mapIdentityInternResult(resources.internCrate(context, key));
}

TypedQueryResult<identity::SourceFileId>
ActiveMaterialization<identity::SourceFileKey>::materialize(const Resource& resources,
                                                            const identity::SourceFileKey& key,
                                                            const Record& record) {
  if (key.encode().asPtr() != record.encode().asPtr()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  const auto context = resources.semanticContext();
  if (!context.isValid()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  return mapIdentityInternResult(resources.internSourceFile(context, key));
}

TypedQueryResult<identity::ModuleId> ActiveMaterialization<identity::ModuleKey>::materialize(
    const Resource& resources, const identity::ModuleKey& key, const Record& record) {
  if (key.encode().asPtr() != record.encode().asPtr()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  const auto context = resources.semanticContext();
  if (!context.isValid()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  return mapIdentityInternResult(resources.internModule(context, key));
}

}  // namespace zomlang::compiler::query

namespace {

#define ZOM_M1_KEY_CompilationRootSetQueryKey \
  zomlang::compiler::driver::incremental_binding_query::CompilationRootSetQueryKey
#define ZOM_M1_CAPABILITY_SELECT_M1(name, domainLiteral, keyType, capabilityType)                \
  static_assert(zomlang::compiler::query::CapabilityQueryDescriptor<                             \
                zomlang::compiler::driver::module_graph_query::name##Query>);                    \
  static_assert(zc::isSameType<zomlang::compiler::driver::module_graph_query::name##Query::Key,  \
                               ZOM_M1_KEY_##keyType>());                                         \
  static_assert(                                                                                 \
      zc::isSameType<zomlang::compiler::driver::module_graph_query::name##Query::Capability,     \
                     zomlang::compiler::driver::module_graph_query::capabilityType>());          \
  static_assert(zc::isSameType<                                                                  \
                zomlang::compiler::driver::module_graph_query::name##Query::FailureAlternatives, \
                zomlang::compiler::query::CapabilityFailureList<                                 \
                    zomlang::compiler::query::SourceRejection<                                   \
                        zomlang::compiler::diagnostics::DiagnosticFact>,                         \
                    zomlang::compiler::query::KeyRejection<                                      \
                        zomlang::compiler::binder::BinderKeyFailure>>>());                       \
  static_assert(zomlang::compiler::driver::module_graph_query::name##Query::descriptor.domain == \
                domainLiteral##_zcc);                                                            \
  static_assert(                                                                                 \
      zomlang::compiler::driver::module_graph_query::name##Query::descriptor.retention ==        \
      zomlang::compiler::query::RetentionClass::Retained);                                       \
  static_assert(zomlang::compiler::driver::module_graph_query::name##Query::descriptor.cycle ==  \
                zomlang::compiler::query::QueryCyclePolicy::Reject);                             \
  static_assert(zomlang::compiler::driver::module_graph_query::name##Query::descriptor.cost ==   \
                zomlang::compiler::query::QueryCostClass::Linear);                               \
  static_assert(                                                                                 \
      zomlang::compiler::driver::module_graph_query::name##Query::descriptor.admission ==        \
      zomlang::compiler::query::CapabilityAdmission::FinalSealedSnapshot)
#define ZOM_M1_CAPABILITY_SELECT_R28_16A(name, domain, keyType, capabilityType)
#define ZOM_M1_CAPABILITY_SELECT_M2(name, domain, keyType, capabilityType)
#define ZOM_M1_CAPABILITY_SELECT_M3(name, domain, keyType, capabilityType)
#define ZOM_M1_CAPABILITY_SELECT_M5(name, domain, keyType, capabilityType)
#define ZOM_M1_CAPABILITY_SELECT(task, name, domain, keyType, capabilityType) \
  ZOM_M1_CAPABILITY_SELECT_EXPAND(task, name, domain, keyType, capabilityType)
#define ZOM_M1_CAPABILITY_SELECT_EXPAND(task, name, domain, keyType, capabilityType) \
  ZOM_M1_CAPABILITY_SELECT_##task(name, domain, keyType, capabilityType)
#define ZOM_STABLE_BINDING_CAPABILITY_QUERY(                                                    \
    name, domain, keyType, resultType, capabilityType, producer, verifier, failureAlternatives, \
    descriptorTask, providerTask, verifierTask, testTask, mutations, test)                      \
  ZOM_M1_CAPABILITY_SELECT(descriptorTask, name, domain, keyType, capabilityType);
#include "zomlang/compiler/binder/stable-binding-schema.def"
#undef ZOM_STABLE_BINDING_CAPABILITY_QUERY
#undef ZOM_M1_CAPABILITY_SELECT_EXPAND
#undef ZOM_M1_CAPABILITY_SELECT
#undef ZOM_M1_CAPABILITY_SELECT_M5
#undef ZOM_M1_CAPABILITY_SELECT_M3
#undef ZOM_M1_CAPABILITY_SELECT_M2
#undef ZOM_M1_CAPABILITY_SELECT_R28_16A
#undef ZOM_M1_CAPABILITY_SELECT_M1
#undef ZOM_M1_KEY_CompilationRootSetQueryKey

#define ZOM_M1_PERMISSION_MaterializeModuleGraph(globalKey, membership)                     \
  static_assert(zomlang::compiler::query::ActiveMaterializerPermission<                     \
                zomlang::compiler::driver::module_graph_query::MaterializeModuleGraphQuery, \
                zomlang::compiler::identity::globalKey,                                     \
                zomlang::compiler::driver::incremental_binding_query::membership##Query>::allowed)
#define ZOM_M1_PERMISSION_MaterializeModuleSkeleton(globalKey, membership)
#define ZOM_M1_PERMISSION_MaterializeOwnerBody(globalKey, membership)
#define ZOM_M1_PERMISSION_SELECT(descriptor, globalKey, membership) \
  ZOM_M1_PERMISSION_SELECT_EXPAND(descriptor, globalKey, membership)
#define ZOM_M1_PERMISSION_SELECT_EXPAND(descriptor, globalKey, membership) \
  ZOM_M1_PERMISSION_##descriptor(globalKey, membership)
#define ZOM_STABLE_BINDING_MATERIALIZER_PERMISSION(descriptor, globalKey, membership) \
  ZOM_M1_PERMISSION_SELECT(descriptor, globalKey, membership);
#define ZOM_STABLE_BINDING_CAPABILITY_QUERY(                                                    \
    name, domain, keyType, resultType, capabilityType, producer, verifier, failureAlternatives, \
    descriptorTask, providerTask, verifierTask, testTask, mutations, test)
#include "zomlang/compiler/binder/stable-binding-schema.def"
#undef ZOM_STABLE_BINDING_CAPABILITY_QUERY
#undef ZOM_STABLE_BINDING_MATERIALIZER_PERMISSION
#undef ZOM_M1_PERMISSION_SELECT_EXPAND
#undef ZOM_M1_PERMISSION_SELECT
#undef ZOM_M1_PERMISSION_MaterializeOwnerBody
#undef ZOM_M1_PERMISSION_MaterializeModuleSkeleton
#undef ZOM_M1_PERMISSION_MaterializeModuleGraph

}  // namespace
