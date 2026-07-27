// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/module-graph-query.h"

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::module_graph_query {
namespace {

constexpr zc::StringPtr kEdgeDomain = "zom.module-dependency-edge"_zc;
constexpr zc::StringPtr kGraphValueDomain = "zom.query.module-graph-value"_zc;
constexpr zc::StringPtr kGraphFailureDomain = "zom.query.module-graph-failure"_zc;
constexpr zc::StringPtr kSccValueDomain = "zom.query.module-graph-scc-value"_zc;
constexpr uint64_t kMaximumCrateKeyBytes = 2 * 1024 * 1024;
constexpr uint64_t kMaximumModuleKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumModules = 4096;
constexpr uint64_t kMaximumEdges = 1024 * 1024;
constexpr uint64_t kMaximumValueBytes = 128 * 1024 * 1024;

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

bool sameCrate(const identity::CrateKey& left, const identity::CrateKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

bool sameModule(const identity::ModuleKey& left, const identity::ModuleKey& right) {
  return left.encode().asPtr() == right.encode().asPtr();
}

template <typename T, typename Bytes>
void sortByBytes(zc::Vector<T>& values, Bytes bytes) {
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
  if (bytes.size() <= prefixSize || bytes.size() > kMaximumValueBytes ||
      bytes.slice(0, domain.size()) != domain.asBytes() || bytes[domain.size()] != 0) {
    return zc::none;
  }
  return bytes.slice(prefixSize, bytes.size());
}

zc::Maybe<identity::CrateKey> decodeCrate(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumCrateKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::CrateKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(crate, value) {
    if (crate.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(crate);
  }
  return zc::none;
}

zc::Maybe<identity::ModuleKey> decodeModule(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto value = identity::ModuleKey::decodeCanonical(decoder);
  if (value == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(module, value) {
    if (module.encode().asPtr() != bytes) { return zc::none; }
    return zc::mv(module);
  }
  return zc::none;
}

query::QueryKindContract derivedContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, query::ReuseClass::Semantic,
                                                    query::RetentionClass::Retained);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

zc::Maybe<size_t> moduleIndex(const ModuleGraphRecord& graph, const identity::ModuleKey& module) {
  for (size_t index = 0; index < graph.modules().size(); ++index) {
    if (sameModule(graph.modules()[index], module)) { return index; }
  }
  return zc::none;
}

struct OwnedModule final {
  identity::CrateKey owner;
  identity::ModuleKey module;
};

bool containsModule(zc::ArrayPtr<const identity::ModuleKey> modules,
                    const identity::ModuleKey& target) {
  for (const auto& module : modules) {
    if (sameModule(module, target)) { return true; }
  }
  return false;
}

bool componentIsCyclic(const ModuleGraphRecord& graph,
                       zc::ArrayPtr<const identity::ModuleKey> modules) {
  if (modules.size() > 1) { return true; }
  if (modules.size() != 1) { return false; }
  for (const auto& edge : graph.edges()) {
    if (sameModule(edge.requester(), modules[0]) && sameModule(edge.dependency(), modules[0])) {
      return true;
    }
  }
  return false;
}

query::TypedQueryResult<ModuleGraphRecord> evaluateGraph(
    query::QueryContext& context,
    const incremental_binding_query::CompilationRootSetQueryKey& key) {
  auto activeCrates = context.get<incremental_binding_query::ActiveCratesQuery>(key);
  if (activeCrates.isRuntimeFailure()) {
    return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
        activeCrates.runtimeFailure());
  }
  if (activeCrates.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  zc::Vector<identity::CrateKey> crates(activeCrates.value().crates().size());
  for (const auto& stable : activeCrates.value().crates()) {
    auto crate = decodeCrate(stable.canonicalCrateBytes());
    if (crate == zc::none) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    crates.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }
  zc::Vector<query::TypedQueryResult<ActiveModuleSetRecord>> memberships(crates.size());
  for (const auto& crate : crates) { memberships.add(context.get<ActiveModulesQuery>(crate)); }
  zc::Vector<OwnedModule> owned;
  for (size_t crateIndex = 0; crateIndex < crates.size(); ++crateIndex) {
    if (memberships[crateIndex].isRuntimeFailure()) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          memberships[crateIndex].runtimeFailure());
    }
    if (memberships[crateIndex].kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    for (const auto& module : memberships[crateIndex].value().modules()) {
      owned.add(OwnedModule{crates[crateIndex].clone(), module.clone()});
    }
  }
  sortByBytes(owned, [](const OwnedModule& value) { return value.module.encode(); });
  for (size_t index = 1; index < owned.size(); ++index) {
    if (sameModule(owned[index - 1].module, owned[index].module)) {
      auto failure = ModuleGraphFailureRecord::duplicate(owned[index].module.clone());
      return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(failure.encodeCanonical());
    }
  }
  for (const auto& value : owned) {
    if (!sameCrate(value.owner, value.module.crate())) {
      auto failure = ModuleGraphFailureRecord::foreign(value.owner.clone(), value.module.clone());
      if (failure == zc::none) {
        return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(
          ZC_ASSERT_NONNULL(failure).encodeCanonical());
    }
  }
  zc::Vector<identity::ModuleKey> modules(owned.size());
  for (const auto& value : owned) { modules.add(value.module.clone()); }
  zc::Vector<query::TypedQueryResult<ModuleDependencySetRecord>> dependencySets(modules.size());
  for (const auto& module : modules) {
    dependencySets.add(context.get<ModuleDependenciesQuery>(module));
  }
  for (size_t index = 0; index < modules.size(); ++index) {
    const auto& result = dependencySets[index];
    if (result.isRuntimeFailure()) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(result.runtimeFailure());
    }
    if (result.kind() == query::QueryValueKind::SemanticFailure) {
      return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(
          zc::heapArray<uint8_t>(result.semanticFailureBytes()));
    }
    if (result.kind() == query::QueryValueKind::Absence) {
      return query::TypedQueryResult<ModuleGraphRecord>::absence();
    }
    if (result.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
  }
  zc::Vector<ModuleDependencyEdgeKey> edges;
  for (size_t index = 0; index < modules.size(); ++index) {
    const auto& result = dependencySets[index];
    for (const auto& dependency : result.value().dependencies()) {
      if (!containsModule(modules.asPtr(), dependency)) {
        auto failure =
            ModuleGraphFailureRecord::outside(modules[index].clone(), dependency.clone());
        if (failure == zc::none) {
          return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
              query::QueryRuntimeFailure::ProviderRejected);
        }
        return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(
            ZC_ASSERT_NONNULL(failure).encodeCanonical());
      }
      auto edge = ModuleDependencyEdgeKey::from(modules[index].clone(), dependency.clone());
      if (edge == zc::none) {
        return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      edges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
    }
  }
  auto graph = ModuleGraphRecord::from(zc::mv(modules), zc::mv(edges));
  if (graph == zc::none) {
    return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<ModuleGraphRecord>::value(zc::mv(ZC_ASSERT_NONNULL(graph)));
}

query::TypedQueryResult<ModuleGraphRecord> evaluateVerifierGraph(
    query::QueryContext& context,
    const incremental_binding_query::CompilationRootSetQueryKey& key) {
  auto activeCrates = context.get<incremental_binding_query::ActiveCratesQuery>(key);
  if (activeCrates.isRuntimeFailure()) {
    return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
        activeCrates.runtimeFailure());
  }
  if (activeCrates.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }

  zc::Vector<identity::CrateKey> crates(activeCrates.value().crates().size());
  for (const auto& stable : activeCrates.value().crates()) {
    identity::CanonicalDecoder decoder(stable.canonicalCrateBytes());
    auto crate = identity::CrateKey::decodeCanonical(decoder);
    if (crate == zc::none || !decoder.finished() ||
        ZC_ASSERT_NONNULL(crate).encode().asPtr() != stable.canonicalCrateBytes()) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    crates.add(zc::mv(ZC_ASSERT_NONNULL(crate)));
  }

  zc::Vector<query::TypedQueryResult<ActiveModuleSetRecord>> memberships(crates.size());
  for (const auto& crate : crates) { memberships.add(context.get<ActiveModulesQuery>(crate)); }

  zc::TreeMap<zc::String, OwnedModule> ordered;
  zc::TreeMap<zc::String, identity::ModuleKey> duplicates;
  for (size_t cursor = crates.size(); cursor != 0; --cursor) {
    const size_t crateIndex = cursor - 1;
    const auto& membership = memberships[crateIndex];
    if (membership.isRuntimeFailure()) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          membership.runtimeFailure());
    }
    if (membership.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    for (const auto& module : membership.value().modules()) {
      auto keyBytes = zc::encodeHex(module.encode().asPtr());
      if (ordered.find(keyBytes) != zc::none) {
        if (duplicates.find(keyBytes) == zc::none) {
          duplicates.insert(zc::mv(keyBytes), module.clone());
        }
        continue;
      }
      ordered.insert(zc::mv(keyBytes), OwnedModule{crates[crateIndex].clone(), module.clone()});
    }
  }
  for (const auto& duplicate : duplicates) {
    auto failure = ModuleGraphFailureRecord::duplicate(duplicate.value.clone());
    return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(failure.encodeCanonical());
  }

  zc::Vector<identity::ModuleKey> modules(ordered.size());
  for (const auto& entry : ordered) {
    if (entry.value.owner.encode().asPtr() != entry.value.module.crate().encode().asPtr()) {
      auto failure =
          ModuleGraphFailureRecord::foreign(entry.value.owner.clone(), entry.value.module.clone());
      if (failure == zc::none) {
        return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(
          ZC_ASSERT_NONNULL(failure).encodeCanonical());
    }
    modules.add(entry.value.module.clone());
  }

  zc::Vector<query::TypedQueryResult<ModuleDependencySetRecord>> dependencySets(modules.size());
  for (const auto& module : modules) {
    dependencySets.add(context.get<ModuleDependenciesQuery>(module));
  }
  for (size_t moduleIndexValue = 0; moduleIndexValue < modules.size(); ++moduleIndexValue) {
    const auto& dependencies = dependencySets[moduleIndexValue];
    if (dependencies.isRuntimeFailure()) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          dependencies.runtimeFailure());
    }
    if (dependencies.kind() == query::QueryValueKind::SemanticFailure) {
      return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(
          zc::heapArray<uint8_t>(dependencies.semanticFailureBytes()));
    }
    if (dependencies.kind() == query::QueryValueKind::Absence) {
      return query::TypedQueryResult<ModuleGraphRecord>::absence();
    }
    if (dependencies.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
  }
  zc::Vector<ModuleDependencyEdgeKey> edges;
  for (size_t moduleIndexValue = 0; moduleIndexValue < modules.size(); ++moduleIndexValue) {
    const auto& dependencies = dependencySets[moduleIndexValue];
    for (const auto& dependency : dependencies.value().dependencies()) {
      bool present = false;
      for (const auto& module : modules) {
        if (module.encode().asPtr() == dependency.encode().asPtr()) {
          present = true;
          break;
        }
      }
      if (!present) {
        auto failure = ModuleGraphFailureRecord::outside(modules[moduleIndexValue].clone(),
                                                         dependency.clone());
        if (failure == zc::none) {
          return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
              query::QueryRuntimeFailure::ProviderRejected);
        }
        return query::TypedQueryResult<ModuleGraphRecord>::semanticFailure(
            ZC_ASSERT_NONNULL(failure).encodeCanonical());
      }
      auto edge =
          ModuleDependencyEdgeKey::from(modules[moduleIndexValue].clone(), dependency.clone());
      if (edge == zc::none) {
        return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      edges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
    }
  }
  auto graph = ModuleGraphRecord::from(zc::mv(modules), zc::mv(edges));
  if (graph == zc::none) {
    return query::TypedQueryResult<ModuleGraphRecord>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<ModuleGraphRecord>::value(zc::mv(ZC_ASSERT_NONNULL(graph)));
}

zc::Maybe<size_t> componentIndex(zc::ArrayPtr<const ModuleGraphSccComponent> components,
                                 const identity::ModuleKey& module) {
  for (size_t index = 0; index < components.size(); ++index) {
    if (containsModule(components[index].modules(), module)) { return index; }
  }
  return zc::none;
}

zc::Maybe<zc::Vector<ModuleGraphSccComponent>> orderComponents(
    const ModuleGraphRecord& graph, zc::Vector<ModuleGraphSccComponent>&& unordered) {
  zc::Vector<uint8_t> placed(unordered.size());
  placed.resize(unordered.size());
  for (auto& value : placed) { value = 0; }
  zc::Vector<ModuleGraphSccComponent> ordered(unordered.size());
  for (size_t count = 0; count < unordered.size(); ++count) {
    size_t selected = unordered.size();
    zc::Array<uint8_t> selectedBytes;
    for (size_t candidate = 0; candidate < unordered.size(); ++candidate) {
      if (placed[candidate] != 0) { continue; }
      bool ready = true;
      for (const auto& edge : graph.edges()) {
        auto requester = componentIndex(unordered.asPtr(), edge.requester());
        auto dependency = componentIndex(unordered.asPtr(), edge.dependency());
        if (requester == zc::none || dependency == zc::none) { return zc::none; }
        if (ZC_ASSERT_NONNULL(requester) == candidate &&
            ZC_ASSERT_NONNULL(dependency) != candidate &&
            placed[ZC_ASSERT_NONNULL(dependency)] == 0) {
          ready = false;
          break;
        }
      }
      if (!ready) { continue; }
      auto bytes = unordered[candidate].encodeCanonical();
      if (selected == unordered.size() || compareBytes(bytes.asPtr(), selectedBytes.asPtr()) < 0) {
        selected = candidate;
        selectedBytes = zc::mv(bytes);
      }
    }
    if (selected == unordered.size()) { return zc::none; }
    placed[selected] = 1;
    ordered.add(unordered[selected].clone());
  }
  return zc::mv(ordered);
}

class Tarjan final {
public:
  explicit Tarjan(const ModuleGraphRecord& graph) : graph(graph) {
    indices.resize(graph.modules().size());
    lowLinks.resize(graph.modules().size());
    onStack.resize(graph.modules().size());
    for (size_t index = 0; index < graph.modules().size(); ++index) {
      indices[index] = -1;
      lowLinks[index] = -1;
      onStack[index] = 0;
    }
  }

  zc::Maybe<zc::Vector<ModuleGraphSccComponent>> run() {
    for (size_t node = 0; node < graph.modules().size(); ++node) {
      if (indices[node] == -1 && !strongConnect(node)) { return zc::none; }
    }
    return orderComponents(graph, zc::mv(components));
  }

private:
  bool strongConnect(size_t node) {
    indices[node] = nextIndex;
    lowLinks[node] = nextIndex;
    ++nextIndex;
    stack.add(node);
    onStack[node] = 1;
    for (const auto& edge : graph.edges()) {
      if (!sameModule(edge.requester(), graph.modules()[node])) { continue; }
      auto target = moduleIndex(graph, edge.dependency());
      if (target == zc::none) { return false; }
      const size_t targetIndex = ZC_ASSERT_NONNULL(target);
      if (indices[targetIndex] == -1) {
        if (!strongConnect(targetIndex)) { return false; }
        if (lowLinks[targetIndex] < lowLinks[node]) { lowLinks[node] = lowLinks[targetIndex]; }
      } else if (onStack[targetIndex] != 0 && indices[targetIndex] < lowLinks[node]) {
        lowLinks[node] = indices[targetIndex];
      }
    }
    if (lowLinks[node] != indices[node]) { return true; }
    zc::Vector<identity::ModuleKey> values;
    while (stack.size() != 0) {
      const size_t value = stack.back();
      stack.removeLast();
      onStack[value] = 0;
      values.add(graph.modules()[value].clone());
      if (value == node) { break; }
    }
    const bool cyclic = componentIsCyclic(graph, values.asPtr());
    auto component = ModuleGraphSccComponent::from(zc::mv(values), cyclic);
    if (component == zc::none) { return false; }
    components.add(zc::mv(ZC_ASSERT_NONNULL(component)));
    return true;
  }

  const ModuleGraphRecord& graph;
  zc::Vector<int64_t> indices;
  zc::Vector<int64_t> lowLinks;
  zc::Vector<uint8_t> onStack;
  zc::Vector<size_t> stack;
  zc::Vector<ModuleGraphSccComponent> components;
  int64_t nextIndex = 0;
};

zc::Maybe<size_t> verifierModuleIndex(const ModuleGraphRecord& graph,
                                      const identity::ModuleKey& module) {
  const auto targetBytes = module.encode();
  for (size_t index = graph.modules().size(); index != 0; --index) {
    if (graph.modules()[index - 1].encode().asPtr() == targetBytes.asPtr()) { return index - 1; }
  }
  return zc::none;
}

zc::Maybe<size_t> verifierComponentIndex(zc::ArrayPtr<const ModuleGraphSccComponent> components,
                                         const identity::ModuleKey& module) {
  const auto targetBytes = module.encode();
  for (size_t component = components.size(); component != 0; --component) {
    for (const auto& member : components[component - 1].modules()) {
      if (member.encode().asPtr() == targetBytes.asPtr()) { return component - 1; }
    }
  }
  return zc::none;
}

bool verifierComponentIsCyclic(const ModuleGraphRecord& graph,
                               zc::ArrayPtr<const identity::ModuleKey> modules) {
  if (modules.size() > 1) { return true; }
  if (modules.size() == 0) { return false; }
  const auto memberBytes = modules[0].encode();
  for (size_t index = graph.edges().size(); index != 0; --index) {
    const auto& edge = graph.edges()[index - 1];
    if (edge.requester().encode().asPtr() == memberBytes.asPtr() &&
        edge.dependency().encode().asPtr() == memberBytes.asPtr()) {
      return true;
    }
  }
  return false;
}

zc::Maybe<zc::Vector<ModuleGraphSccComponent>> verifierOrderComponents(
    const ModuleGraphRecord& graph, zc::Vector<ModuleGraphSccComponent>&& unordered) {
  zc::Vector<uint8_t> emitted(unordered.size());
  emitted.resize(unordered.size());
  for (auto& value : emitted) { value = 0; }
  zc::Vector<ModuleGraphSccComponent> result(unordered.size());
  for (size_t resultIndex = 0; resultIndex < unordered.size(); ++resultIndex) {
    zc::Maybe<size_t> chosen;
    zc::Array<uint8_t> chosenBytes;
    for (size_t candidate = unordered.size(); candidate != 0; --candidate) {
      const size_t candidateIndex = candidate - 1;
      if (emitted[candidateIndex] != 0) { continue; }
      bool dependenciesEmitted = true;
      for (size_t edgeIndex = graph.edges().size(); edgeIndex != 0; --edgeIndex) {
        const auto& edge = graph.edges()[edgeIndex - 1];
        auto requester = verifierComponentIndex(unordered.asPtr(), edge.requester());
        auto dependency = verifierComponentIndex(unordered.asPtr(), edge.dependency());
        if (requester == zc::none || dependency == zc::none) { return zc::none; }
        if (ZC_ASSERT_NONNULL(requester) == candidateIndex &&
            ZC_ASSERT_NONNULL(dependency) != candidateIndex &&
            emitted[ZC_ASSERT_NONNULL(dependency)] == 0) {
          dependenciesEmitted = false;
          break;
        }
      }
      if (!dependenciesEmitted) { continue; }
      auto encoded = unordered[candidateIndex].encodeCanonical();
      if (chosen == zc::none || compareBytes(encoded.asPtr(), chosenBytes.asPtr()) < 0) {
        chosen = candidateIndex;
        chosenBytes = zc::mv(encoded);
      }
    }
    if (chosen == zc::none) { return zc::none; }
    emitted[ZC_ASSERT_NONNULL(chosen)] = 1;
    result.add(unordered[ZC_ASSERT_NONNULL(chosen)].clone());
  }
  return zc::mv(result);
}

class Kosaraju final {
public:
  explicit Kosaraju(const ModuleGraphRecord& graph) : graph(graph) {
    visited.resize(graph.modules().size());
    for (auto& value : visited) { value = 0; }
  }

  zc::Maybe<zc::Vector<ModuleGraphSccComponent>> run() {
    for (size_t node = graph.modules().size(); node != 0; --node) {
      if (visited[node - 1] == 0 && !finish(node - 1)) { return zc::none; }
    }
    for (auto& value : visited) { value = 0; }
    zc::Vector<ModuleGraphSccComponent> components;
    for (size_t cursor = finishOrder.size(); cursor != 0; --cursor) {
      const size_t node = finishOrder[cursor - 1];
      if (visited[node] != 0) { continue; }
      zc::Vector<identity::ModuleKey> values;
      if (!collectTranspose(node, values)) { return zc::none; }
      const bool cyclic = verifierComponentIsCyclic(graph, values.asPtr());
      auto component = ModuleGraphSccComponent::from(zc::mv(values), cyclic);
      if (component == zc::none) { return zc::none; }
      components.add(zc::mv(ZC_ASSERT_NONNULL(component)));
    }
    return verifierOrderComponents(graph, zc::mv(components));
  }

private:
  bool finish(size_t node) {
    visited[node] = 1;
    for (const auto& edge : graph.edges()) {
      if (!sameModule(edge.requester(), graph.modules()[node])) { continue; }
      auto target = verifierModuleIndex(graph, edge.dependency());
      if (target == zc::none) { return false; }
      if (visited[ZC_ASSERT_NONNULL(target)] == 0 && !finish(ZC_ASSERT_NONNULL(target))) {
        return false;
      }
    }
    finishOrder.add(node);
    return true;
  }

  bool collectTranspose(size_t node, zc::Vector<identity::ModuleKey>& values) {
    visited[node] = 1;
    values.add(graph.modules()[node].clone());
    for (const auto& edge : graph.edges()) {
      if (!sameModule(edge.dependency(), graph.modules()[node])) { continue; }
      auto target = verifierModuleIndex(graph, edge.requester());
      if (target == zc::none) { return false; }
      if (visited[ZC_ASSERT_NONNULL(target)] == 0 &&
          !collectTranspose(ZC_ASSERT_NONNULL(target), values)) {
        return false;
      }
    }
    return true;
  }

  const ModuleGraphRecord& graph;
  zc::Vector<uint8_t> visited;
  zc::Vector<size_t> finishOrder;
};

}  // namespace

ModuleDependencyEdgeKey::ModuleDependencyEdgeKey(identity::ModuleKey&& requester,
                                                 identity::ModuleKey&& dependency) noexcept
    : requesterValue(zc::mv(requester)), dependencyValue(zc::mv(dependency)) {}

zc::Maybe<ModuleDependencyEdgeKey> ModuleDependencyEdgeKey::from(identity::ModuleKey&& requester,
                                                                 identity::ModuleKey&& dependency) {
  ModuleDependencyEdgeKey result(zc::mv(requester), zc::mv(dependency));
  if (result.encodeCanonical().size() > 2 * kMaximumModuleKeyBytes + 32) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ModuleDependencyEdgeKey> ModuleDependencyEdgeKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kEdgeDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto requesterBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  auto dependencyBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  if (requesterBytes == zc::none || dependencyBytes == zc::none || !decoder.finished()) {
    return zc::none;
  }
  auto requester = decodeModule(ZC_ASSERT_NONNULL(requesterBytes).asPtr());
  auto dependency = decodeModule(ZC_ASSERT_NONNULL(dependencyBytes).asPtr());
  if (requester == zc::none || dependency == zc::none) { return zc::none; }
  auto result = from(zc::mv(ZC_ASSERT_NONNULL(requester)), zc::mv(ZC_ASSERT_NONNULL(dependency)));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ModuleDependencyEdgeKey ModuleDependencyEdgeKey::clone() const {
  return ModuleDependencyEdgeKey(requesterValue.clone(), dependencyValue.clone());
}

const identity::ModuleKey& ModuleDependencyEdgeKey::requester() const noexcept {
  return requesterValue;
}

const identity::ModuleKey& ModuleDependencyEdgeKey::dependency() const noexcept {
  return dependencyValue;
}

zc::Array<uint8_t> ModuleDependencyEdgeKey::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  const auto requesterBytes = requesterValue.encode();
  const auto dependencyBytes = dependencyValue.encode();
  payload.encodeByteString(requesterBytes.asPtr());
  payload.encodeByteString(dependencyBytes.asPtr());
  return frame(kEdgeDomain, payload.finish().asPtr());
}

ModuleGraphRecord::ModuleGraphRecord(zc::Vector<identity::ModuleKey>&& modules,
                                     zc::Vector<ModuleDependencyEdgeKey>&& edges) noexcept
    : moduleValues(zc::mv(modules)), edgeValues(zc::mv(edges)) {}

zc::Maybe<ModuleGraphRecord> ModuleGraphRecord::from(zc::Vector<identity::ModuleKey>&& modules,
                                                     zc::Vector<ModuleDependencyEdgeKey>&& edges) {
  if (modules.size() == 0 || modules.size() > kMaximumModules || edges.size() > kMaximumEdges) {
    return zc::none;
  }
  sortByBytes(modules, [](const identity::ModuleKey& module) { return module.encode(); });
  for (size_t index = 1; index < modules.size(); ++index) {
    if (sameModule(modules[index - 1], modules[index])) { return zc::none; }
  }
  sortByBytes(edges, [](const ModuleDependencyEdgeKey& edge) { return edge.encodeCanonical(); });
  for (size_t index = 0; index < edges.size(); ++index) {
    if (!containsModule(modules.asPtr(), edges[index].requester()) ||
        !containsModule(modules.asPtr(), edges[index].dependency()) ||
        (index != 0 &&
         edges[index - 1].encodeCanonical().asPtr() == edges[index].encodeCanonical().asPtr())) {
      return zc::none;
    }
  }
  ModuleGraphRecord result(zc::mv(modules), zc::mv(edges));
  if (result.encodeCanonical().size() > kMaximumValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ModuleGraphRecord> ModuleGraphRecord::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kGraphValueDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto moduleCount = decoder.decodeSequenceSize(kMaximumModules);
  if (moduleCount == zc::none || ZC_ASSERT_NONNULL(moduleCount) == 0) { return zc::none; }
  zc::Vector<identity::ModuleKey> modules(static_cast<size_t>(ZC_ASSERT_NONNULL(moduleCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(moduleCount); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    auto module = decodeModule(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (module == zc::none) { return zc::none; }
    modules.add(zc::mv(ZC_ASSERT_NONNULL(module)));
  }
  auto edgeCount = decoder.decodeSequenceSize(kMaximumEdges);
  if (edgeCount == zc::none) { return zc::none; }
  zc::Vector<ModuleDependencyEdgeKey> edges(static_cast<size_t>(ZC_ASSERT_NONNULL(edgeCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(edgeCount); ++index) {
    auto encoded = decoder.decodeByteString(2 * kMaximumModuleKeyBytes + 64);
    if (encoded == zc::none) { return zc::none; }
    auto edge = ModuleDependencyEdgeKey::decodeCanonical(ZC_ASSERT_NONNULL(encoded).asPtr());
    if (edge == zc::none) { return zc::none; }
    edges.add(zc::mv(ZC_ASSERT_NONNULL(edge)));
  }
  if (!decoder.finished()) { return zc::none; }
  auto result = from(zc::mv(modules), zc::mv(edges));
  if (result == zc::none || ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ModuleGraphRecord ModuleGraphRecord::clone() const {
  zc::Vector<identity::ModuleKey> modules(moduleValues.size());
  for (const auto& module : moduleValues) { modules.add(module.clone()); }
  zc::Vector<ModuleDependencyEdgeKey> edges(edgeValues.size());
  for (const auto& edge : edgeValues) { edges.add(edge.clone()); }
  return ModuleGraphRecord(zc::mv(modules), zc::mv(edges));
}

zc::ArrayPtr<const identity::ModuleKey> ModuleGraphRecord::modules() const noexcept {
  return moduleValues.asPtr();
}

zc::ArrayPtr<const ModuleDependencyEdgeKey> ModuleGraphRecord::edges() const noexcept {
  return edgeValues.asPtr();
}

zc::Array<uint8_t> ModuleGraphRecord::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  payload.encodeSequenceSize(moduleValues.size());
  for (const auto& module : moduleValues) {
    const auto bytes = module.encode();
    payload.encodeByteString(bytes.asPtr());
  }
  payload.encodeSequenceSize(edgeValues.size());
  for (const auto& edge : edgeValues) {
    const auto bytes = edge.encodeCanonical();
    payload.encodeByteString(bytes.asPtr());
  }
  return frame(kGraphValueDomain, payload.finish().asPtr());
}

ModuleGraphFailureRecord::ModuleGraphFailureRecord(
    ModuleGraphFailureKind kind, zc::Maybe<identity::CrateKey>&& owner,
    identity::ModuleKey&& module, zc::Maybe<identity::ModuleKey>&& dependency) noexcept
    : kindValue(kind),
      ownerValue(zc::mv(owner)),
      moduleValue(zc::mv(module)),
      dependencyValue(zc::mv(dependency)) {}

ModuleGraphFailureRecord ModuleGraphFailureRecord::duplicate(identity::ModuleKey&& module) {
  zc::Maybe<identity::CrateKey> owner;
  zc::Maybe<identity::ModuleKey> dependency;
  return ModuleGraphFailureRecord(ModuleGraphFailureKind::DuplicateActiveModule, zc::mv(owner),
                                  zc::mv(module), zc::mv(dependency));
}

zc::Maybe<ModuleGraphFailureRecord> ModuleGraphFailureRecord::foreign(
    identity::CrateKey&& owner, identity::ModuleKey&& module) {
  if (sameCrate(owner, module.crate())) { return zc::none; }
  zc::Maybe<identity::CrateKey> ownerValue(zc::mv(owner));
  zc::Maybe<identity::ModuleKey> dependency;
  return ModuleGraphFailureRecord(ModuleGraphFailureKind::ForeignActiveModule, zc::mv(ownerValue),
                                  zc::mv(module), zc::mv(dependency));
}

zc::Maybe<ModuleGraphFailureRecord> ModuleGraphFailureRecord::outside(
    identity::ModuleKey&& requester, identity::ModuleKey&& dependency) {
  if (sameModule(requester, dependency)) { return zc::none; }
  zc::Maybe<identity::CrateKey> owner;
  zc::Maybe<identity::ModuleKey> dependencyValue(zc::mv(dependency));
  return ModuleGraphFailureRecord(ModuleGraphFailureKind::DependencyOutsideGraph, zc::mv(owner),
                                  zc::mv(requester), zc::mv(dependencyValue));
}

zc::Maybe<ModuleGraphFailureRecord> ModuleGraphFailureRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kGraphFailureDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto kind = decoder.decodeUint8();
  if (kind == zc::none) { return zc::none; }
  zc::Maybe<ModuleGraphFailureRecord> result;
  if (ZC_ASSERT_NONNULL(kind) ==
      static_cast<uint8_t>(ModuleGraphFailureKind::DuplicateActiveModule)) {
    auto moduleBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (moduleBytes == zc::none) { return zc::none; }
    auto module = decodeModule(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
    if (module == zc::none) { return zc::none; }
    result = duplicate(zc::mv(ZC_ASSERT_NONNULL(module)));
  } else if (ZC_ASSERT_NONNULL(kind) ==
             static_cast<uint8_t>(ModuleGraphFailureKind::ForeignActiveModule)) {
    auto ownerBytes = decoder.decodeByteString(kMaximumCrateKeyBytes);
    auto moduleBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (ownerBytes == zc::none || moduleBytes == zc::none) { return zc::none; }
    auto owner = decodeCrate(ZC_ASSERT_NONNULL(ownerBytes).asPtr());
    auto module = decodeModule(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
    if (owner == zc::none || module == zc::none) { return zc::none; }
    result = foreign(zc::mv(ZC_ASSERT_NONNULL(owner)), zc::mv(ZC_ASSERT_NONNULL(module)));
  } else if (ZC_ASSERT_NONNULL(kind) ==
             static_cast<uint8_t>(ModuleGraphFailureKind::DependencyOutsideGraph)) {
    auto requesterBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
    auto dependencyBytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
    if (requesterBytes == zc::none || dependencyBytes == zc::none) { return zc::none; }
    auto requester = decodeModule(ZC_ASSERT_NONNULL(requesterBytes).asPtr());
    auto dependency = decodeModule(ZC_ASSERT_NONNULL(dependencyBytes).asPtr());
    if (requester == zc::none || dependency == zc::none) { return zc::none; }
    result = outside(zc::mv(ZC_ASSERT_NONNULL(requester)), zc::mv(ZC_ASSERT_NONNULL(dependency)));
  } else {
    return zc::none;
  }
  if (!decoder.finished() || result == zc::none ||
      ZC_ASSERT_NONNULL(result).encodeCanonical().asPtr() != bytes) {
    return zc::none;
  }
  return zc::mv(ZC_ASSERT_NONNULL(result));
}

ModuleGraphFailureRecord ModuleGraphFailureRecord::clone() const {
  zc::Maybe<identity::CrateKey> owner;
  ZC_IF_SOME(value, ownerValue) { owner = value.clone(); }
  zc::Maybe<identity::ModuleKey> dependency;
  ZC_IF_SOME(value, dependencyValue) { dependency = value.clone(); }
  return ModuleGraphFailureRecord(kindValue, zc::mv(owner), moduleValue.clone(),
                                  zc::mv(dependency));
}

ModuleGraphFailureKind ModuleGraphFailureRecord::kind() const noexcept { return kindValue; }

zc::Maybe<const identity::CrateKey&> ModuleGraphFailureRecord::owner() const noexcept {
  ZC_IF_SOME(value, ownerValue) { return value; }
  return zc::none;
}

const identity::ModuleKey& ModuleGraphFailureRecord::module() const noexcept { return moduleValue; }

zc::Maybe<const identity::ModuleKey&> ModuleGraphFailureRecord::dependency() const noexcept {
  ZC_IF_SOME(value, dependencyValue) { return value; }
  return zc::none;
}

zc::Array<uint8_t> ModuleGraphFailureRecord::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  payload.encodeUint8(static_cast<uint8_t>(kindValue));
  if (kindValue == ModuleGraphFailureKind::ForeignActiveModule) {
    const auto ownerBytes = ZC_REQUIRE_NONNULL(ownerValue).encode();
    payload.encodeByteString(ownerBytes.asPtr());
  }
  const auto moduleBytes = moduleValue.encode();
  payload.encodeByteString(moduleBytes.asPtr());
  if (kindValue == ModuleGraphFailureKind::DependencyOutsideGraph) {
    const auto dependencyBytes = ZC_REQUIRE_NONNULL(dependencyValue).encode();
    payload.encodeByteString(dependencyBytes.asPtr());
  }
  return frame(kGraphFailureDomain, payload.finish().asPtr());
}

ModuleGraphSccComponent::ModuleGraphSccComponent(zc::Vector<identity::ModuleKey>&& modules,
                                                 bool cyclic) noexcept
    : moduleValues(zc::mv(modules)), cyclicValue(cyclic) {}

zc::Maybe<ModuleGraphSccComponent> ModuleGraphSccComponent::from(
    zc::Vector<identity::ModuleKey>&& modules, bool cyclic) {
  if (modules.size() == 0 || modules.size() > kMaximumModules || (modules.size() > 1 && !cyclic)) {
    return zc::none;
  }
  sortByBytes(modules, [](const identity::ModuleKey& module) { return module.encode(); });
  for (size_t index = 1; index < modules.size(); ++index) {
    if (sameModule(modules[index - 1], modules[index])) { return zc::none; }
  }
  return ModuleGraphSccComponent(zc::mv(modules), cyclic);
}

ModuleGraphSccComponent ModuleGraphSccComponent::clone() const {
  zc::Vector<identity::ModuleKey> modules(moduleValues.size());
  for (const auto& module : moduleValues) { modules.add(module.clone()); }
  return ModuleGraphSccComponent(zc::mv(modules), cyclicValue);
}

zc::ArrayPtr<const identity::ModuleKey> ModuleGraphSccComponent::modules() const noexcept {
  return moduleValues.asPtr();
}

bool ModuleGraphSccComponent::cyclic() const noexcept { return cyclicValue; }

zc::Array<uint8_t> ModuleGraphSccComponent::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(moduleValues.size());
  for (const auto& module : moduleValues) {
    const auto bytes = module.encode();
    encoder.encodeByteString(bytes.asPtr());
  }
  encoder.encodeBool(cyclicValue);
  return encoder.finish();
}

ModuleGraphSccRecord::ModuleGraphSccRecord(
    const identity::Sha256Digest& graphDigest,
    zc::Vector<ModuleGraphSccComponent>&& components) noexcept
    : graphDigestValue(graphDigest), componentValues(zc::mv(components)) {}

zc::Maybe<ModuleGraphSccRecord> ModuleGraphSccRecord::fromVerified(
    const ModuleGraphRecord& graph, zc::Vector<ModuleGraphSccComponent>&& components) {
  if (components.size() == 0 || components.size() > graph.modules().size()) { return zc::none; }
  for (const auto& module : graph.modules()) {
    size_t occurrences = 0;
    for (const auto& component : components) {
      if (containsModule(component.modules(), module)) { ++occurrences; }
    }
    if (occurrences != 1) { return zc::none; }
  }
  for (const auto& component : components) {
    for (const auto& module : component.modules()) {
      if (!containsModule(graph.modules(), module)) { return zc::none; }
    }
    if (component.cyclic() != componentIsCyclic(graph, component.modules())) { return zc::none; }
  }
  for (const auto& edge : graph.edges()) {
    auto requester = componentIndex(components.asPtr(), edge.requester());
    auto dependency = componentIndex(components.asPtr(), edge.dependency());
    if (requester == zc::none || dependency == zc::none ||
        (ZC_ASSERT_NONNULL(requester) != ZC_ASSERT_NONNULL(dependency) &&
         ZC_ASSERT_NONNULL(dependency) >= ZC_ASSERT_NONNULL(requester))) {
      return zc::none;
    }
  }
  auto digest = identity::sha256(graph.encodeCanonical().asPtr());
  if (digest == zc::none) { return zc::none; }
  ModuleGraphSccRecord result(ZC_ASSERT_NONNULL(digest), zc::mv(components));
  if (result.encodeCanonical().size() > kMaximumValueBytes) { return zc::none; }
  return zc::mv(result);
}

zc::Maybe<ModuleGraphSccRecord> ModuleGraphSccRecord::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto payload = unframe(kSccValueDomain, bytes);
  if (payload == zc::none) { return zc::none; }
  identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(payload));
  auto graphDigest = decoder.decodeDigest();
  auto componentCount = decoder.decodeSequenceSize(kMaximumModules);
  if (graphDigest == zc::none || componentCount == zc::none ||
      ZC_ASSERT_NONNULL(componentCount) == 0) {
    return zc::none;
  }
  zc::Vector<ModuleGraphSccComponent> components(
      static_cast<size_t>(ZC_ASSERT_NONNULL(componentCount)));
  zc::Vector<identity::ModuleKey> allModules;
  for (uint64_t componentIndexValue = 0; componentIndexValue < ZC_ASSERT_NONNULL(componentCount);
       ++componentIndexValue) {
    auto componentBytes = decoder.decodeByteString(kMaximumValueBytes);
    if (componentBytes == zc::none) { return zc::none; }
    identity::CanonicalDecoder componentDecoder(ZC_ASSERT_NONNULL(componentBytes).asPtr());
    auto moduleCount = componentDecoder.decodeSequenceSize(kMaximumModules);
    if (moduleCount == zc::none || ZC_ASSERT_NONNULL(moduleCount) == 0) { return zc::none; }
    zc::Vector<identity::ModuleKey> modules(static_cast<size_t>(ZC_ASSERT_NONNULL(moduleCount)));
    for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(moduleCount); ++index) {
      auto moduleBytes = componentDecoder.decodeByteString(kMaximumModuleKeyBytes);
      if (moduleBytes == zc::none) { return zc::none; }
      auto module = decodeModule(ZC_ASSERT_NONNULL(moduleBytes).asPtr());
      if (module == zc::none) { return zc::none; }
      for (const auto& prior : allModules) {
        if (sameModule(prior, ZC_ASSERT_NONNULL(module))) { return zc::none; }
      }
      allModules.add(ZC_ASSERT_NONNULL(module).clone());
      modules.add(zc::mv(ZC_ASSERT_NONNULL(module)));
    }
    auto cyclic = componentDecoder.decodeBool();
    if (cyclic == zc::none || !componentDecoder.finished()) { return zc::none; }
    auto component = ModuleGraphSccComponent::from(zc::mv(modules), ZC_ASSERT_NONNULL(cyclic));
    if (component == zc::none || ZC_ASSERT_NONNULL(component).encodeCanonical().asPtr() !=
                                     ZC_ASSERT_NONNULL(componentBytes).asPtr()) {
      return zc::none;
    }
    components.add(zc::mv(ZC_ASSERT_NONNULL(component)));
  }
  if (!decoder.finished()) { return zc::none; }
  ModuleGraphSccRecord result(ZC_ASSERT_NONNULL(graphDigest), zc::mv(components));
  if (result.encodeCanonical().asPtr() != bytes) { return zc::none; }
  return zc::mv(result);
}

ModuleGraphSccRecord ModuleGraphSccRecord::clone() const {
  zc::Vector<ModuleGraphSccComponent> components(componentValues.size());
  for (const auto& component : componentValues) { components.add(component.clone()); }
  return ModuleGraphSccRecord(graphDigestValue, zc::mv(components));
}

const identity::Sha256Digest& ModuleGraphSccRecord::graphDigest() const noexcept {
  return graphDigestValue;
}

zc::ArrayPtr<const ModuleGraphSccComponent> ModuleGraphSccRecord::components() const noexcept {
  return componentValues.asPtr();
}

zc::Array<uint8_t> ModuleGraphSccRecord::encodeCanonical() const {
  identity::CanonicalEncoder payload;
  payload.encodeDigest(graphDigestValue);
  payload.encodeSequenceSize(componentValues.size());
  for (const auto& component : componentValues) {
    const auto bytes = component.encodeCanonical();
    payload.encodeByteString(bytes.asPtr());
  }
  return frame(kSccValueDomain, payload.finish().asPtr());
}

bool ModuleGraphSccRecord::hasCycle(const ModuleGraphRecord& graph) const {
  auto digest = identity::sha256(graph.encodeCanonical().asPtr());
  if (digest == zc::none || ZC_ASSERT_NONNULL(digest) != graphDigestValue) { return true; }
  for (const auto& component : componentValues) {
    if (component.cyclic()) { return true; }
  }
  return false;
}

zc::StringPtr ModuleGraphQuery::domain() { return "zom.query.module-graph"_zc; }

query::QueryKindContract ModuleGraphQuery::contract() { return derivedContract(domain()); }

zc::Array<uint8_t> ModuleGraphQuery::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<ModuleGraphQuery::Key> ModuleGraphQuery::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleGraphQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleGraphQuery::Value> ModuleGraphQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleGraphRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleGraphQuery::Value> ModuleGraphQuery::provide(
    query::QueryContext& context, const Key& key) {
  return evaluateGraph(context, key);
}

bool ModuleGraphQuery::verify(query::QueryContext& context, const Key& key,
                              const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto expected = evaluateVerifierGraph(context, key);
  if (expected.isRuntimeFailure() || expected.kind() != result.kind()) { return false; }
  switch (result.kind()) {
    case query::QueryValueKind::Value:
      return expected.value().encodeCanonical().asPtr() == result.value().encodeCanonical().asPtr();
    case query::QueryValueKind::Absence:
      return true;
    case query::QueryValueKind::SemanticFailure:
      return expected.semanticFailureBytes() == result.semanticFailureBytes();
  }
  return false;
}

zc::StringPtr ModuleGraphSccQuery::domain() { return "zom.query.module-graph-scc"_zc; }

query::QueryKindContract ModuleGraphSccQuery::contract() { return derivedContract(domain()); }

zc::Array<uint8_t> ModuleGraphSccQuery::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<ModuleGraphSccQuery::Key> ModuleGraphSccQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return incremental_binding_query::CompilationRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleGraphSccQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleGraphSccQuery::Value> ModuleGraphSccQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleGraphSccRecord::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleGraphSccQuery::Value> ModuleGraphSccQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto graph = context.get<ModuleGraphQuery>(key);
  if (graph.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(graph.runtimeFailure());
  }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return query::TypedQueryResult<Value>::semanticFailure(
        zc::heapArray<uint8_t>(graph.semanticFailureBytes()));
  }
  if (graph.kind() == query::QueryValueKind::Absence) {
    return query::TypedQueryResult<Value>::absence();
  }
  Tarjan tarjan(graph.value());
  auto components = tarjan.run();
  if (components == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  auto value =
      ModuleGraphSccRecord::fromVerified(graph.value(), zc::mv(ZC_ASSERT_NONNULL(components)));
  if (value == zc::none) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  return query::TypedQueryResult<Value>::value(zc::mv(ZC_ASSERT_NONNULL(value)));
}

bool ModuleGraphSccQuery::verify(query::QueryContext& context, const Key& key,
                                 const query::TypedQueryResult<Value>& result) {
  if (result.isRuntimeFailure()) { return false; }
  auto graph = context.get<ModuleGraphQuery>(key);
  if (graph.isRuntimeFailure() || graph.kind() != result.kind()) { return false; }
  if (graph.kind() == query::QueryValueKind::SemanticFailure) {
    return graph.semanticFailureBytes() == result.semanticFailureBytes();
  }
  if (graph.kind() == query::QueryValueKind::Absence) {
    return result.kind() == query::QueryValueKind::Absence;
  }
  Kosaraju kosaraju(graph.value());
  auto components = kosaraju.run();
  if (components == zc::none) { return false; }
  auto expected =
      ModuleGraphSccRecord::fromVerified(graph.value(), zc::mv(ZC_ASSERT_NONNULL(components)));
  return expected != zc::none && result.kind() == query::QueryValueKind::Value &&
         ZC_ASSERT_NONNULL(expected).encodeCanonical().asPtr() ==
             result.value().encodeCanonical().asPtr();
}

bool registerStableModuleGraphQueries(query::QueryDatabase& database) {
  return database.registerDerivedKind<ModuleGraphQuery>() != zc::none &&
         database.registerDerivedKind<ModuleGraphSccQuery>() != zc::none;
}

}  // namespace zomlang::compiler::driver::module_graph_query
