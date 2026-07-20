// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"

#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/named-item-query.h"
#include "zomlang/compiler/driver/owner-body-query.h"
#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/parser/parse-source-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr uint64_t kMaximumModuleKeyBytes = 16 * 1024;
constexpr uint64_t kMaximumSourceKeyBytes = 64 * 1024;
constexpr uint64_t kMaximumPackageOrCrateKeyBytes = 2 * 1024 * 1024;
constexpr uint64_t kMaximumPackageRoots = 4096;
constexpr uint64_t kMaximumActiveCrates = 4096;
constexpr uint64_t kMaximumEncodedPackageOrCrateSetBytes = 64 * 1024 * 1024;
constexpr uint64_t kMaximumActiveSources = 65536;
constexpr uint64_t kMaximumActiveModules = 4096;
constexpr uint64_t kMaximumEncodedModuleSetBytes = 64 * 1024 * 1024;

int compareBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) noexcept {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) return -1;
    if (left[index] > right[index]) return 1;
  }
  if (left.size() < right.size()) return -1;
  if (left.size() > right.size()) return 1;
  return 0;
}

void encodeModuleKey(identity::CanonicalEncoder& encoder, const StableModuleQueryKey& key) {
  encoder.encodeByteString(key.canonicalModuleBytes());
}

zc::Maybe<StableModuleQueryKey> decodeModuleKey(identity::CanonicalDecoder& decoder) {
  auto bytes = decoder.decodeByteString(kMaximumModuleKeyBytes);
  if (bytes == zc::none) { return zc::none; }
  ZC_IF_SOME(value, bytes) { return ModuleDependenciesInput::decodeKey(value.asPtr()); }
  return zc::none;
}

