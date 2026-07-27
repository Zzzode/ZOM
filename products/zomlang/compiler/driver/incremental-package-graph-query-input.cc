// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"

#include "zomlang/compiler/identity/canonical-decoder.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

constexpr uint64_t kMaximumPackageOrCrateKeyBytes = 2 * 1024 * 1024;
constexpr uint64_t kMaximumPackageEdgeBytes = 4 * 1024 * 1024 + 4096;
constexpr uint64_t kMaximumCrateEdgeBytes = 8 * 1024 * 1024 + 4096;
constexpr uint64_t kMaximumResolvedPackages = 65536;
constexpr uint64_t kMaximumResolvedPackageEdges = 262144;
constexpr uint64_t kMaximumSelectedPackageEdges = 262144;
constexpr uint64_t kMaximumCrates = 4096;
constexpr uint64_t kMaximumCrateEdges = 262144;
constexpr uint64_t kMaximumEncodedGraphBytes = 256 * 1024 * 1024;

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

template <typename Record>
zc::Vector<Record> canonicalMergeSort(zc::Vector<Record>&& input) {
  if (input.size() < 2) { return zc::mv(input); }
  const size_t middle = input.size() / 2;
  zc::Vector<Record> left(middle);
  zc::Vector<Record> right(input.size() - middle);
  for (size_t index = 0; index < input.size(); ++index) {
    if (index < middle) {
      left.add(zc::mv(input[index]));
    } else {
      right.add(zc::mv(input[index]));
    }
  }
  left = canonicalMergeSort(zc::mv(left));
  right = canonicalMergeSort(zc::mv(right));
  zc::Vector<Record> result(input.size());
  size_t leftIndex = 0;
  size_t rightIndex = 0;
  while (leftIndex < left.size() || rightIndex < right.size()) {
    const bool takeLeft = rightIndex == right.size() ||
                          (leftIndex < left.size() && left[leftIndex] < right[rightIndex]);
    if (takeLeft) {
      result.add(zc::mv(left[leftIndex++]));
    } else {
      result.add(zc::mv(right[rightIndex++]));
    }
  }
  return result;
}

template <typename Record>
void canonicalSort(zc::Vector<Record>& records) {
  records = canonicalMergeSort(zc::mv(records));
}

template <typename Record>
bool hasDuplicate(const zc::Vector<Record>& records) {
  for (size_t index = 1; index < records.size(); ++index) {
    if (records[index - 1] == records[index]) { return true; }
  }
  return false;
}

