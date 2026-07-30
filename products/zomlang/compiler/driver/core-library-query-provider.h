// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/identity/compilation-unit-key.h"
#include "zomlang/compiler/identity/module-resolution-key.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/query/query-database.h"
#include "zomlang/compiler/source/core-distribution.h"
#include "zomlang/compiler/source/core-source-catalog.h"

namespace zomlang::compiler::driver::package {
class VerifiedPackageCompilationRequest;
}

namespace zomlang::compiler::driver::core_library_query {

/// \brief Complete compilation context and one projected toolchain-core crate.
class ContextualCoreCrateKey final {
public:
  ContextualCoreCrateKey(ContextualCoreCrateKey&&) noexcept = default;
  ContextualCoreCrateKey& operator=(ContextualCoreCrateKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ContextualCoreCrateKey);

  ZC_NODISCARD static zc::Maybe<ContextualCoreCrateKey> from(
      incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
      identity::CrateKey&& crate);
  ZC_NODISCARD static zc::Maybe<ContextualCoreCrateKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ContextualCoreCrateKey clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const identity::CrateKey& crate() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ContextualCoreCrateKey& other) const noexcept;
  bool operator!=(const ContextualCoreCrateKey& other) const noexcept { return !(*this == other); }

private:
  ContextualCoreCrateKey(incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
                         identity::CrateKey&& crate) noexcept;

  incremental_binding_query::CompilationRootSetQueryKey contextRootsField;
  identity::CrateKey crateField;
};

/// \brief Complete compilation context and one module in a projected toolchain-core crate.
class ContextualCoreModuleKey final {
public:
  ContextualCoreModuleKey(ContextualCoreModuleKey&&) noexcept = default;
  ContextualCoreModuleKey& operator=(ContextualCoreModuleKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ContextualCoreModuleKey);

  ZC_NODISCARD static zc::Maybe<ContextualCoreModuleKey> from(
      incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
      identity::ModuleKey&& module);
  ZC_NODISCARD static zc::Maybe<ContextualCoreModuleKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ContextualCoreModuleKey clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const ContextualCoreModuleKey& other) const noexcept;
  bool operator!=(const ContextualCoreModuleKey& other) const noexcept { return !(*this == other); }

private:
  ContextualCoreModuleKey(incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
                          identity::ModuleKey&& module) noexcept;

  incremental_binding_query::CompilationRootSetQueryKey contextRootsField;
  identity::ModuleKey moduleField;
};

/// \brief High-durability admitted source-distribution authority for Toolchain(Core).
struct CoreDistributionInput final {
  using Key = identity::ToolchainUnitKey;
  using Value = source::core::CoreDistributionInputRecord;

  static constexpr query::InputDescriptorMetadata descriptor{
      "CoreDistributionInput"_zcc, "zom.query.core-distribution"_zcc, query::Durability::High};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Stable revision of one source-backed projected core module graph.
class CoreModuleGraphRevision final {
public:
  ZC_NODISCARD CoreModuleGraphRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CoreModuleGraphRevision& other) const noexcept;
  bool operator!=(const CoreModuleGraphRevision& other) const noexcept { return !(*this == other); }

private:
  explicit CoreModuleGraphRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CoreModuleGraphRecord;
};

/// \brief Handle-free stable graph projection for one exact toolchain-core crate.
class CoreModuleGraphRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreModuleGraphRecord> from(
      identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
      zc::Vector<identity::ModuleKey>&& modules,
      zc::Vector<module_graph_query::ModuleDependencyEdgeKey>&& edges);
  ZC_NODISCARD static zc::Maybe<CoreModuleGraphRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreModuleGraphRecord() noexcept(false);
  CoreModuleGraphRecord(CoreModuleGraphRecord&&) noexcept;
  CoreModuleGraphRecord& operator=(CoreModuleGraphRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreModuleGraphRecord);

  ZC_NODISCARD CoreModuleGraphRecord clone() const;
  ZC_NODISCARD const identity::CrateKey& core() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const CoreModuleGraphRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> modules() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const module_graph_query::ModuleDependencyEdgeKey> edges()
      const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CoreModuleGraphRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable semantic projection of the independently verified singleton core graph.
struct CoreModuleGraphQuery final {
  using Key = ContextualCoreCrateKey;
  using Value = CoreModuleGraphRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "CoreModuleGraphQuery"_zcc,
      "zom.query.core-module-graph"_zcc,
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

/// \brief Verified query inputs and structural catalog for one projected core crate.
class VerifiedCoreProjectionInput final {
public:
  ~VerifiedCoreProjectionInput() noexcept(false);
  VerifiedCoreProjectionInput(VerifiedCoreProjectionInput&&) noexcept;
  VerifiedCoreProjectionInput& operator=(VerifiedCoreProjectionInput&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreProjectionInput);

