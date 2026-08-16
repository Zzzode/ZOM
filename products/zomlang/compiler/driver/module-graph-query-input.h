// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/canonical-input-payload-digest.h"
#include "zomlang/compiler/binder/parsed-module-graph-input.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/package/canonical-package-compilation-request.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/query/query-database.h"
#include "zomlang/compiler/source/core-distribution.h"

namespace zomlang::compiler::binder {
class StructuralModuleResolver;
}  // namespace zomlang::compiler::binder

namespace zomlang::compiler::driver::core_library_query {
class VerifiedCoreDistributionInputTransaction;
}

namespace zomlang::compiler::driver::package {
class VerifiedPackageCompilationRequest;
}

namespace zomlang::compiler::driver::module_graph_query {

class ModuleGraphInputTransactionVerifier;

/// \brief One canonical input key and its exact canonical value.
template <typename Key, typename Value>
class CanonicalInputEntry final {
public:
  CanonicalInputEntry(CanonicalInputEntry&&) noexcept = default;
  CanonicalInputEntry& operator=(CanonicalInputEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalInputEntry);

  ZC_NODISCARD static CanonicalInputEntry from(Key&& key, Value&& value) {
    return CanonicalInputEntry(zc::mv(key), zc::mv(value));
  }
  ZC_NODISCARD CanonicalInputEntry clone() const {
    return CanonicalInputEntry(keyField.clone(), valueField.clone());
  }
  ZC_NODISCARD const Key& key() const noexcept { return keyField; }
  ZC_NODISCARD const Value& value() const noexcept { return valueField; }

private:
  CanonicalInputEntry(Key&& key, Value&& value) noexcept
      : keyField(zc::mv(key)), valueField(zc::mv(value)) {}

  Key keyField;
  Value valueField;
};

using CompilationOptionsEntry =
    CanonicalInputEntry<identity::CrateKey, identity::source_query::CanonicalCompilationOptions>;
using ModuleSearchRootsEntry =
    CanonicalInputEntry<identity::CrateKey,
                        incremental_module_resolution_query::CanonicalModuleSearchRoots>;

/// \brief Independent live authorities used to construct and verify one complete context.
struct CompleteCompilationContextSources final {
  const package::VerifiedPackageCompilationRequest& packageRequest;
  const incremental_binding_query::PackageRootSetKey& packageRootSet;
  const incremental_binding_query::CanonicalPackageGraph& packageGraph;
  zc::ArrayPtr<const identity::CrateKey> userRootCrates;
  zc::ArrayPtr<const identity::CrateKey> projectedCoreCrates;
  zc::ArrayPtr<const CompilationOptionsEntry> compilationOptions;
  zc::ArrayPtr<const ModuleSearchRootsEntry> moduleSearchRoots;
  const source::core::CoreDistributionInputRecord& coreDistribution;
};

/// \brief Handle-free canonical authority for one complete compilation context.
class CompleteCompilationContextAuthority final {
public:
  ~CompleteCompilationContextAuthority() noexcept(false);
  CompleteCompilationContextAuthority(CompleteCompilationContextAuthority&&) noexcept;
  CompleteCompilationContextAuthority& operator=(CompleteCompilationContextAuthority&&) noexcept;
  ZC_DISALLOW_COPY(CompleteCompilationContextAuthority);

