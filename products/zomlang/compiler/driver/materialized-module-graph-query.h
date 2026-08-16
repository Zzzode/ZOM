// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#pragma once

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/binder/metadata/immutable-binding-metadata.h"
#include "zomlang/compiler/binder/metadata/immutable-definition-inventory.h"
#include "zomlang/compiler/binder/surface/materialized-export-surface-verifier.h"
#include "zomlang/compiler/binder/graph/materialized-module-skeleton.h"
#include "zomlang/compiler/binder/graph/module-graph-revision.h"
#include "zomlang/compiler/binder/graph/parsed-module.h"
#include "zomlang/compiler/diagnostics/fact/diagnostic-fact.h"
#include "zomlang/compiler/driver/active-identity-membership-query.h"
#include "zomlang/compiler/driver/module-dependency-provenance-query.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical/identity-interner-set.h"
#include "zomlang/compiler/identity/materialized-identity-entry.h"
#include "zomlang/compiler/identity/semantic/context-fingerprint.h"
#include "zomlang/compiler/parser/query/parse-source-query.h"
#include "zomlang/compiler/query/query-database.h"

namespace zomlang::compiler::binder {
class NamedItemProvenance;
class OwnerBodyProvenance;
class OwnerBodySyntax;
class RevisionLocalDefinitionSites;
class RevisionLocalImplementationSites;
class StableIdentityAdmission;
}  // namespace zomlang::compiler::binder

