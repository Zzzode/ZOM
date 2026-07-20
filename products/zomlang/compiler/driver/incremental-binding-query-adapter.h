// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/source-query-input.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/query/query-database.h"

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

  friend class PackageRootSetQueryKey;
};

/// \brief Canonical non-empty package-root-set query key.
class PackageRootSetQueryKey final {
public:
  PackageRootSetQueryKey(PackageRootSetQueryKey&&) noexcept = default;
  PackageRootSetQueryKey& operator=(PackageRootSetQueryKey&&) noexcept = default;
  ZC_DISALLOW_COPY(PackageRootSetQueryKey);

  ZC_NODISCARD static zc::Maybe<PackageRootSetQueryKey> fromVerified(
      const package::VerifiedPackageCompilationRequest& request);
  ZC_NODISCARD PackageRootSetQueryKey clone() const;
  ZC_NODISCARD zc::ArrayPtr<const StablePackageQueryKey> packages() const ZC_LIFETIMEBOUND;
  bool operator==(const PackageRootSetQueryKey& other) const noexcept;
  bool operator!=(const PackageRootSetQueryKey& other) const noexcept { return !(*this == other); }

private:
  explicit PackageRootSetQueryKey(zc::Vector<StablePackageQueryKey>&& packages) noexcept;
  ZC_NODISCARD static zc::Maybe<PackageRootSetQueryKey> from(
      zc::Vector<StablePackageQueryKey>&& packages);
  ZC_NODISCARD static zc::Maybe<PackageRootSetQueryKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Vector<StablePackageQueryKey> packageFields;

  friend struct ActiveCratesInput;
  friend struct ModuleBindingOrderQuery;
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
  friend struct ActiveModulesInput;
  friend struct ActiveSourcesInput;
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

  friend struct ActiveCratesInput;
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

  friend struct ActiveModulesInput;
  friend struct ModuleDependenciesInput;
  friend struct SelectedModuleSourceInput;
  friend struct ModuleBindingOrderQuery;
  friend class CanonicalModuleSet;
  friend class ModuleBindingOrder;
  friend class ModuleBindingOrderFailure;
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

  friend struct ActiveSourcesInput;
};

/// \brief Bounded canonical selected-source authority for one verified semantic module.
class SelectedModuleSource final {
public:
  SelectedModuleSource(SelectedModuleSource&&) noexcept = default;
  SelectedModuleSource& operator=(SelectedModuleSource&&) noexcept = default;
  ZC_DISALLOW_COPY(SelectedModuleSource);

  /// \brief Projects a source only when it belongs to the selected module's crate.
  ZC_NODISCARD static zc::Maybe<SelectedModuleSource> fromVerified(
      const identity::ModuleKey& module, const identity::SourceFileKey& source);
  ZC_NODISCARD SelectedModuleSource clone() const;
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> canonicalSourceBytes() const ZC_LIFETIMEBOUND;
  bool operator==(const SelectedModuleSource& other) const noexcept;
  bool operator!=(const SelectedModuleSource& other) const noexcept { return !(*this == other); }

private:
  explicit SelectedModuleSource(zc::Array<uint8_t>&& canonicalSourceBytes) noexcept;
  ZC_NODISCARD static zc::Maybe<SelectedModuleSource> decodeBounded(
      zc::ArrayPtr<const uint8_t> bytes);

  zc::Array<uint8_t> canonicalSourceBytesField;

  friend struct SelectedModuleSourceInput;
};

/// \brief Canonically sorted and duplicate-free stable module set.
class CanonicalModuleSet final {
public:
  CanonicalModuleSet(CanonicalModuleSet&&) noexcept = default;
  CanonicalModuleSet& operator=(CanonicalModuleSet&&) noexcept = default;
  ZC_DISALLOW_COPY(CanonicalModuleSet);

  ZC_NODISCARD static zc::Maybe<CanonicalModuleSet> from(
      zc::Vector<StableModuleQueryKey>&& modules);
  ZC_NODISCARD CanonicalModuleSet clone() const;
  ZC_NODISCARD zc::ArrayPtr<const StableModuleQueryKey> modules() const ZC_LIFETIMEBOUND;
  ZC_NODISCARD bool contains(const StableModuleQueryKey& module) const noexcept;

private:
  explicit CanonicalModuleSet(zc::Vector<StableModuleQueryKey>&& modules) noexcept;
  ZC_NODISCARD static zc::Maybe<CanonicalModuleSet> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Vector<StableModuleQueryKey> moduleFields;

  friend struct ActiveModulesInput;
  friend struct ModuleDependenciesInput;
  friend class ModuleDependencySet;
};

/// \brief Explicit dependency input, including a semantic missing state.
class ModuleDependencySet final {
public:
  ModuleDependencySet(ModuleDependencySet&&) noexcept = default;
  ModuleDependencySet& operator=(ModuleDependencySet&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencySet);

  ZC_NODISCARD static ModuleDependencySet missing();
  ZC_NODISCARD static ModuleDependencySet present(CanonicalModuleSet&& dependencies);
  ZC_NODISCARD ModuleDependencySet clone() const;
  ZC_NODISCARD bool isMissing() const noexcept;
  ZC_NODISCARD zc::Maybe<const CanonicalModuleSet&> dependencies() const noexcept;

private:
  explicit ModuleDependencySet(zc::Maybe<CanonicalModuleSet>&& dependencies) noexcept;
  ZC_NODISCARD static zc::Maybe<ModuleDependencySet> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Maybe<CanonicalModuleSet> dependencyFields;

