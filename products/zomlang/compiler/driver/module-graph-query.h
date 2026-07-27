// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"

namespace zomlang::compiler::driver::module_graph_query {

/// \brief One stable requester-to-dependency graph edge.
class ModuleDependencyEdgeKey final {
public:
  ModuleDependencyEdgeKey(ModuleDependencyEdgeKey&&) noexcept = default;
  ModuleDependencyEdgeKey& operator=(ModuleDependencyEdgeKey&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleDependencyEdgeKey);

  ZC_NODISCARD static zc::Maybe<ModuleDependencyEdgeKey> from(identity::ModuleKey&& requester,
                                                              identity::ModuleKey&& dependency);
  ZC_NODISCARD static zc::Maybe<ModuleDependencyEdgeKey> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleDependencyEdgeKey clone() const;
  ZC_NODISCARD const identity::ModuleKey& requester() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& dependency() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ModuleDependencyEdgeKey(identity::ModuleKey&& requester,
                          identity::ModuleKey&& dependency) noexcept;
  identity::ModuleKey requesterValue;
  identity::ModuleKey dependencyValue;
};

/// \brief Complete stable module graph for one compilation-root key.
class ModuleGraphRecord final {
public:
  ModuleGraphRecord(ModuleGraphRecord&&) noexcept = default;
  ModuleGraphRecord& operator=(ModuleGraphRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleGraphRecord);

  ZC_NODISCARD static zc::Maybe<ModuleGraphRecord> from(
      zc::Vector<identity::ModuleKey>&& modules, zc::Vector<ModuleDependencyEdgeKey>&& edges);
  ZC_NODISCARD static zc::Maybe<ModuleGraphRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleGraphRecord clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> modules() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleDependencyEdgeKey> edges() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ModuleGraphRecord(zc::Vector<identity::ModuleKey>&& modules,
                    zc::Vector<ModuleDependencyEdgeKey>&& edges) noexcept;
  zc::Vector<identity::ModuleKey> moduleValues;
  zc::Vector<ModuleDependencyEdgeKey> edgeValues;
};

enum class ModuleGraphFailureKind : uint8_t {
  DuplicateActiveModule = 0x01,
  ForeignActiveModule = 0x02,
  DependencyOutsideGraph = 0x03
};

/// \brief Stable structural graph failure selected before SCC evaluation.
class ModuleGraphFailureRecord final {
public:
  ModuleGraphFailureRecord(ModuleGraphFailureRecord&&) noexcept = default;
  ModuleGraphFailureRecord& operator=(ModuleGraphFailureRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleGraphFailureRecord);

  ZC_NODISCARD static ModuleGraphFailureRecord duplicate(identity::ModuleKey&& module);
  ZC_NODISCARD static zc::Maybe<ModuleGraphFailureRecord> foreign(identity::CrateKey&& owner,
                                                                  identity::ModuleKey&& module);
  ZC_NODISCARD static zc::Maybe<ModuleGraphFailureRecord> outside(identity::ModuleKey&& requester,
                                                                  identity::ModuleKey&& dependency);
  ZC_NODISCARD static zc::Maybe<ModuleGraphFailureRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleGraphFailureRecord clone() const;
  ZC_NODISCARD ModuleGraphFailureKind kind() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::CrateKey&> owner() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD zc::Maybe<const identity::ModuleKey&> dependency() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ModuleGraphFailureRecord(ModuleGraphFailureKind kind, zc::Maybe<identity::CrateKey>&& owner,
                           identity::ModuleKey&& module,
                           zc::Maybe<identity::ModuleKey>&& dependency) noexcept;
  ModuleGraphFailureKind kindValue;
  zc::Maybe<identity::CrateKey> ownerValue;
  identity::ModuleKey moduleValue;
  zc::Maybe<identity::ModuleKey> dependencyValue;
};

/// \brief One sorted non-empty strongly connected module component.
class ModuleGraphSccComponent final {
public:
  ModuleGraphSccComponent(ModuleGraphSccComponent&&) noexcept = default;
  ModuleGraphSccComponent& operator=(ModuleGraphSccComponent&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleGraphSccComponent);

  ZC_NODISCARD static zc::Maybe<ModuleGraphSccComponent> from(
      zc::Vector<identity::ModuleKey>&& modules, bool cyclic);
  ZC_NODISCARD ModuleGraphSccComponent clone() const;
  ZC_NODISCARD zc::ArrayPtr<const identity::ModuleKey> modules() const noexcept;
  ZC_NODISCARD bool cyclic() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  ModuleGraphSccComponent(zc::Vector<identity::ModuleKey>&& modules, bool cyclic) noexcept;
  zc::Vector<identity::ModuleKey> moduleValues;
  bool cyclicValue;
};

/// \brief Exact dependency-first SCC decomposition of one stable module graph.
class ModuleGraphSccRecord final {
public:
  ModuleGraphSccRecord(ModuleGraphSccRecord&&) noexcept = default;
  ModuleGraphSccRecord& operator=(ModuleGraphSccRecord&&) noexcept = default;
  ZC_DISALLOW_COPY(ModuleGraphSccRecord);

  ZC_NODISCARD static zc::Maybe<ModuleGraphSccRecord> fromVerified(
      const ModuleGraphRecord& graph, zc::Vector<ModuleGraphSccComponent>&& components);
  ZC_NODISCARD static zc::Maybe<ModuleGraphSccRecord> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD ModuleGraphSccRecord clone() const;
  ZC_NODISCARD const identity::Sha256Digest& graphDigest() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const ModuleGraphSccComponent> components() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool hasCycle(const ModuleGraphRecord& graph) const;

private:
  ModuleGraphSccRecord(const identity::Sha256Digest& graphDigest,
                       zc::Vector<ModuleGraphSccComponent>&& components) noexcept;
  identity::Sha256Digest graphDigestValue;
  zc::Vector<ModuleGraphSccComponent> componentValues;
};

struct ModuleGraphQuery final {
  using Key = incremental_binding_query::CompilationRootSetQueryKey;
  using Value = ModuleGraphRecord;

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

struct ModuleGraphSccQuery final {
  using Key = incremental_binding_query::CompilationRootSetQueryKey;
  using Value = ModuleGraphSccRecord;

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

/// \brief Registers the stable graph and SCC queries.
ZC_NODISCARD bool registerStableModuleGraphQueries(query::QueryDatabase& database);

}  // namespace zomlang::compiler::driver::module_graph_query