query::QueryKindContract inputContract(zc::StringPtr domain, query::Durability durability) {
  auto contract = query::QueryKindContract::input(domain, 1, 1, durability);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

query::QueryKindContract lowDurabilityInputContract(zc::StringPtr domain) {
  return inputContract(domain, query::Durability::Low);
}

bool moduleBelongsToCrate(const StableModuleQueryKey& module, const StableCrateQueryKey& crate) {
  identity::CanonicalDecoder decoder(module.canonicalModuleBytes());
  auto decoded = identity::ModuleKey::decodeCanonical(decoder);
  if (decoded == zc::none || !decoder.finished()) { return false; }
  ZC_IF_SOME(value, decoded) {
    auto owner = value.crate().encode();
    return owner.asPtr() == crate.canonicalCrateBytes();
  }
  return false;
}

query::QueryKindContract derivedContract(zc::StringPtr domain) {
  auto contract = query::QueryKindContract::derived(domain, 1, 1, query::ReuseClass::Semantic,
                                                    query::RetentionClass::Retained);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

struct TopologyNode final {
  StableModuleQueryKey module;
  ModuleDependencySet dependencyInput;
};

zc::Maybe<size_t> providerModuleIndex(zc::ArrayPtr<const TopologyNode> topology,
                                      const StableModuleQueryKey& module) {
  for (size_t index = 0; index < topology.size(); ++index) {
    if (topology[index].module == module) { return index; }
  }
  return zc::none;
}

bool providerDependenciesReady(const TopologyNode& node, zc::ArrayPtr<const TopologyNode> topology,
                               zc::ArrayPtr<const uint8_t> emitted) {
  ZC_IF_SOME(dependencies, node.dependencyInput.dependencies()) {
    for (const auto& dependency : dependencies.modules()) {
      auto index = providerModuleIndex(topology, dependency);
      if (index == zc::none) { return false; }
      ZC_IF_SOME(value, index) {
        if (value >= emitted.size() || emitted[value] == 0) { return false; }
      }
    }
    return true;
  }
  return false;
}

zc::Maybe<ModuleBindingOrderFailure> providerStructuralFailure(
    zc::ArrayPtr<const TopologyNode> topology, const CanonicalModuleSet& active) {
  for (const auto& node : topology) {
    if (node.dependencyInput.isMissing()) {
      return ModuleBindingOrderFailure::missingDependencies(node.module);
    }
  }
  for (const auto& node : topology) {
    ZC_IF_SOME(dependencies, node.dependencyInput.dependencies()) {
      for (const auto& dependency : dependencies.modules()) {
        if (dependency == node.module) {
          return ModuleBindingOrderFailure::selfDependency(node.module);
        }
        if (!active.contains(dependency)) {
          return ModuleBindingOrderFailure::dependencyOutsideActiveSet(node.module, dependency);
        }
      }
    }
  }
  return zc::none;
}

query::TypedQueryResult<ModuleBindingOrder> providerOrder(
    zc::ArrayPtr<const TopologyNode> topology) {
  zc::Vector<uint8_t> emitted;
  emitted.resize(topology.size());
  for (auto& value : emitted) { value = 0; }
  zc::Vector<StableModuleQueryKey> order(topology.size());
  for (size_t output = 0; output < topology.size(); ++output) {
    size_t ready = topology.size();
    for (size_t candidate = 0; candidate < topology.size(); ++candidate) {
      if (emitted[candidate] == 0 &&
          providerDependenciesReady(topology[candidate], topology, emitted.asPtr())) {
        ready = candidate;
        break;
      }
    }
    if (ready == topology.size()) {
      for (size_t candidate = 0; candidate < topology.size(); ++candidate) {
        if (emitted[candidate] == 0) {
          return query::TypedQueryResult<ModuleBindingOrder>::semanticFailure(
              ModuleBindingOrderFailure::cycle(topology[candidate].module).encode());
        }
      }
      return query::TypedQueryResult<ModuleBindingOrder>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    emitted[ready] = 1;
    order.add(topology[ready].module.clone());
  }
  auto admitted = ModuleBindingOrder::fromUnique(zc::mv(order));
  if (admitted == zc::none) {
    return query::TypedQueryResult<ModuleBindingOrder>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  ZC_IF_SOME(value, admitted) {
    return query::TypedQueryResult<ModuleBindingOrder>::value(zc::mv(value));
  }
  return query::TypedQueryResult<ModuleBindingOrder>::runtimeFailure(
      query::QueryRuntimeFailure::ProviderRejected);
}

zc::Maybe<size_t> verifierModuleIndex(zc::ArrayPtr<const TopologyNode> topology,
                                      const StableModuleQueryKey& module) {
  size_t lower = 0;
  size_t upper = topology.size();
  while (lower < upper) {
    const size_t middle = lower + ((upper - lower) / 2);
    if (topology[middle].module < module) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  if (lower < topology.size() && topology[lower].module == module) { return lower; }
  return zc::none;
}

bool verifierReady(const TopologyNode& node, zc::ArrayPtr<const TopologyNode> topology,
                   zc::ArrayPtr<const uint8_t> placed) {
  auto dependencies = node.dependencyInput.dependencies();
  if (dependencies == zc::none) { return false; }
  ZC_IF_SOME(values, dependencies) {
    for (const auto& dependency : values.modules()) {
      auto index = verifierModuleIndex(topology, dependency);
      if (index == zc::none) { return false; }
      ZC_IF_SOME(value, index) {
        if (value >= placed.size() || placed[value] == 0) { return false; }
      }
    }
  }
  return true;
}

zc::Maybe<ModuleBindingOrderFailure> verifierExpectedFailure(
    zc::ArrayPtr<const TopologyNode> topology) {
  for (size_t requester = 0; requester < topology.size(); ++requester) {
    if (topology[requester].dependencyInput.dependencies() == zc::none) {
      return ModuleBindingOrderFailure::missingDependencies(topology[requester].module);
    }
  }
  for (size_t requester = 0; requester < topology.size(); ++requester) {
    ZC_IF_SOME(dependencies, topology[requester].dependencyInput.dependencies()) {
      for (const auto& dependency : dependencies.modules()) {
        if (dependency == topology[requester].module) {
          return ModuleBindingOrderFailure::selfDependency(topology[requester].module);
        }
        if (verifierModuleIndex(topology, dependency) == zc::none) {
          return ModuleBindingOrderFailure::dependencyOutsideActiveSet(topology[requester].module,
                                                                       dependency);
        }
      }
    }
  }

  zc::Vector<uint8_t> placed;
  placed.resize(topology.size());
  for (auto& value : placed) { value = 0; }
  for (size_t count = 0; count < topology.size(); ++count) {
    size_t next = topology.size();
    for (size_t candidate = 0; candidate < topology.size(); ++candidate) {
      if (placed[candidate] == 0 && verifierReady(topology[candidate], topology, placed.asPtr())) {
        next = candidate;
        break;
      }
    }
    if (next == topology.size()) {
      for (size_t candidate = 0; candidate < topology.size(); ++candidate) {
        if (placed[candidate] == 0) {
          return ModuleBindingOrderFailure::cycle(topology[candidate].module);
        }
      }
      return zc::none;
    }
    placed[next] = 1;
  }
  return zc::none;
}

bool verifierAcceptsOrder(zc::ArrayPtr<const TopologyNode> topology,
                          const ModuleBindingOrder& order) {
  if (order.modules().size() != topology.size()) { return false; }
  zc::Vector<uint8_t> placed;
  placed.resize(topology.size());
  for (auto& value : placed) { value = 0; }
  for (const auto& current : order.modules()) {
    auto currentIndex = verifierModuleIndex(topology, current);
    if (currentIndex == zc::none) { return false; }
    ZC_IF_SOME(index, currentIndex) {
      if (placed[index] != 0) { return false; }
      size_t canonicalReady = topology.size();
      for (size_t candidate = 0; candidate < topology.size(); ++candidate) {
        if (placed[candidate] == 0 &&
            verifierReady(topology[candidate], topology, placed.asPtr())) {
          canonicalReady = candidate;
          break;
        }
      }
      if (canonicalReady != index) { return false; }
      placed[index] = 1;
    }
  }
  return true;
}

}  // namespace

StablePackageQueryKey::StablePackageQueryKey(zc::Array<uint8_t>&& canonicalPackageBytes) noexcept
    : canonicalPackageBytesField(zc::mv(canonicalPackageBytes)) {}

zc::Maybe<StablePackageQueryKey> StablePackageQueryKey::fromVerified(
    const identity::PackageKey& package) {
  return decodeBounded(package.encode().asPtr());
}

zc::Maybe<StablePackageQueryKey> StablePackageQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumPackageOrCrateKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto package = identity::PackageKey::decodeCanonical(decoder);
  if (package == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, package) {
    auto canonical = value.encode();
    if (canonical.asPtr() != bytes) { return zc::none; }
  }
  return StablePackageQueryKey(zc::heapArray<uint8_t>(bytes));
}

StablePackageQueryKey StablePackageQueryKey::clone() const {
  return StablePackageQueryKey(zc::heapArray<uint8_t>(canonicalPackageBytesField.asPtr()));
}

zc::ArrayPtr<const uint8_t> StablePackageQueryKey::canonicalPackageBytes() const {
  return canonicalPackageBytesField.asPtr();
}

bool StablePackageQueryKey::operator==(const StablePackageQueryKey& other) const noexcept {
  return canonicalPackageBytes() == other.canonicalPackageBytes();
}

bool StablePackageQueryKey::operator<(const StablePackageQueryKey& other) const noexcept {
  return compareBytes(canonicalPackageBytes(), other.canonicalPackageBytes()) < 0;
}

PackageRootSetQueryKey::PackageRootSetQueryKey(
    zc::Vector<StablePackageQueryKey>&& packages) noexcept
    : packageFields(zc::mv(packages)) {}

zc::Maybe<PackageRootSetQueryKey> PackageRootSetQueryKey::fromVerified(
    const package::VerifiedPackageCompilationRequest& request) {
  zc::Vector<StablePackageQueryKey> packages(request.roots().size());
  for (const auto& root : request.roots()) {
    auto package = StablePackageQueryKey::fromVerified(root.packageKey());
    if (package == zc::none) { return zc::none; }
    ZC_IF_SOME(value, package) { packages.add(zc::mv(value)); }
  }
  return from(zc::mv(packages));
}

zc::Maybe<PackageRootSetQueryKey> PackageRootSetQueryKey::from(
    zc::Vector<StablePackageQueryKey>&& packages) {
  if (packages.size() == 0 || packages.size() > kMaximumPackageRoots) { return zc::none; }
  for (size_t index = 1; index < packages.size(); ++index) {
    auto current = zc::mv(packages[index]);
    size_t insertion = index;
    while (insertion != 0 && current < packages[insertion - 1]) {
      packages[insertion] = zc::mv(packages[insertion - 1]);
      --insertion;
    }
    packages[insertion] = zc::mv(current);
  }
  zc::Vector<StablePackageQueryKey> unique(packages.size());
  for (auto& package : packages) {
    if (unique.size() == 0 || unique.back() != package) { unique.add(zc::mv(package)); }
  }
  PackageRootSetQueryKey result(zc::mv(unique));
  if (result.encodeCanonical().size() > kMaximumEncodedPackageOrCrateSetBytes) { return zc::none; }
  return zc::mv(result);
}

PackageRootSetQueryKey PackageRootSetQueryKey::clone() const {
  zc::Vector<StablePackageQueryKey> packages(packageFields.size());
  for (const auto& package : packageFields) { packages.add(package.clone()); }
  return PackageRootSetQueryKey(zc::mv(packages));
}

zc::ArrayPtr<const StablePackageQueryKey> PackageRootSetQueryKey::packages() const {
  return packageFields.asPtr();
}

bool PackageRootSetQueryKey::operator==(const PackageRootSetQueryKey& other) const noexcept {
  if (packageFields.size() != other.packageFields.size()) { return false; }
  for (size_t index = 0; index < packageFields.size(); ++index) {
    if (packageFields[index] != other.packageFields[index]) { return false; }
  }
  return true;
}

zc::Array<uint8_t> PackageRootSetQueryKey::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(packageFields.size());
  for (const auto& package : packageFields) {
    encoder.encodeByteString(package.canonicalPackageBytes());
  }
  return encoder.finish();
}

zc::Maybe<PackageRootSetQueryKey> PackageRootSetQueryKey::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedPackageOrCrateSetBytes) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumPackageRoots);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<StablePackageQueryKey> packages(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPackageOrCrateKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto package = StablePackageQueryKey::decodeBounded(value.asPtr());
      if (package == zc::none) { return zc::none; }
      ZC_IF_SOME(packageValue, package) {
        if (packages.size() != 0 && !(packages.back() < packageValue)) { return zc::none; }
        packages.add(zc::mv(packageValue));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return PackageRootSetQueryKey(zc::mv(packages));
}

StableCrateQueryKey::StableCrateQueryKey(zc::Array<uint8_t>&& canonicalCrateBytes) noexcept
    : canonicalCrateBytesField(zc::mv(canonicalCrateBytes)) {}

zc::Maybe<StableCrateQueryKey> StableCrateQueryKey::fromVerified(const identity::CrateKey& crate) {
  return decodeBounded(crate.encode().asPtr());
}

zc::Maybe<StableCrateQueryKey> StableCrateQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumPackageOrCrateKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto crate = identity::CrateKey::decodeCanonical(decoder);
  if (crate == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, crate) {
    auto canonical = value.encode();
    if (canonical.asPtr() != bytes) { return zc::none; }
  }
  return StableCrateQueryKey(zc::heapArray<uint8_t>(bytes));
}

