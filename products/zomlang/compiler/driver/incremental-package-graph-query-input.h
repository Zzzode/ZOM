// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/crate-graph.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/package/package-resolver.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Bounded canonical resolved package dependency edge input record.
class StablePackageDependencyQueryKey final {
public:
  StablePackageDependencyQueryKey(StablePackageDependencyQueryKey&&) noexcept = default;
  StablePackageDependencyQueryKey& operator=(StablePackageDependencyQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StablePackageDependencyQueryKey);

  ZC_NODISCARD static zc::Maybe<StablePackageDependencyQueryKey> fromVerified(
      const identity::PackageDependencyEdgeKey& edge);
  ZC_NODISCARD StablePackageDependencyQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalEdgeBytes() const ZC_LIFETIMEBOUND;
  bool operator==(const StablePackageDependencyQueryKey& other) const noexcept;
  bool operator!=(const StablePackageDependencyQueryKey& other) const noexcept {
    return !(*this == other);
  }
  bool operator<(const StablePackageDependencyQueryKey& other) const noexcept;

private:
  explicit StablePackageDependencyQueryKey(zc::Array<uint8_t>&& canonicalEdgeBytes) noexcept;
  ZC_NODISCARD static zc::Maybe<StablePackageDependencyQueryKey> decodeBounded(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Array<uint8_t> canonicalEdgeBytesField;

  friend class CanonicalPackageGraph;
};

/// \brief Bounded canonical crate dependency edge input record.
class StableCrateDependencyQueryKey final {
public:
  StableCrateDependencyQueryKey(StableCrateDependencyQueryKey&&) noexcept = default;
  StableCrateDependencyQueryKey& operator=(StableCrateDependencyQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableCrateDependencyQueryKey);

  ZC_NODISCARD static zc::Maybe<StableCrateDependencyQueryKey> fromVerified(
      const identity::CrateDependencyEdgeKey& edge);
  ZC_NODISCARD StableCrateDependencyQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalEdgeBytes() const ZC_LIFETIMEBOUND;
  bool operator==(const StableCrateDependencyQueryKey& other) const noexcept;
  bool operator!=(const StableCrateDependencyQueryKey& other) const noexcept {
    return !(*this == other);
  }
  bool operator<(const StableCrateDependencyQueryKey& other) const noexcept;

private:
  explicit StableCrateDependencyQueryKey(zc::Array<uint8_t>&& canonicalEdgeBytes) noexcept;
  ZC_NODISCARD static zc::Maybe<StableCrateDependencyQueryKey> decodeBounded(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Array<uint8_t> canonicalEdgeBytesField;

  friend class CanonicalPackageGraph;
};

/// \brief Exact closed package-resolution graph and final crate-graph projection.
class CanonicalPackageGraph final {
public:
  CanonicalPackageGraph(CanonicalPackageGraph&&) noexcept = default;
  CanonicalPackageGraph& operator=(CanonicalPackageGraph&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalPackageGraph);

  ZC_NODISCARD static zc::Maybe<CanonicalPackageGraph> fromVerified(
      const package::ResolutionOutput& resolution, const VerifiedCrateGraph& crateGraph);
  ZC_NODISCARD CanonicalPackageGraph clone() const;
  ZC_NODISCARD zc::ArrayPtr<const StablePackageQueryKey> resolvedPackages() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const StablePackageDependencyQueryKey> resolvedPackageEdges() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const StablePackageDependencyQueryKey> selectedPackageEdges() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const StableCrateQueryKey> crates() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::ArrayPtr<const StableCrateDependencyQueryKey> crateEdges() const
      ZC_LIFETIMEBOUND;
  bool operator==(const CanonicalPackageGraph& other) const noexcept;
  bool operator!=(const CanonicalPackageGraph& other) const noexcept { return !(*this == other); }

private:
  CanonicalPackageGraph(zc::Vector<StablePackageQueryKey>&& resolvedPackages,
                        zc::Vector<StablePackageDependencyQueryKey>&& resolvedPackageEdges,
                        zc::Vector<StablePackageDependencyQueryKey>&& selectedPackageEdges,
                        zc::Vector<StableCrateQueryKey>&& crates,
                        zc::Vector<StableCrateDependencyQueryKey>&& crateEdges) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalPackageGraph> fromCanonical(
      zc::Vector<StablePackageQueryKey>&& resolvedPackages,
      zc::Vector<StablePackageDependencyQueryKey>&& resolvedPackageEdges,
      zc::Vector<StablePackageDependencyQueryKey>&& selectedPackageEdges,
      zc::Vector<StableCrateQueryKey>&& crates,
      zc::Vector<StableCrateDependencyQueryKey>&& crateEdges);
  ZC_NODISCARD static zc::Maybe<CanonicalPackageGraph> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Vector<StablePackageQueryKey> resolvedPackageFields;
  zc::Vector<StablePackageDependencyQueryKey> resolvedPackageEdgeFields;
  zc::Vector<StablePackageDependencyQueryKey> selectedPackageEdgeFields;
  zc::Vector<StableCrateQueryKey> crateFields;
  zc::Vector<StableCrateDependencyQueryKey> crateEdgeFields;

  friend struct PackageGraphInput;
};

/// \brief Medium-durability exact package and crate graph authority.
struct PackageGraphInput final {
  using Key = PackageRootSetKey;
  using Value = CanonicalPackageGraph;

  static constexpr query::InputDescriptorMetadata descriptor{
      "PackageGraphInput"_zcc, "zom.query.package-graph"_zcc, query::Durability::Medium};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Registers the package-graph input root with the shared query database.
ZC_NODISCARD bool registerIncrementalPackageGraphQueryInput(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::incremental_binding_query
