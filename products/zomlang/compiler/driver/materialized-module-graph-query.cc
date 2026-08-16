// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/materialized-module-graph-query.h"

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/binder/module-binding-allocation-plan.h"
#include "zomlang/compiler/binder/module-skeleton-query.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/binder/stable-binding-diagnostic-fact.h"
#include "zomlang/compiler/driver/core/query.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/module-dependency-provenance-query.h"
#include "zomlang/compiler/driver/named-item-query.h"
#include "zomlang/compiler/driver/owner-body-query.h"
#include "zomlang/compiler/identity/canonical/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/parser/parse-source-query.h"

namespace zomlang::compiler {
namespace {

constexpr zc::StringPtr kDependencyWitnessDomain = "zom.materialized-module-dependency-witness"_zc;
constexpr zc::StringPtr kGraphWitnessDomain = "zom.materialized-module-graph-witness"_zc;
constexpr zc::StringPtr kSkeletonWitnessDomain = "zom.materialized-module-skeleton-witness"_zc;
constexpr zc::StringPtr kOwnerBodyWitnessDomain = "zom.materialized-owner-body-witness"_zc;
constexpr zc::StringPtr kVerifiedBoundModuleWitnessDomain = "zom.verified-bound-module-witness"_zc;
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

bool sameScopeIdentities(zc::ArrayPtr<const binder::ScopeId> left,
                         zc::ArrayPtr<const binder::ScopeId> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
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
    const identity::ContextFingerprint& fingerprint,
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
      ProviderAcquisition&& acquisition, identity::ContextFingerprint&& fingerprint,
      zc::Vector<driver::module_graph_query::StableMaterializedDependencyWitness>&&
          requestEdges) noexcept
      : acquisition(zc::mv(acquisition)),
        fingerprint(zc::mv(fingerprint)),
        requestEdges(zc::mv(requestEdges)) {}

  ProviderAcquisition acquisition;
  identity::ContextFingerprint fingerprint;
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

zc::Maybe<identity::ContextFingerprint> computeProviderFingerprint(
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
  return identity::ContextFingerprint::fromCanonicalDigest(ZC_ASSERT_NONNULL(digest));
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
  if (left.size() > right.size()) { return 1; }
  return 0;
}

void verifierAppendUint64(zc::Vector<uint8_t>& bytes, uint64_t value) {
  for (uint32_t index = 0; index < 8; ++index) {
    const uint32_t shift = 56 - index * 8;
    bytes.add(static_cast<uint8_t>((value >> shift) & 0xffu));
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
  canonicalSort(crates, [](const identity::CrateKey& crate) { return crate.encode(); });
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
  canonicalSort(units, [](const identity::CompilationUnitIdentity& unit) { return unit.encode(); });

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
  canonicalSort(sources, [](const identity::SourceFileKey& source) { return source.encode(); });
  canonicalSort(modules, [](const identity::ModuleKey& module) { return module.encode(); });

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
  canonicalSort(encoded, [](const zc::Array<uint8_t>& value) { return value.asPtr(); });
  for (size_t index = 1; index < encoded.size(); ++index) {
    if (encoded[index - 1].asPtr() == encoded[index].asPtr()) { return false; }
  }
  verifierAppendUint64(bytes, encoded.size());
  for (const auto& value : encoded) { bytes.addAll(value.asPtr()); }
  return true;
}

zc::Maybe<identity::ContextFingerprint> computeVerifierFingerprint(
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
  return identity::ContextFingerprint::fromCanonicalDigest(ZC_ASSERT_NONNULL(digest));
}

struct VerifierModuleAcquisition final {
  VerifierModuleAcquisition(
      VerifierContextAcquisition&& acquisition, identity::ContextFingerprint&& fingerprint,
      zc::Vector<driver::module_graph_query::StableMaterializedDependencyWitness>&&
          stableEdges) noexcept
      : acquisition(zc::mv(acquisition)),
        fingerprint(zc::mv(fingerprint)),
        stableEdges(zc::mv(stableEdges)) {}

  VerifierContextAcquisition acquisition;
  identity::ContextFingerprint fingerprint;
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
  canonicalSort(stableEdges, [](const StableMaterializedDependencyWitness& edge) {
    return edge.encodeCanonical();
  });
  for (size_t index = 1; index < stableEdges.size(); ++index) {
    if (stableEdges[index - 1] == stableEdges[index]) { return zc::none; }
  }

  zc::Vector<identity::SourceFileKey> orderedSources(moduleSources.size());
  for (const auto& source : moduleSources) { orderedSources.add(source.clone()); }
  canonicalSort(orderedSources,
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
    const identity::ContextFingerprint& fingerprint,
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
       identity::ContextFingerprint&& fingerprint, ModuleGraphRecord&& graph,
       ModuleGraphSccRecord&& scc, zc::Vector<StableMaterializedDependencyWitness>&& requestEdges,
       binder::ModuleGraphRevision&& graphRevision) noexcept
      : contextRoots(zc::mv(contextRoots)),
        fingerprint(zc::mv(fingerprint)),
        graph(zc::mv(graph)),
        scc(zc::mv(scc)),
        requestEdges(zc::mv(requestEdges)),
        graphRevision(zc::mv(graphRevision)) {}

  incremental_binding_query::CompilationRootSetQueryKey contextRoots;
  identity::ContextFingerprint fingerprint;
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

const identity::ContextFingerprint& MaterializedModuleGraphWitness::fingerprint()
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
    identity::ContextFingerprint&& fingerprint, ModuleGraphRecord&& graph,
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
           identity::ContextFingerprint::fromCanonicalDigest(
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

namespace {

bool sameSkeletonModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

template <typename Entry>
bool sameIdentityEntries(zc::ArrayPtr<const Entry> left, zc::ArrayPtr<const Entry> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].key().encode().asPtr() != right[index].key().encode().asPtr() ||
        left[index].record().encode().asPtr() != right[index].record().encode().asPtr() ||
        left[index].handle() != right[index].handle()) {
      return false;
    }
  }
  return true;
}

bool sameSkeletonIdentities(const binder::MaterializedModuleSkeletonIdentities& left,
                            const binder::MaterializedModuleSkeletonIdentities& right) {
  return left.context() == right.context() && left.revision() == right.revision() &&
         left.fingerprint().digest() == right.fingerprint().digest() &&
         left.module() == right.module() && left.stableWitness() == right.stableWitness() &&
         sameIdentityEntries(left.definitions(), right.definitions()) &&
         sameIdentityEntries(left.implementations(), right.implementations()) &&
         sameIdentityEntries(left.genericParameters(), right.genericParameters()) &&
         sameIdentityEntries(left.callableParameters(), right.callableParameters());
}

bool graphContainsModule(const MaterializedModuleGraph& graph, const identity::ModuleKey& module) {
  for (const auto& entry : graph.modules()) {
    if (sameSkeletonModule(entry.key(), module)) { return true; }
  }
  return false;
}

zc::Maybe<MaterializedCompilationUnitEntry> materializedSkeletonCompilationUnit(
    const MaterializedModuleGraph& graph, const identity::ModuleKey& module) {
  for (const auto& crate : graph.crates()) {
    if (crate.key().encode().asPtr() != module.crate().encode().asPtr()) { continue; }
    for (const auto& unit : graph.units()) {
      if (unit.key().encode().asPtr() == crate.key().unit().encode().asPtr()) {
        return unit.clone();
      }
    }
    return zc::none;
  }
  return zc::none;
}

zc::Maybe<binder::BinderKeyFailure> missingSelectedModuleSourceFailure(
    const identity::ModuleKey& module) {
  zc::Maybe<binder::LocalSyntaxPath> noPath;
  return binder::BinderKeyFailure::from(binder::BinderKeyFailureKind::MissingSelectedModuleSource,
                                        binder::BinderQueryOwner::module(module.clone()),
                                        zc::mv(noPath));
}

template <typename Context, typename Descriptor, typename Expected>
bool activeMembershipMatches(Context& context, const typename Descriptor::Key& key,
                             Expected expected) {
  auto membership = context.template get<Descriptor>(key.clone());
  return !membership.isRuntimeFailure() && membership.kind() == query::QueryValueKind::Value &&
         membership.value().isActive() && expected(membership.value().record());
}

template <typename Context>
bool skeletonMembershipsMatch(Context& context,
                              const incremental_binding_query::ContextualModuleKey& key,
                              const binder::BoundModuleSkeleton& skeleton) {
  for (const auto& declaration : skeleton.declarations().values()) {
    auto membershipKey = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(), declaration.queryKey().clone());
    if (!activeMembershipMatches<Context,
                                 incremental_binding_query::ActiveDefinitionMembershipQuery>(
            context, membershipKey, [&](const identity::DefinitionIdentityRecord& record) {
              return record.encode().asPtr() == declaration.record().encode().asPtr();
            })) {
      return false;
    }
  }
  for (const auto& occurrence : skeleton.implementationOccurrences().values()) {
    auto membershipKey = incremental_binding_query::ContextualImplementationKey::from(
        key.contextRoots().clone(), occurrence.authority().clone());
    if (!activeMembershipMatches<Context,
                                 incremental_binding_query::ActiveImplementationMembershipQuery>(
            context, membershipKey,
            [&](const incremental_binding_query::ActiveImplementationMembershipRecord& record) {
              if (record.queryKey() != occurrence.authority() ||
                  record.record().encode().asPtr() != occurrence.record().encode().asPtr()) {
                return false;
              }
              for (const auto& equal : record.equalOccurrences()) {
                if (equal == occurrence.occurrence()) { return true; }
              }
              return false;
            })) {
      return false;
    }
  }
  for (const auto& parameter : skeleton.genericParameterDeclarations().values()) {
    auto membershipKey = incremental_binding_query::ContextualGenericParameterKey::from(
        key.contextRoots().clone(), parameter.queryKey().clone());
    if (!activeMembershipMatches<Context,
                                 incremental_binding_query::ActiveGenericParameterMembershipQuery>(
            context, membershipKey,
            [&](const incremental_binding_query::ActiveGenericParameterMembership& record) {
              return record.queryKey() == parameter.queryKey() &&
                     record.record().encode().asPtr() == parameter.record().encode().asPtr();
            })) {
      return false;
    }
  }
  for (const auto& parameter : skeleton.callableParameterDeclarations().values()) {
    auto membershipKey = incremental_binding_query::ContextualCallableParameterKey::from(
        key.contextRoots().clone(), parameter.queryKey().clone());
    if (!activeMembershipMatches<Context,
                                 incremental_binding_query::ActiveCallableParameterMembershipQuery>(
            context, membershipKey,
            [&](const incremental_binding_query::ActiveCallableParameterMembershipRecord& record) {
              return record.queryKey() == parameter.queryKey() &&
                     record.record().encode().asPtr() == parameter.record().encode().asPtr();
            })) {
      return false;
    }
  }
  return true;
}

using SkeletonProviderResult = query::CapabilityProviderResult<MaterializeModuleSkeletonQuery>;

template <typename SourceDescriptor>
SkeletonProviderResult forwardSkeletonSourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  using TargetContract =
      query::CapabilityFailureContract<MaterializeModuleSkeletonQuery,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto diagnostics = TargetContract::decode(SourceContract::encode(source.diagnostics()).asPtr());
  if (diagnostics == zc::none) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  return SkeletonProviderResult::sourceRejected<diagnostics::DiagnosticFact>(
      zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename SourceDescriptor>
SkeletonProviderResult forwardSkeletonKeyRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  return SkeletonProviderResult::keyRejected<binder::BinderKeyFailure>(source.keyFailure().clone());
}

zc::Maybe<const MaterializedModuleEntry&> materializedModuleForHandle(
    const MaterializedModuleGraph& graph, identity::ModuleId handle) {
  zc::Maybe<const MaterializedModuleEntry&> result;
  for (const auto& entry : graph.modules()) {
    if (entry.handle() != handle) { continue; }
    if (result != zc::none) { return zc::none; }
    result = entry;
  }
  return result;
}

using DependencySkeletonAcquisition =
    zc::OneOf<zc::Vector<MaterializedModuleSkeleton::DependencySkeletonLease>,
              SkeletonProviderResult>;

bool containsDependencySkeleton(
    zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease> dependencies,
    const identity::ModuleKey& module) {
  for (const auto& dependency : dependencies) {
    if (sameSkeletonModule(dependency.capability().module(), module)) { return true; }
  }
  return false;
}

DependencySkeletonAcquisition acquireDependencySkeletons(
    query::CapabilityQueryContext<MaterializeModuleSkeletonQuery>& context,
    const incremental_binding_query::ContextualModuleKey& key,
    const MaterializedModuleGraph& graph) {
  zc::Vector<MaterializedModuleSkeleton::DependencySkeletonLease> result;
  for (const auto& edge : graph.requestEdges()) {
    auto requester = materializedModuleForHandle(graph, edge.requester());
    auto dependency = materializedModuleForHandle(graph, edge.dependency());
    if (requester == zc::none || dependency == zc::none) {
      return SkeletonProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    if (!sameSkeletonModule(ZC_ASSERT_NONNULL(requester).key(), key.module())) { continue; }
    if (ZC_ASSERT_NONNULL(dependency).key().encode().asPtr() == key.module().encode().asPtr()) {
      return SkeletonProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    if (containsDependencySkeleton(result.asPtr(), ZC_ASSERT_NONNULL(dependency).key())) {
      continue;
    }
    auto dependencyKey = incremental_binding_query::ContextualModuleKey::from(
        key.contextRoots().clone(), ZC_ASSERT_NONNULL(dependency).key().clone());
    auto materialized =
        context.getCapability<MaterializeModuleSkeletonQuery>(zc::mv(dependencyKey));
    if (materialized.isSourceRejected()) { return forwardSkeletonSourceRejection(materialized); }
    if (materialized.isKeyRejected()) { return forwardSkeletonKeyRejection(materialized); }
    if (materialized.isRuntimeRejected()) {
      return SkeletonProviderResult::runtimeRejected(materialized.runtimeFailure());
    }
    if (!materialized.isPublished()) {
      return SkeletonProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    result.add(zc::mv(materialized).takeLease());
  }
  return result;
}

zc::Maybe<zc::Vector<MaterializedModuleSkeleton::DependencySkeletonLease>>
verifyDependencySkeletons(query::CapabilityQueryContext<MaterializeModuleSkeletonQuery>& context,
                          const incremental_binding_query::ContextualModuleKey& key,
                          const MaterializedModuleGraph& graph) {
  zc::Vector<MaterializedModuleSkeleton::DependencySkeletonLease> result;
  for (const auto& edge : graph.requestEdges()) {
    auto requester = materializedModuleForHandle(graph, edge.requester());
    auto dependency = materializedModuleForHandle(graph, edge.dependency());
    if (requester == zc::none || dependency == zc::none ||
        (ZC_ASSERT_NONNULL(requester).key().encode().asPtr() == key.module().encode().asPtr() &&
         ZC_ASSERT_NONNULL(dependency).key().encode().asPtr() == key.module().encode().asPtr())) {
      return zc::none;
    }
    if (!sameSkeletonModule(ZC_ASSERT_NONNULL(requester).key(), key.module())) { continue; }
    if (containsDependencySkeleton(result.asPtr(), ZC_ASSERT_NONNULL(dependency).key())) {
      continue;
    }
    auto dependencyKey = incremental_binding_query::ContextualModuleKey::from(
        key.contextRoots().clone(), ZC_ASSERT_NONNULL(dependency).key().clone());
    auto materialized =
        context.getCapability<MaterializeModuleSkeletonQuery>(zc::mv(dependencyKey));
    if (!materialized.isPublished()) { return zc::none; }
    result.add(zc::mv(materialized).takeLease());
  }
  return result;
}

zc::Maybe<binder::BoundModuleSkeleton> acquireSkeleton(
    query::CapabilityQueryContext<MaterializeModuleSkeletonQuery>& context,
    const incremental_binding_query::ContextualModuleKey& key,
    zc::Maybe<SkeletonProviderResult>& rejection) {
  auto result = context.get<binder::BindModuleSkeleton>(key.module().clone());
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  const auto& value = result.value().storage();
  if (value.is<binder::BinderSourceRejected>()) {
    auto encoded = binder::encodeStableBindingDiagnosticFacts(
        value.get<binder::BinderSourceRejected>().diagnostics.values());
    if (encoded == zc::none) { return zc::none; }
    auto diagnostics =
        query::CapabilityFailureContract<MaterializeModuleGraphQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>::
            decode(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (diagnostics == zc::none) { return zc::none; }
    rejection = SkeletonProviderResult::sourceRejected<diagnostics::DiagnosticFact>(
        zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
    return zc::none;
  }
  if (value.is<binder::BinderKeyRejected>()) {
    rejection = SkeletonProviderResult::keyRejected<binder::BinderKeyFailure>(
        value.get<binder::BinderKeyRejected>().failure.clone());
    return zc::none;
  }
  const auto& bound = value.get<binder::BinderQueryValue<binder::BoundModuleSkeleton>>();
  if (bound.diagnostics.values().size() != 0 ||
      !sameSkeletonModule(bound.value.module(), key.module())) {
    return zc::none;
  }
  return bound.value.clone();
}

}  // namespace

bool sameDefinitionProvenanceLeases(
    zc::ArrayPtr<const MaterializedModuleSkeleton::DefinitionProvenanceLease> left,
    zc::ArrayPtr<const MaterializedModuleSkeleton::DefinitionProvenanceLease> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].key().canonicalBytes() != right[index].key().canonicalBytes() ||
        left[index].stableWitness() != right[index].stableWitness()) {
      return false;
    }
  }
  return true;
}

bool sameDefinitionSite(const binder::DefinitionSite& left, const binder::DefinitionSite& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::DeclarationDefinitionSite>()) {
    return rightValue.is<binder::DeclarationDefinitionSite>() &&
           leftValue.get<binder::DeclarationDefinitionSite>().node ==
               rightValue.get<binder::DeclarationDefinitionSite>().node;
  }
  if (!rightValue.is<binder::PatternBindingSite>()) { return false; }
  const auto& leftPattern = leftValue.get<binder::PatternBindingSite>();
  const auto& rightPattern = rightValue.get<binder::PatternBindingSite>();
  return leftPattern.introducer == rightPattern.introducer &&
         leftPattern.patternPath.asPtr() == rightPattern.patternPath.asPtr();
}

zc::Maybe<zc::Vector<binder::DefinitionFact>> materializeDefinitionFacts(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    const binder::RevisionLocalDefinitionSites& definitionSites,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource) {
  const auto declarations = identities.stableWitness().declarations().values();
  const auto scopes = identities.stableWitness().scopes().values();
  if (declarations.size() != identities.definitions().size() ||
      declarations.size() != definitionSites.entries().size() ||
      scopes.size() != scopeIdentities.size() ||
      parsedSource.canonicalSourceKey() != source.encode().asPtr()) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  zc::Vector<binder::DefinitionFact> result(declarations.size());
  for (const auto& declaration : declarations) {
    zc::Maybe<identity::DefId> definition;
    for (const auto& entry : identities.definitions()) {
      if (entry.key() != declaration.queryKey().definition()) { continue; }
      if (definition != zc::none) { return zc::none; }
      definition = entry.handle();
    }
    zc::Maybe<const binder::RevisionLocalDefinitionSite&> site;
    for (const auto& entry : definitionSites.entries()) {
      if (entry.definition() != declaration.queryKey().definition()) { continue; }
      if (site != zc::none) { return zc::none; }
      site = entry;
    }
    zc::Maybe<binder::ScopeId> scope;
    for (size_t index = 0; index < scopes.size(); ++index) {
      if (scopes[index].owner() != declaration.declaringScope()) { continue; }
      if (scope != zc::none) { return zc::none; }
      scope = scopeIdentities[index];
    }
    if (definition == zc::none || site == zc::none || scope == zc::none ||
        ZC_ASSERT_NONNULL(site).site().source().encode().asPtr() != source.encode().asPtr()) {
      return zc::none;
    }
    auto span = ZC_ASSERT_NONNULL(snapshot).span(ZC_ASSERT_NONNULL(site).byteStart(),
                                                 ZC_ASSERT_NONNULL(site).byteEnd());
    if (span == zc::none) { return zc::none; }
    zc::Maybe<binder::MemberVisibility> memberVisibility;
    ZC_IF_SOME(value, declaration.visibility()) { memberVisibility = value; }
    result.add(binder::DefinitionFact(
        ZC_ASSERT_NONNULL(definition),
        binder::DefinitionSite::declaration(ZC_ASSERT_NONNULL(site).node()), declaration.kind(),
        declaration.name().clone(), declaration.nameSpace(), ZC_ASSERT_NONNULL(scope),
        zc::mv(ZC_ASSERT_NONNULL(span)), declaration.activation(), zc::mv(memberVisibility)));
  }
  return result;
}

bool sameDefinitionFacts(zc::ArrayPtr<const binder::DefinitionFact> left,
                         zc::ArrayPtr<const binder::DefinitionFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].identity != right[index].identity ||
        !sameDefinitionSite(left[index].site, right[index].site) ||
        left[index].kind != right[index].kind || left[index].name != right[index].name ||
        left[index].nameSpace != right[index].nameSpace ||
        left[index].declaringScope != right[index].declaringScope ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd() ||
        left[index].activation != right[index].activation ||
        left[index].memberVisibility != right[index].memberVisibility) {
      return false;
    }
  }
  return true;
}

bool sameImplementationBindings(zc::ArrayPtr<const binder::ImplBindingFact> left,
                                zc::ArrayPtr<const binder::ImplBindingFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].occurrence != right[index].occurrence ||
        left[index].authority != right[index].authority || left[index].node != right[index].node ||
        left[index].scope != right[index].scope ||
        left[index].members.size() != right[index].members.size() ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd()) {
      return false;
    }
    for (size_t memberIndex = 0; memberIndex < left[index].members.size(); ++memberIndex) {
      if (left[index].members[memberIndex] != right[index].members[memberIndex]) { return false; }
    }
  }
  return true;
}

bool sameGenericParameterFacts(zc::ArrayPtr<const binder::GenericParameterFact> left,
                               zc::ArrayPtr<const binder::GenericParameterFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].identity != right[index].identity ||
        !sameDefinitionSite(left[index].site, right[index].site) ||
        left[index].name != right[index].name ||
        left[index].declaringScope != right[index].declaringScope ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd()) {
      return false;
    }
  }
  return true;
}

bool sameCallableParameterFacts(zc::ArrayPtr<const binder::CallableParameterFact> left,
                                zc::ArrayPtr<const binder::CallableParameterFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].identity != right[index].identity ||
        !sameDefinitionSite(left[index].site, right[index].site) ||
        left[index].name != right[index].name ||
        left[index].declaringScope != right[index].declaringScope ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd() ||
        left[index].receiver != right[index].receiver) {
      return false;
    }
  }
  return true;
}

zc::Vector<binder::DefinitionFact> cloneDefinitionFacts(
    zc::ArrayPtr<const binder::DefinitionFact> facts) {
  zc::Vector<binder::DefinitionFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<binder::MemberVisibility> memberVisibility;
    ZC_IF_SOME(value, fact.memberVisibility) { memberVisibility = value; }
    result.add(binder::DefinitionFact(
        fact.identity, fact.site.clone(), fact.kind, fact.name.clone(), fact.nameSpace,
        fact.declaringScope, fact.source.clone(), fact.activation, zc::mv(memberVisibility)));
  }
  return result;
}