bool containsPackage(zc::ArrayPtr<const StablePackageQueryKey> packages,
                     const identity::PackageKey& package) {
  const auto encoded = package.encode();
  size_t lower = 0;
  size_t upper = packages.size();
  while (lower < upper) {
    const size_t middle = lower + ((upper - lower) / 2);
    if (compareBytes(packages[middle].canonicalPackageBytes(), encoded.asPtr()) < 0) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  return lower < packages.size() && packages[lower].canonicalPackageBytes() == encoded.asPtr();
}

bool containsCrate(zc::ArrayPtr<const StableCrateQueryKey> crates,
                   const identity::CrateKey& crate) {
  const auto encoded = crate.encode();
  size_t lower = 0;
  size_t upper = crates.size();
  while (lower < upper) {
    const size_t middle = lower + ((upper - lower) / 2);
    if (compareBytes(crates[middle].canonicalCrateBytes(), encoded.asPtr()) < 0) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  return lower < crates.size() && crates[lower].canonicalCrateBytes() == encoded.asPtr();
}

bool containsPackageEdge(zc::ArrayPtr<const StablePackageDependencyQueryKey> edges,
                         const identity::PackageDependencyEdgeKey& edge) {
  const auto encoded = edge.encode();
  size_t lower = 0;
  size_t upper = edges.size();
  while (lower < upper) {
    const size_t middle = lower + ((upper - lower) / 2);
    if (compareBytes(edges[middle].canonicalEdgeBytes(), encoded.asPtr()) < 0) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  return lower < edges.size() && edges[lower].canonicalEdgeBytes() == encoded.asPtr();
}

zc::Maybe<identity::PackageDependencyEdgeKey> decodePackageEdge(
    const StablePackageDependencyQueryKey& edge) {
  identity::CanonicalDecoder decoder(edge.canonicalEdgeBytes());
  auto decoded = identity::PackageDependencyEdgeKey::decodeCanonical(decoder);
  if (decoded == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(decoded);
}

zc::Maybe<identity::CrateDependencyEdgeKey> decodeCrateEdge(
    const StableCrateDependencyQueryKey& edge) {
  identity::CanonicalDecoder decoder(edge.canonicalEdgeBytes());
  auto decoded = identity::CrateDependencyEdgeKey::decodeCanonical(decoder);
  if (decoded == zc::none || !decoder.finished()) { return zc::none; }
  return zc::mv(decoded);
}

zc::Maybe<size_t> crateIndex(zc::ArrayPtr<const StableCrateQueryKey> crates,
                             const identity::CrateKey& crate) {
  const auto encoded = crate.encode();
  size_t lower = 0;
  size_t upper = crates.size();
  while (lower < upper) {
    const size_t middle = lower + ((upper - lower) / 2);
    if (compareBytes(crates[middle].canonicalCrateBytes(), encoded.asPtr()) < 0) {
      lower = middle + 1;
    } else {
      upper = middle;
    }
  }
  if (lower < crates.size() && crates[lower].canonicalCrateBytes() == encoded.asPtr()) {
    return lower;
  }
  return zc::none;
}

bool validateGraphClosure(zc::ArrayPtr<const StablePackageQueryKey> resolvedPackages,
                          zc::ArrayPtr<const StablePackageDependencyQueryKey> resolvedEdges,
                          zc::ArrayPtr<const StablePackageDependencyQueryKey> selectedEdges,
                          zc::ArrayPtr<const StableCrateQueryKey> crates,
                          zc::ArrayPtr<const StableCrateDependencyQueryKey> crateEdges) {
  if (resolvedPackages.size() == 0 || crates.size() == 0) { return false; }

  for (const auto& encoded : resolvedEdges) {
    auto edge = decodePackageEdge(encoded);
    if (edge == zc::none) { return false; }
    ZC_IF_SOME(value, edge) {
      if (!containsPackage(resolvedPackages, value.consumer()) ||
          !containsPackage(resolvedPackages, value.provider())) {
        return false;
      }
    }
  }
  for (const auto& encoded : selectedEdges) {
    auto edge = decodePackageEdge(encoded);
    if (edge == zc::none) { return false; }
    ZC_IF_SOME(value, edge) {
      if (!containsPackageEdge(resolvedEdges, value)) { return false; }
    }
  }
  for (const auto& encoded : crates) {
    identity::CanonicalDecoder decoder(encoded.canonicalCrateBytes());
    auto crate = identity::CrateKey::decodeCanonical(decoder);
    if (crate == zc::none || !decoder.finished()) { return false; }
    ZC_IF_SOME(value, crate) {
      if (value.unit().kind() != identity::CompilationUnitKind::UserPackage ||
          !containsPackage(resolvedPackages, value.unit().userPackage())) {
        return false;
      }
    }
  }

  zc::Vector<size_t> consumers(crateEdges.size());
  zc::Vector<size_t> providers(crateEdges.size());
  zc::Vector<StablePackageDependencyQueryKey> projectedPackageEdges(crateEdges.size());
  for (const auto& encoded : crateEdges) {
    auto edge = decodeCrateEdge(encoded);
    if (edge == zc::none) { return false; }
    ZC_IF_SOME(value, edge) {
      if (value.origin().kind() != identity::CrateDependencyOriginKind::UserPackage ||
          !containsPackageEdge(selectedEdges, value.origin().userPackageEdge()) ||
          !containsCrate(crates, value.consumer()) || !containsCrate(crates, value.provider())) {
        return false;
      }
      auto consumer = crateIndex(crates, value.consumer());
      auto provider = crateIndex(crates, value.provider());
      if (consumer == zc::none || provider == zc::none ||
          ZC_ASSERT_NONNULL(consumer) == ZC_ASSERT_NONNULL(provider)) {
        return false;
      }
      consumers.add(ZC_ASSERT_NONNULL(consumer));
      providers.add(ZC_ASSERT_NONNULL(provider));
      auto projected =
          StablePackageDependencyQueryKey::fromVerified(value.origin().userPackageEdge());
      if (projected == zc::none) { return false; }
      ZC_IF_SOME(projectedValue, projected) { projectedPackageEdges.add(zc::mv(projectedValue)); }
    }
  }
  canonicalSort(projectedPackageEdges);
  size_t selectedIndex = 0;
  for (size_t index = 0; index < projectedPackageEdges.size(); ++index) {
    if (index != 0 && projectedPackageEdges[index] == projectedPackageEdges[index - 1]) {
      continue;
    }
    if (selectedIndex >= selectedEdges.size() ||
        projectedPackageEdges[index] != selectedEdges[selectedIndex]) {
      return false;
    }
    ++selectedIndex;
  }
  if (selectedIndex != selectedEdges.size()) { return false; }

  zc::Vector<uint64_t> remainingDependencies;
  zc::Vector<zc::Vector<size_t>> dependents;
  remainingDependencies.resize(crates.size());
  dependents.resize(crates.size());
  for (size_t index = 0; index < crates.size(); ++index) { remainingDependencies[index] = 0; }
  for (size_t edgeIndex = 0; edgeIndex < consumers.size(); ++edgeIndex) {
    ++remainingDependencies[consumers[edgeIndex]];
    dependents[providers[edgeIndex]].add(consumers[edgeIndex]);
  }
  zc::Vector<size_t> ready;
  for (size_t index = 0; index < crates.size(); ++index) {
    if (remainingDependencies[index] == 0) { ready.add(index); }
  }
  size_t emitted = 0;
  for (size_t cursor = 0; cursor < ready.size(); ++cursor) {
    ++emitted;
    for (const auto dependent : dependents[ready[cursor]]) {
      if (remainingDependencies[dependent] == 0) { return false; }
      --remainingDependencies[dependent];
      if (remainingDependencies[dependent] == 0) { ready.add(dependent); }
    }
  }
  return emitted == crates.size();
}

template <typename Record>
bool sameRecords(zc::ArrayPtr<const Record> left, zc::ArrayPtr<const Record> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

}  // namespace

StablePackageDependencyQueryKey::StablePackageDependencyQueryKey(
    zc::Array<uint8_t>&& canonicalEdgeBytes) noexcept
    : canonicalEdgeBytesField(zc::mv(canonicalEdgeBytes)) {}

zc::Maybe<StablePackageDependencyQueryKey> StablePackageDependencyQueryKey::fromVerified(
    const identity::PackageDependencyEdgeKey& edge) {
  return decodeBounded(edge.encode().asPtr());
}

zc::Maybe<StablePackageDependencyQueryKey> StablePackageDependencyQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumPackageEdgeBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto edge = identity::PackageDependencyEdgeKey::decodeCanonical(decoder);
  if (edge == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, edge) {
    if (value.encode().asPtr() != bytes) { return zc::none; }
  }
  return StablePackageDependencyQueryKey(zc::heapArray<uint8_t>(bytes));
}

StablePackageDependencyQueryKey StablePackageDependencyQueryKey::clone() const {
  return StablePackageDependencyQueryKey(zc::heapArray<uint8_t>(canonicalEdgeBytes()));
}

zc::ArrayPtr<const uint8_t> StablePackageDependencyQueryKey::canonicalEdgeBytes() const {
  return canonicalEdgeBytesField.asPtr();
}

bool StablePackageDependencyQueryKey::operator==(
    const StablePackageDependencyQueryKey& other) const noexcept {
  return canonicalEdgeBytes() == other.canonicalEdgeBytes();
}

bool StablePackageDependencyQueryKey::operator<(
    const StablePackageDependencyQueryKey& other) const noexcept {
  return compareBytes(canonicalEdgeBytes(), other.canonicalEdgeBytes()) < 0;
}

StableCrateDependencyQueryKey::StableCrateDependencyQueryKey(
    zc::Array<uint8_t>&& canonicalEdgeBytes) noexcept
    : canonicalEdgeBytesField(zc::mv(canonicalEdgeBytes)) {}

zc::Maybe<StableCrateDependencyQueryKey> StableCrateDependencyQueryKey::fromVerified(
    const identity::CrateDependencyEdgeKey& edge) {
  return decodeBounded(edge.encode().asPtr());
}

zc::Maybe<StableCrateDependencyQueryKey> StableCrateDependencyQueryKey::decodeBounded(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumCrateEdgeBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto edge = identity::CrateDependencyEdgeKey::decodeCanonical(decoder);
  if (edge == zc::none || !decoder.finished()) { return zc::none; }
  ZC_IF_SOME(value, edge) {
    if (value.encode().asPtr() != bytes) { return zc::none; }
  }
  return StableCrateDependencyQueryKey(zc::heapArray<uint8_t>(bytes));
}

StableCrateDependencyQueryKey StableCrateDependencyQueryKey::clone() const {
  return StableCrateDependencyQueryKey(zc::heapArray<uint8_t>(canonicalEdgeBytes()));
}

zc::ArrayPtr<const uint8_t> StableCrateDependencyQueryKey::canonicalEdgeBytes() const {
  return canonicalEdgeBytesField.asPtr();
}

bool StableCrateDependencyQueryKey::operator==(
    const StableCrateDependencyQueryKey& other) const noexcept {
  return canonicalEdgeBytes() == other.canonicalEdgeBytes();
}

bool StableCrateDependencyQueryKey::operator<(
    const StableCrateDependencyQueryKey& other) const noexcept {
  return compareBytes(canonicalEdgeBytes(), other.canonicalEdgeBytes()) < 0;
}

CanonicalPackageGraph::CanonicalPackageGraph(
    zc::Vector<StablePackageQueryKey>&& resolvedPackages,
    zc::Vector<StablePackageDependencyQueryKey>&& resolvedPackageEdges,
    zc::Vector<StablePackageDependencyQueryKey>&& selectedPackageEdges,
    zc::Vector<StableCrateQueryKey>&& crates,
    zc::Vector<StableCrateDependencyQueryKey>&& crateEdges) noexcept
    : resolvedPackageFields(zc::mv(resolvedPackages)),
      resolvedPackageEdgeFields(zc::mv(resolvedPackageEdges)),
      selectedPackageEdgeFields(zc::mv(selectedPackageEdges)),
      crateFields(zc::mv(crates)),
      crateEdgeFields(zc::mv(crateEdges)) {}

zc::Maybe<CanonicalPackageGraph> CanonicalPackageGraph::fromVerified(
    const package::ResolutionOutput& resolution, const VerifiedCrateGraph& crateGraph) {
  zc::Vector<StablePackageQueryKey> packages(resolution.packages().size());
  for (const auto& record : resolution.packages()) {
    auto package = StablePackageQueryKey::fromVerified(record.key());
    if (package == zc::none) { return zc::none; }
    ZC_IF_SOME(value, package) { packages.add(zc::mv(value)); }
  }
  zc::Vector<StablePackageDependencyQueryKey> resolvedEdges(resolution.edges().size());
  for (const auto& edge : resolution.edges()) {
    auto projected = StablePackageDependencyQueryKey::fromVerified(edge);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, projected) { resolvedEdges.add(zc::mv(value)); }
  }
  zc::Vector<StablePackageDependencyQueryKey> selectedEdges(crateGraph.packageEdges().size());
  for (const auto& edge : crateGraph.packageEdges()) {
    auto projected = StablePackageDependencyQueryKey::fromVerified(edge);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, projected) { selectedEdges.add(zc::mv(value)); }
  }
  zc::Vector<StableCrateQueryKey> crates(crateGraph.crates().size());
  for (const auto& crate : crateGraph.crates()) {
    auto projected = StableCrateQueryKey::fromVerified(crate);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, projected) { crates.add(zc::mv(value)); }
  }
  zc::Vector<StableCrateDependencyQueryKey> crateEdges(crateGraph.edges().size());
  for (const auto& edge : crateGraph.edges()) {
    auto projected = StableCrateDependencyQueryKey::fromVerified(edge);
    if (projected == zc::none) { return zc::none; }
    ZC_IF_SOME(value, projected) { crateEdges.add(zc::mv(value)); }
  }
  return fromCanonical(zc::mv(packages), zc::mv(resolvedEdges), zc::mv(selectedEdges),
                       zc::mv(crates), zc::mv(crateEdges));
}

