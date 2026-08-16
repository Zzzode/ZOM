// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zomlang/compiler/binder/stable-binding-facts.h"
#include "zomlang/compiler/driver/core/marker-authority.h"
#include "zomlang/compiler/driver/core/revision.h"
#include "zomlang/compiler/driver/core/signature.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/incremental-module-resolution-query.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/identity/key/compilation-unit-key.h"
#include "zomlang/compiler/identity/key/module-resolution-key.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/query/query-database.h"
#include "zomlang/compiler/source/core-distribution.h"
#include "zomlang/compiler/source/core-source-catalog.h"

namespace zomlang::compiler::driver::package {
class VerifiedPackageCompilationRequest;
}

namespace zomlang::compiler::driver::core_library_query {

/// \brief A complete compilation context and one projected toolchain-core crate.
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
  /// \brief Reconstructs a graph revision after its enclosing canonical record is verified.
  ZC_NODISCARD static CoreModuleGraphRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
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

/// \brief One authenticated semantic role and its stable core definition key.
struct CoreRoleSeedEntry final {
  source::core::CoreSemanticRole role;
  identity::DefinitionKey definition;

  ZC_NODISCARD CoreRoleSeedEntry clone() const;
};

/// \brief Stable revision of one exact source-backed core role seed.
class CoreRoleSeedRevision final {
public:
  /// \brief Reconstructs a role-seed revision after its enclosing canonical record is verified.
  ZC_NODISCARD static CoreRoleSeedRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreRoleSeedRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CoreRoleSeedRevision& other) const noexcept;
  bool operator!=(const CoreRoleSeedRevision& other) const noexcept { return !(*this == other); }

private:
  explicit CoreRoleSeedRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CoreRoleSeedRecord;
};

/// \brief Handle-free stable authority selecting the semantic core marker roles.
class CoreRoleSeedRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreRoleSeedRecord> from(
      identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
      const identity::Sha256Digest& distribution, identity::ModuleKey&& markerModule,
      zc::Vector<CoreRoleSeedEntry>&& roles);
  ZC_NODISCARD static zc::Maybe<CoreRoleSeedRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreRoleSeedRecord() noexcept(false);
  CoreRoleSeedRecord(CoreRoleSeedRecord&&) noexcept;
  CoreRoleSeedRecord& operator=(CoreRoleSeedRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreRoleSeedRecord);

  ZC_NODISCARD CoreRoleSeedRecord clone() const;
  ZC_NODISCARD const identity::CrateKey& core() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distribution() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& markerModule() const noexcept;
  ZC_NODISCARD const CoreRoleSeedRevision& revision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreRoleSeedEntry> roles() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CoreRoleSeedRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable semantic role projection reconstructed from core graph and identity inventory.
struct CoreRoleSeedQuery final {
  using Key = ContextualCoreCrateKey;
  using Value = CoreRoleSeedRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "CoreRoleSeedQuery"_zcc,
      "zom.query.core-role-seed"_zcc,
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

/// \brief One materialized core role identity retained by the bootstrap seed.
struct MaterializedCoreRoleSeedEntry final {
  source::core::CoreSemanticRole role;
  identity::DefinitionKey key;
  identity::DefId definition;

  ZC_NODISCARD MaterializedCoreRoleSeedEntry clone() const;
};

/// \brief Revision-local verified core role seed retaining its marker bound-module lease.
class VerifiedCoreRoleSeed final {
public:
  using BoundModuleLease =
      query::QueryCapabilityLease<const module_graph_query::VerifiedBoundModule>;