zc::Maybe<zc::Vector<binder::NodeScopeFact>> materializeSkeletonNodeScopes(
    const binder::ModuleBodyProvenance& provenance,
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities) {
  const auto stableNodeScopes = identities.stableWitness().nodeScopes().values();
  const auto stableScopes = identities.stableWitness().scopes().values();
  if (stableScopes.size() != scopeIdentities.size()) { return zc::none; }
  zc::Vector<binder::NodeScopeFact> result(stableNodeScopes.size());
  for (const auto& fact : stableNodeScopes) {
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenance.entries()) {
      if (entry.path != fact.nodePath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    zc::Maybe<binder::ScopeId> scope;
    for (size_t index = 0; index < stableScopes.size(); ++index) {
      if (stableScopes[index].owner() != fact.scope()) { continue; }
      if (scope != zc::none) { return zc::none; }
      scope = scopeIdentities[index];
    }
    if (node == zc::none || scope == zc::none) { return zc::none; }
    result.add(binder::NodeScopeFact{ZC_ASSERT_NONNULL(node), ZC_ASSERT_NONNULL(scope)});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::ImplBindingFact>> materializeImplementationBindings(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    const binder::RevisionLocalImplementationSites& implementationSites,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource) {
  const auto occurrences = identities.stableWitness().implementationOccurrences().values();
  const auto scopes = identities.stableWitness().scopes().values();
  if (occurrences.size() != implementationSites.entries().size() ||
      scopes.size() != scopeIdentities.size() ||
      parsedSource.canonicalSourceKey() != source.encode().asPtr()) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  auto allocator =
      binder::ModuleLocalIdentityAllocator::create(identities.context(), identities.module());
  if (snapshot == zc::none || allocator == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  zc::Vector<binder::ImplBindingFact> result(occurrences.size());
  for (const auto& occurrence : occurrences) {
    zc::Maybe<identity::ImplId> authority;
    for (const auto& entry : identities.implementations()) {
      if (entry.key() != occurrence.authority().implementation()) { continue; }
      if (authority != zc::none) { return zc::none; }
      authority = entry.handle();
    }
    zc::Maybe<const binder::RevisionLocalImplementationSite&> site;
    for (const auto& entry : implementationSites.entries()) {
      if (!entry.occurrence().sameAs(occurrence.occurrence().occurrence())) { continue; }
      if (site != zc::none) { return zc::none; }
      site = entry;
    }
    zc::Maybe<binder::ScopeId> scope;
    for (size_t index = 0; index < scopes.size(); ++index) {
      if (scopes[index].owner() != occurrence.declaringScope()) { continue; }
      if (scope != zc::none) { return zc::none; }
      scope = scopeIdentities[index];
    }
    auto identity = ZC_ASSERT_NONNULL(allocator).allocateImplOccurrence();
    auto span = site == zc::none
                    ? zc::Maybe<identity::SourceSpan>()
                    : ZC_ASSERT_NONNULL(snapshot).span(ZC_ASSERT_NONNULL(site).byteStart(),
                                                       ZC_ASSERT_NONNULL(site).byteEnd());
    if (authority == zc::none || site == zc::none || scope == zc::none || identity == zc::none ||
        span == zc::none) {
      return zc::none;
    }
    zc::Vector<identity::DefId> members;
    for (const auto& declaration : identities.stableWitness().declarations().values()) {
      if (declaration.declaringScope() != occurrence.declaringScope() ||
          declaration.activation() != binder::DefinitionActivation::ModuleSkeleton) {
        continue;
      }
      zc::Maybe<identity::DefId> member;
      for (const auto& entry : identities.definitions()) {
        if (entry.key() != declaration.queryKey().definition()) { continue; }
        if (member != zc::none) { return zc::none; }
        member = entry.handle();
      }
      if (member == zc::none) { return zc::none; }
      members.add(ZC_ASSERT_NONNULL(member));
    }
    result.add(binder::ImplBindingFact{ZC_ASSERT_NONNULL(identity), ZC_ASSERT_NONNULL(authority),
                                       ZC_ASSERT_NONNULL(site).node(), ZC_ASSERT_NONNULL(scope),
                                       zc::mv(members), zc::mv(ZC_ASSERT_NONNULL(span))});
  }
  return result;
}

zc::Vector<binder::ImplBindingFact> cloneImplementationBindings(
    zc::ArrayPtr<const binder::ImplBindingFact> facts) {
  zc::Vector<binder::ImplBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<identity::DefId> members(fact.members.size());
    for (const auto member : fact.members) { members.add(member); }
    result.add(binder::ImplBindingFact{fact.occurrence, fact.authority, fact.node, fact.scope,
                                       zc::mv(members), fact.source.clone()});
  }
  return result;
}

zc::Maybe<size_t> scopeIndexFor(const binder::BoundModuleSkeleton& skeleton,
                                const binder::StableScopeOwnerKey& owner) {
  zc::Maybe<size_t> result;
  const auto scopes = skeleton.scopes().values();
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (scopes[index].owner() != owner) { continue; }
    if (result != zc::none) { return zc::none; }
    result = index;
  }
  return result;
}

zc::Maybe<binder::ScopeOwner> materializeScopeOwner(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const binder::ImplBindingFact> implementations,
    const binder::StableScopeOwnerKey& stableOwner) {
  const auto& owner = stableOwner.value();
  if (owner.is<binder::StableModuleScope>()) {
    if (owner.get<binder::StableModuleScope>().module.encode().asPtr() !=
        identities.stableWitness().module().encode().asPtr()) {
      return zc::none;
    }
    return binder::ScopeOwner::module(identities.module());
  }
  if (owner.is<binder::StableDefinitionScope>()) {
    const auto& definition = owner.get<binder::StableDefinitionScope>().definition;
    for (const auto& identity : identities.definitions()) {
      if (identity.key() != definition.definition()) { continue; }
      return binder::ScopeOwner::definition(identity.handle());
    }
    return zc::none;
  }
  if (owner.is<binder::StableImplementationOccurrenceScope>()) {
    const auto& occurrence = owner.get<binder::StableImplementationOccurrenceScope>().occurrence;
    const auto stableOccurrences = identities.stableWitness().implementationOccurrences().values();
    if (stableOccurrences.size() != implementations.size()) { return zc::none; }
    for (size_t index = 0; index < stableOccurrences.size(); ++index) {
      if (stableOccurrences[index].occurrence() != occurrence) { continue; }
      return binder::ScopeOwner::implementation(implementations[index].occurrence);
    }
  }
  return zc::none;
}

zc::Maybe<identity::SourceSpan> materializeScopeSource(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const binder::DefinitionFact> definitions,
    zc::ArrayPtr<const binder::ImplBindingFact> implementations,
    const identity::ImmutableSourceSnapshot& snapshot, size_t sourceSize,
    const binder::StableScopeOwnerKey& stableOwner) {
  const auto& owner = stableOwner.value();
  if (owner.is<binder::StableModuleScope>()) { return snapshot.span(0, sourceSize); }
  if (owner.is<binder::StableDefinitionScope>()) {
    const auto& definition = owner.get<binder::StableDefinitionScope>().definition;
    for (const auto& identity : identities.definitions()) {
      if (identity.key() != definition.definition()) { continue; }
      for (const auto& fact : definitions) {
        if (fact.identity == identity.handle()) { return fact.source.clone(); }
      }
      return zc::none;
    }
    return zc::none;
  }
  if (owner.is<binder::StableImplementationOccurrenceScope>()) {
    const auto& occurrence = owner.get<binder::StableImplementationOccurrenceScope>().occurrence;
    const auto stableOccurrences = identities.stableWitness().implementationOccurrences().values();
    if (stableOccurrences.size() != implementations.size()) { return zc::none; }
    for (size_t index = 0; index < stableOccurrences.size(); ++index) {
      if (stableOccurrences[index].occurrence() == occurrence) {
        return implementations[index].source.clone();
      }
    }
  }
  return zc::none;
}

bool addScopeBinding(zc::Vector<binder::ScopeBindingEntry>& bindings, binder::BindingNameKey&& name,
                     binder::BindingTarget&& bindingIdentity,
                     binder::BindingTarget&& canonicalTarget, binder::Namespace nameSpace,
                     identity::SourceSpan&& source) {
  zc::Maybe<identity::SourceSpan> noAlias;
  bindings.add(binder::ScopeBindingEntry(
      zc::mv(name), binder::NameBinding(zc::mv(bindingIdentity), zc::mv(canonicalTarget), nameSpace,
                                        binder::BindingOrigin::LocalDeclaration, zc::mv(source),
                                        zc::mv(noAlias))));
  return true;
}

bool addScopeBinding(zc::Vector<binder::ScopeBindingEntry>& bindings, binder::BindingNameKey&& name,
                     binder::BindingTarget&& target, binder::Namespace nameSpace,
                     identity::SourceSpan&& source) {
  auto canonicalTarget = target.clone();
  return addScopeBinding(bindings, zc::mv(name), zc::mv(target), zc::mv(canonicalTarget), nameSpace,
                         zc::mv(source));
}

zc::Maybe<zc::Vector<binder::ModuleAliasBindingFact>> materializeModuleAliases(
    const MaterializedModuleGraph& graph,
    zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease> dependencies,
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const binder::DefinitionFact> definitions, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource);

zc::Maybe<binder::Namespace> materializedImportNamespace(identity::DefinitionNamespace value) {
  switch (value) {
    case identity::DefinitionNamespace::Value:
      return binder::Namespace::Value;
    case identity::DefinitionNamespace::Type:
      return binder::Namespace::Type;
    case identity::DefinitionNamespace::Module:
      return binder::Namespace::Module;
  }
  return zc::none;
}

bool isMaterializedImport(const identity::ImportBindingKey& binding) {
  return binding.operation() == identity::SemanticImportOperation::Import ||
         binding.operation() == identity::SemanticImportOperation::ForeignReexport;
}

size_t materializedImportCount(const binder::BoundModuleSkeleton& stableWitness) {
  size_t result = 0;
  for (const auto& stable : stableWitness.imports().values()) {
    if (isMaterializedImport(stable.queryKey().binding())) { ++result; }
  }
  return result;
}

bool sameMaterializedImportTarget(const binder::BindingTarget& left,
                                  const binder::BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::DefinitionBindingTarget>()) {
    return rightValue.is<binder::DefinitionBindingTarget>() &&
           leftValue.get<binder::DefinitionBindingTarget>().definition ==
               rightValue.get<binder::DefinitionBindingTarget>().definition;
  }
  return leftValue.is<binder::ModuleBindingTarget>() &&
         rightValue.is<binder::ModuleBindingTarget>() &&
         leftValue.get<binder::ModuleBindingTarget>().module ==
             rightValue.get<binder::ModuleBindingTarget>().module;
}

zc::Maybe<identity::DefId> materializeDefinitionTarget(
    const binder::StableDefinitionQueryKey& definition,
    zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease> dependencies) {
  zc::Maybe<identity::DefId> result;
  const auto addCandidate = [&](identity::DefId candidate) {
    if (result != zc::none && ZC_ASSERT_NONNULL(result) != candidate) { return false; }
    if (result == zc::none) { result = candidate; }
    return true;
  };
  for (const auto& dependency : dependencies) {
    const auto& skeleton = dependency.capability();
    if (skeleton.module().encode().asPtr() == definition.module().encode().asPtr()) {
      for (const auto& identity : skeleton.identities().definitions()) {
        if (identity.key() != definition.definition()) { continue; }
        if (!addCandidate(identity.handle())) { return zc::none; }
      }
    }
    auto nested = materializeDefinitionTarget(definition, skeleton.dependencySkeletonLeases());
    if (nested != zc::none && !addCandidate(ZC_ASSERT_NONNULL(nested))) { return zc::none; }
  }
  return result;
}

zc::Maybe<binder::BindingTarget> materializeImportTarget(
    const binder::StableBindingTargetKey& stable,
    zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease> dependencies) {
  const auto& value = stable.value();
  if (value.is<binder::StableModuleBindingTarget>()) {
    const auto& module = value.get<binder::StableModuleBindingTarget>().module;
    for (const auto& dependency : dependencies) {
      if (dependency.capability().module().encode().asPtr() == module.encode().asPtr()) {
        return binder::BindingTarget::module(dependency.capability().identities().module());
      }
    }
    return zc::none;
  }
  if (!value.is<binder::StableDefinitionBindingTarget>()) { return zc::none; }
  const auto& definition = value.get<binder::StableDefinitionBindingTarget>().definition;
  auto materialized = materializeDefinitionTarget(definition, dependencies);
  if (materialized == zc::none) { return zc::none; }
  return binder::BindingTarget::definition(ZC_ASSERT_NONNULL(materialized));
}

zc::Maybe<zc::Vector<binder::ImportBindingFact>> materializeImports(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease> dependencies,
    const MaterializedModuleGraph& graph,
    const module_graph_query::ModuleDependencyProvenanceMap& provenance) {
  const auto stableImports = identities.stableWitness().imports().values();
  zc::Vector<binder::ImportBindingFact> result(stableImports.size());
  for (const auto& stable : stableImports) {
    const auto& binding = stable.queryKey().binding();
    if (!isMaterializedImport(binding)) { continue; }
    const bool normal = binding.operation() == identity::SemanticImportOperation::Import;
    if ((!normal && binding.operation() != identity::SemanticImportOperation::ForeignReexport) ||
        binding.requester().encode().asPtr() !=
            identities.stableWitness().module().encode().asPtr() ||
        stable.target().value().is<binder::StableSemanticImportBindingTarget>() == false ||
        stable.nameSpace() !=
            ZC_ASSERT_NONNULL(materializedImportNamespace(binding.localNamespace()))) {
      return zc::none;
    }
    auto canonical = materializeImportTarget(stable.canonicalTarget(), dependencies);
    if (canonical == zc::none) { return zc::none; }
    zc::Maybe<const module_graph_query::ModuleDependencyProvenanceEntry&> request;
    for (const auto& candidate : provenance.entries()) {
      if (candidate.request().encode().asPtr() != binding.resolution().encode().asPtr()) {
        continue;
      }
      if (request != zc::none ||
          candidate.origin().kind() !=
              module_graph_query::ModuleDependencyProvenanceOriginKind::Source ||
          candidate.origin().sites().size() != 1) {
        return zc::none;
      }
      request = candidate;
    }
    zc::Maybe<identity::ModuleKey> requestedModule;
    for (const auto& edge : graph.requestEdges()) {
      if (edge.requester() != identities.module() ||
          edge.request().encode().asPtr() != binding.resolution().encode().asPtr()) {
        continue;
      }
      auto dependency = materializedModuleForHandle(graph, edge.dependency());
      if (dependency == zc::none || requestedModule != zc::none) { return zc::none; }
      requestedModule = ZC_ASSERT_NONNULL(dependency).key().clone();
    }
    if (request == zc::none || requestedModule == zc::none) { return zc::none; }
    zc::Maybe<const MaterializedModuleSkeleton&> source;
    for (const auto& dependency : dependencies) {
      if (dependency.capability().module().encode().asPtr() !=
          ZC_ASSERT_NONNULL(requestedModule).encode().asPtr()) {
        continue;
      }
      if (source != zc::none) { return zc::none; }
      source = dependency.capability();
    }
    if (source == zc::none) { return zc::none; }
    const bool importsModule =
        ZC_ASSERT_NONNULL(canonical).value().is<binder::ModuleBindingTarget>();
    if (!importsModule) {
      bool matchedSurface = false;
      for (const auto& entry : ZC_ASSERT_NONNULL(source).bindingSurface().exports()) {
        auto sourceNamespace = materializedImportNamespace(binding.sourceNamespace());
        if (sourceNamespace == zc::none ||
            entry.name.nameSpace() != ZC_ASSERT_NONNULL(sourceNamespace) ||
            entry.name.name().text() != binding.sourceName().text() ||
            !sameMaterializedImportTarget(entry.canonicalTarget, ZC_ASSERT_NONNULL(canonical))) {
          continue;
        }
        if (matchedSurface) { return zc::none; }
        matchedSurface = true;
      }
      if (!matchedSurface) { return zc::none; }
    }
    zc::Maybe<identity::SourceSpan> noAlias;
    zc::Vector<binder::ReexportProvenanceStep> chain;
    auto declaration = ZC_ASSERT_NONNULL(request).origin().sites()[0].span().clone();
    if (!normal) {
      chain.add(binder::ReexportProvenanceStep{
          identities.module(), binder::BindingTarget::semanticImport(binding.clone()),
          ZC_ASSERT_NONNULL(canonical).clone(), declaration.clone()});
    }
    result.add(binder::ImportBindingFact{
        ZC_ASSERT_NONNULL(request).origin().sites()[0].node(), binding.clone(),
        zc::mv(ZC_ASSERT_NONNULL(canonical)), ZC_ASSERT_NONNULL(source).identities().module(),
        ZC_ASSERT_NONNULL(source).bindingSurface().revision(),
        normal ? binder::ImportBindingKind::Import : binder::ImportBindingKind::ForeignReexport,
        zc::mv(declaration), zc::mv(noAlias), zc::mv(chain)});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::ScopeRecord>> materializeScopeRecords(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities,
    zc::ArrayPtr<const binder::DefinitionFact> definitions,
    zc::ArrayPtr<const binder::GenericParameterFact> genericParameters,
    zc::ArrayPtr<const binder::CallableParameterFact> callableParameters,
    zc::ArrayPtr<const binder::ImplBindingFact> implementations,
    zc::ArrayPtr<const binder::ModuleAliasBindingFact> moduleAliases,
    zc::ArrayPtr<const binder::ImportBindingFact> imports, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource) {
  const auto& stableWitness = identities.stableWitness();
  const auto stableScopes = stableWitness.scopes().values();
  if (stableScopes.size() != scopeIdentities.size() ||
      stableWitness.moduleAliases().values().size() != moduleAliases.size() ||
      materializedImportCount(stableWitness) != imports.size() ||
      parsedSource.canonicalSourceKey() != source.encode().asPtr()) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  zc::Vector<zc::Vector<binder::ScopeBindingEntry>> bindings;
  bindings.resize(stableScopes.size());
  for (const auto& fact : definitions) {
    if (fact.activation != binder::DefinitionActivation::ModuleSkeleton) { continue; }
    zc::Maybe<size_t> index;
    for (size_t candidate = 0; candidate < scopeIdentities.size(); ++candidate) {
      if (scopeIdentities[candidate] != fact.declaringScope) { continue; }
      if (index != zc::none) { return zc::none; }
      index = candidate;
    }
    auto name = binder::BindingNameKey::from(fact.nameSpace, fact.name.clone());
    if (index == zc::none || name == zc::none ||
        !addScopeBinding(bindings[ZC_ASSERT_NONNULL(index)], zc::mv(ZC_ASSERT_NONNULL(name)),
                         binder::BindingTarget::definition(fact.identity), fact.nameSpace,
                         fact.source.clone())) {
      return zc::none;
    }
  }
  for (const auto& fact : genericParameters) {
    zc::Maybe<size_t> index;
    for (size_t candidate = 0; candidate < scopeIdentities.size(); ++candidate) {
      if (scopeIdentities[candidate] != fact.declaringScope) { continue; }
      if (index != zc::none) { return zc::none; }
      index = candidate;
    }
    auto name = binder::BindingNameKey::from(binder::Namespace::Type, fact.name.clone());
    if (index == zc::none || name == zc::none ||
        !addScopeBinding(bindings[ZC_ASSERT_NONNULL(index)], zc::mv(ZC_ASSERT_NONNULL(name)),
                         binder::BindingTarget::genericParameter(fact.identity),
                         binder::Namespace::Type, fact.source.clone())) {
      return zc::none;
    }
  }
  for (const auto& fact : callableParameters) {
    if (fact.name == zc::none) { continue; }
    zc::Maybe<size_t> index;
    for (size_t candidate = 0; candidate < scopeIdentities.size(); ++candidate) {
      if (scopeIdentities[candidate] != fact.declaringScope) { continue; }
      if (index != zc::none) { return zc::none; }
      index = candidate;
    }
    auto name = binder::BindingNameKey::from(binder::Namespace::Value,
                                             ZC_ASSERT_NONNULL(fact.name).clone());
    if (index == zc::none || name == zc::none ||
        !addScopeBinding(bindings[ZC_ASSERT_NONNULL(index)], zc::mv(ZC_ASSERT_NONNULL(name)),
                         binder::BindingTarget::callableParameter(fact.identity),
                         binder::Namespace::Value, fact.source.clone())) {
      return zc::none;
    }
  }
  for (const auto& alias : moduleAliases) {
    zc::Maybe<size_t> index;
    for (size_t candidate = 0; candidate < stableScopes.size(); ++candidate) {
      const auto& owner = stableScopes[candidate].owner().value();
      if (!owner.is<binder::StableModuleScope>() ||
          owner.get<binder::StableModuleScope>().module.encode().asPtr() !=
              identities.stableWitness().module().encode().asPtr()) {
        continue;
      }
      if (index != zc::none) { return zc::none; }
      index = candidate;
    }
    const binder::StableModuleAliasFact* stableAlias = nullptr;
    for (const auto& candidate : stableWitness.moduleAliases().values()) {
      zc::Maybe<identity::DefId> candidateIdentity;
      for (const auto& entry : identities.definitions()) {
        if (entry.key() != candidate.alias().definition()) { continue; }
        if (candidateIdentity != zc::none) { return zc::none; }
        candidateIdentity = entry.handle();
      }
      if (candidateIdentity == zc::none || ZC_ASSERT_NONNULL(candidateIdentity) != alias.alias) {
        continue;
      }
      if (stableAlias != nullptr) { return zc::none; }
      stableAlias = &candidate;
    }
    if (index == zc::none || stableAlias == nullptr ||
        stableAlias->queryKey().binding().localNamespace() !=
            identity::DefinitionNamespace::Module) {
      return zc::none;
    }
    auto name = binder::BindingNameKey::from(binder::Namespace::Module,
                                             stableAlias->queryKey().binding().localName().clone());
    if (name == zc::none ||
        !addScopeBinding(bindings[ZC_ASSERT_NONNULL(index)], zc::mv(ZC_ASSERT_NONNULL(name)),
                         binder::BindingTarget::definition(alias.alias),
                         binder::BindingTarget::module(alias.canonicalTarget),
                         binder::Namespace::Module, alias.declarationSpan.clone())) {
      return zc::none;
    }
  }
  for (const auto& import : imports) {
    zc::Maybe<size_t> index;
    for (size_t candidate = 0; candidate < stableScopes.size(); ++candidate) {
      const auto& owner = stableScopes[candidate].owner().value();
      if (!owner.is<binder::StableModuleScope>() ||
          owner.get<binder::StableModuleScope>().module.encode().asPtr() !=
              identities.stableWitness().module().encode().asPtr()) {
        continue;
      }
      if (index != zc::none) { return zc::none; }
      index = candidate;
    }
    const binder::StableImportFact* stableImport = nullptr;
    for (const auto& candidate : stableWitness.imports().values()) {
      if (candidate.queryKey().binding() != import.binding) { continue; }
      if (stableImport != nullptr) { return zc::none; }
      stableImport = &candidate;
    }
    const auto nameSpace = materializedImportNamespace(import.binding.localNamespace());
    if (index == zc::none || stableImport == nullptr || nameSpace == zc::none ||
        ZC_ASSERT_NONNULL(stableImport).nameSpace() != ZC_ASSERT_NONNULL(nameSpace) ||
        (import.kind == binder::ImportBindingKind::Import) !=
            (import.binding.operation() == identity::SemanticImportOperation::Import) ||
        !import.declarationSpan.belongsTo(source)) {
      return zc::none;
    }
    auto name = binder::BindingNameKey::from(ZC_ASSERT_NONNULL(nameSpace),
                                             import.binding.localName().clone());
    if (name == zc::none) { return zc::none; }
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, import.aliasSpan) { aliasSpan = value.clone(); }
    bindings[ZC_ASSERT_NONNULL(index)].add(binder::ScopeBindingEntry(
        zc::mv(ZC_ASSERT_NONNULL(name)),
        binder::NameBinding(binder::BindingTarget::semanticImport(import.binding.clone()),
                            import.canonicalTarget.clone(), ZC_ASSERT_NONNULL(nameSpace),
                            import.kind == binder::ImportBindingKind::Import
                                ? binder::BindingOrigin::ImportAlias
                                : binder::BindingOrigin::ReexportAlias,
                            import.declarationSpan.clone(), zc::mv(aliasSpan))));
  }
  zc::Vector<binder::ScopeRecord> result(stableScopes.size());
  for (size_t index = 0; index < stableScopes.size(); ++index) {
    zc::Maybe<binder::ScopeId> parent;
    ZC_IF_SOME(stableParent, stableScopes[index].parent()) {
      auto parentIndex = scopeIndexFor(stableWitness, stableParent);
      if (parentIndex == zc::none) { return zc::none; }
      parent = scopeIdentities[ZC_ASSERT_NONNULL(parentIndex)];
    }
    auto owner = materializeScopeOwner(identities, implementations, stableScopes[index].owner());
    auto scopeSource = materializeScopeSource(
        identities, definitions, implementations, ZC_ASSERT_NONNULL(snapshot),
        parsedSource.sourceBytes().size(), stableScopes[index].owner());
    if (owner == zc::none || scopeSource == zc::none) { return zc::none; }
    result.add(binder::ScopeRecord(scopeIdentities[index], zc::mv(parent),
                                   zc::mv(ZC_ASSERT_NONNULL(owner)), stableScopes[index].kind(),
                                   zc::mv(bindings[index]),
                                   zc::mv(ZC_ASSERT_NONNULL(scopeSource))));
  }
  return result;
}

zc::Vector<binder::ScopeRecord> cloneScopeRecords(zc::ArrayPtr<const binder::ScopeRecord> records) {
  zc::Vector<binder::ScopeRecord> result(records.size());
  for (const auto& record : records) {
    zc::Maybe<binder::ScopeId> parent;
    ZC_IF_SOME(value, record.parent) { parent = value; }
    zc::Vector<binder::ScopeBindingEntry> bindings(record.bindings.size());
    for (const auto& entry : record.bindings) {
      zc::Maybe<identity::SourceSpan> alias;
      ZC_IF_SOME(value, entry.binding.aliasSpan) { alias = value.clone(); }
      bindings.add(binder::ScopeBindingEntry(
          entry.name.clone(),
          binder::NameBinding(entry.binding.bindingIdentity.clone(),
                              entry.binding.canonicalTarget.clone(), entry.binding.nameSpace,
                              entry.binding.origin, entry.binding.declarationSpan.clone(),
                              zc::mv(alias))));
    }
    result.add(binder::ScopeRecord(record.id, zc::mv(parent), record.owner.clone(), record.kind,
                                   zc::mv(bindings), record.source.clone()));
  }
  return result;
}

bool sameScopeBindingTarget(const binder::BindingTarget& left, const binder::BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::DefinitionBindingTarget>()) {
    return rightValue.is<binder::DefinitionBindingTarget>() &&
           leftValue.get<binder::DefinitionBindingTarget>().definition ==
               rightValue.get<binder::DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<binder::GenericParameterBindingTarget>()) {
    return rightValue.is<binder::GenericParameterBindingTarget>() &&
           leftValue.get<binder::GenericParameterBindingTarget>().parameter ==
               rightValue.get<binder::GenericParameterBindingTarget>().parameter;
  }
  if (leftValue.is<binder::CallableParameterBindingTarget>()) {
    return rightValue.is<binder::CallableParameterBindingTarget>() &&
           leftValue.get<binder::CallableParameterBindingTarget>().parameter ==
               rightValue.get<binder::CallableParameterBindingTarget>().parameter;
  }
  if (leftValue.is<binder::OwnerLocalBindingTarget>()) {
    return rightValue.is<binder::OwnerLocalBindingTarget>() &&
           leftValue.get<binder::OwnerLocalBindingTarget>().binding ==
               rightValue.get<binder::OwnerLocalBindingTarget>().binding;
  }
  if (leftValue.is<binder::SemanticImportBindingTarget>()) {
    return rightValue.is<binder::SemanticImportBindingTarget>() &&
           leftValue.get<binder::SemanticImportBindingTarget>().binding ==
               rightValue.get<binder::SemanticImportBindingTarget>().binding;
  }
  return rightValue.is<binder::ModuleBindingTarget>() &&
         leftValue.get<binder::ModuleBindingTarget>().module ==
             rightValue.get<binder::ModuleBindingTarget>().module;
}

bool sameScopeSource(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().encode().asPtr() == right.source().encode().asPtr() &&
         left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

bool sameOptionalScopeSource(const zc::Maybe<identity::SourceSpan>& left,
                             const zc::Maybe<identity::SourceSpan>& right) {
  if ((left == zc::none) != (right == zc::none)) { return false; }
  return left == zc::none || sameScopeSource(ZC_ASSERT_NONNULL(left), ZC_ASSERT_NONNULL(right));
}

bool sameScopeRecords(zc::ArrayPtr<const binder::ScopeRecord> left,
                      zc::ArrayPtr<const binder::ScopeRecord> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].id != right[index].id || left[index].parent != right[index].parent ||
        left[index].owner != right[index].owner || left[index].kind != right[index].kind ||
        !sameScopeSource(left[index].source, right[index].source) ||
        left[index].bindings.size() != right[index].bindings.size()) {
      return false;
    }
    for (size_t bindingIndex = 0; bindingIndex < left[index].bindings.size(); ++bindingIndex) {
      const auto& leftBinding = left[index].bindings[bindingIndex];
      const auto& rightBinding = right[index].bindings[bindingIndex];
      if (leftBinding.name.nameSpace() != rightBinding.name.nameSpace() ||
          leftBinding.name.name() != rightBinding.name.name() ||
          !sameScopeBindingTarget(leftBinding.binding.bindingIdentity,
                                  rightBinding.binding.bindingIdentity) ||
          !sameScopeBindingTarget(leftBinding.binding.canonicalTarget,
                                  rightBinding.binding.canonicalTarget) ||
          leftBinding.binding.nameSpace != rightBinding.binding.nameSpace ||
          leftBinding.binding.origin != rightBinding.binding.origin ||
          !sameScopeSource(leftBinding.binding.declarationSpan,
                           rightBinding.binding.declarationSpan) ||
          !sameOptionalScopeSource(leftBinding.binding.aliasSpan, rightBinding.binding.aliasSpan)) {
        return false;
      }
    }
  }
  return true;
}

zc::Maybe<ast::NodeId> definitionNodeFor(const binder::RevisionLocalDefinitionSites& sites,
                                         const identity::DefinitionKey& definition) {
  zc::Maybe<ast::NodeId> result;
  for (const auto& site : sites.entries()) {
    if (site.definition() != definition) { continue; }
    if (result != zc::none) { return zc::none; }
    result = site.node();
  }
  return result;
}

zc::Maybe<ast::NodeId> implementationNodeFor(const binder::RevisionLocalImplementationSites& sites,
                                             const identity::ImplKey& implementation) {
  zc::Maybe<ast::NodeId> result;
  for (const auto& site : sites.entries()) {
    if (site.occurrence().implementation() != implementation) { continue; }
    if (result != zc::none) { return zc::none; }
    result = site.node();
  }
  return result;
}

zc::Maybe<binder::ScopeId> scopeForStableOwner(const binder::BoundModuleSkeleton& skeleton,
                                               zc::ArrayPtr<const binder::ScopeId> identities,
                                               const binder::StableScopeOwnerKey& owner) {
  const auto scopes = skeleton.scopes().values();
  if (scopes.size() != identities.size()) { return zc::none; }
  zc::Maybe<binder::ScopeId> result;
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (scopes[index].owner() != owner) { continue; }
    if (result != zc::none) { return zc::none; }
    result = identities[index];
  }
  return result;
}

zc::Maybe<identity::SourceSpan> sourceSpanForNode(const parser::CanonicalParsedSource& parsedSource,
                                                  const identity::ImmutableSourceSnapshot& snapshot,
                                                  ast::NodeId node) {
  const auto& tree = parsedSource.tree();
  if (!tree.contains(node)) { return zc::none; }
  const auto range = tree.node(node).range;
  if (!range.isValid()) { return zc::none; }
  const auto start =
      parsedSource.sourceManager().getLocOffsetInBuffer(range.getStart(), parsedSource.buffer());
  const auto end =
      parsedSource.sourceManager().getLocOffsetInBuffer(range.getEnd(), parsedSource.buffer());
  return snapshot.span(start, end);
}

zc::Maybe<identity::SourceSpan> identifierTokenSpanForNode(
    const parser::CanonicalParsedSource& parsedSource,
    const identity::ImmutableSourceSnapshot& snapshot, ast::NodeId node, size_t ordinal) {
  auto nodeSpan = sourceSpanForNode(parsedSource, snapshot, node);
  if (nodeSpan == zc::none) { return zc::none; }
  size_t identifierOrdinal = 0;
  for (const auto& token : parsedSource.tokens()) {
    if (token.kind != ast::SyntaxKind::Identifier ||
        token.byteStart < ZC_ASSERT_NONNULL(nodeSpan).byteStart() ||
        token.byteEnd > ZC_ASSERT_NONNULL(nodeSpan).byteEnd()) {
      continue;
    }
    if (identifierOrdinal++ == ordinal) { return snapshot.span(token.byteStart, token.byteEnd); }
  }
  return zc::none;
}

zc::Maybe<zc::Vector<binder::ModuleAliasBindingFact>> materializeModuleAliases(
    const MaterializedModuleGraph& graph,
    zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease> dependencies,
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const binder::DefinitionFact> definitions, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource) {
  const auto stableAliases = identities.stableWitness().moduleAliases().values();
  if (graph.context() != identities.context() || graph.revision() != identities.revision() ||
      parsedSource.canonicalSourceKey() != source.encode().asPtr()) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  const auto& tree = parsedSource.tree();
  zc::Vector<binder::ModuleAliasBindingFact> result(stableAliases.size());
  for (const auto& stableAlias : stableAliases) {
    const auto& owner = stableAlias.declaringScope().value();
    if (!owner.is<binder::StableModuleScope>() ||
        owner.get<binder::StableModuleScope>().module.encode().asPtr() !=
            identities.stableWitness().module().encode().asPtr() ||
        stableAlias.queryKey().binding().operation() !=
            identity::SemanticImportOperation::ModuleAlias ||
        stableAlias.queryKey().binding().localNamespace() !=
            identity::DefinitionNamespace::Module) {
      return zc::none;
    }
    zc::Maybe<identity::DefId> alias;
    for (const auto& entry : identities.definitions()) {
      if (entry.key() != stableAlias.alias().definition()) { continue; }
      if (alias != zc::none) { return zc::none; }
      alias = entry.handle();
    }
    zc::Maybe<const binder::DefinitionFact&> definition;
    for (const auto& candidate : definitions) {
      if (candidate.identity != alias) { continue; }
      if (definition != zc::none) { return zc::none; }
      definition = candidate;
    }
    auto target = materializedModuleHandle(graph.modules(), stableAlias.canonicalModule());
    zc::Maybe<const MaterializedModuleSkeleton&> dependency;
    for (const auto& candidate : dependencies) {
      if (candidate.capability().module().encode().asPtr() !=
          stableAlias.canonicalModule().encode().asPtr()) {
        continue;
      }
      if (dependency != zc::none) { return zc::none; }
      dependency = candidate.capability();
    }
    if (alias == zc::none || definition == zc::none || target == zc::none ||
        dependency == zc::none) {
      return zc::none;
    }
    const auto& definitionSite = ZC_ASSERT_NONNULL(definition).site.value();
    if (!definitionSite.is<binder::DeclarationDefinitionSite>()) { return zc::none; }
    const auto node = definitionSite.get<binder::DeclarationDefinitionSite>().node;
    if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::ModuleDeclaration ||
        static_cast<ast::ModuleDeclarationForm>(
            tree.node(node).payload.words[ast::kModuleDeclarationFormWord]) !=
            ast::ModuleDeclarationForm::Alias) {
      return zc::none;
    }
    const ast::NodeId targetNode(
        tree.node(node).payload.words[ast::kModuleDeclarationAliasTargetWord]);
    auto declarationSpan = sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), node);
    auto targetSpan = sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), targetNode);
    if (declarationSpan == zc::none || targetSpan == zc::none) { return zc::none; }
    result.add(binder::ModuleAliasBindingFact{
        node, ZC_ASSERT_NONNULL(alias), ZC_ASSERT_NONNULL(target),
        binder::ModuleAliasExportNamesRevision::fromDigest(
            stableAlias.targetExportNamesRevision().digest()),
        zc::mv(ZC_ASSERT_NONNULL(declarationSpan)), zc::mv(ZC_ASSERT_NONNULL(targetSpan))});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::LocalExportFact>> materializeLocalExports(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    zc::ArrayPtr<const binder::DefinitionFact> definitions,
    zc::ArrayPtr<const binder::ImportBindingFact> imports,
    zc::ArrayPtr<const binder::MaterializedDependencyExportSurface> dependencies,
    const binder::ModuleBodyProvenance& provenance, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource) {
  const auto stableExports = identities.stableWitness().localExports().values();
  if (parsedSource.canonicalSourceKey() != source.encode().asPtr()) { return zc::none; }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  const auto& tree = parsedSource.tree();
  zc::Vector<binder::LocalExportFact> result(stableExports.size());
  for (const auto& stableExport : stableExports) {
    const auto& binding = stableExport.binding().value();
    const auto& canonical = stableExport.canonicalTarget().value();
    if (stableExport.declaringModule().encode().asPtr() !=
            identities.stableWitness().module().encode().asPtr() ||
        !canonical.is<binder::StableDefinitionBindingTarget>()) {
      return zc::none;
    }
    if (binding.is<binder::StableSemanticImportBindingTarget>()) {
      const auto& importBinding =
          binding.get<binder::StableSemanticImportBindingTarget>().import.binding();
      zc::Maybe<const binder::ImportBindingFact&> imported;
      for (const auto& candidate : imports) {
        if (candidate.binding != importBinding) { continue; }
        if (imported != zc::none) { return zc::none; }
        imported = candidate;
      }
      if (imported == zc::none ||
          ZC_ASSERT_NONNULL(imported).kind != binder::ImportBindingKind::ForeignReexport) {
        return zc::none;
      }
      const auto& stableCanonical =
          canonical.get<binder::StableDefinitionBindingTarget>().definition;
      const auto& importedTarget = ZC_ASSERT_NONNULL(imported).canonicalTarget.value();
      if (!importedTarget.is<binder::DefinitionBindingTarget>()) { return zc::none; }
      bool matchingCanonicalTarget = false;
      for (const auto& dependency : dependencies) {
        if (dependency.module != ZC_ASSERT_NONNULL(imported).sourceModule ||
            dependency.surface.revision().digest() !=
                ZC_ASSERT_NONNULL(imported).sourceRevision.digest()) {
          continue;
        }
        for (const auto& identity : dependency.definitions) {
          if (identity.key() == stableCanonical.definition() &&
              identity.handle() ==
                  importedTarget.get<binder::DefinitionBindingTarget>().definition) {
            matchingCanonicalTarget = true;
          }
        }
      }
      if (!matchingCanonicalTarget) { return zc::none; }
      zc::Maybe<ast::NodeId> node;
      for (const auto& entry : provenance.entries()) {
        if (entry.path != stableExport.exportPath()) { continue; }
        if (node != zc::none) { return zc::none; }
        node = entry.node;
      }
      if (node == zc::none || !tree.contains(ZC_ASSERT_NONNULL(node)) ||
          tree.node(ZC_ASSERT_NONNULL(node)).kind != ast::SyntaxKind::ExportDeclaration) {
        return zc::none;
      }
      auto exportSpan =
          sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), ZC_ASSERT_NONNULL(node));
      if (exportSpan == zc::none) { return zc::none; }
      const binder::ExportSurfaceEntry* sourceEntry = nullptr;
      for (const auto& dependency : dependencies) {
        if (dependency.module != ZC_ASSERT_NONNULL(imported).sourceModule ||
            dependency.surface.revision().digest() !=
                ZC_ASSERT_NONNULL(imported).sourceRevision.digest()) {
          continue;
        }
        for (const auto& entry : dependency.surface.exports()) {
          if (!sameMaterializedImportTarget(entry.canonicalTarget,
                                            ZC_ASSERT_NONNULL(imported).canonicalTarget)) {
            continue;
          }
          if (sourceEntry != nullptr) { return zc::none; }
          sourceEntry = &entry;
        }
      }
      if (sourceEntry == nullptr || stableExport.reexportChain().values().size() != 1 ||
          ZC_ASSERT_NONNULL(imported).reexportChain.size() != 1) {
        return zc::none;
      }
      zc::Maybe<identity::SourceSpan> aliasSpan;
      ZC_IF_SOME(value, ZC_ASSERT_NONNULL(imported).aliasSpan) { aliasSpan = value.clone(); }
      zc::Vector<binder::ReexportProvenanceStep> chain;
      chain.add(ZC_ASSERT_NONNULL(imported).reexportChain[0].clone());
      result.add(binder::LocalExportFact{
          ZC_ASSERT_NONNULL(node), binder::BindingTarget::semanticImport(importBinding.clone()),
          ZC_ASSERT_NONNULL(imported).canonicalTarget.clone(),
          ZC_ASSERT_NONNULL(imported).declarationSpan.clone(),
          sourceEntry->canonicalDeclarationSpan.clone(), zc::mv(aliasSpan),
          zc::mv(ZC_ASSERT_NONNULL(exportSpan)), zc::mv(chain)});
      continue;
    }
    if (!binding.is<binder::StableDefinitionBindingTarget>()) { return zc::none; }
    const auto& bindingDefinition = binding.get<binder::StableDefinitionBindingTarget>().definition;
    const auto& canonicalDefinition =
        canonical.get<binder::StableDefinitionBindingTarget>().definition;
    if (bindingDefinition != canonicalDefinition ||
        bindingDefinition.module().encode().asPtr() !=
            identities.stableWitness().module().encode().asPtr()) {
      return zc::none;
    }
    zc::Maybe<identity::DefId> definitionIdentity;
    for (const auto& identity : identities.definitions()) {
      if (identity.key() != bindingDefinition.definition()) { continue; }
      if (definitionIdentity != zc::none) { return zc::none; }
      definitionIdentity = identity.handle();
    }
    zc::Maybe<const binder::DefinitionFact&> definition;
    for (const auto& candidate : definitions) {
      if (candidate.identity != definitionIdentity) { continue; }
      if (definition != zc::none) { return zc::none; }
      definition = candidate;
    }
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenance.entries()) {
      if (entry.path != stableExport.exportPath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    if (definitionIdentity == zc::none || definition == zc::none || node == zc::none ||
        !tree.contains(ZC_ASSERT_NONNULL(node)) ||
        tree.node(ZC_ASSERT_NONNULL(node)).kind != ast::SyntaxKind::ExportDeclaration) {
      return zc::none;
    }
    const auto& exportSyntax = tree.node(ZC_ASSERT_NONNULL(node));
    const auto& definitionSite = ZC_ASSERT_NONNULL(definition).site.value();
    const ast::NodeId declaration(
        exportSyntax.payload.words[ast::kExportDeclarationDeclarationWord]);
    const ast::NodeList specifiers{
        exportSyntax.payload.words[ast::kExportDeclarationSpecifiersFirstWord],
        exportSyntax.payload.words[ast::kExportDeclarationSpecifiersSizeWord]};
    if (!definitionSite.is<binder::DeclarationDefinitionSite>() || !tree.contains(specifiers)) {
      return zc::none;
    }
    auto exportSpan =
        sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), ZC_ASSERT_NONNULL(node));
    if (exportSpan == zc::none) { return zc::none; }
    zc::Maybe<identity::SourceSpan> aliasSpan;
    zc::Vector<binder::ReexportProvenanceStep> reexportChain;
    const auto stableChain = stableExport.reexportChain().values();
    if (stableChain.size() == 0) {
      if (!tree.contains(declaration) ||
          definitionSite.get<binder::DeclarationDefinitionSite>().node != declaration) {
        return zc::none;
      }
    } else if (stableChain.size() == 1) {
      const auto& stableStep = stableChain[0];
      if (stableStep.module().encode().asPtr() !=
              identities.stableWitness().module().encode().asPtr() ||
          stableStep.exportPath() != stableExport.exportPath() ||
          stableStep.binding() != stableExport.binding() ||
          stableStep.canonicalTarget() != stableExport.canonicalTarget()) {
        return zc::none;
      }
      bool exactSpecifier = false;
      zc::Maybe<identity::SourceSpan> matchedAliasSpan;
      for (const auto specifier : tree.list(specifiers)) {
        if (!tree.contains(specifier) ||
            tree.node(specifier).kind != ast::SyntaxKind::ExportSpecifier) {
          return zc::none;
        }
        const auto& specifierSyntax = tree.node(specifier);
        const ast::IdentId sourceIdentifier(
            specifierSyntax.payload.words[ast::kExportSpecifierNameWord]);
        const ast::IdentId aliasIdentifier(
            specifierSyntax.payload.words[ast::kExportSpecifierAliasWord]);
        if (!sourceIdentifier) { return zc::none; }
        const auto sourceName = tree.ident(sourceIdentifier);
        const auto exportedName = tree.ident(aliasIdentifier ? aliasIdentifier : sourceIdentifier);
        if (sourceName != ZC_ASSERT_NONNULL(definition).name.text() ||
            exportedName != stableExport.name().name().text()) {
          continue;
        }
        if (exactSpecifier) { return zc::none; }
        if (aliasIdentifier) {
          auto candidateAliasSpan =
              identifierTokenSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), specifier, 1);
          if (candidateAliasSpan == zc::none) { return zc::none; }
          matchedAliasSpan = zc::mv(ZC_ASSERT_NONNULL(candidateAliasSpan));
        }
        exactSpecifier = true;
      }
      if (!exactSpecifier) { return zc::none; }
      aliasSpan = zc::mv(matchedAliasSpan);
      reexportChain.add(binder::ReexportProvenanceStep{
          identities.module(),
          binder::BindingTarget::definition(ZC_ASSERT_NONNULL(definitionIdentity)),
          binder::BindingTarget::definition(ZC_ASSERT_NONNULL(definitionIdentity)),
          ZC_ASSERT_NONNULL(exportSpan).clone()});
    } else if (stableChain.size() != 0) {
      return zc::none;
    }
    result.add(binder::LocalExportFact{
        ZC_ASSERT_NONNULL(node),
        binder::BindingTarget::definition(ZC_ASSERT_NONNULL(definitionIdentity)),
        binder::BindingTarget::definition(ZC_ASSERT_NONNULL(definitionIdentity)),
        ZC_ASSERT_NONNULL(definition).source.clone(), ZC_ASSERT_NONNULL(definition).source.clone(),
        zc::mv(aliasSpan), zc::mv(ZC_ASSERT_NONNULL(exportSpan)), zc::mv(reexportChain)});
  }
  return result;
}