StableCrateQueryKey StableCrateQueryKey::clone() const {
  return StableCrateQueryKey(zc::heapArray<uint8_t>(canonicalCrateBytesField.asPtr()));
}

zc::ArrayPtr<const uint8_t> StableCrateQueryKey::canonicalCrateBytes() const {
  return canonicalCrateBytesField.asPtr();
}

bool StableCrateQueryKey::operator==(const StableCrateQueryKey& other) const noexcept {
  return canonicalCrateBytes() == other.canonicalCrateBytes();
}

bool StableCrateQueryKey::operator<(const StableCrateQueryKey& other) const noexcept {
  return compareBytes(canonicalCrateBytes(), other.canonicalCrateBytes()) < 0;
}

CanonicalCrateSet::CanonicalCrateSet(zc::Vector<StableCrateQueryKey>&& crates) noexcept
    : crateFields(zc::mv(crates)) {}

zc::Maybe<CanonicalCrateSet> CanonicalCrateSet::from(zc::Vector<StableCrateQueryKey>&& crates) {
  if (crates.size() == 0 || crates.size() > kMaximumActiveCrates) { return zc::none; }
  for (size_t index = 1; index < crates.size(); ++index) {
    auto current = zc::mv(crates[index]);
    size_t insertion = index;
    while (insertion != 0 && current < crates[insertion - 1]) {
      crates[insertion] = zc::mv(crates[insertion - 1]);
      --insertion;
    }
    crates[insertion] = zc::mv(current);
  }
  zc::Vector<StableCrateQueryKey> unique(crates.size());
  for (auto& crate : crates) {
    if (unique.size() == 0 || unique.back() != crate) { unique.add(zc::mv(crate)); }
  }
  CanonicalCrateSet result(zc::mv(unique));
  if (result.encodeCanonical().size() > kMaximumEncodedPackageOrCrateSetBytes) { return zc::none; }
  return zc::mv(result);
}

CanonicalCrateSet CanonicalCrateSet::clone() const {
  zc::Vector<StableCrateQueryKey> crates(crateFields.size());
  for (const auto& crate : crateFields) { crates.add(crate.clone()); }
  return CanonicalCrateSet(zc::mv(crates));
}

zc::ArrayPtr<const StableCrateQueryKey> CanonicalCrateSet::crates() const {
  return crateFields.asPtr();
}

bool CanonicalCrateSet::contains(const StableCrateQueryKey& crate) const noexcept {
  for (const auto& candidate : crateFields) {
    if (candidate == crate) { return true; }
  }
  return false;
}

bool CanonicalCrateSet::operator==(const CanonicalCrateSet& other) const noexcept {
  if (crateFields.size() != other.crateFields.size()) { return false; }
  for (size_t index = 0; index < crateFields.size(); ++index) {
    if (crateFields[index] != other.crateFields[index]) { return false; }
  }
  return true;
}

zc::Array<uint8_t> CanonicalCrateSet::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(crateFields.size());
  for (const auto& crate : crateFields) { encoder.encodeByteString(crate.canonicalCrateBytes()); }
  return encoder.finish();
}

