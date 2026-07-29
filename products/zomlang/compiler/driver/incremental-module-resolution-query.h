// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/module-resolution.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::incremental_module_resolution_query {

/// \brief Exact crate-and-alias key for one explicit dependency-root projection.
class DependencyAliasRootQueryKey final {
public:
  DependencyAliasRootQueryKey(DependencyAliasRootQueryKey&&) noexcept = default;
  DependencyAliasRootQueryKey& operator=(DependencyAliasRootQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(DependencyAliasRootQueryKey);

  ZC_NODISCARD static zc::Maybe<DependencyAliasRootQueryKey> from(
      identity::CrateKey&& crate, identity::DependencyAlias&& alias);
  ZC_NODISCARD DependencyAliasRootQueryKey clone() const;
  ZC_NODISCARD const identity::CrateKey& crate() const noexcept;
  ZC_NODISCARD const identity::DependencyAlias& alias() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  bool operator==(const DependencyAliasRootQueryKey& other) const;
  bool operator!=(const DependencyAliasRootQueryKey& other) const { return !(*this == other); }

private:
  DependencyAliasRootQueryKey(identity::CrateKey&& crate,
                              identity::DependencyAlias&& alias) noexcept;
  ZC_NODISCARD static zc::Maybe<DependencyAliasRootQueryKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  identity::CrateKey crateValue;
  identity::DependencyAlias aliasValue;

  friend struct DependencyAliasRootInput;
};

/// \brief Explicit absent-or-present stable module target.
class ExplicitModuleTarget final {
public:
  ExplicitModuleTarget(ExplicitModuleTarget&&) noexcept = default;
  ExplicitModuleTarget& operator=(ExplicitModuleTarget&&) noexcept = default;
  ZC_DISALLOW_COPY(ExplicitModuleTarget);

  ZC_NODISCARD static ExplicitModuleTarget absent();
  ZC_NODISCARD static ExplicitModuleTarget present(identity::ModuleKey&& target);
  ZC_NODISCARD ExplicitModuleTarget clone() const;
  ZC_NODISCARD zc::Maybe<const identity::ModuleKey&> target() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit ExplicitModuleTarget(zc::Maybe<identity::ModuleKey>&& target) noexcept;
  ZC_NODISCARD static zc::Maybe<ExplicitModuleTarget> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Maybe<identity::ModuleKey> targetValue;

  friend struct ConfiguredPreludeInput;
  friend struct DependencyAliasRootInput;
};

/// \brief Canonical independently verified search roots for exactly one stable crate.
class CanonicalModuleSearchRoots final {
public:
  CanonicalModuleSearchRoots(CanonicalModuleSearchRoots&&) noexcept = default;
  CanonicalModuleSearchRoots& operator=(CanonicalModuleSearchRoots&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalModuleSearchRoots);

  ZC_NODISCARD static zc::Maybe<CanonicalModuleSearchRoots> fromVerified(
      const identity::CrateKey& crate,
      zc::ArrayPtr<const binder::ModuleSearchRoot> environmentRoots);
  ZC_NODISCARD CanonicalModuleSearchRoots clone() const;
  ZC_NODISCARD const identity::CrateKey& crate() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ModuleSearchRoot> roots() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalModuleSearchRoots(zc::Vector<binder::ModuleSearchRoot>&& roots) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalModuleSearchRoots> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Vector<binder::ModuleSearchRoot> rootValues;

  friend struct ModuleSearchRootsInput;
};

/// \brief Self-describing exact catalog bucket value for typed query decoding.
class CanonicalModuleCatalogBucket final {
public:
  CanonicalModuleCatalogBucket(CanonicalModuleCatalogBucket&&) noexcept = default;
  CanonicalModuleCatalogBucket& operator=(CanonicalModuleCatalogBucket&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalModuleCatalogBucket);

  ZC_NODISCARD static CanonicalModuleCatalogBucket fromVerified(
      const identity::ModuleCatalogPathBucket& bucket);
  ZC_NODISCARD CanonicalModuleCatalogBucket clone() const;
  ZC_NODISCARD const identity::ModuleCatalogPathBucketKey& key() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::ModuleKey&> module() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;

private:
  explicit CanonicalModuleCatalogBucket(identity::ModuleCatalogPathBucket&& bucket) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalModuleCatalogBucket> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  identity::ModuleCatalogPathBucket bucketValue;

  friend struct ModuleCatalogPathBucketInput;
};

/// \brief Pinned Low-durability requester-to-root ancestry input.
struct RequesterModuleAncestryInput final {
  using Key = identity::ModuleKey;
  using Value = identity::RequesterModuleAncestry;

  static constexpr query::InputDescriptorMetadata descriptor{
      "RequesterModuleAncestryInput"_zcc, "zom.query.requester-module-ancestry"_zcc,
      query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Pinned Low-durability explicit exact-path catalog bucket input.
struct ModuleCatalogPathBucketInput final {
  using Key = identity::ModuleCatalogPathBucketKey;
  using Value = CanonicalModuleCatalogBucket;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ModuleCatalogPathBucketInput"_zcc, "zom.query.module-catalog-path-bucket"_zcc,
      query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Low-durability exact search-root set for one stable crate.
struct ModuleSearchRootsInput final {
  using Key = identity::CrateKey;
  using Value = CanonicalModuleSearchRoots;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ModuleSearchRootsInput"_zcc, "zom.query.module-search-roots"_zcc, query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Low-durability explicit dependency alias target, including semantic absence.
struct DependencyAliasRootInput final {
  using Key = DependencyAliasRootQueryKey;
  using Value = ExplicitModuleTarget;

  static constexpr query::InputDescriptorMetadata descriptor{"DependencyAliasRootInput"_zcc,
                                                             "zom.query.dependency-alias-root"_zcc,
                                                             query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Low-durability explicit configured prelude target, including semantic absence.
struct ConfiguredPreludeInput final {
  using Key = identity::CrateKey;
  using Value = ExplicitModuleTarget;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ConfiguredPreludeInput"_zcc, "zom.query.configured-prelude"_zcc, query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Semantic module-resolution query over exact fine-grained projections.
struct ResolveModuleRequestQuery final {
  using Key = identity::ModuleResolutionKey;
  using Value = identity::ModuleResolutionCandidates;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ResolveModuleRequestQuery"_zcc,
      "zom.query.resolve-module-request"_zcc,
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

/// \brief Stages the exact verified input closure needed by the supplied module requests.
ZC_NODISCARD bool stageModuleResolutionQueryInputs(
    query::InputTransaction& transaction, const binder::StructuralModuleResolver& resolver,
    zc::ArrayPtr<const binder::ModuleDependencyRequest> requests);

/// \brief Registers the complete initial module-resolution query family exactly once.
ZC_NODISCARD bool registerIncrementalModuleResolutionQueries(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::incremental_module_resolution_query