ast::NodeId genericBinderForDefinition(const ast::Tree& tree, ast::NodeId node) {
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

ast::NodeId genericBinderForImplementation(const ast::Tree& tree, ast::NodeId node) {
  if (!tree.contains(node) || tree.node(node).kind != ast::SyntaxKind::StandaloneImplDecl) {
    return {};
  }
  return ast::NodeId(tree.node(node).payload.words[ast::kStandaloneImplDeclTypeParamsIdWord]);
}

zc::Maybe<ast::NodeId> genericParameterNode(const ast::Tree& tree, ast::NodeId binder,
                                            uint32_t ordinal) {
  if (!tree.contains(binder) || tree.node(binder).kind != ast::SyntaxKind::GenericParams) {
    return zc::none;
  }
  const auto& syntax = tree.node(binder);
  const ast::NodeList parameters{syntax.payload.words[ast::kGenericParamsParamsFirstWord],
                                 syntax.payload.words[ast::kGenericParamsParamsSizeWord]};
  if (!tree.contains(parameters) || ordinal >= parameters.size) { return zc::none; }
  const auto result = tree.list(parameters)[ordinal];
  if (!tree.contains(result) || tree.node(result).kind != ast::SyntaxKind::GenericTypeParam) {
    return zc::none;
  }
  return result;
}

zc::Maybe<ast::NodeId> callableParameterNode(const ast::Tree& tree, ast::NodeId owner,
                                             identity::CallableParameterPosition position) {
  if (!tree.contains(owner)) { return zc::none; }
  const auto& syntax = tree.node(owner);
  ast::NodeList parameters;
  switch (syntax.kind) {
    case ast::SyntaxKind::FunctionDecl: {
      const ast::NodeId list(syntax.payload.words[ast::kFunctionDeclParamsIdWord]);
      if (!tree.contains(list) || tree.node(list).kind != ast::SyntaxKind::FunctionParameterList) {
        return zc::none;
      }
      const auto& listSyntax = tree.node(list);
      parameters =
          ast::NodeList{listSyntax.payload.words[ast::kFunctionParameterListParamsFirstWord],
                        listSyntax.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      break;
    }
    case ast::SyntaxKind::MethodDecl: {
      const ast::NodeId list(syntax.payload.words[ast::kMethodDeclParamsIdWord]);
      if (!tree.contains(list) || tree.node(list).kind != ast::SyntaxKind::FunctionParameterList) {
        return zc::none;
      }
      const auto& listSyntax = tree.node(list);
      parameters =
          ast::NodeList{listSyntax.payload.words[ast::kFunctionParameterListParamsFirstWord],
                        listSyntax.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      break;
    }
    case ast::SyntaxKind::ConstructorDecl: {
      const ast::NodeId list(syntax.payload.words[ast::kConstructorDeclParamsIdWord]);
      if (!tree.contains(list) || tree.node(list).kind != ast::SyntaxKind::FunctionParameterList) {
        return zc::none;
      }
      const auto& listSyntax = tree.node(list);
      parameters =
          ast::NodeList{listSyntax.payload.words[ast::kFunctionParameterListParamsFirstWord],
                        listSyntax.payload.words[ast::kFunctionParameterListParamsSizeWord]};
      break;
    }
    case ast::SyntaxKind::ExternDecl:
      parameters = ast::NodeList{syntax.payload.words[ast::kExternDeclParamsFirstWord],
                                 syntax.payload.words[ast::kExternDeclParamsSizeWord]};
      break;
    default:
      return zc::none;
  }
  if (!tree.contains(parameters)) { return zc::none; }
  bool hasReceiver = false;
  if (syntax.kind == ast::SyntaxKind::MethodDecl && parameters.size != 0) {
    const auto first = tree.list(parameters)[0];
    if (!tree.contains(first) || tree.node(first).kind != ast::SyntaxKind::FunctionParameterDecl) {
      return zc::none;
    }
    const auto& firstSyntax = tree.node(first);
    hasReceiver =
        tree.ident(ast::IdentId(firstSyntax.payload.words[ast::kFunctionParameterDeclNameWord])) ==
        "this"_zc;
  }
  uint32_t index = 0;
  if (position.kind() == identity::CallableParameterPositionKind::Ordinary) {
    auto ordinal = position.ordinal();
    if (ordinal == zc::none) { return zc::none; }
    index = ZC_ASSERT_NONNULL(ordinal);
    if (hasReceiver) { ++index; }
  } else if (position.kind() == identity::CallableParameterPositionKind::Receiver) {
    if (!hasReceiver) { return zc::none; }
  } else {
    return zc::none;
  }
  if (index >= parameters.size) { return zc::none; }
  const auto result = tree.list(parameters)[index];
  if (!tree.contains(result) || tree.node(result).kind != ast::SyntaxKind::FunctionParameterDecl) {
    return zc::none;
  }
  return result;
}

zc::Maybe<zc::Vector<binder::GenericParameterFact>> materializeGenericParameters(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    const binder::RevisionLocalDefinitionSites& definitionSites,
    const binder::RevisionLocalImplementationSites& implementationSites,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource) {
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  const auto& tree = parsedSource.tree();
  const auto declarations = identities.stableWitness().genericParameterDeclarations().values();
  zc::Vector<binder::GenericParameterFact> result(declarations.size());
  for (const auto& declaration : declarations) {
    zc::Maybe<identity::GenericParameterId> parameter;
    for (const auto& entry : identities.genericParameters()) {
      if (entry.key() != declaration.queryKey().parameter()) { continue; }
      if (parameter != zc::none) { return zc::none; }
      parameter = entry.handle();
    }
    const auto& owner = declaration.record().owner();
    ast::NodeId ownerNode;
    bool definitionOwner = false;
    ZC_IF_SOME(definition, owner.definitionKey()) {
      auto node = definitionNodeFor(definitionSites, definition);
      if (node == zc::none) { return zc::none; }
      ownerNode = ZC_ASSERT_NONNULL(node);
      definitionOwner = true;
    } else {
      ZC_IF_SOME(implementation, owner.implKey()) {
        auto node = implementationNodeFor(implementationSites, implementation);
        if (node == zc::none) { return zc::none; }
        ownerNode = ZC_ASSERT_NONNULL(node);
      }
    }
    const auto binder = definitionOwner ? genericBinderForDefinition(tree, ownerNode)
                                        : genericBinderForImplementation(tree, ownerNode);
    auto node = genericParameterNode(tree, binder, declaration.record().ordinal());
    auto scope = scopeForStableOwner(identities.stableWitness(), scopeIdentities,
                                     declaration.declaringScope());
    auto span = node == zc::none ? zc::Maybe<identity::SourceSpan>()
                                 : sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot),
                                                     ZC_ASSERT_NONNULL(node));
    if (parameter == zc::none || node == zc::none || scope == zc::none || span == zc::none) {
      return zc::none;
    }
    result.add(binder::GenericParameterFact{
        ZC_ASSERT_NONNULL(parameter), binder::DefinitionSite::declaration(ZC_ASSERT_NONNULL(node)),
        declaration.name().clone(), ZC_ASSERT_NONNULL(scope), zc::mv(ZC_ASSERT_NONNULL(span))});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::CallableParameterFact>> materializeCallableParameters(
    const binder::MaterializedModuleSkeletonIdentities& identities,
    const binder::RevisionLocalDefinitionSites& definitionSites,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities, const identity::SourceFileKey& source,
    const parser::CanonicalParsedSource& parsedSource) {
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  const auto& tree = parsedSource.tree();
  const auto declarations = identities.stableWitness().callableParameterDeclarations().values();
  zc::Vector<binder::CallableParameterFact> result(declarations.size());
  for (const auto& declaration : declarations) {
    zc::Maybe<identity::CallableParameterId> parameter;
    for (const auto& entry : identities.callableParameters()) {
      if (entry.key() != declaration.queryKey().parameter()) { continue; }
      if (parameter != zc::none) { return zc::none; }
      parameter = entry.handle();
    }
    auto owner = definitionNodeFor(definitionSites, declaration.record().owner());
    auto node = owner == zc::none ? zc::Maybe<ast::NodeId>()
                                  : callableParameterNode(tree, ZC_ASSERT_NONNULL(owner),
                                                          declaration.record().position());
    auto scope = scopeForStableOwner(identities.stableWitness(), scopeIdentities,
                                     declaration.declaringScope());
    auto span = node == zc::none ? zc::Maybe<identity::SourceSpan>()
                                 : sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot),
                                                     ZC_ASSERT_NONNULL(node));
    if (parameter == zc::none || node == zc::none || scope == zc::none || span == zc::none) {
      return zc::none;
    }
    zc::Maybe<identity::DeclaredDefinitionName> name;
    ZC_IF_SOME(value, declaration.name()) { name = value.clone(); }
    result.add(binder::CallableParameterFact{
        ZC_ASSERT_NONNULL(parameter), binder::DefinitionSite::declaration(ZC_ASSERT_NONNULL(node)),
        zc::mv(name), ZC_ASSERT_NONNULL(scope), zc::mv(ZC_ASSERT_NONNULL(span)),
        declaration.record().position().kind() ==
            identity::CallableParameterPositionKind::Receiver});
  }
  return result;
}

zc::Vector<binder::GenericParameterFact> cloneGenericParameterFacts(
    zc::ArrayPtr<const binder::GenericParameterFact> facts) {
  zc::Vector<binder::GenericParameterFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(binder::GenericParameterFact{fact.identity, fact.site.clone(), fact.name.clone(),
                                            fact.declaringScope, fact.source.clone()});
  }
  return result;
}

zc::Vector<binder::CallableParameterFact> cloneCallableParameterFacts(
    zc::ArrayPtr<const binder::CallableParameterFact> facts) {
  zc::Vector<binder::CallableParameterFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<identity::DeclaredDefinitionName> name;
    ZC_IF_SOME(value, fact.name) { name = value.clone(); }
    result.add(binder::CallableParameterFact{fact.identity, fact.site.clone(), zc::mv(name),
                                             fact.declaringScope, fact.source.clone(),
                                             fact.receiver});
  }
  return result;
}

zc::Vector<binder::ModuleAliasBindingFact> cloneModuleAliasFacts(
    zc::ArrayPtr<const binder::ModuleAliasBindingFact> facts) {
  zc::Vector<binder::ModuleAliasBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(binder::ModuleAliasBindingFact{
        fact.node, fact.alias, fact.canonicalTarget,
        binder::ModuleAliasExportNamesRevision::fromDigest(fact.targetExportNamesRevision.digest()),
        fact.declarationSpan.clone(), fact.targetSpan.clone()});
  }
  return result;
}

zc::Vector<binder::LocalExportFact> cloneLocalExportFacts(
    zc::ArrayPtr<const binder::LocalExportFact> facts) {
  zc::Vector<binder::LocalExportFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, fact.aliasSpan) { aliasSpan = value.clone(); }
    zc::Vector<binder::ReexportProvenanceStep> reexportChain(fact.reexportChain.size());
    for (const auto& step : fact.reexportChain) { reexportChain.add(step.clone()); }
    result.add(binder::LocalExportFact{fact.node, fact.sourceBinding.clone(),
                                       fact.canonicalTarget.clone(), fact.bindingSpan.clone(),
                                       fact.canonicalDeclarationSpan.clone(), zc::mv(aliasSpan),
                                       fact.exportSpan.clone(), zc::mv(reexportChain)});
  }
  return result;
}

bool addDependencyDefinitionIdentity(
    zc::Vector<binder::MaterializedDefinitionIdentityEntry>& identities,
    const binder::MaterializedDefinitionIdentityEntry& candidate) {
  for (const auto& existing : identities) {
    if (existing.handle() != candidate.handle()) { continue; }
    return existing.key() == candidate.key() &&
           existing.record().encode().asPtr() == candidate.record().encode().asPtr();
  }
  identities.add(candidate.clone());
  return true;
}

bool collectDependencyDefinitionIdentities(
    zc::Vector<binder::MaterializedDefinitionIdentityEntry>& identities,
    const MaterializedModuleSkeleton& dependency) {
  for (const auto& identity : dependency.identities().definitions()) {
    if (!addDependencyDefinitionIdentity(identities, identity)) { return false; }
  }
  for (const auto& nested : dependency.dependencySkeletonLeases()) {
    if (!collectDependencyDefinitionIdentities(identities, nested.capability())) { return false; }
  }
  return true;
}

zc::Maybe<zc::Vector<binder::MaterializedDependencyExportSurface>> materializedDependencySurfaces(
    zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease> dependencies) {
  zc::Vector<binder::MaterializedDependencyExportSurface> result(dependencies.size());
  for (const auto& dependency : dependencies) {
    zc::Vector<binder::MaterializedDefinitionIdentityEntry> definitions;
    if (!collectDependencyDefinitionIdentities(definitions, dependency.capability())) {
      return zc::none;
    }
    result.add(binder::MaterializedDependencyExportSurface{
        dependency.capability().module().clone(), dependency.capability().identities().module(),
        dependency.capability().bindingSurface().clone(), zc::mv(definitions)});
  }
  return result;
}

zc::Vector<binder::ImportBindingFact> cloneImportBindingFacts(
    zc::ArrayPtr<const binder::ImportBindingFact> facts) {
  zc::Vector<binder::ImportBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Maybe<identity::SourceSpan> aliasSpan;
    ZC_IF_SOME(value, fact.aliasSpan) { aliasSpan = value.clone(); }
    zc::Vector<binder::ReexportProvenanceStep> reexportChain(fact.reexportChain.size());
    for (const auto& step : fact.reexportChain) { reexportChain.add(step.clone()); }
    result.add(binder::ImportBindingFact{
        fact.node, fact.binding.clone(), fact.canonicalTarget.clone(), fact.sourceModule,
        binder::ExportSurfaceRevision::fromDigest(fact.sourceRevision.digest()), fact.kind,
        fact.declarationSpan.clone(), zc::mv(aliasSpan), zc::mv(reexportChain)});
  }
  return result;
}

size_t requiredDefinitionProvenanceCount(const binder::BoundModuleSkeleton& skeleton) {
  size_t result = 0;
  for (const auto& declaration : skeleton.declarations().values()) {
    if (declaration.activation() == binder::DefinitionActivation::ModuleSkeleton) { ++result; }
  }
  return result;
}

bool sameModuleAliasFacts(zc::ArrayPtr<const binder::ModuleAliasBindingFact> left,
                          zc::ArrayPtr<const binder::ModuleAliasBindingFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node || left[index].alias != right[index].alias ||
        left[index].canonicalTarget != right[index].canonicalTarget ||
        left[index].targetExportNamesRevision.digest() !=
            right[index].targetExportNamesRevision.digest() ||
        !sameScopeSource(left[index].declarationSpan, right[index].declarationSpan) ||
        !sameScopeSource(left[index].targetSpan, right[index].targetSpan)) {
      return false;
    }
  }
  return true;
}

bool sameLocalExportFacts(zc::ArrayPtr<const binder::LocalExportFact> left,
                          zc::ArrayPtr<const binder::LocalExportFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node ||
        !sameScopeBindingTarget(left[index].sourceBinding, right[index].sourceBinding) ||
        !sameScopeBindingTarget(left[index].canonicalTarget, right[index].canonicalTarget) ||
        !sameScopeSource(left[index].bindingSpan, right[index].bindingSpan) ||
        !sameScopeSource(left[index].canonicalDeclarationSpan,
                         right[index].canonicalDeclarationSpan) ||
        !sameOptionalScopeSource(left[index].aliasSpan, right[index].aliasSpan) ||
        !sameScopeSource(left[index].exportSpan, right[index].exportSpan) ||
        left[index].reexportChain.size() != right[index].reexportChain.size()) {
      return false;
    }
    for (size_t chainIndex = 0; chainIndex < left[index].reexportChain.size(); ++chainIndex) {
      const auto& leftStep = left[index].reexportChain[chainIndex];
      const auto& rightStep = right[index].reexportChain[chainIndex];
      if (leftStep.module != rightStep.module ||
          !sameScopeBindingTarget(leftStep.bindingIdentity, rightStep.bindingIdentity) ||
          !sameScopeBindingTarget(leftStep.canonicalTarget, rightStep.canonicalTarget) ||
          !sameScopeSource(leftStep.exportSpan, rightStep.exportSpan)) {
        return false;
      }
    }
  }
  return true;
}

bool sameImportBindingFacts(zc::ArrayPtr<const binder::ImportBindingFact> left,
                            zc::ArrayPtr<const binder::ImportBindingFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node || left[index].binding != right[index].binding ||
        !sameScopeBindingTarget(left[index].canonicalTarget, right[index].canonicalTarget) ||
        left[index].sourceModule != right[index].sourceModule ||
        left[index].sourceRevision.digest() != right[index].sourceRevision.digest() ||
        left[index].kind != right[index].kind ||
        !sameScopeSource(left[index].declarationSpan, right[index].declarationSpan) ||
        !sameOptionalScopeSource(left[index].aliasSpan, right[index].aliasSpan) ||
        left[index].reexportChain.size() != right[index].reexportChain.size()) {
      return false;
    }
    for (size_t chainIndex = 0; chainIndex < left[index].reexportChain.size(); ++chainIndex) {
      const auto& leftStep = left[index].reexportChain[chainIndex];
      const auto& rightStep = right[index].reexportChain[chainIndex];
      if (leftStep.module != rightStep.module ||
          !sameScopeBindingTarget(leftStep.bindingIdentity, rightStep.bindingIdentity) ||
          !sameScopeBindingTarget(leftStep.canonicalTarget, rightStep.canonicalTarget) ||
          !sameScopeSource(leftStep.exportSpan, rightStep.exportSpan)) {
        return false;
      }
    }
  }
  return true;
}

bool isSkeletonFailedLookupOwner(const binder::BoundModuleSkeleton& stableWitness,
                                 const binder::BinderQueryOwner& owner) {
  const auto& value = owner.value();
  if (value.is<binder::BinderModuleQueryOwner>()) {
    return sameSkeletonModule(value.get<binder::BinderModuleQueryOwner>().module,
                              stableWitness.module());
  }
  if (value.is<binder::BinderDefinitionHeaderQueryOwner>()) {
    const auto& definition = value.get<binder::BinderDefinitionHeaderQueryOwner>().definition;
    for (const auto& declaration : stableWitness.declarations().values()) {
      if (declaration.queryKey() == definition) { return true; }
    }
    return false;
  }
  if (value.is<binder::BinderImplementationHeaderQueryOwner>()) {
    const auto& implementation =
        value.get<binder::BinderImplementationHeaderQueryOwner>().implementation;
    for (const auto& occurrence : stableWitness.implementationOccurrences().values()) {
      if (occurrence.occurrence() == implementation) { return true; }
    }
    return false;
  }
  return false;
}

zc::Maybe<zc::Vector<binder::MaterializedFailedLookupFact>> materializeSkeletonFailedLookups(
    const binder::ModuleBodyProvenance& provenance,
    const binder::BoundModuleSkeleton& stableWitness) {
  zc::Vector<binder::MaterializedFailedLookupFact> result(
      stableWitness.failedLookups().values().size());
  for (const auto& stable : stableWitness.failedLookups().values()) {
    if (!isSkeletonFailedLookupOwner(stableWitness, stable.owner())) { return zc::none; }
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenance.entries()) {
      if (entry.path != stable.usePath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    if (node == zc::none) { return zc::none; }
    result.add(binder::MaterializedFailedLookupFact{ZC_ASSERT_NONNULL(node), stable.nameSpace(),
                                                    stable.name().clone(),
                                                    stable.outcome().clone()});
  }
  return result;
}

zc::Vector<binder::MaterializedFailedLookupFact> cloneSkeletonFailedLookups(
    zc::ArrayPtr<const binder::MaterializedFailedLookupFact> lookups) {
  zc::Vector<binder::MaterializedFailedLookupFact> result(lookups.size());
  for (const auto& lookup : lookups) {
    result.add(binder::MaterializedFailedLookupFact{lookup.node, lookup.nameSpace,
                                                    lookup.name.clone(), lookup.outcome.clone()});
  }
  return result;
}

bool sameSkeletonFailedLookups(zc::ArrayPtr<const binder::MaterializedFailedLookupFact> left,
                               zc::ArrayPtr<const binder::MaterializedFailedLookupFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node || left[index].nameSpace != right[index].nameSpace ||
        left[index].name != right[index].name || left[index].outcome != right[index].outcome) {
      return false;
    }
  }
  return true;
}

struct MaterializedModuleSkeleton::Impl final {
  Impl(incremental_binding_query::ContextualModuleKey&& key, GraphLease&& graph,
       zc::Vector<DependencySkeletonLease>&& dependencySkeletons, identity::SourceFileKey&& source,
       binder::ModuleBodyProvenance&& provenance, DependencyProvenanceLease&& dependencyProvenance,
       binder::MaterializedModuleSkeletonIdentities&& identities,
       IdentityAdmissionLease&& identityAdmission,
       zc::Vector<binder::MaterializedDependencyExportSurface>&& dependencySurfaces,
       zc::Vector<binder::ScopeId>&& scopeIdentities, DefinitionSitesLease&& definitionSites,
       zc::Vector<binder::DefinitionFact>&& definitions,
       zc::Vector<binder::GenericParameterFact>&& genericParameters,
       zc::Vector<binder::CallableParameterFact>&& callableParameters,
       zc::Vector<binder::ModuleAliasBindingFact>&& moduleAliases,
       zc::Vector<binder::ImportBindingFact>&& imports,
       zc::Vector<binder::LocalExportFact>&& localExports,
       binder::VerifiedExportSurface&& bindingSurface,
       zc::Vector<binder::NodeScopeFact>&& nodeScopes,
       zc::Vector<binder::MaterializedFailedLookupFact>&& failedLookups,
       ImplementationSitesLease&& implementationSites,
       zc::Vector<binder::ImplBindingFact>&& implementations,
       zc::Vector<binder::ScopeRecord>&& scopes,
       zc::Vector<DefinitionProvenanceLease>&& definitionProvenances) noexcept
      : key(zc::mv(key)),
        graph(zc::mv(graph)),
        dependencySkeletons(zc::mv(dependencySkeletons)),
        source(zc::mv(source)),
        provenance(zc::mv(provenance)),
        dependencyProvenance(zc::mv(dependencyProvenance)),
        identities(zc::mv(identities)),
        identityAdmission(zc::mv(identityAdmission)),
        dependencySurfaces(zc::mv(dependencySurfaces)),
        scopeIdentities(zc::mv(scopeIdentities)),
        definitionSites(zc::mv(definitionSites)),
        definitions(zc::mv(definitions)),
        genericParameters(zc::mv(genericParameters)),
        callableParameters(zc::mv(callableParameters)),
        moduleAliases(zc::mv(moduleAliases)),
        imports(zc::mv(imports)),
        localExports(zc::mv(localExports)),
        bindingSurface(zc::mv(bindingSurface)),
        nodeScopes(zc::mv(nodeScopes)),
        failedLookups(zc::mv(failedLookups)),
        implementationSites(zc::mv(implementationSites)),
        implementations(zc::mv(implementations)),
        scopes(zc::mv(scopes)),
        definitionProvenances(zc::mv(definitionProvenances)) {}

  incremental_binding_query::ContextualModuleKey key;
  GraphLease graph;
  zc::Vector<DependencySkeletonLease> dependencySkeletons;
  identity::SourceFileKey source;
  binder::ModuleBodyProvenance provenance;
  DependencyProvenanceLease dependencyProvenance;
  binder::MaterializedModuleSkeletonIdentities identities;
  IdentityAdmissionLease identityAdmission;
  zc::Vector<binder::MaterializedDependencyExportSurface> dependencySurfaces;
  zc::Vector<binder::ScopeId> scopeIdentities;
  DefinitionSitesLease definitionSites;
  zc::Vector<binder::DefinitionFact> definitions;
  zc::Vector<binder::GenericParameterFact> genericParameters;
  zc::Vector<binder::CallableParameterFact> callableParameters;
  zc::Vector<binder::ModuleAliasBindingFact> moduleAliases;
  zc::Vector<binder::ImportBindingFact> imports;
  zc::Vector<binder::LocalExportFact> localExports;
  binder::VerifiedExportSurface bindingSurface;
  zc::Vector<binder::NodeScopeFact> nodeScopes;
  zc::Vector<binder::MaterializedFailedLookupFact> failedLookups;
  ImplementationSitesLease implementationSites;
  zc::Vector<binder::ImplBindingFact> implementations;
  zc::Vector<binder::ScopeRecord> scopes;
  zc::Vector<DefinitionProvenanceLease> definitionProvenances;
};

MaterializedModuleSkeleton::MaterializedModuleSkeleton(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
MaterializedModuleSkeleton::~MaterializedModuleSkeleton() noexcept(false) = default;
MaterializedModuleSkeleton::MaterializedModuleSkeleton(MaterializedModuleSkeleton&&) noexcept =
    default;
MaterializedModuleSkeleton& MaterializedModuleSkeleton::operator=(
    MaterializedModuleSkeleton&&) noexcept = default;

zc::Maybe<zc::Vector<binder::ScopeId>> MaterializedModuleSkeleton::materializeScopeIdentities(
    identity::ModuleId module, const binder::BoundModuleSkeleton& stableWitness) {
  const auto scopes = stableWitness.scopes().values();
  if (scopes.size() > static_cast<size_t>(0xffffffffu)) { return zc::none; }
  zc::Vector<zc::Maybe<uint32_t>> depths;
  depths.resize(scopes.size());
  zc::Vector<uint8_t> states;
  states.resize(scopes.size());
  for (auto& state : states) { state = 0; }
  auto depthFor = [&](auto&& self, size_t index) -> zc::Maybe<uint32_t> {
    if (index >= scopes.size() || states[index] == 1) { return zc::none; }
    ZC_IF_SOME(value, depths[index]) { return value; }
    states[index] = 1;
    uint32_t depth = 0;
    ZC_IF_SOME(parent, scopes[index].parent()) {
      zc::Maybe<size_t> parentIndex;
      for (size_t candidate = 0; candidate < scopes.size(); ++candidate) {
        if (scopes[candidate].owner() != parent) { continue; }
        if (parentIndex != zc::none) { return zc::none; }
        parentIndex = candidate;
      }
      if (parentIndex == zc::none) { return zc::none; }
      auto parentDepth = self(self, ZC_ASSERT_NONNULL(parentIndex));
      if (parentDepth == zc::none || ZC_ASSERT_NONNULL(parentDepth) == 0xffffffffu) {
        return zc::none;
      }
      depth = ZC_ASSERT_NONNULL(parentDepth) + 1;
    }
    states[index] = 2;
    depths[index] = depth;
    return depth;
  };
  zc::Vector<size_t> order(scopes.size());
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (depthFor(depthFor, index) == zc::none) { return zc::none; }
    order.add(index);
  }
  for (size_t index = 1; index < order.size(); ++index) {
    const auto current = order[index];
    size_t insertion = index;
    while (insertion != 0) {
      const auto previous = order[insertion - 1];
      const auto currentDepth = ZC_ASSERT_NONNULL(depths[current]);
      const auto previousDepth = ZC_ASSERT_NONNULL(depths[previous]);
      const auto currentBytes =
          binder::StableBindingCodec<binder::StableScopeOwnerKey>::encode(scopes[current].owner());
      const auto previousBytes =
          binder::StableBindingCodec<binder::StableScopeOwnerKey>::encode(scopes[previous].owner());
      if (currentDepth > previousDepth ||
          (currentDepth == previousDepth &&
           compareBytes(currentBytes.asPtr(), previousBytes.asPtr()) >= 0)) {
        break;
      }
      order[insertion] = previous;
      --insertion;
    }
    order[insertion] = current;
  }
  zc::Vector<zc::Maybe<binder::ScopeId>> slots;
  slots.resize(scopes.size());
  for (size_t index = 0; index < order.size(); ++index) {
    slots[order[index]] = binder::ScopeId(module, static_cast<uint32_t>(index));
  }
  zc::Vector<binder::ScopeId> identities(scopes.size());
  for (size_t index = 0; index < slots.size(); ++index) {
    if (slots[index] == zc::none) { return zc::none; }
    identities.add(ZC_ASSERT_NONNULL(slots[index]));
  }
  return identities;
}

zc::Maybe<MaterializedModuleSkeleton> MaterializedModuleSkeleton::from(
    incremental_binding_query::ContextualModuleKey&& key, GraphLease&& graph,
    zc::Vector<DependencySkeletonLease>&& dependencySkeletons, identity::SourceFileKey&& source,
    binder::ModuleBodyProvenance&& provenance, DependencyProvenanceLease&& dependencyProvenance,
    binder::MaterializedModuleSkeletonIdentities&& identities,
    IdentityAdmissionLease&& identityAdmission, DefinitionSitesLease&& definitionSites,
    ImplementationSitesLease&& implementationSites,
    zc::Vector<DefinitionProvenanceLease>&& definitionProvenances,
    const parser::CanonicalParsedSource& parsedSource) {
  if (!identities.context().isValid() || identities.revision().value() == 0 ||
      graph.revision() != identities.revision() ||
      graph.capability().context() != identities.context() ||
      !graphContainsModule(graph.capability(), key.module()) ||
      !sameSkeletonModule(key.module(), identities.stableWitness().module()) ||
      !source.belongsTo(key.module().crate()) ||
      provenance.source().encode().asPtr() != source.encode().asPtr() ||
      dependencyProvenance.revision() != identities.revision() ||
      dependencyProvenance.capability().module().encode().asPtr() !=
          key.module().encode().asPtr() ||
      dependencyProvenance.capability().source().encode().asPtr() != source.encode().asPtr()) {
    return zc::none;
  }
  if (identityAdmission.revision() != identities.revision() ||
      identityAdmission.capability().module().encode().asPtr() != key.module().encode().asPtr() ||
      identityAdmission.capability().source().encode().asPtr() != source.encode().asPtr()) {
    return zc::none;
  }
  for (const auto& dependency : dependencySkeletons) {
    if (dependency.revision() != identities.revision() ||
        dependency.capability().contextRoots() != key.contextRoots() ||
        dependency.capability().context() != identities.context() ||
        dependency.capability().module().encode().asPtr() == key.module().encode().asPtr() ||
        dependency.capability().graphLease().capability().witness().encodeCanonical().asPtr() !=
            graph.capability().witness().encodeCanonical().asPtr()) {
      return zc::none;
    }
  }
  size_t dependencyIndex = 0;
  for (const auto& edge : graph.capability().requestEdges()) {
    auto requester = materializedModuleForHandle(graph.capability(), edge.requester());
    auto dependency = materializedModuleForHandle(graph.capability(), edge.dependency());
    if (requester == zc::none || dependency == zc::none) { return zc::none; }
    if (!sameSkeletonModule(ZC_ASSERT_NONNULL(requester).key(), key.module())) { continue; }
    if (containsDependencySkeleton(dependencySkeletons.asPtr().first(dependencyIndex),
                                   ZC_ASSERT_NONNULL(dependency).key())) {
      continue;
    }
    if (dependencyIndex >= dependencySkeletons.size() ||
        dependencySkeletons[dependencyIndex].capability().module().encode().asPtr() !=
            ZC_ASSERT_NONNULL(dependency).key().encode().asPtr()) {
      return zc::none;
    }
    ++dependencyIndex;
  }
  if (dependencyIndex != dependencySkeletons.size()) { return zc::none; }
  auto scopeIdentities =
      materializeScopeIdentities(identities.module(), identities.stableWitness());
  if (scopeIdentities == zc::none || definitionSites.revision() != identities.revision() ||
      implementationSites.revision() != identities.revision() ||
      definitionProvenances.size() !=
          requiredDefinitionProvenanceCount(identities.stableWitness())) {
    return zc::none;
  }
  auto definitions =
      materializeDefinitionFacts(identities, definitionSites.capability(),
                                 ZC_ASSERT_NONNULL(scopeIdentities).asPtr(), source, parsedSource);
  auto genericParameters = materializeGenericParameters(
      identities, definitionSites.capability(), implementationSites.capability(),
      ZC_ASSERT_NONNULL(scopeIdentities).asPtr(), source, parsedSource);
  auto callableParameters = materializeCallableParameters(
      identities, definitionSites.capability(), ZC_ASSERT_NONNULL(scopeIdentities).asPtr(), source,
      parsedSource);
  auto nodeScopes = materializeSkeletonNodeScopes(provenance, identities,
                                                  ZC_ASSERT_NONNULL(scopeIdentities).asPtr());
  auto failedLookups = materializeSkeletonFailedLookups(provenance, identities.stableWitness());
  auto implementations = materializeImplementationBindings(
      identities, implementationSites.capability(), ZC_ASSERT_NONNULL(scopeIdentities).asPtr(),
      source, parsedSource);
  auto moduleAliases =
      definitions == zc::none
          ? zc::Maybe<zc::Vector<binder::ModuleAliasBindingFact>>()
          : materializeModuleAliases(graph.capability(), dependencySkeletons.asPtr(), identities,
                                     ZC_ASSERT_NONNULL(definitions).asPtr(), source, parsedSource);
  auto imports = materializedImportCount(identities.stableWitness()) == 0
                     ? zc::Maybe<zc::Vector<binder::ImportBindingFact>>(
                           zc::Vector<binder::ImportBindingFact>())
                     : materializeImports(identities, dependencySkeletons.asPtr(),
                                          graph.capability(), dependencyProvenance.capability());
  auto dependencySurfaces = materializedDependencySurfaces(dependencySkeletons.asPtr());
  auto localExports =
      definitions == zc::none ? zc::Maybe<zc::Vector<binder::LocalExportFact>>()
      : imports == zc::none
          ? zc::Maybe<zc::Vector<binder::LocalExportFact>>()
          : materializeLocalExports(identities, ZC_ASSERT_NONNULL(definitions).asPtr(),
                                    ZC_ASSERT_NONNULL(imports).asPtr(),
                                    ZC_ASSERT_NONNULL(dependencySurfaces).asPtr(), provenance,
                                    source, parsedSource);
  auto scopes =
      definitions == zc::none || genericParameters == zc::none || callableParameters == zc::none ||
              implementations == zc::none || moduleAliases == zc::none || imports == zc::none ||
              localExports == zc::none
          ? zc::Maybe<zc::Vector<binder::ScopeRecord>>()
          : materializeScopeRecords(identities, ZC_ASSERT_NONNULL(scopeIdentities).asPtr(),
                                    ZC_ASSERT_NONNULL(definitions).asPtr(),
                                    ZC_ASSERT_NONNULL(genericParameters).asPtr(),
                                    ZC_ASSERT_NONNULL(callableParameters).asPtr(),
                                    ZC_ASSERT_NONNULL(implementations).asPtr(),
                                    ZC_ASSERT_NONNULL(moduleAliases).asPtr(),
                                    ZC_ASSERT_NONNULL(imports).asPtr(), source, parsedSource);
  auto compilationUnit = materializedSkeletonCompilationUnit(graph.capability(), key.module());
  auto bindingSurface =
      definitions == zc::none || imports == zc::none || localExports == zc::none ||
              compilationUnit == zc::none
          ? zc::Maybe<binder::VerifiedExportSurface>()
          : binder::MaterializedExportSurfaceVerifier::from(
                identities.context(), identities.fingerprint(), key.module(), identities.module(),
                ZC_ASSERT_NONNULL(compilationUnit).key(),
                ZC_ASSERT_NONNULL(compilationUnit).handle(), source, identities.stableWitness(),
                identities.definitions(), ZC_ASSERT_NONNULL(definitions).asPtr(),
                ZC_ASSERT_NONNULL(dependencySurfaces).asPtr(), ZC_ASSERT_NONNULL(imports).asPtr(),
                ZC_ASSERT_NONNULL(localExports).asPtr());
  if (definitions == zc::none || genericParameters == zc::none || callableParameters == zc::none ||
      nodeScopes == zc::none || implementations == zc::none || moduleAliases == zc::none ||
      imports == zc::none || dependencySurfaces == zc::none || localExports == zc::none ||
      scopes == zc::none || failedLookups == zc::none || bindingSurface == zc::none) {
    return zc::none;
  }
  size_t provenanceIndex = 0;
  for (const auto& declaration : identities.stableWitness().declarations().values()) {
    if (declaration.activation() != binder::DefinitionActivation::ModuleSkeleton) { continue; }
    const auto expectedKey = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(), declaration.queryKey().clone());
    if (provenanceIndex >= definitionProvenances.size() ||
        definitionProvenances[provenanceIndex].revision() != identities.revision() ||
        definitionProvenances[provenanceIndex].key().canonicalBytes() !=
            expectedKey.encodeCanonical().asPtr() ||
        definitionProvenances[provenanceIndex]
                .capability()
                .detachedProvenance()
                .source()
                .encode()
                .asPtr() != source.encode().asPtr()) {
      return zc::none;
    }
    ++provenanceIndex;
  }
  if (provenanceIndex != definitionProvenances.size()) { return zc::none; }
  return MaterializedModuleSkeleton(zc::heap<Impl>(
      zc::mv(key), zc::mv(graph), zc::mv(dependencySkeletons), zc::mv(source), zc::mv(provenance),
      zc::mv(dependencyProvenance), zc::mv(identities), zc::mv(identityAdmission),
      zc::mv(ZC_ASSERT_NONNULL(dependencySurfaces)), zc::mv(ZC_ASSERT_NONNULL(scopeIdentities)),
      zc::mv(definitionSites), zc::mv(ZC_ASSERT_NONNULL(definitions)),
      zc::mv(ZC_ASSERT_NONNULL(genericParameters)), zc::mv(ZC_ASSERT_NONNULL(callableParameters)),
      zc::mv(ZC_ASSERT_NONNULL(moduleAliases)), zc::mv(ZC_ASSERT_NONNULL(imports)),
      zc::mv(ZC_ASSERT_NONNULL(localExports)), zc::mv(ZC_ASSERT_NONNULL(bindingSurface)),
      zc::mv(ZC_ASSERT_NONNULL(nodeScopes)), zc::mv(ZC_ASSERT_NONNULL(failedLookups)),
      zc::mv(implementationSites), zc::mv(ZC_ASSERT_NONNULL(implementations)),
      zc::mv(ZC_ASSERT_NONNULL(scopes)), zc::mv(definitionProvenances)));
}

MaterializedModuleSkeleton MaterializedModuleSkeleton::clone() const {
  auto scopeIdentities =
      materializeScopeIdentities(impl->identities.module(), impl->identities.stableWitness());
  auto nodeScopes = materializeSkeletonNodeScopes(impl->provenance, impl->identities,
                                                  ZC_ASSERT_NONNULL(scopeIdentities).asPtr());
  zc::Vector<DefinitionProvenanceLease> definitionProvenances;
  for (const auto& provenance : impl->definitionProvenances) {
    definitionProvenances.add(provenance.retain());
  }
  zc::Vector<DependencySkeletonLease> dependencySkeletons;
  for (const auto& dependency : impl->dependencySkeletons) {
    dependencySkeletons.add(dependency.retain());
  }
  auto dependencySurfaces = materializedDependencySurfaces(dependencySkeletons.asPtr());
  return MaterializedModuleSkeleton(zc::heap<Impl>(
      impl->key.clone(), impl->graph.retain(), zc::mv(dependencySkeletons), impl->source.clone(),
      impl->provenance.clone(), impl->dependencyProvenance.retain(), impl->identities.clone(),
      impl->identityAdmission.retain(), zc::mv(ZC_ASSERT_NONNULL(dependencySurfaces)),
      zc::mv(ZC_ASSERT_NONNULL(scopeIdentities)), impl->definitionSites.retain(),
      cloneDefinitionFacts(impl->definitions.asPtr()),
      cloneGenericParameterFacts(impl->genericParameters.asPtr()),
      cloneCallableParameterFacts(impl->callableParameters.asPtr()),
      cloneModuleAliasFacts(impl->moduleAliases.asPtr()),
      cloneImportBindingFacts(impl->imports.asPtr()),
      cloneLocalExportFacts(impl->localExports.asPtr()), impl->bindingSurface.clone(),
      zc::mv(ZC_ASSERT_NONNULL(nodeScopes)),
      cloneSkeletonFailedLookups(impl->failedLookups.asPtr()), impl->implementationSites.retain(),
      cloneImplementationBindings(impl->implementations.asPtr()),
      cloneScopeRecords(impl->scopes.asPtr()), zc::mv(definitionProvenances)));
}

const incremental_binding_query::CompilationRootSetQueryKey&
MaterializedModuleSkeleton::contextRoots() const noexcept {
  return impl->key.contextRoots();
}

const identity::ModuleKey& MaterializedModuleSkeleton::module() const noexcept {
  return impl->key.module();
}

identity::SemanticContextBrand MaterializedModuleSkeleton::context() const noexcept {
  return impl->identities.context();
}

query::DatabaseRevision MaterializedModuleSkeleton::revision() const noexcept {
  return impl->identities.revision();
}

const identity::ContextFingerprint& MaterializedModuleSkeleton::fingerprint()
    const noexcept {
  return impl->identities.fingerprint();
}

const identity::SourceFileKey& MaterializedModuleSkeleton::source() const noexcept {
  return impl->source;
}

const binder::ModuleBodyProvenance& MaterializedModuleSkeleton::provenance() const noexcept {
  return impl->provenance;
}

const MaterializedModuleSkeleton::DependencyProvenanceLease&
MaterializedModuleSkeleton::dependencyProvenanceLease() const noexcept {
  return impl->dependencyProvenance;
}

const binder::MaterializedModuleSkeletonIdentities& MaterializedModuleSkeleton::identities()
    const noexcept {
  return impl->identities;
}

const MaterializedModuleSkeleton::IdentityAdmissionLease&
MaterializedModuleSkeleton::identityAdmissionLease() const noexcept {
  return impl->identityAdmission;
}

const MaterializedModuleSkeleton::GraphLease& MaterializedModuleSkeleton::graphLease()
    const noexcept {
  return impl->graph;
}

zc::ArrayPtr<const MaterializedModuleSkeleton::DependencySkeletonLease>
MaterializedModuleSkeleton::dependencySkeletonLeases() const noexcept {
  return impl->dependencySkeletons.asPtr();
}