zc::Maybe<CanonicalCrateSet> CanonicalCrateSet::decodeCanonical(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedPackageOrCrateSetBytes) {
    return zc::none;
  }
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumActiveCrates);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<StableCrateQueryKey> crates(static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPackageOrCrateKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto crate = StableCrateQueryKey::decodeBounded(value.asPtr());
      if (crate == zc::none) { return zc::none; }
      ZC_IF_SOME(crateValue, crate) {
        if (crates.size() != 0 && !(crates.back() < crateValue)) { return zc::none; }
        crates.add(zc::mv(crateValue));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return CanonicalCrateSet(zc::mv(crates));
}

StableModuleQueryKey::StableModuleQueryKey(zc::Array<uint8_t>&& canonicalModuleBytes) noexcept
    : canonicalModuleBytesField(zc::mv(canonicalModuleBytes)) {}

zc::Maybe<StableModuleQueryKey> StableModuleQueryKey::fromVerified(
    const identity::ModuleKey& module) {
  return decodeBounded(module.encode().asPtr());
}

zc::Maybe<StableModuleQueryKey> StableModuleQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumModuleKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto module = identity::ModuleKey::decodeCanonical(decoder);
  if (module == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, module) {
    auto canonical = value.encode();
    if (canonical.asPtr() != bytes) { return zc::none; }
  }
  return StableModuleQueryKey(zc::heapArray<uint8_t>(bytes));
}

StableModuleQueryKey StableModuleQueryKey::clone() const {
  return StableModuleQueryKey(zc::heapArray<uint8_t>(canonicalModuleBytesField.asPtr()));
}

zc::ArrayPtr<const uint8_t> StableModuleQueryKey::canonicalModuleBytes() const {
  return canonicalModuleBytesField.asPtr();
}

bool StableModuleQueryKey::operator==(const StableModuleQueryKey& other) const noexcept {
  return canonicalModuleBytes() == other.canonicalModuleBytes();
}

bool StableModuleQueryKey::operator<(const StableModuleQueryKey& other) const noexcept {
  return compareBytes(canonicalModuleBytes(), other.canonicalModuleBytes()) < 0;
}

CanonicalSourceSet::CanonicalSourceSet(
    zc::Vector<identity::source_query::StableSourceQueryKey>&& sources) noexcept
    : sourceFields(zc::mv(sources)) {}

zc::Maybe<CanonicalSourceSet> CanonicalSourceSet::from(
    zc::Vector<identity::source_query::StableSourceQueryKey>&& sources) {
  if (sources.size() == 0 || sources.size() > kMaximumActiveSources) { return zc::none; }
  for (size_t index = 1; index < sources.size(); ++index) {
    auto current = zc::mv(sources[index]);
    size_t insertion = index;
    while (insertion != 0 && current < sources[insertion - 1]) {
      sources[insertion] = zc::mv(sources[insertion - 1]);
      --insertion;
    }
    sources[insertion] = zc::mv(current);
  }
  zc::Vector<identity::source_query::StableSourceQueryKey> unique(sources.size());
  for (auto& source : sources) {
    if (unique.size() == 0 || unique.back() != source) { unique.add(zc::mv(source)); }
  }
  CanonicalSourceSet result(zc::mv(unique));
  if (result.encodeCanonical().size() > kMaximumEncodedModuleSetBytes) { return zc::none; }
  return zc::mv(result);
}

CanonicalSourceSet CanonicalSourceSet::clone() const {
  zc::Vector<identity::source_query::StableSourceQueryKey> sources(sourceFields.size());
  for (const auto& source : sourceFields) { sources.add(source.clone()); }
  return CanonicalSourceSet(zc::mv(sources));
}

zc::ArrayPtr<const identity::source_query::StableSourceQueryKey> CanonicalSourceSet::sources()
    const {
  return sourceFields.asPtr();
}

bool CanonicalSourceSet::contains(
    const identity::source_query::StableSourceQueryKey& source) const noexcept {
  for (const auto& candidate : sourceFields) {
    if (candidate == source) { return true; }
  }
  return false;
}

bool CanonicalSourceSet::operator==(const CanonicalSourceSet& other) const noexcept {
  if (sourceFields.size() != other.sourceFields.size()) { return false; }
  for (size_t index = 0; index < sourceFields.size(); ++index) {
    if (sourceFields[index] != other.sourceFields[index]) { return false; }
  }
  return true;
}

zc::Array<uint8_t> CanonicalSourceSet::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(sourceFields.size());
  for (const auto& source : sourceFields) {
    encoder.encodeByteString(source.canonicalSourceBytes());
  }
  return encoder.finish();
}