  ZC_NODISCARD static zc::Maybe<CompleteCompilationContextAuthority> fromVerified(
      const CompleteCompilationContextSources& sources);
  ZC_NODISCARD static zc::Maybe<CompleteCompilationContextAuthority> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD CompleteCompilationContextAuthority clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const package::CanonicalPackageCompilationRequest& packageRequest() const noexcept;
  ZC_NODISCARD const incremental_binding_query::PackageRootSetKey& packageRootSet() const noexcept;
  ZC_NODISCARD const incremental_binding_query::CanonicalPackageGraph& packageGraph()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> userRootCrates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> projectedCoreCrates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> expectedRootCrates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> completeCrates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CompilationOptionsEntry> compilationOptions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleSearchRootsEntry> moduleSearchRoots() const noexcept;
  ZC_NODISCARD const source::core::CoreDistributionRecord& coreDistributionRecord() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& coreDistributionDigest() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const CompleteCompilationContextAuthority& other) const;

private:
  struct Impl;
  explicit CompleteCompilationContextAuthority(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Reconstructs a complete context without trusting its candidate value.
class CompleteCompilationContextAuthorityInputVerifier final {
public:
  ZC_NODISCARD static bool verify(const CompleteCompilationContextAuthority& candidate,
                                  const CompleteCompilationContextSources& sources);
};

/// \brief Computes the canonical witness of one complete-context authority value.
ZC_NODISCARD zc::Maybe<identity::Sha256Digest> computeCompleteCompilationContextWitness(
    const CompleteCompilationContextAuthority& authority);

/// \brief Computes one transaction witness from its domain and complete payload bytes.
ZC_NODISCARD zc::Maybe<binder::CanonicalInputPayloadDigest> computeCanonicalInputPayloadDigest(
    zc::StringPtr transactionDomain, zc::ArrayPtr<const uint8_t> payloadBytes);

/// \brief Frozen witness installed by the complete core-distribution transaction.
struct CoreDistributionTransactionWitnessInput final {
  using Key = incremental_binding_query::CompilationRootSetQueryKey;
  using Value = binder::CanonicalInputPayloadDigest;

  static constexpr query::InputDescriptorMetadata descriptor{
      "CoreDistributionTransactionWitnessInput"_zcc,
      "zom.query.core-distribution-transaction-witness"_zcc, query::Durability::Frozen};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Frozen witness installed by the complete structural-input transaction.
struct ModuleStructureTransactionWitnessInput final {
  using Key = incremental_binding_query::CompilationRootSetQueryKey;
  using Value = binder::CanonicalInputPayloadDigest;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ModuleStructureTransactionWitnessInput"_zcc,
      "zom.query.module-structure-transaction-witness"_zcc, query::Durability::Frozen};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Frozen witness installed by the complete contextual-authority transaction.
struct ContextualIdentityAuthorityTransactionWitnessInput final {
  using Key = incremental_binding_query::CompilationRootSetQueryKey;
  using Value = binder::CanonicalInputPayloadDigest;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ContextualIdentityAuthorityTransactionWitnessInput"_zcc,
      "zom.query.contextual-identity-authority-transaction-witness"_zcc, query::Durability::Frozen};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Independently reconstructs the complete final-snapshot witness.
ZC_NODISCARD zc::Maybe<identity::Sha256Digest> computeFinalSnapshotWitness(
    const query::QuerySnapshot& snapshot,
    const incremental_binding_query::CompilationRootSetQueryKey& contextRoots);

/// \brief Frozen complete-context input required by final snapshot admission.
struct CompleteCompilationContextAuthorityInput final {
  using Key = incremental_binding_query::CompilationRootSetQueryKey;
  using Value = CompleteCompilationContextAuthority;

  static constexpr query::InputDescriptorMetadata descriptor{
      "CompleteCompilationContextAuthorityInput"_zcc,
      "zom.input.complete-compilation-context-authority"_zcc, query::Durability::Frozen};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::FinalAuthorityCheck verifyFinalAuthority(
      const query::QuerySnapshot& snapshot, const Key& key, const Value& value,
      const identity::Sha256Digest& witness);
};

/// \brief One selected stable module and its exact source.
class SelectedModuleRecord final {
public:
  SelectedModuleRecord(identity::ModuleKey&& module, identity::SourceFileKey&& source) noexcept;
  SelectedModuleRecord(SelectedModuleRecord&&) noexcept = default;
  SelectedModuleRecord& operator=(SelectedModuleRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(SelectedModuleRecord);

  ZC_NODISCARD SelectedModuleRecord clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  identity::ModuleKey moduleValue;
  identity::SourceFileKey sourceValue;
};

/// \brief Canonical sole module-to-source authority for one active crate.
class SelectedModuleCatalog final {
public:
  SelectedModuleCatalog(SelectedModuleCatalog&&) noexcept = default;
  SelectedModuleCatalog& operator=(SelectedModuleCatalog&&) noexcept = default;
  ZC_DISALLOW_COPY(SelectedModuleCatalog);

  ZC_NODISCARD static zc::Maybe<SelectedModuleCatalog> from(
      identity::CrateKey&& crate, zc::Vector<SelectedModuleRecord>&& entries);
  ZC_NODISCARD static zc::Maybe<SelectedModuleCatalog> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD SelectedModuleCatalog clone() const;
  ZC_NODISCARD const identity::CrateKey& crate() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const SelectedModuleRecord> entries() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  SelectedModuleCatalog(identity::CrateKey&& crate,
                        zc::Vector<SelectedModuleRecord>&& entries) noexcept;

  identity::CrateKey crateValue;
  zc::Vector<SelectedModuleRecord> entryValues;
};

enum class DetachedModuleDependencySiteKind : uint8_t {
  Import = 0x01,
  ForeignReexport = 0x02,
  ModuleAlias = 0x03
};

/// \brief Stable syntax-independent coordinate for one module dependency.
class DetachedModuleDependencySite final {
public:
  DetachedModuleDependencySite(DetachedModuleDependencySite&&) noexcept = default;
  DetachedModuleDependencySite& operator=(DetachedModuleDependencySite&&) noexcept = default;
  ZC_DISALLOW_COPY(DetachedModuleDependencySite);

  ZC_NODISCARD static zc::Maybe<DetachedModuleDependencySite> from(
      DetachedModuleDependencySiteKind kind,
      zc::Vector<identity::ModulePathSegment>&& normalizedPath, uint32_t schemaPreorderOrdinal);
  ZC_NODISCARD DetachedModuleDependencySite clone() const;
  ZC_NODISCARD DetachedModuleDependencySiteKind kind() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModulePathSegment> normalizedPath() const noexcept;
  ZC_NODISCARD uint32_t schemaPreorderOrdinal() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  DetachedModuleDependencySite(DetachedModuleDependencySiteKind kind,
                               zc::Vector<identity::ModulePathSegment>&& normalizedPath,
                               uint32_t schemaPreorderOrdinal) noexcept;

  DetachedModuleDependencySiteKind kindValue;
  zc::Vector<identity::ModulePathSegment> normalizedPathValue;
  uint32_t schemaPreorderOrdinalValue;
};

/// \brief Complete detached dependency-site authority for one selected module.
class DetachedModuleDependencySiteSet final {
public:
  DetachedModuleDependencySiteSet(DetachedModuleDependencySiteSet&&) noexcept = default;
  DetachedModuleDependencySiteSet& operator=(DetachedModuleDependencySiteSet&&) noexcept = default;
  ZC_DISALLOW_COPY(DetachedModuleDependencySiteSet);

  ZC_NODISCARD static zc::Maybe<DetachedModuleDependencySiteSet> from(
      identity::ModuleKey&& module, identity::SourceFileKey&& source,
      const identity::Sha256Digest& sourceDigest, zc::Vector<DetachedModuleDependencySite>&& sites);
  ZC_NODISCARD static zc::Maybe<DetachedModuleDependencySiteSet> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD DetachedModuleDependencySiteSet clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& sourceDigest() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DetachedModuleDependencySite> sites() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  DetachedModuleDependencySiteSet(identity::ModuleKey&& module, identity::SourceFileKey&& source,
                                  const identity::Sha256Digest& sourceDigest,
                                  zc::Vector<DetachedModuleDependencySite>&& sites) noexcept;

  identity::ModuleKey moduleValue;
  identity::SourceFileKey sourceValue;
  identity::Sha256Digest sourceDigestValue;
  zc::Vector<DetachedModuleDependencySite> siteValues;
};

struct SelectedModuleCatalogInput final {
  using Key = identity::CrateKey;
  using Value = SelectedModuleCatalog;

  static constexpr query::InputDescriptorMetadata descriptor{
      "SelectedModuleCatalogInput"_zcc, "zom.query.selected-module-catalog"_zcc,
      query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

struct ModuleDependencySiteInput final {
  using Key = identity::ModuleKey;
  using Value = DetachedModuleDependencySiteSet;

  static constexpr query::InputDescriptorMetadata descriptor{
      "ModuleDependencySiteInput"_zcc, "zom.query.module-dependency-site-input"_zcc,
      query::Durability::Low};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Canonical active module membership derived for one crate.
class ActiveModuleSetRecord final {
public:
  ActiveModuleSetRecord(ActiveModuleSetRecord&&) noexcept = default;
  ActiveModuleSetRecord& operator=(ActiveModuleSetRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ActiveModuleSetRecord);

  ZC_NODISCARD static zc::Maybe<ActiveModuleSetRecord> from(
      zc::Vector<identity::ModuleKey>&& modules);
  ZC_NODISCARD static zc::Maybe<ActiveModuleSetRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ActiveModuleSetRecord clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> modules() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  explicit ActiveModuleSetRecord(zc::Vector<identity::ModuleKey>&& modules) noexcept;
  zc::Vector<identity::ModuleKey> moduleValues;
};

/// \brief Canonical semantic dependency requests for one selected module.
class ModuleDependencyRequestSetRecord final {
public:
  ModuleDependencyRequestSetRecord(ModuleDependencyRequestSetRecord&&) noexcept = default;
  ModuleDependencyRequestSetRecord& operator=(ModuleDependencyRequestSetRecord&&) noexcept =
      default;
  ZC_DISALLOW_COPY(ModuleDependencyRequestSetRecord);

  ZC_NODISCARD static zc::Maybe<ModuleDependencyRequestSetRecord> from(
      zc::Vector<identity::ModuleResolutionKey>&& requests);
  ZC_NODISCARD static zc::Maybe<ModuleDependencyRequestSetRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleDependencyRequestSetRecord clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleResolutionKey> requests() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  explicit ModuleDependencyRequestSetRecord(
      zc::Vector<identity::ModuleResolutionKey>&& requests) noexcept;
  zc::Vector<identity::ModuleResolutionKey> requestValues;
};

/// \brief Canonical distinct resolved module dependencies.
class ModuleDependencySetRecord final {
public:
  ModuleDependencySetRecord(ModuleDependencySetRecord&&) noexcept = default;
  ModuleDependencySetRecord& operator=(ModuleDependencySetRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencySetRecord);

  ZC_NODISCARD static zc::Maybe<ModuleDependencySetRecord> from(
      zc::Vector<identity::ModuleKey>&& dependencies);
  ZC_NODISCARD static zc::Maybe<ModuleDependencySetRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleDependencySetRecord clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> dependencies() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  explicit ModuleDependencySetRecord(zc::Vector<identity::ModuleKey>&& dependencies) noexcept;
  zc::Vector<identity::ModuleKey> dependencyValues;
};

enum class ModuleDependencyFailureKind : uint8_t { Missing = 0x01, Ambiguous = 0x02 };

/// \brief Stable source-backed dependency resolution failure.
class ModuleDependencyFailureRecord final {
public:
  ModuleDependencyFailureRecord(ModuleDependencyFailureRecord&&) noexcept = default;
  ModuleDependencyFailureRecord& operator=(ModuleDependencyFailureRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencyFailureRecord);

  ZC_NODISCARD static zc::Maybe<ModuleDependencyFailureRecord> missing(
      identity::ModuleResolutionKey&& request);
  ZC_NODISCARD static zc::Maybe<ModuleDependencyFailureRecord> ambiguous(
      identity::ModuleResolutionKey&& request, zc::Vector<identity::ModuleKey>&& candidates);
  ZC_NODISCARD static zc::Maybe<ModuleDependencyFailureRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleDependencyFailureRecord clone() const;
  ZC_NODISCARD ModuleDependencyFailureKind kind() const noexcept;
  ZC_NODISCARD const identity::ModuleResolutionKey& request() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> candidates() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ModuleDependencyFailureRecord(ModuleDependencyFailureKind kind,
                                identity::ModuleResolutionKey&& request,
                                zc::Vector<identity::ModuleKey>&& candidates) noexcept;

  ModuleDependencyFailureKind kindValue;
  identity::ModuleResolutionKey requestValue;
  zc::Vector<identity::ModuleKey> candidateValues;
};

struct SelectedModuleSourceQuery final {
  using Key = identity::ModuleKey;
  using Value = identity::SourceFileKey;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "SelectedModuleSourceQuery"_zcc,
      "zom.query.selected-module-source"_zcc,
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

struct ActiveModulesQuery final {
  using Key = identity::CrateKey;
  using Value = ActiveModuleSetRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ActiveModulesQuery"_zcc,
      "zom.query.active-modules"_zcc,
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

struct ModuleDependencySitesQuery final {
  using Key = identity::ModuleKey;
  using Value = DetachedModuleDependencySiteSet;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ModuleDependencySitesQuery"_zcc,
      "zom.query.module-dependency-sites"_zcc,
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

struct ModuleDependencyRequestsQuery final {
  using Key = identity::ModuleKey;
  using Value = ModuleDependencyRequestSetRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ModuleDependencyRequestsQuery"_zcc,
      "zom.query.module-dependency-requests"_zcc,
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

struct ModuleDependenciesQuery final {
  using Key = identity::ModuleKey;
  using Value = ModuleDependencySetRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "ModuleDependenciesQuery"_zcc,
      "zom.query.module-dependencies"_zcc,
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

/// \brief One staged dependency-alias input value.
struct ConfiguredDependencyAlias final {
  incremental_module_resolution_query::DependencyAliasRootQueryKey key;
  incremental_module_resolution_query::ExplicitModuleTarget target;
};

/// \brief One staged configured-prelude input value.
struct ConfiguredCratePrelude final {
  identity::CrateKey crate;
  incremental_module_resolution_query::ExplicitModuleTarget target;
};

/// \brief Complete canonical value installed by the structural-input transaction.
class VerifiedModuleGraphInputPayload final {
public:
  ~VerifiedModuleGraphInputPayload() noexcept(false);
  VerifiedModuleGraphInputPayload(VerifiedModuleGraphInputPayload&&) noexcept;
  VerifiedModuleGraphInputPayload& operator=(VerifiedModuleGraphInputPayload&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedModuleGraphInputPayload);

  ZC_NODISCARD static zc::Maybe<VerifiedModuleGraphInputPayload> from(
      incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
      zc::Vector<identity::CrateKey>&& projectedCoreCrates,
      zc::Vector<SelectedModuleCatalog>&& selectedModuleCatalogs,
      zc::Vector<DetachedModuleDependencySiteSet>&& dependencySiteSets,
      zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestries,
      zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket>&&
          catalogBuckets,
      zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots>&& searchRoots,
      zc::Vector<ConfiguredDependencyAlias>&& dependencyAliases,
      zc::Vector<ConfiguredCratePrelude>&& configuredPreludes);
  ZC_NODISCARD static zc::Maybe<VerifiedModuleGraphInputPayload> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD VerifiedModuleGraphInputPayload clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> projectedCoreCrates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const SelectedModuleCatalog> selectedModuleCatalogs() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DetachedModuleDependencySiteSet> dependencySiteSets()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::RequesterModuleAncestry> requesterAncestries()
      const noexcept;
  ZC_NODISCARD
  zc::ArrayPtr<const incremental_module_resolution_query::CanonicalModuleCatalogBucket>
  catalogBuckets() const noexcept;
  ZC_NODISCARD
  zc::ArrayPtr<const incremental_module_resolution_query::CanonicalModuleSearchRoots> searchRoots()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ConfiguredDependencyAlias> dependencyAliases() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ConfiguredCratePrelude> configuredPreludes() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const VerifiedModuleGraphInputPayload& other) const;

private:
  struct Impl;
  explicit VerifiedModuleGraphInputPayload(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ModuleGraphInputTransactionVerifier;
  friend class VerifiedModuleGraphInputVerifier;
  friend class VerifiedModuleGraphInputTransaction;
};

enum class ModuleGraphInputFamily : uint8_t {
  SelectedModuleCatalog = 0x01,
  ModuleDependencySite = 0x02,
  ModuleCatalogPathBucket = 0x03,
  RequesterModuleAncestry = 0x04,
  ModuleSearchRoots = 0x05,
  DependencyAliasRoot = 0x06,
  ConfiguredPrelude = 0x07
};

/// \brief One exact family-and-key entry in the session input ledger.
class ModuleGraphInputLedgerEntry final {
public:
  ModuleGraphInputLedgerEntry(ModuleGraphInputLedgerEntry&&) noexcept = default;
  ModuleGraphInputLedgerEntry& operator=(ModuleGraphInputLedgerEntry&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleGraphInputLedgerEntry);

  ZC_NODISCARD static zc::Maybe<ModuleGraphInputLedgerEntry> from(ModuleGraphInputFamily family,
                                                                  zc::Array<uint8_t>&& keyBytes);
  ZC_NODISCARD ModuleGraphInputLedgerEntry clone() const;
  ZC_NODISCARD ModuleGraphInputFamily family() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> keyBytes() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ModuleGraphInputLedgerEntry& other) const noexcept;
  bool operator<(const ModuleGraphInputLedgerEntry& other) const noexcept;

private:
  ModuleGraphInputLedgerEntry(ModuleGraphInputFamily family,
                              zc::Array<uint8_t>&& keyBytes) noexcept;

  ModuleGraphInputFamily familyValue;
  zc::Array<uint8_t> keyBytesValue;
};

/// \brief Canonical process-local inventory of all structural input keys.
class VerifiedModuleGraphInputLedger final {
public:
  VerifiedModuleGraphInputLedger(VerifiedModuleGraphInputLedger&&) noexcept = default;
  VerifiedModuleGraphInputLedger& operator=(VerifiedModuleGraphInputLedger&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedModuleGraphInputLedger);

  ZC_NODISCARD static VerifiedModuleGraphInputLedger empty();
  ZC_NODISCARD static zc::Maybe<VerifiedModuleGraphInputLedger> from(
      zc::Vector<ModuleGraphInputLedgerEntry>&& entries);
  ZC_NODISCARD VerifiedModuleGraphInputLedger clone() const;
  ZC_NODISCARD zc::ArrayPtr<const ModuleGraphInputLedgerEntry> entries() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const VerifiedModuleGraphInputLedger& other) const noexcept;

private:
  explicit VerifiedModuleGraphInputLedger(
      zc::Vector<ModuleGraphInputLedgerEntry>&& entries) noexcept;

  zc::Vector<ModuleGraphInputLedgerEntry> entryValues;
};

/// \brief Immutable authorities from which complete structural inputs are reconstructed.
struct ModuleGraphInputTransactionAuthority final {
  const package::VerifiedPackageCompilationRequest& packageRequest;
  const core_library_query::VerifiedCoreDistributionInputTransaction& coreInputs;
  const binder::StructuralModuleResolver& resolver;
  zc::ArrayPtr<const binder::ParsedModuleGraphInput> parsedModules;
};

/// \brief Complete atomic replacement of one session's module-graph structural inputs.
class VerifiedModuleGraphInputTransaction final {
public:
  VerifiedModuleGraphInputTransaction(VerifiedModuleGraphInputTransaction&&) noexcept;
  VerifiedModuleGraphInputTransaction& operator=(VerifiedModuleGraphInputTransaction&&) noexcept;
  ~VerifiedModuleGraphInputTransaction() noexcept(false);
  ZC_DISALLOW_COPY(VerifiedModuleGraphInputTransaction);

  ZC_NODISCARD static zc::Maybe<VerifiedModuleGraphInputTransaction> prepare(
      const ModuleGraphInputTransactionAuthority& authority,
      query::DatabaseRevision expectedPreviousRevision,
      incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
      zc::Vector<identity::CrateKey>&& projectedCoreCrates,
      zc::Vector<SelectedModuleCatalog>&& catalogs,
      zc::Vector<DetachedModuleDependencySiteSet>&& dependencySites,
      zc::Vector<identity::RequesterModuleAncestry>&& requesterAncestries,
      zc::Vector<incremental_module_resolution_query::CanonicalModuleCatalogBucket>&&
          catalogBuckets,
      zc::Vector<incremental_module_resolution_query::CanonicalModuleSearchRoots>&& searchRoots,
      zc::Vector<ConfiguredDependencyAlias>&& dependencyAliases,
      zc::Vector<ConfiguredCratePrelude>&& configuredPreludes,
      const VerifiedModuleGraphInputLedger& priorLedger);

  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const VerifiedModuleGraphInputPayload& payload() const noexcept;
  ZC_NODISCARD const VerifiedModuleGraphInputLedger& priorLedger() const noexcept;
  ZC_NODISCARD const VerifiedModuleGraphInputLedger& nextLedger() const noexcept;
  ZC_NODISCARD query::InputCommitResult commit(query::QueryDatabase& database);

private:
  struct Impl;
  explicit VerifiedModuleGraphInputTransaction(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class ModuleGraphInputTransactionVerifier;
  friend class VerifiedModuleGraphInputVerifier;
};

/// \brief Independently verifies one complete structural-input transaction candidate.
class ModuleGraphInputTransactionVerifier final {
public:
  ZC_NODISCARD static bool verify(const ModuleGraphInputTransactionAuthority& authority,
                                  const VerifiedModuleGraphInputTransaction& candidate);
};

/// \brief Independently reconstructs and verifies the complete structural payload.
class VerifiedModuleGraphInputVerifier final {
public:
  ZC_NODISCARD static bool verify(const ModuleGraphInputTransactionAuthority& authority,
                                  const VerifiedModuleGraphInputPayload& candidate);
};

/// \brief Registers the structural inputs owned by the module-graph transaction.
ZC_NODISCARD bool registerModuleGraphQueries(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::module_graph_query