zc::ArrayPtr<const binder::MaterializedDependencyExportSurface>
MaterializedModuleSkeleton::dependencySurfaces() const noexcept {
  return impl->dependencySurfaces.asPtr();
}

zc::Maybe<const binder::MaterializedDependencyExportSurface&>
MaterializedModuleSkeleton::preludeSurface() const noexcept {
  zc::Maybe<const binder::MaterializedDependencyExportSurface&> result;
  for (const auto& edge : impl->graph.capability().requestEdges()) {
    if (edge.requester() != impl->identities.module() ||
        edge.request().dependencyKind() != identity::ModuleDependencyKind::Prelude) {
      continue;
    }
    for (const auto& surface : impl->dependencySurfaces) {
      if (surface.module != edge.dependency()) { continue; }
      if (result != zc::none) { return zc::none; }
      result = surface;
    }
  }
  return result;
}

const MaterializedModuleSkeleton::DefinitionSitesLease&
MaterializedModuleSkeleton::definitionSitesLease() const noexcept {
  return impl->definitionSites;
}

const MaterializedModuleSkeleton::ImplementationSitesLease&
MaterializedModuleSkeleton::implementationSitesLease() const noexcept {
  return impl->implementationSites;
}

zc::ArrayPtr<const MaterializedModuleSkeleton::DefinitionProvenanceLease>
MaterializedModuleSkeleton::definitionProvenanceLeases() const noexcept {
  return impl->definitionProvenances.asPtr();
}

zc::ArrayPtr<const binder::DefinitionFact> MaterializedModuleSkeleton::materializedDefinitions()
    const noexcept {
  return impl->definitions.asPtr();
}

zc::ArrayPtr<const binder::GenericParameterFact>
MaterializedModuleSkeleton::materializedGenericParameters() const noexcept {
  return impl->genericParameters.asPtr();
}

zc::ArrayPtr<const binder::CallableParameterFact>
MaterializedModuleSkeleton::materializedCallableParameters() const noexcept {
  return impl->callableParameters.asPtr();
}

zc::ArrayPtr<const binder::ModuleAliasBindingFact>
MaterializedModuleSkeleton::materializedModuleAliases() const noexcept {
  return impl->moduleAliases.asPtr();
}

zc::ArrayPtr<const binder::LocalExportFact> MaterializedModuleSkeleton::materializedLocalExports()
    const noexcept {
  return impl->localExports.asPtr();
}

zc::ArrayPtr<const binder::ImportBindingFact> MaterializedModuleSkeleton::materializedImports()
    const noexcept {
  return impl->imports.asPtr();
}

const binder::VerifiedExportSurface& MaterializedModuleSkeleton::bindingSurface() const noexcept {
  return impl->bindingSurface;
}

zc::ArrayPtr<const binder::NodeScopeFact> MaterializedModuleSkeleton::materializedNodeScopes()
    const noexcept {
  return impl->nodeScopes.asPtr();
}

zc::ArrayPtr<const binder::MaterializedFailedLookupFact>
MaterializedModuleSkeleton::materializedFailedLookups() const noexcept {
  return impl->failedLookups.asPtr();
}

zc::ArrayPtr<const binder::ImplBindingFact>
MaterializedModuleSkeleton::materializedImplementations() const noexcept {
  return impl->implementations.asPtr();
}

zc::ArrayPtr<const binder::ScopeRecord> MaterializedModuleSkeleton::materializedScopes()
    const noexcept {
  return impl->scopes.asPtr();
}

zc::ArrayPtr<const binder::ScopeId> MaterializedModuleSkeleton::scopeIdentities() const noexcept {
  return impl->scopeIdentities.asPtr();
}

zc::Maybe<binder::ScopeId> MaterializedModuleSkeleton::scopeFor(
    const binder::StableScopeOwnerKey& scope) const noexcept {
  const auto scopes = identities().stableWitness().scopes().values();
  if (scopes.size() != impl->scopeIdentities.size()) { return zc::none; }
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (scopes[index].owner() == scope) { return impl->scopeIdentities[index]; }
  }
  return zc::none;
}

zc::Array<uint8_t> MaterializedModuleSkeleton::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(contextRoots().encodeCanonical().asPtr());
  encoder.encodeByteString(module().encode().asPtr());
  encoder.encodeUint64(revision().value());
  encoder.encodeByteString(fingerprint().digest().bytes());
  encoder.encodeByteString(source().encode().asPtr());
  encoder.encodeByteString(provenance().encodeCanonical().asPtr());
  encoder.encodeByteString(graphLease().key().canonicalBytes());
  encoder.encodeByteString(graphLease().stableWitness());
  encoder.encodeUint64(dependencySkeletonLeases().size());
  for (const auto& dependency : dependencySkeletonLeases()) {
    encoder.encodeByteString(dependency.key().canonicalBytes());
    encoder.encodeByteString(dependency.stableWitness());
  }
  encoder.encodeByteString(dependencyProvenanceLease().key().canonicalBytes());
  encoder.encodeByteString(dependencyProvenanceLease().stableWitness());
  encoder.encodeByteString(identityAdmissionLease().key().canonicalBytes());
  encoder.encodeByteString(identityAdmissionLease().stableWitness());
  auto stable =
      binder::StableBindingCodec<binder::BoundModuleSkeleton>::encode(identities().stableWitness());
  encoder.encodeByteString(stable.asPtr());
  encoder.encodeDigest(bindingSurface().revision().digest());
  encoder.encodeByteString(definitionSitesLease().key().canonicalBytes());
  encoder.encodeByteString(definitionSitesLease().stableWitness());
  encoder.encodeByteString(implementationSitesLease().key().canonicalBytes());
  encoder.encodeByteString(implementationSitesLease().stableWitness());
  encoder.encodeUint64(definitionProvenanceLeases().size());
  for (const auto& provenance : definitionProvenanceLeases()) {
    encoder.encodeByteString(provenance.key().canonicalBytes());
    encoder.encodeByteString(provenance.stableWitness());
  }
  return frame(kSkeletonWitnessDomain, encoder.finish().asPtr());
}

zc::Array<uint8_t> MaterializeModuleSkeletonQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<MaterializeModuleSkeletonQuery::Key> MaterializeModuleSkeletonQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return incremental_binding_query::ContextualModuleKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<MaterializeModuleSkeletonQuery>
MaterializeModuleSkeletonQuery::provide(
    query::CapabilityQueryContext<MaterializeModuleSkeletonQuery>& context, const Key& key) {
  auto selected = context.get<SelectedModuleSourceQuery>(key.module());
  if (selected.isRuntimeFailure()) {
    if (selected.runtimeFailure() != query::QueryRuntimeFailure::MissingInput) {
      return SkeletonProviderResult::runtimeRejected(selected.runtimeFailure());
    }
    auto failure = missingSelectedModuleSourceFailure(key.module());
    if (failure == zc::none) {
      return SkeletonProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return SkeletonProviderResult::keyRejected<binder::BinderKeyFailure>(
        zc::mv(ZC_ASSERT_NONNULL(failure)));
  }
  if (selected.kind() == query::QueryValueKind::Absence ||
      selected.kind() == query::QueryValueKind::SemanticFailure) {
    auto failure = missingSelectedModuleSourceFailure(key.module());
    if (failure == zc::none) {
      return SkeletonProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    return SkeletonProviderResult::keyRejected<binder::BinderKeyFailure>(
        zc::mv(ZC_ASSERT_NONNULL(failure)));
  }
  if (selected.kind() != query::QueryValueKind::Value) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }

  auto graph = context.getCapability<MaterializeModuleGraphQuery>(key.contextRoots().clone());
  if (graph.isSourceRejected()) { return forwardSkeletonSourceRejection(graph); }
  if (graph.isKeyRejected()) { return forwardSkeletonKeyRejection(graph); }
  if (graph.isRuntimeRejected()) {
    return SkeletonProviderResult::runtimeRejected(graph.runtimeFailure());
  }
  if (!graph.isPublished()) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& graphValue = graph.lease().capability();
  auto resources =
      context.template semanticContextResources<ModuleGraphIdentityMaterializationResources>();
  if (resources == zc::none ||
      graphValue.context() != ZC_ASSERT_NONNULL(resources).semanticContext() ||
      graphValue.revision() != context.snapshotRevision() ||
      !graphContainsModule(graphValue, key.module())) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto dependencies = acquireDependencySkeletons(context, key, graphValue);
  if (dependencies.is<SkeletonProviderResult>()) {
    return zc::mv(dependencies).get<SkeletonProviderResult>();
  }
  zc::Maybe<SkeletonProviderResult> rejection;
  auto skeleton = acquireSkeleton(context, key, rejection);
  if (rejection != zc::none) { return zc::mv(ZC_ASSERT_NONNULL(rejection)); }
  if (skeleton == zc::none ||
      !skeletonMembershipsMatch(context, key, ZC_ASSERT_NONNULL(skeleton))) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (parsed.isSourceRejected()) { return forwardSkeletonSourceRejection(parsed); }
  if (parsed.isRuntimeRejected()) {
    return SkeletonProviderResult::runtimeRejected(parsed.runtimeFailure());
  }
  if (!parsed.isPublished() ||
      parsed.lease().capability().canonicalSourceKey() != selected.value().encode().asPtr()) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto moduleKey = incremental_binding_query::StableModuleQueryKey::fromVerified(key.module());
  if (moduleKey == zc::none) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto identityAdmission =
      context.getCapability<incremental_binding_query::StableIdentityAdmissionQuery>(
          ZC_ASSERT_NONNULL(moduleKey).clone());
  if (identityAdmission.isSourceRejected()) {
    return forwardSkeletonSourceRejection(identityAdmission);
  }
  if (identityAdmission.isKeyRejected()) { return forwardSkeletonKeyRejection(identityAdmission); }
  if (identityAdmission.isRuntimeRejected()) {
    return SkeletonProviderResult::runtimeRejected(identityAdmission.runtimeFailure());
  }
  if (!identityAdmission.isPublished()) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto definitionSites =
      context.getCapability<incremental_binding_query::RevisionLocalDefinitionSitesQuery>(
          ZC_ASSERT_NONNULL(moduleKey).clone());
  if (definitionSites.isSourceRejected()) {
    return forwardSkeletonSourceRejection(definitionSites);
  }
  if (definitionSites.isKeyRejected()) { return forwardSkeletonKeyRejection(definitionSites); }
  if (definitionSites.isRuntimeRejected()) {
    return SkeletonProviderResult::runtimeRejected(definitionSites.runtimeFailure());
  }
  if (!definitionSites.isPublished()) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto implementationSites =
      context.getCapability<incremental_binding_query::RevisionLocalImplementationSitesQuery>(
          ZC_ASSERT_NONNULL(moduleKey).clone());
  if (implementationSites.isSourceRejected()) {
    return forwardSkeletonSourceRejection(implementationSites);
  }
  if (implementationSites.isKeyRejected()) {
    return forwardSkeletonKeyRejection(implementationSites);
  }
  if (implementationSites.isRuntimeRejected()) {
    return SkeletonProviderResult::runtimeRejected(implementationSites.runtimeFailure());
  }
  if (!implementationSites.isPublished()) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto provenance = context.getCapability<incremental_binding_query::ModuleBodyProvenanceQuery>(
      zc::mv(ZC_ASSERT_NONNULL(moduleKey)));
  if (provenance.isSourceRejected()) { return forwardSkeletonSourceRejection(provenance); }
  if (provenance.isKeyRejected()) { return forwardSkeletonKeyRejection(provenance); }
  if (provenance.isRuntimeRejected()) {
    return SkeletonProviderResult::runtimeRejected(provenance.runtimeFailure());
  }
  if (!provenance.isPublished()) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto dependencyProvenance =
      context.getCapability<ModuleDependencyProvenanceQuery>(key.module().clone());
  if (dependencyProvenance.isSourceRejected()) {
    return forwardSkeletonSourceRejection(dependencyProvenance);
  }
  if (dependencyProvenance.isKeyRejected()) {
    return forwardSkeletonKeyRejection(dependencyProvenance);
  }
  if (dependencyProvenance.isRuntimeRejected()) {
    return SkeletonProviderResult::runtimeRejected(dependencyProvenance.runtimeFailure());
  }
  if (!dependencyProvenance.isPublished()) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto identities = binder::MaterializedModuleSkeletonIdentities::from(
      graphValue.context(), graphValue.revision(), graphValue.witness().fingerprint(),
      ZC_ASSERT_NONNULL(skeleton), ZC_ASSERT_NONNULL(resources).identityInterners());
  if (identities == zc::none) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Vector<MaterializedModuleSkeleton::DefinitionProvenanceLease> definitionProvenances;
  for (const auto& declaration : ZC_ASSERT_NONNULL(skeleton).declarations().values()) {
    if (declaration.activation() != binder::DefinitionActivation::ModuleSkeleton) { continue; }
    auto definitionKey = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(), declaration.queryKey().clone());
    auto definition = context.getCapability<incremental_binding_query::NamedItemProvenanceQuery>(
        zc::mv(definitionKey));
    if (definition.isSourceRejected()) { return forwardSkeletonSourceRejection(definition); }
    if (definition.isKeyRejected()) { return forwardSkeletonKeyRejection(definition); }
    if (definition.isRuntimeRejected()) {
      return SkeletonProviderResult::runtimeRejected(definition.runtimeFailure());
    }
    if (!definition.isPublished()) {
      return SkeletonProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    definitionProvenances.add(zc::mv(definition).takeLease());
  }
  auto candidate = MaterializedModuleSkeleton::from(
      key.clone(), zc::mv(graph).takeLease(),
      zc::mv(dependencies).get<zc::Vector<MaterializedModuleSkeleton::DependencySkeletonLease>>(),
      selected.value().clone(), provenance.lease().capability().clone(),
      zc::mv(dependencyProvenance).takeLease(), zc::mv(ZC_ASSERT_NONNULL(identities)),
      zc::mv(identityAdmission).takeLease(), zc::mv(definitionSites).takeLease(),
      zc::mv(implementationSites).takeLease(), zc::mv(definitionProvenances),
      parsed.lease().capability());
  if (candidate == zc::none) {
    return SkeletonProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto owned = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  auto stableWitness =
      query::CapabilityCandidateContract<MaterializeModuleSkeletonQuery>::encode(*owned);
  return SkeletonProviderResult::candidate(zc::mv(owned), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> MaterializeModuleSkeletonQuery::verify(
    query::CapabilityQueryContext<MaterializeModuleSkeletonQuery>& context, const Key& key,
    const Capability& candidate) {
  auto graph = context.getCapability<MaterializeModuleGraphQuery>(key.contextRoots().clone());
  auto resources =
      context.template semanticContextResources<ModuleGraphIdentityMaterializationResources>();
  if (!graph.isPublished() || resources == zc::none ||
      candidate.contextRoots() != key.contextRoots() ||
      !sameSkeletonModule(candidate.module(), key.module()) ||
      candidate.context() != ZC_ASSERT_NONNULL(resources).semanticContext() ||
      candidate.revision() != context.snapshotRevision() ||
      candidate.revision() != graph.lease().capability().revision() ||
      candidate.graphLease().capability().witness().encodeCanonical().asPtr() !=
          graph.lease().capability().witness().encodeCanonical().asPtr() ||
      candidate.fingerprint().digest() !=
          graph.lease().capability().witness().fingerprint().digest() ||
      !graphContainsModule(graph.lease().capability(), key.module())) {
    return zc::none;
  }
  auto dependencies = verifyDependencySkeletons(context, key, graph.lease().capability());
  if (dependencies == zc::none) { return zc::none; }
  zc::Maybe<SkeletonProviderResult> rejection;
  auto skeleton = acquireSkeleton(context, key, rejection);
  if (rejection != zc::none || skeleton == zc::none ||
      !skeletonMembershipsMatch(context, key, ZC_ASSERT_NONNULL(skeleton))) {
    return zc::none;
  }
  auto selected = context.get<SelectedModuleSourceQuery>(key.module());
  if (selected.isRuntimeFailure() || selected.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(selected.value());
  if (sourceKey == zc::none) { return zc::none; }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (!parsed.isPublished() ||
      parsed.lease().capability().canonicalSourceKey() != selected.value().encode().asPtr()) {
    return zc::none;
  }
  auto moduleKey = incremental_binding_query::StableModuleQueryKey::fromVerified(key.module());
  if (moduleKey == zc::none) { return zc::none; }
  auto identityAdmission =
      context.getCapability<incremental_binding_query::StableIdentityAdmissionQuery>(
          ZC_ASSERT_NONNULL(moduleKey).clone());
  if (!identityAdmission.isPublished() || candidate.identityAdmissionLease().stableWitness() !=
                                              identityAdmission.lease().stableWitness()) {
    return zc::none;
  }
  auto definitionSites =
      context.getCapability<incremental_binding_query::RevisionLocalDefinitionSitesQuery>(
          ZC_ASSERT_NONNULL(moduleKey).clone());
  if (!definitionSites.isPublished()) { return zc::none; }
  auto implementationSites =
      context.getCapability<incremental_binding_query::RevisionLocalImplementationSitesQuery>(
          ZC_ASSERT_NONNULL(moduleKey).clone());
  if (!implementationSites.isPublished()) { return zc::none; }
  auto provenance = context.getCapability<incremental_binding_query::ModuleBodyProvenanceQuery>(
      zc::mv(ZC_ASSERT_NONNULL(moduleKey)));
  if (!provenance.isPublished() ||
      candidate.source().encode().asPtr() != selected.value().encode().asPtr() ||
      candidate.provenance() != provenance.lease().capability()) {
    return zc::none;
  }
  auto dependencyProvenance =
      context.getCapability<ModuleDependencyProvenanceQuery>(key.module().clone());
  if (!dependencyProvenance.isPublished() ||
      candidate.dependencyProvenanceLease().stableWitness() !=
          dependencyProvenance.lease().stableWitness()) {
    return zc::none;
  }
  auto expectedIdentities = binder::MaterializedModuleSkeletonIdentities::from(
      candidate.context(), candidate.revision(), candidate.fingerprint(),
      ZC_ASSERT_NONNULL(skeleton), ZC_ASSERT_NONNULL(resources).identityInterners());
  if (expectedIdentities == zc::none) { return zc::none; }
  zc::Vector<MaterializedModuleSkeleton::DefinitionProvenanceLease> definitionProvenances;
  for (const auto& declaration : ZC_ASSERT_NONNULL(skeleton).declarations().values()) {
    if (declaration.activation() != binder::DefinitionActivation::ModuleSkeleton) { continue; }
    auto definitionKey = incremental_binding_query::ContextualDefinitionKey::from(
        key.contextRoots().clone(), declaration.queryKey().clone());
    auto definition = context.getCapability<incremental_binding_query::NamedItemProvenanceQuery>(
        zc::mv(definitionKey));
    if (!definition.isPublished()) { return zc::none; }
    definitionProvenances.add(zc::mv(definition).takeLease());
  }
  auto expected = MaterializedModuleSkeleton::from(
      key.clone(), zc::mv(graph).takeLease(), zc::mv(ZC_ASSERT_NONNULL(dependencies)),
      selected.value().clone(), provenance.lease().capability().clone(),
      zc::mv(dependencyProvenance).takeLease(), zc::mv(ZC_ASSERT_NONNULL(expectedIdentities)),
      zc::mv(identityAdmission).takeLease(), zc::mv(definitionSites).takeLease(),
      zc::mv(implementationSites).takeLease(), zc::mv(definitionProvenances),
      parsed.lease().capability());
  if (expected == zc::none ||
      !sameSkeletonIdentities(candidate.identities(), ZC_ASSERT_NONNULL(expected).identities()) ||
      !sameScopeIdentities(candidate.scopeIdentities(),
                           ZC_ASSERT_NONNULL(expected).scopeIdentities()) ||
      candidate.definitionSitesLease().stableWitness() !=
          ZC_ASSERT_NONNULL(expected).definitionSitesLease().stableWitness() ||
      candidate.implementationSitesLease().stableWitness() !=
          ZC_ASSERT_NONNULL(expected).implementationSitesLease().stableWitness() ||
      !sameDefinitionProvenanceLeases(candidate.definitionProvenanceLeases(),
                                      ZC_ASSERT_NONNULL(expected).definitionProvenanceLeases()) ||
      !sameDefinitionFacts(candidate.materializedDefinitions(),
                           ZC_ASSERT_NONNULL(expected).materializedDefinitions()) ||
      !sameGenericParameterFacts(candidate.materializedGenericParameters(),
                                 ZC_ASSERT_NONNULL(expected).materializedGenericParameters()) ||
      !sameCallableParameterFacts(candidate.materializedCallableParameters(),
                                  ZC_ASSERT_NONNULL(expected).materializedCallableParameters()) ||
      !sameModuleAliasFacts(candidate.materializedModuleAliases(),
                            ZC_ASSERT_NONNULL(expected).materializedModuleAliases()) ||
      !sameImportBindingFacts(candidate.materializedImports(),
                              ZC_ASSERT_NONNULL(expected).materializedImports()) ||
      !sameLocalExportFacts(candidate.materializedLocalExports(),
                            ZC_ASSERT_NONNULL(expected).materializedLocalExports()) ||
      !sameSkeletonFailedLookups(candidate.materializedFailedLookups(),
                                 ZC_ASSERT_NONNULL(expected).materializedFailedLookups()) ||
      candidate.bindingSurface().revision().digest() !=
          ZC_ASSERT_NONNULL(expected).bindingSurface().revision().digest() ||
      !sameImplementationBindings(candidate.materializedImplementations(),
                                  ZC_ASSERT_NONNULL(expected).materializedImplementations()) ||
      !sameScopeRecords(candidate.materializedScopes(),
                        ZC_ASSERT_NONNULL(expected).materializedScopes())) {
    return zc::none;
  }
  return candidate.encodeCanonical();
}

namespace {

using OwnerBodyProviderResult = query::CapabilityProviderResult<MaterializeOwnerBodyQuery>;

bool sameOwnerBody(const binder::StableOwnerBodyQueryKey& left,
                   const binder::StableOwnerBodyQueryKey& right) {
  return left.encodeCanonical().asPtr() == right.encodeCanonical().asPtr();
}

bool skeletonContainsOwner(const binder::BoundModuleSkeleton& skeleton,
                           const binder::StableOwnerBodyQueryKey& owner) {
  for (const auto& candidate : skeleton.bodyOwners().values()) {
    if (sameOwnerBody(candidate, owner)) { return true; }
  }
  return false;
}

template <typename SourceDescriptor>
OwnerBodyProviderResult forwardOwnerBodySourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  using TargetContract =
      query::CapabilityFailureContract<MaterializeOwnerBodyQuery,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto diagnostics = TargetContract::decode(SourceContract::encode(source.diagnostics()).asPtr());
  if (diagnostics == zc::none) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  return OwnerBodyProviderResult::sourceRejected<diagnostics::DiagnosticFact>(
      zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename SourceDescriptor>
OwnerBodyProviderResult forwardOwnerBodyKeyRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  return OwnerBodyProviderResult::keyRejected<binder::BinderKeyFailure>(
      source.keyFailure().clone());
}

zc::Maybe<binder::BoundOwnerBody> acquireOwnerBody(
    query::CapabilityQueryContext<MaterializeOwnerBodyQuery>& context,
    const incremental_binding_query::ContextualBodyOwnerKey& key,
    zc::Maybe<OwnerBodyProviderResult>& rejection) {
  auto result = context.get<binder::BindOwnerBody>(key.clone());
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  const auto& value = result.value().storage();
  if (value.is<binder::BinderSourceRejected>()) {
    auto encoded = binder::encodeStableBindingDiagnosticFacts(
        value.get<binder::BinderSourceRejected>().diagnostics.values());
    if (encoded == zc::none) { return zc::none; }
    auto diagnostics =
        query::CapabilityFailureContract<MaterializeOwnerBodyQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>::
            decode(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (diagnostics == zc::none) { return zc::none; }
    rejection = OwnerBodyProviderResult::sourceRejected<diagnostics::DiagnosticFact>(
        zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
    return zc::none;
  }
  if (value.is<binder::BinderKeyRejected>()) {
    rejection = OwnerBodyProviderResult::keyRejected<binder::BinderKeyFailure>(
        value.get<binder::BinderKeyRejected>().failure.clone());
    return zc::none;
  }
  const auto& bound = value.get<binder::BinderQueryValue<binder::BoundOwnerBody>>();
  if (bound.diagnostics.values().size() != 0 || !sameOwnerBody(bound.value.owner(), key.body())) {
    return zc::none;
  }
  return bound.value.clone();
}

zc::Maybe<binder::OwnerAllocationRange> acquireOwnerAllocation(
    query::CapabilityQueryContext<MaterializeOwnerBodyQuery>& context,
    const incremental_binding_query::ContextualBodyOwnerKey& key,
    zc::Maybe<OwnerBodyProviderResult>& rejection) {
  auto moduleKey = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), key.body().module().clone());
  auto result = context.get<binder::ModuleBindingAllocationPlanQuery>(zc::mv(moduleKey));
  if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) {
    return zc::none;
  }
  const auto& value = result.value().storage();
  if (value.is<binder::BinderSourceRejected>()) {
    auto encoded = binder::encodeStableBindingDiagnosticFacts(
        value.get<binder::BinderSourceRejected>().diagnostics.values());
    if (encoded == zc::none) { return zc::none; }
    auto diagnostics =
        query::CapabilityFailureContract<MaterializeOwnerBodyQuery,
                                         query::SourceRejection<diagnostics::DiagnosticFact>>::
            decode(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (diagnostics == zc::none) { return zc::none; }
    rejection = OwnerBodyProviderResult::sourceRejected<diagnostics::DiagnosticFact>(
        zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
    return zc::none;
  }
  if (value.is<binder::BinderKeyRejected>()) {
    rejection = OwnerBodyProviderResult::keyRejected<binder::BinderKeyFailure>(
        value.get<binder::BinderKeyRejected>().failure.clone());
    return zc::none;
  }
  const auto& plan = value.get<binder::BinderQueryValue<binder::ModuleBindingAllocationPlan>>();
  if (plan.diagnostics.values().size() != 0 ||
      plan.value.key().encode().asPtr() != key.body().module().encode().asPtr()) {
    return zc::none;
  }
  zc::Maybe<binder::OwnerAllocationRange> allocation;
  for (const auto& candidate : plan.value.owners().values()) {
    if (!sameOwnerBody(candidate.owner(), key.body())) { continue; }
    if (allocation != zc::none) { return zc::none; }
    allocation = candidate.clone();
  }
  return allocation;
}

zc::Maybe<zc::Vector<binder::OwnerLocalBindingId>> materializeOwnerLocalBindings(
    identity::SemanticContextBrand context, identity::ModuleId module,
    const binder::OwnerAllocationRange& allocation, const binder::BoundOwnerBody& stableWitness) {
  if (!module.belongsTo(context) || allocation.owner() != stableWitness.owner() ||
      allocation.ownerLocalCount() != stableWitness.bindings().values().size()) {
    return zc::none;
  }
  auto allocator = binder::ModuleLocalIdentityAllocator::create(context, module);
  if (allocator == zc::none ||
      !ZC_ASSERT_NONNULL(allocator).skipOwnerLocalBindings(allocation.ownerLocalBegin())) {
    return zc::none;
  }
  zc::Vector<binder::OwnerLocalBindingId> identities(allocation.ownerLocalCount());
  for (uint32_t index = 0; index < allocation.ownerLocalCount(); ++index) {
    auto identity = ZC_ASSERT_NONNULL(allocator).allocateOwnerLocalBinding();
    if (identity == zc::none) { return zc::none; }
    identities.add(ZC_ASSERT_NONNULL(identity));
  }
  return identities;
}

zc::Vector<binder::OwnerLocalBindingFact> cloneOwnerLocalBindingFacts(
    zc::ArrayPtr<const binder::OwnerLocalBindingFact> facts) {
  zc::Vector<binder::OwnerLocalBindingFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(binder::OwnerLocalBindingFact{fact.identity, fact.node, fact.site.clone(), fact.kind,
                                             fact.name.clone(), fact.nameSpace, fact.declaringScope,
                                             fact.source.clone(), fact.activation});
  }
  return result;
}

void appendOwnerLocalBindingFacts(zc::Vector<binder::OwnerLocalBindingFact>& result,
                                  zc::ArrayPtr<const binder::OwnerLocalBindingFact> facts) {
  for (const auto& fact : facts) {
    result.add(binder::OwnerLocalBindingFact{fact.identity, fact.node, fact.site.clone(), fact.kind,
                                             fact.name.clone(), fact.nameSpace, fact.declaringScope,
                                             fact.source.clone(), fact.activation});
  }
}

zc::Vector<binder::AnonymousEntityFact> cloneAnonymousEntityFacts(
    zc::ArrayPtr<const binder::AnonymousEntityFact> facts) {
  zc::Vector<binder::AnonymousEntityFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(binder::AnonymousEntityFact{fact.node, fact.site.clone(), fact.identity,
                                           fact.key.clone(), fact.source.clone()});
  }
  return result;
}

void appendAnonymousEntityFacts(zc::Vector<binder::AnonymousEntityFact>& result,
                                zc::ArrayPtr<const binder::AnonymousEntityFact> facts) {
  for (const auto& fact : facts) {
    result.add(binder::AnonymousEntityFact{fact.node, fact.site.clone(), fact.identity,
                                           fact.key.clone(), fact.source.clone()});
  }
}

bool sameAnonymousEntityFacts(zc::ArrayPtr<const binder::AnonymousEntityFact> left,
                              zc::ArrayPtr<const binder::AnonymousEntityFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node ||
        !sameDefinitionSite(left[index].site, right[index].site) ||
        left[index].identity != right[index].identity || left[index].key != right[index].key ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd()) {
      return false;
    }
  }
  return true;
}

zc::Maybe<identity::DefId> materializedDefinition(const MaterializedModuleSkeleton& skeleton,
                                                  const binder::StableDefinitionQueryKey& key);

zc::Maybe<binder::BindingTarget> materializeBindingTarget(
    const MaterializedModuleSkeleton& skeleton, const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::OwnerLocalBindingId> ownerLocalIdentities,
    const binder::StableBindingTargetKey& stableTarget) {
  const auto& value = stableTarget.value();
  if (value.is<binder::StableDefinitionBindingTarget>()) {
    const auto& definition = value.get<binder::StableDefinitionBindingTarget>().definition;
    auto materialized = materializedDefinition(skeleton, definition);
    if (materialized == zc::none) { return zc::none; }
    return binder::BindingTarget::definition(ZC_ASSERT_NONNULL(materialized));
  }
  if (value.is<binder::StableModuleBindingTarget>()) {
    const auto& module = value.get<binder::StableModuleBindingTarget>().module;
    if (module.encode().asPtr() != skeleton.module().encode().asPtr()) { return zc::none; }
    return binder::BindingTarget::module(skeleton.identities().module());
  }
  if (value.is<binder::StableSemanticImportBindingTarget>()) {
    return binder::BindingTarget::semanticImport(
        value.get<binder::StableSemanticImportBindingTarget>().import.binding().clone());
  }
  if (value.is<binder::StableOwnerLocalBindingTarget>()) {
    const auto& local = value.get<binder::StableOwnerLocalBindingTarget>();
    if (local.owner != stableWitness.owner() ||
        ownerLocalIdentities.size() != stableWitness.bindings().values().size()) {
      return zc::none;
    }
    for (size_t index = 0; index < stableWitness.bindings().values().size(); ++index) {
      if (stableWitness.bindings().values()[index].key() == local.binding) {
        return binder::BindingTarget::ownerLocal(ownerLocalIdentities[index]);
      }
    }
    return zc::none;
  }
  if (value.is<binder::StableGenericParameterBindingTarget>()) {
    const auto& parameter = value.get<binder::StableGenericParameterBindingTarget>().parameter;
    if (parameter.module().encode().asPtr() != skeleton.module().encode().asPtr()) {
      return zc::none;
    }
    for (const auto& identity : skeleton.identities().genericParameters()) {
      if (identity.key() == parameter.parameter()) {
        return binder::BindingTarget::genericParameter(identity.handle());
      }
    }
    return zc::none;
  }
  if (value.is<binder::StableCallableParameterBindingTarget>()) {
    const auto& parameter = value.get<binder::StableCallableParameterBindingTarget>().parameter;
    if (parameter.module().encode().asPtr() != skeleton.module().encode().asPtr()) {
      return zc::none;
    }
    for (const auto& identity : skeleton.identities().callableParameters()) {
      if (identity.key() == parameter.parameter()) {
        return binder::BindingTarget::callableParameter(identity.handle());
      }
    }
  }
  return zc::none;
}

zc::Maybe<identity::DefId> materializedDefinition(const MaterializedModuleSkeleton& skeleton,
                                                  const binder::StableDefinitionQueryKey& key) {
  const auto findIn = [&](const binder::MaterializedModuleSkeletonIdentities& identities)
      -> zc::Maybe<identity::DefId> {
    zc::Maybe<identity::DefId> result;
    for (const auto& candidate : identities.definitions()) {
      if (candidate.key() != key.definition()) { continue; }
      if (result != zc::none) { return zc::none; }
      result = candidate.handle();
    }
    return result;
  };
  auto result = findIn(skeleton.identities());
  for (const auto& dependency : skeleton.dependencySkeletonLeases()) {
    auto candidate = findIn(dependency.capability().identities());
    if (candidate == zc::none) { continue; }
    if (result != zc::none) { return zc::none; }
    result = ZC_ASSERT_NONNULL(candidate);
  }
  return result;
}

zc::Maybe<zc::Vector<binder::ShadowTargetFact>> materializeShadowTargets(
    const MaterializedModuleSkeleton& skeleton, const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::OwnerLocalBindingId> ownerLocalIdentities) {
  zc::Vector<binder::ShadowTargetFact> result(stableWitness.shadowTargets().values().size());
  for (const auto& stable : stableWitness.shadowTargets().values()) {
    if (stable.owner() != stableWitness.owner()) { return zc::none; }
    auto binding =
        materializeBindingTarget(skeleton, stableWitness, ownerLocalIdentities, stable.binding());
    auto shadowed =
        materializeBindingTarget(skeleton, stableWitness, ownerLocalIdentities, stable.shadowed());
    if (binding == zc::none || shadowed == zc::none) { return zc::none; }
    result.add(binder::ShadowTargetFact{zc::mv(ZC_ASSERT_NONNULL(binding)),
                                        zc::mv(ZC_ASSERT_NONNULL(shadowed))});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::ClosureFreeVariableFact>> materializeClosureFreeVariables(
    const MaterializedModuleSkeleton& skeleton, const binder::OwnerBodyProvenance& provenance,
    const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::OwnerLocalBindingId> ownerLocalIdentities) {
  const auto& provenanceEntries = provenance.detachedProvenance().entries();
  zc::Vector<binder::ClosureFreeVariableFact> result(
      stableWitness.closureFreeVariables().values().size());
  for (const auto& stable : stableWitness.closureFreeVariables().values()) {
    if (stable.owner() != stableWitness.owner()) { return zc::none; }
    zc::Vector<binder::FreeVariableFact> variables(stable.variables().values().size());
    for (const auto& variable : stable.variables().values()) {
      auto target = materializeBindingTarget(skeleton, stableWitness, ownerLocalIdentities,
                                             variable.target());
      if (target == zc::none) { return zc::none; }
      zc::Vector<ast::NodeId> referenceSites(variable.referencePaths().values().size());
      for (const auto& path : variable.referencePaths().values()) {
        zc::Maybe<ast::NodeId> node;
        for (const auto& entry : provenanceEntries) {
          if (entry.path != path) { continue; }
          if (node != zc::none) { return zc::none; }
          node = entry.node;
        }
        if (node == zc::none) { return zc::none; }
        referenceSites.add(ZC_ASSERT_NONNULL(node));
      }
      variables.add(
          binder::FreeVariableFact{zc::mv(ZC_ASSERT_NONNULL(target)), zc::mv(referenceSites)});
    }
    result.add(binder::ClosureFreeVariableFact{stable.closure().clone(), zc::mv(variables)});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::ExplicitClosureCaptureFact>> materializeExplicitClosureCaptures(
    const MaterializedModuleSkeleton& skeleton, const identity::SourceFileKey& source,
    const binder::OwnerBodyProvenance& provenance, const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::OwnerLocalBindingId> ownerLocalIdentities,
    const parser::CanonicalParsedSource& parsedSource) {
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none || parsedSource.canonicalSourceKey() != source.encode().asPtr() ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest())
    return zc::none;
  zc::Vector<binder::ExplicitClosureCaptureFact> result(
      stableWitness.explicitClosureCaptures().values().size());
  for (const auto& stable : stableWitness.explicitClosureCaptures().values()) {
    if (stable.owner() != stableWitness.owner()) return zc::none;
    zc::Maybe<ast::NodeId> captureList;
    for (const auto& entry : provenance.detachedProvenance().entries()) {
      if (entry.path != stable.captureListPath()) continue;
      if (captureList != zc::none) return zc::none;
      captureList = entry.node;
    }
    auto span = captureList == zc::none
                    ? zc::Maybe<identity::SourceSpan>()
                    : sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot),
                                        ZC_ASSERT_NONNULL(captureList));
    if (captureList == zc::none || span == zc::none ||
        !parsedSource.tree().contains(ZC_ASSERT_NONNULL(captureList)) ||
        parsedSource.tree().node(ZC_ASSERT_NONNULL(captureList)).kind !=
            ast::SyntaxKind::CaptureList)
      return zc::none;
    zc::Vector<binder::ExplicitCaptureBindingFact> captures(stable.captures().values().size());
    for (const auto& capture : stable.captures().values()) {
      zc::Maybe<ast::NodeId> item;
      for (const auto& entry : provenance.detachedProvenance().entries()) {
        if (entry.path != capture.itemPath()) continue;
        if (item != zc::none) return zc::none;
        item = entry.node;
      }
      auto target =
          materializeBindingTarget(skeleton, stableWitness, ownerLocalIdentities, capture.target());
      auto itemSpan = item == zc::none
                          ? zc::Maybe<identity::SourceSpan>()
                          : sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot),
                                              ZC_ASSERT_NONNULL(item));
      if (item == zc::none || target == zc::none || itemSpan == zc::none) return zc::none;
      captures.add(binder::ExplicitCaptureBindingFact{ZC_ASSERT_NONNULL(item),
                                                      zc::mv(ZC_ASSERT_NONNULL(target)),
                                                      zc::mv(ZC_ASSERT_NONNULL(itemSpan))});
    }
    result.add(
        binder::ExplicitClosureCaptureFact{stable.closure().clone(), ZC_ASSERT_NONNULL(captureList),
                                           zc::mv(ZC_ASSERT_NONNULL(span)), zc::mv(captures)});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::BindingResolution>> materializeResolutions(
    const MaterializedModuleSkeleton& skeleton, const binder::OwnerBodyProvenance& provenance,
    const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::OwnerLocalBindingId> ownerLocalIdentities) {
  const auto& provenanceEntries = provenance.detachedProvenance().entries();
  zc::Vector<binder::BindingResolution> result(stableWitness.resolutions().values().size());
  for (const auto& stable : stableWitness.resolutions().values()) {
    if (stable.owner() != stableWitness.owner()) { return zc::none; }
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenanceEntries) {
      if (entry.path != stable.usePath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    auto binding =
        materializeBindingTarget(skeleton, stableWitness, ownerLocalIdentities, stable.binding());
    auto canonicalTarget = materializeBindingTarget(skeleton, stableWitness, ownerLocalIdentities,
                                                    stable.canonicalTarget());
    if (node == zc::none || binding == zc::none || canonicalTarget == zc::none) { return zc::none; }
    result.add(binder::BindingResolution{
        ZC_ASSERT_NONNULL(node),
        binder::BindingResolutionValue(binder::BoundNameResolution{
            zc::mv(ZC_ASSERT_NONNULL(binding)), zc::mv(ZC_ASSERT_NONNULL(canonicalTarget)),
            stable.nameSpace(), stable.origin()})});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::BoundSelfType>> materializeSelfTypes(
    const MaterializedModuleSkeleton& skeleton, const identity::SourceFileKey& source,
    const binder::OwnerBodyProvenance& provenance, const binder::BoundOwnerBody& stableWitness,
    const parser::CanonicalParsedSource& parsedSource) {
  if (parsedSource.canonicalSourceKey() != source.encode().asPtr()) { return zc::none; }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  zc::Vector<binder::BoundSelfType> result(stableWitness.selfTypes().values().size());
  for (const auto& stable : stableWitness.selfTypes().values()) {
    if (stable.owner() != stableWitness.owner()) { return zc::none; }
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenance.detachedProvenance().entries()) {
      if (entry.path != stable.syntaxPath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    if (node == zc::none || !parsedSource.tree().contains(ZC_ASSERT_NONNULL(node)) ||
        parsedSource.tree().node(ZC_ASSERT_NONNULL(node)).kind != ast::SyntaxKind::NamedTypeExpr) {
      return zc::none;
    }
    zc::Maybe<identity::DefId> definition;
    const auto& stableOwner = stable.selfOwner().value();
    if (stableOwner.is<binder::StableNominalSelfOwner>()) {
      const auto& target = stableOwner.get<binder::StableNominalSelfOwner>().definition;
      definition = materializedDefinition(skeleton, target);
      if (definition == zc::none) { return zc::none; }
      auto span =
          sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), ZC_ASSERT_NONNULL(node));
      if (span == zc::none) { return zc::none; }
      result.add(binder::BoundSelfType{
          ZC_ASSERT_NONNULL(node),
          binder::SelfOwner(binder::NominalSelfOwner{ZC_ASSERT_NONNULL(definition)}),
          zc::mv(ZC_ASSERT_NONNULL(span))});
      continue;
    }
    if (stableOwner.is<binder::StableInterfaceSelfOwner>()) {
      const auto& target = stableOwner.get<binder::StableInterfaceSelfOwner>().definition;
      definition = materializedDefinition(skeleton, target);
      if (definition == zc::none) { return zc::none; }
      auto span =
          sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), ZC_ASSERT_NONNULL(node));
      if (span == zc::none) { return zc::none; }
      result.add(binder::BoundSelfType{
          ZC_ASSERT_NONNULL(node),
          binder::SelfOwner(binder::InterfaceSelfOwner{ZC_ASSERT_NONNULL(definition)}),
          zc::mv(ZC_ASSERT_NONNULL(span))});
      continue;
    }
    return zc::none;
  }
  return result;
}

zc::Vector<binder::BindingResolution> cloneBindingResolutions(
    zc::ArrayPtr<const binder::BindingResolution> resolutions) {
  zc::Vector<binder::BindingResolution> result(resolutions.size());
  for (const auto& resolution : resolutions) {
    if (resolution.value.is<binder::BoundNameResolution>()) {
      const auto& name = resolution.value.get<binder::BoundNameResolution>();
      result.add(binder::BindingResolution{
          resolution.node, binder::BindingResolutionValue(binder::BoundNameResolution{
                               name.bindingIdentity.clone(), name.canonicalTarget.clone(),
                               name.nameSpace, name.origin})});
      continue;
    }
    if (resolution.value.is<binder::BoundLabelResolution>()) {
      const auto& label = resolution.value.get<binder::BoundLabelResolution>();
      result.add(binder::BindingResolution{
          resolution.node, binder::BindingResolutionValue(binder::BoundLabelResolution{
                               label.label.clone(), label.target.clone()})});
      continue;
    }
    ZC_UNREACHABLE
  }
  return result;
}

zc::Maybe<zc::Vector<binder::BoundThis>> materializeThisBindings(
    const MaterializedModuleSkeleton& skeleton, const identity::SourceFileKey& source,
    const binder::OwnerBodyProvenance& provenance, const binder::BoundOwnerBody& stableWitness,
    const parser::CanonicalParsedSource& parsedSource) {
  if (parsedSource.canonicalSourceKey() != source.encode().asPtr()) { return zc::none; }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  zc::Vector<binder::BoundThis> result(stableWitness.thisBindings().values().size());
  for (const auto& stable : stableWitness.thisBindings().values()) {
    if (stable.owner() != stableWitness.owner()) { return zc::none; }
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenance.detachedProvenance().entries()) {
      if (entry.path != stable.expressionPath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    zc::Maybe<identity::CallableParameterId> receiver;
    for (const auto& identity : skeleton.identities().callableParameters()) {
      if (identity.key() != stable.receiver().parameter()) { continue; }
      if (receiver != zc::none) { return zc::none; }
      receiver = identity.handle();
    }
    auto span = node == zc::none ? zc::Maybe<identity::SourceSpan>()
                                 : sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot),
                                                     ZC_ASSERT_NONNULL(node));
    if (node == zc::none || receiver == zc::none || span == zc::none ||
        !parsedSource.tree().contains(ZC_ASSERT_NONNULL(node)) ||
        parsedSource.tree().node(ZC_ASSERT_NONNULL(node)).kind != ast::SyntaxKind::ThisExpr) {
      return zc::none;
    }
    result.add(binder::BoundThis{ZC_ASSERT_NONNULL(node),
                                 binder::ThisBinding{ZC_ASSERT_NONNULL(receiver)},
                                 zc::mv(ZC_ASSERT_NONNULL(span))});
  }
  return result;
}

zc::Vector<binder::BoundSelfType> cloneSelfTypes(
    zc::ArrayPtr<const binder::BoundSelfType> selfTypes) {
  zc::Vector<binder::BoundSelfType> result(selfTypes.size());
  for (const auto& selfType : selfTypes) {
    const auto& owner = selfType.owner;
    if (owner.is<binder::NominalSelfOwner>()) {
      result.add(binder::BoundSelfType{selfType.syntax,
                                       binder::SelfOwner(binder::NominalSelfOwner{
                                           owner.get<binder::NominalSelfOwner>().definition}),
                                       selfType.source.clone()});
      continue;
    }
    if (owner.is<binder::InterfaceSelfOwner>()) {
      result.add(binder::BoundSelfType{selfType.syntax,
                                       binder::SelfOwner(binder::InterfaceSelfOwner{
                                           owner.get<binder::InterfaceSelfOwner>().definition}),
                                       selfType.source.clone()});
      continue;
    }
    ZC_UNREACHABLE
  }
  return result;
}

zc::Vector<binder::BoundThis> cloneThisBindings(zc::ArrayPtr<const binder::BoundThis> bindings) {
  zc::Vector<binder::BoundThis> result(bindings.size());
  for (const auto& binding : bindings) {
    result.add(binder::BoundThis{binding.expression,
                                 binder::ThisBinding{binding.binding.receiverParameter},
                                 binding.source.clone()});
  }
  return result;
}

zc::Vector<binder::ShadowTargetFact> cloneShadowTargetFacts(
    zc::ArrayPtr<const binder::ShadowTargetFact> shadows) {
  zc::Vector<binder::ShadowTargetFact> result(shadows.size());
  for (const auto& shadow : shadows) {
    result.add(binder::ShadowTargetFact{shadow.binding.clone(), shadow.target.clone()});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::MaterializedFailedLookupFact>> materializeFailedLookups(
    const binder::OwnerBodyProvenance& provenance, const binder::BoundOwnerBody& stableWitness) {
  const auto& provenanceEntries = provenance.detachedProvenance().entries();
  zc::Vector<binder::MaterializedFailedLookupFact> result(
      stableWitness.failedLookups().values().size());
  for (const auto& stable : stableWitness.failedLookups().values()) {
    const auto& owner = stable.owner().value();
    if (!owner.is<binder::BinderBodyQueryOwner>() ||
        owner.get<binder::BinderBodyQueryOwner>().body != stableWitness.owner()) {
      return zc::none;
    }
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenanceEntries) {
      if (entry.path != stable.usePath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    if (node == zc::none) { return zc::none; }
    result.add(binder::MaterializedFailedLookupFact{ZC_ASSERT_NONNULL(node), stable.nameSpace(),
                                                    stable.name().clone(),
                                                    stable.outcome().clone()});
  }
  return result;
}

zc::Vector<binder::MaterializedFailedLookupFact> cloneMaterializedFailedLookups(
    zc::ArrayPtr<const binder::MaterializedFailedLookupFact> lookups) {
  zc::Vector<binder::MaterializedFailedLookupFact> result(lookups.size());
  for (const auto& lookup : lookups) {
    result.add(binder::MaterializedFailedLookupFact{lookup.node, lookup.nameSpace,
                                                    lookup.name.clone(), lookup.outcome.clone()});
  }
  return result;
}

bool sameMaterializedFailedLookups(zc::ArrayPtr<const binder::MaterializedFailedLookupFact> left,
                                   zc::ArrayPtr<const binder::MaterializedFailedLookupFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node || left[index].nameSpace != right[index].nameSpace ||
        left[index].name != right[index].name || left[index].outcome != right[index].outcome) {
      return false;
    }
  }
  return true;
}

bool sameThisBindings(zc::ArrayPtr<const binder::BoundThis> left,
                      zc::ArrayPtr<const binder::BoundThis> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].expression != right[index].expression ||
        left[index].binding.receiverParameter != right[index].binding.receiverParameter ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd()) {
      return false;
    }
  }
  return true;
}

bool sameSelfTypes(zc::ArrayPtr<const binder::BoundSelfType> left,
                   zc::ArrayPtr<const binder::BoundSelfType> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    const auto& leftOwner = left[index].owner;
    const auto& rightOwner = right[index].owner;
    if (left[index].syntax != right[index].syntax ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd()) {
      return false;
    }
    if (leftOwner.is<binder::NominalSelfOwner>()) {
      if (!rightOwner.is<binder::NominalSelfOwner>() ||
          leftOwner.get<binder::NominalSelfOwner>().definition !=
              rightOwner.get<binder::NominalSelfOwner>().definition) {
        return false;
      }
      continue;
    }
    if (leftOwner.is<binder::InterfaceSelfOwner>()) {
      if (!rightOwner.is<binder::InterfaceSelfOwner>() ||
          leftOwner.get<binder::InterfaceSelfOwner>().definition !=
              rightOwner.get<binder::InterfaceSelfOwner>().definition) {
        return false;
      }
      continue;
    }
    if (!rightOwner.is<binder::ImplSelfOwner>() ||
        leftOwner.get<binder::ImplSelfOwner>().occurrence !=
            rightOwner.get<binder::ImplSelfOwner>().occurrence) {
      return false;
    }
  }
  return true;
}