  ZC_NODISCARD const identity::CrateKey& crate() const noexcept;
  ZC_NODISCARD const source::core::AdmittedCoreSourceCatalog& catalog() const noexcept;
  ZC_NODISCARD const incremental_module_resolution_query::CanonicalModuleSearchRoots& searchRoots()
      const noexcept;

private:
  struct Impl;
  explicit VerifiedCoreProjectionInput(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
  friend class VerifiedCoreDistributionInputTransaction;
};

using ProjectedCoreSourceEntry =
    module_graph_query::CanonicalInputEntry<identity::source_query::StableSourceQueryKey,
                                            identity::source_query::CanonicalSourceSnapshot>;

/// \brief Complete canonical value installed by the core-distribution transaction.
class VerifiedCoreDistributionInputPayload final {
public:
  ~VerifiedCoreDistributionInputPayload() noexcept(false);
  VerifiedCoreDistributionInputPayload(VerifiedCoreDistributionInputPayload&&) noexcept;
  VerifiedCoreDistributionInputPayload& operator=(VerifiedCoreDistributionInputPayload&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreDistributionInputPayload);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreDistributionInputPayload> from(
      incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
      source::core::CoreDistributionRecord&& distributionRecord,
      const identity::Sha256Digest& distributionDigest,
      source::core::CoreStandardMarkerPolicyTemplate&& policyTemplate,
      zc::Vector<ProjectedCoreSourceEntry>&& projectedCoreSources,
      zc::Vector<module_graph_query::CompilationOptionsEntry>&& compilationOptions,
      zc::Vector<module_graph_query::ModuleSearchRootsEntry>&& moduleSearchRoots,
      zc::Vector<identity::CrateKey>&& projectedCoreInventory,
      module_graph_query::CompleteCompilationContextAuthority&& contextAuthority);
  ZC_NODISCARD static zc::Maybe<VerifiedCoreDistributionInputPayload> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD VerifiedCoreDistributionInputPayload clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const source::core::CoreDistributionRecord& distributionRecord() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distributionDigest() const noexcept;
  ZC_NODISCARD const source::core::CoreStandardMarkerPolicyTemplate& policyTemplate()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ProjectedCoreSourceEntry> projectedCoreSources() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const module_graph_query::CompilationOptionsEntry> compilationOptions()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const module_graph_query::ModuleSearchRootsEntry> moduleSearchRoots()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const identity::CrateKey> projectedCoreInventory() const noexcept;
  ZC_NODISCARD const module_graph_query::CompleteCompilationContextAuthority& contextAuthority()
      const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const VerifiedCoreDistributionInputPayload& other) const;

private:
  struct Impl;
  explicit VerifiedCoreDistributionInputPayload(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Independently reconstructs the complete core-distribution payload.
class VerifiedCoreDistributionInputVerifier final {
public:
  ZC_NODISCARD static bool verify(
      const VerifiedCoreDistributionInputPayload& candidate,
      const source::core::VerifiedCoreDistribution& distribution,
      const package::VerifiedPackageCompilationRequest& packageRequest,
      const identity::source_query::CanonicalCompilationOptions& compilationOptions,
      zc::ArrayPtr<const identity::CrateKey> completeConsumerInventory);
};

/// \brief Sole atomic writer for one session's complete pre-parse core input root.
class VerifiedCoreDistributionInputTransaction final {
public:
  ZC_NODISCARD static zc::Maybe<VerifiedCoreDistributionInputTransaction> prepare(
      query::DatabaseRevision expectedPreviousRevision,
      const source::core::VerifiedCoreDistribution& distribution,
      const package::VerifiedPackageCompilationRequest& packageRequest,
      module_graph_query::CompleteCompilationContextAuthority&& contextAuthority,
      const identity::source_query::CanonicalCompilationOptions& compilationOptions,
      zc::ArrayPtr<const identity::CrateKey> completeConsumerInventory);

  ~VerifiedCoreDistributionInputTransaction() noexcept(false);
  VerifiedCoreDistributionInputTransaction(VerifiedCoreDistributionInputTransaction&&) noexcept;
  VerifiedCoreDistributionInputTransaction& operator=(
      VerifiedCoreDistributionInputTransaction&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreDistributionInputTransaction);

  ZC_NODISCARD const source::core::CoreDistributionInputRecord& distribution() const noexcept;
  ZC_NODISCARD const VerifiedCoreDistributionInputPayload& payload() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const VerifiedCoreProjectionInput> projections() const noexcept;
  ZC_NODISCARD query::InputCommitResult commit(query::QueryDatabase& database);

private:
  struct Impl;
  explicit VerifiedCoreDistributionInputTransaction(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Registers the source-backed core query inventory implemented by this translation unit.
ZC_NODISCARD bool registerCoreLibraryQueryProvider(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::core_library_query