  friend struct ModuleDependenciesInput;
};

/// \brief Canonical dependency-first binding schedule.
class ModuleBindingOrder final {
public:
  ModuleBindingOrder(ModuleBindingOrder&&) noexcept = default;
  ModuleBindingOrder& operator=(ModuleBindingOrder&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleBindingOrder);

  ZC_NODISCARD static zc::Maybe<ModuleBindingOrder> fromUnique(
      zc::Vector<StableModuleQueryKey>&& modules);
  ZC_NODISCARD ModuleBindingOrder clone() const;
  ZC_NODISCARD zc::ArrayPtr<const StableModuleQueryKey> modules() const ZC_LIFETIMEBOUND;

private:
  explicit ModuleBindingOrder(zc::Vector<StableModuleQueryKey>&& modules) noexcept;
  ZC_NODISCARD static zc::Maybe<ModuleBindingOrder> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  zc::Vector<StableModuleQueryKey> moduleFields;

  friend struct ModuleBindingOrderQuery;
};

enum class ModuleBindingOrderFailureKind : uint8_t {
  MissingDependencies = 0x01,
  DependencyOutsideActiveSet = 0x02,
  SelfDependency = 0x03,
  Cycle = 0x04
};

/// \brief Deterministic semantic witness for one invalid active-module topology.
class ModuleBindingOrderFailure final {
public:
  ModuleBindingOrderFailure(ModuleBindingOrderFailure&&) noexcept = default;
  ModuleBindingOrderFailure& operator=(ModuleBindingOrderFailure&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleBindingOrderFailure);

  ZC_NODISCARD static ModuleBindingOrderFailure missingDependencies(
      const StableModuleQueryKey& requester);
  ZC_NODISCARD static ModuleBindingOrderFailure dependencyOutsideActiveSet(
      const StableModuleQueryKey& requester, const StableModuleQueryKey& dependency);
  ZC_NODISCARD static ModuleBindingOrderFailure selfDependency(
      const StableModuleQueryKey& requester);
  ZC_NODISCARD static ModuleBindingOrderFailure cycle(const StableModuleQueryKey& requester);
  ZC_NODISCARD ModuleBindingOrderFailure clone() const;
  ZC_NODISCARD ModuleBindingOrderFailureKind kind() const noexcept;
  ZC_NODISCARD const StableModuleQueryKey& requester() const noexcept;
  ZC_NODISCARD zc::Maybe<const StableModuleQueryKey&> dependency() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encode() const;
  ZC_NODISCARD static zc::Maybe<ModuleBindingOrderFailure> decode(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD bool sameAs(const ModuleBindingOrderFailure& other) const;

private:
  ModuleBindingOrderFailure(ModuleBindingOrderFailureKind kind, StableModuleQueryKey&& requester,
                            zc::Maybe<StableModuleQueryKey>&& dependency) noexcept;
  ZC_NODISCARD static ModuleBindingOrderFailure withoutDependency(
      ModuleBindingOrderFailureKind kind, const StableModuleQueryKey& requester);
  ZC_NODISCARD static ModuleBindingOrderFailure withDependency(
      ModuleBindingOrderFailureKind kind, const StableModuleQueryKey& requester,
      const StableModuleQueryKey& dependency);

  ModuleBindingOrderFailureKind kindField;
  StableModuleQueryKey requesterField;
  zc::Maybe<StableModuleQueryKey> dependencyField;

  friend struct ModuleBindingOrderQuery;
};

struct ActiveModulesInput final {
  using Key = StableCrateQueryKey;
  using Value = CanonicalModuleSet;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Low-durability canonical active source set for one stable crate.
struct ActiveSourcesInput final {
  using Key = StableCrateQueryKey;
  using Value = CanonicalSourceSet;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Medium-durability active replacement crates for one package-root set.
struct ActiveCratesInput final {
  using Key = PackageRootSetQueryKey;
  using Value = CanonicalCrateSet;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

struct ModuleDependenciesInput final {
  using Key = StableModuleQueryKey;
  using Value = ModuleDependencySet;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Low-durability explicit module-to-selected-source authority.
struct SelectedModuleSourceInput final {
  using Key = StableModuleQueryKey;
  using Value = SelectedModuleSource;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static zc::Array<uint8_t> encodeValue(const Value& value);
  ZC_NODISCARD static zc::Maybe<Value> decodeValue(zc::ArrayPtr<const uint8_t> bytes);
};

/// \brief Verifies that every selected module source has exactly one snapshot input key.
ZC_NODISCARD bool verifySelectedSourceSnapshotClosure(
    zc::ArrayPtr<const SelectedModuleSource> selectedSources,
    zc::ArrayPtr<const identity::source_query::StableSourceQueryKey> snapshotSources);

struct ModuleBindingOrderQuery final {
  using Key = PackageRootSetQueryKey;
  using Value = ModuleBindingOrder;

  ZC_NODISCARD static zc::StringPtr domain();
  ZC_NODISCARD static query::QueryKindContract contract();
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