bool sameBindingTarget(const binder::BindingTarget& left, const binder::BindingTarget& right) {
  const auto& leftValue = left.value();
  const auto& rightValue = right.value();
  if (leftValue.is<binder::DefinitionBindingTarget>()) {
    return rightValue.is<binder::DefinitionBindingTarget>() &&
           leftValue.get<binder::DefinitionBindingTarget>().definition ==
               rightValue.get<binder::DefinitionBindingTarget>().definition;
  }
  if (leftValue.is<binder::GenericParameterBindingTarget>()) {
    return rightValue.is<binder::GenericParameterBindingTarget>() &&
           leftValue.get<binder::GenericParameterBindingTarget>().parameter ==
               rightValue.get<binder::GenericParameterBindingTarget>().parameter;
  }
  if (leftValue.is<binder::CallableParameterBindingTarget>()) {
    return rightValue.is<binder::CallableParameterBindingTarget>() &&
           leftValue.get<binder::CallableParameterBindingTarget>().parameter ==
               rightValue.get<binder::CallableParameterBindingTarget>().parameter;
  }
  if (leftValue.is<binder::OwnerLocalBindingTarget>()) {
    return rightValue.is<binder::OwnerLocalBindingTarget>() &&
           leftValue.get<binder::OwnerLocalBindingTarget>().binding ==
               rightValue.get<binder::OwnerLocalBindingTarget>().binding;
  }
  if (leftValue.is<binder::SemanticImportBindingTarget>()) {
    return rightValue.is<binder::SemanticImportBindingTarget>() &&
           leftValue.get<binder::SemanticImportBindingTarget>().binding ==
               rightValue.get<binder::SemanticImportBindingTarget>().binding;
  }
  return rightValue.is<binder::ModuleBindingTarget>() &&
         leftValue.get<binder::ModuleBindingTarget>().module ==
             rightValue.get<binder::ModuleBindingTarget>().module;
}

bool sameShadowTargetFacts(zc::ArrayPtr<const binder::ShadowTargetFact> left,
                           zc::ArrayPtr<const binder::ShadowTargetFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!sameBindingTarget(left[index].binding, right[index].binding) ||
        !sameBindingTarget(left[index].target, right[index].target)) {
      return false;
    }
  }
  return true;
}

zc::Vector<binder::ClosureFreeVariableFact> cloneClosureFreeVariableFacts(
    zc::ArrayPtr<const binder::ClosureFreeVariableFact> facts) {
  zc::Vector<binder::ClosureFreeVariableFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<binder::FreeVariableFact> variables(fact.variables.size());
    for (const auto& variable : fact.variables) {
      zc::Vector<ast::NodeId> referenceSites(variable.referenceSites.size());
      for (const auto node : variable.referenceSites) { referenceSites.add(node); }
      variables.add(binder::FreeVariableFact{variable.target.clone(), zc::mv(referenceSites)});
    }
    result.add(binder::ClosureFreeVariableFact{fact.closure.clone(), zc::mv(variables)});
  }
  return result;
}

zc::Vector<binder::ExplicitClosureCaptureFact> cloneExplicitClosureCaptureFacts(
    zc::ArrayPtr<const binder::ExplicitClosureCaptureFact> facts) {
  zc::Vector<binder::ExplicitClosureCaptureFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<binder::ExplicitCaptureBindingFact> captures(fact.captures.size());
    for (const auto& capture : fact.captures) {
      captures.add(binder::ExplicitCaptureBindingFact{capture.item, capture.target.clone(),
                                                      capture.source.clone()});
    }
    result.add(binder::ExplicitClosureCaptureFact{fact.closure.clone(), fact.captureList,
                                                  fact.source.clone(), zc::mv(captures)});
  }
  return result;
}

bool sameExplicitClosureCaptureFacts(zc::ArrayPtr<const binder::ExplicitClosureCaptureFact> left,
                                     zc::ArrayPtr<const binder::ExplicitClosureCaptureFact> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].closure != right[index].closure ||
        left[index].captureList != right[index].captureList ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd() ||
        left[index].captures.size() != right[index].captures.size())
      return false;
    for (size_t captureIndex = 0; captureIndex < left[index].captures.size(); ++captureIndex) {
      const auto& leftCapture = left[index].captures[captureIndex];
      const auto& rightCapture = right[index].captures[captureIndex];
      if (leftCapture.item != rightCapture.item ||
          !sameBindingTarget(leftCapture.target, rightCapture.target) ||
          leftCapture.source.source().encode().asPtr() !=
              rightCapture.source.source().encode().asPtr() ||
          leftCapture.source.byteStart() != rightCapture.source.byteStart() ||
          leftCapture.source.byteEnd() != rightCapture.source.byteEnd())
        return false;
    }
  }
  return true;
}

bool sameClosureFreeVariableFacts(zc::ArrayPtr<const binder::ClosureFreeVariableFact> left,
                                  zc::ArrayPtr<const binder::ClosureFreeVariableFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].closure != right[index].closure ||
        left[index].variables.size() != right[index].variables.size()) {
      return false;
    }
    for (size_t variableIndex = 0; variableIndex < left[index].variables.size(); ++variableIndex) {
      const auto& leftVariable = left[index].variables[variableIndex];
      const auto& rightVariable = right[index].variables[variableIndex];
      if (!sameBindingTarget(leftVariable.target, rightVariable.target) ||
          leftVariable.referenceSites.asPtr() != rightVariable.referenceSites.asPtr()) {
        return false;
      }
    }
  }
  return true;
}

bool sameBindingResolutions(zc::ArrayPtr<const binder::BindingResolution> left,
                            zc::ArrayPtr<const binder::BindingResolution> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node) { return false; }
    if (left[index].value.is<binder::BoundNameResolution>()) {
      if (!right[index].value.is<binder::BoundNameResolution>()) { return false; }
      const auto& leftName = left[index].value.get<binder::BoundNameResolution>();
      const auto& rightName = right[index].value.get<binder::BoundNameResolution>();
      if (!sameBindingTarget(leftName.bindingIdentity, rightName.bindingIdentity) ||
          !sameBindingTarget(leftName.canonicalTarget, rightName.canonicalTarget) ||
          leftName.nameSpace != rightName.nameSpace || leftName.origin != rightName.origin) {
        return false;
      }
      continue;
    }
    if (!left[index].value.is<binder::BoundLabelResolution>() ||
        !right[index].value.is<binder::BoundLabelResolution>()) {
      return false;
    }
    const auto& leftLabel = left[index].value.get<binder::BoundLabelResolution>();
    const auto& rightLabel = right[index].value.get<binder::BoundLabelResolution>();
    if (leftLabel.label != rightLabel.label || leftLabel.target != rightLabel.target) {
      return false;
    }
  }
  return true;
}

zc::Maybe<zc::Vector<binder::DeferredMemberFact>> materializeDeferredMembers(
    const identity::SourceFileKey& source, const binder::OwnerBodyProvenance& provenance,
    const binder::BoundOwnerBody& stableWitness,
    const parser::CanonicalParsedSource& parsedSource) {
  if (parsedSource.canonicalSourceKey() != source.encode().asPtr()) { return zc::none; }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  const auto& entries = provenance.detachedProvenance().entries();
  const auto findNode = [&](const binder::LocalSyntaxPath& path) -> zc::Maybe<ast::NodeId> {
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : entries) {
      if (entry.path != path) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    return node;
  };
  const auto& tree = parsedSource.tree();
  zc::Vector<binder::DeferredMemberFact> result(stableWitness.deferredMembers().values().size());
  for (const auto& stable : stableWitness.deferredMembers().values()) {
    if (stable.owner() != stableWitness.owner()) { return zc::none; }
    auto node = findNode(stable.usePath());
    auto base = findNode(stable.basePath());
    if (node == zc::none || base == zc::none || !tree.contains(ZC_ASSERT_NONNULL(node)) ||
        !tree.contains(ZC_ASSERT_NONNULL(base))) {
      return zc::none;
    }
    zc::Vector<binder::Namespace> expectedNamespaces(stable.expectedNamespaces().values().size());
    for (const auto nameSpace : stable.expectedNamespaces().values()) {
      expectedNamespaces.add(nameSpace);
    }
    zc::Vector<ast::NodeId> genericArguments(stable.genericArgumentPaths().values().size());
    for (const auto& path : stable.genericArgumentPaths().values()) {
      auto argument = findNode(path);
      if (argument == zc::none || !tree.contains(ZC_ASSERT_NONNULL(argument))) { return zc::none; }
      genericArguments.add(ZC_ASSERT_NONNULL(argument));
    }
    const auto& syntax = tree.node(ZC_ASSERT_NONNULL(node));
    zc::Maybe<identity::DeclaredDefinitionName> name;
    if (syntax.kind == ast::SyntaxKind::MemberExpression) {
      if (ast::NodeId(syntax.payload.words[ast::kMemberExpressionObjectWord]) !=
          ZC_ASSERT_NONNULL(base)) {
        return zc::none;
      }
      const auto access = static_cast<ast::MemberAccessKind>(
          syntax.payload.words[ast::kMemberExpressionAccessWord]);
      if ((stable.accessKind() == binder::MemberAccessKind::Dot &&
           access != ast::MemberAccessKind::Dot) ||
          (stable.accessKind() == binder::MemberAccessKind::Optional &&
           access != ast::MemberAccessKind::Optional) ||
          (stable.accessKind() == binder::MemberAccessKind::Qualified &&
           access != ast::MemberAccessKind::Qualified)) {
        return zc::none;
      }
      name = identity::DeclaredDefinitionName::fromSource(
          tree.ident(ast::IdentId(syntax.payload.words[ast::kMemberExpressionPropertyWord])));
    } else if (syntax.kind == ast::SyntaxKind::NamedTypeExpr &&
               stable.accessKind() == binder::MemberAccessKind::Qualified &&
               ast::NodeId(syntax.payload.words[ast::kNamedTypeExprPathWord]) ==
                   ZC_ASSERT_NONNULL(base) &&
               tree.node(ZC_ASSERT_NONNULL(base)).kind == ast::SyntaxKind::ModulePath) {
      const auto& pathSyntax = tree.node(ZC_ASSERT_NONNULL(base));
      const ast::IdentList segments{pathSyntax.payload.words[ast::kModulePathSegmentsFirstWord],
                                    pathSyntax.payload.words[ast::kModulePathSegmentsSizeWord]};
      const ast::NodeList arguments{syntax.payload.words[ast::kNamedTypeExprArgsFirstWord],
                                    syntax.payload.words[ast::kNamedTypeExprArgsSizeWord]};
      if (!tree.contains(segments) || !tree.contains(arguments)) { return zc::none; }
      const auto names = tree.identList(segments);
      const auto argumentNodes = tree.list(arguments);
      if (names.size() < 2 || argumentNodes.size() != genericArguments.size()) { return zc::none; }
      for (size_t index = 0; index < argumentNodes.size(); ++index) {
        if (argumentNodes[index] != genericArguments[index]) { return zc::none; }
      }
      name = identity::DeclaredDefinitionName::fromSource(tree.ident(names[names.size() - 1]));
    } else {
      return zc::none;
    }
    auto span =
        sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), ZC_ASSERT_NONNULL(node));
    if (name == zc::none || ZC_ASSERT_NONNULL(name) != stable.member() || span == zc::none) {
      return zc::none;
    }
    result.add(binder::DeferredMemberFact{
        ZC_ASSERT_NONNULL(node), ZC_ASSERT_NONNULL(base), zc::mv(ZC_ASSERT_NONNULL(name)),
        zc::mv(expectedNamespaces), zc::mv(genericArguments), zc::mv(ZC_ASSERT_NONNULL(span))});
  }
  return result;
}

zc::Vector<binder::DeferredMemberFact> cloneDeferredMemberFacts(
    zc::ArrayPtr<const binder::DeferredMemberFact> facts) {
  zc::Vector<binder::DeferredMemberFact> result(facts.size());
  for (const auto& fact : facts) {
    zc::Vector<binder::Namespace> expectedNamespaces(fact.expectedNamespaces.size());
    for (const auto nameSpace : fact.expectedNamespaces) { expectedNamespaces.add(nameSpace); }
    zc::Vector<ast::NodeId> genericArguments(fact.genericArguments.size());
    for (const auto argument : fact.genericArguments) { genericArguments.add(argument); }
    result.add(binder::DeferredMemberFact{fact.node, fact.base, fact.member.clone(),
                                          zc::mv(expectedNamespaces), zc::mv(genericArguments),
                                          fact.source.clone()});
  }
  return result;
}

template <typename T>
void appendFacts(zc::Vector<T>& destination, zc::Vector<T>&& source) {
  for (auto& fact : source) { destination.add(zc::mv(fact)); }
}

zc::Vector<binder::NodeScopeFact> cloneNodeScopeFacts(
    zc::ArrayPtr<const binder::NodeScopeFact> facts) {
  zc::Vector<binder::NodeScopeFact> result(facts.size());
  for (const auto& fact : facts) { result.add(binder::NodeScopeFact{fact.node, fact.scope}); }
  return result;
}

zc::Vector<binder::LabelFact> cloneLabelFacts(zc::ArrayPtr<const binder::LabelFact> facts) {
  zc::Vector<binder::LabelFact> result(facts.size());
  for (const auto& fact : facts) {
    result.add(binder::LabelFact{fact.identity.clone(), fact.name.clone(), fact.owner.clone(),
                                 fact.statement, fact.target.clone(), fact.source.clone()});
  }
  return result;
}

zc::Vector<binder::ControlTransferFact> cloneControlTransferFacts(
    zc::ArrayPtr<const binder::ControlTransferFact> facts) {
  zc::Vector<binder::ControlTransferFact> result(facts.size());
  for (const auto& fact : facts) {
    const auto& target = fact.target;
    if (target.is<binder::ExplicitLabelControlTarget>()) {
      result.add(binder::ControlTransferFact{
          fact.node, fact.kind,
          binder::ControlTarget(binder::ExplicitLabelControlTarget{
              target.get<binder::ExplicitLabelControlTarget>().label.clone()}),
          fact.source.clone()});
      continue;
    }
    if (target.is<binder::LoopControlTarget>()) {
      result.add(binder::ControlTransferFact{fact.node, fact.kind,
                                             binder::ControlTarget(binder::LoopControlTarget{
                                                 target.get<binder::LoopControlTarget>().scope}),
                                             fact.source.clone()});
      continue;
    }
    result.add(binder::ControlTransferFact{fact.node, fact.kind,
                                           binder::ControlTarget(binder::MatchControlTarget{
                                               target.get<binder::MatchControlTarget>().scope}),
                                           fact.source.clone()});
  }
  return result;
}

struct AggregatedMaterializedBindingFacts final {
  zc::Vector<binder::NodeScopeFact> nodeScopes;
  zc::Vector<binder::BindingResolution> nodeBindings;
  zc::Vector<binder::BoundSelfType> selfTypes;
  zc::Vector<binder::BoundThis> thisBindings;
  zc::Vector<binder::DefinitionFact> definitions;
  zc::Vector<binder::ImplBindingFact> implementations;
  zc::Vector<binder::ScopeRecord> scopes;
  zc::Vector<binder::ModuleAliasBindingFact> moduleAliases;
  zc::Vector<binder::ImportBindingFact> imports;
  zc::Vector<binder::LocalExportFact> localExports;
  zc::Vector<binder::DeferredMemberFact> deferredMembers;
  zc::Vector<binder::LabelFact> labels;
  zc::Vector<binder::ControlTransferFact> controlTransfers;
  zc::Vector<binder::ShadowTargetFact> shadowTargets;
  zc::Vector<binder::ClosureFreeVariableFact> closureFreeVariables;
  zc::Vector<binder::ExplicitClosureCaptureFact> explicitClosureCaptures;
  zc::Vector<binder::GenericParameterFact> genericParameters;
  zc::Vector<binder::CallableParameterFact> callableParameters;
  zc::Vector<binder::OwnerLocalBindingFact> ownerLocalBindings;
  zc::Vector<binder::MaterializedFailedLookupFact> failedLookups;

  ZC_NODISCARD binder::MaterializedBindingFacts view() const noexcept {
    return binder::MaterializedBindingFacts{nodeScopes.asPtr(),
                                            nodeBindings.asPtr(),
                                            selfTypes.asPtr(),
                                            thisBindings.asPtr(),
                                            definitions.asPtr(),
                                            implementations.asPtr(),
                                            scopes.asPtr(),
                                            moduleAliases.asPtr(),
                                            imports.asPtr(),
                                            localExports.asPtr(),
                                            deferredMembers.asPtr(),
                                            labels.asPtr(),
                                            controlTransfers.asPtr(),
                                            shadowTargets.asPtr(),
                                            closureFreeVariables.asPtr(),
                                            explicitClosureCaptures.asPtr(),
                                            genericParameters.asPtr(),
                                            callableParameters.asPtr(),
                                            ownerLocalBindings.asPtr(),
                                            failedLookups.asPtr()};
  }
};

AggregatedMaterializedBindingFacts aggregateMaterializedBindingFacts(
    const MaterializedModuleSkeleton& skeleton,
    zc::ArrayPtr<const VerifiedBoundModule::OwnerBodyLease> ownerBodies) {
  AggregatedMaterializedBindingFacts result;
  appendFacts(result.nodeScopes, cloneNodeScopeFacts(skeleton.materializedNodeScopes()));
  appendFacts(result.definitions, cloneDefinitionFacts(skeleton.materializedDefinitions()));
  appendFacts(result.implementations,
              cloneImplementationBindings(skeleton.materializedImplementations()));
  appendFacts(result.scopes, cloneScopeRecords(skeleton.materializedScopes()));
  appendFacts(result.moduleAliases, cloneModuleAliasFacts(skeleton.materializedModuleAliases()));
  appendFacts(result.imports, cloneImportBindingFacts(skeleton.materializedImports()));
  appendFacts(result.localExports, cloneLocalExportFacts(skeleton.materializedLocalExports()));
  appendFacts(result.genericParameters,
              cloneGenericParameterFacts(skeleton.materializedGenericParameters()));
  appendFacts(result.callableParameters,
              cloneCallableParameterFacts(skeleton.materializedCallableParameters()));
  appendFacts(result.failedLookups,
              cloneMaterializedFailedLookups(skeleton.materializedFailedLookups()));
  for (const auto& bodyLease : ownerBodies) {
    const auto& body = bodyLease.capability();
    appendFacts(result.nodeScopes, cloneNodeScopeFacts(body.materializedNodeScopes()));
    appendFacts(result.nodeBindings, cloneBindingResolutions(body.materializedResolutions()));
    appendFacts(result.selfTypes, cloneSelfTypes(body.materializedSelfTypes()));
    appendFacts(result.thisBindings, cloneThisBindings(body.materializedThisBindings()));
    appendFacts(result.deferredMembers,
                cloneDeferredMemberFacts(body.materializedDeferredMembers()));
    appendFacts(result.labels, cloneLabelFacts(body.materializedLabels()));
    appendFacts(result.controlTransfers,
                cloneControlTransferFacts(body.materializedControlTransfers()));
    appendFacts(result.shadowTargets, cloneShadowTargetFacts(body.materializedShadowTargets()));
    appendFacts(result.closureFreeVariables,
                cloneClosureFreeVariableFacts(body.materializedClosureFreeVariables()));
    appendFacts(result.explicitClosureCaptures,
                cloneExplicitClosureCaptureFacts(body.materializedExplicitClosureCaptures()));
    appendFacts(result.ownerLocalBindings,
                cloneOwnerLocalBindingFacts(body.materializedOwnerLocalBindings()));
    appendFacts(result.failedLookups,
                cloneMaterializedFailedLookups(body.materializedFailedLookups()));
  }
  return result;
}

bool sameOwnerLocalBindings(zc::ArrayPtr<const binder::OwnerLocalBindingFact> left,
                            zc::ArrayPtr<const binder::OwnerLocalBindingFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].identity != right[index].identity || left[index].node != right[index].node ||
        !sameDefinitionSite(left[index].site, right[index].site) ||
        left[index].kind != right[index].kind || left[index].name != right[index].name ||
        left[index].nameSpace != right[index].nameSpace ||
        left[index].declaringScope != right[index].declaringScope ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd() ||
        left[index].activation != right[index].activation) {
      return false;
    }
  }
  return true;
}

bool sameDeferredMemberFacts(zc::ArrayPtr<const binder::DeferredMemberFact> left,
                             zc::ArrayPtr<const binder::DeferredMemberFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node || left[index].base != right[index].base ||
        left[index].member != right[index].member ||
        left[index].expectedNamespaces.asPtr() != right[index].expectedNamespaces.asPtr() ||
        left[index].genericArguments.asPtr() != right[index].genericArguments.asPtr() ||
        left[index].source.source().encode().asPtr() !=
            right[index].source.source().encode().asPtr() ||
        left[index].source.byteStart() != right[index].source.byteStart() ||
        left[index].source.byteEnd() != right[index].source.byteEnd()) {
      return false;
    }
  }
  return true;
}

bool sameNodeScopes(zc::ArrayPtr<const binder::NodeScopeFact> left,
                    zc::ArrayPtr<const binder::NodeScopeFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node || left[index].scope != right[index].scope) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct MaterializedOwnerBody::Impl final {
  Impl(incremental_binding_query::ContextualBodyOwnerKey&& key,
       identity::SemanticContextBrand context, query::DatabaseRevision revision,
       identity::ContextFingerprint&& fingerprint, identity::ModuleId module,
       SkeletonLease&& skeleton, identity::SourceFileKey&& source, ProvenanceLease&& provenance,
       binder::BoundOwnerBody&& stableWitness, binder::OwnerAllocationRange&& allocation,
       zc::Vector<binder::ScopeId>&& scopeIdentities,
       zc::Vector<binder::NodeScopeFact>&& nodeScopes,
       zc::Vector<binder::OwnerLocalBindingFact>&& ownerLocalBindings,
       zc::Vector<binder::AnonymousEntityFact>&& anonymousEntities,
       zc::Vector<binder::BindingResolution>&& resolutions,
       zc::Vector<binder::BoundSelfType>&& selfTypes, zc::Vector<binder::BoundThis>&& thisBindings,
       zc::Vector<binder::ShadowTargetFact>&& shadowTargets, zc::Vector<binder::LabelFact>&& labels,
       zc::Vector<binder::ControlTransferFact>&& controlTransfers,
       zc::Vector<binder::ClosureFreeVariableFact>&& closureFreeVariables,
       zc::Vector<binder::ExplicitClosureCaptureFact>&& explicitCaptures,
       zc::Vector<binder::MaterializedFailedLookupFact>&& failedLookups,
       zc::Vector<binder::DeferredMemberFact>&& deferredMembers) noexcept
      : key(zc::mv(key)),
        context(context),
        revision(revision),
        fingerprint(zc::mv(fingerprint)),
        module(module),
        skeleton(zc::mv(skeleton)),
        source(zc::mv(source)),
        provenance(zc::mv(provenance)),
        stableWitness(zc::mv(stableWitness)),
        allocation(zc::mv(allocation)),
        scopeIdentities(zc::mv(scopeIdentities)),
        nodeScopes(zc::mv(nodeScopes)),
        ownerLocalBindings(zc::mv(ownerLocalBindings)),
        anonymousEntities(zc::mv(anonymousEntities)),
        resolutions(zc::mv(resolutions)),
        selfTypes(zc::mv(selfTypes)),
        thisBindings(zc::mv(thisBindings)),
        shadowTargets(zc::mv(shadowTargets)),
        labels(zc::mv(labels)),
        controlTransfers(zc::mv(controlTransfers)),
        closureFreeVariables(zc::mv(closureFreeVariables)),
        explicitCaptures(zc::mv(explicitCaptures)),
        failedLookups(zc::mv(failedLookups)),
        deferredMembers(zc::mv(deferredMembers)) {}

  incremental_binding_query::ContextualBodyOwnerKey key;
  identity::SemanticContextBrand context;
  query::DatabaseRevision revision;
  identity::ContextFingerprint fingerprint;
  identity::ModuleId module;
  SkeletonLease skeleton;
  identity::SourceFileKey source;
  ProvenanceLease provenance;
  binder::BoundOwnerBody stableWitness;
  binder::OwnerAllocationRange allocation;
  zc::Vector<binder::ScopeId> scopeIdentities;
  zc::Vector<binder::NodeScopeFact> nodeScopes;
  zc::Vector<binder::OwnerLocalBindingFact> ownerLocalBindings;
  zc::Vector<binder::AnonymousEntityFact> anonymousEntities;
  zc::Vector<binder::BindingResolution> resolutions;
  zc::Vector<binder::BoundSelfType> selfTypes;
  zc::Vector<binder::BoundThis> thisBindings;
  zc::Vector<binder::ShadowTargetFact> shadowTargets;
  zc::Vector<binder::LabelFact> labels;
  zc::Vector<binder::ControlTransferFact> controlTransfers;
  zc::Vector<binder::ClosureFreeVariableFact> closureFreeVariables;
  zc::Vector<binder::ExplicitClosureCaptureFact> explicitCaptures;
  zc::Vector<binder::MaterializedFailedLookupFact> failedLookups;
  zc::Vector<binder::DeferredMemberFact> deferredMembers;
};