zc::Maybe<CanonicalSourceSet> CanonicalSourceSet::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedModuleSetBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumActiveSources);
  if (count == zc::none || ZC_ASSERT_NONNULL(count) == 0) { return zc::none; }
  zc::Vector<identity::source_query::StableSourceQueryKey> sources(
      static_cast<size_t>(ZC_ASSERT_NONNULL(count)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(count); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumSourceKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto source = identity::source_query::StableSourceQueryKey::decodeBounded(value.asPtr());
      if (source == zc::none) { return zc::none; }
      ZC_IF_SOME(sourceValue, source) {
        if (sources.size() != 0 && !(sources.back() < sourceValue)) { return zc::none; }
        sources.add(zc::mv(sourceValue));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return CanonicalSourceSet(zc::mv(sources));
}

SelectedModuleSource::SelectedModuleSource(zc::Array<uint8_t>&& canonicalSourceBytes) noexcept
    : canonicalSourceBytesField(zc::mv(canonicalSourceBytes)) {}

zc::Maybe<SelectedModuleSource> SelectedModuleSource::fromVerified(
    const identity::ModuleKey& module, const identity::SourceFileKey& source) {
  if (!source.belongsTo(module.crate())) { return zc::none; }
  return decodeBounded(source.encode().asPtr());
}

zc::Maybe<SelectedModuleSource> SelectedModuleSource::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumSourceKeyBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto source = identity::SourceFileKey::decodeCanonical(decoder);
  if (source == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, source) {
    auto canonical = value.encode();
    if (canonical.asPtr() != bytes) { return zc::none; }
  }
  return SelectedModuleSource(zc::heapArray<uint8_t>(bytes));
}

SelectedModuleSource SelectedModuleSource::clone() const {
  return SelectedModuleSource(zc::heapArray<uint8_t>(canonicalSourceBytesField.asPtr()));
}

zc::ArrayPtr<const uint8_t> SelectedModuleSource::canonicalSourceBytes() const {
  return canonicalSourceBytesField.asPtr();
}

bool SelectedModuleSource::operator==(const SelectedModuleSource& other) const noexcept {
  return canonicalSourceBytes() == other.canonicalSourceBytes();
}

CanonicalModuleSet::CanonicalModuleSet(zc::Vector<StableModuleQueryKey>&& modules) noexcept
    : moduleFields(zc::mv(modules)) {}

zc::Maybe<CanonicalModuleSet> CanonicalModuleSet::from(zc::Vector<StableModuleQueryKey>&& modules) {
  if (modules.size() > kMaximumActiveModules) { return zc::none; }
  for (size_t index = 1; index < modules.size(); ++index) {
    auto current = zc::mv(modules[index]);
    size_t insertion = index;
    while (insertion != 0 && current < modules[insertion - 1]) {
      modules[insertion] = zc::mv(modules[insertion - 1]);
      --insertion;
    }
    modules[insertion] = zc::mv(current);
  }
  zc::Vector<StableModuleQueryKey> unique(modules.size());
  for (auto& module : modules) {
    if (unique.size() == 0 || unique.back() != module) { unique.add(zc::mv(module)); }
  }
  return CanonicalModuleSet(zc::mv(unique));
}

CanonicalModuleSet CanonicalModuleSet::clone() const {
  zc::Vector<StableModuleQueryKey> modules(moduleFields.size());
  for (const auto& module : moduleFields) { modules.add(module.clone()); }
  return CanonicalModuleSet(zc::mv(modules));
}

zc::ArrayPtr<const StableModuleQueryKey> CanonicalModuleSet::modules() const {
  return moduleFields.asPtr();
}

bool CanonicalModuleSet::contains(const StableModuleQueryKey& module) const noexcept {
  size_t lower = 0;
  size_t upper = moduleFields.size();
  while (lower < upper) {
    const size_t middle = lower + ((upper - lower) / 2);
    if (moduleFields[middle] < module) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  return lower < moduleFields.size() && moduleFields[lower] == module;
}

zc::Array<uint8_t> CanonicalModuleSet::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(moduleFields.size());
  for (const auto& module : moduleFields) { encodeModuleKey(encoder, module); }
  return encoder.finish();
}

zc::Maybe<CanonicalModuleSet> CanonicalModuleSet::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumActiveModules);
  if (count == zc::none) { return zc::none; }
  zc::Vector<StableModuleQueryKey> modules(ZC_REQUIRE_NONNULL(count));
  ZC_IF_SOME(value, count) {
    for (uint64_t index = 0; index < value; ++index) {
      auto module = decodeModuleKey(decoder);
      if (module == zc::none) { return zc::none; }
      ZC_IF_SOME(moduleValue, module) {
        if (modules.size() != 0 && !(modules.back() < moduleValue)) { return zc::none; }
        modules.add(zc::mv(moduleValue));
      }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return CanonicalModuleSet(zc::mv(modules));
}

ModuleDependencySet::ModuleDependencySet(zc::Maybe<CanonicalModuleSet>&& dependencies) noexcept
    : dependencyFields(zc::mv(dependencies)) {}

ModuleDependencySet ModuleDependencySet::missing() {
  zc::Maybe<CanonicalModuleSet> noDependencies;
  return ModuleDependencySet(zc::mv(noDependencies));
}

ModuleDependencySet ModuleDependencySet::present(CanonicalModuleSet&& dependencies) {
  zc::Maybe<CanonicalModuleSet> retained(zc::mv(dependencies));
  return ModuleDependencySet(zc::mv(retained));
}

ModuleDependencySet ModuleDependencySet::clone() const {
  zc::Maybe<CanonicalModuleSet> dependencies;
  ZC_IF_SOME(value, dependencyFields) { dependencies = value.clone(); }
  return ModuleDependencySet(zc::mv(dependencies));
}

bool ModuleDependencySet::isMissing() const noexcept { return dependencyFields == zc::none; }

zc::Maybe<const CanonicalModuleSet&> ModuleDependencySet::dependencies() const noexcept {
  ZC_IF_SOME(value, dependencyFields) { return value; }
  return zc::none;
}

zc::Array<uint8_t> ModuleDependencySet::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  ZC_IF_SOME(value, dependencyFields) {
    encoder.encodeSome();
    encoder.encodeByteString(value.encodeCanonical().asPtr());
  } else {
    encoder.encodeNone();
  }
  return encoder.finish();
}

zc::Maybe<ModuleDependencySet> ModuleDependencySet::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto presence = decoder.decodeBool();
  if (presence == zc::none) { return zc::none; }
  ZC_IF_SOME(value, presence) {
    if (!value) {
      if (!decoder.finished()) { return zc::none; }
      return missing();
    }
    auto encodedSet = decoder.decodeByteString(kMaximumEncodedModuleSetBytes);
    if (encodedSet == zc::none || !decoder.finished()) { return zc::none; }
    ZC_IF_SOME(encoded, encodedSet) {
      auto dependencies = CanonicalModuleSet::decodeCanonical(encoded.asPtr());
      if (dependencies == zc::none) { return zc::none; }
      ZC_IF_SOME(dependencyValue, dependencies) { return present(zc::mv(dependencyValue)); }
    }
  }
  return zc::none;
}

ModuleBindingOrder::ModuleBindingOrder(zc::Vector<StableModuleQueryKey>&& modules) noexcept
    : moduleFields(zc::mv(modules)) {}

zc::Maybe<ModuleBindingOrder> ModuleBindingOrder::fromUnique(
    zc::Vector<StableModuleQueryKey>&& modules) {
  if (modules.size() > kMaximumActiveModules) { return zc::none; }
  for (size_t index = 0; index < modules.size(); ++index) {
    for (size_t prior = 0; prior < index; ++prior) {
      if (modules[index] == modules[prior]) { return zc::none; }
    }
  }
  return ModuleBindingOrder(zc::mv(modules));
}

ModuleBindingOrder ModuleBindingOrder::clone() const {
  zc::Vector<StableModuleQueryKey> modules(moduleFields.size());
  for (const auto& module : moduleFields) { modules.add(module.clone()); }
  return ModuleBindingOrder(zc::mv(modules));
}

zc::ArrayPtr<const StableModuleQueryKey> ModuleBindingOrder::modules() const {
  return moduleFields.asPtr();
}

zc::Array<uint8_t> ModuleBindingOrder::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(moduleFields.size());
  for (const auto& module : moduleFields) { encodeModuleKey(encoder, module); }
  return encoder.finish();
}