namespace zomlang::compiler::driver::module_graph_query {

template <typename Key, typename Record, typename Handle>
using MaterializedIdentityEntry = identity::MaterializedIdentityEntry<Key, Record, Handle>;

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

using MaterializedCompilationUnitEntry =
    identity::MaterializedIdentityEntry<identity::CompilationUnitIdentity,
                                        identity::CompilationUnitIdentity,
                                        identity::CompilationUnitId>;
using MaterializedCrateEntry =
    identity::MaterializedIdentityEntry<identity::CrateKey, identity::CrateKey, identity::CrateId>;
using MaterializedSourceEntry =
    identity::MaterializedIdentityEntry<identity::SourceFileKey, identity::SourceFileKey,
                                        identity::SourceFileId>;
using MaterializedModuleEntry =
    identity::MaterializedIdentityEntry<identity::ModuleKey, identity::ModuleKey,
                                        identity::ModuleId>;

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
      identity::ContextFingerprint&& fingerprint, ModuleGraphRecord&& graph,
      ModuleGraphSccRecord&& scc, zc::Vector<StableMaterializedDependencyWitness>&& requestEdges,
      binder::ModuleGraphRevision&& graphRevision);
  ZC_NODISCARD static zc::Maybe<MaterializedModuleGraphWitness> decodeCanonical(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD MaterializedModuleGraphWitness clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
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

/// \brief Retained module skeleton identity expansion bound to one sealed context.
class MaterializedModuleSkeleton final {
public:
  using GraphLease = query::QueryCapabilityLease<const MaterializedModuleGraph>;
  using DependencySkeletonLease = query::QueryCapabilityLease<const MaterializedModuleSkeleton>;
  using DependencyProvenanceLease =
      query::QueryCapabilityLease<const module_graph_query::ModuleDependencyProvenanceMap>;
  using IdentityAdmissionLease = query::QueryCapabilityLease<const binder::StableIdentityAdmission>;
  using DefinitionProvenanceLease = query::QueryCapabilityLease<const binder::NamedItemProvenance>;
  using DefinitionSitesLease =
      query::QueryCapabilityLease<const binder::RevisionLocalDefinitionSites>;
  using ImplementationSitesLease =
      query::QueryCapabilityLease<const binder::RevisionLocalImplementationSites>;

  ~MaterializedModuleSkeleton() noexcept(false);
  MaterializedModuleSkeleton(MaterializedModuleSkeleton&&) noexcept;
  MaterializedModuleSkeleton& operator=(MaterializedModuleSkeleton&&) noexcept;
  ZC_DISALLOW_COPY(MaterializedModuleSkeleton);

  ZC_NODISCARD static zc::Maybe<MaterializedModuleSkeleton> from(
      incremental_binding_query::ContextualModuleKey&& key, GraphLease&& graph,
      zc::Vector<DependencySkeletonLease>&& dependencySkeletons, identity::SourceFileKey&& source,
      binder::ModuleBodyProvenance&& provenance, DependencyProvenanceLease&& dependencyProvenance,
      binder::MaterializedModuleSkeletonIdentities&& identities,
      IdentityAdmissionLease&& identityAdmission, DefinitionSitesLease&& definitionSites,
      ImplementationSitesLease&& implementationSites,
      zc::Vector<DefinitionProvenanceLease>&& definitionProvenances,
      const parser::CanonicalParsedSource& parsedSource);
  ZC_NODISCARD MaterializedModuleSkeleton clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const binder::ModuleBodyProvenance& provenance() const noexcept;
  ZC_NODISCARD const DependencyProvenanceLease& dependencyProvenanceLease() const noexcept;
  ZC_NODISCARD const binder::MaterializedModuleSkeletonIdentities& identities() const noexcept;
  ZC_NODISCARD const IdentityAdmissionLease& identityAdmissionLease() const noexcept;
  ZC_NODISCARD const GraphLease& graphLease() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DependencySkeletonLease> dependencySkeletonLeases()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::MaterializedDependencyExportSurface> dependencySurfaces()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedDependencyExportSurface&> preludeSurface()
      const noexcept;
  ZC_NODISCARD const DefinitionSitesLease& definitionSitesLease() const noexcept;
  ZC_NODISCARD const ImplementationSitesLease& implementationSitesLease() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const DefinitionProvenanceLease> definitionProvenanceLeases()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::DefinitionFact> materializedDefinitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::GenericParameterFact> materializedGenericParameters()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::CallableParameterFact> materializedCallableParameters()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ModuleAliasBindingFact> materializedModuleAliases()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ImportBindingFact> materializedImports() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::LocalExportFact> materializedLocalExports()
      const noexcept;
  ZC_NODISCARD const binder::VerifiedExportSurface& bindingSurface() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ImplBindingFact> materializedImplementations()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ScopeRecord> materializedScopes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::NodeScopeFact> materializedNodeScopes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::MaterializedFailedLookupFact> materializedFailedLookups()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ScopeId> scopeIdentities() const noexcept;
  ZC_NODISCARD zc::Maybe<binder::ScopeId> scopeFor(
      const binder::StableScopeOwnerKey& scope) const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

  /// \brief Rebuilds materialized owner-body facts from retained stable evidence.
  ZC_NODISCARD static zc::Maybe<zc::Vector<binder::ScopeId>> materializeScopeIdentities(
      identity::ModuleId module, const binder::BoundModuleSkeleton& stableWitness);
  struct Impl;
  explicit MaterializedModuleSkeleton(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Retained owner-body fact expansion bound to one sealed context.
class MaterializedOwnerBody final {
public:
  using SkeletonLease = query::QueryCapabilityLease<const MaterializedModuleSkeleton>;
  using ProvenanceLease = query::QueryCapabilityLease<const binder::OwnerBodyProvenance>;

  ~MaterializedOwnerBody() noexcept(false);
  MaterializedOwnerBody(MaterializedOwnerBody&&) noexcept;
  MaterializedOwnerBody& operator=(MaterializedOwnerBody&&) noexcept;
  ZC_DISALLOW_COPY(MaterializedOwnerBody);

  ZC_NODISCARD static zc::Maybe<MaterializedOwnerBody> from(
      incremental_binding_query::ContextualBodyOwnerKey&& key,
      identity::SemanticContextBrand context, query::DatabaseRevision revision,
      identity::ContextFingerprint&& fingerprint, identity::ModuleId module,
      SkeletonLease&& skeleton, identity::SourceFileKey&& source, ProvenanceLease&& provenance,
      binder::BoundOwnerBody&& stableWitness, binder::OwnerAllocationRange&& allocation,
      const binder::OwnerBodySyntax& syntax, const parser::CanonicalParsedSource& parsedSource);
  ZC_NODISCARD MaterializedOwnerBody clone() const;
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const binder::StableOwnerBodyQueryKey& owner() const noexcept;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const SkeletonLease& skeletonLease() const noexcept;
  ZC_NODISCARD const ProvenanceLease& provenanceLease() const noexcept;
  ZC_NODISCARD const binder::BoundOwnerBody& stableWitness() const noexcept;
  ZC_NODISCARD const binder::OwnerAllocationRange& allocation() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ScopeId> scopeIdentities() const noexcept;
  ZC_NODISCARD zc::Maybe<binder::ScopeId> scopeFor(
      const binder::StableScopeOwnerKey& scope) const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::NodeScopeFact> materializedNodeScopes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::OwnerLocalBindingFact> materializedOwnerLocalBindings()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::AnonymousEntityFact> materializedAnonymousEntities()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::BindingResolution> materializedResolutions()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::BoundSelfType> materializedSelfTypes() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::BoundThis> materializedThisBindings() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ShadowTargetFact> materializedShadowTargets()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::LabelFact> materializedLabels() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ControlTransferFact> materializedControlTransfers()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ClosureFreeVariableFact>
  materializedClosureFreeVariables() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ExplicitClosureCaptureFact>
  materializedExplicitClosureCaptures() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::MaterializedFailedLookupFact> materializedFailedLookups()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::DeferredMemberFact> materializedDeferredMembers()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableBodyScopeFact>& scopes()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableBodyNodeScopeFact>& nodeScopes()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableOwnerLocalBindingFact>& bindings()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableResolutionFact>& resolutions()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableDeferredMemberFact>& deferredMembers()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableSelfTypeFact>& selfTypes()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableThisBindingFact>& thisBindings()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableShadowTargetFact>& shadowTargets()
      const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableLabelFact>& labels() const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableControlTransferFact>&
  controlTransfers() const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableClosureFreeVariableFact>&
  closureFreeVariables() const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableExplicitClosureCaptureFact>&
  explicitClosureCaptures() const noexcept;
  ZC_NODISCARD const binder::CanonicalSequence<binder::StableFailedLookupFact>& failedLookups()
      const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

public:
  /// \brief Rebuilds materialized owner-body facts from retained stable evidence.
  ZC_NODISCARD static zc::Maybe<zc::Vector<binder::ScopeId>> materializeScopeIdentities(
      identity::ModuleId module, const binder::OwnerAllocationRange& allocation,
      const binder::BoundOwnerBody& stableWitness);
  ZC_NODISCARD static zc::Maybe<zc::Vector<binder::NodeScopeFact>> materializeNodeScopes(
      const MaterializedModuleSkeleton& skeleton, const binder::OwnerBodyProvenance& provenance,
      const binder::BoundOwnerBody& stableWitness,
      zc::ArrayPtr<const binder::ScopeId> scopeIdentities);
  ZC_NODISCARD static zc::Maybe<zc::Vector<binder::OwnerLocalBindingFact>>
  materializeOwnerLocalBindingFacts(const MaterializedModuleSkeleton& skeleton,
                                    const identity::SourceFileKey& source,
                                    const binder::OwnerBodyProvenance& provenance,
                                    const binder::BoundOwnerBody& stableWitness,
                                    zc::ArrayPtr<const binder::ScopeId> scopeIdentities,
                                    zc::ArrayPtr<const binder::OwnerLocalBindingId> identities,
                                    const binder::OwnerBodySyntax& syntax,
                                    const parser::CanonicalParsedSource& parsedSource);
  ZC_NODISCARD static zc::Maybe<zc::Vector<binder::AnonymousEntityFact>>
  materializeAnonymousEntities(identity::SemanticContextBrand context, identity::ModuleId module,
                               const binder::OwnerAllocationRange& allocation,
                               const identity::SourceFileKey& source,
                               const binder::OwnerBodyProvenance& provenance,
                               const binder::BoundOwnerBody& stableWitness,
                               const parser::CanonicalParsedSource& parsedSource);
  ZC_NODISCARD static zc::Maybe<zc::Vector<binder::LabelFact>> materializeLabels(
      identity::ModuleId module, const MaterializedModuleSkeleton& skeleton,
      const identity::SourceFileKey& source, const binder::OwnerBodyProvenance& provenance,
      const binder::BoundOwnerBody& stableWitness, const binder::OwnerAllocationRange& allocation,
      zc::ArrayPtr<const binder::ScopeId> scopeIdentities,
      const parser::CanonicalParsedSource& parsedSource);
  ZC_NODISCARD static zc::Maybe<zc::Vector<binder::ControlTransferFact>>
  materializeControlTransfers(const MaterializedModuleSkeleton& skeleton,
                              const identity::SourceFileKey& source,
                              const binder::OwnerBodyProvenance& provenance,
                              const binder::BoundOwnerBody& stableWitness,
                              zc::ArrayPtr<const binder::ScopeId> scopeIdentities,
                              zc::ArrayPtr<const binder::LabelFact> labels,
                              const parser::CanonicalParsedSource& parsedSource);

private:
  struct Impl;
  explicit MaterializedOwnerBody(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Retained aggregate that owns the complete verified binding lease lineage.
class VerifiedBoundModule final {
public:
  using GraphLease = query::QueryCapabilityLease<const MaterializedModuleGraph>;
  using SkeletonLease = query::QueryCapabilityLease<const MaterializedModuleSkeleton>;
  using ParsedSourceLease = query::QueryCapabilityLease<const parser::CanonicalParsedSource>;
  using OwnerBodyLease = query::QueryCapabilityLease<const MaterializedOwnerBody>;

  ~VerifiedBoundModule() noexcept(false);
  VerifiedBoundModule(VerifiedBoundModule&&) noexcept;
  VerifiedBoundModule& operator=(VerifiedBoundModule&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedBoundModule);

  ZC_NODISCARD static zc::Maybe<VerifiedBoundModule> from(GraphLease&& graph,
                                                          SkeletonLease&& skeleton,
                                                          identity::SourceFileKey&& source,
                                                          ParsedSourceLease&& parsedSource,
                                                          zc::Vector<OwnerBodyLease>&& ownerBodies);
  ZC_NODISCARD const incremental_binding_query::CompilationRootSetQueryKey& contextRoots()
      const noexcept;
  ZC_NODISCARD const identity::ModuleKey& module() const noexcept;
  ZC_NODISCARD identity::SemanticContextBrand context() const noexcept;
  ZC_NODISCARD query::DatabaseRevision revision() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& fingerprint() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD const identity::SourceFileKey& source() const noexcept;
  ZC_NODISCARD const GraphLease& graphLease() const noexcept;
  ZC_NODISCARD const SkeletonLease& skeletonLease() const noexcept;
  ZC_NODISCARD const ParsedSourceLease& parsedSourceLease() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const OwnerBodyLease> ownerBodyLeases() const noexcept;
  ZC_NODISCARD const binder::ImmutableDefinitionInventory& definitions() const noexcept;
  ZC_NODISCARD const binder::ImmutableBindingMetadata& bindings() const noexcept;
  ZC_NODISCARD const binder::VerifiedExportSurface& bindingSurface() const noexcept;
  ZC_NODISCARD zc::Array<uint8_t> encodeCanonical() const;

private:
  struct Impl;
  explicit VerifiedBoundModule(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
};

/// \brief Logical-const eight-domain identity service retained by the capability arena.
class ModuleGraphIdentityMaterializationResources
    : public query::SemanticContextCapabilityResources {
public:
  ~ModuleGraphIdentityMaterializationResources() override = default;
  ZC_DISALLOW_COPY_AND_MOVE(ModuleGraphIdentityMaterializationResources);

  ZC_NODISCARD virtual identity::SemanticContextBrand semanticContext() const noexcept = 0;
  ZC_NODISCARD virtual identity::IdentityInternerSet& identityInterners() const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::CompilationUnitId>
  internCompilationUnit(identity::SemanticContextBrand context,
                        const identity::CompilationUnitIdentity& key) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::CrateId> internCrate(
      identity::SemanticContextBrand context, const identity::CrateKey& key) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::SourceFileId> internSourceFile(
      identity::SemanticContextBrand context, const identity::SourceFileKey& key) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::ModuleId> internModule(
      identity::SemanticContextBrand context, const identity::ModuleKey& key) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::DefId> internDefinition(
      identity::SemanticContextBrand context, const identity::DefinitionKey& key,
      const identity::DefinitionIdentityRecord& record) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::ImplId> internImplementation(
      identity::SemanticContextBrand context, const identity::ImplKey& key,
      const identity::ImplIdentityRecord& record) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::GenericParameterId>
  internGenericParameter(identity::SemanticContextBrand context,
                         const identity::GenericParameterKey& key,
                         const identity::GenericParameterIdentityRecord& record) const = 0;
  ZC_NODISCARD virtual identity::IdentityInternResult<identity::CallableParameterId>
  internCallableParameter(identity::SemanticContextBrand context,
                          const identity::CallableParameterKey& key,
                          const identity::CallableParameterIdentityRecord& record) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::CompilationUnitIdentityEntry> compilationUnit(
      identity::CompilationUnitId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::CrateIdentityEntry> crate(
      identity::CrateId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::SourceFileIdentityEntry> sourceFile(
      identity::SourceFileId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::ModuleIdentityEntry> module(
      identity::ModuleId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::DefinitionIdentityEntry> definition(
      identity::DefId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::ImplementationIdentityEntry> implementation(
      identity::ImplId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::GenericParameterIdentityEntry> genericParameter(
      identity::GenericParameterId handle) const = 0;
  ZC_NODISCARD virtual zc::Maybe<identity::CallableParameterIdentityEntry> callableParameter(
      identity::CallableParameterId handle) const = 0;

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
      "MaterializeModuleGraphQuery"_zcc,
      "zom.query.materialize-module-graph"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<MaterializeModuleGraphQuery> provide(
      query::CapabilityQueryContext<MaterializeModuleGraphQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<MaterializeModuleGraphQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Final-sealed retained materializer for one contextual module skeleton.
struct MaterializeModuleSkeletonQuery final {
  using Key = incremental_binding_query::ContextualModuleKey;
  using Capability = MaterializedModuleSkeleton;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "MaterializeModuleSkeletonQuery"_zcc,
      "zom.query.materialize-module-skeleton"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<MaterializeModuleSkeletonQuery> provide(
      query::CapabilityQueryContext<MaterializeModuleSkeletonQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<MaterializeModuleSkeletonQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Final-sealed retained materializer for one contextual owner body.
struct MaterializeOwnerBodyQuery final {
  using Key = incremental_binding_query::ContextualBodyOwnerKey;
  using Capability = MaterializedOwnerBody;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "MaterializeOwnerBodyQuery"_zcc,
      "zom.query.materialize-owner-body"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<MaterializeOwnerBodyQuery> provide(
      query::CapabilityQueryContext<MaterializeOwnerBodyQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<MaterializeOwnerBodyQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Final-sealed retained aggregation of the graph, skeleton, and owner-body leases.
struct VerifyBoundModuleQuery final {
  using Key = incremental_binding_query::ContextualModuleKey;
  using Capability = VerifiedBoundModule;
  using FailureAlternatives =
      query::CapabilityFailureList<query::SourceRejection<diagnostics::DiagnosticFact>,
                                   query::KeyRejection<binder::BinderKeyFailure>>;

  static constexpr query::CapabilityDescriptorMetadata descriptor{
      "VerifyBoundModuleQuery"_zcc,
      "zom.query.verify-bound-module"_zcc,
      query::RetentionClass::Retained,
      query::QueryCyclePolicy::Reject,
      query::QueryCostClass::Linear,
      query::CapabilityAdmission::FinalSealedSnapshot,
      query::FinalFailureProjection::SourceOrKey};
  ZC_NODISCARD static zc::Array<uint8_t> encodeKey(const Key& key);
  ZC_NODISCARD static zc::Maybe<Key> decodeKey(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static query::CapabilityProviderResult<VerifyBoundModuleQuery> provide(
      query::CapabilityQueryContext<VerifyBoundModuleQuery>& context, const Key& key);
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> verify(
      query::CapabilityQueryContext<VerifyBoundModuleQuery>& context, const Key& key,
      const Capability& candidate);
};

/// \brief Checker handoff retaining one verified bound-module capability lease.
class CheckerBoundModuleView final {
public:
  using BoundModuleLease = query::QueryCapabilityLease<const VerifiedBoundModule>;

  ~CheckerBoundModuleView() noexcept(false);
  CheckerBoundModuleView(CheckerBoundModuleView&&) noexcept;
  CheckerBoundModuleView& operator=(CheckerBoundModuleView&&) noexcept;
  ZC_DISALLOW_COPY(CheckerBoundModuleView);

  ZC_NODISCARD static zc::Maybe<CheckerBoundModuleView> from(BoundModuleLease&& lease);
  ZC_NODISCARD CheckerBoundModuleView retain() const;
  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD identity::CompilationUnitId compilationUnit() const noexcept;
  ZC_NODISCARD identity::CrateId crate() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD identity::SourceFileId sourceFile() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& semanticFingerprint() const noexcept;
  ZC_NODISCARD const ast::Tree& tree() const noexcept;
  ZC_NODISCARD const binder::CanonicalParsedModule& parsedModule() const noexcept;
  ZC_NODISCARD const binder::ImmutableDefinitionInventory& definitions() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::MaterializedDependencyExportSurface> dependencySurfaces()
      const noexcept;
  ZC_NODISCARD zc::Maybe<const binder::MaterializedDependencyExportSurface&> preludeSurface()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ImportBindingFact> resolvedImports() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const binder::ModuleAliasBindingFact> resolvedModuleAliases()
      const noexcept;
  ZC_NODISCARD const binder::ImmutableBindingMetadata& bindings() const noexcept;
  ZC_NODISCARD const binder::VerifiedExportSurface& bindingSurface() const noexcept;
  ZC_NODISCARD const BoundModuleLease& boundModuleLease() const noexcept;

private:
  struct Impl;
  explicit CheckerBoundModuleView(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;
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
class CapabilityCandidateContract<driver::module_graph_query::MaterializeModuleSkeletonQuery>
    final {
public:
  using Descriptor = driver::module_graph_query::MaterializeModuleSkeletonQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityCandidateContract<driver::module_graph_query::MaterializeOwnerBodyQuery> final {
public:
  using Descriptor = driver::module_graph_query::MaterializeOwnerBodyQuery;
  ZC_NODISCARD static StableWitnessBytes encode(const Descriptor::Capability& candidate);
  ZC_NODISCARD static zc::Maybe<zc::Own<Descriptor::Capability>> decode(
      zc::ArrayPtr<const uint8_t> bytes);
};

template <>
class CapabilityCandidateContract<driver::module_graph_query::VerifyBoundModuleQuery> final {
public:
  using Descriptor = driver::module_graph_query::VerifyBoundModuleQuery;
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
class CapabilityFailureContract<driver::module_graph_query::MaterializeModuleSkeletonQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::module_graph_query::MaterializeModuleSkeletonQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::MaterializeModuleSkeletonQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::module_graph_query::MaterializeModuleSkeletonQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::MaterializeOwnerBodyQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::module_graph_query::MaterializeOwnerBodyQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::MaterializeOwnerBodyQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::module_graph_query::MaterializeOwnerBodyQuery;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const binder::BinderKeyFailure& failure);
  ZC_NODISCARD static zc::Maybe<binder::BinderKeyFailure> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const binder::BinderKeyFailure& failure);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::VerifyBoundModuleQuery,
                                SourceRejection<diagnostics::DiagnosticFact>>
    final {
public:
  using Descriptor = driver::module_graph_query::VerifyBoundModuleQuery;
  using Sequence = CanonicalNonEmptySequence<diagnostics::DiagnosticFact>;
  ZC_NODISCARD static zc::Array<uint8_t> encode(const Sequence& diagnostics);
  ZC_NODISCARD static zc::Maybe<Sequence> decode(zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static CapabilityRejectionCheck verify(CapabilityQueryContext<Descriptor>& context,
                                                      const Descriptor::Key& key,
                                                      const Sequence& diagnostics);
};

template <>
class CapabilityFailureContract<driver::module_graph_query::VerifyBoundModuleQuery,
                                KeyRejection<binder::BinderKeyFailure>>
    final {
public:
  using Descriptor = driver::module_graph_query::VerifyBoundModuleQuery;
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

template <>
struct ActiveMaterialization<identity::DefinitionKey> final {
  using Handle = identity::DefId;
  using Record = identity::DefinitionIdentityRecord;
  using Resource = driver::module_graph_query::ModuleGraphIdentityMaterializationResources;
  ZC_NODISCARD static TypedQueryResult<Handle> materialize(const Resource& resources,
                                                           const identity::DefinitionKey& key,
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

#define ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(GlobalKey, Membership)                       \
  template <>                                                                                     \
  struct ActiveMaterializerPermission<driver::module_graph_query::MaterializeModuleSkeletonQuery, \
                                      GlobalKey,                                                  \
                                      driver::incremental_binding_query::Membership##Query>       \
      final {                                                                                     \
    static constexpr bool allowed = true;                                                         \
  }

ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::CompilationUnitIdentity,
                                             ActiveCompilationUnitMembership);
ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::CrateKey, ActiveCrateMembership);
ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::SourceFileKey, ActiveSourceMembership);
ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::ModuleKey, ActiveModuleMembership);
ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::DefinitionKey, ActiveDefinitionMembership);
ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::ImplKey, ActiveImplementationMembership);
ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::GenericParameterKey,
                                             ActiveGenericParameterMembership);
ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION(identity::CallableParameterKey,
                                             ActiveCallableParameterMembership);

#undef ZOM_DECLARE_SKELETON_MATERIALIZER_PERMISSION

#define ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(GlobalKey, Membership)                \
  template <>                                                                                \
  struct ActiveMaterializerPermission<driver::module_graph_query::MaterializeOwnerBodyQuery, \
                                      GlobalKey,                                             \
                                      driver::incremental_binding_query::Membership##Query>  \
      final {                                                                                \
    static constexpr bool allowed = true;                                                    \
  }

ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::CompilationUnitIdentity,
                                               ActiveCompilationUnitMembership);
ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::CrateKey, ActiveCrateMembership);
ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::SourceFileKey, ActiveSourceMembership);
ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::ModuleKey, ActiveModuleMembership);
ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::DefinitionKey, ActiveDefinitionMembership);
ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::ImplKey, ActiveImplementationMembership);
ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::GenericParameterKey,
                                               ActiveGenericParameterMembership);
ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION(identity::CallableParameterKey,
                                               ActiveCallableParameterMembership);

#undef ZOM_DECLARE_OWNER_BODY_MATERIALIZER_PERMISSION

}  // namespace zomlang::compiler::query