MaterializedOwnerBody::MaterializedOwnerBody(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
MaterializedOwnerBody::~MaterializedOwnerBody() noexcept(false) = default;
MaterializedOwnerBody::MaterializedOwnerBody(MaterializedOwnerBody&&) noexcept = default;
MaterializedOwnerBody& MaterializedOwnerBody::operator=(MaterializedOwnerBody&&) noexcept = default;

zc::Maybe<zc::Vector<binder::ScopeId>> MaterializedOwnerBody::materializeScopeIdentities(
    identity::ModuleId module, const binder::OwnerAllocationRange& allocation,
    const binder::BoundOwnerBody& stableWitness) {
  if (allocation.owner() != stableWitness.owner() ||
      allocation.scopeCount() != stableWitness.scopes().values().size() ||
      static_cast<uint64_t>(allocation.scopeBegin()) + allocation.scopeCount() >
          static_cast<uint64_t>(0xffffffffu)) {
    return zc::none;
  }
  zc::Vector<binder::ScopeId> identities(allocation.scopeCount());
  for (uint32_t index = 0; index < allocation.scopeCount(); ++index) {
    identities.add(binder::ScopeId(module, allocation.scopeBegin() + index));
  }
  return identities;
}

zc::Maybe<zc::Vector<binder::NodeScopeFact>> MaterializedOwnerBody::materializeNodeScopes(
    const MaterializedModuleSkeleton& skeleton, const binder::OwnerBodyProvenance& provenance,
    const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities) {
  const auto bodyScopes = stableWitness.scopes().values();
  if (bodyScopes.size() != scopeIdentities.size()) { return zc::none; }
  zc::Vector<binder::NodeScopeFact> result(stableWitness.nodeScopes().values().size());
  for (const auto& fact : stableWitness.nodeScopes().values()) {
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : provenance.detachedProvenance().entries()) {
      if (entry.path != fact.nodePath()) { continue; }
      if (node != zc::none) { return zc::none; }
      node = entry.node;
    }
    auto scope = skeleton.scopeFor(fact.scope());
    if (scope == zc::none) {
      for (size_t index = 0; index < bodyScopes.size(); ++index) {
        if (bodyScopes[index].scope() == fact.scope()) {
          scope = scopeIdentities[index];
          break;
        }
      }
    }
    if (node == zc::none || scope == zc::none) { return zc::none; }
    result.add(binder::NodeScopeFact{ZC_ASSERT_NONNULL(node), ZC_ASSERT_NONNULL(scope)});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::OwnerLocalBindingFact>>
MaterializedOwnerBody::materializeOwnerLocalBindingFacts(
    const MaterializedModuleSkeleton& skeleton, const identity::SourceFileKey& source,
    const binder::OwnerBodyProvenance& provenance, const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities,
    zc::ArrayPtr<const binder::OwnerLocalBindingId> identities,
    const binder::OwnerBodySyntax& syntax, const parser::CanonicalParsedSource& parsedSource) {
  if (identities.size() != stableWitness.bindings().values().size() ||
      parsedSource.canonicalSourceKey() != source.encode().asPtr() ||
      syntax.owner() != stableWitness.owner().owner()) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  auto traversal = binder::OwnerBodySyntaxTraversal::from(syntax.detachedSyntax());
  if (traversal == zc::none) { return zc::none; }
  const auto entries = ZC_ASSERT_NONNULL(traversal).entries();
  zc::Vector<binder::OwnerLocalBindingFact> result(identities.size());
  for (size_t index = 0; index < identities.size(); ++index) {
    const auto& fact = stableWitness.bindings().values()[index];
    zc::Maybe<size_t> entryIndex;
    zc::Maybe<const binder::ModuleBodyProvenanceEntry&> provenanceEntry;
    for (size_t candidate = 0; candidate < entries.size(); ++candidate) {
      if (entries[candidate].path == fact.key().path()) {
        if (entryIndex != zc::none) { return zc::none; }
        entryIndex = candidate;
      }
    }
    for (const auto& candidate : provenance.detachedProvenance().entries()) {
      if (candidate.path == fact.key().path()) {
        if (provenanceEntry != zc::none) { return zc::none; }
        provenanceEntry = candidate;
      }
    }
    auto scope = skeleton.scopeFor(fact.declaringScope());
    if (scope == zc::none) {
      for (size_t candidate = 0; candidate < stableWitness.scopes().values().size(); ++candidate) {
        if (stableWitness.scopes().values()[candidate].scope() == fact.declaringScope()) {
          scope = scopeIdentities[candidate];
          break;
        }
      }
    }
    if (entryIndex == zc::none || provenanceEntry == zc::none || scope == zc::none) {
      return zc::none;
    }
    const auto& entry = entries[ZC_ASSERT_NONNULL(entryIndex)];
    binder::DefinitionSite site =
        binder::DefinitionSite::declaration(ZC_ASSERT_NONNULL(provenanceEntry).node);
    if (fact.kind() == binder::OwnerLocalBindingKind::Local ||
        fact.kind() == binder::OwnerLocalBindingKind::PatternBinding) {
      uint32_t ancestorIndex = entry.parentIndex;
      zc::Maybe<size_t> introducerIndex;
      const auto expectedKind = fact.kind() == binder::OwnerLocalBindingKind::Local
                                    ? ast::SyntaxKind::VariableDeclarator
                                    : ast::SyntaxKind::ForInStatement;
      while (ancestorIndex != 0xffffffffu) {
        if (ancestorIndex >= entries.size()) { return zc::none; }
        const auto& ancestor = entries[ancestorIndex];
        if (ancestor.syntaxKind == expectedKind ||
            (fact.kind() == binder::OwnerLocalBindingKind::PatternBinding &&
             ancestor.syntaxKind == ast::SyntaxKind::MatchArmStmt)) {
          introducerIndex = ancestorIndex;
          break;
        }
        ancestorIndex = ancestor.parentIndex;
      }
      if (introducerIndex == zc::none) { return zc::none; }
      const auto& introducer = entries[ZC_ASSERT_NONNULL(introducerIndex)];
      zc::Maybe<ast::NodeId> introducerNode;
      for (const auto& candidate : provenance.detachedProvenance().entries()) {
        if (candidate.path != introducer.path) { continue; }
        if (introducerNode != zc::none) { return zc::none; }
        introducerNode = candidate.node;
      }
      if (introducerNode == zc::none ||
          introducer.path.components().size() >= entry.path.components().size()) {
        return zc::none;
      }
      zc::Vector<uint32_t> patternPath;
      for (size_t component = introducer.path.components().size();
           component < entry.path.components().size(); ++component) {
        patternPath.add(entry.path.components()[component]);
      }
      site =
          binder::DefinitionSite::pattern(ZC_ASSERT_NONNULL(introducerNode), zc::mv(patternPath));
    }
    auto span = ZC_ASSERT_NONNULL(snapshot).span(ZC_ASSERT_NONNULL(provenanceEntry).byteStart,
                                                 ZC_ASSERT_NONNULL(provenanceEntry).byteEnd);
    if (span == zc::none) { return zc::none; }
    result.add(binder::OwnerLocalBindingFact{
        identities[index], ZC_ASSERT_NONNULL(provenanceEntry).node, zc::mv(site), fact.kind(),
        fact.name().clone(), fact.nameSpace(), ZC_ASSERT_NONNULL(scope),
        zc::mv(ZC_ASSERT_NONNULL(span)), fact.activation()});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::AnonymousEntityFact>>
MaterializedOwnerBody::materializeAnonymousEntities(
    identity::SemanticContextBrand context, identity::ModuleId module,
    const binder::OwnerAllocationRange& allocation, const identity::SourceFileKey& source,
    const binder::OwnerBodyProvenance& provenance, const binder::BoundOwnerBody& stableWitness,
    const parser::CanonicalParsedSource& parsedSource) {
  const size_t expectedCount = stableWitness.closures().values().size() +
                               stableWitness.explicitClosureCaptures().values().size();
  if (!module.belongsTo(context) || allocation.owner() != stableWitness.owner() ||
      allocation.anonymousCount() != expectedCount ||
      parsedSource.canonicalSourceKey() != source.encode().asPtr()) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  auto allocator = binder::ModuleLocalIdentityAllocator::create(context, module);
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest() ||
      allocator == zc::none ||
      !ZC_ASSERT_NONNULL(allocator).skipAnonymousOwnerLocals(allocation.anonymousBegin())) {
    return zc::none;
  }

  zc::Vector<binder::AnonymousEntityFact> result(expectedCount);
  auto append = [&](const binder::AnonymousOwnerLocalKey& key) {
    zc::Maybe<const binder::ModuleBodyProvenanceEntry&> provenanceEntry;
    for (const auto& entry : provenance.detachedProvenance().entries()) {
      if (entry.path != key.path()) { continue; }
      if (provenanceEntry != zc::none) { return false; }
      provenanceEntry = entry;
    }
    if (provenanceEntry == zc::none ||
        !parsedSource.tree().contains(ZC_ASSERT_NONNULL(provenanceEntry).node)) {
      return false;
    }
    const auto node = ZC_ASSERT_NONNULL(provenanceEntry).node;
    const auto syntax = parsedSource.tree().node(node).kind;
    if ((key.role() == binder::AnonymousOwnerLocalRole::Closure &&
         syntax != ast::SyntaxKind::FunctionExpression &&
         syntax != ast::SyntaxKind::LambdaExpression) ||
        (key.role() == binder::AnonymousOwnerLocalRole::FunctionExpression &&
         syntax != ast::SyntaxKind::FunctionExpression)) {
      return false;
    }
    auto identity = ZC_ASSERT_NONNULL(allocator).allocateAnonymousOwnerLocal();
    auto span = sourceSpanForNode(parsedSource, ZC_ASSERT_NONNULL(snapshot), node);
    if (identity == zc::none || span == zc::none) { return false; }
    result.add(binder::AnonymousEntityFact{node, binder::DefinitionSite::declaration(node),
                                           ZC_ASSERT_NONNULL(identity), key.clone(),
                                           zc::mv(ZC_ASSERT_NONNULL(span))});
    return true;
  };
  for (const auto& closure : stableWitness.closures().values()) {
    if (closure.owner() != stableWitness.owner() || !append(closure.closure())) { return zc::none; }
  }
  for (const auto& capture : stableWitness.explicitClosureCaptures().values()) {
    if (capture.owner() != stableWitness.owner() || !append(capture.closure())) { return zc::none; }
  }
  return result;
}

zc::Maybe<zc::Vector<binder::LabelFact>> MaterializedOwnerBody::materializeLabels(
    identity::ModuleId module, const MaterializedModuleSkeleton& skeleton,
    const identity::SourceFileKey& source, const binder::OwnerBodyProvenance& provenance,
    const binder::BoundOwnerBody& stableWitness, const binder::OwnerAllocationRange& allocation,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities,
    const parser::CanonicalParsedSource& parsedSource) {
  const auto stableLabels = stableWitness.labels().values();
  if (parsedSource.canonicalSourceKey() != source.encode().asPtr() ||
      allocation.labelCount() != stableLabels.size() ||
      static_cast<uint64_t>(allocation.labelBegin()) + allocation.labelCount() >
          static_cast<uint64_t>(0xffffffffu)) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  const auto labelOwner =
      [&](const binder::StableScopeOwnerKey& targetScope) -> zc::Maybe<binder::LabelOwner> {
    auto scope = targetScope.clone();
    for (size_t depth = 0; depth <= stableWitness.scopes().values().size(); ++depth) {
      for (const auto& closure : stableWitness.closures().values()) {
        if (closure.scope() == scope) {
          return binder::LabelOwner::anonymous(module, closure.closure().clone());
        }
      }
      zc::Maybe<const binder::StableBodyScopeFact&> bodyScope;
      for (const auto& candidate : stableWitness.scopes().values()) {
        if (candidate.scope() != scope) { continue; }
        if (bodyScope != zc::none) { return zc::none; }
        bodyScope = candidate;
      }
      if (bodyScope == zc::none) { break; }
      scope = ZC_ASSERT_NONNULL(bodyScope).parent().clone();
    }
    if (stableWitness.owner().owner().kind() == binder::StableBodyOwnerKind::Module) {
      return binder::LabelOwner::module(module);
    }
    auto stableDefinition = stableWitness.owner().owner().definitionKey();
    if (stableDefinition == zc::none) { return zc::none; }
    for (const auto& definition : skeleton.identities().definitions()) {
      if (definition.key() == ZC_ASSERT_NONNULL(stableDefinition)) {
        return binder::LabelOwner::callable(definition.handle());
      }
    }
    return zc::none;
  };
  const auto& entries = provenance.detachedProvenance().entries();
  zc::Vector<binder::LabelFact> result(stableLabels.size());
  for (size_t index = 0; index < stableLabels.size(); ++index) {
    const auto& stable = stableLabels[index];
    if (stable.key().owner() != stableWitness.owner()) { return zc::none; }
    zc::Maybe<const binder::ModuleBodyProvenanceEntry&> declaration;
    zc::Maybe<const binder::ModuleBodyProvenanceEntry&> statement;
    for (const auto& entry : entries) {
      if (entry.path == stable.key().declarationPath()) {
        if (declaration != zc::none) { return zc::none; }
        declaration = entry;
      }
      if (entry.path == stable.statementPath()) {
        if (statement != zc::none) { return zc::none; }
        statement = entry;
      }
    }
    auto owner = labelOwner(stable.target().scope());
    auto scope = skeleton.scopeFor(stable.target().scope());
    if (scope == zc::none) {
      for (size_t scopeIndex = 0; scopeIndex < stableWitness.scopes().values().size();
           ++scopeIndex) {
        if (stableWitness.scopes().values()[scopeIndex].scope() == stable.target().scope() &&
            scopeIndex < scopeIdentities.size()) {
          scope = scopeIdentities[scopeIndex];
          break;
        }
      }
    }
    if (scope == zc::none) { return zc::none; }
    auto name = identity::SemanticIdentifier::fromCanonical(stable.name().text());
    auto declarationSpan = declaration == zc::none ? zc::Maybe<identity::SourceSpan>()
                                                   : ZC_ASSERT_NONNULL(snapshot).span(
                                                         ZC_ASSERT_NONNULL(declaration).byteStart,
                                                         ZC_ASSERT_NONNULL(declaration).byteEnd);
    if (owner == zc::none || statement == zc::none || name == zc::none ||
        declarationSpan == zc::none) {
      return zc::none;
    }
    binder::LabelTarget target = stable.target().value().is<binder::StableBlockLabelTarget>()
                                     ? binder::LabelTarget::block(ZC_ASSERT_NONNULL(scope))
                                     : binder::LabelTarget::loop(ZC_ASSERT_NONNULL(scope));
    result.add(binder::LabelFact{
        binder::LabelId(ZC_ASSERT_NONNULL(owner).clone(), allocation.labelBegin() + index),
        zc::mv(ZC_ASSERT_NONNULL(name)), ZC_ASSERT_NONNULL(owner).clone(),
        ZC_ASSERT_NONNULL(statement).node, zc::mv(target),
        zc::mv(ZC_ASSERT_NONNULL(declarationSpan))});
  }
  return result;
}

zc::Maybe<zc::Vector<binder::ControlTransferFact>>
MaterializedOwnerBody::materializeControlTransfers(
    const MaterializedModuleSkeleton& skeleton, const identity::SourceFileKey& source,
    const binder::OwnerBodyProvenance& provenance, const binder::BoundOwnerBody& stableWitness,
    zc::ArrayPtr<const binder::ScopeId> scopeIdentities,
    zc::ArrayPtr<const binder::LabelFact> labels,
    const parser::CanonicalParsedSource& parsedSource) {
  if (parsedSource.canonicalSourceKey() != source.encode().asPtr() ||
      labels.size() != stableWitness.labels().values().size()) {
    return zc::none;
  }
  auto snapshot = identity::ImmutableSourceSnapshot::from(
      source.clone(), zc::heapArray<uint8_t>(parsedSource.sourceBytes()));
  if (snapshot == zc::none ||
      ZC_ASSERT_NONNULL(snapshot).contentDigest() != parsedSource.contentDigest()) {
    return zc::none;
  }
  const auto& entries = provenance.detachedProvenance().entries();
  zc::Vector<binder::ControlTransferFact> result(stableWitness.controlTransfers().values().size());
  for (const auto& stable : stableWitness.controlTransfers().values()) {
    if (stable.owner() != stableWitness.owner()) { return zc::none; }
    zc::Maybe<const binder::ModuleBodyProvenanceEntry&> transfer;
    for (const auto& entry : entries) {
      if (entry.path != stable.transferPath()) { continue; }
      if (transfer != zc::none) { return zc::none; }
      transfer = entry;
    }
    if (transfer == zc::none) { return zc::none; }
    auto span = ZC_ASSERT_NONNULL(snapshot).span(ZC_ASSERT_NONNULL(transfer).byteStart,
                                                 ZC_ASSERT_NONNULL(transfer).byteEnd);
    if (span == zc::none) { return zc::none; }
    const auto& targetValue = stable.target().value();
    if (targetValue.is<binder::StableExplicitLabelControlTarget>()) {
      const auto& targetLabel = targetValue.get<binder::StableExplicitLabelControlTarget>().label;
      zc::Maybe<size_t> labelIndex;
      for (size_t index = 0; index < stableWitness.labels().values().size(); ++index) {
        if (stableWitness.labels().values()[index].key() != targetLabel) { continue; }
        if (labelIndex != zc::none) { return zc::none; }
        labelIndex = index;
      }
      if (labelIndex == zc::none) { return zc::none; }
      result.add(binder::ControlTransferFact{
          ZC_ASSERT_NONNULL(transfer).node, stable.kind(),
          binder::ControlTarget(binder::ExplicitLabelControlTarget{
              binder::LabelId(labels[ZC_ASSERT_NONNULL(labelIndex)].identity.owner().clone(),
                              labels[ZC_ASSERT_NONNULL(labelIndex)].identity.index())}),
          zc::mv(ZC_ASSERT_NONNULL(span))});
      continue;
    }
    const auto& stableScope = targetValue.is<binder::StableLoopControlTarget>()
                                  ? targetValue.get<binder::StableLoopControlTarget>().scope
                                  : targetValue.get<binder::StableMatchControlTarget>().scope;
    auto scope = skeleton.scopeFor(stableScope);
    if (scope == zc::none) {
      for (size_t scopeIndex = 0; scopeIndex < stableWitness.scopes().values().size();
           ++scopeIndex) {
        if (stableWitness.scopes().values()[scopeIndex].scope() == stableScope &&
            scopeIndex < scopeIdentities.size()) {
          scope = scopeIdentities[scopeIndex];
          break;
        }
      }
    }
    if (scope == zc::none) { return zc::none; }
    auto target = targetValue.is<binder::StableLoopControlTarget>()
                      ? binder::ControlTarget(binder::LoopControlTarget{ZC_ASSERT_NONNULL(scope)})
                      : binder::ControlTarget(binder::MatchControlTarget{ZC_ASSERT_NONNULL(scope)});
    result.add(binder::ControlTransferFact{ZC_ASSERT_NONNULL(transfer).node, stable.kind(),
                                           zc::mv(target), zc::mv(ZC_ASSERT_NONNULL(span))});
  }
  return result;
}

bool appendLabelResolutions(zc::Vector<binder::BindingResolution>& resolutions,
                            zc::ArrayPtr<const binder::LabelFact> labels,
                            zc::ArrayPtr<const binder::ControlTransferFact> transfers) {
  for (const auto& transfer : transfers) {
    if (!transfer.target.is<binder::ExplicitLabelControlTarget>()) { continue; }
    const auto& target = transfer.target.get<binder::ExplicitLabelControlTarget>().label;
    zc::Maybe<const binder::LabelFact&> label;
    for (const auto& candidate : labels) {
      if (candidate.identity != target) { continue; }
      if (label != zc::none) { return false; }
      label = candidate;
    }
    if (label == zc::none) { return false; }
    resolutions.add(binder::BindingResolution{
        transfer.node,
        binder::BindingResolutionValue(binder::BoundLabelResolution{
            ZC_ASSERT_NONNULL(label).identity.clone(), ZC_ASSERT_NONNULL(label).target.clone()})});
  }
  return true;
}

bool sameSourceSpan(const identity::SourceSpan& left, const identity::SourceSpan& right) {
  return left.source().encode().asPtr() == right.source().encode().asPtr() &&
         left.byteStart() == right.byteStart() && left.byteEnd() == right.byteEnd();
}

bool sameLabelFacts(zc::ArrayPtr<const binder::LabelFact> left,
                    zc::ArrayPtr<const binder::LabelFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].identity != right[index].identity || left[index].name != right[index].name ||
        left[index].owner != right[index].owner ||
        left[index].statement != right[index].statement ||
        left[index].target != right[index].target ||
        !sameSourceSpan(left[index].source, right[index].source)) {
      return false;
    }
  }
  return true;
}

bool sameControlTarget(const binder::ControlTarget& left, const binder::ControlTarget& right) {
  if (left.is<binder::ExplicitLabelControlTarget>()) {
    return right.is<binder::ExplicitLabelControlTarget>() &&
           left.get<binder::ExplicitLabelControlTarget>().label ==
               right.get<binder::ExplicitLabelControlTarget>().label;
  }
  if (left.is<binder::LoopControlTarget>()) {
    return right.is<binder::LoopControlTarget>() &&
           left.get<binder::LoopControlTarget>().scope ==
               right.get<binder::LoopControlTarget>().scope;
  }
  return right.is<binder::MatchControlTarget>() &&
         left.get<binder::MatchControlTarget>().scope ==
             right.get<binder::MatchControlTarget>().scope;
}

bool sameControlTransferFacts(zc::ArrayPtr<const binder::ControlTransferFact> left,
                              zc::ArrayPtr<const binder::ControlTransferFact> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].node != right[index].node || left[index].kind != right[index].kind ||
        !sameControlTarget(left[index].target, right[index].target) ||
        !sameSourceSpan(left[index].source, right[index].source)) {
      return false;
    }
  }
  return true;
}

zc::Maybe<MaterializedOwnerBody> MaterializedOwnerBody::from(
    incremental_binding_query::ContextualBodyOwnerKey&& key, identity::SemanticContextBrand context,
    query::DatabaseRevision revision, identity::ContextFingerprint&& fingerprint,
    identity::ModuleId module, SkeletonLease&& skeleton, identity::SourceFileKey&& source,
    ProvenanceLease&& provenance, binder::BoundOwnerBody&& stableWitness,
    binder::OwnerAllocationRange&& allocation, const binder::OwnerBodySyntax& syntax,
    const parser::CanonicalParsedSource& parsedSource) {
  if (!context.isValid() || revision.value() == 0 || !module.belongsTo(context) ||
      skeleton.revision() != revision || skeleton.capability().context() != context ||
      skeleton.capability().identities().module() != module || provenance.revision() != revision ||
      provenance.capability().owner() != key.body().owner() ||
      provenance.capability().detachedProvenance().source().encode().asPtr() !=
          source.encode().asPtr() ||
      !sameOwnerBody(key.body(), stableWitness.owner()) ||
      !sameOwnerBody(allocation.owner(), stableWitness.owner()) ||
      allocation.scopeCount() != stableWitness.scopes().values().size() ||
      allocation.ownerLocalCount() != stableWitness.bindings().values().size() ||
      allocation.anonymousCount() != stableWitness.closures().values().size() +
                                         stableWitness.explicitClosureCaptures().values().size() ||
      allocation.labelCount() != stableWitness.labels().values().size()) {
    return zc::none;
  }
  auto ownerLocalBindings =
      materializeOwnerLocalBindings(context, module, allocation, stableWitness);
  auto scopeIdentities = materializeScopeIdentities(module, allocation, stableWitness);
  if (ownerLocalBindings == zc::none || scopeIdentities == zc::none) { return zc::none; }
  auto nodeScopes =
      materializeNodeScopes(skeleton.capability(), provenance.capability(), stableWitness,
                            ZC_ASSERT_NONNULL(scopeIdentities).asPtr());
  auto ownerLocalFacts = materializeOwnerLocalBindingFacts(
      skeleton.capability(), source, provenance.capability(), stableWitness,
      ZC_ASSERT_NONNULL(scopeIdentities).asPtr(), ZC_ASSERT_NONNULL(ownerLocalBindings).asPtr(),
      syntax, parsedSource);
  auto anonymousEntities = materializeAnonymousEntities(
      context, module, allocation, source, provenance.capability(), stableWitness, parsedSource);
  auto resolutions =
      materializeResolutions(skeleton.capability(), provenance.capability(), stableWitness,
                             ZC_ASSERT_NONNULL(ownerLocalBindings).asPtr());
  auto selfTypes = materializeSelfTypes(skeleton.capability(), source, provenance.capability(),
                                        stableWitness, parsedSource);
  auto thisBindings = materializeThisBindings(skeleton.capability(), source,
                                              provenance.capability(), stableWitness, parsedSource);
  auto shadowTargets = materializeShadowTargets(skeleton.capability(), stableWitness,
                                                ZC_ASSERT_NONNULL(ownerLocalBindings).asPtr());
  auto closureFreeVariables =
      materializeClosureFreeVariables(skeleton.capability(), provenance.capability(), stableWitness,
                                      ZC_ASSERT_NONNULL(ownerLocalBindings).asPtr());
  auto explicitCaptures = materializeExplicitClosureCaptures(
      skeleton.capability(), source, provenance.capability(), stableWitness,
      ZC_ASSERT_NONNULL(ownerLocalBindings).asPtr(), parsedSource);
  auto labels = materializeLabels(module, skeleton.capability(), source, provenance.capability(),
                                  stableWitness, allocation,
                                  ZC_ASSERT_NONNULL(scopeIdentities).asPtr(), parsedSource);
  auto controlTransfers =
      labels == zc::none
          ? zc::Maybe<zc::Vector<binder::ControlTransferFact>>()
          : materializeControlTransfers(skeleton.capability(), source, provenance.capability(),
                                        stableWitness, ZC_ASSERT_NONNULL(scopeIdentities).asPtr(),
                                        ZC_ASSERT_NONNULL(labels).asPtr(), parsedSource);
  auto failedLookups = materializeFailedLookups(provenance.capability(), stableWitness);
  auto deferredMembers =
      materializeDeferredMembers(source, provenance.capability(), stableWitness, parsedSource);
  if (nodeScopes == zc::none || ownerLocalFacts == zc::none || anonymousEntities == zc::none ||
      resolutions == zc::none || selfTypes == zc::none || thisBindings == zc::none ||
      shadowTargets == zc::none || closureFreeVariables == zc::none ||
      explicitCaptures == zc::none || labels == zc::none || controlTransfers == zc::none ||
      failedLookups == zc::none || deferredMembers == zc::none) {
    return zc::none;
  }
  if (!appendLabelResolutions(ZC_ASSERT_NONNULL(resolutions), ZC_ASSERT_NONNULL(labels).asPtr(),
                              ZC_ASSERT_NONNULL(controlTransfers).asPtr())) {
    return zc::none;
  }
  return MaterializedOwnerBody(zc::heap<Impl>(
      zc::mv(key), context, revision, zc::mv(fingerprint), module, zc::mv(skeleton), zc::mv(source),
      zc::mv(provenance), zc::mv(stableWitness), zc::mv(allocation),
      zc::mv(ZC_ASSERT_NONNULL(scopeIdentities)), zc::mv(ZC_ASSERT_NONNULL(nodeScopes)),
      zc::mv(ZC_ASSERT_NONNULL(ownerLocalFacts)), zc::mv(ZC_ASSERT_NONNULL(anonymousEntities)),
      zc::mv(ZC_ASSERT_NONNULL(resolutions)), zc::mv(ZC_ASSERT_NONNULL(selfTypes)),
      zc::mv(ZC_ASSERT_NONNULL(thisBindings)), zc::mv(ZC_ASSERT_NONNULL(shadowTargets)),
      zc::mv(ZC_ASSERT_NONNULL(labels)), zc::mv(ZC_ASSERT_NONNULL(controlTransfers)),
      zc::mv(ZC_ASSERT_NONNULL(closureFreeVariables)), zc::mv(ZC_ASSERT_NONNULL(explicitCaptures)),
      zc::mv(ZC_ASSERT_NONNULL(failedLookups)), zc::mv(ZC_ASSERT_NONNULL(deferredMembers))));
}

MaterializedOwnerBody MaterializedOwnerBody::clone() const {
  auto scopeIdentities =
      materializeScopeIdentities(impl->module, impl->allocation, impl->stableWitness);
  auto nodeScopes =
      materializeNodeScopes(impl->skeleton.capability(), impl->provenance.capability(),
                            impl->stableWitness, ZC_ASSERT_NONNULL(scopeIdentities).asPtr());
  zc::Vector<binder::LabelFact> labels(impl->labels.size());
  for (const auto& fact : impl->labels) {
    const auto& targetValue = fact.target.value();
    auto target =
        targetValue.is<binder::BlockLabelTarget>()
            ? binder::LabelTarget::block(targetValue.get<binder::BlockLabelTarget>().scope)
            : binder::LabelTarget::loop(targetValue.get<binder::LoopLabelTarget>().scope);
    labels.add(binder::LabelFact{
        binder::LabelId(fact.identity.owner().clone(), fact.identity.index()), fact.name.clone(),
        fact.owner.clone(), fact.statement, zc::mv(target), fact.source.clone()});
  }
  zc::Vector<binder::ControlTransferFact> controlTransfers(impl->controlTransfers.size());
  for (const auto& fact : impl->controlTransfers) {
    const auto& target = fact.target;
    if (target.is<binder::ExplicitLabelControlTarget>()) {
      const auto& label = target.get<binder::ExplicitLabelControlTarget>().label;
      controlTransfers.add(
          binder::ControlTransferFact{fact.node, fact.kind,
                                      binder::ControlTarget(binder::ExplicitLabelControlTarget{
                                          binder::LabelId(label.owner().clone(), label.index())}),
                                      fact.source.clone()});
      continue;
    }
    if (target.is<binder::LoopControlTarget>()) {
      controlTransfers.add(binder::ControlTransferFact{
          fact.node, fact.kind,
          binder::ControlTarget(
              binder::LoopControlTarget{target.get<binder::LoopControlTarget>().scope}),
          fact.source.clone()});
      continue;
    }
    controlTransfers.add(binder::ControlTransferFact{
        fact.node, fact.kind,
        binder::ControlTarget(
            binder::MatchControlTarget{target.get<binder::MatchControlTarget>().scope}),
        fact.source.clone()});
  }
  return MaterializedOwnerBody(zc::heap<Impl>(
      impl->key.clone(), impl->context, impl->revision, impl->fingerprint.clone(), impl->module,
      impl->skeleton.retain(), impl->source.clone(), impl->provenance.retain(),
      impl->stableWitness.clone(), impl->allocation.clone(),
      zc::mv(ZC_ASSERT_NONNULL(scopeIdentities)), zc::mv(ZC_ASSERT_NONNULL(nodeScopes)),
      cloneOwnerLocalBindingFacts(impl->ownerLocalBindings),
      cloneAnonymousEntityFacts(impl->anonymousEntities.asPtr()),
      cloneBindingResolutions(impl->resolutions.asPtr()), cloneSelfTypes(impl->selfTypes.asPtr()),
      cloneThisBindings(impl->thisBindings.asPtr()),
      cloneShadowTargetFacts(impl->shadowTargets.asPtr()), zc::mv(labels), zc::mv(controlTransfers),
      cloneClosureFreeVariableFacts(impl->closureFreeVariables.asPtr()),
      cloneExplicitClosureCaptureFacts(impl->explicitCaptures.asPtr()),
      cloneMaterializedFailedLookups(impl->failedLookups.asPtr()),
      cloneDeferredMemberFacts(impl->deferredMembers.asPtr())));
}

const incremental_binding_query::CompilationRootSetQueryKey& MaterializedOwnerBody::contextRoots()
    const noexcept {
  return impl->key.contextRoots();
}

const binder::StableOwnerBodyQueryKey& MaterializedOwnerBody::owner() const noexcept {
  return impl->key.body();
}

identity::SemanticContextBrand MaterializedOwnerBody::context() const noexcept {
  return impl->context;
}

query::DatabaseRevision MaterializedOwnerBody::revision() const noexcept { return impl->revision; }

const identity::ContextFingerprint& MaterializedOwnerBody::fingerprint() const noexcept {
  return impl->fingerprint;
}

identity::ModuleId MaterializedOwnerBody::module() const noexcept { return impl->module; }

const identity::SourceFileKey& MaterializedOwnerBody::source() const noexcept {
  return impl->source;
}

const MaterializedOwnerBody::SkeletonLease& MaterializedOwnerBody::skeletonLease() const noexcept {
  return impl->skeleton;
}

const MaterializedOwnerBody::ProvenanceLease& MaterializedOwnerBody::provenanceLease()
    const noexcept {
  return impl->provenance;
}

const binder::BoundOwnerBody& MaterializedOwnerBody::stableWitness() const noexcept {
  return impl->stableWitness;
}

const binder::OwnerAllocationRange& MaterializedOwnerBody::allocation() const noexcept {
  return impl->allocation;
}

zc::ArrayPtr<const binder::ScopeId> MaterializedOwnerBody::scopeIdentities() const noexcept {
  return impl->scopeIdentities.asPtr();
}

zc::Maybe<binder::ScopeId> MaterializedOwnerBody::scopeFor(
    const binder::StableScopeOwnerKey& scope) const noexcept {
  const auto scopes = stableWitness().scopes().values();
  if (scopes.size() != impl->scopeIdentities.size()) { return zc::none; }
  for (size_t index = 0; index < scopes.size(); ++index) {
    if (scopes[index].scope() == scope) { return impl->scopeIdentities[index]; }
  }
  return zc::none;
}

zc::ArrayPtr<const binder::NodeScopeFact> MaterializedOwnerBody::materializedNodeScopes()
    const noexcept {
  return impl->nodeScopes.asPtr();
}

zc::ArrayPtr<const binder::OwnerLocalBindingFact>
MaterializedOwnerBody::materializedOwnerLocalBindings() const noexcept {
  return impl->ownerLocalBindings.asPtr();
}

zc::ArrayPtr<const binder::AnonymousEntityFact>
MaterializedOwnerBody::materializedAnonymousEntities() const noexcept {
  return impl->anonymousEntities.asPtr();
}

zc::ArrayPtr<const binder::BindingResolution> MaterializedOwnerBody::materializedResolutions()
    const noexcept {
  return impl->resolutions.asPtr();
}

zc::ArrayPtr<const binder::BoundSelfType> MaterializedOwnerBody::materializedSelfTypes()
    const noexcept {
  return impl->selfTypes.asPtr();
}

zc::ArrayPtr<const binder::BoundThis> MaterializedOwnerBody::materializedThisBindings()
    const noexcept {
  return impl->thisBindings.asPtr();
}

zc::ArrayPtr<const binder::ShadowTargetFact> MaterializedOwnerBody::materializedShadowTargets()
    const noexcept {
  return impl->shadowTargets.asPtr();
}

zc::ArrayPtr<const binder::LabelFact> MaterializedOwnerBody::materializedLabels() const noexcept {
  return impl->labels.asPtr();
}

zc::ArrayPtr<const binder::ControlTransferFact>
MaterializedOwnerBody::materializedControlTransfers() const noexcept {
  return impl->controlTransfers.asPtr();
}

zc::ArrayPtr<const binder::ClosureFreeVariableFact>
MaterializedOwnerBody::materializedClosureFreeVariables() const noexcept {
  return impl->closureFreeVariables.asPtr();
}

zc::ArrayPtr<const binder::ExplicitClosureCaptureFact>
MaterializedOwnerBody::materializedExplicitClosureCaptures() const noexcept {
  return impl->explicitCaptures.asPtr();
}

zc::ArrayPtr<const binder::MaterializedFailedLookupFact>
MaterializedOwnerBody::materializedFailedLookups() const noexcept {
  return impl->failedLookups.asPtr();
}

zc::ArrayPtr<const binder::DeferredMemberFact> MaterializedOwnerBody::materializedDeferredMembers()
    const noexcept {
  return impl->deferredMembers.asPtr();
}

const binder::CanonicalSequence<binder::StableBodyScopeFact>& MaterializedOwnerBody::scopes()
    const noexcept {
  return stableWitness().scopes();
}

const binder::CanonicalSequence<binder::StableBodyNodeScopeFact>&
MaterializedOwnerBody::nodeScopes() const noexcept {
  return stableWitness().nodeScopes();
}

const binder::CanonicalSequence<binder::StableOwnerLocalBindingFact>&
MaterializedOwnerBody::bindings() const noexcept {
  return stableWitness().bindings();
}