zc::Maybe<CanonicalPackageGraph> CanonicalPackageGraph::fromCanonical(
    zc::Vector<StablePackageQueryKey>&& resolvedPackages,
    zc::Vector<StablePackageDependencyQueryKey>&& resolvedPackageEdges,
    zc::Vector<StablePackageDependencyQueryKey>&& selectedPackageEdges,
    zc::Vector<StableCrateQueryKey>&& crates,
    zc::Vector<StableCrateDependencyQueryKey>&& crateEdges) {
  if (resolvedPackages.size() == 0 || resolvedPackages.size() > kMaximumResolvedPackages ||
      resolvedPackageEdges.size() > kMaximumResolvedPackageEdges ||
      selectedPackageEdges.size() > kMaximumSelectedPackageEdges || crates.size() == 0 ||
      crates.size() > kMaximumCrates || crateEdges.size() > kMaximumCrateEdges) {
    return zc::none;
  }
  canonicalSort(resolvedPackages);
  canonicalSort(resolvedPackageEdges);
  canonicalSort(selectedPackageEdges);
  canonicalSort(crates);
  canonicalSort(crateEdges);
  if (hasDuplicate(resolvedPackages) || hasDuplicate(resolvedPackageEdges) ||
      hasDuplicate(selectedPackageEdges) || hasDuplicate(crates) || hasDuplicate(crateEdges) ||
      !validateGraphClosure(resolvedPackages.asPtr(), resolvedPackageEdges.asPtr(),
                            selectedPackageEdges.asPtr(), crates.asPtr(), crateEdges.asPtr())) {
    return zc::none;
  }
  CanonicalPackageGraph result(zc::mv(resolvedPackages), zc::mv(resolvedPackageEdges),
                               zc::mv(selectedPackageEdges), zc::mv(crates), zc::mv(crateEdges));
  if (result.encodeCanonical().size() > kMaximumEncodedGraphBytes) { return zc::none; }
  return zc::mv(result);
}

