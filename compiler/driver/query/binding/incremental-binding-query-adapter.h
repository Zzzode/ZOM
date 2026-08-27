// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/one-of.h"
#include "zc/core/vector.h"
#include "compiler/driver/package/package-compilation-request.h"
#include "compiler/identity/source-query-input.h"
#include "compiler/identity/source-snapshot.h"
#include "compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_binding_query {

/// \brief Bounded canonical query identity projected from one verified package key.
class StablePackageQueryKey final {
public:
  StablePackageQueryKey(StablePackageQueryKey&&) noexcept = default;
  StablePackageQueryKey& operator=(StablePackageQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StablePackageQueryKey);

  ZC_NODISCARD static zc::Maybe<StablePackageQueryKey> fromVerified(
      const identity::PackageKey& package);
  ZC_NODISCARD StablePackageQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalPackageBytes() const ZC_LIFETIMEBOUND;
  bool operator==(const StablePackageQueryKey& other) const noexcept;
  bool operator!=(const StablePackageQueryKey& other) const noexcept { return !(*this == other); }
  bool operator<(const StablePackageQueryKey& other) const noexcept;

private:
  explicit StablePackageQueryKey(zc::Array<uint8_t>&& canonicalPackageBytes) noexcept;
  ZC_NODISCARD static zc::Maybe<StablePackageQueryKey> decodeBounded(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Array<uint8_t> canonicalPackageBytesField;

  friend class PackageRootSetKey;
};

/// \brief Canonical non-empty package-root-set query key.
class PackageRootSetKey final {
public:
  PackageRootSetKey(PackageRootSetKey&&) noexcept = default;
  PackageRootSetKey& operator=(PackageRootSetKey&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageRootSetKey);

  ZC_NODISCARD static zc::Maybe<PackageRootSetKey> fromVerified(
      const package::VerifiedPackageCompilationRequest& request);
  ZC_NODISCARD PackageRootSetKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const StablePackageQueryKey> packages() const ZC_LIFETIMEBOUND;
  bool operator==(const PackageRootSetKey& other) const noexcept;
  bool operator!=(const PackageRootSetKey& other) const noexcept { return !(*this == other); }

private:
  explicit PackageRootSetKey(zc::Vector<StablePackageQueryKey>&& packages) noexcept;
  ZC_NODISCARD static zc::Maybe<PackageRootSetKey> from(
      zc::Vector<StablePackageQueryKey>&& packages);
  ZC_NODISCARD static zc::Maybe<PackageRootSetKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Vector<StablePackageQueryKey> packageFields;

  friend struct ActiveCrates;
  friend struct PackageGraphInput;
};

/// \brief Bounded canonical query identity projected from one verified crate key.
class StableCrateQueryKey final {
public:
  StableCrateQueryKey(StableCrateQueryKey&&) noexcept = default;
  StableCrateQueryKey& operator=(StableCrateQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableCrateQueryKey);

  ZC_NODISCARD static zc::Maybe<StableCrateQueryKey> fromVerified(const identity::CrateKey& crate);
  ZC_NODISCARD StableCrateQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalCrateBytes() const ZC_LIFETIMEBOUND;
  bool operator==(const StableCrateQueryKey& other) const noexcept;
  bool operator!=(const StableCrateQueryKey& other) const noexcept { return !(*this == other); }
  bool operator<(const StableCrateQueryKey& other) const noexcept;

private:
  explicit StableCrateQueryKey(zc::Array<uint8_t>&& canonicalCrateBytes) noexcept;
  ZC_NODISCARD static zc::Maybe<StableCrateQueryKey> decodeBounded(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Array<uint8_t> canonicalCrateBytesField;

  friend class CanonicalCrateSet;
  friend class CompilationRootKey;
  friend struct UserPackageActiveSourcesInput;
  friend struct ActiveSources;
};

struct UserPackageCompilationRoot final {
  StablePackageQueryKey package;
};

struct ToolchainCoreCompilationRoot final {
  StableCrateQueryKey crate;
};

enum class CompilationRootKind : uint8_t { UserPackage = 0x01, ToolchainCore = 0x02 };

/// \brief Exhaustive stable root of one user package or projected toolchain core graph.
class CompilationRootKey final {
public:
  CompilationRootKey(CompilationRootKey&&) noexcept = default;
  CompilationRootKey& operator=(CompilationRootKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CompilationRootKey);