zc::Maybe<ModuleBindingOrder> ModuleBindingOrder::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto count = decoder.decodeSequenceSize(kMaximumActiveModules);
  if (count == zc::none) { return zc::none; }
  zc::Vector<StableModuleQueryKey> modules(ZC_REQUIRE_NONNULL(count));
  ZC_IF_SOME(value, count) {
    for (uint64_t index = 0; index < value; ++index) {
      auto module = decodeModuleKey(decoder);
      if (module == zc::none) { return zc::none; }
      ZC_IF_SOME(moduleValue, module) { modules.add(zc::mv(moduleValue)); }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  return fromUnique(zc::mv(modules));
}

ModuleBindingOrderFailure::ModuleBindingOrderFailure(
    ModuleBindingOrderFailureKind kind, StableModuleQueryKey&& requester,
    zc::Maybe<StableModuleQueryKey>&& dependency) noexcept
    : kindField(kind), requesterField(zc::mv(requester)), dependencyField(zc::mv(dependency)) {}

ModuleBindingOrderFailure ModuleBindingOrderFailure::withoutDependency(
    ModuleBindingOrderFailureKind kind, const StableModuleQueryKey& requester) {
  zc::Maybe<StableModuleQueryKey> noDependency;
  return ModuleBindingOrderFailure(kind, requester.clone(), zc::mv(noDependency));
}

ModuleBindingOrderFailure ModuleBindingOrderFailure::withDependency(
    ModuleBindingOrderFailureKind kind, const StableModuleQueryKey& requester,
    const StableModuleQueryKey& dependency) {
  zc::Maybe<StableModuleQueryKey> retained(dependency.clone());
  return ModuleBindingOrderFailure(kind, requester.clone(), zc::mv(retained));
}

ModuleBindingOrderFailure ModuleBindingOrderFailure::missingDependencies(
    const StableModuleQueryKey& requester) {
  return withoutDependency(ModuleBindingOrderFailureKind::MissingDependencies, requester);
}

ModuleBindingOrderFailure ModuleBindingOrderFailure::dependencyOutsideActiveSet(
    const StableModuleQueryKey& requester, const StableModuleQueryKey& dependency) {
  return withDependency(ModuleBindingOrderFailureKind::DependencyOutsideActiveSet, requester,
                        dependency);
}

ModuleBindingOrderFailure ModuleBindingOrderFailure::selfDependency(
    const StableModuleQueryKey& requester) {
  return withDependency(ModuleBindingOrderFailureKind::SelfDependency, requester, requester);
}

ModuleBindingOrderFailure ModuleBindingOrderFailure::cycle(const StableModuleQueryKey& requester) {
  return withoutDependency(ModuleBindingOrderFailureKind::Cycle, requester);
}

ModuleBindingOrderFailure ModuleBindingOrderFailure::clone() const {
  zc::Maybe<StableModuleQueryKey> dependency;
  ZC_IF_SOME(value, dependencyField) { dependency = value.clone(); }
  return ModuleBindingOrderFailure(kindField, requesterField.clone(), zc::mv(dependency));
}

ModuleBindingOrderFailureKind ModuleBindingOrderFailure::kind() const noexcept { return kindField; }

const StableModuleQueryKey& ModuleBindingOrderFailure::requester() const noexcept {
  return requesterField;
}

zc::Maybe<const StableModuleQueryKey&> ModuleBindingOrderFailure::dependency() const noexcept {
  ZC_IF_SOME(value, dependencyField) { return value; }
  return zc::none;
}

zc::Array<uint8_t> ModuleBindingOrderFailure::encode() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeUint8(static_cast<uint8_t>(kindField));
  encodeModuleKey(encoder, requesterField);
  ZC_IF_SOME(value, dependencyField) {
    encoder.encodeSome();
    encodeModuleKey(encoder, value);
  } else {
    encoder.encodeNone();
  }
  return encoder.finish();
}

zc::Maybe<ModuleBindingOrderFailure> ModuleBindingOrderFailure::decode(
    zc::ArrayPtr<const uint8_t> bytes) {
  identity::CanonicalDecoder decoder(bytes);
  auto tag = decoder.decodeUint8();
  auto requester = decodeModuleKey(decoder);
  auto hasDependency = decoder.decodeBool();
  if (tag == zc::none || requester == zc::none || hasDependency == zc::none) { return zc::none; }
  zc::Maybe<StableModuleQueryKey> dependency;
  ZC_IF_SOME(present, hasDependency) {
    if (present) {
      auto value = decodeModuleKey(decoder);
      if (value == zc::none) { return zc::none; }
      ZC_IF_SOME(decoded, value) { dependency = zc::mv(decoded); }
    }
  }
  if (!decoder.finished()) { return zc::none; }
  ZC_IF_SOME(tagValue, tag) {
    const auto kind = static_cast<ModuleBindingOrderFailureKind>(tagValue);
    const bool needsDependency =
        kind == ModuleBindingOrderFailureKind::DependencyOutsideActiveSet ||
        kind == ModuleBindingOrderFailureKind::SelfDependency;
    if ((kind != ModuleBindingOrderFailureKind::MissingDependencies &&
         kind != ModuleBindingOrderFailureKind::DependencyOutsideActiveSet &&
         kind != ModuleBindingOrderFailureKind::SelfDependency &&
         kind != ModuleBindingOrderFailureKind::Cycle) ||
        needsDependency != (dependency != zc::none)) {
      return zc::none;
    }
    ZC_IF_SOME(requesterValue, requester) {
      if (kind == ModuleBindingOrderFailureKind::SelfDependency) {
        ZC_IF_SOME(dependencyValue, dependency) {
          if (requesterValue != dependencyValue) { return zc::none; }
        }
      } else if (kind == ModuleBindingOrderFailureKind::DependencyOutsideActiveSet) {
        ZC_IF_SOME(dependencyValue, dependency) {
          if (requesterValue == dependencyValue) { return zc::none; }
        }
      }
      return ModuleBindingOrderFailure(kind, zc::mv(requesterValue), zc::mv(dependency));
    }
  }
  return zc::none;
}

bool ModuleBindingOrderFailure::sameAs(const ModuleBindingOrderFailure& other) const {
  return encode().asPtr() == other.encode().asPtr();
}

zc::StringPtr ActiveModulesInput::domain() { return "zom.query.active-modules.v1"_zc; }