CanonicalPackageGraph CanonicalPackageGraph::clone() const {
  zc::Vector<StablePackageQueryKey> packages(resolvedPackageFields.size());
  for (const auto& value : resolvedPackageFields) { packages.add(value.clone()); }
  zc::Vector<StablePackageDependencyQueryKey> resolvedEdges(resolvedPackageEdgeFields.size());
  for (const auto& value : resolvedPackageEdgeFields) { resolvedEdges.add(value.clone()); }
  zc::Vector<StablePackageDependencyQueryKey> selectedEdges(selectedPackageEdgeFields.size());
  for (const auto& value : selectedPackageEdgeFields) { selectedEdges.add(value.clone()); }
  zc::Vector<StableCrateQueryKey> crates(crateFields.size());
  for (const auto& value : crateFields) { crates.add(value.clone()); }
  zc::Vector<StableCrateDependencyQueryKey> crateEdges(crateEdgeFields.size());
  for (const auto& value : crateEdgeFields) { crateEdges.add(value.clone()); }
  return CanonicalPackageGraph(zc::mv(packages), zc::mv(resolvedEdges), zc::mv(selectedEdges),
                               zc::mv(crates), zc::mv(crateEdges));
}

zc::ArrayPtr<const StablePackageQueryKey> CanonicalPackageGraph::resolvedPackages() const {
  return resolvedPackageFields.asPtr();
}