  ZC_NODISCARD static zc::Maybe<CompilationRootKey> userPackage(
      const identity::PackageKey& package);
  ZC_NODISCARD static zc::Maybe<CompilationRootKey> toolchainCore(const identity::CrateKey& crate);
  ZC_NODISCARD CompilationRootKey clone() const;
  ZC_NODISCARD CompilationRootKind kind() const noexcept;
  ZC_NODISCARD const StablePackageQueryKey& userPackage() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD const StableCrateQueryKey& toolchainCore() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CompilationRootKey& other) const noexcept;
  bool operator!=(const CompilationRootKey& other) const noexcept { return !(*this == other); }
  bool operator<(const CompilationRootKey& other) const noexcept;

private:
  explicit CompilationRootKey(UserPackageCompilationRoot&& root) noexcept;
  explicit CompilationRootKey(ToolchainCoreCompilationRoot&& root) noexcept;
  ZC_NODISCARD static zc::Maybe<CompilationRootKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::OneOf<UserPackageCompilationRoot, ToolchainCoreCompilationRoot> value;

  friend class CompilationRootSetQueryKey;
};

/// \brief Complete canonical non-empty compilation-root-set query key.
class CompilationRootSetQueryKey final {
public:
  CompilationRootSetQueryKey(CompilationRootSetQueryKey&&) noexcept = default;
  CompilationRootSetQueryKey& operator=(CompilationRootSetQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(CompilationRootSetQueryKey);

  ZC_NODISCARD static zc::Maybe<CompilationRootSetQueryKey> fromVerified(
      const package::VerifiedPackageCompilationRequest& request);
  ZC_NODISCARD static zc::Maybe<CompilationRootSetQueryKey> fromVerified(
      const package::VerifiedPackageCompilationRequest& request,
      zc::ArrayPtr<const identity::CrateKey> projectedCoreCrates);
  ZC_NODISCARD static zc::Maybe<CompilationRootSetQueryKey> from(
      zc::Vector<CompilationRootKey>&& roots);
  ZC_NODISCARD static zc::Maybe<CompilationRootSetQueryKey> singletonToolchainCore(
      const identity::CrateKey& crate);
  ZC_NODISCARD static zc::Maybe<CompilationRootSetQueryKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CompilationRootSetQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const CompilationRootKey> roots() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CompilationRootSetQueryKey& other) const noexcept;
  bool operator!=(const CompilationRootSetQueryKey& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CompilationRootSetQueryKey(zc::Vector<CompilationRootKey>&& roots) noexcept;

  zc::Vector<CompilationRootKey> rootFields;
};

/// \brief Canonically sorted and duplicate-free active replacement crate set.
class CanonicalCrateSet final {
public:
  CanonicalCrateSet(CanonicalCrateSet&&) noexcept = default;
  CanonicalCrateSet& operator=(CanonicalCrateSet&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalCrateSet);

  ZC_NODISCARD static zc::Maybe<CanonicalCrateSet> from(zc::Vector<StableCrateQueryKey>&& crates);
  ZC_NODISCARD CanonicalCrateSet clone() const;
  ZC_NODISCARD zc::ArrayPtr<const StableCrateQueryKey> crates() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD bool contains(const StableCrateQueryKey& crate) const noexcept;
  bool operator==(const CanonicalCrateSet& other) const noexcept;
  bool operator!=(const CanonicalCrateSet& other) const noexcept { return !(*this == other); }

private:
  explicit CanonicalCrateSet(zc::Vector<StableCrateQueryKey>&& crates) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalCrateSet> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Vector<StableCrateQueryKey> crateFields;