  ~VerifiedCoreRoleSeed() noexcept(false);
  VerifiedCoreRoleSeed(VerifiedCoreRoleSeed&&) noexcept;
  VerifiedCoreRoleSeed& operator=(VerifiedCoreRoleSeed&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreRoleSeed);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreRoleSeed> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      identity::CoreSemanticContextFingerprint&& coreContext,
      const identity::Sha256Digest& distribution, identity::CrateId crate,
      identity::ModuleId markerModule, zc::Vector<MaterializedCoreRoleSeedEntry>&& roles,
      CoreRoleSeedRevision revision, BoundModuleLease&& markerBoundModule);
  ZC_NODISCARD VerifiedCoreRoleSeed clone() const;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& distribution() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId markerModule() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedCoreRoleSeedEntry> roles() const noexcept;
  ZC_NODISCARD const CoreRoleSeedRevision& revision() const noexcept;
  ZC_NODISCARD const BoundModuleLease& markerBoundModuleLease() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreRoleSeed(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Final-sealed materializer for the lease-owning core role seed.
struct MaterializeCoreRoleSeedQuery final {
  using Key = ContextualCoreCrateKey;
  using Capability = VerifiedCoreRoleSeed;
  using FailureAlternatives = query::CapabilityFailureList<>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "MaterializeCoreRoleSeedQuery"_zcc, "zom.query.materialize-core-role-seed"_zcc,
      query::RetentionClass::Retained,    query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::None};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<MaterializeCoreRoleSeedQuery> provide(
      query::CapabilityQueryContext<MaterializeCoreRoleSeedQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<MaterializeCoreRoleSeedQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Closed declaration surface admitted for one initial source-backed core module.
enum class CoreBootstrapModuleSurface : uint8_t { Root = 0x01, Marker = 0x02, Prelude = 0x03 };

/// \brief Stable revision of one initial core bootstrap interface record.
class CoreBootstrapModuleInterfaceRevision final {
public:
  /// \brief Reconstructs an interface revision after its enclosing canonical record is verified.
  ZC_NODISCARD static CoreBootstrapModuleInterfaceRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreBootstrapModuleInterfaceRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CoreBootstrapModuleInterfaceRevision& other) const noexcept;
  bool operator!=(const CoreBootstrapModuleInterfaceRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CoreBootstrapModuleInterfaceRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CoreBootstrapModuleInterfaceRecord;
};

/// \brief Stable revision of the exported initial role projection for one core module.
class CoreExportSurfaceRevision final {
public:
  /// \brief Reconstructs an export-surface revision after its canonical record is verified.
  ZC_NODISCARD static CoreExportSurfaceRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreExportSurfaceRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CoreExportSurfaceRevision& other) const noexcept;
  bool operator!=(const CoreExportSurfaceRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CoreExportSurfaceRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CoreExportSurfaceRecord;
};

/// \brief Handle-free closed declaration projection for one initial core module.
class CoreBootstrapModuleInterfaceRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreBootstrapModuleInterfaceRecord> from(
      identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
      CoreModuleGraphRevision graphRevision, identity::ModuleKey&& module,
      const binder::ExportSurfaceRevision& bindingSurfaceRevision,
      CoreRoleSeedRevision roleSeedRevision, CoreBootstrapModuleSurface surface,
      zc::Vector<CoreRoleSeedEntry>&& roles);
  ZC_NODISCARD static zc::Maybe<CoreBootstrapModuleInterfaceRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreBootstrapModuleInterfaceRecord() noexcept(false);
  CoreBootstrapModuleInterfaceRecord(CoreBootstrapModuleInterfaceRecord&&) noexcept;
  CoreBootstrapModuleInterfaceRecord& operator=(CoreBootstrapModuleInterfaceRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreBootstrapModuleInterfaceRecord);

  ZC_NODISCARD CoreBootstrapModuleInterfaceRecord clone() const;
  ZC_NODISCARD const identity::CrateKey& core() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const CoreModuleGraphRevision& graphRevision() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const binder::ExportSurfaceRevision& bindingSurfaceRevision() const noexcept;
  ZC_NODISCARD const CoreRoleSeedRevision& roleSeedRevision() const noexcept;
  ZC_NODISCARD CoreBootstrapModuleSurface surface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreRoleSeedEntry> roles() const noexcept;
  ZC_NODISCARD const CoreBootstrapModuleInterfaceRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CoreBootstrapModuleInterfaceRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Handle-free defined and re-exported initial role projection for one core module.
class CoreExportSurfaceRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreExportSurfaceRecord> from(
      identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
      CoreModuleGraphRevision graphRevision, identity::ModuleKey&& module,
      CoreBootstrapModuleInterfaceRevision interfaceRevision,
      zc::Vector<CoreRoleSeedEntry>&& definedRoles,
      zc::Vector<CoreRoleSeedEntry>&& reexportedRoles);
  ZC_NODISCARD static zc::Maybe<CoreExportSurfaceRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreExportSurfaceRecord() noexcept(false);
  CoreExportSurfaceRecord(CoreExportSurfaceRecord&&) noexcept;
  CoreExportSurfaceRecord& operator=(CoreExportSurfaceRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreExportSurfaceRecord);

  ZC_NODISCARD CoreExportSurfaceRecord clone() const;
  ZC_NODISCARD const identity::CrateKey& core() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const CoreModuleGraphRevision& graphRevision() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const CoreBootstrapModuleInterfaceRevision& interfaceRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreRoleSeedEntry> definedRoles() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreRoleSeedEntry> reexportedRoles() const noexcept;
  ZC_NODISCARD const CoreExportSurfaceRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CoreExportSurfaceRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable semantic projection of one initial core module's defined and re-exported roles.
struct CoreExportSurfaceQuery final {
  using Key = ContextualCoreModuleKey;
  using Value = CoreExportSurfaceRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "CoreExportSurfaceQuery"_zcc,
      "zom.query.core-export-surface"_zcc,
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

/// \brief Stable revision of the exact marker-to-prelude role re-export projection.
class CorePreludeSurfaceRevision final {
public:
  /// \brief Reconstructs a prelude-surface revision after its canonical record is verified.
  ZC_NODISCARD static CorePreludeSurfaceRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CorePreludeSurfaceRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CorePreludeSurfaceRevision& other) const noexcept;
  bool operator!=(const CorePreludeSurfaceRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CorePreludeSurfaceRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CorePreludeSurfaceRecord;
};

/// \brief Handle-free exact prelude re-export contract for one projected core crate.
class CorePreludeSurfaceRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CorePreludeSurfaceRecord> from(
      identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
      CoreModuleGraphRevision graphRevision, identity::ModuleKey&& markerModule,
      identity::ModuleKey&& preludeModule, CoreExportSurfaceRevision markerExportRevision,
      CoreExportSurfaceRevision preludeExportRevision, zc::Vector<CoreRoleSeedEntry>&& roles);
  ZC_NODISCARD static zc::Maybe<CorePreludeSurfaceRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CorePreludeSurfaceRecord() noexcept(false);
  CorePreludeSurfaceRecord(CorePreludeSurfaceRecord&&) noexcept;
  CorePreludeSurfaceRecord& operator=(CorePreludeSurfaceRecord&&) noexcept;
  ZC_DISALLOW_COPY(CorePreludeSurfaceRecord);

  ZC_NODISCARD CorePreludeSurfaceRecord clone() const;
  ZC_NODISCARD const identity::CrateKey& core() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const CoreModuleGraphRevision& graphRevision() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& markerModule() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& preludeModule() const noexcept;
  ZC_NODISCARD const CoreExportSurfaceRevision& markerExportRevision() const noexcept;
  ZC_NODISCARD const CoreExportSurfaceRevision& preludeExportRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreRoleSeedEntry> roles() const noexcept;
  ZC_NODISCARD const CorePreludeSurfaceRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CorePreludeSurfaceRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable revision of the aggregate initial core role authority projection.
class CoreRoleAuthorityRevision final {
public:
  ZC_NODISCARD static CoreRoleAuthorityRevision fromDigest(
      const identity::Sha256Digest& digest) noexcept;
  ZC_NODISCARD CoreRoleAuthorityRevision clone() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept;
  bool operator==(const CoreRoleAuthorityRevision& other) const noexcept;
  bool operator!=(const CoreRoleAuthorityRevision& other) const noexcept {
    return !(*this == other);
  }

private:
  explicit CoreRoleAuthorityRevision(const identity::Sha256Digest& digest) noexcept;
  identity::Sha256Digest digestValue;

  friend class CoreRoleAuthorityRecord;
};

/// \brief Handle-free aggregate authority over the exact initial core roles.
class CoreRoleAuthorityRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreRoleAuthorityRecord> from(
      identity::CrateKey&& core, identity::CoreSemanticContextFingerprint&& coreContext,
      const identity::Sha256Digest& policyTemplateRevision, CoreRoleSeedRevision roleSeedRevision,
      CorePreludeSurfaceRevision preludeRevision, zc::Vector<CoreRoleSeedEntry>&& roles);
  ZC_NODISCARD static zc::Maybe<CoreRoleAuthorityRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);

  ~CoreRoleAuthorityRecord() noexcept(false);
  CoreRoleAuthorityRecord(CoreRoleAuthorityRecord&&) noexcept;
  CoreRoleAuthorityRecord& operator=(CoreRoleAuthorityRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreRoleAuthorityRecord);

  ZC_NODISCARD CoreRoleAuthorityRecord clone() const;
  ZC_NODISCARD const identity::CrateKey& core() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const identity::Sha256Digest& policyTemplateRevision() const noexcept;
  ZC_NODISCARD const CoreRoleSeedRevision& roleSeedRevision() const noexcept;
  ZC_NODISCARD const CorePreludeSurfaceRevision& preludeRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreRoleSeedEntry> roles() const noexcept;
  ZC_NODISCARD const CoreRoleAuthorityRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CoreRoleAuthorityRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

class VerifiedCoreBootstrapModuleInterface;

/// \brief Stable semantic projection proving the initial core prelude re-exports every role.
struct CorePreludeSurfaceQuery final {
  using Key = ContextualCoreCrateKey;
  using Value = CorePreludeSurfaceRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "CorePreludeSurfaceQuery"_zcc,
      "zom.query.core-prelude-surface"_zcc,
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

/// \brief Stable semantic projection authenticating the initial core role authority inputs.
struct CoreRoleAuthorityQuery final {
  using Key = ContextualCoreCrateKey;
  using Value = CoreRoleAuthorityRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "CoreRoleAuthorityQuery"_zcc,
      "zom.query.core-role-authority"_zcc,
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

/// \brief Revision-local core role authority retaining the verified role and prelude inputs.
class VerifiedCoreAuthorityBundle final {
public:
  using RoleSeedLease = query::QueryCapabilityLease<const VerifiedCoreRoleSeed>;
  using PreludeBoundModuleLease =
      query::QueryCapabilityLease<const module_graph_query::VerifiedBoundModule>;

  ~VerifiedCoreAuthorityBundle() noexcept(false);
  VerifiedCoreAuthorityBundle(VerifiedCoreAuthorityBundle&&) noexcept;
  VerifiedCoreAuthorityBundle& operator=(VerifiedCoreAuthorityBundle&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreAuthorityBundle);

  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const CoreRoleAuthorityRecord& record() const noexcept;
  ZC_NODISCARD const core::VerifiedCoreMarkerShapeInventory& shapes() const noexcept;
  ZC_NODISCARD const core::VerifiedCoreMarkerPolicyRegistry& policies() const noexcept;
  ZC_NODISCARD const core::VerifiedCoreStandardMarkerAuthority& authority() const noexcept;
  ZC_NODISCARD identity::ModuleId preludeModule() const noexcept;
  ZC_NODISCARD identity::DefId copy() const noexcept;
  ZC_NODISCARD identity::DefId linear() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreAuthorityBundle(zc::Own<Impl>&& impl) noexcept;
  ZC_NODISCARD static zc::Maybe<VerifiedCoreAuthorityBundle> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      CoreRoleAuthorityRecord&& record,
      source::core::CoreStandardMarkerPolicyTemplate&& policyTemplate,
      identity::ModuleKey&& preludeKey, RoleSeedLease&& roleSeed,
      PreludeBoundModuleLease&& prelude);
  ZC_NODISCARD const RoleSeedLease& roleSeedLease() const noexcept;
  ZC_NODISCARD const PreludeBoundModuleLease& preludeBoundModuleLease() const noexcept;
  zc::Own<Impl> impl;
  friend struct MaterializeCoreAuthorityQuery;
  friend class CoreLibraryQueryVerifier;
};

/// \brief Final-sealed materializer for the lease-owning core role authority.
struct MaterializeCoreAuthorityQuery final {
  using Key = ContextualCoreCrateKey;
  using Capability = VerifiedCoreAuthorityBundle;
  using FailureAlternatives = query::CapabilityFailureList<>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "MaterializeCoreAuthorityQuery"_zcc, "zom.query.materialize-core-authority"_zcc,
      query::RetentionClass::Retained,     query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,       query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::None};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<MaterializeCoreAuthorityQuery> provide(
      query::CapabilityQueryContext<MaterializeCoreAuthorityQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<MaterializeCoreAuthorityQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Revision-local bootstrap interface retaining the exact bound module and role-seed leases.
class VerifiedCoreBootstrapModuleInterface final {
public:
  using BoundModuleLease =
      query::QueryCapabilityLease<const module_graph_query::VerifiedBoundModule>;
  using RoleSeedLease = query::QueryCapabilityLease<const VerifiedCoreRoleSeed>;

  ~VerifiedCoreBootstrapModuleInterface() noexcept(false);
  VerifiedCoreBootstrapModuleInterface(VerifiedCoreBootstrapModuleInterface&&) noexcept;
  VerifiedCoreBootstrapModuleInterface& operator=(VerifiedCoreBootstrapModuleInterface&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreBootstrapModuleInterface);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreBootstrapModuleInterface> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      CoreBootstrapModuleInterfaceRecord&& record, core::VerifiedCoreSignatureFacts&& signatures,
      core::VerifiedCoreImportedSignatureView&& importedSignatures, BoundModuleLease&& boundModule,
      RoleSeedLease&& roleSeed);
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const CoreBootstrapModuleInterfaceRecord& record() const noexcept;
  ZC_NODISCARD const core::VerifiedCoreSignatureFacts& signatures() const noexcept;
  ZC_NODISCARD const core::VerifiedCoreImportedSignatureView& importedSignatures() const noexcept;
  ZC_NODISCARD const BoundModuleLease& boundModuleLease() const noexcept;
  ZC_NODISCARD const RoleSeedLease& roleSeedLease() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreBootstrapModuleInterface(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief One final declaration root projected from a stable core binding surface.
struct CoreFinalSignatureRoot final {
  identity::DefinitionKey binding;
  identity::DefinitionKey canonicalDefinition;
  zc::Maybe<binder::MemberVisibility> visibility;
  identity::ModuleKey sourceModule;
  CoreBindingSurfaceRevision bindingSurfaceRevision;

  ZC_NODISCARD CoreFinalSignatureRoot clone() const;
};

/// \brief One final declaration-only core module target.
struct CoreCanonicalModuleTarget final {
  binder::BindingNameKey name;
  identity::ModuleKey module;
  CoreBindingSurfaceRevision surfaceRevision;

  ZC_NODISCARD CoreCanonicalModuleTarget clone() const;
};

/// \brief Flat declaration-only final core module interface with no bootstrap lineage.
class CoreModuleInterfaceRecord final {
public:
  ZC_NODISCARD static zc::Maybe<CoreModuleInterfaceRecord> from(
      identity::ModuleKey&& module, identity::CoreSemanticContextFingerprint&& coreContext,
      zc::Vector<binder::StableExportedBinding>&& visibleBindings,
      zc::Vector<binder::StableExportedBinding>&& exportedBindings,
      zc::Vector<core::TypeFreeInterfaceSignatureRecord>&& lookupDefinitions,
      zc::Vector<core::TypeFreeInterfaceSignatureRecord>&& supportDefinitions,
      zc::Vector<CoreFinalSignatureRoot>&& signatureRoots,
      zc::Vector<CoreCanonicalModuleTarget>&& moduleTargets,
      zc::Vector<core::CoreMarkerShapeEntry>&& definedRoles,
      core::CoreStandardMarkerAuthorityRevision authorityRevision);

  ~CoreModuleInterfaceRecord() noexcept(false);
  CoreModuleInterfaceRecord(CoreModuleInterfaceRecord&&) noexcept;
  CoreModuleInterfaceRecord& operator=(CoreModuleInterfaceRecord&&) noexcept;
  ZC_DISALLOW_COPY(CoreModuleInterfaceRecord);

  ZC_NODISCARD CoreModuleInterfaceRecord clone() const;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD const identity::CoreSemanticContextFingerprint& coreContext() const noexcept;
  ZC_NODISCARD const CoreBindingSurfaceRevision& bindingSurfaceRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::StableExportedBinding> visibleBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::StableExportedBinding> exportedBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> lookupDefinitions()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const core::TypeFreeInterfaceSignatureRecord> supportDefinitions()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreFinalSignatureRoot> signatureRoots() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CoreCanonicalModuleTarget> moduleTargets() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const core::CoreMarkerShapeEntry> definedRoles() const noexcept;
  ZC_NODISCARD const core::CoreStandardMarkerAuthorityRevision& authorityRevision() const noexcept;
  ZC_NODISCARD const CoreModuleInterfaceRevision& revision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit CoreModuleInterfaceRecord(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Final declaration-only interface published after core authority materializes.
class VerifiedCoreModuleInterface final {
public:
  ~VerifiedCoreModuleInterface() noexcept(false);
  VerifiedCoreModuleInterface(VerifiedCoreModuleInterface&&) noexcept;
  VerifiedCoreModuleInterface& operator=(VerifiedCoreModuleInterface&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCoreModuleInterface);

  ZC_NODISCARD static zc::Maybe<VerifiedCoreModuleInterface> from(
      identity::SemanticContextBrand context, identity::ContextFingerprint&& fingerprint,
      identity::ModuleId module, CoreModuleInterfaceRecord&& record);
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const CoreModuleInterfaceRecord& record() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedCoreModuleInterface(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Final-sealed promotion from private bootstrap interfaces to final core interfaces.
struct FinalizeCoreModuleInterfaceQuery final {
  using Key = ContextualCoreModuleKey;
  using Capability = VerifiedCoreModuleInterface;
  using FailureAlternatives = query::CapabilityFailureList<>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "FinalizeCoreModuleInterfaceQuery"_zcc, "zom.query.finalize-core-module-interface"_zcc,
      query::RetentionClass::Retained,        query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,          query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::None};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<FinalizeCoreModuleInterfaceQuery> provide(
      query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<FinalizeCoreModuleInterfaceQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Stable closed declaration projection for one initial core bootstrap interface.
struct CoreBootstrapModuleInterfaceQuery final {
  using Key = ContextualCoreModuleKey;
  using Value = CoreBootstrapModuleInterfaceRecord;

  static constexpr query::SemanticDescriptorMetadata descriptor{
      "CoreBootstrapModuleInterfaceQuery"_zcc,
      "zom.query.core-bootstrap-module-interface"_zcc,
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

/// \brief Final-sealed materializer for one closed initial core bootstrap interface.
struct MaterializeCoreBootstrapModuleInterfaceQuery final {
  using Key = ContextualCoreModuleKey;
  using Capability = VerifiedCoreBootstrapModuleInterface;
  using FailureAlternatives = query::CapabilityFailureList<>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "MaterializeCoreBootstrapModuleInterfaceQuery"_zcc,
      "zom.query.materialize-core-bootstrap-module-interface"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::None};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<MaterializeCoreBootstrapModuleInterfaceQuery>
  provide(query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
          const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<MaterializeCoreBootstrapModuleInterfaceQuery>& context,
      const Key& key, const Capability& candidate);
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

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<driver::core_library_query::MaterializeCoreRoleSeedQuery> final {
public:
  using Descriptor = driver::core_library_query::MaterializeCoreRoleSeedQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityCandidateContract<
    driver::core_library_query::MaterializeCoreBootstrapModuleInterfaceQuery>
    final {
public:
  using Descriptor = driver::core_library_query::MaterializeCoreBootstrapModuleInterfaceQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityCandidateContract<driver::core_library_query::MaterializeCoreAuthorityQuery> final {
public:
  using Descriptor = driver::core_library_query::MaterializeCoreAuthorityQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityCandidateContract<driver::core_library_query::FinalizeCoreModuleInterfaceQuery>
    final {
public:
  using Descriptor = driver::core_library_query::FinalizeCoreModuleInterfaceQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

#define ZOM_DECLARE_CORE_ROLE_SEED_MATERIALIZER_PERMISSION(GlobalKey, Membership)               \
  template <>                                                                                   \
  struct ActiveMaterializerPermission<driver::core_library_query::MaterializeCoreRoleSeedQuery, \
                                      GlobalKey,                                                \
                                      driver::incremental_binding_query::Membership##Query>     \
      final {                                                                                   \
    static constexpr bool allowed = true;                                                       \
  }

ZOM_DECLARE_CORE_ROLE_SEED_MATERIALIZER_PERMISSION(identity::CrateKey, ActiveCrateMembership);
ZOM_DECLARE_CORE_ROLE_SEED_MATERIALIZER_PERMISSION(identity::ModuleKey, ActiveModuleMembership);
ZOM_DECLARE_CORE_ROLE_SEED_MATERIALIZER_PERMISSION(identity::DefinitionKey,
                                                   ActiveDefinitionMembership);

#undef ZOM_DECLARE_CORE_ROLE_SEED_MATERIALIZER_PERMISSION

}  // namespace zomlang::compiler::query