zc::ArrayPtr<const StablePackageDependencyQueryKey> CanonicalPackageGraph::resolvedPackageEdges()
    const {
  return resolvedPackageEdgeFields.asPtr();
}

zc::ArrayPtr<const StablePackageDependencyQueryKey> CanonicalPackageGraph::selectedPackageEdges()
    const {
  return selectedPackageEdgeFields.asPtr();
}

zc::ArrayPtr<const StableCrateQueryKey> CanonicalPackageGraph::crates() const {
  return crateFields.asPtr();
}

zc::ArrayPtr<const StableCrateDependencyQueryKey> CanonicalPackageGraph::crateEdges() const {
  return crateEdgeFields.asPtr();
}

bool CanonicalPackageGraph::operator==(const CanonicalPackageGraph& other) const noexcept {
  return sameRecords(resolvedPackages(), other.resolvedPackages()) &&
         sameRecords(resolvedPackageEdges(), other.resolvedPackageEdges()) &&
         sameRecords(selectedPackageEdges(), other.selectedPackageEdges()) &&
         sameRecords(crates(), other.crates()) && sameRecords(crateEdges(), other.crateEdges());
}

zc::Array<uint8_t> CanonicalPackageGraph::encodeCanonical() const {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(resolvedPackageFields.size());
  for (const auto& value : resolvedPackageFields) {
    encoder.encodeByteString(value.canonicalPackageBytes());
  }
  encoder.encodeSequenceSize(resolvedPackageEdgeFields.size());
  for (const auto& value : resolvedPackageEdgeFields) {
    encoder.encodeByteString(value.canonicalEdgeBytes());
  }
  encoder.encodeSequenceSize(selectedPackageEdgeFields.size());
  for (const auto& value : selectedPackageEdgeFields) {
    encoder.encodeByteString(value.canonicalEdgeBytes());
  }
  encoder.encodeSequenceSize(crateFields.size());
  for (const auto& value : crateFields) { encoder.encodeByteString(value.canonicalCrateBytes()); }
  encoder.encodeSequenceSize(crateEdgeFields.size());
  for (const auto& value : crateEdgeFields) {
    encoder.encodeByteString(value.canonicalEdgeBytes());
  }
  return encoder.finish();
}