const binder::CanonicalSequence<binder::StableResolutionFact>& MaterializedOwnerBody::resolutions()
    const noexcept {
  return stableWitness().resolutions();
}

const binder::CanonicalSequence<binder::StableDeferredMemberFact>&
MaterializedOwnerBody::deferredMembers() const noexcept {
  return stableWitness().deferredMembers();
}

const binder::CanonicalSequence<binder::StableSelfTypeFact>& MaterializedOwnerBody::selfTypes()
    const noexcept {
  return stableWitness().selfTypes();
}

const binder::CanonicalSequence<binder::StableThisBindingFact>&
MaterializedOwnerBody::thisBindings() const noexcept {
  return stableWitness().thisBindings();
}

const binder::CanonicalSequence<binder::StableShadowTargetFact>&
MaterializedOwnerBody::shadowTargets() const noexcept {
  return stableWitness().shadowTargets();
}

const binder::CanonicalSequence<binder::StableLabelFact>& MaterializedOwnerBody::labels()
    const noexcept {
  return stableWitness().labels();
}

const binder::CanonicalSequence<binder::StableControlTransferFact>&
MaterializedOwnerBody::controlTransfers() const noexcept {
  return stableWitness().controlTransfers();
}

const binder::CanonicalSequence<binder::StableClosureFreeVariableFact>&
MaterializedOwnerBody::closureFreeVariables() const noexcept {
  return stableWitness().closureFreeVariables();
}

const binder::CanonicalSequence<binder::StableExplicitClosureCaptureFact>&
MaterializedOwnerBody::explicitClosureCaptures() const noexcept {
  return stableWitness().explicitClosureCaptures();
}

const binder::CanonicalSequence<binder::StableFailedLookupFact>&
MaterializedOwnerBody::failedLookups() const noexcept {
  return stableWitness().failedLookups();
}

zc::Array<uint8_t> MaterializedOwnerBody::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(contextRoots().encodeCanonical().asPtr());
  encoder.encodeByteString(owner().encodeCanonical().asPtr());
  encoder.encodeUint64(revision().value());
  encoder.encodeByteString(fingerprint().digest().bytes());
  auto stable = binder::StableBindingCodec<binder::BoundOwnerBody>::encode(stableWitness());
  encoder.encodeByteString(stable.asPtr());
  auto allocation =
      binder::StableBindingCodec<binder::OwnerAllocationRange>::encode(this->allocation());
  encoder.encodeByteString(allocation.asPtr());
  return frame(kOwnerBodyWitnessDomain, encoder.finish().asPtr());
}

zc::Array<uint8_t> MaterializeOwnerBodyQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<MaterializeOwnerBodyQuery::Key> MaterializeOwnerBodyQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return incremental_binding_query::ContextualBodyOwnerKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<MaterializeOwnerBodyQuery> MaterializeOwnerBodyQuery::provide(
    query::CapabilityQueryContext<MaterializeOwnerBodyQuery>& context, const Key& key) {
  auto skeletonKey = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), key.body().module().clone());
  auto skeleton = context.getCapability<MaterializeModuleSkeletonQuery>(zc::mv(skeletonKey));
  if (skeleton.isSourceRejected()) { return forwardOwnerBodySourceRejection(skeleton); }
  if (skeleton.isKeyRejected()) { return forwardOwnerBodyKeyRejection(skeleton); }
  if (skeleton.isRuntimeRejected()) {
    return OwnerBodyProviderResult::runtimeRejected(skeleton.runtimeFailure());
  }
  if (!skeleton.isPublished()) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& skeletonValue = skeleton.lease().capability();
  auto membershipKey = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), key.body().module().clone());
  if (skeletonValue.contextRoots() != key.contextRoots() ||
      skeletonValue.revision() != context.snapshotRevision() ||
      !skeletonContainsOwner(skeletonValue.identities().stableWitness(), key.body()) ||
      !skeletonMembershipsMatch(context, membershipKey,
                                skeletonValue.identities().stableWitness())) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto provenance =
      context.getCapability<incremental_binding_query::OwnerBodyProvenanceQuery>(key.clone());
  if (provenance.isSourceRejected()) { return forwardOwnerBodySourceRejection(provenance); }
  if (provenance.isKeyRejected()) { return forwardOwnerBodyKeyRejection(provenance); }
  if (provenance.isRuntimeRejected()) {
    return OwnerBodyProviderResult::runtimeRejected(provenance.runtimeFailure());
  }
  if (!provenance.isPublished() || provenance.lease().revision() != skeletonValue.revision() ||
      provenance.lease().capability().owner() != key.body().owner() ||
      provenance.lease().capability().detachedProvenance().source().encode().asPtr() !=
          skeletonValue.source().encode().asPtr()) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto syntax = context.get<incremental_binding_query::OwnerBodySyntaxQuery>(key.clone());
  if (syntax.isRuntimeFailure()) {
    return OwnerBodyProviderResult::runtimeRejected(syntax.runtimeFailure());
  }
  if (syntax.kind() != query::QueryValueKind::Value ||
      syntax.value().owner() != key.body().owner()) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto sourceKey =
      identity::source_query::StableSourceQueryKey::fromVerified(skeletonValue.source());
  if (sourceKey == zc::none) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (parsed.isSourceRejected()) { return forwardOwnerBodySourceRejection(parsed); }
  if (parsed.isRuntimeRejected()) {
    return OwnerBodyProviderResult::runtimeRejected(parsed.runtimeFailure());
  }
  if (!parsed.isPublished() ||
      parsed.lease().capability().canonicalSourceKey() != skeletonValue.source().encode().asPtr()) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  zc::Maybe<OwnerBodyProviderResult> rejection;
  auto body = acquireOwnerBody(context, key, rejection);
  if (rejection != zc::none) { return zc::mv(ZC_ASSERT_NONNULL(rejection)); }
  if (body == zc::none) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto allocation = acquireOwnerAllocation(context, key, rejection);
  if (rejection != zc::none) { return zc::mv(ZC_ASSERT_NONNULL(rejection)); }
  if (allocation == zc::none) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto ownerContext = skeletonValue.context();
  const auto ownerRevision = skeletonValue.revision();
  auto ownerFingerprint = skeletonValue.fingerprint().clone();
  const auto ownerModule = skeletonValue.identities().module();
  auto ownerSource = skeletonValue.source().clone();
  auto ownerSkeleton = zc::mv(skeleton).takeLease();
  auto ownerProvenance = zc::mv(provenance).takeLease();
  auto candidate = MaterializedOwnerBody::from(
      key.clone(), ownerContext, ownerRevision, zc::mv(ownerFingerprint), ownerModule,
      zc::mv(ownerSkeleton), zc::mv(ownerSource), zc::mv(ownerProvenance),
      zc::mv(ZC_ASSERT_NONNULL(body)), zc::mv(ZC_ASSERT_NONNULL(allocation)), syntax.value(),
      parsed.lease().capability());
  if (candidate == zc::none) {
    return OwnerBodyProviderResult::runtimeRejected(query::QueryRuntimeFailure::InvariantViolation);
  }
  auto owned = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  auto stableWitness =
      query::CapabilityCandidateContract<MaterializeOwnerBodyQuery>::encode(*owned);
  return OwnerBodyProviderResult::candidate(zc::mv(owned), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> MaterializeOwnerBodyQuery::verify(
    query::CapabilityQueryContext<MaterializeOwnerBodyQuery>& context, const Key& key,
    const Capability& candidate) {
  auto skeletonKey = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), key.body().module().clone());
  auto skeleton = context.getCapability<MaterializeModuleSkeletonQuery>(zc::mv(skeletonKey));
  auto membershipKey = incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), key.body().module().clone());
  if (!skeleton.isPublished() || candidate.contextRoots() != key.contextRoots() ||
      !sameOwnerBody(candidate.owner(), key.body()) ||
      candidate.context() != skeleton.lease().capability().context() ||
      candidate.module() != skeleton.lease().capability().identities().module() ||
      candidate.source().encode().asPtr() !=
          skeleton.lease().capability().source().encode().asPtr() ||
      candidate.skeletonLease().stableWitness() != skeleton.lease().stableWitness() ||
      candidate.revision() != context.snapshotRevision() ||
      candidate.revision() != skeleton.lease().capability().revision() ||
      candidate.fingerprint().digest() != skeleton.lease().capability().fingerprint().digest() ||
      !skeletonContainsOwner(skeleton.lease().capability().identities().stableWitness(),
                             key.body()) ||
      !skeletonMembershipsMatch(context, membershipKey,
                                skeleton.lease().capability().identities().stableWitness())) {
    return zc::none;
  }
  auto provenance =
      context.getCapability<incremental_binding_query::OwnerBodyProvenanceQuery>(key.clone());
  if (!provenance.isPublished() || provenance.lease().revision() != candidate.revision() ||
      provenance.lease().capability().owner() != key.body().owner() ||
      provenance.lease().capability().detachedProvenance().source().encode().asPtr() !=
          candidate.source().encode().asPtr() ||
      candidate.provenanceLease().stableWitness() != provenance.lease().stableWitness()) {
    return zc::none;
  }
  auto syntax = context.get<incremental_binding_query::OwnerBodySyntaxQuery>(key.clone());
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(candidate.source());
  if (syntax.isRuntimeFailure() || syntax.kind() != query::QueryValueKind::Value ||
      syntax.value().owner() != key.body().owner() || sourceKey == zc::none) {
    return zc::none;
  }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (!parsed.isPublished() ||
      parsed.lease().capability().canonicalSourceKey() != candidate.source().encode().asPtr()) {
    return zc::none;
  }
  zc::Maybe<OwnerBodyProviderResult> rejection;
  auto body = acquireOwnerBody(context, key, rejection);
  if (rejection != zc::none || body == zc::none ||
      !(candidate.stableWitness() == ZC_ASSERT_NONNULL(body))) {
    return zc::none;
  }
  auto allocation = acquireOwnerAllocation(context, key, rejection);
  if (rejection != zc::none || allocation == zc::none ||
      !(candidate.allocation() == ZC_ASSERT_NONNULL(allocation))) {
    return zc::none;
  }
  const auto expectedContext = skeleton.lease().capability().context();
  const auto expectedRevision = skeleton.lease().capability().revision();
  auto expectedFingerprint = skeleton.lease().capability().fingerprint().clone();
  const auto expectedModule = skeleton.lease().capability().identities().module();
  auto expectedSource = skeleton.lease().capability().source().clone();
  auto expectedSkeleton = zc::mv(skeleton).takeLease();
  auto expectedProvenance = zc::mv(provenance).takeLease();
  auto expected = MaterializedOwnerBody::from(
      key.clone(), expectedContext, expectedRevision, zc::mv(expectedFingerprint), expectedModule,
      zc::mv(expectedSkeleton), zc::mv(expectedSource), zc::mv(expectedProvenance),
      zc::mv(ZC_ASSERT_NONNULL(body)), zc::mv(ZC_ASSERT_NONNULL(allocation)), syntax.value(),
      parsed.lease().capability());
  if (expected == zc::none ||
      !sameScopeIdentities(candidate.scopeIdentities(),
                           ZC_ASSERT_NONNULL(expected).scopeIdentities()) ||
      !sameNodeScopes(candidate.materializedNodeScopes(),
                      ZC_ASSERT_NONNULL(expected).materializedNodeScopes()) ||
      !sameOwnerLocalBindings(candidate.materializedOwnerLocalBindings(),
                              ZC_ASSERT_NONNULL(expected).materializedOwnerLocalBindings()) ||
      !sameBindingResolutions(candidate.materializedResolutions(),
                              ZC_ASSERT_NONNULL(expected).materializedResolutions()) ||
      !sameSelfTypes(candidate.materializedSelfTypes(),
                     ZC_ASSERT_NONNULL(expected).materializedSelfTypes()) ||
      !sameThisBindings(candidate.materializedThisBindings(),
                        ZC_ASSERT_NONNULL(expected).materializedThisBindings()) ||
      !sameShadowTargetFacts(candidate.materializedShadowTargets(),
                             ZC_ASSERT_NONNULL(expected).materializedShadowTargets()) ||
      !sameLabelFacts(candidate.materializedLabels(),
                      ZC_ASSERT_NONNULL(expected).materializedLabels()) ||
      !sameControlTransferFacts(candidate.materializedControlTransfers(),
                                ZC_ASSERT_NONNULL(expected).materializedControlTransfers()) ||
      !sameClosureFreeVariableFacts(
          candidate.materializedClosureFreeVariables(),
          ZC_ASSERT_NONNULL(expected).materializedClosureFreeVariables()) ||
      !sameExplicitClosureCaptureFacts(
          candidate.materializedExplicitClosureCaptures(),
          ZC_ASSERT_NONNULL(expected).materializedExplicitClosureCaptures()) ||
      !sameMaterializedFailedLookups(candidate.materializedFailedLookups(),
                                     ZC_ASSERT_NONNULL(expected).materializedFailedLookups()) ||
      !sameDeferredMemberFacts(candidate.materializedDeferredMembers(),
                               ZC_ASSERT_NONNULL(expected).materializedDeferredMembers())) {
    return zc::none;
  }
  return candidate.encodeCanonical();
}