query::QueryKindContract ActiveModulesInput::contract() {
  return lowDurabilityInputContract(domain());
}

zc::Array<uint8_t> ActiveModulesInput::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalCrateBytes());
}

zc::Maybe<ActiveModulesInput::Key> ActiveModulesInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableCrateQueryKey::decodeBounded(bytes);
}

zc::Array<uint8_t> ActiveModulesInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveModulesInput::Value> ActiveModulesInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  auto modules = CanonicalModuleSet::decodeCanonical(bytes);
  if (modules == zc::none || ZC_ASSERT_NONNULL(modules).modules().size() == 0) { return zc::none; }
  return zc::mv(ZC_ASSERT_NONNULL(modules));
}

zc::StringPtr ActiveSourcesInput::domain() { return "zom.query.active-sources.v1"_zc; }

query::QueryKindContract ActiveSourcesInput::contract() {
  return lowDurabilityInputContract(domain());
}

zc::Array<uint8_t> ActiveSourcesInput::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalCrateBytes());
}

zc::Maybe<ActiveSourcesInput::Key> ActiveSourcesInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableCrateQueryKey::decodeBounded(bytes);
}

zc::Array<uint8_t> ActiveSourcesInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveSourcesInput::Value> ActiveSourcesInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalSourceSet::decodeCanonical(bytes);
}

zc::StringPtr ActiveCratesInput::domain() { return "zom.query.active-crates.v1"_zc; }

query::QueryKindContract ActiveCratesInput::contract() {
  return inputContract(domain(), query::Durability::Medium);
}

zc::Array<uint8_t> ActiveCratesInput::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<ActiveCratesInput::Key> ActiveCratesInput::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return PackageRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ActiveCratesInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ActiveCratesInput::Value> ActiveCratesInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalCrateSet::decodeCanonical(bytes);
}

zc::StringPtr ModuleDependenciesInput::domain() { return "zom.driver.module-dependencies"_zc; }

query::QueryKindContract ModuleDependenciesInput::contract() {
  return lowDurabilityInputContract(domain());
}

zc::Array<uint8_t> ModuleDependenciesInput::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalModuleBytes());
}

zc::Maybe<ModuleDependenciesInput::Key> ModuleDependenciesInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableModuleQueryKey::decodeBounded(bytes);
}

zc::Array<uint8_t> ModuleDependenciesInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleDependenciesInput::Value> ModuleDependenciesInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleDependencySet::decodeCanonical(bytes);
}

zc::StringPtr SelectedModuleSourceInput::domain() {
  return "zom.query.selected-module-source.v1"_zc;
}

query::QueryKindContract SelectedModuleSourceInput::contract() {
  return lowDurabilityInputContract(domain());
}

zc::Array<uint8_t> SelectedModuleSourceInput::encodeKey(const Key& key) {
  return zc::heapArray<uint8_t>(key.canonicalModuleBytes());
}

zc::Maybe<SelectedModuleSourceInput::Key> SelectedModuleSourceInput::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return StableModuleQueryKey::decodeBounded(bytes);
}

zc::Array<uint8_t> SelectedModuleSourceInput::encodeValue(const Value& value) {
  return zc::heapArray<uint8_t>(value.canonicalSourceBytes());
}

zc::Maybe<SelectedModuleSourceInput::Value> SelectedModuleSourceInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return SelectedModuleSource::decodeBounded(bytes);
}

bool verifySelectedSourceSnapshotClosure(
    zc::ArrayPtr<const SelectedModuleSource> selectedSources,
    zc::ArrayPtr<const identity::source_query::StableSourceQueryKey> snapshotSources) {
  for (const auto& selected : selectedSources) {
    size_t occurrences = 0;
    for (const auto& snapshot : snapshotSources) {
      if (selected.canonicalSourceBytes() == snapshot.canonicalSourceBytes()) { ++occurrences; }
    }
    if (occurrences != 1) { return false; }
  }
  return true;
}

zc::StringPtr ModuleBindingOrderQuery::domain() { return "zom.driver.module-binding-order"_zc; }

query::QueryKindContract ModuleBindingOrderQuery::contract() { return derivedContract(domain()); }

zc::Array<uint8_t> ModuleBindingOrderQuery::encodeKey(const Key& key) {
  return key.encodeCanonical();
}

zc::Maybe<ModuleBindingOrderQuery::Key> ModuleBindingOrderQuery::decodeKey(
    zc::ArrayPtr<const uint8_t> bytes) {
  return PackageRootSetQueryKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> ModuleBindingOrderQuery::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<ModuleBindingOrderQuery::Value> ModuleBindingOrderQuery::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return ModuleBindingOrder::decodeCanonical(bytes);
}