  friend struct ActiveCrates;
};

/// \brief Bounded canonical query identity projected from one verified semantic module key.
class StableModuleQueryKey final {
public:
  StableModuleQueryKey(StableModuleQueryKey&&) noexcept = default;
  StableModuleQueryKey& operator=(StableModuleQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(StableModuleQueryKey);

  ZC_NODISCARD static zc::Maybe<StableModuleQueryKey> fromVerified(
      const identity::ModuleKey& module);
  ZC_NODISCARD StableModuleQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalModuleBytes() const ZC_LIFETIMEBOUND;
  bool operator==(const StableModuleQueryKey& other) const noexcept;
  bool operator!=(const StableModuleQueryKey& other) const noexcept { return !(*this == other); }
  bool operator<(const StableModuleQueryKey& other) const noexcept;

private:
  explicit StableModuleQueryKey(zc::Array<uint8_t>&& canonicalModuleBytes) noexcept;
  ZC_NODISCARD static zc::Maybe<StableModuleQueryKey> decodeBounded(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Array<uint8_t> canonicalModuleBytesField;

  friend struct NamedDefinitionInventoryQuery;
  friend struct NamedImplementationInventoryQuery;
  friend struct NamedItemSyntaxQuery;
  friend struct ModuleBodySyntaxQuery;
  friend struct OwnerBodySyntaxQuery;
};

/// \brief Canonically sorted and duplicate-free active source set for one crate.
class CanonicalSourceSet final {
public:
  CanonicalSourceSet(CanonicalSourceSet&&) noexcept = default;
  CanonicalSourceSet& operator=(CanonicalSourceSet&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalSourceSet);

  ZC_NODISCARD static zc::Maybe<CanonicalSourceSet> from(
      zc::Vector<identity::source_query::StableSourceQueryKey>&& sources);
  ZC_NODISCARD CanonicalSourceSet clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::source_query::StableSourceQueryKey> sources() const
      ZC_LIFETIMEBOUND;
  ZC_NODISCARD bool contains(
      const identity::source_query::StableSourceQueryKey& source) const noexcept;
  bool operator==(const CanonicalSourceSet& other) const noexcept;
  bool operator!=(const CanonicalSourceSet& other) const noexcept { return !(*this == other); }

private:
  explicit CanonicalSourceSet(
      zc::Vector<identity::source_query::StableSourceQueryKey>&& sources) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalSourceSet> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Vector<identity::source_query::StableSourceQueryKey> sourceFields;

  friend struct UserPackageActiveSourcesInput;
  friend struct ActiveSources;
};

/// \brief Low-durability explicit active source set for one user-package crate.
struct UserPackageActiveSourcesInput final {
  using Key = StableCrateQueryKey;
  using Value = CanonicalSourceSet;

  static constexpr query::InputDescriptorMetadata descriptor{
      "UserPackageActiveSourcesInput"_zcc, "zom.query.user-package-active-sources"_zcc,
      query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Canonical active sources derived from user-package or toolchain-core authority.
struct ActiveSources final {
  using Key = StableCrateQueryKey;
  using Value = CanonicalSourceSet;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveSources"_zcc,
      "zom.query.active-sources"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,
                                                             const Key& key);
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result);
};

/// \brief Canonical active crates derived from one exhaustive compilation-root set.
struct ActiveCrates final {
  using Key = CompilationRootSetQueryKey;
  using Value = CanonicalCrateSet;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveCrates"_zcc,
      "zom.query.active-crates"_zcc,
      query::ReuseClass::Semantic,
      query::RetentionClass::Retained,
      query::QueryEqualityPolicy::CanonicalBytes,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::TypedQueryResult<Value> provide(query::QueryContext& context,
                                                             const Key& key);
  ZC_NODISCARD static bool verify(query::QueryContext& context, const Key& key,
                                  const query::TypedQueryResult<Value>& result);
};

/// \brief Registers the complete production module-topology query family exactly once.
ZC_NODISCARD bool registerIncrementalBindingQueryAdapter(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::incremental_binding_query