namespace {

using BoundModuleProviderResult = query::CapabilityProviderResult<VerifyBoundModuleQuery>;

template <typename SourceDescriptor>
BoundModuleProviderResult forwardBoundModuleSourceRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  using SourceContract =
      query::CapabilityFailureContract<SourceDescriptor,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  using TargetContract =
      query::CapabilityFailureContract<VerifyBoundModuleQuery,
                                       query::SourceRejection<diagnostics::DiagnosticFact>>;
  auto diagnostics = TargetContract::decode(SourceContract::encode(source.diagnostics()).asPtr());
  if (diagnostics == zc::none) {
    return BoundModuleProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  return BoundModuleProviderResult::sourceRejected<diagnostics::DiagnosticFact>(
      zc::mv(ZC_ASSERT_NONNULL(diagnostics)));
}

template <typename SourceDescriptor>
BoundModuleProviderResult forwardBoundModuleKeyRejection(
    const query::CapabilityDemandResult<SourceDescriptor>& source) {
  return BoundModuleProviderResult::keyRejected<binder::BinderKeyFailure>(
      source.keyFailure().clone());
}

bool graphContainsSource(const MaterializedModuleGraph& graph,
                         const identity::SourceFileKey& source) {
  for (const auto& entry : graph.sources()) {
    if (entry.key().encode().asPtr() == source.encode().asPtr()) { return true; }
  }
  return false;
}

struct MaterializedModuleOwnership final {
  identity::CompilationUnitId compilationUnit;
  identity::CrateId crate;
};

zc::Maybe<MaterializedModuleOwnership> materializedModuleOwnership(
    const MaterializedModuleGraph& graph, const identity::ModuleKey& module) {
  for (const auto& materializedModule : graph.modules()) {
    if (materializedModule.key().encode().asPtr() != module.encode().asPtr()) { continue; }
    for (const auto& materializedCrate : graph.crates()) {
      if (materializedCrate.key().encode().asPtr() != module.crate().encode().asPtr()) { continue; }
      for (const auto& materializedUnit : graph.units()) {
        if (materializedUnit.key().encode().asPtr() !=
            materializedCrate.key().unit().encode().asPtr()) {
          continue;
        }
        return MaterializedModuleOwnership{materializedUnit.handle(), materializedCrate.handle()};
      }
      return zc::none;
    }
    return zc::none;
  }
  return zc::none;
}

bool validMaterializedOwnerBodyFacts(const MaterializedModuleSkeleton& skeleton,
                                     const parser::CanonicalParsedSource& parsedSource,
                                     const MaterializedOwnerBody& body) {
  if (body.skeletonLease().revision() != skeleton.revision() ||
      body.skeletonLease().capability().encodeCanonical().asPtr() !=
          skeleton.encodeCanonical().asPtr() ||
      body.materializedNodeScopes().size() != body.stableWitness().nodeScopes().values().size() ||
      body.materializedOwnerLocalBindings().size() !=
          body.stableWitness().bindings().values().size() ||
      body.materializedResolutions().size() < body.stableWitness().resolutions().values().size() ||
      body.materializedSelfTypes().size() != body.stableWitness().selfTypes().values().size() ||
      body.materializedThisBindings().size() !=
          body.stableWitness().thisBindings().values().size() ||
      body.materializedShadowTargets().size() !=
          body.stableWitness().shadowTargets().values().size() ||
      body.materializedLabels().size() != body.stableWitness().labels().values().size() ||
      body.materializedControlTransfers().size() !=
          body.stableWitness().controlTransfers().values().size() ||
      body.materializedClosureFreeVariables().size() !=
          body.stableWitness().closureFreeVariables().values().size() ||
      body.materializedExplicitClosureCaptures().size() !=
          body.stableWitness().explicitClosureCaptures().values().size() ||
      body.materializedFailedLookups().size() !=
          body.stableWitness().failedLookups().values().size() ||
      body.materializedDeferredMembers().size() !=
          body.stableWitness().deferredMembers().values().size()) {
    return false;
  }
  auto expectedDeferredMembers = materializeDeferredMembers(
      body.source(), body.provenanceLease().capability(), body.stableWitness(), parsedSource);
  if (expectedDeferredMembers == zc::none ||
      !sameDeferredMemberFacts(body.materializedDeferredMembers(),
                               ZC_ASSERT_NONNULL(expectedDeferredMembers).asPtr())) {
    return false;
  }
  auto ownerLocalIdentities = materializeOwnerLocalBindings(
      body.context(), body.module(), body.allocation(), body.stableWitness());
  auto expectedAnonymousEntities = MaterializedOwnerBody::materializeAnonymousEntities(
      body.context(), body.module(), body.allocation(), body.source(),
      body.provenanceLease().capability(), body.stableWitness(), parsedSource);
  if (expectedAnonymousEntities == zc::none ||
      !sameAnonymousEntityFacts(body.materializedAnonymousEntities(),
                                ZC_ASSERT_NONNULL(expectedAnonymousEntities).asPtr())) {
    return false;
  }
  auto expectedLabels = MaterializedOwnerBody::materializeLabels(
      body.module(), skeleton, body.source(), body.provenanceLease().capability(),
      body.stableWitness(), body.allocation(), body.scopeIdentities(), parsedSource);
  auto expectedControlTransfers =
      expectedLabels == zc::none
          ? zc::Maybe<zc::Vector<binder::ControlTransferFact>>()
          : MaterializedOwnerBody::materializeControlTransfers(
                skeleton, body.source(), body.provenanceLease().capability(), body.stableWitness(),
                body.scopeIdentities(), ZC_ASSERT_NONNULL(expectedLabels).asPtr(), parsedSource);
  if (expectedLabels == zc::none || expectedControlTransfers == zc::none ||
      !sameLabelFacts(body.materializedLabels(), ZC_ASSERT_NONNULL(expectedLabels).asPtr()) ||
      !sameControlTransferFacts(body.materializedControlTransfers(),
                                ZC_ASSERT_NONNULL(expectedControlTransfers).asPtr())) {
    return false;
  }
  auto expectedResolutions =
      ownerLocalIdentities == zc::none
          ? zc::Maybe<zc::Vector<binder::BindingResolution>>()
          : materializeResolutions(skeleton, body.provenanceLease().capability(),
                                   body.stableWitness(),
                                   ZC_ASSERT_NONNULL(ownerLocalIdentities).asPtr());
  if (expectedResolutions == zc::none ||
      !appendLabelResolutions(ZC_ASSERT_NONNULL(expectedResolutions),
                              ZC_ASSERT_NONNULL(expectedLabels).asPtr(),
                              ZC_ASSERT_NONNULL(expectedControlTransfers).asPtr()) ||
      !sameBindingResolutions(body.materializedResolutions(),
                              ZC_ASSERT_NONNULL(expectedResolutions).asPtr())) {
    return false;
  }
  auto expectedThisBindings =
      materializeThisBindings(skeleton, body.source(), body.provenanceLease().capability(),
                              body.stableWitness(), parsedSource);
  if (expectedThisBindings == zc::none ||
      !sameThisBindings(body.materializedThisBindings(),
                        ZC_ASSERT_NONNULL(expectedThisBindings).asPtr())) {
    return false;
  }
  auto expectedSelfTypes =
      materializeSelfTypes(skeleton, body.source(), body.provenanceLease().capability(),
                           body.stableWitness(), parsedSource);
  if (expectedSelfTypes == zc::none ||
      !sameSelfTypes(body.materializedSelfTypes(), ZC_ASSERT_NONNULL(expectedSelfTypes).asPtr())) {
    return false;
  }
  auto expectedShadowTargets =
      ownerLocalIdentities == zc::none
          ? zc::Maybe<zc::Vector<binder::ShadowTargetFact>>()
          : materializeShadowTargets(skeleton, body.stableWitness(),
                                     ZC_ASSERT_NONNULL(ownerLocalIdentities).asPtr());
  if (expectedShadowTargets == zc::none ||
      !sameShadowTargetFacts(body.materializedShadowTargets(),
                             ZC_ASSERT_NONNULL(expectedShadowTargets).asPtr())) {
    return false;
  }
  auto expectedClosureFreeVariables =
      ownerLocalIdentities == zc::none
          ? zc::Maybe<zc::Vector<binder::ClosureFreeVariableFact>>()
          : materializeClosureFreeVariables(skeleton, body.provenanceLease().capability(),
                                            body.stableWitness(),
                                            ZC_ASSERT_NONNULL(ownerLocalIdentities).asPtr());
  if (expectedClosureFreeVariables == zc::none ||
      !sameClosureFreeVariableFacts(body.materializedClosureFreeVariables(),
                                    ZC_ASSERT_NONNULL(expectedClosureFreeVariables).asPtr())) {
    return false;
  }
  auto expectedExplicitCaptures =
      ownerLocalIdentities == zc::none
          ? zc::Maybe<zc::Vector<binder::ExplicitClosureCaptureFact>>()
          : materializeExplicitClosureCaptures(
                skeleton, body.source(), body.provenanceLease().capability(), body.stableWitness(),
                ZC_ASSERT_NONNULL(ownerLocalIdentities).asPtr(), parsedSource);
  if (expectedExplicitCaptures == zc::none ||
      !sameExplicitClosureCaptureFacts(body.materializedExplicitClosureCaptures(),
                                       ZC_ASSERT_NONNULL(expectedExplicitCaptures).asPtr())) {
    return false;
  }
  auto expectedFailedLookups =
      materializeFailedLookups(body.provenanceLease().capability(), body.stableWitness());
  if (expectedFailedLookups == zc::none ||
      !sameMaterializedFailedLookups(body.materializedFailedLookups(),
                                     ZC_ASSERT_NONNULL(expectedFailedLookups).asPtr())) {
    return false;
  }
  for (const auto& fact : body.stableWitness().nodeScopes().values()) {
    zc::Maybe<ast::NodeId> node;
    for (const auto& entry : body.provenanceLease().capability().detachedProvenance().entries()) {
      if (entry.path != fact.nodePath()) { continue; }
      if (node != zc::none) { return false; }
      node = entry.node;
    }
    auto scope = skeleton.scopeFor(fact.scope());
    if (scope == zc::none) { scope = body.scopeFor(fact.scope()); }
    if (node == zc::none || scope == zc::none) { return false; }
    size_t matches = 0;
    for (const auto& materialized : body.materializedNodeScopes()) {
      if (materialized.node == ZC_ASSERT_NONNULL(node) &&
          materialized.scope == ZC_ASSERT_NONNULL(scope)) {
        ++matches;
      }
    }
    if (matches != 1) { return false; }
  }
  if (static_cast<uint64_t>(body.allocation().ownerLocalBegin()) +
          body.materializedOwnerLocalBindings().size() >
      static_cast<uint64_t>(0xffffffffu)) {
    return false;
  }
  const auto& provenance = body.provenanceLease().capability().detachedProvenance();
  const auto& tree = parsedSource.tree();
  for (size_t index = 0; index < body.materializedOwnerLocalBindings().size(); ++index) {
    const auto& materialized = body.materializedOwnerLocalBindings()[index];
    const auto& stable = body.stableWitness().bindings().values()[index];
    const auto identity = materialized.identity;
    if (!identity.belongsTo(skeleton.context()) || !identity.belongsTo(body.module()) ||
        identity.index() != body.allocation().ownerLocalBegin() + index ||
        materialized.kind != stable.kind() || materialized.name != stable.name() ||
        materialized.nameSpace != stable.nameSpace() ||
        materialized.activation != stable.activation()) {
      return false;
    }
    zc::Maybe<const binder::ModuleBodyProvenanceEntry&> declaration;
    for (const auto& entry : provenance.entries()) {
      if (entry.path != stable.key().path()) { continue; }
      if (declaration != zc::none) { return false; }
      declaration = entry;
    }
    auto scope = skeleton.scopeFor(stable.declaringScope());
    if (scope == zc::none) { scope = body.scopeFor(stable.declaringScope()); }
    if (declaration == zc::none || materialized.node != ZC_ASSERT_NONNULL(declaration).node ||
        scope == zc::none || materialized.declaringScope != scope ||
        !materialized.source.belongsTo(body.source()) ||
        materialized.source.byteStart() != ZC_ASSERT_NONNULL(declaration).byteStart ||
        materialized.source.byteEnd() != ZC_ASSERT_NONNULL(declaration).byteEnd) {
      return false;
    }
    const auto& site = materialized.site.value();
    if (materialized.kind == binder::OwnerLocalBindingKind::CallableParameter ||
        materialized.kind == binder::OwnerLocalBindingKind::GenericParameter) {
      if (!site.is<binder::DeclarationDefinitionSite>() ||
          site.get<binder::DeclarationDefinitionSite>().node !=
              ZC_ASSERT_NONNULL(declaration).node) {
        return false;
      }
      continue;
    }
    if ((materialized.kind != binder::OwnerLocalBindingKind::Local &&
         materialized.kind != binder::OwnerLocalBindingKind::PatternBinding) ||
        !site.is<binder::PatternBindingSite>()) {
      return false;
    }
    const auto& pattern = site.get<binder::PatternBindingSite>();
    if (!tree.contains(pattern.introducer)) { return false; }
    const auto introducerKind = tree.node(pattern.introducer).kind;
    if ((materialized.kind == binder::OwnerLocalBindingKind::Local &&
         introducerKind != ast::SyntaxKind::VariableDeclarator) ||
        (materialized.kind == binder::OwnerLocalBindingKind::PatternBinding &&
         introducerKind != ast::SyntaxKind::ForInStatement &&
         introducerKind != ast::SyntaxKind::MatchArmStmt)) {
      return false;
    }
    zc::Maybe<const binder::ModuleBodyProvenanceEntry&> introducer;
    for (const auto& entry : provenance.entries()) {
      if (entry.node != pattern.introducer) { continue; }
      if (introducer != zc::none) { return false; }
      introducer = entry;
    }
    if (introducer == zc::none ||
        ZC_ASSERT_NONNULL(introducer).path.components().size() >=
            stable.key().path().components().size() ||
        pattern.patternPath.size() != stable.key().path().components().size() -
                                          ZC_ASSERT_NONNULL(introducer).path.components().size()) {
      return false;
    }
    for (size_t component = 0; component < ZC_ASSERT_NONNULL(introducer).path.components().size();
         ++component) {
      if (ZC_ASSERT_NONNULL(introducer).path.components()[component] !=
          stable.key().path().components()[component]) {
        return false;
      }
    }
    for (size_t component = 0; component < pattern.patternPath.size(); ++component) {
      if (pattern.patternPath[component] !=
          stable.key().path().components()[ZC_ASSERT_NONNULL(introducer).path.components().size() +
                                           component]) {
        return false;
      }
    }
  }
  return true;
}

bool validMaterializedSkeletonFacts(const MaterializedModuleSkeleton& skeleton,
                                    const parser::CanonicalParsedSource& parsedSource) {
  const auto& stable = skeleton.identities().stableWitness();
  if (skeleton.materializedDefinitions().size() != stable.declarations().values().size() ||
      skeleton.materializedGenericParameters().size() !=
          stable.genericParameterDeclarations().values().size() ||
      skeleton.materializedCallableParameters().size() !=
          stable.callableParameterDeclarations().values().size() ||
      skeleton.materializedModuleAliases().size() != stable.moduleAliases().values().size() ||
      skeleton.materializedImports().size() != materializedImportCount(stable) ||
      skeleton.materializedLocalExports().size() != stable.localExports().values().size() ||
      skeleton.materializedImplementations().size() !=
          stable.implementationOccurrences().values().size() ||
      skeleton.materializedNodeScopes().size() != stable.nodeScopes().values().size() ||
      skeleton.materializedScopes().size() != stable.scopes().values().size() ||
      skeleton.scopeIdentities().size() != stable.scopes().values().size()) {
    return false;
  }
  auto expectedFailedLookups = materializeSkeletonFailedLookups(skeleton.provenance(), stable);
  return expectedFailedLookups != zc::none &&
         sameSkeletonFailedLookups(skeleton.materializedFailedLookups(),
                                   ZC_ASSERT_NONNULL(expectedFailedLookups).asPtr()) &&
         parsedSource.canonicalSourceKey() == skeleton.source().encode().asPtr();
}

bool validBoundModuleLeaseLineage(
    const MaterializedModuleGraph& graph, const MaterializedModuleSkeleton& skeleton,
    const identity::SourceFileKey& source,
    const VerifiedBoundModule::ParsedSourceLease& parsedSource,
    zc::ArrayPtr<const VerifiedBoundModule::OwnerBodyLease> ownerBodies) {
  if (graph.witness().contextRoots() != skeleton.contextRoots() ||
      graph.context() != skeleton.context() || graph.revision() != skeleton.revision() ||
      graph.witness().fingerprint().digest() != skeleton.fingerprint().digest() ||
      skeleton.graphLease().revision() != graph.revision() ||
      !skeleton.graphLease().capability().witness().sameAs(graph.witness()) ||
      !graphContainsModule(graph, skeleton.module()) || !graphContainsSource(graph, source) ||
      skeleton.source().encode().asPtr() != source.encode().asPtr() ||
      skeleton.dependencyProvenanceLease().revision() != skeleton.revision() ||
      skeleton.dependencyProvenanceLease().capability().module().encode().asPtr() !=
          skeleton.module().encode().asPtr() ||
      skeleton.dependencyProvenanceLease().capability().source().encode().asPtr() !=
          source.encode().asPtr() ||
      skeleton.identityAdmissionLease().revision() != skeleton.revision() ||
      skeleton.identityAdmissionLease().capability().module().encode().asPtr() !=
          skeleton.module().encode().asPtr() ||
      skeleton.identityAdmissionLease().capability().source().encode().asPtr() !=
          source.encode().asPtr() ||
      parsedSource.revision() != skeleton.revision() ||
      parsedSource.capability().canonicalSourceKey() != source.encode().asPtr() ||
      ownerBodies.size() != skeleton.identities().stableWitness().bodyOwners().values().size() ||
      !validMaterializedSkeletonFacts(skeleton, parsedSource.capability())) {
    return false;
  }
  size_t dependencyIndex = 0;
  for (const auto& edge : graph.requestEdges()) {
    auto requester = materializedModuleForHandle(graph, edge.requester());
    auto dependency = materializedModuleForHandle(graph, edge.dependency());
    if (requester == zc::none || dependency == zc::none) { return false; }
    if (!sameSkeletonModule(ZC_ASSERT_NONNULL(requester).key(), skeleton.module())) { continue; }
    if (sameSkeletonModule(ZC_ASSERT_NONNULL(dependency).key(), skeleton.module()) ||
        dependencyIndex > skeleton.dependencySkeletonLeases().size()) {
      return false;
    }
    if (containsDependencySkeleton(skeleton.dependencySkeletonLeases().first(dependencyIndex),
                                   ZC_ASSERT_NONNULL(dependency).key())) {
      continue;
    }
    if (dependencyIndex >= skeleton.dependencySkeletonLeases().size()) { return false; }
    const auto& dependencyLease = skeleton.dependencySkeletonLeases()[dependencyIndex];
    const auto& dependencySkeleton = dependencyLease.capability();
    if (dependencyLease.revision() != skeleton.revision() ||
        dependencySkeleton.contextRoots() != skeleton.contextRoots() ||
        dependencySkeleton.context() != skeleton.context() ||
        dependencySkeleton.revision() != skeleton.revision() ||
        dependencySkeleton.fingerprint().digest() != skeleton.fingerprint().digest() ||
        !sameSkeletonModule(dependencySkeleton.module(), ZC_ASSERT_NONNULL(dependency).key()) ||
        dependencySkeleton.graphLease().revision() != graph.revision() ||
        !dependencySkeleton.graphLease().capability().witness().sameAs(graph.witness())) {
      return false;
    }
    ++dependencyIndex;
  }
  if (dependencyIndex != skeleton.dependencySkeletonLeases().size()) { return false; }
  zc::Vector<binder::BoundOwnerBody> bodyWitnesses(ownerBodies.size());
  for (size_t index = 0; index < ownerBodies.size(); ++index) {
    const auto& body = ownerBodies[index].capability();
    const auto& expected = skeleton.identities().stableWitness().bodyOwners().values()[index];
    if (ownerBodies[index].revision() != skeleton.revision() ||
        body.contextRoots() != skeleton.contextRoots() || body.context() != skeleton.context() ||
        body.module() != skeleton.identities().module() || body.revision() != skeleton.revision() ||
        body.fingerprint().digest() != skeleton.fingerprint().digest() ||
        body.source().encode().asPtr() != source.encode().asPtr() ||
        body.provenanceLease().revision() != skeleton.revision() ||
        body.provenanceLease().capability().owner() != body.owner().owner() ||
        body.provenanceLease().capability().detachedProvenance().source().encode().asPtr() !=
            source.encode().asPtr() ||
        !sameOwnerBody(body.owner(), expected) ||
        body.scopeIdentities().size() != body.stableWitness().scopes().values().size() ||
        !validMaterializedOwnerBodyFacts(skeleton, parsedSource.capability(), body)) {
      return false;
    }
    for (size_t index = 0; index < body.scopeIdentities().size(); ++index) {
      const auto identity = body.scopeIdentities()[index];
      if (!identity.belongsTo(skeleton.context()) || identity.module() != body.module() ||
          identity.index() != body.allocation().scopeBegin() + index) {
        return false;
      }
    }
    bodyWitnesses.add(body.stableWitness().clone());
  }
  auto allocationPlan = binder::ModuleBindingAllocationPlanner::from(
      skeleton.identities().stableWitness(), bodyWitnesses.asPtr().asConst());
  if (allocationPlan == zc::none ||
      ZC_ASSERT_NONNULL(allocationPlan).owners().values().size() != ownerBodies.size()) {
    return false;
  }
  for (size_t index = 0; index < ownerBodies.size(); ++index) {
    if (!(ownerBodies[index].capability().allocation() ==
          ZC_ASSERT_NONNULL(allocationPlan).owners().values()[index])) {
      return false;
    }
  }
  return true;
}

bool sameBoundModuleLeaseLineage(
    const VerifiedBoundModule& candidate, const VerifiedBoundModule::GraphLease& graph,
    const VerifiedBoundModule::SkeletonLease& skeleton,
    const VerifiedBoundModule::ParsedSourceLease& parsedSource,
    zc::ArrayPtr<const VerifyBoundModuleQuery::Capability::OwnerBodyLease> ownerBodies) {
  if (candidate.graphLease().stableWitness() != graph.stableWitness() ||
      candidate.skeletonLease().stableWitness() != skeleton.stableWitness() ||
      candidate.parsedSourceLease().stableWitness() != parsedSource.stableWitness() ||
      candidate.ownerBodyLeases().size() != ownerBodies.size()) {
    return false;
  }
  for (size_t index = 0; index < ownerBodies.size(); ++index) {
    if (candidate.ownerBodyLeases()[index].stableWitness() != ownerBodies[index].stableWitness()) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct VerifiedBoundModule::Impl final {
  Impl(GraphLease&& graph, SkeletonLease&& skeleton, identity::SourceFileKey&& source,
       ParsedSourceLease&& parsedSource, identity::CompilationUnitId compilationUnit,
       identity::CrateId crate, zc::Vector<OwnerBodyLease>&& ownerBodies,
       binder::ImmutableDefinitionInventory&& definitions,
       binder::ImmutableBindingMetadata&& bindings) noexcept
      : graph(zc::mv(graph)),
        skeleton(zc::mv(skeleton)),
        source(zc::mv(source)),
        parsedSource(zc::mv(parsedSource)),
        compilationUnit(compilationUnit),
        crate(crate),
        ownerBodies(zc::mv(ownerBodies)),
        definitions(zc::mv(definitions)),
        bindings(zc::mv(bindings)) {}

  GraphLease graph;
  SkeletonLease skeleton;
  identity::SourceFileKey source;
  ParsedSourceLease parsedSource;
  identity::CompilationUnitId compilationUnit;
  identity::CrateId crate;
  zc::Vector<OwnerBodyLease> ownerBodies;
  binder::ImmutableDefinitionInventory definitions;
  binder::ImmutableBindingMetadata bindings;
};

VerifiedBoundModule::VerifiedBoundModule(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedBoundModule::~VerifiedBoundModule() noexcept(false) = default;
VerifiedBoundModule::VerifiedBoundModule(VerifiedBoundModule&&) noexcept = default;
VerifiedBoundModule& VerifiedBoundModule::operator=(VerifiedBoundModule&&) noexcept = default;

zc::Maybe<VerifiedBoundModule> VerifiedBoundModule::from(GraphLease&& graph,
                                                         SkeletonLease&& skeleton,
                                                         identity::SourceFileKey&& source,
                                                         ParsedSourceLease&& parsedSource,
                                                         zc::Vector<OwnerBodyLease>&& ownerBodies) {
  if (graph.revision() != graph.capability().revision() ||
      skeleton.revision() != skeleton.capability().revision() ||
      !validBoundModuleLeaseLineage(graph.capability(), skeleton.capability(), source, parsedSource,
                                    ownerBodies.asPtr())) {
    return zc::none;
  }
  auto ownership = materializedModuleOwnership(graph.capability(), skeleton.capability().module());
  if (ownership == zc::none ||
      skeleton.capability().bindingSurface().sourceModule() !=
          skeleton.capability().identities().module() ||
      skeleton.capability().bindingSurface().sourceCompilationUnit() !=
          ZC_ASSERT_NONNULL(ownership).compilationUnit) {
    return zc::none;
  }
  zc::Vector<binder::BoundOwnerBody> definitionBodies;
  zc::Vector<binder::BoundOwnerBody> bindingBodies;
  zc::Vector<binder::OwnerLocalBindingFact> ownerLocalBindings;
  zc::Vector<binder::AnonymousEntityFact> anonymousEntities;
  for (const auto& ownerBody : ownerBodies) {
    definitionBodies.add(ownerBody.capability().stableWitness().clone());
    bindingBodies.add(ownerBody.capability().stableWitness().clone());
    appendOwnerLocalBindingFacts(ownerLocalBindings,
                                 ownerBody.capability().materializedOwnerLocalBindings());
    appendAnonymousEntityFacts(anonymousEntities,
                               ownerBody.capability().materializedAnonymousEntities());
  }
  auto definitions = binder::ImmutableDefinitionInventory::from(
      skeleton.capability().identities().clone(), zc::mv(definitionBodies),
      skeleton.capability().materializedDefinitions(),
      skeleton.capability().materializedGenericParameters(),
      skeleton.capability().materializedCallableParameters(),
      skeleton.capability().materializedImplementations(), ownerLocalBindings.asPtr(),
      anonymousEntities.asPtr());
  if (definitions == zc::none) { return zc::none; }
  auto materializedFacts =
      aggregateMaterializedBindingFacts(skeleton.capability(), ownerBodies.asPtr());
  auto bindings = binder::ImmutableBindingMetadata::from(
      skeleton.capability().context(), skeleton.capability().revision(),
      skeleton.capability().fingerprint(),
      skeleton.capability().identities().stableWitness().clone(), zc::mv(bindingBodies),
      materializedFacts.view());
  if (bindings == zc::none) { return zc::none; }
  return VerifiedBoundModule(
      zc::heap<Impl>(zc::mv(graph), zc::mv(skeleton), zc::mv(source), zc::mv(parsedSource),
                     ZC_ASSERT_NONNULL(ownership).compilationUnit,
                     ZC_ASSERT_NONNULL(ownership).crate, zc::mv(ownerBodies),
                     zc::mv(ZC_ASSERT_NONNULL(definitions)), zc::mv(ZC_ASSERT_NONNULL(bindings))));
}

const incremental_binding_query::CompilationRootSetQueryKey& VerifiedBoundModule::contextRoots()
    const noexcept {
  return skeletonLease().capability().contextRoots();
}

const identity::ModuleKey& VerifiedBoundModule::module() const noexcept {
  return skeletonLease().capability().module();
}

identity::SemanticContextBrand VerifiedBoundModule::context() const noexcept {
  return skeletonLease().capability().context();
}

query::DatabaseRevision VerifiedBoundModule::revision() const noexcept {
  return skeletonLease().capability().revision();
}

const identity::ContextFingerprint& VerifiedBoundModule::fingerprint() const noexcept {
  return skeletonLease().capability().fingerprint();
}

identity::CompilationUnitId VerifiedBoundModule::compilationUnit() const noexcept {
  return impl->compilationUnit;
}

identity::CrateId VerifiedBoundModule::crate() const noexcept { return impl->crate; }

const identity::SourceFileKey& VerifiedBoundModule::source() const noexcept { return impl->source; }

const VerifiedBoundModule::GraphLease& VerifiedBoundModule::graphLease() const noexcept {
  return impl->graph;
}

const VerifiedBoundModule::SkeletonLease& VerifiedBoundModule::skeletonLease() const noexcept {
  return impl->skeleton;
}

const VerifiedBoundModule::ParsedSourceLease& VerifiedBoundModule::parsedSourceLease()
    const noexcept {
  return impl->parsedSource;
}

zc::ArrayPtr<const VerifiedBoundModule::OwnerBodyLease> VerifiedBoundModule::ownerBodyLeases()
    const noexcept {
  return impl->ownerBodies.asPtr();
}

const binder::ImmutableDefinitionInventory& VerifiedBoundModule::definitions() const noexcept {
  return impl->definitions;
}

const binder::ImmutableBindingMetadata& VerifiedBoundModule::bindings() const noexcept {
  return impl->bindings;
}

const binder::VerifiedExportSurface& VerifiedBoundModule::bindingSurface() const noexcept {
  return skeletonLease().capability().bindingSurface();
}

zc::Array<uint8_t> VerifiedBoundModule::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeByteString(contextRoots().encodeCanonical().asPtr());
  encoder.encodeByteString(module().encode().asPtr());
  encoder.encodeUint64(revision().value());
  encoder.encodeByteString(fingerprint().digest().bytes());
  encoder.encodeByteString(source().encode().asPtr());
  encoder.encodeByteString(graphLease().stableWitness());
  encoder.encodeByteString(skeletonLease().stableWitness());
  encoder.encodeByteString(parsedSourceLease().stableWitness());
  encoder.encodeUint64(ownerBodyLeases().size());
  for (const auto& body : ownerBodyLeases()) { encoder.encodeByteString(body.stableWitness()); }
  return frame(kVerifiedBoundModuleWitnessDomain, encoder.finish().asPtr());
}

struct CheckerBoundModuleView::Impl final {
  Impl(BoundModuleLease&& lease, identity::SourceFileId sourceFile,
       binder::CanonicalParsedModule&& parsedModule) noexcept
      : lease(zc::mv(lease)), sourceFile(sourceFile), parsedModule(zc::mv(parsedModule)) {}

  BoundModuleLease lease;
  identity::SourceFileId sourceFile;
  binder::CanonicalParsedModule parsedModule;
};

CheckerBoundModuleView::CheckerBoundModuleView(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
CheckerBoundModuleView::~CheckerBoundModuleView() noexcept(false) = default;
CheckerBoundModuleView::CheckerBoundModuleView(CheckerBoundModuleView&&) noexcept = default;
CheckerBoundModuleView& CheckerBoundModuleView::operator=(CheckerBoundModuleView&&) noexcept =
    default;

zc::Maybe<CheckerBoundModuleView> CheckerBoundModuleView::from(BoundModuleLease&& lease) {
  const auto& boundModule = lease.capability();
  if (lease.revision() != boundModule.revision() || !boundModule.context().isValid() ||
      !boundModule.definitions().module().belongsTo(boundModule.context()) ||
      boundModule.definitions().semanticContext() != boundModule.context() ||
      boundModule.definitions().revision() != boundModule.revision() ||
      boundModule.bindings().semanticContext() != boundModule.context() ||
      boundModule.bindings().revision() != boundModule.revision() ||
      boundModule.bindings().fingerprint().digest() != boundModule.fingerprint().digest()) {
    return zc::none;
  }
  auto parsed = binder::CanonicalParsedModule::fromQueryResult(
      boundModule.parsedSourceLease().capability().clone());
  if (parsed == zc::none ||
      ZC_ASSERT_NONNULL(parsed).source().encode().asPtr() !=
          boundModule.source().encode().asPtr() ||
      ZC_ASSERT_NONNULL(parsed).contentDigest() !=
          boundModule.parsedSourceLease().capability().contentDigest()) {
    return zc::none;
  }
  zc::Maybe<identity::SourceFileId> sourceFile;
  for (const auto& entry : boundModule.graphLease().capability().sources()) {
    if (entry.key().encode().asPtr() != boundModule.source().encode().asPtr()) { continue; }
    if (sourceFile != zc::none) { return zc::none; }
    sourceFile = entry.handle();
  }
  if (sourceFile == zc::none || !ZC_ASSERT_NONNULL(sourceFile).belongsTo(boundModule.context())) {
    return zc::none;
  }
  return CheckerBoundModuleView(zc::heap<Impl>(zc::mv(lease), ZC_ASSERT_NONNULL(sourceFile),
                                               zc::mv(ZC_ASSERT_NONNULL(parsed))));
}

CheckerBoundModuleView CheckerBoundModuleView::retain() const {
  return CheckerBoundModuleView(
      zc::heap<Impl>(impl->lease.retain(), impl->sourceFile, impl->parsedModule.clone()));
}

identity::SemanticContextBrand CheckerBoundModuleView::semanticContext() const noexcept {
  return impl->lease.capability().context();
}

identity::CompilationUnitId CheckerBoundModuleView::compilationUnit() const noexcept {
  return impl->lease.capability().compilationUnit();
}

identity::CrateId CheckerBoundModuleView::crate() const noexcept {
  return impl->lease.capability().crate();
}

identity::ModuleId CheckerBoundModuleView::module() const noexcept {
  return impl->lease.capability().definitions().module();
}

identity::SourceFileId CheckerBoundModuleView::sourceFile() const noexcept {
  return impl->sourceFile;
}

const identity::ContextFingerprint& CheckerBoundModuleView::semanticFingerprint()
    const noexcept {
  return impl->lease.capability().fingerprint();
}

const ast::Tree& CheckerBoundModuleView::tree() const noexcept { return impl->parsedModule.tree(); }

const binder::CanonicalParsedModule& CheckerBoundModuleView::parsedModule() const noexcept {
  return impl->parsedModule;
}

const binder::ImmutableDefinitionInventory& CheckerBoundModuleView::definitions() const noexcept {
  return impl->lease.capability().definitions();
}

zc::ArrayPtr<const binder::MaterializedDependencyExportSurface>
CheckerBoundModuleView::dependencySurfaces() const noexcept {
  return impl->lease.capability().skeletonLease().capability().dependencySurfaces();
}

zc::Maybe<const binder::MaterializedDependencyExportSurface&>
CheckerBoundModuleView::preludeSurface() const noexcept {
  return impl->lease.capability().skeletonLease().capability().preludeSurface();
}

zc::ArrayPtr<const binder::ImportBindingFact> CheckerBoundModuleView::resolvedImports()
    const noexcept {
  return impl->lease.capability().bindings().imports();
}

zc::ArrayPtr<const binder::ModuleAliasBindingFact> CheckerBoundModuleView::resolvedModuleAliases()
    const noexcept {
  return impl->lease.capability().bindings().moduleAliases();
}

const binder::ImmutableBindingMetadata& CheckerBoundModuleView::bindings() const noexcept {
  return impl->lease.capability().bindings();
}

const binder::VerifiedExportSurface& CheckerBoundModuleView::bindingSurface() const noexcept {
  return impl->lease.capability().bindingSurface();
}

const CheckerBoundModuleView::BoundModuleLease& CheckerBoundModuleView::boundModuleLease()
    const noexcept {
  return impl->lease;
}

zc::Array<uint8_t> VerifyBoundModuleQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<VerifyBoundModuleQuery::Key> VerifyBoundModuleQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return incremental_binding_query::ContextualModuleKey::decodeCanonical(bytes);
}

query::CapabilityProviderResult<VerifyBoundModuleQuery> VerifyBoundModuleQuery::provide(
    query::CapabilityQueryContext<VerifyBoundModuleQuery>& context, const Key& key) {
  auto graph = context.getCapability<MaterializeModuleGraphQuery>(key.contextRoots().clone());
  if (graph.isSourceRejected()) { return forwardBoundModuleSourceRejection(graph); }
  if (graph.isKeyRejected()) { return forwardBoundModuleKeyRejection(graph); }
  if (graph.isRuntimeRejected()) {
    return BoundModuleProviderResult::runtimeRejected(graph.runtimeFailure());
  }
  if (!graph.isPublished()) {
    return BoundModuleProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto skeleton = context.getCapability<MaterializeModuleSkeletonQuery>(key.clone());
  if (skeleton.isSourceRejected()) { return forwardBoundModuleSourceRejection(skeleton); }
  if (skeleton.isKeyRejected()) { return forwardBoundModuleKeyRejection(skeleton); }
  if (skeleton.isRuntimeRejected()) {
    return BoundModuleProviderResult::runtimeRejected(skeleton.runtimeFailure());
  }
  if (!skeleton.isPublished()) {
    return BoundModuleProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  const auto& source = skeleton.lease().capability().source();
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(source);
  if (sourceKey == zc::none) {
    return BoundModuleProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (parsed.isSourceRejected()) { return forwardBoundModuleSourceRejection(parsed); }
  if (parsed.isRuntimeRejected()) {
    return BoundModuleProviderResult::runtimeRejected(parsed.runtimeFailure());
  }
  if (!parsed.isPublished()) {
    return BoundModuleProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto selectedSource = source.clone();
  zc::Vector<VerifiedBoundModule::OwnerBodyLease> ownerBodies;
  for (const auto& owner :
       skeleton.lease().capability().identities().stableWitness().bodyOwners().values()) {
    auto ownerKey = incremental_binding_query::ContextualBodyOwnerKey::from(
        key.contextRoots().clone(), owner.clone());
    auto body = context.getCapability<MaterializeOwnerBodyQuery>(zc::mv(ownerKey));
    if (body.isSourceRejected()) { return forwardBoundModuleSourceRejection(body); }
    if (body.isKeyRejected()) { return forwardBoundModuleKeyRejection(body); }
    if (body.isRuntimeRejected()) {
      return BoundModuleProviderResult::runtimeRejected(body.runtimeFailure());
    }
    if (!body.isPublished()) {
      return BoundModuleProviderResult::runtimeRejected(
          query::QueryRuntimeFailure::InvariantViolation);
    }
    ownerBodies.add(zc::mv(body).takeLease());
  }
  auto candidate = VerifiedBoundModule::from(zc::mv(graph).takeLease(),
                                             zc::mv(skeleton).takeLease(), zc::mv(selectedSource),
                                             zc::mv(parsed).takeLease(), zc::mv(ownerBodies));
  if (candidate == zc::none) {
    return BoundModuleProviderResult::runtimeRejected(
        query::QueryRuntimeFailure::InvariantViolation);
  }
  auto owned = zc::heap<Capability>(zc::mv(ZC_ASSERT_NONNULL(candidate)));
  auto stableWitness = query::CapabilityCandidateContract<VerifyBoundModuleQuery>::encode(*owned);
  return BoundModuleProviderResult::candidate(zc::mv(owned), zc::mv(stableWitness));
}

zc::Maybe<zc::Array<uint8_t>> VerifyBoundModuleQuery::verify(
    query::CapabilityQueryContext<VerifyBoundModuleQuery>& context, const Key& key,
    const Capability& candidate) {
  auto graph = context.getCapability<MaterializeModuleGraphQuery>(key.contextRoots().clone());
  auto skeleton = context.getCapability<MaterializeModuleSkeletonQuery>(key.clone());
  if (!skeleton.isPublished()) { return zc::none; }
  const auto& source = skeleton.lease().capability().source();
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(source);
  if (sourceKey == zc::none) { return zc::none; }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (!graph.isPublished() || !parsed.isPublished() ||
      candidate.source().encode().asPtr() != source.encode().asPtr() ||
      candidate.contextRoots() != key.contextRoots() ||
      !sameSkeletonModule(candidate.module(), key.module()) ||
      !validBoundModuleLeaseLineage(graph.lease().capability(), skeleton.lease().capability(),
                                    source, parsed.lease(), candidate.ownerBodyLeases())) {
    return zc::none;
  }
  auto ownership = materializedModuleOwnership(graph.lease().capability(), key.module());
  if (ownership == zc::none ||
      candidate.compilationUnit() != ZC_ASSERT_NONNULL(ownership).compilationUnit ||
      candidate.crate() != ZC_ASSERT_NONNULL(ownership).crate) {
    return zc::none;
  }
  zc::Vector<VerifiedBoundModule::OwnerBodyLease> ownerBodies;
  for (const auto& owner :
       skeleton.lease().capability().identities().stableWitness().bodyOwners().values()) {
    auto ownerKey = incremental_binding_query::ContextualBodyOwnerKey::from(
        key.contextRoots().clone(), owner.clone());
    auto body = context.getCapability<MaterializeOwnerBodyQuery>(zc::mv(ownerKey));
    if (!body.isPublished()) { return zc::none; }
    ownerBodies.add(zc::mv(body).takeLease());
  }
  if (!sameBoundModuleLeaseLineage(candidate, graph.lease(), skeleton.lease(), parsed.lease(),
                                   ownerBodies.asPtr())) {
    return zc::none;
  }
  zc::Vector<binder::BoundOwnerBody> stableOwnerBodies;
  zc::Vector<binder::OwnerLocalBindingFact> ownerLocalBindings;
  zc::Vector<binder::AnonymousEntityFact> anonymousEntities;
  for (const auto& ownerBody : ownerBodies) {
    stableOwnerBodies.add(ownerBody.capability().stableWitness().clone());
    appendOwnerLocalBindingFacts(ownerLocalBindings,
                                 ownerBody.capability().materializedOwnerLocalBindings());
    appendAnonymousEntityFacts(anonymousEntities,
                               ownerBody.capability().materializedAnonymousEntities());
  }
  if (!candidate.definitions().matches(
          skeleton.lease().capability().identities(), stableOwnerBodies.asPtr(),
          skeleton.lease().capability().materializedDefinitions(),
          skeleton.lease().capability().materializedGenericParameters(),
          skeleton.lease().capability().materializedCallableParameters(),
          skeleton.lease().capability().materializedImplementations(), ownerLocalBindings.asPtr(),
          anonymousEntities.asPtr())) {
    return zc::none;
  }
  auto materializedFacts =
      aggregateMaterializedBindingFacts(skeleton.lease().capability(), ownerBodies.asPtr());
  if (!candidate.bindings().matches(skeleton.lease().capability().context(),
                                    skeleton.lease().capability().revision(),
                                    skeleton.lease().capability().fingerprint(),
                                    skeleton.lease().capability().identities().stableWitness(),
                                    stableOwnerBodies.asPtr(), materializedFacts.view())) {
    return zc::none;
  }
  return candidate.encodeCanonical();
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

TypedQueryResult<identity::DefId> ActiveMaterialization<identity::DefinitionKey>::materialize(
    const Resource& resources, const identity::DefinitionKey& key, const Record& record) {
  if (identity::DefinitionKey::compute(record) != key) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  const auto context = resources.semanticContext();
  if (!context.isValid()) {
    return TypedQueryResult<Handle>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  return mapIdentityInternResult(resources.internDefinition(context, key, record));
}

using MaterializeModuleSkeletonDescriptor =
    driver::module_graph_query::MaterializeModuleSkeletonQuery;

StableWitnessBytes CapabilityCandidateContract<MaterializeModuleSkeletonDescriptor>::encode(
    const MaterializeModuleSkeletonDescriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<MaterializeModuleSkeletonDescriptor::Capability>> CapabilityCandidateContract<
    MaterializeModuleSkeletonDescriptor>::decode(zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

zc::Array<uint8_t> CapabilityFailureContract<
    MaterializeModuleSkeletonDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<MaterializeModuleSkeletonDescriptor,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<
    MaterializeModuleSkeletonDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);
  if (facts == zc::none) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

CapabilityRejectionCheck CapabilityFailureContract<MaterializeModuleSkeletonDescriptor,
                                                   SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<MaterializeModuleSkeletonDescriptor>& context,
           const MaterializeModuleSkeletonDescriptor::Key& key, const Sequence& diagnostics) {
  auto graph = context.getCapability<driver::module_graph_query::MaterializeModuleGraphQuery>(
      key.contextRoots().clone());
  if (graph.isSourceRejected()) {
    auto actual = encode(graph.diagnostics());
    auto expected = encode(diagnostics);
    return actual.asPtr() == expected.asPtr() ? CapabilityRejectionCheck::Verified
                                              : CapabilityRejectionCheck::Rejected;
  }
  auto bound = context.get<binder::BindModuleSkeleton>(key.module().clone());
  if (bound.isRuntimeFailure() || bound.kind() != QueryValueKind::Value) {
    return CapabilityRejectionCheck::Rejected;
  }
  const auto& result = bound.value().storage();
  const auto matches = [&](const auto& actual) {
    return encode(actual).asPtr() == encode(diagnostics).asPtr()
               ? CapabilityRejectionCheck::Verified
               : CapabilityRejectionCheck::Rejected;
  };
  const auto matchesBinder =
      [&](const binder::CanonicalNonEmptySequence<diagnostics::DiagnosticFact>& actual) {
        auto actualBytes = binder::encodeStableBindingDiagnosticFacts(actual.values());
        auto expectedBytes = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
        return actualBytes != zc::none && expectedBytes != zc::none &&
                       ZC_ASSERT_NONNULL(actualBytes).asPtr() ==
                           ZC_ASSERT_NONNULL(expectedBytes).asPtr()
                   ? CapabilityRejectionCheck::Verified
                   : CapabilityRejectionCheck::Rejected;
      };
  if (result.is<binder::BinderSourceRejected>()) {
    return matchesBinder(result.get<binder::BinderSourceRejected>().diagnostics);
  }
  if (!result.is<binder::BinderQueryValue<binder::BoundModuleSkeleton>>()) {
    return CapabilityRejectionCheck::Rejected;
  }
  auto selected = context.get<driver::module_graph_query::SelectedModuleSourceQuery>(key.module());
  if (selected.isRuntimeFailure() || selected.kind() != QueryValueKind::Value) {
    return CapabilityRejectionCheck::Rejected;
  }
  auto moduleKey =
      driver::incremental_binding_query::StableModuleQueryKey::fromVerified(key.module());
  if (moduleKey == zc::none) { return CapabilityRejectionCheck::Rejected; }
  auto provenance =
      context.getCapability<driver::incremental_binding_query::ModuleBodyProvenanceQuery>(
          zc::mv(ZC_ASSERT_NONNULL(moduleKey)));
  return provenance.isSourceRejected() ? matches(provenance.diagnostics())
                                       : CapabilityRejectionCheck::Rejected;
}

zc::Array<uint8_t> CapabilityFailureContract<
    MaterializeModuleSkeletonDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::encode(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> CapabilityFailureContract<
    MaterializeModuleSkeletonDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

CapabilityRejectionCheck CapabilityFailureContract<MaterializeModuleSkeletonDescriptor,
                                                   KeyRejection<binder::BinderKeyFailure>>::
    verify(CapabilityQueryContext<MaterializeModuleSkeletonDescriptor>& context,
           const MaterializeModuleSkeletonDescriptor::Key& key,
           const binder::BinderKeyFailure& failure) {
  auto selected = context.get<driver::module_graph_query::SelectedModuleSourceQuery>(key.module());
  if (selected.isRuntimeFailure() &&
      selected.runtimeFailure() != QueryRuntimeFailure::MissingInput) {
    return CapabilityRejectionCheck::Rejected;
  }
  if ((selected.isRuntimeFailure() &&
       selected.runtimeFailure() == QueryRuntimeFailure::MissingInput) ||
      selected.kind() == QueryValueKind::Absence ||
      selected.kind() == QueryValueKind::SemanticFailure) {
    zc::Maybe<binder::LocalSyntaxPath> noPath;
    auto expected = binder::BinderKeyFailure::from(
        binder::BinderKeyFailureKind::MissingSelectedModuleSource,
        binder::BinderQueryOwner::module(key.module().clone()), zc::mv(noPath));
    return expected != zc::none && ZC_ASSERT_NONNULL(expected) == failure
               ? CapabilityRejectionCheck::Verified
               : CapabilityRejectionCheck::Rejected;
  }
  if (selected.kind() != QueryValueKind::Value) { return CapabilityRejectionCheck::Rejected; }

  auto graph = context.getCapability<driver::module_graph_query::MaterializeModuleGraphQuery>(
      key.contextRoots().clone());
  if (graph.isKeyRejected()) {
    return graph.keyFailure() == failure ? CapabilityRejectionCheck::Verified
                                         : CapabilityRejectionCheck::Rejected;
  }
  auto bound = context.get<binder::BindModuleSkeleton>(key.module().clone());
  if (bound.isRuntimeFailure() || bound.kind() != QueryValueKind::Value) {
    return CapabilityRejectionCheck::Rejected;
  }
  const auto& result = bound.value().storage();
  if (result.is<binder::BinderKeyRejected>()) {
    return result.get<binder::BinderKeyRejected>().failure == failure
               ? CapabilityRejectionCheck::Verified
               : CapabilityRejectionCheck::Rejected;
  }
  if (!result.is<binder::BinderQueryValue<binder::BoundModuleSkeleton>>()) {
    return CapabilityRejectionCheck::Rejected;
  }
  auto moduleKey =
      driver::incremental_binding_query::StableModuleQueryKey::fromVerified(key.module());
  if (moduleKey == zc::none) { return CapabilityRejectionCheck::Rejected; }
  auto provenance =
      context.getCapability<driver::incremental_binding_query::ModuleBodyProvenanceQuery>(
          zc::mv(ZC_ASSERT_NONNULL(moduleKey)));
  return provenance.isKeyRejected() && provenance.keyFailure() == failure
             ? CapabilityRejectionCheck::Verified
             : CapabilityRejectionCheck::Rejected;
}

using MaterializeOwnerBodyDescriptor = driver::module_graph_query::MaterializeOwnerBodyQuery;

StableWitnessBytes CapabilityCandidateContract<MaterializeOwnerBodyDescriptor>::encode(
    const MaterializeOwnerBodyDescriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<MaterializeOwnerBodyDescriptor::Capability>>
CapabilityCandidateContract<MaterializeOwnerBodyDescriptor>::decode(zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

zc::Array<uint8_t> CapabilityFailureContract<
    MaterializeOwnerBodyDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<MaterializeOwnerBodyDescriptor,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<
    MaterializeOwnerBodyDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);
  if (facts == zc::none) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

CapabilityRejectionCheck CapabilityFailureContract<MaterializeOwnerBodyDescriptor,
                                                   SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<MaterializeOwnerBodyDescriptor>& context,
           const MaterializeOwnerBodyDescriptor::Key& key, const Sequence& diagnostics) {
  auto skeletonKey = driver::incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), key.body().module().clone());
  auto skeleton = context.getCapability<driver::module_graph_query::MaterializeModuleSkeletonQuery>(
      zc::mv(skeletonKey));
  if (skeleton.isSourceRejected()) {
    auto actual = encode(skeleton.diagnostics());
    auto expected = encode(diagnostics);
    return actual.asPtr() == expected.asPtr() ? CapabilityRejectionCheck::Verified
                                              : CapabilityRejectionCheck::Rejected;
  }
  auto bound = context.get<binder::BindOwnerBody>(key.clone());
  if (bound.isRuntimeFailure() || bound.kind() != QueryValueKind::Value) {
    return CapabilityRejectionCheck::Rejected;
  }
  const auto& result = bound.value().storage();
  if (!result.is<binder::BinderSourceRejected>()) { return CapabilityRejectionCheck::Rejected; }
  auto actual = binder::encodeStableBindingDiagnosticFacts(
      result.get<binder::BinderSourceRejected>().diagnostics.values());
  auto expected = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return actual != zc::none && expected != zc::none &&
                 ZC_ASSERT_NONNULL(actual).asPtr() == ZC_ASSERT_NONNULL(expected).asPtr()
             ? CapabilityRejectionCheck::Verified
             : CapabilityRejectionCheck::Rejected;
}

zc::Array<uint8_t> CapabilityFailureContract<
    MaterializeOwnerBodyDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::encode(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> CapabilityFailureContract<
    MaterializeOwnerBodyDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

CapabilityRejectionCheck
CapabilityFailureContract<MaterializeOwnerBodyDescriptor, KeyRejection<binder::BinderKeyFailure>>::
    verify(CapabilityQueryContext<MaterializeOwnerBodyDescriptor>& context,
           const MaterializeOwnerBodyDescriptor::Key& key,
           const binder::BinderKeyFailure& failure) {
  auto skeletonKey = driver::incremental_binding_query::ContextualModuleKey::from(
      key.contextRoots().clone(), key.body().module().clone());
  auto skeleton = context.getCapability<driver::module_graph_query::MaterializeModuleSkeletonQuery>(
      zc::mv(skeletonKey));
  if (skeleton.isKeyRejected()) {
    return skeleton.keyFailure() == failure ? CapabilityRejectionCheck::Verified
                                            : CapabilityRejectionCheck::Rejected;
  }
  auto bound = context.get<binder::BindOwnerBody>(key.clone());
  if (bound.isRuntimeFailure() || bound.kind() != QueryValueKind::Value) {
    return CapabilityRejectionCheck::Rejected;
  }
  const auto& result = bound.value().storage();
  return result.is<binder::BinderKeyRejected>() &&
                 result.get<binder::BinderKeyRejected>().failure == failure
             ? CapabilityRejectionCheck::Verified
             : CapabilityRejectionCheck::Rejected;
}

using VerifyBoundModuleDescriptor = driver::module_graph_query::VerifyBoundModuleQuery;

StableWitnessBytes CapabilityCandidateContract<VerifyBoundModuleDescriptor>::encode(
    const VerifyBoundModuleDescriptor::Capability& candidate) {
  return StableWitnessBytes(candidate.encodeCanonical());
}

zc::Maybe<zc::Own<VerifyBoundModuleDescriptor::Capability>>
CapabilityCandidateContract<VerifyBoundModuleDescriptor>::decode(zc::ArrayPtr<const uint8_t>) {
  return zc::none;
}

zc::Array<uint8_t> CapabilityFailureContract<
    VerifyBoundModuleDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::encode(const Sequence& diagnostics) {
  auto encoded = binder::encodeStableBindingDiagnosticFacts(diagnostics.values());
  return zc::mv(ZC_ASSERT_NONNULL(encoded));
}

zc::Maybe<CapabilityFailureContract<VerifyBoundModuleDescriptor,
                                    SourceRejection<diagnostics::DiagnosticFact>>::Sequence>
CapabilityFailureContract<
    VerifyBoundModuleDescriptor,
    SourceRejection<diagnostics::DiagnosticFact>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  auto facts = binder::decodeStableBindingDiagnosticFacts(bytes);
  if (facts == zc::none) { return zc::none; }
  return Sequence(zc::mv(ZC_ASSERT_NONNULL(facts)));
}

CapabilityRejectionCheck CapabilityFailureContract<VerifyBoundModuleDescriptor,
                                                   SourceRejection<diagnostics::DiagnosticFact>>::
    verify(CapabilityQueryContext<VerifyBoundModuleDescriptor>& context,
           const VerifyBoundModuleDescriptor::Key& key, const Sequence& diagnostics) {
  const auto matches = [&](const auto& actual) {
    return encode(actual).asPtr() == encode(diagnostics).asPtr()
               ? CapabilityRejectionCheck::Verified
               : CapabilityRejectionCheck::Rejected;
  };
  auto graph = context.getCapability<driver::module_graph_query::MaterializeModuleGraphQuery>(
      key.contextRoots().clone());
  if (graph.isSourceRejected()) { return matches(graph.diagnostics()); }
  auto skeleton = context.getCapability<driver::module_graph_query::MaterializeModuleSkeletonQuery>(
      key.clone());
  if (skeleton.isSourceRejected()) { return matches(skeleton.diagnostics()); }
  if (!skeleton.isPublished()) { return CapabilityRejectionCheck::Rejected; }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(
      skeleton.lease().capability().source());
  if (sourceKey == zc::none) { return CapabilityRejectionCheck::Rejected; }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (parsed.isSourceRejected()) { return matches(parsed.diagnostics()); }
  if (!parsed.isPublished()) { return CapabilityRejectionCheck::Rejected; }
  for (const auto& owner :
       skeleton.lease().capability().identities().stableWitness().bodyOwners().values()) {
    auto ownerKey = driver::incremental_binding_query::ContextualBodyOwnerKey::from(
        key.contextRoots().clone(), owner.clone());
    auto body = context.getCapability<driver::module_graph_query::MaterializeOwnerBodyQuery>(
        zc::mv(ownerKey));
    if (body.isSourceRejected()) { return matches(body.diagnostics()); }
  }
  return CapabilityRejectionCheck::Rejected;
}

zc::Array<uint8_t> CapabilityFailureContract<
    VerifyBoundModuleDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::encode(const binder::BinderKeyFailure& failure) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::encode(failure);
}

zc::Maybe<binder::BinderKeyFailure> CapabilityFailureContract<
    VerifyBoundModuleDescriptor,
    KeyRejection<binder::BinderKeyFailure>>::decode(zc::ArrayPtr<const uint8_t> bytes) {
  return binder::StableBindingCodec<binder::BinderKeyFailure>::decode(bytes);
}

CapabilityRejectionCheck
CapabilityFailureContract<VerifyBoundModuleDescriptor, KeyRejection<binder::BinderKeyFailure>>::
    verify(CapabilityQueryContext<VerifyBoundModuleDescriptor>& context,
           const VerifyBoundModuleDescriptor::Key& key, const binder::BinderKeyFailure& failure) {
  auto graph = context.getCapability<driver::module_graph_query::MaterializeModuleGraphQuery>(
      key.contextRoots().clone());
  if (graph.isKeyRejected()) {
    return graph.keyFailure() == failure ? CapabilityRejectionCheck::Verified
                                         : CapabilityRejectionCheck::Rejected;
  }
  auto skeleton = context.getCapability<driver::module_graph_query::MaterializeModuleSkeletonQuery>(
      key.clone());
  if (skeleton.isKeyRejected()) {
    return skeleton.keyFailure() == failure ? CapabilityRejectionCheck::Verified
                                            : CapabilityRejectionCheck::Rejected;
  }
  if (!skeleton.isPublished()) { return CapabilityRejectionCheck::Rejected; }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(
      skeleton.lease().capability().source());
  if (sourceKey == zc::none) { return CapabilityRejectionCheck::Rejected; }
  auto parsed =
      context.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_ASSERT_NONNULL(sourceKey)));
  if (!parsed.isPublished()) { return CapabilityRejectionCheck::Rejected; }
  for (const auto& owner :
       skeleton.lease().capability().identities().stableWitness().bodyOwners().values()) {
    auto ownerKey = driver::incremental_binding_query::ContextualBodyOwnerKey::from(
        key.contextRoots().clone(), owner.clone());
    auto body = context.getCapability<driver::module_graph_query::MaterializeOwnerBodyQuery>(
        zc::mv(ownerKey));
    if (body.isKeyRejected()) {
      return body.keyFailure() == failure ? CapabilityRejectionCheck::Verified
                                          : CapabilityRejectionCheck::Rejected;
    }
  }
  return CapabilityRejectionCheck::Rejected;
}

}  // namespace zomlang::compiler::query

namespace {

#define ZOM_M1_KEY_CompilationRootSetQueryKey \
  zomlang::compiler::driver::incremental_binding_query::CompilationRootSetQueryKey
#define ZOM_M1_KEY_ContextualModuleKey \
  zomlang::compiler::driver::incremental_binding_query::ContextualModuleKey
#define ZOM_M1_KEY_ContextualBodyOwnerKey \
  zomlang::compiler::driver::incremental_binding_query::ContextualBodyOwnerKey
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
#define ZOM_M1_CAPABILITY_SELECT_M2(name, domain, keyType, capabilityType) \
  ZOM_M1_CAPABILITY_SELECT_M1(name, domain, keyType, capabilityType)
#define ZOM_M1_CAPABILITY_SELECT_M3(name, domain, keyType, capabilityType) \
  ZOM_M1_CAPABILITY_SELECT_M1(name, domain, keyType, capabilityType)
#define ZOM_M1_CAPABILITY_SELECT_M5(name, domain, keyType, capabilityType) \
  ZOM_M1_CAPABILITY_SELECT_M1(name, domain, keyType, capabilityType)
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
#undef ZOM_M1_KEY_ContextualBodyOwnerKey
#undef ZOM_M1_KEY_ContextualModuleKey
#undef ZOM_M1_KEY_CompilationRootSetQueryKey

#define ZOM_M1_PERMISSION_MaterializeModuleGraph(globalKey, membership)                     \
  static_assert(zomlang::compiler::query::ActiveMaterializerPermission<                     \
                zomlang::compiler::driver::module_graph_query::MaterializeModuleGraphQuery, \
                zomlang::compiler::identity::globalKey,                                     \
                zomlang::compiler::driver::incremental_binding_query::membership##Query>::allowed)
#define ZOM_M1_PERMISSION_MaterializeModuleSkeleton(globalKey, membership)                     \
  static_assert(zomlang::compiler::query::ActiveMaterializerPermission<                        \
                zomlang::compiler::driver::module_graph_query::MaterializeModuleSkeletonQuery, \
                zomlang::compiler::identity::globalKey,                                        \
                zomlang::compiler::driver::incremental_binding_query::membership##Query>::allowed)
#define ZOM_M1_PERMISSION_MaterializeOwnerBody(globalKey, membership)                     \
  static_assert(zomlang::compiler::query::ActiveMaterializerPermission<                   \
                zomlang::compiler::driver::module_graph_query::MaterializeOwnerBodyQuery, \
                zomlang::compiler::identity::globalKey,                                   \
                zomlang::compiler::driver::incremental_binding_query::membership##Query>::allowed)
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