query::TypedQueryResult<ModuleBindingOrderQuery::Value> ModuleBindingOrderQuery::provide(
    query::QueryContext& context, const Key& key) {
  auto activeCratesResult = context.get<ActiveCratesInput>(key);
  if (activeCratesResult.isRuntimeFailure()) {
    return query::TypedQueryResult<Value>::runtimeFailure(activeCratesResult.runtimeFailure());
  }
  if (activeCratesResult.kind() != query::QueryValueKind::Value) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  const auto& activeCrates = activeCratesResult.value();
  auto moduleSetResults = context.getParallel<ActiveModulesInput>(activeCrates.crates());
  if (moduleSetResults.size() != activeCrates.crates().size()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  size_t moduleCount = 0;
  for (const auto& result : moduleSetResults) {
    if (result.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(result.runtimeFailure());
    }
    if (result.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
    moduleCount += result.value().modules().size();
  }
  zc::Vector<StableModuleQueryKey> activeModuleValues(moduleCount);
  for (size_t crateIndex = 0; crateIndex < activeCrates.crates().size(); ++crateIndex) {
    for (const auto& module : moduleSetResults[crateIndex].value().modules()) {
      if (!moduleBelongsToCrate(module, activeCrates.crates()[crateIndex])) {
        return query::TypedQueryResult<Value>::runtimeFailure(
            query::QueryRuntimeFailure::ProviderRejected);
      }
      activeModuleValues.add(module.clone());
    }
  }
  auto canonicalActive = CanonicalModuleSet::from(zc::mv(activeModuleValues));
  if (canonicalActive == zc::none ||
      ZC_ASSERT_NONNULL(canonicalActive).modules().size() != moduleCount) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  const auto& active = ZC_ASSERT_NONNULL(canonicalActive);
  auto dependencyResults = context.getParallel<ModuleDependenciesInput>(active.modules());
  if (dependencyResults.size() != active.modules().size()) {
    return query::TypedQueryResult<Value>::runtimeFailure(
        query::QueryRuntimeFailure::ProviderRejected);
  }
  for (const auto& result : dependencyResults) {
    if (result.isRuntimeFailure()) {
      return query::TypedQueryResult<Value>::runtimeFailure(result.runtimeFailure());
    }
    if (result.kind() != query::QueryValueKind::Value) {
      return query::TypedQueryResult<Value>::runtimeFailure(
          query::QueryRuntimeFailure::ProviderRejected);
    }
  }
  zc::Vector<TopologyNode> topology(active.modules().size());
  for (size_t index = 0; index < active.modules().size(); ++index) {
    topology.add(
        TopologyNode{active.modules()[index].clone(), dependencyResults[index].value().clone()});
  }
  ZC_IF_SOME(failure, providerStructuralFailure(topology.asPtr(), active)) {
    return query::TypedQueryResult<Value>::semanticFailure(failure.encode());
  }
  return providerOrder(topology.asPtr());
}

bool ModuleBindingOrderQuery::verify(query::QueryContext& context, const Key& key,
                                     const query::TypedQueryResult<Value>& result) {
  auto activeCratesResult = context.get<ActiveCratesInput>(key);
  if (activeCratesResult.isRuntimeFailure() ||
      activeCratesResult.kind() != query::QueryValueKind::Value) {
    return false;
  }
  const auto& activeCrates = activeCratesResult.value();
  auto moduleSetResults = context.getParallel<ActiveModulesInput>(activeCrates.crates());
  if (moduleSetResults.size() != activeCrates.crates().size()) { return false; }
  size_t moduleCount = 0;
  zc::Vector<StableModuleQueryKey> activeModuleValues;
  for (size_t crateIndex = 0; crateIndex < activeCrates.crates().size(); ++crateIndex) {
    const auto& moduleSet = moduleSetResults[crateIndex];
    if (moduleSet.isRuntimeFailure() || moduleSet.kind() != query::QueryValueKind::Value) {
      return false;
    }
    moduleCount += moduleSet.value().modules().size();
    for (const auto& module : moduleSet.value().modules()) {
      if (!moduleBelongsToCrate(module, activeCrates.crates()[crateIndex])) { return false; }
      for (const auto& prior : activeModuleValues) {
        if (prior == module) { return false; }
      }
      activeModuleValues.add(module.clone());
    }
  }
  if (activeModuleValues.size() != moduleCount) { return false; }
  auto canonicalActive = CanonicalModuleSet::from(zc::mv(activeModuleValues));
  if (canonicalActive == zc::none) { return false; }
  const auto& active = ZC_ASSERT_NONNULL(canonicalActive);
  auto dependencyResults = context.getParallel<ModuleDependenciesInput>(active.modules());
  if (dependencyResults.size() != active.modules().size()) { return false; }
  for (const auto& result : dependencyResults) {
    if (result.isRuntimeFailure() || result.kind() != query::QueryValueKind::Value) {
      return false;
    }
  }
  zc::Vector<TopologyNode> topology(active.modules().size());
  for (size_t index = 0; index < active.modules().size(); ++index) {
    topology.add(
        TopologyNode{active.modules()[index].clone(), dependencyResults[index].value().clone()});
  }

  auto expectedFailure = verifierExpectedFailure(topology.asPtr());
  if (expectedFailure != zc::none) {
    if (result.kind() != query::QueryValueKind::SemanticFailure) { return false; }
    auto retainedFailure = ModuleBindingOrderFailure::decode(result.semanticFailureBytes());
    if (retainedFailure == zc::none) { return false; }
    ZC_IF_SOME(expected, expectedFailure) {
      ZC_IF_SOME(retained, retainedFailure) { return expected.sameAs(retained); }
    }
    return false;
  }
  return result.kind() == query::QueryValueKind::Value &&
         verifierAcceptsOrder(topology.asPtr(), result.value());
}

bool registerIncrementalBindingQueryAdapter(query::QueryDatabase& database) {
  if (!registerIncrementalPackageGraphQueryInput(database)) { return false; }
  if (!incremental_module_resolution_query::registerIncrementalModuleResolutionQueries(database)) {
    return false;
  }
  if (database.registerInputKind<ActiveModulesInput>() == zc::none) { return false; }
  if (database.registerInputKind<ActiveSourcesInput>() == zc::none) { return false; }
  if (!identity::source_query::registerSourceQueryInputs(database)) { return false; }
  if (!parser::registerParseSourceQuery(database)) { return false; }
  if (database.registerInputKind<ActiveCratesInput>() == zc::none) { return false; }
  if (database.registerInputKind<ModuleDependenciesInput>() == zc::none) { return false; }
  if (database.registerInputKind<SelectedModuleSourceInput>() == zc::none) { return false; }
  if (!registerActiveDefinitionAuthorityInputs(database)) { return false; }
  if (database.registerDerivedKind<NamedDefinitionInventoryQuery>() == zc::none) { return false; }
  if (database.registerDerivedKind<NamedImplementationInventoryQuery>() == zc::none) {
    return false;
  }
  if (database.registerDerivedKind<RevisionLocalDefinitionSitesQuery>() == zc::none) {
    return false;
  }
  if (database.registerDerivedKind<RevisionLocalImplementationSitesQuery>() == zc::none) {
    return false;
  }
  if (database.registerDerivedKind<NamedItemSyntaxQuery>() == zc::none) { return false; }
  if (database.registerDerivedKind<NamedItemProvenanceQuery>() == zc::none) { return false; }
  if (database.registerDerivedKind<ModuleBodySyntaxQuery>() == zc::none) { return false; }
  if (database.registerDerivedKind<ModuleBodyProvenanceQuery>() == zc::none) { return false; }
  if (database.registerDerivedKind<ModuleBodyOwnersQuery>() == zc::none) { return false; }
  if (database.registerDerivedKind<OwnerBodySyntaxQuery>() == zc::none) { return false; }
  if (database.registerDerivedKind<OwnerBodyProvenanceQuery>() == zc::none) { return false; }
  return database.registerDerivedKind<ModuleBindingOrderQuery>() != zc::none;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