zc::Maybe<CanonicalPackageGraph> CanonicalPackageGraph::decodeCanonical(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.size() > kMaximumEncodedGraphBytes) { return zc::none; }
  identity::CanonicalDecoder decoder(bytes);
  auto packageCount = decoder.decodeSequenceSize(kMaximumResolvedPackages);
  if (packageCount == zc::none || ZC_ASSERT_NONNULL(packageCount) == 0) { return zc::none; }
  zc::Vector<StablePackageQueryKey> packages(static_cast<size_t>(ZC_ASSERT_NONNULL(packageCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(packageCount); ++index) {
    auto bytesValue = decoder.decodeByteString(kMaximumPackageOrCrateKeyBytes);
    if (bytesValue == zc::none) { return zc::none; }
    ZC_IF_SOME(value, bytesValue) {
      identity::CanonicalDecoder keyDecoder(value.asPtr());
      auto package = identity::PackageKey::decodeCanonical(keyDecoder);
      if (package == zc::none || !keyDecoder.finished()) { return zc::none; }
      ZC_IF_SOME(decoded, package) {
        auto key = StablePackageQueryKey::fromVerified(decoded);
        if (key == zc::none) { return zc::none; }
        ZC_IF_SOME(projected, key) { packages.add(zc::mv(projected)); }
      }
    }
  }
  auto resolvedCount = decoder.decodeSequenceSize(kMaximumResolvedPackageEdges);
  if (resolvedCount == zc::none) { return zc::none; }
  zc::Vector<StablePackageDependencyQueryKey> resolvedEdges(
      static_cast<size_t>(ZC_ASSERT_NONNULL(resolvedCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(resolvedCount); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPackageEdgeBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto edge = StablePackageDependencyQueryKey::decodeBounded(value.asPtr());
      if (edge == zc::none) { return zc::none; }
      ZC_IF_SOME(projected, edge) { resolvedEdges.add(zc::mv(projected)); }
    }
  }
  auto selectedCount = decoder.decodeSequenceSize(kMaximumSelectedPackageEdges);
  if (selectedCount == zc::none) { return zc::none; }
  zc::Vector<StablePackageDependencyQueryKey> selectedEdges(
      static_cast<size_t>(ZC_ASSERT_NONNULL(selectedCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(selectedCount); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPackageEdgeBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto edge = StablePackageDependencyQueryKey::decodeBounded(value.asPtr());
      if (edge == zc::none) { return zc::none; }
      ZC_IF_SOME(projected, edge) { selectedEdges.add(zc::mv(projected)); }
    }
  }
  auto crateCount = decoder.decodeSequenceSize(kMaximumCrates);
  if (crateCount == zc::none || ZC_ASSERT_NONNULL(crateCount) == 0) { return zc::none; }
  zc::Vector<StableCrateQueryKey> crates(static_cast<size_t>(ZC_ASSERT_NONNULL(crateCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(crateCount); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumPackageOrCrateKeyBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      identity::CanonicalDecoder keyDecoder(value.asPtr());
      auto crate = identity::CrateKey::decodeCanonical(keyDecoder);
      if (crate == zc::none || !keyDecoder.finished()) { return zc::none; }
      ZC_IF_SOME(decoded, crate) {
        auto key = StableCrateQueryKey::fromVerified(decoded);
        if (key == zc::none) { return zc::none; }
        ZC_IF_SOME(projected, key) { crates.add(zc::mv(projected)); }
      }
    }
  }
  auto edgeCount = decoder.decodeSequenceSize(kMaximumCrateEdges);
  if (edgeCount == zc::none) { return zc::none; }
  zc::Vector<StableCrateDependencyQueryKey> crateEdges(
      static_cast<size_t>(ZC_ASSERT_NONNULL(edgeCount)));
  for (uint64_t index = 0; index < ZC_ASSERT_NONNULL(edgeCount); ++index) {
    auto encoded = decoder.decodeByteString(kMaximumCrateEdgeBytes);
    if (encoded == zc::none) { return zc::none; }
    ZC_IF_SOME(value, encoded) {
      auto edge = StableCrateDependencyQueryKey::decodeBounded(value.asPtr());
      if (edge == zc::none) { return zc::none; }
      ZC_IF_SOME(projected, edge) { crateEdges.add(zc::mv(projected)); }
    }
  }
  if (!decoder.finished()) { return zc::none; }

  for (size_t index = 1; index < packages.size(); ++index) {
    if (!(packages[index - 1] < packages[index])) { return zc::none; }
  }
  for (size_t index = 1; index < resolvedEdges.size(); ++index) {
    if (!(resolvedEdges[index - 1] < resolvedEdges[index])) { return zc::none; }
  }
  for (size_t index = 1; index < selectedEdges.size(); ++index) {
    if (!(selectedEdges[index - 1] < selectedEdges[index])) { return zc::none; }
  }
  for (size_t index = 1; index < crates.size(); ++index) {
    if (!(crates[index - 1] < crates[index])) { return zc::none; }
  }
  for (size_t index = 1; index < crateEdges.size(); ++index) {
    if (!(crateEdges[index - 1] < crateEdges[index])) { return zc::none; }
  }
  if (!validateGraphClosure(packages.asPtr(), resolvedEdges.asPtr(), selectedEdges.asPtr(),
                            crates.asPtr(), crateEdges.asPtr())) {
    return zc::none;
  }
  return CanonicalPackageGraph(zc::mv(packages), zc::mv(resolvedEdges), zc::mv(selectedEdges),
                               zc::mv(crates), zc::mv(crateEdges));
}

zc::StringPtr PackageGraphInput::domain() { return "zom.query.package-graph"_zc; }

query::QueryKindContract PackageGraphInput::contract() {
  auto contract = query::QueryKindContract::input(domain(), query::Durability::Medium);
  return zc::mv(ZC_REQUIRE_NONNULL(contract));
}

zc::Array<uint8_t> PackageGraphInput::encodeKey(const Key& key) { return key.encodeCanonical(); }

zc::Maybe<PackageGraphInput::Key> PackageGraphInput::decodeKey(zc::ArrayPtr<const uint8_t> bytes) {
  return PackageRootSetKey::decodeCanonical(bytes);
}

zc::Array<uint8_t> PackageGraphInput::encodeValue(const Value& value) {
  return value.encodeCanonical();
}

zc::Maybe<PackageGraphInput::Value> PackageGraphInput::decodeValue(
    zc::ArrayPtr<const uint8_t> bytes) {
  return CanonicalPackageGraph::decodeCanonical(bytes);
}

bool registerIncrementalPackageGraphQueryInput(query::QueryDatabase& database) {
  return database.registerInputKind<PackageGraphInput>() != zc::none;
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
