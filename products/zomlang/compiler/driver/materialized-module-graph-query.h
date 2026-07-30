// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/binding-input.h"
#include "zomlang/compiler/diagnostics/diagnostic-fact.h"
#include "zomlang/compiler/driver/active-identity-membership-query.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/identity/canonical-identity-interner-set.h"
#include "zomlang/compiler/identity/semantic-context-fingerprint.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::driver::module_graph_query {

/// \brief Stable request-level dependency authority retained by a materialized graph.
class StableMaterializedDependencyWitness final {
public:
  ~StableMaterializedDependencyWitness() noexcept(false);
  StableMaterializedDependencyWitness(StableMaterializedDependencyWitness&&) noexcept;
  StableMaterializedDependencyWitness& operator=(StableMaterializedDependencyWitness&&) noexcept;
  ZC_DISALLOW_COPY(StableMaterializedDependencyWitness);

  ZC_NODISCARD static zc::Maybe<StableMaterializedDependencyWitness> from(
      identity::ModuleKey&& requester, identity::ModuleResolutionKey&& request,
      identity::ModuleKey&& dependency);
  ZC_NODISCARD static zc::Maybe<StableMaterializedDependencyWitness> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD StableMaterializedDependencyWitness clone() const;
  ZC_NODISCARD const identity::ModuleKey& requester() const noexcept;
  ZC_NODISCARD const identity::ModuleResolutionKey& request() const noexcept;
  ZC_NODISCARD const identity::ModuleKey& dependency() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  bool operator==(const StableMaterializedDependencyWitness& other) const;

private:
  struct Impl;
  explicit StableMaterializedDependencyWitness(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Runtime identity authority paired with one arena-local typed handle.
template <typename Key, typename Record, typename Handle>
class MaterializedIdentityEntry final {
public:
  ~MaterializedIdentityEntry() noexcept(false);
  MaterializedIdentityEntry(MaterializedIdentityEntry&&) noexcept;
  MaterializedIdentityEntry& operator=(MaterializedIdentityEntry&&) noexcept;
  ZC_DISALLOW_COPY(MaterializedIdentityEntry);

  ZC_NODISCARD static MaterializedIdentityEntry fromVerified(Key&& key, Record&& record,
                                                             Handle handle) noexcept;
  ZC_NODISCARD MaterializedIdentityEntry clone() const;
  ZC_NODISCARD const Key& key() const noexcept;
  ZC_NODISCARD const Record& record() const noexcept;
  ZC_NODISCARD Handle handle() const noexcept;

private:
  struct Impl;
  explicit MaterializedIdentityEntry(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

using MaterializedCompilationUnitEntry =
    MaterializedIdentityEntry<identity::CompilationUnitIdentity, identity::CompilationUnitIdentity,
                              identity::CompilationUnitId>;
using MaterializedCrateEntry =
    MaterializedIdentityEntry<identity::CrateKey, identity::CrateKey, identity::CrateId>;
using MaterializedSourceEntry =
    MaterializedIdentityEntry<identity::SourceFileKey, identity::SourceFileKey,
                              identity::SourceFileId>;
using MaterializedModuleEntry =
    MaterializedIdentityEntry<identity::ModuleKey, identity::ModuleKey, identity::ModuleId>;

/// \brief Request-level dependency edge using arena-local module handles.
class MaterializedModuleDependencyEdge final {
public:
  MaterializedModuleDependencyEdge(identity::ModuleId requester,
                                   identity::ModuleResolutionKey&& request,
                                   identity::ModuleId dependency) noexcept;
  ~MaterializedModuleDependencyEdge() noexcept(false);
  MaterializedModuleDependencyEdge(MaterializedModuleDependencyEdge&&) noexcept;
  MaterializedModuleDependencyEdge& operator=(MaterializedModuleDependencyEdge&&) noexcept;
  ZC_DISALLOW_COPY(MaterializedModuleDependencyEdge);

  ZC_NODISCARD MaterializedModuleDependencyEdge clone() const;
  ZC_NODISCARD identity::ModuleId requester() const noexcept;
  ZC_NODISCARD const identity::ModuleResolutionKey& request() const noexcept;
  ZC_NODISCARD identity::ModuleId dependency() const noexcept;

private:
  struct Impl;
  explicit MaterializedModuleDependencyEdge(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Stable complete witness underlying one revision-local materialized graph.
class MaterializedModuleGraphWitness final {
public:
  ~MaterializedModuleGraphWitness() noexcept(false);
  MaterializedModuleGraphWitness(MaterializedModuleGraphWitness&&) noexcept;
  MaterializedModuleGraphWitness& operator=(MaterializedModuleGraphWitness&&) noexcept;
  ZC_DISALLOW_COPY(MaterializedModuleGraphWitness);

  ZC_NODISCARD static zc::Maybe<MaterializedModuleGraphWitness> from(
      incremental_binding_query::CompilationRootSetQueryKey&& contextRoots,
      identity::SemanticContextFingerprint&& fingerprint, ModuleGraphRecord&& graph,
      ModuleGraphSccRecord&& scc, zc::Vector<StableMaterializedDependencyWitness>&& requestEdges,
      binder::ModuleGraphRevision&& graphRevision);
  ZC_NODISCARD static zc::Maybe<MaterializedModuleGraphWitness> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD MaterializedModuleGraphWitness clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const identity::SemanticContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const ModuleGraphRecord& graph() const noexcept;
  ZC_NODISCARD const ModuleGraphSccRecord& scc() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const StableMaterializedDependencyWitness> requestEdges()
      const noexcept;
  ZC_NODISCARD const binder::ModuleGraphRevision& graphRevision() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;
  ZC_NODISCARD bool sameAs(const MaterializedModuleGraphWitness& other) const;

private:
  struct Impl;
  explicit MaterializedModuleGraphWitness(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Retained revision-local module graph with stable authority and typed handles.
class MaterializedModuleGraph final {
public:
  ~MaterializedModuleGraph() noexcept(false);
  MaterializedModuleGraph(MaterializedModuleGraph&&) noexcept;
  MaterializedModuleGraph& operator=(MaterializedModuleGraph&&) noexcept;
  ZC_DISALLOW_COPY(MaterializedModuleGraph);

  ZC_NODISCARD static zc::Maybe<MaterializedModuleGraph> from(
      identity::SemanticContextBrand context, query::DatabaseRevision revision,
      MaterializedModuleGraphWitness&& witness,
      zc::Vector<MaterializedCompilationUnitEntry>&& units,
      zc::Vector<MaterializedCrateEntry>&& crates, zc::Vector<MaterializedSourceEntry>&& sources,
      zc::Vector<MaterializedModuleEntry>&& modules,
      zc::Vector<MaterializedModuleDependencyEdge>&& requestEdges);
  ZC_NODISCARD MaterializedModuleGraph clone() const;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const MaterializedModuleGraphWitness& witness() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedCompilationUnitEntry> units() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedCrateEntry> crates() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedSourceEntry> sources() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedModuleEntry> modules() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const MaterializedModuleDependencyEdge> requestEdges() const noexcept;

private:
  struct Impl;
  explicit MaterializedModuleGraph(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Logical-const four-domain identity service retained by the capability arena.
class ModuleGraphIdentityMaterializationResources
    : public query::SemanticContextCapabilityResources {
public:
  ~ModuleGraphIdentityMaterializationResources() override = default;
  ZC_DISALLOW_COPY_AND_MOVE(ModuleGraphIdentityMaterializationResources);

  ZC_NODISCARD virtual identity::SemanticContextBrand semanticContext() const noexcept = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::CompilationUnitId>
  internCompilationUnit(identity::SemanticContextBrand context,
                        const identity::CompilationUnitIdentity& key) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::CrateId> internCrate(
      identity::SemanticContextBrand context, const identity::CrateKey& key) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::SourceFileId> internSourceFile(
      identity::SemanticContextBrand context, const identity::SourceFileKey& key) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::ModuleId> internModule(
      identity::SemanticContextBrand context, const identity::ModuleKey& key) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::CompilationUnitIdentityEntry> compilationUnit(
      identity::CompilationUnitId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::CrateIdentityEntry> crate(
      identity::CrateId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::SourceFileIdentityEntry> sourceFile(
      identity::SourceFileId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::ModuleIdentityEntry> module(
      identity::ModuleId handle) const = 0;

protected:
  ModuleGraphIdentityMaterializationResources() = default;
};

/// \brief Final-sealed retained materializer for one complete compilation root set.
struct MaterializeModuleGraphQuery final {
  using Key = incremental_binding_query::CompilationRootSetQueryKey;
  using Capability = MaterializedModuleGraph;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "MaterializeModuleGraphQuery"_zcc, "zom.query.materialize-module-graph"_zcc,
      query::RetentionClass::Retained,   query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,     query::CapabilityAdmission::FinalSealedSnapshot};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<MaterializeModuleGraphQuery> provide(
      query::CapabilityQueryContext<MaterializeModuleGraphQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<MaterializeModuleGraphQuery>& context, const Key& key,
      const Capability& candidate);
};

}  // namespace zomlang::compiler::driver::module_graph_query

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<driver::module_graph_query::MaterializeModuleGraphQuery> final {
public:
  using Descriptor = driver::module_graph_query::MaterializeModuleGraphQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::MaterializeModuleGraphQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::module_graph_query::MaterializeModuleGraphQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::MaterializeModuleGraphQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::module_graph_query::MaterializeModuleGraphQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

template <>
struct ActiveMaterialization<identity::CompilationUnitIdentity> final {
  using Handle = identity::CompilationUnitId;
  using Record = driver::incremental_binding_query::ActiveCompilationUnitMembership;
  using Resource = driver::module_graph_query::ModuleGraphIdentityMaterializationResources;
  ZC_NODISCARD static TypedQueryResult<Handle> materialize(
      const Resource& resources, const identity::CompilationUnitIdentity& key,
      const Record& record);
};

template <>
struct ActiveMaterialization<identity::CrateKey> final {
  using Handle = identity::CrateId;
  using Record = identity::CrateKey;
  using Resource = driver::module_graph_query::ModuleGraphIdentityMaterializationResources;
  ZC_NODISCARD static TypedQueryResult<Handle> materialize(const Resource& resources,
                                                           const identity::CrateKey& key,
                                                           const Record& record);
};

template <>
struct ActiveMaterialization<identity::SourceFileKey> final {
  using Handle = identity::SourceFileId;
  using Record = identity::SourceFileKey;
  using Resource = driver::module_graph_query::ModuleGraphIdentityMaterializationResources;
  ZC_NODISCARD static TypedQueryResult<Handle> materialize(const Resource& resources,
                                                           const identity::SourceFileKey& key,
                                                           const Record& record);
};

template <>
struct ActiveMaterialization<identity::ModuleKey> final {
  using Handle = identity::ModuleId;
  using Record = identity::ModuleKey;
  using Resource = driver::module_graph_query::ModuleGraphIdentityMaterializationResources;
  ZC_NODISCARD static TypedQueryResult<Handle> materialize(const Resource& resources,
                                                           const identity::ModuleKey& key,
                                                           const Record& record);
};

#define ZOM_DECLARE_GRAPH_MATERIALIZER_PERMISSION(GlobalKey, Membership)                       \
  template <>                                                                                  \
  struct ActiveMaterializerPermission<driver::module_graph_query::MaterializeModuleGraphQuery, \
                                      GlobalKey,                                               \
                                      driver::incremental_binding_query::Membership##Query>    \
      final {                                                                                  \
    static constexpr bool allowed = true;                                                      \
  }

ZOM_DECLARE_GRAPH_MATERIALIZER_PERMISSION(identity::CompilationUnitIdentity,
                                          ActiveCompilationUnitMembership);
ZOM_DECLARE_GRAPH_MATERIALIZER_PERMISSION(identity::CrateKey, ActiveCrateMembership);
ZOM_DECLARE_GRAPH_MATERIALIZER_PERMISSION(identity::SourceFileKey, ActiveSourceMembership);
ZOM_DECLARE_GRAPH_MATERIALIZER_PERMISSION(identity::ModuleKey, ActiveModuleMembership);

#undef ZOM_DECLARE_GRAPH_MATERIALIZER_PERMISSION

}  // namespace zomlang::compiler::query
