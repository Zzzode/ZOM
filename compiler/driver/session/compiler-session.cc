// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "compiler/driver/session/compiler-session.h"

#include "zc/core/encoding.h"
#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "compiler/ast/generated/node-traverse.h"
#include "compiler/ast/tree.h"
#include "compiler/basic/compiler-opts.h"
#include "compiler/basic/string-pool.h"
#include "compiler/basic/thread-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/binder/diagnostics/binding-diagnostic-adapter.h"
#include "compiler/binder/diagnostics/module-graph-diagnostic-adapter.h"
#include "compiler/binder/graph/module-dependency-requests.h"
#include "compiler/binder/graph/module-graph-source-failure.h"
#include "compiler/binder/graph/parsed-module-graph-input.h"
#include "compiler/binder/identity/local-identity.h"
#include "compiler/binder/metadata/definition-inventory.h"
#include "compiler/binder/stable/candidate/producer.h"
#include "compiler/binder/stable/candidate/verifier.h"
#include "compiler/checker/body/body-checker.h"
#include "compiler/checker/borrow/borrow-interface-diagnostic-adapter.h"
#include "compiler/checker/checker-identity-authority.h"
#include "compiler/checker/diagnostics/checker-diagnostic-adapter.h"
#include "compiler/checker/facts/checked-facts-repository.h"
#include "compiler/checker/facts/coherence-facts.h"
#include "compiler/checker/facts/cross-module-facts.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/diagnostics/consumer/consoling-diagnostic-consumer.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/diagnostics/core/diagnostic-ids.h"
#include "compiler/diagnostics/core/diagnostic.h"
#include "compiler/diagnostics/fact/diagnostic-materializer.h"
#include "compiler/driver/core/query.h"
#include "compiler/driver/graph/module-discovery.h"
#include "compiler/driver/interface/coherence-builder.h"
#include "compiler/driver/interface/imported-signature-view-projector.h"
#include "compiler/driver/interface/module-interface-diagnostic-adapter.h"
#include "compiler/driver/package/package-diagnostic.h"
#include "compiler/driver/query/binding/active-definition-authority-query.h"
#include "compiler/driver/query/binding/active-definition-authority-session.h"
#include "compiler/driver/query/binding/incremental-binding-query-adapter.h"
#include "compiler/driver/query/binding/incremental-package-graph-query-input.h"
#include "compiler/driver/query/binding/named-identity-inventory-query.h"
#include "compiler/driver/query/binding/named-item-query.h"
#include "compiler/driver/query/module-graph/incremental-module-resolution-query.h"
#include "compiler/driver/query/module-graph/materialized-module-graph-query.h"
#include "compiler/driver/query/module-graph/module-graph-query-input.h"
#include "compiler/driver/query/module-graph/module-graph-query.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/canonical/identity-interner-set.h"
#include "compiler/identity/identity-diagnostic-adapter.h"
#include "compiler/ownership/drop-elaborated-mir.h"
#include "compiler/ownership/ownership-checked-mir.h"
#include "compiler/ownership/ownership-diagnostic-adapter.h"
#include "compiler/ownership/ownership-proof-validation.h"
#include "compiler/ownership/surface-admission.h"
#include "compiler/parser/query/parse-source-query.h"
#include "compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace driver {
namespace {

namespace source_query = identity::source_query;

bool sameBytes(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  return left == right;
}

bool publishSourceDiagnostics(zc::ArrayPtr<const diagnostics::DiagnosticFact> facts,
                              const diagnostics::SourceDiagnosticProvenanceMap& provenance,
                              const identity::SourceFileKey& sourceKey,
                              source::SourceManager& sources, const source::BufferId& buffer,
                              diagnostics::DiagnosticEngine& engine) {
  diagnostics::SourceDiagnosticProvenanceResolver resolver(sourceKey, provenance);
  auto materialized = diagnostics::materializeDiagnosticFacts(facts, resolver, sources, buffer);
  if (materialized.is<diagnostics::DiagnosticMaterializationFailure>()) {
    engine.diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                               zc::str(uint64_t{1}));
    return false;
  }
  diagnostics::publishResolvedDiagnosticBatch(
      zc::mv(materialized.get<diagnostics::ResolvedDiagnosticBatch>()), engine);
  return true;
}

bool sameModulePath(zc::ArrayPtr<const identity::ModulePathSegment> left,
                    zc::ArrayPtr<const identity::ModulePathSegment> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) { return false; }
  }
  return true;
}

bool sameRelativePath(const identity::CanonicalRelativePath& left,
                      const identity::CanonicalRelativePath& right) {
  if (left.segments().size() != right.segments().size()) { return false; }
  for (size_t index = 0; index < left.segments().size(); ++index) {
    if (left.segments()[index] != right.segments()[index]) { return false; }
  }
  return true;
}

zc::Vector<identity::ModulePathSegment> cloneModulePath(
    zc::ArrayPtr<const identity::ModulePathSegment> path) {
  zc::Vector<identity::ModulePathSegment> result(path.size());
  for (const auto& segment : path) { result.add(segment.clone()); }
  return result;
}

zc::Array<uint8_t> encodeStructuralModulePath(
    const identity::CrateKey& crate, zc::ArrayPtr<const identity::ModulePathSegment> path) {
  identity::CanonicalEncoder encoder;
  crate.encode(encoder);
  encoder.encodeSequenceSize(path.size());
  for (const auto& segment : path) { segment.encode(encoder); }
  return encoder.finish();
}

identity::CanonicalRelativePath parentDirectory(const identity::CanonicalRelativePath& path) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  if (path.segments().size() != 0) {
    segments.reserve(path.segments().size() - 1);
    for (size_t index = 0; index + 1 < path.segments().size(); ++index) {
      segments.add(path.segments()[index].clone());
    }
  }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

zc::Array<uint8_t> targetSelectionBytes(const package::RegisteredTargetSelection& selection) {
  identity::CanonicalEncoder encoder;
  selection.encode(encoder);
  return encoder.finish();
}

bool sameTargetSelection(const package::RegisteredTargetSelection& left,
                         const package::RegisteredTargetSelection& right) {
  return sameBytes(targetSelectionBytes(left), targetSelectionBytes(right));
}

bool packageMatchesBase(const identity::PackageKey& package, const identity::PackageBaseKey& base) {
  identity::CanonicalEncoder packageSource;
  identity::CanonicalEncoder baseSource;
  package.source().encode(packageSource);
  base.source().encode(baseSource);
  return packageSource.finish().asPtr() == baseSource.finish().asPtr() &&
         package.name() == base.name() && package.version() == base.version();
}

bool samePackage(const identity::PackageKey& left, const identity::PackageKey& right) {
  return sameBytes(left.encode().asPtr(), right.encode().asPtr());
}

zc::String packageSourceIdentifier(const identity::PackageKey& package,
                                   const identity::CanonicalRelativePath& path) {
  zc::String result = zc::str(package.name(), "/");
  for (size_t index = 0; index < path.segments().size(); ++index) {
    if (index != 0) { result = zc::str(result, "/"); }
    result = zc::str(result, path.segments()[index].text());
  }
  return result;
}

zc::String generatedSourceIdentifier(const identity::PackageKey& package,
                                     const identity::CanonicalRelativePath& path) {
  zc::String result = zc::str(package.name(), "/<generated>");
  for (const auto& segment : path.segments()) { result = zc::str(result, "/", segment.text()); }
  return result;
}

zc::String coreSourceIdentifier(const identity::CanonicalRelativePath& path) {
  zc::String result = zc::str("<toolchain-core>");
  for (const auto& segment : path.segments()) { result = zc::str(result, "/", segment.text()); }
  return result;
}

bool graphContainsPackage(const package::ResolutionOutput& graph,
                          const identity::PackageKey& package) {
  const auto expected = package.encode();
  for (const auto& selected : graph.packages()) {
    if (sameBytes(expected, selected.key().encode())) { return true; }
  }
  return false;
}

bool graphAndSnapshotsMatch(const package::ResolutionOutput& graph,
                            zc::ArrayPtr<const package::ResolvedPackageSourceSnapshot> snapshots) {
  if (graph.packages().size() == 0 || snapshots.size() == 0) { return false; }
  for (const auto& selected : graph.packages()) {
    bool found = false;
    for (const auto& snapshot : snapshots) {
      if (packageMatchesBase(selected.key(), snapshot.package())) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  for (const auto& snapshot : snapshots) {
    bool found = false;
    for (const auto& selected : graph.packages()) {
      if (packageMatchesBase(selected.key(), snapshot.package())) {
        found = true;
        break;
      }
    }
    if (!found) { return false; }
  }
  return true;
}

zc::Maybe<identity::SourceOriginKey> sourceOriginFor(const identity::PackageKey& package,
                                                     const identity::CanonicalRelativePath& path) {
  const auto& packageSource = package.source();
  switch (packageSource.kind()) {
    case identity::PackageSourceKind::LocalPath: {
      zc::Vector<identity::CanonicalPathSegment> segments;
      for (const auto& segment : packageSource.localPath().segments()) {
        segments.add(segment.clone());
      }
      for (const auto& segment : path.segments()) { segments.add(segment.clone()); }
      return identity::SourceOriginKey::localFile(identity::CanonicalWorkspaceRelativePath::from(
          packageSource.localPath().leadingParents(), zc::mv(segments)));
    }
    case identity::PackageSourceKind::Registry:
      return identity::SourceOriginKey::registryFile(package.clone(), path.clone());
    case identity::PackageSourceKind::Vcs:
      return identity::SourceOriginKey::vcsFile(package.clone(), path.clone());
  }
  ZC_UNREACHABLE
}

template <typename Handle>
bool containsIdentityHandle(const zc::Vector<Handle>& handles, Handle candidate) {
  for (const auto handle : handles) {
    if (handle == candidate) { return true; }
  }
  return false;
}

}  // namespace
// ================================================================================
// VerifiedPackageSessionInput

struct VerifiedPackageSessionInput::Impl final {
  Impl(package::VerifiedPackageCompilationRequest&& request,
       ir::VerifiedTargetSelection&& hostTarget, ir::VerifiedTargetSelection&& target,
       package::ResolutionOutput&& graph, package::VerifiedBuildScriptPlan&& buildScriptPlan,
       zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots) noexcept
      : request(zc::mv(request)),
        hostTarget(zc::mv(hostTarget)),
        target(zc::mv(target)),
        graph(zc::mv(graph)),
        buildScriptPlan(zc::mv(buildScriptPlan)),
        snapshots(zc::mv(snapshots)) {}

  package::VerifiedPackageCompilationRequest request;
  ir::VerifiedTargetSelection hostTarget;
  ir::VerifiedTargetSelection target;
  package::ResolutionOutput graph;
  package::VerifiedBuildScriptPlan buildScriptPlan;
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
};

VerifiedPackageSessionInput::VerifiedPackageSessionInput(
    package::VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, package::ResolutionOutput&& graph,
    package::VerifiedBuildScriptPlan&& buildScriptPlan,
    zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots)
    : impl(zc::heap<Impl>(zc::mv(request), zc::mv(hostTarget), zc::mv(target), zc::mv(graph),
                          zc::mv(buildScriptPlan), zc::mv(snapshots))) {}

VerifiedPackageSessionInput::~VerifiedPackageSessionInput() noexcept(false) = default;
VerifiedPackageSessionInput::VerifiedPackageSessionInput(VerifiedPackageSessionInput&&) noexcept =
    default;
VerifiedPackageSessionInput& VerifiedPackageSessionInput::operator=(
    VerifiedPackageSessionInput&&) noexcept = default;

zc::Maybe<VerifiedPackageSessionInput> VerifiedPackageSessionInput::from(
    package::VerifiedPackageCompilationRequest&& request, ir::VerifiedTargetSelection&& hostTarget,
    ir::VerifiedTargetSelection&& target, package::ResolutionOutput&& graph,
    zc::Vector<package::ResolvedPackageSourceSnapshot>&& snapshots) {
  const auto& requestHost = request.hostTarget();
  const auto& requestTarget = request.target();
  const auto& verifiedHost = hostTarget.packageSelection();
  const auto& verifiedTarget = target.packageSelection();
  if (requestHost.registryRevision() != verifiedHost.registryRevision() ||
      requestTarget.registryRevision() != verifiedTarget.registryRevision() ||
      verifiedHost.registryRevision() != verifiedTarget.registryRevision() ||
      !sameTargetSelection(requestHost, verifiedHost) ||
      !sameTargetSelection(requestTarget, verifiedTarget) ||
      !graphAndSnapshotsMatch(graph, snapshots)) {
    return zc::none;
  }
  for (const auto& root : request.roots()) {
    if (!graphContainsPackage(graph, root.packageKey())) { return zc::none; }
  }
  auto plan = VerifiedPreparatoryCrateGraph::buildPlan(request, graph);
  if (!plan.is<package::VerifiedBuildScriptPlan>()) { return zc::none; }
  auto buildScriptPlan = zc::mv(plan.get<package::VerifiedBuildScriptPlan>());
  return VerifiedPackageSessionInput(zc::mv(request), zc::mv(hostTarget), zc::mv(target),
                                     zc::mv(graph), zc::mv(buildScriptPlan), zc::mv(snapshots));
}

namespace {

enum class SemanticContextResourceFailure : uint8_t {
  None = 0,
  ContextBrandExhausted = 1,
  IdentityInternerUnavailable = 2,
  RegistryBrandIssuerUnavailable = 3,
  SemanticTypeStoreUnavailable = 4,
};

class CompilerSessionSemanticContextResources final
    : public module_graph_query::ModuleGraphIdentityMaterializationResources {
public:
  identity::SemanticContextBrand contextBrand;
  mutable zc::Maybe<identity::IdentityInternerSet> identityInternerSet;
  zc::Own<type::SemanticTypeStore> semanticTypeStore;
  zc::Maybe<identity::RegistryBrandIssuer> factStoreBrands;

  identity::SemanticContextBrand semanticContext() const noexcept override { return contextBrand; }

  identity::IdentityInternerSet& identityInterners() const override {
    return ZC_ASSERT_NONNULL(identityInternerSet);
  }

  identity::IdentityInternResult<identity::CompilationUnitId> internCompilationUnit(
      identity::SemanticContextBrand context,
      const identity::CompilationUnitIdentity& key) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internCompilationUnit(context, key);
  }

  identity::IdentityInternResult<identity::CrateId> internCrate(
      identity::SemanticContextBrand context, const identity::CrateKey& key) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internCrate(context, key);
  }

  identity::IdentityInternResult<identity::SourceFileId> internSourceFile(
      identity::SemanticContextBrand context, const identity::SourceFileKey& key) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internSourceFile(context, key);
  }

  identity::IdentityInternResult<identity::ModuleId> internModule(
      identity::SemanticContextBrand context, const identity::ModuleKey& key) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internModule(context, key);
  }

  identity::IdentityInternResult<identity::DefId> internDefinition(
      identity::SemanticContextBrand context, const identity::DefinitionKey& key,
      const identity::DefinitionIdentityRecord& record) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internDefinition(context, key, record);
  }

  identity::IdentityInternResult<identity::ImplId> internImplementation(
      identity::SemanticContextBrand context, const identity::ImplKey& key,
      const identity::ImplIdentityRecord& record) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internImplementation(context, key, record);
  }

  identity::IdentityInternResult<identity::GenericParameterId> internGenericParameter(
      identity::SemanticContextBrand context, const identity::GenericParameterKey& key,
      const identity::GenericParameterIdentityRecord& record) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internGenericParameter(context, key, record);
  }

  identity::IdentityInternResult<identity::CallableParameterId> internCallableParameter(
      identity::SemanticContextBrand context, const identity::CallableParameterKey& key,
      const identity::CallableParameterIdentityRecord& record) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).internCallableParameter(context, key, record);
  }

  zc::Maybe<identity::CompilationUnitIdentityEntry> compilationUnit(
      identity::CompilationUnitId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).compilationUnit(handle);
  }

  zc::Maybe<identity::CrateIdentityEntry> crate(identity::CrateId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).crate(handle);
  }

  zc::Maybe<identity::SourceFileIdentityEntry> sourceFile(
      identity::SourceFileId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).sourceFile(handle);
  }

  zc::Maybe<identity::ModuleIdentityEntry> module(identity::ModuleId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).module(handle);
  }

  zc::Maybe<identity::DefinitionIdentityEntry> definition(identity::DefId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).definition(handle);
  }

  zc::Maybe<identity::ImplementationIdentityEntry> implementation(
      identity::ImplId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).implementation(handle);
  }

  zc::Maybe<identity::GenericParameterIdentityEntry> genericParameter(
      identity::GenericParameterId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).genericParameter(handle);
  }

  zc::Maybe<identity::CallableParameterIdentityEntry> callableParameter(
      identity::CallableParameterId handle) const override {
    return ZC_ASSERT_NONNULL(identityInternerSet).callableParameter(handle);
  }
};

struct InitializedSemanticContextResources final {
  InitializedSemanticContextResources(zc::Own<CompilerSessionSemanticContextResources>&& resources,
                                      SemanticContextResourceFailure failure) noexcept
      : resources(zc::mv(resources)), failure(failure) {}
  InitializedSemanticContextResources(InitializedSemanticContextResources&&) noexcept = default;
  ZC_DISALLOW_COPY(InitializedSemanticContextResources);

  zc::Own<CompilerSessionSemanticContextResources> resources;
  SemanticContextResourceFailure failure;
};

InitializedSemanticContextResources initializeSemanticContextResources(
    identity::SemanticContextFactory& contextFactory) {
  auto resources = zc::heap<CompilerSessionSemanticContextResources>();
  auto issuedContext = contextFactory.issue();
  if (issuedContext == zc::none) {
    return InitializedSemanticContextResources(
        zc::mv(resources), SemanticContextResourceFailure::ContextBrandExhausted);
  }
  ZC_IF_SOME(context, issuedContext) { resources->contextBrand = context; }

  auto issuedInterners =
      identity::IdentityInternerSet::create(contextFactory, resources->contextBrand);
  if (issuedInterners == zc::none) {
    return InitializedSemanticContextResources(
        zc::mv(resources), SemanticContextResourceFailure::IdentityInternerUnavailable);
  }
  ZC_IF_SOME(interners, issuedInterners) { resources->identityInternerSet = zc::mv(interners); }

  auto issuedFactStoreBrands = contextFactory.issueRegistryBrandIssuer(resources->contextBrand);
  if (issuedFactStoreBrands == zc::none) {
    return InitializedSemanticContextResources(
        zc::mv(resources), SemanticContextResourceFailure::RegistryBrandIssuerUnavailable);
  }
  ZC_IF_SOME(issuer, issuedFactStoreBrands) { resources->factStoreBrands = zc::mv(issuer); }

  auto issuedTypeStoreToken =
      contextFactory.issueSemanticTypeStoreConstructionToken(resources->contextBrand);
  if (issuedTypeStoreToken == zc::none) {
    return InitializedSemanticContextResources(
        zc::mv(resources), SemanticContextResourceFailure::SemanticTypeStoreUnavailable);
  }
  ZC_IF_SOME(token, issuedTypeStoreToken) {
    ZC_IF_SOME(identities, resources->identityInternerSet) {
      resources->semanticTypeStore = zc::heap<type::SemanticTypeStore>(zc::mv(token), identities);
    }
  }
  if (resources->semanticTypeStore.get() == nullptr) {
    return InitializedSemanticContextResources(
        zc::mv(resources), SemanticContextResourceFailure::SemanticTypeStoreUnavailable);
  }
  return InitializedSemanticContextResources(zc::mv(resources),
                                             SemanticContextResourceFailure::None);
}

}  // namespace

// ================================================================================
// CompilerSession::Impl

struct CompilerSession::Impl {
  Impl(identity::SemanticContextFactory& contextFactory, const basic::LangOptions& opts,
       const basic::CompilerOptions& compOpts)
      : Impl(initializeSemanticContextResources(contextFactory), opts, compOpts) {}

  Impl(InitializedSemanticContextResources&& initializedContext, const basic::LangOptions& opts,
       const basic::CompilerOptions& compOpts)
      : langOpts(opts),
        compilerOpts(compOpts),
        semanticContextResources(*initializedContext.resources),
        semanticContextCapabilityArena(
            zc::arc<query::SemanticContextCapabilityArena>(zc::mv(initializedContext.resources))),
        queryScheduler(4),
        queryDatabase(queryScheduler, query::productionQueryDescriptorInventory(),
                      semanticContextCapabilityArena.addRef()),
        contextBrand(semanticContextResources.contextBrand),
        semanticTypeStore(semanticContextResources.semanticTypeStore),
        factStoreBrands(semanticContextResources.factStoreBrands),
        stringPool(zc::heap<basic::StringPool>()),
        sourceManager(zc::heap<source::SourceManager>(*stringPool)),
        diagnosticEngine(zc::heap<diagnostics::DiagnosticEngine>(*sourceManager)) {
    diagnosticEngine->addConsumer(zc::heap<diagnostics::ConsolingDiagnosticConsumer>());
    if (!incremental_binding_query::registerIncrementalBindingQueryAdapter(queryDatabase) ||
        !module_graph_query::registerModuleGraphQueries(queryDatabase) ||
        !module_graph_query::registerStableModuleGraphQueries(queryDatabase)) {
      diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                                            zc::str(uint64_t{1}));
      return;
    }
    if (!core_library_query::registerCoreLibraryQueryProvider(queryDatabase)) {
      diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                                            zc::str(uint64_t{1}));
      return;
    }
    if (initializedContext.failure == SemanticContextResourceFailure::ContextBrandExhausted) {
      diagnosticEngine->diagnose<diagnostics::DiagID::IdentityBrandExhausted>(source::SourceLoc(),
                                                                              zc::str(uint64_t{1}));
      return;
    }
    if (initializedContext.failure != SemanticContextResourceFailure::None) {
      diagnosticEngine->diagnose<diagnostics::DiagID::IdentityDuplicateSingletonStore>(
          source::SourceLoc(), zc::str(uint64_t{1}));
      return;
    }
  }
  ~Impl() noexcept(false) { releaseSessionLeases(); }

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  struct OutputDirective {
    zc::ArrayPtr<zc::byte> name;
    zc::Maybe<zc::Path> dir;

    ZC_DISALLOW_COPY(OutputDirective);
    OutputDirective(OutputDirective&&) noexcept = default;
    OutputDirective(const zc::ArrayPtr<zc::byte> name, zc::Maybe<zc::Path> dir)
        : name(name), dir(zc::mv(dir)) {}
  };

  /// Language options
  const basic::LangOptions& langOpts;
  /// Compiler options
  const basic::CompilerOptions& compilerOpts;
  /// Exact semantic resources retained by every revision-local query capability lease.
  CompilerSessionSemanticContextResources& semanticContextResources;
  /// Session lifetime anchor shared by the query database and every snapshot.
  zc::Arc<query::SemanticContextCapabilityArena> semanticContextCapabilityArena;
  /// Sole session-owned scheduler for query dependency groups and future frontend work.
  basic::ThreadPool queryScheduler;
  /// Sole session-owned RFC 0017 input, memo, dependency, and flight authority.
  query::QueryDatabase queryDatabase;
  /// Final-admitted snapshot that is the only production root for retained materialization.
  using FinalSealedSnapshot =
      query::SealedQuerySnapshot<incremental_binding_query::CompilationRootSetQueryKey,
                                 identity::Sha256Digest>;
  zc::Maybe<FinalSealedSnapshot> finalSealedSnapshot;
  /// Process-unique identity retained inside the capability resource owner.
  identity::SemanticContextBrand& contextBrand;
  /// Sole RFC 0005 canonical semantic type store retained inside the capability resource owner.
  zc::Own<type::SemanticTypeStore>& semanticTypeStore;
  /// Context-local issuer retained inside the capability resource owner.
  zc::Maybe<identity::RegistryBrandIssuer>& factStoreBrands;
  /// Stable source-input keys retained to replace the complete source snapshot root.
  zc::Vector<source_query::StableSourceQueryKey> stagedSourceSnapshots;
  /// Complete crate keys retained to replace crate-keyed compilation options.
  zc::Vector<identity::CrateKey> stagedCompilationOptions;
  /// Stable crate keys retained to replace per-crate active source and module roots.
  zc::Vector<incremental_binding_query::StableCrateQueryKey> stagedActiveCrates;
  /// User-package crate keys retained to replace the active-source family before parsing.
  zc::Vector<incremental_binding_query::StableCrateQueryKey> stagedUserSourceCrates;
  /// Package-root-set key retained to replace the package-graph input root.
  zc::Maybe<incremental_binding_query::PackageRootSetKey> stagedPackageRoots;
  /// Complete compilation context retained only until final snapshot sealing.
  zc::Maybe<incremental_binding_query::CompilationRootSetQueryKey> stagedCompilationRoots;
  /// Session-owned append-only checked evidence publication root.
  zc::Own<checker::checked::CheckedFactsRepository> checkedFactsRepository;
  /// Session-owned append-only RFC 0013 borrow-evidence publication root.
  zc::Own<borrow_evidence::BorrowEvidenceRepository> borrowEvidenceRepository;
  /// Explicit storage owner for resolver inputs and the retained resolution output.
  zc::MemoryResource packageResolutionMemory;
  /// Workspace-verified package roots and their semantic identities.
  zc::Maybe<package::VerifiedPackageCompilationRequest> packageRequest;
  zc::Maybe<ir::VerifiedTargetSelection> verifiedHostTarget;
  zc::Maybe<ir::VerifiedTargetSelection> verifiedTarget;
  zc::Maybe<package::ResolutionOutput> packageGraph;
  zc::Maybe<VerifiedCrateGraph> crateGraph;
  zc::Vector<VerifiedPreparatoryCrateGraph> preparatoryCrateGraphs;
  zc::Vector<package::ResolvedPackageSourceSnapshot> packageSnapshots;
  zc::Maybe<package::VerifiedBuildScriptPlan> buildScriptPlan;
  zc::Maybe<package::VerifiedBuildScriptResultSet> buildScriptResults;
  /// Complete pre-parse core transaction retained with its structural catalogs.
  zc::Maybe<core_library_query::VerifiedCoreDistributionInputTransaction> coreDistributionInputs;
  /// Canonical source identities retained from package admission until source freeze.
  zc::HashMap<source::BufferId, identity::SourceFileKey> pendingSourceIdentities;
  /// Structurally selected non-empty module paths retained until module identity freeze.
  zc::HashMap<source::BufferId, zc::Vector<identity::ModulePathSegment>> pendingModulePaths;
  struct ModuleKeyBinding final {
    source::BufferId buffer;
    ast::NodeId node;
    identity::ModuleKey key;
  };
  /// Canonical module keys indexed by one tree-local node, with node zero denoting an implicit
  /// root. Module handles are resolved only at the immediate consumer boundary.
  zc::Vector<ModuleKeyBinding> moduleKeys;
  /// String pool to manage interned strings.
  zc::Own<basic::StringPool> stringPool;
  /// Source manager to manage source files.
  zc::Own<source::SourceManager> sourceManager;
  /// Diagnostic engine to report diagnostics.
  zc::Own<diagnostics::DiagnosticEngine> diagnosticEngine;
  /// Parser results retained only while constructing the final sealed snapshot.
  zc::Vector<ParsedModuleRecord> parsedModules;
  /// True only after every discovered source publishes a promoted immutable parser result.
  bool verifiedParsedSyntax = false;
  zc::Maybe<checker::signature::VerifiedMarkerShapeInventory> markerShapes;
  zc::Maybe<checker::signature::VerifiedMarkerPolicyRegistry> markerPolicies;
  zc::Maybe<core::VerifiedCoreLibrary> coreLibrary;
  zc::Vector<checker::signature::VerifiedSignatureFacts> signatureFacts;
  zc::Vector<checker::cross_module::ImportedSignatureView> importedSignatureViews;
  zc::Vector<checker::body::VerifiedBodyFactRequirementInventory> bodyRequirements;
  zc::Vector<VerifiedModuleInterface> moduleInterfaces;
  zc::Maybe<checker::CheckerIdentityAuthority> checkerIdentityAuthority;
  zc::Vector<ownership::OwnershipAdmittedBoundModule> ownershipAdmittedModules;
  zc::Vector<ownership::ValidatedOwnershipProofs> validatedOwnershipProofs;
  zc::Maybe<checker::coherence::FrozenCoherenceView> coherenceView;
  zc::Vector<checker::checked::CheckedEvidenceLease> checkedEvidence;
  zc::Vector<checker::dispatch::VerifiedDispatchFacts> dispatchFacts;
  zc::Vector<hir::VerifiedHirModule> hirModules;
  zc::Vector<ownership::OwnershipCheckedMir> ownershipCheckedMirModules;
  zc::Vector<ownership::VerifiedExecutableMir> verifiedExecutableMirModules;
  zc::Vector<ir::IrDiagnosticGroup> irFailureGroups;
  zc::Vector<identity::IdentityInvariant> irIdentityInvariantFailures;
  bool verifiedCheckedSources = false;
  /// Closed Checker invariant rejection retained when no complete publication exists.
  zc::Vector<checker::signature::CheckerVerificationFailure> checkerFailures;

  /// Test-only Built MIR, overlay, and borrow-evidence repository retained when
  /// checkSources rejects a module at the borrow-source stage. Never committed
  /// to a production accessor; read only by firstStagedBorrowSourceRejectionForTesting.
  zc::Vector<mir::VerifiedBuiltMir> stagedBorrowSourceRejectedBuiltMir;
  zc::Vector<ownership::VerifiedOwnershipEventOverlay> stagedBorrowSourceRejectedOverlays;
  zc::Own<borrow_evidence::BorrowEvidenceRepository> stagedBorrowSourceRejectedBorrowEvidence;

  void releaseSessionLeases() noexcept(false) {
    verifiedExecutableMirModules.clear();
    ownershipCheckedMirModules.clear();
    stagedBorrowSourceRejectedBuiltMir.clear();
    stagedBorrowSourceRejectedOverlays.clear();
    validatedOwnershipProofs.clear();
    hirModules.clear();
    ownershipAdmittedModules.clear();
    dispatchFacts.clear();
    checkedEvidence.clear();
    coherenceView = zc::none;
    checkerIdentityAuthority = zc::none;
    moduleInterfaces.clear();
    bodyRequirements.clear();
    importedSignatureViews.clear();
    signatureFacts.clear();
    markerPolicies = zc::none;
    markerShapes = zc::none;
    coreLibrary = zc::none;
    borrowEvidenceRepository = nullptr;
    stagedBorrowSourceRejectedBorrowEvidence = nullptr;
    checkedFactsRepository = nullptr;
    finalSealedSnapshot = zc::none;
  }

  bool stageParseSourceInputs() {
    namespace incremental = incremental_binding_query;

    if (packageRequest == zc::none || packageGraph == zc::none || crateGraph == zc::none ||
        pendingSourceIdentities.size() == 0) {
      return false;
    }
    zc::Maybe<source_query::CanonicalCompilationOptions> compilationOptions;
    zc::Maybe<incremental::PackageRootSetKey> packageRoots;
    zc::Maybe<incremental::CanonicalPackageGraph> packageGraphInput;
    ZC_IF_SOME(request, packageRequest) {
      compilationOptions = source_query::CanonicalCompilationOptions::fromVerified(request);
      packageRoots = incremental::PackageRootSetKey::fromVerified(request);
    }
    ZC_IF_SOME(resolution, packageGraph) {
      ZC_IF_SOME(graph, crateGraph) {
        packageGraphInput = incremental::CanonicalPackageGraph::fromVerified(resolution, graph);
      }
    }
    if (compilationOptions == zc::none || packageRoots == zc::none ||
        packageGraphInput == zc::none) {
      return false;
    }

    struct StagedSource final {
      StagedSource(source_query::StableSourceQueryKey&& key,
                   source_query::CanonicalSourceSnapshot&& snapshot) noexcept
          : key(zc::mv(key)), snapshot(zc::mv(snapshot)) {}
      StagedSource(StagedSource&&) noexcept = default;
      StagedSource& operator=(StagedSource&&) noexcept = default;
      ZC_DISALLOW_COPY(StagedSource);
      source_query::StableSourceQueryKey key;
      source_query::CanonicalSourceSnapshot snapshot;
    };

    struct StagedUserSources final {
      StagedUserSources(incremental::StableCrateQueryKey&& crate,
                        zc::Vector<source_query::StableSourceQueryKey>&& sources) noexcept
          : crate(zc::mv(crate)), sources(zc::mv(sources)) {}
      StagedUserSources(StagedUserSources&&) noexcept = default;
      StagedUserSources& operator=(StagedUserSources&&) noexcept = default;
      ZC_DISALLOW_COPY(StagedUserSources);
      incremental::StableCrateQueryKey crate;
      zc::Vector<source_query::StableSourceQueryKey> sources;
    };

    zc::TreeMap<zc::String, StagedSource> canonicalSources;
    zc::TreeMap<zc::String, identity::CrateKey> compilationCrates;
    zc::TreeMap<zc::String, StagedUserSources> userSources;
    for (const auto& entry : pendingSourceIdentities) {
      auto key = source_query::StableSourceQueryKey::fromVerified(entry.value);
      auto snapshot = identity::ImmutableSourceSnapshot::from(
          entry.value.clone(), zc::heapArray(sourceManager->getEntireTextForBuffer(entry.key)));
      if (key == zc::none || snapshot == zc::none) { return false; }
      auto canonicalSnapshot =
          source_query::CanonicalSourceSnapshot::fromVerified(ZC_ASSERT_NONNULL(snapshot));
      if (canonicalSnapshot == zc::none) { return false; }
      auto stableSource = ZC_ASSERT_NONNULL(key).clone();
      auto sortKey = zc::encodeHex(ZC_ASSERT_NONNULL(key).canonicalSourceBytes());
      if (canonicalSources.find(sortKey) != zc::none) { return false; }
      canonicalSources.insert(zc::mv(sortKey),
                              StagedSource(zc::mv(ZC_ASSERT_NONNULL(key)),
                                           zc::mv(ZC_ASSERT_NONNULL(canonicalSnapshot))));
      auto crateBytes = entry.value.crate().encode();
      auto crateSortKey = zc::encodeHex(crateBytes.asPtr());
      if (compilationCrates.find(crateSortKey) == zc::none) {
        compilationCrates.insert(zc::mv(crateSortKey), entry.value.crate().clone());
      }
      if (entry.value.crate().unit().kind() == identity::CompilationUnitKind::UserPackage) {
        auto userCrateSortKey = zc::encodeHex(crateBytes.asPtr());
        auto user = userSources.find(userCrateSortKey);
        if (user == zc::none) {
          auto stableCrate = incremental::StableCrateQueryKey::fromVerified(entry.value.crate());
          if (stableCrate == zc::none) { return false; }
          zc::Vector<source_query::StableSourceQueryKey> sources;
          sources.add(stableSource.clone());
          userSources.insert(
              zc::mv(userCrateSortKey),
              StagedUserSources(zc::mv(ZC_ASSERT_NONNULL(stableCrate)), zc::mv(sources)));
        } else {
          ZC_IF_SOME(value, user) { value.sources.add(stableSource.clone()); }
        }
      }
    }

    auto opened = queryDatabase.beginInputTransaction(queryDatabase.snapshot().revision());
    if (!opened.isOpened()) { return false; }
    {
      auto transaction = zc::mv(opened).takeTransaction();
      for (const auto& prior : stagedSourceSnapshots) {
        auto key = zc::encodeHex(prior.canonicalSourceBytes());
        if (canonicalSources.find(key) == zc::none &&
            !transaction.erase<source_query::SourceSnapshotInput>(prior).isApplied()) {
          return false;
        }
      }
      for (const auto& prior : stagedCompilationOptions) {
        auto key = zc::encodeHex(prior.encode().asPtr());
        if (compilationCrates.find(key) == zc::none &&
            !transaction.erase<source_query::CompilationOptionsInput>(prior).isApplied()) {
          return false;
        }
      }
      for (const auto& prior : stagedUserSourceCrates) {
        auto key = zc::encodeHex(prior.canonicalCrateBytes());
        if (userSources.find(key) == zc::none &&
            !transaction.erase<incremental::UserPackageActiveSourcesInput>(prior).isApplied()) {
          return false;
        }
      }
      ZC_IF_SOME(prior, stagedPackageRoots) {
        if (prior != ZC_ASSERT_NONNULL(packageRoots) &&
            !transaction.erase<incremental::PackageGraphInput>(prior).isApplied()) {
          return false;
        }
      }
      for (const auto& entry : compilationCrates) {
        if (!transaction
                 .set<source_query::CompilationOptionsInput>(entry.value,
                                                             ZC_ASSERT_NONNULL(compilationOptions))
                 .isApplied()) {
          return false;
        }
      }
      for (const auto& entry : canonicalSources) {
        if (!transaction
                 .set<source_query::SourceSnapshotInput>(entry.value.key, entry.value.snapshot)
                 .isApplied()) {
          return false;
        }
      }
      ZC_IF_SOME(rootKey, packageRoots) {
        ZC_IF_SOME(graphValue, packageGraphInput) {
          if (!transaction.set<incremental::PackageGraphInput>(rootKey, graphValue).isApplied()) {
            return false;
          }
        }
      }
      for (auto& entry : userSources) {
        auto sources = incremental::CanonicalSourceSet::from(zc::mv(entry.value.sources));
        if (sources == zc::none || !transaction
                                        .set<incremental::UserPackageActiveSourcesInput>(
                                            entry.value.crate, ZC_ASSERT_NONNULL(sources))
                                        .isApplied()) {
          return false;
        }
      }
      if (!transaction.commit().isCommitted()) { return false; }
    }
    stagedSourceSnapshots.clear();
    stagedSourceSnapshots.reserve(canonicalSources.size());
    for (const auto& entry : canonicalSources) {
      stagedSourceSnapshots.add(entry.value.key.clone());
    }
    stagedCompilationOptions.clear();
    stagedCompilationOptions.reserve(compilationCrates.size());
    for (const auto& entry : compilationCrates) {
      stagedCompilationOptions.add(entry.value.clone());
    }
    stagedUserSourceCrates.clear();
    stagedUserSourceCrates.reserve(userSources.size());
    for (const auto& entry : userSources) { stagedUserSourceCrates.add(entry.value.crate.clone()); }
    ZC_IF_SOME(rootKey, packageRoots) { stagedPackageRoots = rootKey.clone(); }
    return true;
  }

  zc::Maybe<source::BufferId> registerVerifiedSource(
      const identity::CrateKey& crate, const identity::CanonicalRelativePath& sourcePath,
      zc::ArrayPtr<const identity::ModulePathSegment> modulePath, bool& added) {
    added = false;
    if (modulePath.size() == 0 || packageRequest == zc::none) { return zc::none; }

    zc::Maybe<const package::ResolvedPackageSourceSnapshot&> selectedSnapshot;
    if (crate.unit().kind() != identity::CompilationUnitKind::UserPackage) { return zc::none; }
    const auto& package = crate.unit().userPackage();
    for (const auto& candidate : packageSnapshots) {
      if (!packageMatchesBase(package, candidate.package())) { continue; }
      if (selectedSnapshot != zc::none) { return zc::none; }
      selectedSnapshot = candidate;
    }
    if (selectedSnapshot == zc::none) { return zc::none; }

    auto origin = sourceOriginFor(package, sourcePath);
    if (origin == zc::none) { return zc::none; }
    ZC_IF_SOME(originValue, origin) {
      auto sourceKey = identity::SourceFileKey::from(crate.clone(), zc::mv(originValue));
      ZC_IF_SOME(snapshot, selectedSnapshot) {
        auto bytes = snapshot.snapshot().readVerifiedFile(sourcePath);
        if (!bytes.is<zc::Array<zc::byte>>()) { return zc::none; }
        return registerSource(zc::mv(sourceKey), modulePath,
                              zc::mv(bytes.get<zc::Array<zc::byte>>()),
                              packageSourceIdentifier(package, sourcePath), added);
      }
    }
    return zc::none;
  }

  zc::Maybe<source::BufferId> registerGeneratedSource(
      const identity::CrateKey& crate, const package::VerifiedBuildScriptResult& result,
      identity::BuildScriptProducerKey producer, const identity::CanonicalRelativePath& sourcePath,
      zc::ArrayPtr<const identity::ModulePathSegment> modulePath, bool& added) {
    added = false;
    if (modulePath.size() == 0 || result.output().producerKey().digest() != producer.digest()) {
      return zc::none;
    }
    if (crate.unit().kind() != identity::CompilationUnitKind::UserPackage) { return zc::none; }
    const auto& package = crate.unit().userPackage();
    size_t matches = 0;
    for (const auto& file : result.run().outputs().files()) {
      if (!sameRelativePath(file.path(), sourcePath)) { continue; }
      ++matches;
    }
    if (matches != 1) { return zc::none; }
    auto bytes = result.run().outputSnapshot().readVerifiedFile(sourcePath);
    if (!bytes.is<zc::Array<zc::byte>>()) { return zc::none; }
    auto sourceKey = identity::SourceFileKey::from(
        crate.clone(), identity::SourceOriginKey::generatedFile(producer, sourcePath.clone()));
    return registerSource(zc::mv(sourceKey), modulePath, zc::mv(bytes.get<zc::Array<zc::byte>>()),
                          generatedSourceIdentifier(package, sourcePath), added);
  }

  zc::Maybe<source::BufferId> registerSource(
      identity::SourceFileKey&& sourceKey,
      zc::ArrayPtr<const identity::ModulePathSegment> modulePath, zc::Array<zc::byte>&& bytes,
      zc::String&& identifier, bool& added) {
    const auto encoded = sourceKey.encode();
    for (const auto& existing : pendingSourceIdentities) {
      if (!sameBytes(existing.value.encode().asPtr(), encoded.asPtr())) { continue; }
      auto existingPath = pendingModulePaths.find(existing.key);
      if (existingPath == zc::none) { return zc::none; }
      ZC_IF_SOME(pathValue, existingPath) {
        if (!sameModulePath(pathValue.asPtr(), modulePath)) { return zc::none; }
      }
      return existing.key;
    }

    const auto buffer = sourceManager->addNewSourceBuffer(zc::mv(bytes), identifier);
    pendingSourceIdentities.upsert(buffer, zc::mv(sourceKey));
    pendingModulePaths.upsert(buffer, cloneModulePath(modulePath));
    added = true;
    return buffer;
  }

  zc::Maybe<const package::FinalizedCompilationRoot&> compilationRoot(
      const identity::CrateKey& crate) const {
    if (crateGraph == zc::none) { return zc::none; }
    zc::Maybe<const package::FinalizedCompilationRoot&> selected;
    ZC_IF_SOME(graph, crateGraph) {
      const auto expected = crate.encode();
      for (const auto& root : graph.roots()) {
        if (!sameBytes(root.crateKey().encode().asPtr(), expected.asPtr())) { continue; }
        if (selected != zc::none) { return zc::none; }
        selected = root;
      }
    }
    return selected;
  }

  zc::Maybe<const package::ResolvedPackageSourceSnapshot&> packageSnapshot(
      const identity::PackageKey& package) const {
    zc::Maybe<const package::ResolvedPackageSourceSnapshot&> selected;
    for (const auto& candidate : packageSnapshots) {
      if (!packageMatchesBase(package, candidate.package())) { continue; }
      if (selected != zc::none) { return zc::none; }
      selected = candidate;
    }
    return selected;
  }

  bool discoverModuleSourceCandidate(const identity::CrateKey& crate,
                                     zc::ArrayPtr<const identity::ModulePathSegment> selectedPath,
                                     bool& addedAny) {
    if (selectedPath.size() == 0) { return false; }
    for (const auto& source : pendingSourceIdentities) {
      if (!sameBytes(source.value.crate().encode().asPtr(), crate.encode().asPtr())) { continue; }
      auto path = pendingModulePaths.find(source.key);
      if (path != zc::none && sameModulePath(ZC_ASSERT_NONNULL(path).asPtr(), selectedPath)) {
        return true;
      }
    }
    if (crate.unit().kind() == identity::CompilationUnitKind::Toolchain &&
        crate.unit().toolchain().component() == identity::ToolchainComponent::Core) {
      return true;
    }
    if (crate.unit().kind() != identity::CompilationUnitKind::UserPackage) { return false; }
    const auto& package = crate.unit().userPackage();
    auto root = compilationRoot(crate);
    auto snapshot = packageSnapshot(package);
    if (root == zc::none || snapshot == zc::none) { return false; }
    ZC_IF_SOME(rootValue, root) {
      if (selectedPath[0].text() != rootValue.crateKey().targetName()) { return true; }
      if (selectedPath.size() == 1) { return true; }
      ZC_IF_SOME(snapshotValue, snapshot) {
        const auto searchRoot = parentDirectory(rootValue.sourcePath());
        const auto moduleSuffix = selectedPath.slice(1, selectedPath.size());
        auto discovered =
            discoverModuleSource(snapshotValue.snapshot().record(), searchRoot, moduleSuffix);
        if (discovered.is<InvalidModuleSourceRequest>()) { return false; }
        const auto registerPath = [&](const identity::CanonicalRelativePath& path) {
          bool added = false;
          auto registered = registerVerifiedSource(crate, path, selectedPath, added);
          if (registered == zc::none) { return false; }
          addedAny = addedAny || added;
          return true;
        };
        if (discovered.is<ResolvedModuleSource>()) {
          if (!registerPath(discovered.get<ResolvedModuleSource>().path())) { return false; }
        }
        if (discovered.is<AmbiguousModuleSource>()) {
          for (const auto& path : discovered.get<AmbiguousModuleSource>().paths()) {
            if (!registerPath(path)) { return false; }
          }
        }

        ZC_IF_SOME(results, buildScriptResults) {
          zc::Vector<identity::CanonicalPathSegment> noSegments;
          const auto generatedRoot = identity::CanonicalRelativePath::from(zc::mv(noSegments));
          for (size_t index = 0; index < results.results().size(); ++index) {
            const auto& planKey = results.planKeys()[index];
            const auto& result = results.results()[index];
            if (!samePackage(package, planKey.package())) { continue; }
            auto generated =
                discoverModuleSource(result.run().outputs(), generatedRoot, moduleSuffix);
            if (generated.is<InvalidModuleSourceRequest>()) { return false; }
            const auto registerGenerated = [&](const identity::CanonicalRelativePath& path) {
              bool added = false;
              auto registered = registerGeneratedSource(crate, result, planKey.producerKey(), path,
                                                        selectedPath, added);
              if (registered == zc::none) { return false; }
              addedAny = addedAny || added;
              return true;
            };
            if (generated.is<ResolvedModuleSource>() &&
                !registerGenerated(generated.get<ResolvedModuleSource>().path())) {
              return false;
            }
            if (generated.is<AmbiguousModuleSource>()) {
              for (const auto& path : generated.get<AmbiguousModuleSource>().paths()) {
                if (!registerGenerated(path)) { return false; }
              }
            }
          }
        }
        return true;
      }
    }
    return false;
  }

  bool discoverDependencies(const source::BufferId& requesterBuffer,
                            zc::ArrayPtr<const StructuralModuleDependencyRequest> requests,
                            bool& addedAny) {
    if (crateGraph == zc::none) { return false; }
    auto requesterSource = pendingSourceIdentities.find(requesterBuffer);
    auto requesterPath = pendingModulePaths.find(requesterBuffer);
    if (requesterSource == zc::none || requesterPath == zc::none) { return false; }

    struct DiscoveryTarget final {
      DiscoveryTarget(identity::CrateKey&& crate,
                      zc::Vector<identity::ModulePathSegment>&& path) noexcept
          : crate(zc::mv(crate)), path(zc::mv(path)) {}
      DiscoveryTarget(DiscoveryTarget&&) noexcept = default;
      DiscoveryTarget& operator=(DiscoveryTarget&&) noexcept = default;
      ZC_DISALLOW_COPY(DiscoveryTarget);
      identity::CrateKey crate;
      zc::Vector<identity::ModulePathSegment> path;
    };

    ZC_IF_SOME(sourceValue, requesterSource) {
      ZC_IF_SOME(pathValue, requesterPath) {
        auto requesterCrate = sourceValue.crate().clone();
        auto requesterModulePath = cloneModulePath(pathValue.asPtr());
        for (const auto& request : requests) {
          if (request.normalizedPath().size() == 0) { return false; }
          zc::TreeMap<zc::String, DiscoveryTarget> targets;
          const auto addTarget = [&](identity::CrateKey&& crate,
                                     zc::Vector<identity::ModulePathSegment>&& path) {
            if (path.size() == 0) { return false; }
            auto key = zc::encodeHex(encodeStructuralModulePath(crate, path.asPtr()).asPtr());
            if (targets.find(key) == zc::none) {
              targets.insert(zc::mv(key), DiscoveryTarget(zc::mv(crate), zc::mv(path)));
            }
            return true;
          };

          for (size_t prefixSize = requesterModulePath.size(); prefixSize != 0; --prefixSize) {
            zc::Vector<identity::ModulePathSegment> candidate(prefixSize +
                                                              request.normalizedPath().size());
            for (size_t index = 0; index < prefixSize; ++index) {
              candidate.add(requesterModulePath[index].clone());
            }
            for (const auto& segment : request.normalizedPath()) { candidate.add(segment.clone()); }
            if (!addTarget(requesterCrate.clone(), zc::mv(candidate))) { return false; }
          }
          if (!addTarget(requesterCrate.clone(), cloneModulePath(request.normalizedPath()))) {
            return false;
          }

          ZC_IF_SOME(graph, crateGraph) {
            for (const auto& edge : graph.edges()) {
              if (!sameBytes(edge.consumer().encode().asPtr(), requesterCrate.encode().asPtr()) ||
                  edge.origin().kind() != identity::CrateDependencyOriginKind::UserPackage ||
                  request.normalizedPath()[0].text() != edge.origin().userPackageEdge().alias()) {
                continue;
              }
              zc::Vector<identity::ModulePathSegment> providerPath;
              auto rootSegment =
                  identity::ModulePathSegment::fromCanonical(edge.provider().targetName());
              if (rootSegment == zc::none) { return false; }
              ZC_IF_SOME(value, rootSegment) { providerPath.add(zc::mv(value)); }
              for (size_t index = 1; index < request.normalizedPath().size(); ++index) {
                providerPath.add(request.normalizedPath()[index].clone());
              }
              if (!addTarget(edge.provider().clone(), zc::mv(providerPath))) { return false; }
            }
          }

          for (const auto& entry : targets) {
            if (!discoverModuleSourceCandidate(entry.value.crate, entry.value.path.asPtr(),
                                               addedAny)) {
              return false;
            }
          }
        }
      }
    }
    return true;
  }

  bool internPackageAndCrateIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (crateGraph == zc::none) {
      package::PackageDiagnosticAdapter::emitBuildScriptIssue(
          *diagnosticEngine, package::BuildScriptIssue::BuildResultIntegrityViolation);
      return false;
    }

    zc::TreeMap<zc::String, identity::CompilationUnitIdentity> compilationUnits;
    zc::TreeMap<zc::String, identity::CrateKey> crates;
    const auto addCrate = [&](const identity::CrateKey& crate) {
      auto unitKey = zc::encodeHex(crate.unit().encode().asPtr());
      if (compilationUnits.find(unitKey) == zc::none) {
        compilationUnits.insert(zc::mv(unitKey), crate.unit().clone());
      }
      auto crateKey = zc::encodeHex(crate.encode().asPtr());
      if (crates.find(crateKey) != zc::none) { return false; }
      crates.insert(zc::mv(crateKey), crate.clone());
      return true;
    };
    ZC_IF_SOME(graph, crateGraph) {
      for (const auto& crate : graph.crates()) {
        if (!addCrate(crate)) { return false; }
      }
    }
    ZC_IF_SOME(coreInputs, coreDistributionInputs) {
      for (const auto& projection : coreInputs.projections()) {
        if (!addCrate(projection.crate())) { return false; }
      }
    }
    for (const auto& entry : compilationUnits) {
      if (!semanticContextResources.identityInterners()
               .internCompilationUnit(contextBrand, entry.value)
               .is<identity::CompilationUnitId>()) {
        return false;
      }
    }
    for (const auto& entry : crates) {
      if (!semanticContextResources.identityInterners()
               .internCrate(contextBrand, entry.value)
               .is<identity::CrateId>()) {
        return false;
      }
    }
    return true;
  }

  bool internSourceIdentities() {
    if (packageRequest == zc::none) { return true; }
    if (pendingSourceIdentities.size() == 0 ||
        pendingSourceIdentities.size() != sourceManager->getManagedBufferIds().size()) {
      return false;
    }

    zc::TreeMap<zc::String, source::BufferId> canonicalSources;
    for (const auto& entry : pendingSourceIdentities) {
      auto sortKey = zc::encodeHex(entry.value.encode().asPtr());
      if (canonicalSources.find(sortKey) != zc::none) { return false; }
      canonicalSources.insert(zc::mv(sortKey), entry.key);
    }
    for (const auto& entry : canonicalSources) {
      auto sourceKey = pendingSourceIdentities.find(entry.value);
      if (sourceKey == zc::none) { return false; }
      ZC_IF_SOME(value, sourceKey) {
        if (!semanticContextResources.identityInterners()
                 .internSourceFile(contextBrand, value)
                 .is<identity::SourceFileId>()) {
          return false;
        }
      }
    }
    return true;
  }

  zc::Maybe<identity::ModuleId> moduleIdentity(const source::BufferId& buffer,
                                               ast::NodeId node) const {
    for (const auto& binding : moduleKeys) {
      if (binding.buffer == buffer && binding.node == node) {
        auto entry = semanticContextResources.identityInterners().module(binding.key);
        ZC_IF_SOME(value, entry) { return value.handle(); }
      }
    }
    return zc::none;
  }

  bool internModuleIdentities() {
    if (packageRequest == zc::none) { return true; }

    struct PendingModule final {
      source::BufferId buffer;
      ast::NodeId node;
      identity::ModuleKey key;

      PendingModule(source::BufferId buffer, ast::NodeId node, identity::ModuleKey&& key)
          : buffer(zc::mv(buffer)), node(node), key(zc::mv(key)) {}
      PendingModule(PendingModule&&) noexcept = default;
      PendingModule& operator=(PendingModule&&) noexcept = default;
      ZC_DISALLOW_COPY(PendingModule);
    };

    zc::Vector<PendingModule> pending;
    for (const auto& parsedRecord : parsedModules) {
      const auto& buffer = parsedRecord.buffer();
      const auto& tree = parsedRecord.parsedModule().tree();
      auto inventoryValue = binder::DefinitionInventory::collect(tree);
      auto sourceKey = pendingSourceIdentities.find(buffer);
      auto selectedPath = pendingModulePaths.find(buffer);
      if (sourceKey == zc::none || selectedPath == zc::none ||
          inventoryValue.modules().size() > 1) {
        return false;
      }
      ZC_IF_SOME(sourceValue, sourceKey) {
        if (semanticContextResources.identityInterners().sourceFile(sourceValue) == zc::none) {
          return false;
        }
        ZC_IF_SOME(pathValue, selectedPath) {
          if (pathValue.size() == 0) { return false; }
          ast::NodeId moduleNode;
          if (inventoryValue.modules().size() == 1) {
            const auto& module = inventoryValue.modules()[0];
            auto declaredName =
                identity::ModulePathSegment::fromSource(tree.ident(module.declaredName));
            if (module.parentModuleNode || declaredName == zc::none) { return false; }
            bool nameMatches = false;
            bool declaresToolchainModuleRoot = false;
            ZC_IF_SOME(value, declaredName) {
              nameMatches = value == pathValue.back();
              zc::Vector<identity::ModulePathSegment> declaredPath;
              declaredPath.add(value.clone());
              declaresToolchainModuleRoot = diagnostics::ModuleRootArgument::fromCanonicalPath(
                                                zc::mv(declaredPath)) != zc::none;
            }
            if (!nameMatches && !declaresToolchainModuleRoot) {
              auto declarationSpan = parsedRecord.parsedModule().spanFor(module.source);
              if (declarationSpan == zc::none) { return false; }
              auto declarationStart =
                  parsedRecord.parsedModule().sourceLocFor(ZC_ASSERT_NONNULL(declarationSpan));
              if (declarationStart == zc::none) { return false; }
              ZC_IF_SOME(value, declaredName) {
                const auto start = ZC_ASSERT_NONNULL(declarationStart);
                auto diagnostic =
                    diagnosticEngine->diagnose<diagnostics::DiagID::ModuleDeclarationNameMismatch>(
                        start, zc::str(value.text()), zc::str(pathValue.back().text()));
                diagnostic.addRange(source::CharSourceRange::getCharRange(
                    start, start.getAdvancedLoc(module.source.getLength())));
                diagnostic.emit();
              }
              return false;
            }
            moduleNode = module.node;
          }
          zc::Vector<identity::ModulePathSegment> keyPath(pathValue.size());
          for (const auto& segment : pathValue) { keyPath.add(segment.clone()); }
          auto key = identity::ModuleKey::from(sourceValue.crate().clone(), zc::mv(keyPath));
          ZC_IF_SOME(value, key) {
            auto retained = value.clone();
            if (!semanticContextResources.identityInterners()
                     .internModule(contextBrand, value)
                     .is<identity::ModuleId>()) {
              return false;
            }
            pending.add(PendingModule(buffer, moduleNode, zc::mv(retained)));
          } else {
            return false;
          }
        }
      }
    }
    for (const auto& module : pending) {
      if (semanticContextResources.identityInterners().module(module.key) == zc::none) {
        return false;
      }
      moduleKeys.add(ModuleKeyBinding{module.buffer, module.node, module.key.clone()});
    }
    return true;
  }

  bool stageVerifiedModuleGraphInputs(
      const binder::StructuralModuleResolver& resolver,
      zc::ArrayPtr<const binder::ModuleDependencyRequest> requests,
      zc::ArrayPtr<const binder::ParsedModuleGraphInput> parsedModuleInputs) {
    namespace graph_query = module_graph_query;
    namespace resolution_query = incremental_module_resolution_query;

    if (packageRequest == zc::none || packageGraph == zc::none || crateGraph == zc::none ||
        coreDistributionInputs == zc::none || resolver.catalog().size() == 0) {
      return false;
    }

    struct CatalogAccumulator final {
      CatalogAccumulator(identity::CrateKey&& crate,
                         zc::Vector<graph_query::SelectedModuleRecord>&& entries) noexcept
          : crate(zc::mv(crate)), entries(zc::mv(entries)) {}
      CatalogAccumulator(CatalogAccumulator&&) noexcept = default;
      CatalogAccumulator& operator=(CatalogAccumulator&&) noexcept = default;
      ZC_DISALLOW_COPY(CatalogAccumulator);

      identity::CrateKey crate;
      zc::Vector<graph_query::SelectedModuleRecord> entries;
    };

    zc::TreeMap<zc::String, CatalogAccumulator> catalogAccumulators;
    for (const auto& entry : resolver.catalog()) {
      const auto crateBytes = entry.key.crate().encode();
      auto crateSlot = zc::encodeHex(crateBytes.asPtr());
      auto accumulator = catalogAccumulators.find(crateSlot);
      if (accumulator == zc::none) {
        zc::Vector<graph_query::SelectedModuleRecord> entries;
        entries.add(graph_query::SelectedModuleRecord(entry.key.clone(), entry.source.clone()));
        catalogAccumulators.insert(zc::mv(crateSlot),
                                   CatalogAccumulator(entry.key.crate().clone(), zc::mv(entries)));
      } else {
        ZC_IF_SOME(value, accumulator) {
          value.entries.add(
              graph_query::SelectedModuleRecord(entry.key.clone(), entry.source.clone()));
        }
      }
    }

    zc::Vector<graph_query::SelectedModuleCatalog> catalogs(catalogAccumulators.size());
    for (auto& entry : catalogAccumulators) {
      auto catalog = graph_query::SelectedModuleCatalog::from(zc::mv(entry.value.crate),
                                                              zc::mv(entry.value.entries));
      if (catalog == zc::none) { return false; }
      catalogs.add(zc::mv(ZC_ASSERT_NONNULL(catalog)));
    }

    const auto sourceDigest =
        [&](const identity::SourceFileKey& source) -> zc::Maybe<const identity::Sha256Digest&> {
      zc::Maybe<const identity::Sha256Digest&> found;
      for (const auto& parsed : parsedModules) {
        if (!sameBytes(parsed.parsedModule().source().encode().asPtr(), source.encode().asPtr())) {
          continue;
        }
        if (found != zc::none) { return zc::none; }
        found = parsed.parsedModule().contentDigest();
      }
      return found;
    };

    zc::Vector<graph_query::DetachedModuleDependencySiteSet> dependencySites(
        resolver.catalog().size());
    for (const auto& entry : resolver.catalog()) {
      auto digest = sourceDigest(entry.source);
      if (digest == zc::none) { return false; }
      zc::Vector<graph_query::DetachedModuleDependencySite> sites;
      for (const auto& request : requests) {
        if (!sameBytes(request.key().requester().encode().asPtr(), entry.key.encode().asPtr()) ||
            request.isPrelude()) {
          continue;
        }
        graph_query::DetachedModuleDependencySiteKind kind;
        switch (request.kind()) {
          case identity::ModuleDependencyKind::Import:
            kind = graph_query::DetachedModuleDependencySiteKind::Import;
            break;
          case identity::ModuleDependencyKind::ForeignReexport:
            kind = graph_query::DetachedModuleDependencySiteKind::ForeignReexport;
            break;
          case identity::ModuleDependencyKind::ModuleAlias:
            kind = graph_query::DetachedModuleDependencySiteKind::ModuleAlias;
            break;
          case identity::ModuleDependencyKind::Prelude:
            return false;
        }
        for (const auto& syntax : request.syntaxSites()) {
          auto site = graph_query::DetachedModuleDependencySite::from(
              kind, cloneModulePath(request.normalizedPath()), syntax.schemaPreorderOrdinal);
          if (site == zc::none) { return false; }
          sites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
        }
      }
      auto siteSet = graph_query::DetachedModuleDependencySiteSet::from(
          entry.key.clone(), entry.source.clone(), ZC_ASSERT_NONNULL(digest), zc::mv(sites));
      if (siteSet == zc::none) { return false; }
      dependencySites.add(zc::mv(ZC_ASSERT_NONNULL(siteSet)));
    }

    zc::Vector<identity::RequesterModuleAncestry> ancestries(
        resolver.requesterAncestryInputs().size());
    for (const auto& ancestry : resolver.requesterAncestryInputs()) {
      ancestries.add(ancestry.clone());
    }

    zc::Vector<resolution_query::CanonicalModuleSearchRoots> searchRoots(catalogs.size());
    for (const auto& catalog : catalogs) {
      auto roots = resolution_query::CanonicalModuleSearchRoots::fromVerified(
          catalog.crate(), resolver.searchRootInputs());
      if (roots == zc::none) { return false; }
      searchRoots.add(zc::mv(ZC_ASSERT_NONNULL(roots)));
    }

    zc::TreeMap<zc::String, resolution_query::CanonicalModuleCatalogBucket> bucketMap;
    const auto addBucket = [&](const identity::CrateKey& crate,
                               zc::ArrayPtr<const identity::ModulePathSegment> path) {
      auto bucket = resolver.catalogPathBucketInput(crate, path);
      if (bucket == zc::none) { return false; }
      const auto encoded = ZC_ASSERT_NONNULL(bucket).key().encode();
      auto key = zc::encodeHex(encoded.asPtr());
      if (bucketMap.find(key) == zc::none) {
        bucketMap.insert(zc::mv(key), resolution_query::CanonicalModuleCatalogBucket::fromVerified(
                                          ZC_ASSERT_NONNULL(bucket)));
      }
      return true;
    };
    for (const auto& catalog : catalogs) {
      for (const auto& selected : catalog.entries()) {
        if (!addBucket(selected.module().crate(), selected.module().path())) { return false; }
      }
    }
    for (const auto& request : requests) {
      if (request.isPrelude() || request.normalizedPath().size() == 0) { return false; }
      zc::Maybe<const identity::RequesterModuleAncestry&> selectedAncestry;
      for (const auto& ancestry : resolver.requesterAncestryInputs()) {
        if (!sameBytes(ancestry.requester().encode().asPtr(),
                       request.key().requester().encode().asPtr())) {
          continue;
        }
        if (selectedAncestry != zc::none) { return false; }
        selectedAncestry = ancestry;
      }
      if (selectedAncestry == zc::none) { return false; }
      ZC_IF_SOME(ancestry, selectedAncestry) {
        for (const auto& ancestor : ancestry.ancestry()) {
          auto path = cloneModulePath(ancestor.path());
          for (const auto& segment : request.normalizedPath()) { path.add(segment.clone()); }
          if (!addBucket(request.key().requester().crate(), path.asPtr())) { return false; }
        }
      }
      if (!addBucket(request.key().requester().crate(), request.normalizedPath())) { return false; }
      ZC_IF_SOME(aliasText, request.key().dependencyAlias()) {
        zc::Maybe<const binder::ModuleDependencyAliasRoot&> aliasRoot;
        for (const auto& candidate : resolver.dependencyAliasRootInputs()) {
          if (!sameBytes(candidate.requester.encode().asPtr(),
                         request.key().requester().crate().encode().asPtr()) ||
              candidate.alias.text() != aliasText) {
            continue;
          }
          if (aliasRoot != zc::none) { return false; }
          aliasRoot = candidate;
        }
        if (aliasRoot == zc::none) { return false; }
        ZC_IF_SOME(root, aliasRoot) {
          auto path = cloneModulePath(root.target.path());
          for (size_t index = 1; index < request.normalizedPath().size(); ++index) {
            path.add(request.normalizedPath()[index].clone());
          }
          if (!addBucket(root.target.crate(), path.asPtr())) { return false; }
        }
      }
    }

    zc::TreeMap<zc::String, graph_query::ConfiguredDependencyAlias> aliasMap;
    for (const auto& request : requests) {
      if (request.isPrelude() || request.normalizedPath().size() == 0) { return false; }
      auto alias =
          identity::DependencyAlias::fromCanonical(request.normalizedPath().front().text());
      if (alias == zc::none) { return false; }
      auto key = resolution_query::DependencyAliasRootQueryKey::from(
          request.key().requester().crate().clone(), ZC_ASSERT_NONNULL(alias).clone());
      if (key == zc::none) { return false; }
      auto target = resolution_query::ExplicitModuleTarget::absent();
      for (const auto& candidate : resolver.dependencyAliasRootInputs()) {
        if (sameBytes(candidate.requester.encode().asPtr(),
                      request.key().requester().crate().encode().asPtr()) &&
            candidate.alias.text() == ZC_ASSERT_NONNULL(alias).text()) {
          if (target.target() != zc::none) { return false; }
          target = resolution_query::ExplicitModuleTarget::present(candidate.target.clone());
        }
      }
      const auto encoded = ZC_ASSERT_NONNULL(key).encode();
      auto sortKey = zc::encodeHex(encoded.asPtr());
      auto prior = aliasMap.find(sortKey);
      if (prior == zc::none) {
        aliasMap.insert(zc::mv(sortKey), graph_query::ConfiguredDependencyAlias{
                                             zc::mv(ZC_ASSERT_NONNULL(key)), zc::mv(target)});
      } else {
        ZC_IF_SOME(value, prior) {
          if (value.target.encode().asPtr() != target.encode().asPtr()) { return false; }
        }
      }
    }
    zc::Vector<graph_query::ConfiguredDependencyAlias> aliases(aliasMap.size());
    for (auto& entry : aliasMap) { aliases.add(zc::mv(entry.value)); }

    zc::Vector<graph_query::ConfiguredCratePrelude> preludes(catalogs.size());
    for (const auto& catalog : catalogs) {
      auto target = resolution_query::ExplicitModuleTarget::absent();
      if (catalog.crate().unit().kind() != identity::CompilationUnitKind::Toolchain) {
        auto projected = identity::projectToolchainCoreCrate(catalog.crate());
        if (projected == zc::none) { return false; }
        zc::Maybe<identity::ModuleKey> selectedPrelude;
        for (const auto& candidateCatalog : catalogs) {
          if (!sameBytes(candidateCatalog.crate().encode().asPtr(),
                         ZC_ASSERT_NONNULL(projected).encode().asPtr())) {
            continue;
          }
          for (const auto& selected : candidateCatalog.entries()) {
            if (selected.module().path().size() != 2 ||
                selected.module().path()[0].text() != "core"_zc ||
                selected.module().path()[1].text() != "prelude"_zc) {
              continue;
            }
            if (selectedPrelude != zc::none) { return false; }
            selectedPrelude = selected.module().clone();
          }
        }
        if (selectedPrelude == zc::none) { return false; }
        if (!addBucket(ZC_ASSERT_NONNULL(selectedPrelude).crate(),
                       ZC_ASSERT_NONNULL(selectedPrelude).path())) {
          return false;
        }
        target = resolution_query::ExplicitModuleTarget::present(
            zc::mv(ZC_ASSERT_NONNULL(selectedPrelude)));
      }
      preludes.add(graph_query::ConfiguredCratePrelude{catalog.crate().clone(), zc::mv(target)});
    }

    zc::Vector<resolution_query::CanonicalModuleCatalogBucket> buckets(bucketMap.size());
    for (auto& entry : bucketMap) { buckets.add(zc::mv(entry.value)); }

    zc::Vector<identity::CrateKey> projectedCoreCrates;
    ZC_IF_SOME(coreInputs, coreDistributionInputs) {
      projectedCoreCrates.reserve(coreInputs.projections().size());
      for (const auto& projection : coreInputs.projections()) {
        projectedCoreCrates.add(projection.crate().clone());
      }
    }
    zc::Maybe<incremental_binding_query::CompilationRootSetQueryKey> contextRoots;
    ZC_IF_SOME(request, packageRequest) {
      contextRoots = incremental_binding_query::CompilationRootSetQueryKey::fromVerified(
          request, projectedCoreCrates.asPtr());
    }
    if (contextRoots == zc::none) { return false; }

    const graph_query::ModuleGraphInputTransactionAuthority authority{
        ZC_ASSERT_NONNULL(packageRequest), ZC_ASSERT_NONNULL(coreDistributionInputs), resolver,
        parsedModuleInputs};
    const auto expectedPreviousRevision = queryDatabase.snapshot().revision();
    const auto priorLedger = graph_query::VerifiedModuleGraphInputLedger::empty();
    auto prepared = graph_query::VerifiedModuleGraphInputTransaction::prepare(
        authority, expectedPreviousRevision, ZC_ASSERT_NONNULL(contextRoots).clone(),
        zc::mv(projectedCoreCrates), zc::mv(catalogs), zc::mv(dependencySites), zc::mv(ancestries),
        zc::mv(buckets), zc::mv(searchRoots), zc::mv(aliases), zc::mv(preludes), priorLedger);
    if (prepared == zc::none) { return false; }
    auto commit = ZC_ASSERT_NONNULL(prepared).commit(queryDatabase);
    if (!commit.isCommitted()) { return false; }
    stagedCompilationRoots = zc::mv(ZC_ASSERT_NONNULL(contextRoots));

    const auto authorityStagingSnapshot = queryDatabase.snapshot();
    auto graph = authorityStagingSnapshot.get<graph_query::ModuleGraph>(
        ZC_ASSERT_NONNULL(stagedCompilationRoots));
    auto scc = authorityStagingSnapshot.get<graph_query::ModuleGraphScc>(
        ZC_ASSERT_NONNULL(stagedCompilationRoots));
    if (!graph.isRuntimeFailure() && graph.kind() == query::QueryValueKind::SemanticFailure) {
      auto dependencyFailure =
          graph_query::ModuleDependencyFailureRecord::decodeCanonical(graph.semanticFailureBytes());
      if (dependencyFailure == zc::none) { return false; }
      for (const auto& request : requests) {
        if (request.key().encode().asPtr() !=
            ZC_ASSERT_NONNULL(dependencyFailure).request().encode().asPtr()) {
          continue;
        }
        for (const auto& parsed : parsedModules) {
          const auto syntax = binder::DefinitionInventory::collect(parsed.parsedModule().tree());
          if (syntax.modules().size() > 1) { return false; }
          const auto moduleNode =
              syntax.modules().size() == 1 ? syntax.modules()[0].node : ast::NodeId();
          auto module = moduleIdentity(parsed.buffer(), moduleNode);
          if (module == zc::none || ZC_ASSERT_NONNULL(module) != request.requester()) { continue; }
          if (!binder::emitModuleDependencyResolutionFailure(
                  *diagnosticEngine, parsed.parsedModule(), request,
                  ZC_ASSERT_NONNULL(dependencyFailure).kind() ==
                      graph_query::ModuleDependencyFailureKind::Ambiguous)) {
            return false;
          }
          return false;
        }
      }
      return false;
    }
    if (graph.isRuntimeFailure() || scc.isRuntimeFailure() ||
        graph.kind() != query::QueryValueKind::Value ||
        scc.kind() != query::QueryValueKind::Value || scc.value().hasCycle(graph.value())) {
      return false;
    }
    ZC_IF_SOME(coreInputs, coreDistributionInputs) {
      if (coreInputs.projections().size() == 0) { return false; }
      for (const auto& projection : coreInputs.projections()) {
        auto coreKey = core_library_query::ContextualCoreCrateKey::from(
            ZC_ASSERT_NONNULL(stagedCompilationRoots).clone(), projection.crate().clone());
        if (coreKey == zc::none) { return false; }
        auto coreGraph = authorityStagingSnapshot.get<core_library_query::CoreModuleGraph>(
            zc::mv(ZC_ASSERT_NONNULL(coreKey)));
        if (coreGraph.isRuntimeFailure() || coreGraph.kind() != query::QueryValueKind::Value ||
            coreGraph.value().core().encode().asPtr() != projection.crate().encode().asPtr() ||
            coreGraph.value().modules().size() == 0) {
          return false;
        }
      }
    }
    return true;
  }

  bool sealFinalSnapshot() {
    if (stagedCompilationRoots == zc::none || finalSealedSnapshot != zc::none) { return false; }
    const auto snapshot = queryDatabase.snapshot();
    const auto& roots = ZC_ASSERT_NONNULL(stagedCompilationRoots);
    auto witness = module_graph_query::computeFinalSnapshotWitness(snapshot, roots);
    if (witness == zc::none) { return false; }
    auto seal =
        queryDatabase.sealInputs<module_graph_query::CompleteCompilationContextAuthorityInput>(
            snapshot, roots, ZC_ASSERT_NONNULL(witness));
    if (!seal.isSealed()) { return false; }
    auto admitted =
        queryDatabase
            .admitFinalSnapshot<module_graph_query::CompleteCompilationContextAuthorityInput>(
                queryDatabase.snapshot(), seal.seal());
    if (!admitted.isAdmitted()) { return false; }
    finalSealedSnapshot = zc::mv(admitted).takeSnapshot();
    stagedCompilationRoots = zc::none;
    return true;
  }

  bool stageMaterializedModuleGraphInputs() {
    if (packageRequest == zc::none) { return true; }
    if (crateGraph == zc::none) { return false; }
    const auto rejectInvariant = [&]() {
      diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(source::SourceLoc(),
                                                                            zc::str(uint64_t{1}));
      return false;
    };
    if (moduleKeys.size() == 0) { return rejectInvariant(); }

    zc::TreeMap<zc::String, size_t> canonicalBindings;
    zc::TreeMap<zc::String, size_t> canonicalCrates;
    for (size_t index = 0; index < moduleKeys.size(); ++index) {
      const auto& binding = moduleKeys[index];
      if (semanticContextResources.identityInterners().module(binding.key) == zc::none) {
        return rejectInvariant();
      }
      auto moduleSortKey = zc::encodeHex(binding.key.encode().asPtr());
      if (canonicalBindings.find(moduleSortKey) != zc::none) { return rejectInvariant(); }
      canonicalBindings.insert(zc::mv(moduleSortKey), index);

      auto crateSortKey = zc::encodeHex(binding.key.crate().encode().asPtr());
      if (canonicalCrates.find(crateSortKey) == zc::none) {
        canonicalCrates.insert(zc::mv(crateSortKey), index);
      }
    }
    if (canonicalBindings.size() != moduleKeys.size() || canonicalCrates.size() == 0) {
      return rejectInvariant();
    }

    zc::Vector<binder::ModuleSearchRoot> searchRoots(canonicalCrates.size());
    for (const auto& entry : canonicalCrates) {
      const auto& binding = moduleKeys[entry.value];
      const auto& crate = binding.key.crate();
      if (crate.unit().kind() == identity::CompilationUnitKind::Toolchain) {
        if (coreDistributionInputs == zc::none) { return rejectInvariant(); }
        ZC_IF_SOME(inputs, coreDistributionInputs) {
          auto root = binder::ModuleSearchRoot::toolchainCore(crate.clone(),
                                                              inputs.distribution().digest());
          if (root == zc::none) { return rejectInvariant(); }
          searchRoots.add(zc::mv(ZC_ASSERT_NONNULL(root)));
        }
        continue;
      }
      const auto& package = crate.unit().userPackage();
      auto root = compilationRoot(crate);
      if (root == zc::none) { return rejectInvariant(); }
      ZC_IF_SOME(rootValue, root) {
        const auto packageRelativeRoot = parentDirectory(rootValue.sourcePath());
        switch (package.source().kind()) {
          case identity::PackageSourceKind::LocalPath: {
            zc::Vector<identity::CanonicalPathSegment> workspaceSegments;
            for (const auto& segment : package.source().localPath().segments()) {
              workspaceSegments.add(segment.clone());
            }
            for (const auto& segment : packageRelativeRoot.segments()) {
              workspaceSegments.add(segment.clone());
            }
            searchRoots.add(binder::ModuleSearchRoot::workspace(
                crate.clone(),
                identity::CanonicalWorkspaceRelativePath::from(
                    package.source().localPath().leadingParents(), zc::mv(workspaceSegments))));
            break;
          }
          case identity::PackageSourceKind::Registry:
          case identity::PackageSourceKind::Vcs:
            searchRoots.add(binder::ModuleSearchRoot::package(crate.clone(), package.clone(),
                                                              packageRelativeRoot.clone()));
            break;
        }
        ZC_IF_SOME(results, buildScriptResults) {
          for (size_t index = 0; index < results.results().size(); ++index) {
            const auto& planKey = results.planKeys()[index];
            const auto& result = results.results()[index];
            if (!samePackage(package, planKey.package())) { continue; }
            zc::Vector<identity::CanonicalPathSegment> noSegments;
            searchRoots.add(binder::ModuleSearchRoot::generated(
                crate.clone(), result.output().producerKey(),
                identity::CanonicalRelativePath::from(zc::mv(noSegments))));
          }
        }
      }
    }

    zc::Vector<binder::ModuleGraphModule> modules(canonicalBindings.size());
    zc::Vector<binder::ParsedModuleGraphInput> parsedInputs(canonicalBindings.size());
    zc::Vector<binder::StructuralModuleCatalogEntry> catalog(canonicalBindings.size());
    zc::Vector<binder::RequesterModuleAncestryCandidate> requesterAncestry(
        canonicalBindings.size());
    for (const auto& entry : canonicalBindings) {
      const auto& binding = moduleKeys[entry.value];
      zc::Maybe<size_t> parsedRecordIndex;
      for (size_t index = 0; index < parsedModules.size(); ++index) {
        if (parsedModules[index].buffer() != binding.buffer) { continue; }
        if (parsedRecordIndex != zc::none) { return rejectInvariant(); }
        parsedRecordIndex = index;
      }
      if (parsedRecordIndex == zc::none) { return rejectInvariant(); }
      size_t selectedParsedRecordIndex = 0;
      ZC_IF_SOME(value, parsedRecordIndex) { selectedParsedRecordIndex = value; }
      const auto& parsedRecord = parsedModules[selectedParsedRecordIndex];

      auto module = semanticContextResources.identityInterners().module(binding.key);
      if (module == zc::none) { return rejectInvariant(); }
      ZC_IF_SOME(moduleValue, module) {
        const auto moduleId = moduleValue.handle();
        modules.add(binder::ModuleGraphModule(binding.key.clone(), moduleId));
        parsedInputs.add(binder::ParsedModuleGraphInput{moduleId, parsedRecord.parsedModule()});
        catalog.add(binder::StructuralModuleCatalogEntry(
            binding.key.clone(), moduleId,
            parsedModules[selectedParsedRecordIndex].parsedModule().source().clone()));

        zc::Vector<identity::ModuleKey> ancestry;
        ancestry.add(binding.key.clone());
        zc::Vector<identity::ModulePathSegment> currentPath = cloneModulePath(binding.key.path());
        while (currentPath.size() > 1) {
          currentPath.removeLast();
          zc::Maybe<identity::ModuleKey> parent;
          for (const auto& candidate : canonicalBindings) {
            const auto& candidateValue = moduleKeys[candidate.value].key;
            if (candidateValue.crate().encode().asPtr() != binding.key.crate().encode().asPtr() ||
                !sameModulePath(candidateValue.path(), currentPath.asPtr())) {
              continue;
            }
            if (parent != zc::none) { return rejectInvariant(); }
            parent = candidateValue.clone();
          }
          if (parent == zc::none) {
            parent = identity::ModuleKey::from(binding.key.crate().clone(),
                                               cloneModulePath(currentPath.asPtr()));
          }
          if (parent == zc::none) { return rejectInvariant(); }
          ZC_IF_SOME(parentValue, parent) { ancestry.add(zc::mv(parentValue)); }
        }
        requesterAncestry.add(
            binder::RequesterModuleAncestryCandidate(binding.key.clone(), zc::mv(ancestry)));
      }
    }
    bool sourceRejected = false;
    for (size_t index = 0; index < modules.size(); ++index) {
      auto failure = binder::ModuleGraphSourceFailureBuilder::buildToolchainModuleRootReserved(
          modules[index], parsedInputs[index]);
      ZC_IF_SOME(value, failure) {
        if (!binder::emitModuleGraphSourceFailure(*diagnosticEngine,
                                                  parsedInputs[index].parsedModule, value)) {
          return rejectInvariant();
        }
        sourceRejected = true;
      }
    }
    zc::Vector<binder::ModuleSourceSnapshotRevision> sourceSnapshots(parsedInputs.size());
    for (const auto& parsed : parsedInputs) {
      sourceSnapshots.add(binder::ModuleSourceSnapshotRevision(
          parsed.parsedModule.source().clone(), parsed.parsedModule.contentDigest()));
    }
    zc::Vector<binder::GeneratedModuleSourceRevision> generatedSourceRevisions;
    ZC_IF_SOME(results, buildScriptResults) {
      generatedSourceRevisions.reserve(results.results().size());
      for (const auto& result : results.results()) {
        generatedSourceRevisions.add(binder::GeneratedModuleSourceRevision(
            result.output().producerKey(), result.run().outputs().digest()));
      }
    }
    zc::Vector<binder::ModuleDependencyAliasRoot> dependencyAliasRoots;
    ZC_IF_SOME(resolvedCrates, crateGraph) {
      for (const auto& edge : resolvedCrates.edges()) {
        zc::Maybe<identity::ModuleKey> providerRoot;
        const auto providerBytes = edge.provider().encode();
        for (const auto& binding : moduleKeys) {
          const auto& candidateValue = binding.key;
          if (candidateValue.crate().encode().asPtr() != providerBytes.asPtr() ||
              candidateValue.path().size() != 1 ||
              candidateValue.path()[0].text() != edge.provider().targetName()) {
            continue;
          }
          if (providerRoot != zc::none) { return rejectInvariant(); }
          providerRoot = candidateValue.clone();
        }
        if (edge.origin().kind() != identity::CrateDependencyOriginKind::UserPackage) { continue; }
        auto alias =
            identity::DependencyAlias::fromCanonical(edge.origin().userPackageEdge().alias());
        if (alias == zc::none) { return rejectInvariant(); }
        if (providerRoot == zc::none) { return rejectInvariant(); }
        ZC_IF_SOME(rootValue, providerRoot) {
          ZC_IF_SOME(aliasValue, alias) {
            dependencyAliasRoots.add(binder::ModuleDependencyAliasRoot(
                edge.consumer().clone(), zc::mv(aliasValue), zc::mv(rootValue)));
          }
        }
      }
      for (const auto& consumer : resolvedCrates.crates()) {
        if (consumer.unit().kind() != identity::CompilationUnitKind::UserPackage) { continue; }
        auto projected = identity::projectToolchainCoreCrate(consumer);
        auto coreAlias = identity::DependencyAlias::fromCanonical("core"_zc);
        if (projected == zc::none || coreAlias == zc::none) { return rejectInvariant(); }
        zc::Maybe<identity::ModuleKey> projectedRoot;
        for (const auto& binding : moduleKeys) {
          const auto& candidateValue = binding.key;
          if (!sameBytes(candidateValue.crate().encode().asPtr(),
                         ZC_ASSERT_NONNULL(projected).encode().asPtr()) ||
              candidateValue.path().size() != 1 || candidateValue.path()[0].text() != "core"_zc) {
            continue;
          }
          if (projectedRoot != zc::none) { return rejectInvariant(); }
          projectedRoot = candidateValue.clone();
        }
        if (projectedRoot == zc::none) { return rejectInvariant(); }
        dependencyAliasRoots.add(binder::ModuleDependencyAliasRoot(
            consumer.clone(), zc::mv(ZC_ASSERT_NONNULL(coreAlias)),
            zc::mv(ZC_ASSERT_NONNULL(projectedRoot))));
      }
    }
    auto resolverResult = binder::StructuralModuleResolver::freeze(
        contextBrand,
        binder::ModuleResolutionEnvironmentRecord(
            zc::mv(searchRoots), zc::mv(sourceSnapshots), zc::mv(generatedSourceRevisions),
            zc::mv(dependencyAliasRoots), zc::mv(requesterAncestry)),
        zc::mv(catalog));
    if (!resolverResult.is<binder::StructuralModuleResolver>()) {
      const auto& failure = resolverResult.get<binder::ModuleResolutionInvariantFact>();
      diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(failure.occurrence));
      return false;
    }
    auto resolver = zc::mv(resolverResult.get<binder::StructuralModuleResolver>());
    zc::Vector<binder::ModuleDependencyRequest> requests;
    for (const auto& parsed : parsedInputs) {
      auto derived = binder::ModuleDependencyRequestDeriver::derive(parsed.module,
                                                                    parsed.parsedModule, resolver);
      if (!derived.is<zc::Vector<binder::ModuleDependencyRequest>>()) {
        const auto& failure = derived.get<binder::ModuleResolutionInvariantFact>();
        diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(failure.occurrence));
        return false;
      }
      auto derivedRequests = zc::mv(derived.get<zc::Vector<binder::ModuleDependencyRequest>>());
      for (auto& request : derivedRequests) { requests.add(zc::mv(request)); }
    }
    if (!stageVerifiedModuleGraphInputs(resolver, requests.asPtr(), parsedInputs.asPtr())) {
      if (diagnosticEngine->hasErrors()) { return false; }
      return rejectInvariant();
    }
    if (sourceRejected) { return false; }
    return true;
  }
};

// ================================================================================
// CompilerSession

ParsedModuleRecord::ParsedModuleRecord(const source::BufferId& buffer,
                                       binder::VerifiedParsedModule&& parsedModule) noexcept
    : bufferValue(buffer), parsedModuleValue(zc::mv(parsedModule)) {}

const source::BufferId& ParsedModuleRecord::buffer() const noexcept { return bufferValue; }

const binder::VerifiedParsedModule& ParsedModuleRecord::parsedModule() const noexcept {
  return parsedModuleValue;
}

CompilerSession::CompilerSession(identity::SemanticContextFactory& contextFactory,
                                 const basic::LangOptions& langOpts,
                                 const basic::CompilerOptions& compilerOpts)
    : impl(zc::heap<Impl>(contextFactory, langOpts, compilerOpts)) {}
CompilerSession::~CompilerSession() noexcept(false) = default;

zc::Maybe<source::BufferId> CompilerSession::addVerifiedPackageRoot(
    const package::FinalizedCompilationRoot& root) {
  if (impl->packageRequest == zc::none || impl->crateGraph == zc::none) { return zc::none; }
  bool admittedRoot = false;
  ZC_IF_SOME(graph, impl->crateGraph) {
    for (const auto& candidate : graph.roots()) {
      if (!sameBytes(candidate.packageKey().encode().asPtr(), root.packageKey().encode().asPtr()) ||
          !sameBytes(candidate.crateKey().encode().asPtr(), root.crateKey().encode().asPtr()) ||
          !sameRelativePath(candidate.sourcePath(), root.sourcePath())) {
        continue;
      }
      if (admittedRoot) { return zc::none; }
      admittedRoot = true;
    }
  }
  if (!admittedRoot) { return zc::none; }

  auto rootModule = identity::ModulePathSegment::fromCanonical(root.crateKey().targetName());
  if (rootModule == zc::none) { return zc::none; }
  zc::Vector<identity::ModulePathSegment> selectedPath;
  ZC_IF_SOME(segment, rootModule) { selectedPath.add(zc::mv(segment)); }
  bool added = false;
  auto registered =
      impl->registerVerifiedSource(root.crateKey(), root.sourcePath(), selectedPath.asPtr(), added);
  if (!added) { return zc::none; }
  return registered;
}

zc::Maybe<zc::Vector<ParsedModuleRecord>> CompilerSession::materializeParsedModules() const {
  if (impl->finalSealedSnapshot == zc::none || !impl->verifiedParsedSyntax) { return zc::none; }
  zc::TreeMap<zc::String, source::BufferId> canonicalOrder;
  for (const auto& candidate : impl->pendingSourceIdentities) {
    auto key = zc::encodeHex(candidate.value.encode().asPtr());
    if (canonicalOrder.find(key) != zc::none) { return zc::none; }
    canonicalOrder.insert(zc::mv(key), candidate.key);
  }

  zc::Vector<ParsedModuleRecord> results;
  for (const auto& entry : canonicalOrder) {
    auto sourceKey = impl->pendingSourceIdentities.find(entry.value);
    if (sourceKey == zc::none) { return zc::none; }
    auto queryKey = source_query::StableSourceQueryKey::fromVerified(ZC_ASSERT_NONNULL(sourceKey));
    auto sourceSnapshot = identity::ImmutableSourceSnapshot::from(
        ZC_ASSERT_NONNULL(sourceKey).clone(),
        zc::heapArray(impl->sourceManager->getEntireTextForBuffer(entry.value)));
    if (queryKey == zc::none || sourceSnapshot == zc::none) { return zc::none; }
    auto parsed = ZC_ASSERT_NONNULL(impl->finalSealedSnapshot)
                      .getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(queryKey));
    if (!parsed.isPublished()) { return zc::none; }
    auto verified = binder::ParsedModuleVerifier::verifyQueryResult(
        impl->contextBrand, ZC_ASSERT_NONNULL(sourceSnapshot), ZC_ASSERT_NONNULL(sourceKey),
        *impl->sourceManager, entry.value, parsed.lease().capability().clone());
    if (!verified.is<binder::VerifiedParsedModule>()) { return zc::none; }
    results.add(
        ParsedModuleRecord(entry.value, zc::mv(verified.get<binder::VerifiedParsedModule>())));
  }
  return zc::mv(results);
}

zc::ArrayPtr<const ParsedModuleRecord> CompilerSession::retainedParsedModules() const noexcept {
  return impl->parsedModules.asPtr();
}

bool CompilerSession::hasVerifiedParsedSyntax() const noexcept {
  return impl->verifiedParsedSyntax;
}

zc::Maybe<CompilerSession::MaterializedModuleGraphLease> CompilerSession::materializeModuleGraph()
    const {
  if (impl->finalSealedSnapshot == zc::none) { return zc::none; }
  const auto& snapshot = ZC_ASSERT_NONNULL(impl->finalSealedSnapshot);
  const auto& roots = snapshot.contextRoots();
  auto demand = snapshot.getCapability<module_graph_query::MaterializeModuleGraph>(roots);
  if (!demand.isPublished()) { return zc::none; }
  return zc::mv(demand).takeLease();
}

zc::Maybe<core::VerifiedCoreLibrary> CompilerSession::materializeCoreLibrary(
    const identity::CrateKey& coreCrate) const {
  if (impl->finalSealedSnapshot == zc::none) { return zc::none; }
  const auto& snapshot = ZC_ASSERT_NONNULL(impl->finalSealedSnapshot);
  auto coreKey = core_library_query::ContextualCoreCrateKey::from(snapshot.contextRoots().clone(),
                                                                  coreCrate.clone());
  if (coreKey == zc::none) { return zc::none; }
  auto graph =
      snapshot.get<core_library_query::CoreModuleGraph>(ZC_ASSERT_NONNULL(coreKey).clone());
  auto distribution =
      snapshot.get<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core());
  auto prelude =
      snapshot.get<core_library_query::CorePreludeSurface>(ZC_ASSERT_NONNULL(coreKey).clone());
  auto authority = snapshot.getCapability<core_library_query::MaterializeCoreAuthority>(
      zc::mv(ZC_ASSERT_NONNULL(coreKey)));
  if (graph.isRuntimeFailure() || distribution.isRuntimeFailure() || prelude.isRuntimeFailure() ||
      graph.kind() != query::QueryValueKind::Value ||
      distribution.kind() != query::QueryValueKind::Value ||
      prelude.kind() != query::QueryValueKind::Value || !authority.isPublished() ||
      graph.value().core().encode().asPtr() != coreCrate.encode().asPtr() ||
      prelude.value().core().encode().asPtr() != coreCrate.encode().asPtr()) {
    return zc::none;
  }
  zc::Vector<core::VerifiedCoreModule> modules(graph.value().modules().size());
  for (const auto& module : graph.value().modules()) {
    auto moduleKey = core_library_query::ContextualCoreModuleKey::from(
        snapshot.contextRoots().clone(), module.clone());
    if (moduleKey == zc::none) { return zc::none; }
    auto interface = snapshot.getCapability<core_library_query::FinalizeCoreModuleInterface>(
        zc::mv(ZC_ASSERT_NONNULL(moduleKey)));
    if (!interface.isPublished()) { return zc::none; }
    auto published = core::VerifiedCoreModule::from(module.clone(), zc::mv(interface).takeLease());
    if (published == zc::none) { return zc::none; }
    modules.add(zc::mv(ZC_ASSERT_NONNULL(published)));
  }
  return core::VerifiedCoreLibrary::from(
      authority.lease().capability().context(),
      authority.lease().capability().fingerprint().clone(), snapshot.contextRoots().clone(),
      snapshot.revision(), distribution.value().digest(), graph.value().clone(), zc::mv(modules),
      prelude.value().preludeModule().clone(), zc::mv(authority).takeLease());
}

zc::Maybe<checker::CheckerIdentityAuthority> CompilerSession::materializeCheckerIdentityAuthority()
    const {
  if (impl->finalSealedSnapshot == zc::none) { return zc::none; }
  const auto& finalSnapshot = ZC_ASSERT_NONNULL(impl->finalSealedSnapshot);
  const auto& roots = finalSnapshot.contextRoots();
  auto graphDemand = finalSnapshot.getCapability<module_graph_query::MaterializeModuleGraph>(roots);
  if (!graphDemand.isPublished()) { return zc::none; }
  zc::Vector<checker::CheckerIdentityAuthority::BoundModuleView> checkerViews;
  for (const auto& graphModule : graphDemand.lease().capability().modules()) {
    auto key = incremental_binding_query::ContextualModuleKey::from(roots.clone(),
                                                                    graphModule.key().clone());
    auto boundDemand =
        finalSnapshot.getCapability<module_graph_query::VerifyBoundModule>(zc::mv(key));
    if (!boundDemand.isPublished()) { return zc::none; }
    auto view = module_graph_query::CheckerBoundModuleView::from(zc::mv(boundDemand).takeLease());
    if (view == zc::none) { return zc::none; }
    checkerViews.add(zc::mv(ZC_ASSERT_NONNULL(view)));
  }
  return checker::CheckerIdentityAuthority::from(zc::mv(checkerViews));
}

zc::ArrayPtr<const checker::signature::CheckerVerificationFailure>
CompilerSession::getCheckerInvariantFailures() const noexcept {
  return impl->checkerFailures;
}

zc::ArrayPtr<const checker::signature::VerifiedSignatureFacts>
CompilerSession::getVerifiedSignatureFacts() const noexcept {
  return impl->signatureFacts;
}

zc::ArrayPtr<const checker::cross_module::ImportedSignatureView>
CompilerSession::getImportedSignatureViews() const noexcept {
  return impl->importedSignatureViews;
}

zc::ArrayPtr<const VerifiedModuleInterface> CompilerSession::getVerifiedModuleInterfaces()
    const noexcept {
  return impl->moduleInterfaces;
}

zc::Maybe<const checker::coherence::FrozenCoherenceView&> CompilerSession::getFrozenCoherenceView()
    const noexcept {
  ZC_IF_SOME(view, impl->coherenceView) { return view; }
  return zc::none;
}

zc::Maybe<checker::marker::MarkerProofResult> CompilerSession::proveMarker(
    identity::ModuleId requester, identity::DefId marker, identity::SemanticTypeId subject) {
  if (impl->semanticTypeStore.get() == nullptr || impl->markerPolicies == zc::none ||
      impl->coreLibrary == zc::none || impl->coherenceView == zc::none ||
      impl->checkerIdentityAuthority == zc::none ||
      impl->signatureFacts.size() != impl->importedSignatureViews.size()) {
    return zc::none;
  }
  size_t requesterIndex = impl->signatureFacts.size();
  for (size_t index = 0; index < impl->signatureFacts.size(); ++index) {
    if (impl->signatureFacts[index].module() == requester) {
      requesterIndex = index;
      break;
    }
  }
  if (requesterIndex == impl->signatureFacts.size()) return zc::none;
  ZC_IF_SOME(policies, impl->markerPolicies) {
    ZC_IF_SOME(coherence, impl->coherenceView) {
      const auto& authority = ZC_ASSERT_NONNULL(impl->checkerIdentityAuthority);
      auto bound = authority.boundModule(requester);
      if (bound == zc::none) return zc::none;
      const auto& boundView = ZC_ASSERT_NONNULL(bound);
      auto inventoryResult = checker::body::BodyFactRequirementInventoryBuilder::build(boundView);
      if (!inventoryResult.is<checker::body::VerifiedBodyFactRequirementInventory>()) {
        return zc::none;
      }
      auto inventory =
          zc::mv(inventoryResult).get<checker::body::VerifiedBodyFactRequirementInventory>();
      auto crate = authority.crate(boundView.crate());
      if (crate == zc::none) return zc::none;
      ZC_IF_SOME(crateEntry, crate) {
        const auto& standardMarkers =
            ZC_ASSERT_NONNULL(impl->coreLibrary).authorityLease().capability().authority();
        checker::body::BodyCheckingInput bodyInput{boundView.retain(),
                                                   authority,
                                                   policies,
                                                   standardMarkers,
                                                   impl->signatureFacts[requesterIndex],
                                                   impl->importedSignatureViews[requesterIndex],
                                                   coherence,
                                                   *impl->semanticTypeStore,
                                                   inventory,
                                                   crateEntry.key().semanticOptions()};
        auto input = checker::marker::MarkerProofInput::from(bodyInput);
        if (input == zc::none) return zc::none;
        ZC_IF_SOME(value, input) {
          checker::marker::MarkerProofEngine engine(zc::mv(value));
          return engine.prove(marker, subject);
        }
      }
    }
  }
  return zc::none;
}

zc::Maybe<const checker::checked::CheckedFactsRepository&>
CompilerSession::getCheckedFactsRepository() const noexcept {
  if (impl->checkedFactsRepository.get() == nullptr) { return zc::none; }
  return *impl->checkedFactsRepository;
}

zc::ArrayPtr<const checker::checked::CheckedEvidenceLease>
CompilerSession::getCheckedEvidenceLeases() const noexcept {
  return impl->checkedEvidence;
}

zc::ArrayPtr<const checker::dispatch::VerifiedDispatchFacts>
CompilerSession::getVerifiedDispatchFacts() const noexcept {
  return impl->dispatchFacts;
}

zc::Maybe<const borrow_evidence::BorrowEvidenceRepository&>
CompilerSession::getBorrowEvidenceRepository() const noexcept {
  if (impl->borrowEvidenceRepository.get() == nullptr) { return zc::none; }
  return *impl->borrowEvidenceRepository;
}

zc::ArrayPtr<const hir::VerifiedHirModule> CompilerSession::getVerifiedHirModules() const noexcept {
  return impl->hirModules;
}

zc::ArrayPtr<const ownership::OwnershipCheckedMir> CompilerSession::getOwnershipCheckedMirModules()
    const noexcept {
  return impl->ownershipCheckedMirModules;
}

zc::ArrayPtr<const ownership::ValidatedOwnershipProofs>
CompilerSession::getValidatedOwnershipProofs() const noexcept {
  return impl->validatedOwnershipProofs;
}

zc::ArrayPtr<const ownership::VerifiedExecutableMir>
CompilerSession::getVerifiedExecutableMirModules() const noexcept {
  return impl->verifiedExecutableMirModules;
}

zc::Maybe<ownership::OwnershipEventOverlayInput> CompilerSession::getOwnershipEventOverlayInput(
    identity::ModuleId module) const noexcept {
  if (impl->checkerIdentityAuthority == zc::none || impl->markerPolicies == zc::none ||
      impl->coreLibrary == zc::none || impl->coherenceView == zc::none ||
      impl->semanticTypeStore.get() == nullptr ||
      impl->signatureFacts.size() != impl->importedSignatureViews.size() ||
      impl->signatureFacts.size() != impl->bodyRequirements.size() ||
      impl->signatureFacts.size() != impl->hirModules.size() ||
      impl->signatureFacts.size() != impl->ownershipCheckedMirModules.size() ||
      impl->signatureFacts.size() != impl->ownershipAdmittedModules.size()) {
    return zc::none;
  }
  for (size_t index = 0; index < impl->hirModules.size(); ++index) {
    const auto& hirModule = impl->hirModules[index];
    const auto& builtMir = impl->ownershipCheckedMirModules[index].builtMir();
    const auto& admittedModule = impl->ownershipAdmittedModules[index];
    if (hirModule.module() != module || builtMir.module() != module) continue;
    const auto& authority = ZC_ASSERT_NONNULL(impl->checkerIdentityAuthority);
    const auto& coherence = ZC_ASSERT_NONNULL(impl->coherenceView);
    auto crate = authority.crate(admittedModule.crate());
    if (crate == zc::none) return zc::none;
    ZC_IF_SOME(crateEntry, crate) {
      const auto& standardMarkers =
          ZC_ASSERT_NONNULL(impl->coreLibrary).authorityLease().capability().authority();
      return ownership::OwnershipEventOverlayInput{
          admittedModule, hirModule.admittedCheckedModule(), hirModule, builtMir,
          checker::body::BodyCheckingInput{
              admittedModule.boundModule().retain(), authority,
              ZC_ASSERT_NONNULL(impl->markerPolicies), standardMarkers, impl->signatureFacts[index],
              impl->importedSignatureViews[index], coherence, *impl->semanticTypeStore,
              impl->bodyRequirements[index], crateEntry.key().semanticOptions()}};
    }
    return zc::none;
  }
  return zc::none;
}

zc::Maybe<StagedOwnershipMirForTesting>
CompilerSession::firstStagedBorrowSourceRejectionForTesting() const noexcept {
  if (impl->stagedBorrowSourceRejectedBuiltMir.size() != 1 ||
      impl->stagedBorrowSourceRejectedOverlays.size() != 1 ||
      impl->stagedBorrowSourceRejectedBorrowEvidence.get() == nullptr) {
    return zc::none;
  }
  return StagedOwnershipMirForTesting{impl->stagedBorrowSourceRejectedBuiltMir[0],
                                      impl->stagedBorrowSourceRejectedOverlays[0],
                                      *impl->stagedBorrowSourceRejectedBorrowEvidence};
}

zc::ArrayPtr<const ir::IrDiagnosticGroup> CompilerSession::getIrFailureGroups() const noexcept {
  return impl->irFailureGroups;
}

zc::ArrayPtr<const identity::IdentityInvariant> CompilerSession::getIrIdentityInvariantFailures()
    const noexcept {
  return impl->irIdentityInvariantFailures;
}

const diagnostics::DiagnosticEngine& CompilerSession::getDiagnosticEngine() const {
  return *impl->diagnosticEngine;
}

diagnostics::DiagnosticEngine& CompilerSession::getDiagnosticEngine() {
  return *impl->diagnosticEngine;
}

bool CompilerSession::parseSources() {
  if (impl->diagnosticEngine->hasErrors() || impl->verifiedParsedSyntax) { return false; }
  if (impl->packageRequest == zc::none) { return true; }
  if (impl->coreDistributionInputs == zc::none) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
        source::SourceLoc(), zc::str(uint64_t{1}));
    return false;
  }
  if (!impl->internPackageAndCrateIdentities()) { return false; }

  if (impl->crateGraph == zc::none || impl->pendingSourceIdentities.size() == 0) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
        source::SourceLoc(), zc::str(uint64_t{1}));
    return false;
  }
  ZC_IF_SOME(graph, impl->crateGraph) {
    for (const auto& root : graph.roots()) {
      bool found = false;
      for (const auto& source : impl->pendingSourceIdentities) {
        if (sameBytes(source.value.crate().encode().asPtr(), root.crateKey().encode().asPtr())) {
          found = true;
          break;
        }
      }
      if (!found) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
    }
  }

  zc::Vector<source::BufferId> processed;
  while (true) {
    zc::TreeMap<zc::String, source::BufferId> worklist;
    for (const auto& source : impl->pendingSourceIdentities) {
      bool alreadyProcessed = false;
      for (const auto& buffer : processed) {
        if (buffer == source.key) {
          alreadyProcessed = true;
          break;
        }
      }
      if (alreadyProcessed) { continue; }
      auto key = zc::encodeHex(source.value.encode().asPtr());
      if (worklist.find(key) != zc::none) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
      worklist.insert(zc::mv(key), source.key);
    }
    if (worklist.size() == 0) { break; }
    if (!impl->stageParseSourceInputs()) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
      return false;
    }
    auto parseSnapshot = impl->queryDatabase.snapshot();

    for (const auto& entry : worklist) {
      auto sourceKey = impl->pendingSourceIdentities.find(entry.value);
      if (sourceKey == zc::none) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{1}));
        return false;
      }
      ZC_IF_SOME(sourceValue, sourceKey) {
        auto queryKey = source_query::StableSourceQueryKey::fromVerified(sourceValue);
        if (queryKey == zc::none) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        auto parsed =
            parseSnapshot.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(queryKey));
        if (parsed.isRuntimeRejected()) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        if (parsed.isSourceRejected()) {
          zc::Maybe<source_query::CanonicalCompilationOptions> compilationOptions;
          auto sourceSnapshot = identity::ImmutableSourceSnapshot::from(
              sourceValue.clone(),
              zc::heapArray(impl->sourceManager->getEntireTextForBuffer(entry.value)));
          zc::Maybe<source_query::CanonicalSourceSnapshot> canonicalSource;
          ZC_IF_SOME(request, impl->packageRequest) {
            compilationOptions = source_query::CanonicalCompilationOptions::fromVerified(request);
          }
          ZC_IF_SOME(snapshot, sourceSnapshot) {
            canonicalSource = source_query::CanonicalSourceSnapshot::fromVerified(snapshot);
          }
          if (compilationOptions == zc::none || canonicalSource == zc::none) {
            impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
                source::SourceLoc(), zc::str(uint64_t{1}));
            return false;
          }
          auto rejected = parser::reconstructParseRejection(
              ZC_ASSERT_NONNULL(queryKey), ZC_ASSERT_NONNULL(compilationOptions),
              ZC_ASSERT_NONNULL(canonicalSource), parsed.diagnostics().values());
          if (rejected == zc::none) {
            impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
                source::SourceLoc(), zc::str(uint64_t{1}));
            return false;
          }
          static_cast<void>(publishSourceDiagnostics(
              ZC_ASSERT_NONNULL(rejected).facts(), ZC_ASSERT_NONNULL(rejected).provenance(),
              sourceValue, *impl->sourceManager, entry.value, *impl->diagnosticEngine));
          return false;
        }
        if (!parsed.isPublished()) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{1}));
          return false;
        }
        if (impl->compilerOpts.emission.outputType !=
            basic::CompilerOptions::EmissionOptions::OutputType::AST) {
          auto requests =
              extractStructuralModuleDependencyRequests(parsed.lease().capability().tree());
          if (!requests.is<zc::Vector<StructuralModuleDependencyRequest>>()) {
            impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
                source::SourceLoc(), zc::str(uint64_t{1}));
            return false;
          }
          bool addedAny = false;
          if (!impl->discoverDependencies(
                  entry.value, requests.get<zc::Vector<StructuralModuleDependencyRequest>>(),
                  addedAny)) {
            impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
                source::SourceLoc(), zc::str(uint64_t{1}));
            return false;
          }
        }
        processed.add(entry.value);
      }
    }
  }

  if (processed.size() != impl->pendingSourceIdentities.size() || !impl->internSourceIdentities()) {
    if (!impl->diagnosticEngine->hasErrors()) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
    }
    return false;
  }

  zc::TreeMap<zc::String, source::BufferId> canonicalOrder;
  for (const auto& candidate : impl->pendingSourceIdentities) {
    auto key = zc::encodeHex(candidate.value.encode().asPtr());
    if (canonicalOrder.find(key) != zc::none) { return false; }
    canonicalOrder.insert(zc::mv(key), candidate.key);
  }
  auto parseSnapshot = impl->queryDatabase.snapshot();
  for (const auto& entry : canonicalOrder) {
    auto sourceKey = impl->pendingSourceIdentities.find(entry.value);
    if (sourceKey == zc::none) { return false; }
    auto queryKey = source_query::StableSourceQueryKey::fromVerified(ZC_ASSERT_NONNULL(sourceKey));
    if (queryKey == zc::none) { return false; }
    auto parsed =
        parseSnapshot.getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(queryKey));
    if (!parsed.isPublished()) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
      return false;
    }
    if (!publishSourceDiagnostics(parsed.lease().capability().facts(),
                                  parsed.lease().capability().provenance(),
                                  ZC_ASSERT_NONNULL(sourceKey), *impl->sourceManager, entry.value,
                                  *impl->diagnosticEngine)) {
      return false;
    }
    auto materializedSnapshot = identity::ImmutableSourceSnapshot::from(
        ZC_ASSERT_NONNULL(sourceKey).clone(),
        zc::heapArray(impl->sourceManager->getEntireTextForBuffer(entry.value)));
    if (materializedSnapshot == zc::none) { return false; }
    auto verified = binder::ParsedModuleVerifier::verifyQueryResult(
        impl->contextBrand, ZC_ASSERT_NONNULL(materializedSnapshot), ZC_ASSERT_NONNULL(sourceKey),
        *impl->sourceManager, entry.value, parsed.lease().capability().clone());
    if (!verified.is<binder::VerifiedParsedModule>()) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
      return false;
    }
    impl->parsedModules.add(
        ParsedModuleRecord(entry.value, zc::mv(verified.get<binder::VerifiedParsedModule>())));
  }
  impl->verifiedParsedSyntax = true;

  if (impl->compilerOpts.emission.outputType ==
      basic::CompilerOptions::EmissionOptions::OutputType::AST) {
    return !impl->diagnosticEngine->hasErrors();
  }

  if (impl->diagnosticEngine->hasErrors() || !impl->internModuleIdentities() ||
      !impl->stageMaterializedModuleGraphInputs()) {
    return false;
  }
  const auto identityAdmissionSnapshot = impl->queryDatabase.snapshot();
  for (const auto& binding : impl->moduleKeys) {
    auto moduleKey = incremental_binding_query::StableModuleQueryKey::fromVerified(binding.key);
    if (moduleKey == zc::none) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
      return false;
    }
    auto admission = identityAdmissionSnapshot
                         .getCapability<incremental_binding_query::StableIdentityAdmissionQuery>(
                             zc::mv(ZC_ASSERT_NONNULL(moduleKey)));
    if (admission.isSourceRejected()) {
      for (const auto& diagnostic : admission.diagnostics().values()) {
        zc::Vector<diagnostics::DiagnosticArgument> arguments(diagnostic.arguments().size());
        for (const auto& argument : diagnostic.arguments()) { arguments.add(zc::str(argument)); }
        impl->diagnosticEngine->emit(
            diagnostics::Diagnostic(diagnostic.code(), source::SourceLoc(), zc::mv(arguments)));
      }
      return false;
    }
    if (!admission.isPublished()) {
      impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
          source::SourceLoc(), zc::str(uint64_t{1}));
      return false;
    }
  }
  if (impl->stagedCompilationRoots == zc::none) { return false; }
  const auto authorityStagingSnapshot = impl->queryDatabase.snapshot();
  const auto expectedPreviousRevision = authorityStagingSnapshot.revision();
  const incremental_binding_query::ContextualIdentityAuthorityInputLedger priorAuthorityInputs;
  auto authorityTransaction =
      incremental_binding_query::ContextualIdentityAuthorityInputTransaction::prepare(
          authorityStagingSnapshot, expectedPreviousRevision,
          ZC_ASSERT_NONNULL(impl->stagedCompilationRoots), priorAuthorityInputs);
  if (authorityTransaction == zc::none) { return false; }
  auto authorityCommit = ZC_ASSERT_NONNULL(authorityTransaction).commit(impl->queryDatabase);
  if (!authorityCommit.isCommitted()) { return false; }
  if (!impl->sealFinalSnapshot()) { return false; }
  impl->parsedModules.clear();
  return !impl->diagnosticEngine->hasErrors();
}

bool CompilerSession::bindSources() {
  if (impl->diagnosticEngine->hasErrors()) { return false; }
  if (impl->packageRequest == zc::none) { return true; }
  auto authority = materializeCheckerIdentityAuthority();
  if (authority == zc::none) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
        source::SourceLoc(), zc::str(uint64_t{1}));
    return false;
  }
  if (ZC_REQUIRE_NONNULL(authority).modules().size() == 0) {
    impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
        source::SourceLoc(), zc::str(uint64_t{2}));
    return false;
  }
  for (const auto& module : ZC_REQUIRE_NONNULL(authority).modules()) {
    for (const auto& lookup : module.bindings().failedLookups()) {
      if (!module.tree().contains(lookup.node)) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{3}));
        return false;
      }
      auto span = module.parsedModule().spanFor(module.tree().node(lookup.node).range);
      if (span == zc::none) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{4}));
        return false;
      }
      zc::Maybe<source::BufferId> buffer;
      for (const auto& candidate : impl->pendingSourceIdentities) {
        if (candidate.value.encode().asPtr() != module.parsedModule().source().encode().asPtr()) {
          continue;
        }
        if (buffer != zc::none) {
          impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
              source::SourceLoc(), zc::str(uint64_t{5}));
          return false;
        }
        buffer = candidate.key;
      }
      if (buffer == zc::none) {
        impl->diagnosticEngine->diagnose<diagnostics::DiagID::ModuleGraphInvariant>(
            source::SourceLoc(), zc::str(uint64_t{6}));
        return false;
      }
      const auto location =
          impl->sourceManager->getLocForBufferStart(ZC_ASSERT_NONNULL(buffer))
              .getAdvancedLoc(static_cast<unsigned>(ZC_ASSERT_NONNULL(span).byteStart()));
      const auto& outcome = lookup.outcome.value();
      if (outcome.is<binder::StableMissingLookupOutcome>()) {
        if (!binder::BindingDiagnosticAdapter::emitLookupFailure(
                *impl->diagnosticEngine, binder::BinderDiagnosticCode::UndefinedIdentifier,
                location, binder::VerifiedIdentifierArgument::from(lookup.name),
                lookup.nameSpace)) {
          return false;
        }
      }
    }
  }
  return !impl->diagnosticEngine->hasErrors();
}
bool CompilerSession::checkSources() {
  if (impl->diagnosticEngine->hasErrors() || !impl->checkerFailures.empty() ||
      !impl->irFailureGroups.empty() || !impl->irIdentityInvariantFailures.empty()) {
    return false;
  }
  if (impl->packageRequest == zc::none) { return true; }
  if (impl->semanticTypeStore.get() == nullptr || impl->factStoreBrands == zc::none) {
    return false;
  }
  if (impl->verifiedCheckedSources) { return true; }
  if (!impl->signatureFacts.empty() || !impl->importedSignatureViews.empty() ||
      !impl->moduleInterfaces.empty() || impl->checkerIdentityAuthority != zc::none ||
      impl->coherenceView != zc::none || impl->checkedFactsRepository.get() != nullptr ||
      !impl->checkedEvidence.empty() || !impl->dispatchFacts.empty() ||
      impl->borrowEvidenceRepository.get() != nullptr || !impl->hirModules.empty() ||
      !impl->ownershipCheckedMirModules.empty()) {
    return false;
  }

  if (impl->finalSealedSnapshot == zc::none) { return false; }
  const auto& finalSnapshot = ZC_ASSERT_NONNULL(impl->finalSealedSnapshot);
  auto graphDemand = finalSnapshot.getCapability<module_graph_query::MaterializeModuleGraph>(
      finalSnapshot.contextRoots());
  if (!graphDemand.isPublished()) { return false; }
  for (const auto& crate : graphDemand.lease().capability().crates()) {
    if (crate.key().unit().kind() != identity::CompilationUnitKind::Toolchain ||
        crate.key().unit().toolchain().component() != identity::ToolchainComponent::Core) {
      continue;
    }
    auto coreKey = core_library_query::ContextualCoreCrateKey::from(
        finalSnapshot.contextRoots().clone(), crate.key().clone());
    if (coreKey == zc::none) { return false; }
    auto preludeSurface = finalSnapshot.get<core_library_query::CorePreludeSurface>(
        ZC_ASSERT_NONNULL(coreKey).clone());
    if (preludeSurface.isRuntimeFailure() ||
        preludeSurface.kind() != query::QueryValueKind::Value ||
        preludeSurface.value().core().encode().asPtr() != crate.key().encode().asPtr()) {
      return false;
    }
    auto roleAuthority = finalSnapshot.get<core_library_query::CoreRoleAuthority>(
        ZC_ASSERT_NONNULL(coreKey).clone());
    if (roleAuthority.isRuntimeFailure() || roleAuthority.kind() != query::QueryValueKind::Value ||
        roleAuthority.value().core().encode().asPtr() != crate.key().encode().asPtr() ||
        roleAuthority.value().preludeRevision().digest() !=
            preludeSurface.value().revision().digest() ||
        roleAuthority.value().roles().size() != preludeSurface.value().roles().size()) {
      return false;
    }
    auto materializedAuthority =
        finalSnapshot.getCapability<core_library_query::MaterializeCoreAuthority>(
            zc::mv(ZC_ASSERT_NONNULL(coreKey)));
    if (!materializedAuthority.isPublished() ||
        materializedAuthority.lease().capability().record().revision().digest() !=
            roleAuthority.value().revision().digest() ||
        materializedAuthority.lease().capability().record().roleSeedRevision().digest() !=
            roleAuthority.value().roleSeedRevision().digest() ||
        materializedAuthority.lease().capability().authority().prelude().encode().asPtr() !=
            preludeSurface.value().preludeModule().encode().asPtr()) {
      return false;
    }
    for (const auto& module : graphDemand.lease().capability().modules()) {
      if (module.key().crate().encode().asPtr() != crate.key().encode().asPtr()) { continue; }
      auto finalInterfaceKey = core_library_query::ContextualCoreModuleKey::from(
          finalSnapshot.contextRoots().clone(), module.key().clone());
      if (finalInterfaceKey == zc::none) { return false; }
      auto finalInterface =
          finalSnapshot.getCapability<core_library_query::FinalizeCoreModuleInterface>(
              zc::mv(ZC_ASSERT_NONNULL(finalInterfaceKey)));
      if (!finalInterface.isPublished() ||
          finalInterface.lease().capability().record().module().encode().asPtr() !=
              module.key().encode().asPtr() ||
          finalInterface.lease().capability().record().coreContext().digest() !=
              materializedAuthority.lease().capability().record().coreContext().digest() ||
          finalInterface.lease().capability().record().authorityRevision().digest() !=
              materializedAuthority.lease().capability().authority().revision().digest()) {
        return false;
      }
    }
  }

  auto checkerAuthority = materializeCheckerIdentityAuthority();
  if (checkerAuthority == zc::none) { return false; }
  const auto& retainedCheckerAuthority = ZC_ASSERT_NONNULL(checkerAuthority);
  const auto boundModules = retainedCheckerAuthority.modules();
  if (boundModules.size() == 0) {
    impl->verifiedCheckedSources = true;
    return true;
  }

  const auto parsedFor = [&](identity::ModuleId module) -> zc::Maybe<binder::VerifiedParsedModule> {
    auto boundModule = retainedCheckerAuthority.boundModule(module);
    if (boundModule == zc::none || impl->finalSealedSnapshot == zc::none) { return zc::none; }
    const auto& source = ZC_ASSERT_NONNULL(boundModule).boundModuleLease().capability().source();
    zc::Maybe<source::BufferId> selectedBuffer;
    zc::Maybe<identity::SourceFileKey> selectedSource;
    for (const auto& candidate : impl->pendingSourceIdentities) {
      if (candidate.value.encode().asPtr() != source.encode().asPtr()) { continue; }
      if (selectedBuffer != zc::none || selectedSource != zc::none) { return zc::none; }
      selectedBuffer = candidate.key;
      selectedSource = candidate.value.clone();
    }
    if (selectedBuffer == zc::none || selectedSource == zc::none) { return zc::none; }
    auto queryKey =
        source_query::StableSourceQueryKey::fromVerified(ZC_ASSERT_NONNULL(selectedSource));
    auto materializedSnapshot = identity::ImmutableSourceSnapshot::from(
        ZC_ASSERT_NONNULL(selectedSource).clone(),
        zc::heapArray(
            impl->sourceManager->getEntireTextForBuffer(ZC_ASSERT_NONNULL(selectedBuffer))));
    if (queryKey == zc::none || materializedSnapshot == zc::none) { return zc::none; }
    auto parsed = ZC_ASSERT_NONNULL(impl->finalSealedSnapshot)
                      .getCapability<parser::ParseSourceQuery>(ZC_ASSERT_NONNULL(queryKey));
    if (!parsed.isPublished()) { return zc::none; }
    auto verified = binder::ParsedModuleVerifier::verifyQueryResult(
        impl->contextBrand, ZC_ASSERT_NONNULL(materializedSnapshot),
        ZC_ASSERT_NONNULL(selectedSource), *impl->sourceManager, ZC_ASSERT_NONNULL(selectedBuffer),
        parsed.lease().capability().clone());
    if (!verified.is<binder::VerifiedParsedModule>()) { return zc::none; }
    return zc::mv(verified.get<binder::VerifiedParsedModule>());
  };
  const auto locationFor = [&](const binder::VerifiedParsedModule& parsed,
                               const identity::SourceSpan& span) {
    ZC_IF_SOME(location, parsed.sourceLocFor(span)) { return location; }
    return source::SourceLoc();
  };
  const auto rejectChecker =
      [&](identity::ModuleId module,
          zc::Vector<checker::signature::CheckerVerificationFailure>&& failures) {
        for (auto& failure : failures) { impl->checkerFailures.add(zc::mv(failure)); }
        ZC_IF_SOME(parsed, parsedFor(module)) {
          checker::emitCheckerVerificationFailures(*impl->diagnosticEngine, parsed,
                                                   impl->checkerFailures.asPtr());
        }
        return false;
      };
  const auto rejectOne = [&](identity::ModuleId module,
                             checker::signature::CheckerInvariantKind kind,
                             checker::signature::CheckerInvariantStage stage, uint32_t ordinal) {
    zc::Vector<checker::signature::CheckerVerificationFailure> failures;
    failures.add(
        checker::signature::CheckerVerificationFailure(checker::signature::CheckerInvariantFact{
            kind, stage, module, zc::none, zc::none, zc::none, zc::Vector<uint32_t>(), zc::none,
            zc::none, ordinal}));
    return rejectChecker(module, zc::mv(failures));
  };
  const auto rejectDispatch = [&](identity::ModuleId module,
                                  checker::dispatch::DispatchFactsInvariantRejected&& rejected) {
    ZC_IF_SOME(parsed, parsedFor(module)) {
      checker::emitDispatchVerificationFailures(*impl->diagnosticEngine, parsed,
                                                rejected.failures.asPtr());
    }
    return false;
  };
  const auto emitSignatureSource = [&](const binder::VerifiedParsedModule& parsed,
                                       const checker::signature::SignatureFactsSourceRejected&
                                           rejected,
                                       const checker::CheckerIdentityAuthority& identities) {
    for (const auto& failure : rejected.failures) {
      const auto id = static_cast<diagnostics::DiagID>(failure.diagnostic);
      const auto location = locationFor(parsed, failure.primarySpan);
      if (failure.diagnostic ==
          checker::signature::SignatureSourceDiagnostic::BodyLiteralOutOfRange) {
        if (failure.arguments.size() != 2 ||
            !failure.arguments[0].variant().is<checker::signature::SignatureLiteralDisplayArg>() ||
            !failure.arguments[1]
                 .variant()
                 .is<checker::signature::SignaturePrimitiveTypeDisplayArg>()) {
          return false;
        }
        auto literal = checker::checked::CheckerDisplayArgument(checker::checked::LiteralDisplayArg{
            failure.arguments[0]
                .variant()
                .get<checker::signature::SignatureLiteralDisplayArg>()
                .literal.clone()});
        auto primitive =
            checker::checked::CheckerDisplayArgument(checker::checked::PrimitiveTypeDisplayArg{
                failure.arguments[1]
                    .variant()
                    .get<checker::signature::SignaturePrimitiveTypeDisplayArg>()
                    .kind});
        impl->diagnosticEngine->emit(diagnostics::Diagnostic(
            id, location,
            checker::renderCheckerDisplayArgument(literal, identities, *impl->semanticTypeStore),
            checker::renderCheckerDisplayArgument(primitive, identities,
                                                  *impl->semanticTypeStore)));
      } else if (failure.diagnostic ==
                     checker::signature::SignatureSourceDiagnostic::ConflictingImpl ||
                 failure.diagnostic == checker::signature::SignatureSourceDiagnostic::OrphanImpl) {
        impl->diagnosticEngine->emit(
            diagnostics::Diagnostic(id, location, "interface"_zc, "type"_zc));
      } else {
        impl->diagnosticEngine->emit(diagnostics::Diagnostic(id, location));
      }
    }
    for (const auto& advisory : rejected.advisories) {
      impl->diagnosticEngine->emit(
          diagnostics::Diagnostic(advisory.diagnostic, locationFor(parsed, advisory.primarySpan)));
    }
    return true;
  };
  const auto rejectIrIdentity = [&](const ir::SortedIdentityInvariantFacts& failures) {
    ir::emitIrIdentityInvariantFailures(*impl->diagnosticEngine, failures);
    for (const auto& failure : failures.facts()) {
      impl->irIdentityInvariantFailures.add(failure.clone());
    }
    return false;
  };
  const auto rejectIrCapability = [&](const ir::SortedCapabilityFailureFacts& failures) {
    auto groups = ir::groupIrCapabilityFailures(failures);
    ir::emitIrDiagnosticGroups(*impl->diagnosticEngine, groups.asPtr());
    impl->irFailureGroups = zc::mv(groups);
    return false;
  };
  const auto rejectIrInvariant = [&](const ir::SortedIrInvariantFailureFacts& failures) {
    auto groups = ir::groupIrInvariantFailures(failures);
    ir::emitIrDiagnosticGroups(*impl->diagnosticEngine, groups.asPtr());
    impl->irFailureGroups = zc::mv(groups);
    return false;
  };

  zc::Vector<ownership::OwnershipAdmittedBoundModule> checkerModules(boundModules.size());
  for (const auto& boundModule : boundModules) {
    const auto module = boundModule.module();
    auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(boundModule.retain());
    if (admission.is<ownership::OwnershipSurfaceSourceRejected>()) {
      auto parsed = parsedFor(module);
      if (parsed == zc::none) {
        return rejectOne(module, checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                         checker::signature::CheckerInvariantStage::Signature, 0);
      }
      ZC_IF_SOME(parsedModule, parsed) {
        // Surface admission failures are mapped inline until they migrate to the
        // closed OwnershipSourceFailure union and emitOwnershipSourceFailures.
        for (const auto& failure :
             admission.get<ownership::OwnershipSurfaceSourceRejected>().failures()) {
          diagnostics::DiagID diagnostic = diagnostics::DiagID::ControlFlowSemanticsUnavailable;
          if (failure.kind == ownership::OwnershipSurfaceSyntaxKind::Spawn ||
              failure.kind == ownership::OwnershipSurfaceSyntaxKind::Suspend) {
            diagnostic = diagnostics::DiagID::ConcurrencySemanticsUnavailable;
          } else if (failure.kind == ownership::OwnershipSurfaceSyntaxKind::VoidReturn) {
            diagnostic = diagnostics::DiagID::VoidReturnSemanticsUnavailable;
          } else if (failure.kind == ownership::OwnershipSurfaceSyntaxKind::ExpressionStatement) {
            diagnostic = diagnostics::DiagID::ExpressionStatementSemanticsUnavailable;
          } else if (failure.kind == ownership::OwnershipSurfaceSyntaxKind::FunctionBody) {
            diagnostic = diagnostics::DiagID::FunctionBodySemanticsUnavailable;
          }
          impl->diagnosticEngine->emit(
              diagnostics::Diagnostic(diagnostic, locationFor(parsedModule, failure.primarySpan)));
        }
      }
      return false;
    }
    checkerModules.add(zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>());
  }

  zc::Maybe<checker::signature::VerifiedMarkerShapeInventory> stagedMarkerShapes;
  zc::Maybe<checker::signature::VerifiedMarkerPolicyRegistry> stagedMarkerPolicies;
  zc::Vector<checker::signature::VerifiedSignatureFacts> stagedSignatureFacts;
  zc::Vector<checker::cross_module::ImportedSignatureView> stagedImportedSignatureViews;
  zc::Vector<VerifiedModuleInterface> stagedModuleInterfaces;
  zc::Maybe<checker::coherence::FrozenCoherenceView> stagedCoherenceView;
  auto stagedCheckedFactsRepository =
      zc::heap<checker::checked::CheckedFactsRepository>(impl->contextBrand);
  zc::Vector<checker::checked::CheckedEvidenceLease> stagedCheckedEvidence;
  zc::Vector<checker::dispatch::VerifiedDispatchFacts> stagedDispatchFacts;
  zc::Vector<checker::body::VerifiedBodyFactRequirementInventory> stagedBodyRequirements;
  zc::Own<borrow_evidence::BorrowEvidenceRepository> stagedBorrowEvidenceRepository;
  zc::Vector<hir::VerifiedHirModule> stagedHirModules;
  zc::Vector<mir::VerifiedBuiltMir> stagedBuiltMirModules;
  zc::Vector<ownership::VerifiedOwnershipEventOverlay> stagedOwnershipEventOverlays;
  zc::Vector<ownership::ValidatedOwnershipProofs> stagedValidatedOwnershipProofs;
  zc::Vector<ownership::OwnershipAdmittedBoundModule> stagedOwnershipAdmittedModules;
  zc::Vector<ownership::OwnershipCheckedMir> stagedOwnershipCheckedMir;
  zc::Vector<ownership::VerifiedExecutableMir> stagedVerifiedExecutableMir;
  zc::Vector<size_t> checkerFactModuleIndices;
  zc::Vector<size_t> checkerFactIndexByModule;
  zc::Vector<size_t> ordinaryBoundModuleIndices;

  const auto& materializedGraph = retainedCheckerAuthority.graphLease().capability();
  const auto& materializedGraphWitness = materializedGraph.witness();
  if (materializedGraphWitness.scc().hasCycle(materializedGraphWitness.graph())) { return false; }
  checkerFactModuleIndices.reserve(checkerModules.size());
  checkerFactIndexByModule.reserve(checkerModules.size());
  for (size_t index = 0; index < checkerModules.size(); ++index) {
    checkerFactIndexByModule.add(checkerModules.size());
  }
  for (const auto& component : materializedGraphWitness.scc().components()) {
    if (component.modules().size() != 1 || component.cyclic()) { return false; }
    zc::Maybe<size_t> moduleIndex;
    for (size_t index = 0; index < materializedGraph.modules().size(); ++index) {
      if (materializedGraph.modules()[index].key().encode().asPtr() !=
          component.modules()[0].encode().asPtr()) {
        continue;
      }
      if (moduleIndex != zc::none) { return false; }
      moduleIndex = index;
    }
    if (moduleIndex == zc::none || ZC_ASSERT_NONNULL(moduleIndex) >= checkerModules.size() ||
        checkerFactIndexByModule[ZC_ASSERT_NONNULL(moduleIndex)] != checkerModules.size()) {
      return false;
    }
    checkerFactIndexByModule[ZC_ASSERT_NONNULL(moduleIndex)] = checkerFactModuleIndices.size();
    checkerFactModuleIndices.add(ZC_ASSERT_NONNULL(moduleIndex));
  }
  if (checkerFactModuleIndices.size() != checkerModules.size()) { return false; }

  ordinaryBoundModuleIndices.reserve(checkerModules.size());
  for (const auto index : checkerFactModuleIndices) {
    const auto& boundModule = checkerModules[index];
    auto crate = retainedCheckerAuthority.crate(boundModule.crate());
    if (crate == zc::none) {
      return rejectOne(boundModule.module(),
                       checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                       checker::signature::CheckerInvariantStage::Signature, 0);
    }
    if (ZC_ASSERT_NONNULL(crate).key().unit().kind() ==
        identity::CompilationUnitKind::UserPackage) {
      ordinaryBoundModuleIndices.add(index);
    }
  }
  if (ordinaryBoundModuleIndices.empty()) {
    impl->verifiedCheckedSources = true;
    return true;
  }
  const auto ordinaryDiagnosticModule = checkerModules[ordinaryBoundModuleIndices[0]].module();
  zc::Vector<core::VerifiedCoreLibrary> coreLibraries;
  for (const auto& crate : graphDemand.lease().capability().crates()) {
    if (crate.key().unit().kind() != identity::CompilationUnitKind::Toolchain ||
        crate.key().unit().toolchain().component() != identity::ToolchainComponent::Core) {
      continue;
    }
    auto library = materializeCoreLibrary(crate.key());
    if (library == zc::none) { return false; }
    ZC_IF_SOME(value, library) {
      if (value.context() != impl->contextBrand ||
          value.fingerprint().digest() != retainedCheckerAuthority.fingerprint().digest() ||
          value.modules().size() == 0) {
        return false;
      }
      coreLibraries.add(zc::mv(value));
    }
  }
  if (coreLibraries.size() != 1) { return false; }

  if (retainedCheckerAuthority.semanticContext() == impl->contextBrand) {
    const auto& checkerAuthority = retainedCheckerAuthority;
    const auto& fingerprint = checkerAuthority.fingerprint();
    zc::Vector<checker::signature::MarkerShapeModuleInput> markerInputs(checkerModules.size());
    for (const auto& boundModule : checkerModules) {
      markerInputs.add(checker::signature::MarkerShapeModuleInput{boundModule});
    }
    auto shapeResult = checker::signature::MarkerShapeInventoryBuilder::build(
        impl->contextBrand, fingerprint, ordinaryDiagnosticModule, markerInputs.asPtr(),
        checkerAuthority);
    if (!shapeResult.is<checker::signature::VerifiedMarkerShapeInventory>()) {
      auto rejected =
          zc::mv(shapeResult).get<checker::signature::SignatureFactsInvariantRejected>();
      return rejectChecker(ordinaryDiagnosticModule, zc::mv(rejected.failures));
    }
    stagedMarkerShapes =
        zc::mv(shapeResult).get<checker::signature::VerifiedMarkerShapeInventory>();

    zc::Vector<identity::ModuleId> authorizedPreludeModules;
    const auto addAuthorizedModule = [&](identity::ModuleId module) {
      for (const auto authorized : authorizedPreludeModules) {
        if (authorized == module) return;
      }
      authorizedPreludeModules.add(module);
    };
    addAuthorizedModule(coreLibraries[0].authorityLease().capability().preludeModule());
    for (const auto& entry : coreLibraries[0].authorityLease().capability().policies().entries()) {
      auto definition = checkerAuthority.definition(entry.definition);
      if (definition == zc::none) { return false; }
      ZC_IF_SOME(value, definition) {
        auto owner = checkerAuthority.module(value.record().module());
        if (owner == zc::none) { return false; }
        ZC_IF_SOME(module, owner) { addAuthorizedModule(module.handle()); }
      }
      for (const auto& rule : entry.policy.referenceRules()) {
        ZC_IF_SOME(requiredMarker, rule.requiredMarker) {
          auto required = checkerAuthority.definition(requiredMarker);
          if (required == zc::none) { return false; }
          ZC_IF_SOME(value, required) {
            auto owner = checkerAuthority.module(value.record().module());
            if (owner == zc::none) { return false; }
            ZC_IF_SOME(module, owner) { addAuthorizedModule(module.handle()); }
          }
        }
      }
    }
    for (const auto& edge : materializedGraph.requestEdges()) {
      if (edge.request().dependencyKind() != identity::ModuleDependencyKind::Prelude) { continue; }
      addAuthorizedModule(edge.dependency());
    }
    auto markerConfiguration =
        core::checkerConfig(coreLibraries[0].authorityLease().capability().policies());
    if (markerConfiguration == zc::none) { return false; }
    ZC_IF_SOME(shapes, stagedMarkerShapes) {
      ZC_IF_SOME(configuration, markerConfiguration) {
        auto policyResult = checker::signature::MarkerPolicyRegistryBuilder::build(
            ordinaryDiagnosticModule, configuration, shapes, authorizedPreludeModules.asPtr(),
            checkerAuthority);
        if (!policyResult.is<checker::signature::VerifiedMarkerPolicyRegistry>()) {
          auto rejected =
              zc::mv(policyResult).get<checker::signature::SignatureFactsInvariantRejected>();
          return rejectChecker(ordinaryDiagnosticModule, zc::mv(rejected.failures));
        }
        stagedMarkerPolicies =
            zc::mv(policyResult).get<checker::signature::VerifiedMarkerPolicyRegistry>();
      }
    }

    for (const auto moduleIndex : checkerFactModuleIndices) {
      const auto& boundView = checkerModules[moduleIndex];
      ZC_IF_SOME(shapes, stagedMarkerShapes) {
        ZC_IF_SOME(policies, stagedMarkerPolicies) {
          auto signatureResult = checker::signature::SignatureFactsBuilder::build(
              checker::signature::SignatureFactsBuildInput{boundView, *impl->semanticTypeStore,
                                                           shapes, policies, checkerAuthority});
          if (signatureResult.is<checker::signature::SignatureFactsSourceRejected>()) {
            auto parsed = parsedFor(boundView.module());
            if (parsed == zc::none) {
              for (const auto& failure :
                   signatureResult.get<checker::signature::SignatureFactsSourceRejected>()
                       .failures) {
                impl->diagnosticEngine->emit(diagnostics::Diagnostic(
                    static_cast<diagnostics::DiagID>(failure.diagnostic), source::SourceLoc()));
              }
              return false;
            }
            ZC_IF_SOME(parsedModule, parsed) {
              if (!emitSignatureSource(
                      parsedModule,
                      signatureResult.get<checker::signature::SignatureFactsSourceRejected>(),
                      retainedCheckerAuthority)) {
                return rejectOne(boundView.module(),
                                 checker::signature::CheckerInvariantKind::InvalidFact,
                                 checker::signature::CheckerInvariantStage::Signature, 0);
              }
            }
            return false;
          }
          if (signatureResult.is<checker::signature::SignatureFactsInvariantRejected>()) {
            auto rejected =
                zc::mv(signatureResult).get<checker::signature::SignatureFactsInvariantRejected>();
            return rejectChecker(boundView.module(), zc::mv(rejected.failures));
          }
          stagedSignatureFacts.add(
              zc::mv(signatureResult).get<checker::signature::VerifiedSignatureFacts>());

          zc::Vector<VerifiedInterfaceSource> interfaceSources(stagedModuleInterfaces.size());
          auto requesterCrate = retainedCheckerAuthority.crate(boundView.crate());
          if (requesterCrate == zc::none) { return false; }
          const bool ordinaryRequester = ZC_ASSERT_NONNULL(requesterCrate).key().unit().kind() ==
                                         identity::CompilationUnitKind::UserPackage;
          for (size_t index = 0; index < stagedModuleInterfaces.size(); ++index) {
            const auto boundIndex = checkerFactModuleIndices[index];
            auto crate = retainedCheckerAuthority.crate(checkerModules[boundIndex].crate());
            if (crate == zc::none ||
                (ordinaryRequester && ZC_ASSERT_NONNULL(crate).key().unit().kind() !=
                                          identity::CompilationUnitKind::UserPackage)) {
              continue;
            }
            interfaceSources.add(VerifiedInterfaceSource(
                UserVerifiedInterfaceSource{stagedModuleInterfaces[index]}));
          }
          if (ordinaryRequester) {
            for (const auto& library : coreLibraries) {
              for (const auto& module : library.modules()) {
                interfaceSources.add(VerifiedInterfaceSource(
                    ToolchainCoreVerifiedInterfaceSource{module.interfaceLease().capability()}));
              }
            }
          }
          auto imported = ImportedSignatureViewProjector::build(
              boundView, interfaceSources.asPtr(), *impl->semanticTypeStore, checkerAuthority);
          if (imported == zc::none) {
            return rejectOne(boundView.module(),
                             checker::signature::CheckerInvariantKind::ViewMismatch,
                             checker::signature::CheckerInvariantStage::Signature, 0);
          }
          ZC_IF_SOME(view, imported) { stagedImportedSignatureViews.add(zc::mv(view)); }
          const auto& signatures = stagedSignatureFacts.back();
          const auto& importedView = stagedImportedSignatureViews.back();
          auto borrowResult = checker::borrow::BorrowInterfaceBuilder::build(
              checker::borrow::BorrowInterfaceBuildInput{
                  impl->contextBrand, fingerprint, boundView.module(), signatures.revision(),
                  importedView.revision(), signatures.signatures(),
                  zc::ArrayPtr<const checker::signature::SemanticSignature>(),
                  retainedCheckerAuthority, *impl->semanticTypeStore});
          if (borrowResult.is<checker::borrow::BorrowInterfaceSourceRejected>()) {
            auto parsed = parsedFor(boundView.module());
            if (parsed == zc::none) {
              return rejectOne(boundView.module(),
                               checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                               checker::signature::CheckerInvariantStage::Signature, 0);
            }
            ZC_IF_SOME(parsedModule, parsed) {
              checker::borrow::emitBorrowSignatureFailures(
                  *impl->diagnosticEngine, parsedModule,
                  borrowResult.get<checker::borrow::BorrowInterfaceSourceRejected>()
                      .failures.asPtr());
            }
            return false;
          }
          if (borrowResult.is<checker::borrow::BorrowInterfaceInvariantRejected>()) {
            auto rejected =
                zc::mv(borrowResult).get<checker::borrow::BorrowInterfaceInvariantRejected>();
            return rejectChecker(boundView.module(), zc::mv(rejected.failures));
          }
          auto interfaceResult = ModuleInterfaceVerifier::build(ModuleInterfaceBuildInput{
              boundView, signatures, importedView, policies,
              zc::mv(borrowResult).get<checker::borrow::VerifiedBorrowInterfaceSurface>(),
              *impl->semanticTypeStore, checkerAuthority});
          if (!interfaceResult.is<VerifiedModuleInterface>()) {
            auto rejected = zc::mv(interfaceResult).get<ModuleInterfaceInvariantRejected>();
            auto parsed = parsedFor(boundView.module());
            if (parsed == zc::none) {
              return rejectOne(boundView.module(),
                               checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                               checker::signature::CheckerInvariantStage::Signature, 0);
            }
            ZC_IF_SOME(parsedModule, parsed) {
              emitModuleInterfaceInvariantFacts(*impl->diagnosticEngine, parsedModule,
                                                rejected.failures.asPtr());
            }
            return false;
          }
          stagedModuleInterfaces.add(zc::mv(interfaceResult).get<VerifiedModuleInterface>());
        }
      }
    }

    if (stagedSignatureFacts.size() != checkerModules.size() ||
        stagedImportedSignatureViews.size() != checkerModules.size() ||
        stagedModuleInterfaces.size() != checkerModules.size()) {
      return rejectOne(ordinaryDiagnosticModule,
                       checker::signature::CheckerInvariantKind::MissingRequiredFact,
                       checker::signature::CheckerInvariantStage::Signature, 0);
    }

    const auto buildCoherence = [&]() -> checker::coherence::CoherenceBuildResult {
      ZC_IF_SOME(policies, stagedMarkerPolicies) {
        return CoherenceBuilder::build(CoherenceBuildInput{impl->contextBrand, fingerprint,
                                                           policies, stagedModuleInterfaces.asPtr(),
                                                           checkerAuthority});
      }
      ZC_UNREACHABLE
    };
    auto coherenceResult = buildCoherence();
    if (coherenceResult.is<checker::coherence::CoherenceSourceRejected>()) {
      const auto& rejected = coherenceResult.get<checker::coherence::CoherenceSourceRejected>();
      for (const auto& failure : rejected.failures) {
        auto implementation = checkerAuthority.implementation(failure.primaryImpl);
        if (implementation == zc::none) {
          return rejectOne(ordinaryDiagnosticModule,
                           checker::signature::CheckerInvariantKind::InvalidFact,
                           checker::signature::CheckerInvariantStage::Coherence, 0);
        }
        ZC_IF_SOME(implementationEntry, implementation) {
          auto module = checkerAuthority.module(implementationEntry.record().module());
          if (module == zc::none) {
            return rejectOne(ordinaryDiagnosticModule,
                             checker::signature::CheckerInvariantKind::InvalidFact,
                             checker::signature::CheckerInvariantStage::Coherence, 0);
          }
          ZC_IF_SOME(moduleEntry, module) {
            ZC_IF_SOME(parsed, parsedFor(moduleEntry.handle())) {
              checker::emitCoherenceSourceFailure(*impl->diagnosticEngine, parsed,
                                                  retainedCheckerAuthority,
                                                  *impl->semanticTypeStore, failure);
            }
          }
        }
      }
      return false;
    }
    if (coherenceResult.is<checker::coherence::CoherenceInvariantRejected>()) {
      auto rejected = zc::mv(coherenceResult).get<checker::coherence::CoherenceInvariantRejected>();
      return rejectChecker(ordinaryDiagnosticModule, zc::mv(rejected.failures));
    }
    auto frozenCoherence = zc::mv(coherenceResult).get<checker::coherence::CoherenceFrozen>();
    for (const auto& advisory : frozenCoherence.advisories) {
      ZC_IF_SOME(parsed, parsedFor(ordinaryDiagnosticModule)) {
        impl->diagnosticEngine->emit(diagnostics::Diagnostic(
            advisory.diagnostic, locationFor(parsed, advisory.primarySpan)));
      }
    }
    stagedCoherenceView = zc::mv(frozenCoherence.view);

    ZC_IF_SOME(coherence, stagedCoherenceView) {
      ZC_IF_SOME(factStoreBrands, impl->factStoreBrands) {
        checker::body::BodyChecker bodyChecker;
        const auto& checkerAuthority = retainedCheckerAuthority;
        for (const auto moduleIndex : checkerFactModuleIndices) {
          const auto& boundView = checkerModules[moduleIndex];
          const auto factIndex = checkerFactIndexByModule[moduleIndex];
          auto inventoryResult =
              checker::body::BodyFactRequirementInventoryBuilder::build(boundView.boundModule());
          if (!inventoryResult.is<checker::body::VerifiedBodyFactRequirementInventory>()) {
            auto rejected =
                zc::mv(inventoryResult).get<checker::checked::CheckedFactsInvariantRejected>();
            return rejectChecker(boundView.module(), zc::mv(rejected.failures));
          }
          auto inventory =
              zc::mv(inventoryResult).get<checker::body::VerifiedBodyFactRequirementInventory>();
          auto dispatchInventoryResult = checker::dispatch::DispatchSiteInventoryBuilder::build(
              boundView.boundModule(), inventory);
          if (!dispatchInventoryResult.is<checker::dispatch::VerifiedDispatchSiteInventory>()) {
            auto rejected = zc::mv(dispatchInventoryResult)
                                .get<checker::dispatch::DispatchFactsInvariantRejected>();
            return rejectDispatch(boundView.module(), zc::mv(rejected));
          }
          auto dispatchInventory = zc::mv(dispatchInventoryResult)
                                       .get<checker::dispatch::VerifiedDispatchSiteInventory>();
          auto crate = checkerAuthority.crate(boundView.crate());
          if (crate == zc::none) {
            return rejectOne(boundView.module(),
                             checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                             checker::signature::CheckerInvariantStage::Body, 0);
          }
          ZC_IF_SOME(crateEntry, crate) {
            auto bodyResult = bodyChecker.check(
                checker::body::BodyCheckingInput{
                    boundView.boundModule().retain(), checkerAuthority,
                    ZC_ASSERT_NONNULL(stagedMarkerPolicies),
                    coreLibraries[0].authorityLease().capability().authority(),
                    stagedSignatureFacts[factIndex], stagedImportedSignatureViews[factIndex],
                    coherence, *impl->semanticTypeStore, inventory,
                    crateEntry.key().semanticOptions()},
                factStoreBrands);
            if (bodyResult.is<checker::checked::CheckedFactsSourceRejected>()) {
              auto rejected =
                  zc::mv(bodyResult).get<checker::checked::CheckedFactsSourceRejected>();
              zc::Vector<identity::DefId> importedDefinitions;
              for (const auto& importedModule : stagedImportedSignatureViews[factIndex].modules()) {
                for (const auto& definition : importedModule.lookupDefinitions()) {
                  bool duplicate = false;
                  for (const auto existing : importedDefinitions) {
                    if (existing == definition.definition) {
                      duplicate = true;
                      break;
                    }
                  }
                  if (!duplicate) { importedDefinitions.add(definition.definition); }
                }
              }
              zc::Vector<identity::ImplId> coherentImpls(coherence.implHeads().size());
              for (const auto& implementation : coherence.implHeads()) {
                coherentImpls.add(implementation.impl);
              }
              checker::checked::CheckedFactsVerificationInput rejectionInput{
                  impl->contextBrand,
                  fingerprint,
                  boundView.module(),
                  boundView.parsedModule().source(),
                  boundView.parsedModule().contentDigest(),
                  boundView.parsedModule().receipt(),
                  stagedSignatureFacts[factIndex].revision(),
                  stagedImportedSignatureViews[factIndex].revision(),
                  coherence.revision(),
                  crateEntry.key().semanticOptions(),
                  inventory.nodeRequirements(),
                  inventory.definitionRequirements(),
                  inventory.captureRequirements(),
                  importedDefinitions.asPtr(),
                  coherentImpls.asPtr(),
                  rejected.failures.asPtr(),
                  boundView.definitions().ownerLocalBindings(),
                  boundView.definitions().anonymousEntities(),
                  checkerAuthority,
                  *impl->semanticTypeStore};
              auto verifiedRejection =
                  checker::checked::CheckedFactsSourceRejectionVerifier::verify(zc::mv(rejected),
                                                                                rejectionInput);
              if (verifiedRejection.is<checker::checked::CheckedFactsInvariantRejected>()) {
                auto invariant = zc::mv(verifiedRejection)
                                     .get<checker::checked::CheckedFactsInvariantRejected>();
                return rejectChecker(boundView.module(), zc::mv(invariant.failures));
              }
              const auto& verified =
                  verifiedRejection.get<checker::checked::CheckedFactsSourceRejected>();
              auto parsed = parsedFor(boundView.module());
              if (parsed == zc::none) {
                return rejectOne(boundView.module(),
                                 checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                                 checker::signature::CheckerInvariantStage::Verification, 0);
              }
              ZC_IF_SOME(parsedModule, parsed) {
                checker::emitCheckedFactsSourceFailures(
                    *impl->diagnosticEngine, parsedModule, retainedCheckerAuthority,
                    *impl->semanticTypeStore, verified.failures.asPtr());
              }
              return false;
            }
            if (bodyResult.is<checker::checked::CheckedFactsInvariantRejected>()) {
              auto rejected =
                  zc::mv(bodyResult).get<checker::checked::CheckedFactsInvariantRejected>();
              return rejectChecker(boundView.module(), zc::mv(rejected.failures));
            }
            auto candidate = zc::mv(bodyResult).get<checker::checked::CheckedFactsCandidate>();
            zc::Vector<identity::DefId> importedDefinitions;
            for (const auto& module : stagedImportedSignatureViews[factIndex].modules()) {
              for (const auto& definition : module.lookupDefinitions()) {
                bool duplicate = false;
                for (const auto existing : importedDefinitions) {
                  if (existing == definition.definition) {
                    duplicate = true;
                    break;
                  }
                }
                if (!duplicate) { importedDefinitions.add(definition.definition); }
              }
            }
            zc::Vector<identity::ImplId> coherentImpls(coherence.implHeads().size());
            for (const auto& implementation : coherence.implHeads()) {
              coherentImpls.add(implementation.impl);
            }
            checker::checked::CheckedFactsVerificationInput verificationInput{
                impl->contextBrand,
                fingerprint,
                boundView.module(),
                boundView.parsedModule().source(),
                boundView.parsedModule().contentDigest(),
                boundView.parsedModule().receipt(),
                stagedSignatureFacts[factIndex].revision(),
                stagedImportedSignatureViews[factIndex].revision(),
                coherence.revision(),
                crateEntry.key().semanticOptions(),
                inventory.nodeRequirements(),
                inventory.definitionRequirements(),
                inventory.captureRequirements(),
                importedDefinitions.asPtr(),
                coherentImpls.asPtr(),
                candidate.sourceFailures.asPtr(),
                boundView.definitions().ownerLocalBindings(),
                boundView.definitions().anonymousEntities(),
                checkerAuthority,
                *impl->semanticTypeStore};
            auto verified = checker::checked::CheckedFactsVerifier::verify(zc::mv(candidate),
                                                                           verificationInput);
            if (verified.is<checker::checked::CheckedFactsSourceRejected>()) {
              const auto& rejected = verified.get<checker::checked::CheckedFactsSourceRejected>();
              auto parsed = parsedFor(boundView.module());
              if (parsed == zc::none) {
                return rejectOne(boundView.module(),
                                 checker::signature::CheckerInvariantKind::InputReceiptMismatch,
                                 checker::signature::CheckerInvariantStage::Verification, 0);
              }
              ZC_IF_SOME(parsedModule, parsed) {
                checker::emitCheckedFactsSourceFailures(
                    *impl->diagnosticEngine, parsedModule, retainedCheckerAuthority,
                    *impl->semanticTypeStore, rejected.failures.asPtr());
              }
              return false;
            }
            if (verified.is<checker::checked::CheckedFactsInvariantRejected>()) {
              auto rejected =
                  zc::mv(verified).get<checker::checked::CheckedFactsInvariantRejected>();
              return rejectChecker(boundView.module(), zc::mv(rejected.failures));
            }
            auto adoption = stagedCheckedFactsRepository->adopt(
                zc::mv(verified).get<checker::checked::VerifiedCheckedFacts>());
            if (!adoption.is<checker::checked::CheckedEvidenceLease>()) {
              return rejectOne(boundView.module(),
                               checker::signature::CheckerInvariantKind::InvalidFact,
                               checker::signature::CheckerInvariantStage::Verification, 0);
            }
            auto lease = zc::mv(adoption).get<checker::checked::CheckedEvidenceLease>();
            auto adoptedFacts = stagedCheckedFactsRepository->lookup(lease);
            if (adoptedFacts == zc::none) {
              return rejectOne(boundView.module(),
                               checker::signature::CheckerInvariantKind::InvalidFact,
                               checker::signature::CheckerInvariantStage::Verification, 0);
            }
            ZC_IF_SOME(facts, adoptedFacts) {
              auto dispatchBuild = checker::dispatch::DispatchFactsBuilder::build(
                  dispatchInventory, fingerprint, lease, facts, checkerAuthority,
                  *impl->semanticTypeStore);
              if (!dispatchBuild.is<checker::dispatch::DispatchFactsCandidate>()) {
                auto rejected =
                    zc::mv(dispatchBuild).get<checker::dispatch::DispatchFactsInvariantRejected>();
                return rejectDispatch(boundView.module(), zc::mv(rejected));
              }
              auto dispatchVerification = checker::dispatch::DispatchFactsVerifier::verify(
                  zc::mv(dispatchBuild).get<checker::dispatch::DispatchFactsCandidate>(),
                  checker::dispatch::DispatchFactsVerificationInput{
                      fingerprint, boundView.module(), boundView.parsedModule().source(),
                      dispatchInventory.requirements(), dispatchInventory.nodeProjections(), lease,
                      facts, checkerAuthority, *impl->semanticTypeStore});
              if (!dispatchVerification.is<checker::dispatch::VerifiedDispatchFacts>()) {
                auto rejected = zc::mv(dispatchVerification)
                                    .get<checker::dispatch::DispatchFactsInvariantRejected>();
                return rejectDispatch(boundView.module(), zc::mv(rejected));
              }
              stagedDispatchFacts.add(
                  zc::mv(dispatchVerification).get<checker::dispatch::VerifiedDispatchFacts>());
            }
            stagedCheckedEvidence.add(zc::mv(lease));
            stagedBodyRequirements.add(zc::mv(inventory));
          }
        }
      }
    }
  }
  if (stagedCheckedEvidence.size() != checkerModules.size() ||
      stagedDispatchFacts.size() != checkerModules.size() ||
      stagedBodyRequirements.size() != checkerModules.size() ||
      impl->diagnosticEngine->hasErrors()) {
    return false;
  }
  if (checkerModules.size() > UINT32_MAX) {
    return rejectOne(ordinaryDiagnosticModule,
                     checker::signature::CheckerInvariantKind::InvalidFact,
                     checker::signature::CheckerInvariantStage::Verification, 0);
  }
  ZC_IF_SOME(issuer, impl->factStoreBrands) {
    auto repositoryBrand = issuer.issue();
    if (repositoryBrand == zc::none) {
      return rejectOne(ordinaryDiagnosticModule,
                       checker::signature::CheckerInvariantKind::InferenceLifecycle,
                       checker::signature::CheckerInvariantStage::Verification, 0);
    }
    ZC_IF_SOME(brand, repositoryBrand) {
      auto repository = borrow_evidence::BorrowEvidenceRepository::create(
          impl->contextBrand, brand, static_cast<uint32_t>(checkerModules.size()));
      if (repository == zc::none) {
        return rejectOne(ordinaryDiagnosticModule,
                         checker::signature::CheckerInvariantKind::InferenceLifecycle,
                         checker::signature::CheckerInvariantStage::Verification, 0);
      }
      ZC_IF_SOME(value, repository) {
        stagedBorrowEvidenceRepository =
            zc::heap<borrow_evidence::BorrowEvidenceRepository>(zc::mv(value));
      }
    }
  }
  if (stagedBorrowEvidenceRepository.get() == nullptr) {
    return rejectOne(ordinaryDiagnosticModule,
                     checker::signature::CheckerInvariantKind::InferenceLifecycle,
                     checker::signature::CheckerInvariantStage::Verification, 0);
  }
  zc::Vector<checker::signature::VerifiedSignatureFacts> ordinarySignatureFacts(
      ordinaryBoundModuleIndices.size());
  zc::Vector<checker::cross_module::ImportedSignatureView> ordinaryImportedSignatureViews(
      ordinaryBoundModuleIndices.size());
  zc::Vector<checker::body::VerifiedBodyFactRequirementInventory> ordinaryBodyRequirements(
      ordinaryBoundModuleIndices.size());
  zc::Vector<VerifiedModuleInterface> ordinaryModuleInterfaces(ordinaryBoundModuleIndices.size());
  zc::Vector<checker::checked::CheckedEvidenceLease> ordinaryCheckedEvidence(
      ordinaryBoundModuleIndices.size());
  zc::Vector<checker::dispatch::VerifiedDispatchFacts> ordinaryDispatchFacts(
      ordinaryBoundModuleIndices.size());
  for (const auto index : ordinaryBoundModuleIndices) {
    const auto factIndex = checkerFactIndexByModule[index];
    ordinarySignatureFacts.add(zc::mv(stagedSignatureFacts[factIndex]));
    ordinaryImportedSignatureViews.add(zc::mv(stagedImportedSignatureViews[factIndex]));
    ordinaryBodyRequirements.add(zc::mv(stagedBodyRequirements[factIndex]));
    ordinaryModuleInterfaces.add(zc::mv(stagedModuleInterfaces[factIndex]));
    ordinaryCheckedEvidence.add(zc::mv(stagedCheckedEvidence[factIndex]));
    ordinaryDispatchFacts.add(zc::mv(stagedDispatchFacts[factIndex]));
  }
  zc::Vector<VerifiedInterfaceSource> checkedModuleInterfaceSources(
      ordinaryModuleInterfaces.size() + coreLibraries.size());
  for (const auto& interface : ordinaryModuleInterfaces) {
    checkedModuleInterfaceSources.add(
        VerifiedInterfaceSource(UserVerifiedInterfaceSource{interface}));
  }
  for (const auto& library : coreLibraries) {
    for (const auto& module : library.modules()) {
      checkedModuleInterfaceSources.add(VerifiedInterfaceSource(
          ToolchainCoreVerifiedInterfaceSource{module.interfaceLease().capability()}));
    }
  }
  for (size_t ordinaryIndex = 0; ordinaryIndex < ordinaryBoundModuleIndices.size();
       ++ordinaryIndex) {
    const auto boundIndex = ordinaryBoundModuleIndices[ordinaryIndex];
    const auto& checkerBound = checkerModules[boundIndex];
    auto checkedModule = hir::CheckedModuleBuilder::build(hir::CheckedModuleBuildInput{
        checkerBound, ordinarySignatureFacts[ordinaryIndex],
        ordinaryModuleInterfaces[ordinaryIndex], ordinaryImportedSignatureViews[ordinaryIndex],
        checkedModuleInterfaceSources.asPtr(), ordinaryCheckedEvidence[ordinaryIndex],
        *stagedCheckedFactsRepository, ordinaryDispatchFacts[ordinaryIndex],
        *stagedBorrowEvidenceRepository, retainedCheckerAuthority, *impl->semanticTypeStore});
    if (checkedModule.isCapabilityRejected()) {
      return rejectIrCapability(checkedModule.capabilityFailures());
    }
    if (checkedModule.isIdentityInvariantRejected()) {
      return rejectIrIdentity(checkedModule.identityFailures());
    }
    if (checkedModule.isIrInvariantRejected()) {
      return rejectIrInvariant(checkedModule.invariantFailures());
    }

    auto hirCandidate = hir::HirBuilder::build(zc::mv(checkedModule).takeVerified());
    if (hirCandidate.isCapabilityRejected()) {
      return rejectIrCapability(hirCandidate.capabilityFailures());
    }
    if (hirCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(hirCandidate.identityFailures());
    }
    if (hirCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(hirCandidate.invariantFailures());
    }

    auto verifiedHir = hir::HirVerifier::verify(zc::mv(hirCandidate).takeVerified());
    if (verifiedHir.isCapabilityRejected()) {
      return rejectIrCapability(verifiedHir.capabilityFailures());
    }
    if (verifiedHir.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedHir.identityFailures());
    }
    if (verifiedHir.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedHir.invariantFailures());
    }
    stagedHirModules.add(zc::mv(verifiedHir).takeVerified());

    auto crate = retainedCheckerAuthority.crate(checkerBound.crate());
    if (crate == zc::none) {
      return rejectOne(checkerBound.module(), checker::signature::CheckerInvariantKind::InvalidFact,
                       checker::signature::CheckerInvariantStage::Verification, 0);
    }
    checker::body::BodyCheckingInput bodyInput{
        checkerBound.boundModule().retain(),
        retainedCheckerAuthority,
        ZC_ASSERT_NONNULL(stagedMarkerPolicies),
        coreLibraries[0].authorityLease().capability().authority(),
        ordinarySignatureFacts[ordinaryIndex],
        ordinaryImportedSignatureViews[ordinaryIndex],
        ZC_ASSERT_NONNULL(stagedCoherenceView),
        *impl->semanticTypeStore,
        ordinaryBodyRequirements[ordinaryIndex],
        ZC_ASSERT_NONNULL(crate).key().semanticOptions()};
    const mir::BuiltMirInput mirInput{stagedHirModules[ordinaryIndex], bodyInput};
    auto mirCandidate = mir::BuiltMirBuilder::build(mirInput);
    if (mirCandidate.isCapabilityRejected()) {
      return rejectIrCapability(mirCandidate.capabilityFailures());
    }
    if (mirCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(mirCandidate.identityFailures());
    }
    if (mirCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(mirCandidate.invariantFailures());
    }

    auto verifiedMir = mir::BuiltMirVerifier::verify(zc::mv(mirCandidate).takeVerified(), mirInput);
    if (verifiedMir.isCapabilityRejected()) {
      return rejectIrCapability(verifiedMir.capabilityFailures());
    }
    if (verifiedMir.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedMir.identityFailures());
    }
    if (verifiedMir.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedMir.invariantFailures());
    }
    stagedBuiltMirModules.add(zc::mv(verifiedMir).takeVerified());

    const auto& hirModule = stagedHirModules[stagedHirModules.size() - 1];
    const auto& builtMir = stagedBuiltMirModules[stagedBuiltMirModules.size() - 1];
    ownership::OwnershipEventOverlayInput ownershipInput{
        checkerBound, hirModule.admittedCheckedModule(), hirModule, builtMir, zc::mv(bodyInput)};
    auto ownershipCandidate = ownership::OwnershipEventOverlayBuilder::build(ownershipInput);
    if (ownershipCandidate.isCapabilityRejected()) {
      return rejectIrCapability(ownershipCandidate.capabilityFailures());
    }
    if (ownershipCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(ownershipCandidate.identityFailures());
    }
    if (ownershipCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(ownershipCandidate.invariantFailures());
    }
    auto verifiedOwnership = ownership::OwnershipEventOverlayVerifier::verify(
        zc::mv(ownershipCandidate).takeVerified(), ownershipInput);
    if (verifiedOwnership.isCapabilityRejected()) {
      return rejectIrCapability(verifiedOwnership.capabilityFailures());
    }
    if (verifiedOwnership.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedOwnership.identityFailures());
    }
    if (verifiedOwnership.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedOwnership.invariantFailures());
    }
    stagedOwnershipEventOverlays.add(zc::mv(verifiedOwnership).takeVerified());

    auto movePathCandidate = ownership::facts::MovePathBuilder::build(
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (movePathCandidate.isCapabilityRejected()) {
      return rejectIrCapability(movePathCandidate.capabilityFailures());
    }
    if (movePathCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(movePathCandidate.identityFailures());
    }
    if (movePathCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(movePathCandidate.invariantFailures());
    }
    auto verifiedMovePaths = ownership::facts::MovePathVerifier::verify(
        zc::mv(movePathCandidate).takeVerified(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedMovePaths.isCapabilityRejected()) {
      return rejectIrCapability(verifiedMovePaths.capabilityFailures());
    }
    if (verifiedMovePaths.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedMovePaths.identityFailures());
    }
    if (verifiedMovePaths.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedMovePaths.invariantFailures());
    }
    auto flowCandidate = ownership::facts::FlowBuilder::build(
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (flowCandidate.isCapabilityRejected()) {
      return rejectIrCapability(flowCandidate.capabilityFailures());
    }
    if (flowCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(flowCandidate.identityFailures());
    }
    if (flowCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(flowCandidate.invariantFailures());
    }
    auto verifiedFlow = ownership::facts::FlowVerifier::verify(
        zc::mv(flowCandidate).takeVerified(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedFlow.isCapabilityRejected()) {
      return rejectIrCapability(verifiedFlow.capabilityFailures());
    }
    if (verifiedFlow.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedFlow.identityFailures());
    }
    if (verifiedFlow.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedFlow.invariantFailures());
    }
    auto initializationCandidate = ownership::facts::InitializationBuilder::build(
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1],
        verifiedFlow.verifiedValue(), verifiedMovePaths.verifiedValue());
    if (initializationCandidate.isCapabilityRejected()) {
      return rejectIrCapability(initializationCandidate.capabilityFailures());
    }
    if (initializationCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(initializationCandidate.identityFailures());
    }
    if (initializationCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(initializationCandidate.invariantFailures());
    }
    auto verifiedInitialization = ownership::facts::InitializationVerifier::verify(
        zc::mv(initializationCandidate).takeVerified(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1],
        verifiedFlow.verifiedValue(), verifiedMovePaths.verifiedValue());
    if (verifiedInitialization.isCapabilityRejected()) {
      return rejectIrCapability(verifiedInitialization.capabilityFailures());
    }
    if (verifiedInitialization.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedInitialization.identityFailures());
    }
    if (verifiedInitialization.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedInitialization.invariantFailures());
    }
    auto initializationSource = ownership::facts::InitializationSourceVerifier::verify(
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1],
        verifiedInitialization.verifiedValue());
    if (initializationSource.isSourceRejected()) {
      auto parsed = parsedFor(checkerBound.module());
      if (parsed == zc::none) {
        return rejectOne(checkerBound.module(),
                         checker::signature::CheckerInvariantKind::InvalidFact,
                         checker::signature::CheckerInvariantStage::Verification, 0);
      }
      ZC_IF_SOME(parsedModule, parsed) {
        auto failures = zc::mv(initializationSource).takeSourceFailures();
        ownership::emitOwnershipSourceFailures(*impl->diagnosticEngine, parsedModule,
                                               failures.facts());
      }
      return false;
    }
    if (initializationSource.isIdentityInvariantRejected()) {
      return rejectIrIdentity(zc::mv(initializationSource).takeIdentityFailures());
    }
    if (initializationSource.isIrInvariantRejected()) {
      return rejectIrInvariant(zc::mv(initializationSource).takeInvariantFailures());
    }
    auto loanCandidate = ownership::facts::LoanBuilder::build(
        verifiedMovePaths.verifiedValue(), stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (loanCandidate.isCapabilityRejected()) {
      return rejectIrCapability(loanCandidate.capabilityFailures());
    }
    if (loanCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(loanCandidate.identityFailures());
    }
    if (loanCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(loanCandidate.invariantFailures());
    }
    auto verifiedLoans = ownership::facts::LoanVerifier::verify(
        zc::mv(loanCandidate).takeVerified(), verifiedMovePaths.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedLoans.isCapabilityRejected()) {
      return rejectIrCapability(verifiedLoans.capabilityFailures());
    }
    if (verifiedLoans.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedLoans.identityFailures());
    }
    if (verifiedLoans.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedLoans.invariantFailures());
    }
    auto referenceCandidate = ownership::facts::ReferenceDefinitionBuilder::build(
        verifiedMovePaths.verifiedValue(), verifiedLoans.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (referenceCandidate.isCapabilityRejected()) {
      return rejectIrCapability(referenceCandidate.capabilityFailures());
    }
    if (referenceCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(referenceCandidate.identityFailures());
    }
    if (referenceCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(referenceCandidate.invariantFailures());
    }
    auto verifiedReferences = ownership::facts::ReferenceDefinitionVerifier::verify(
        zc::mv(referenceCandidate).takeVerified(), verifiedMovePaths.verifiedValue(),
        verifiedLoans.verifiedValue(), stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedReferences.isCapabilityRejected()) {
      return rejectIrCapability(verifiedReferences.capabilityFailures());
    }
    if (verifiedReferences.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedReferences.identityFailures());
    }
    if (verifiedReferences.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedReferences.invariantFailures());
    }
    // Reject borrow-source violations (conflicting loans, move-out-of-borrow,
    // and returned references whose origin is a function-local binding) before
    // deriving the reborrow regions. The verifier independently reconstructs
    // each loan's liveness from the verified loan and reference inventories; a
    // rejected source emits closed ownership diagnostics and fails the check.
    auto borrowSource = ownership::facts::BorrowSourceVerifier::verify(
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1],
        verifiedMovePaths.verifiedValue(), verifiedLoans.verifiedValue(),
        verifiedReferences.verifiedValue());
    if (borrowSource.isSourceRejected()) {
      // Retain the staged Built MIR, overlay, and borrow-evidence repository so
      // ownership fact-derivation tests can reconstruct fact families from
      // verified MIR (the borrow checker rejects the only sources that produce
      // a function-local loan, so no committed module ever carries one). This
      // is test-only: no production accessor reads these fields, checkSources
      // still returns false, and the closed ownership diagnostics below are
      // emitted unchanged.
      impl->stagedBorrowSourceRejectedBuiltMir = zc::mv(stagedBuiltMirModules);
      impl->stagedBorrowSourceRejectedOverlays = zc::mv(stagedOwnershipEventOverlays);
      impl->stagedBorrowSourceRejectedBorrowEvidence = zc::mv(stagedBorrowEvidenceRepository);
      auto parsed = parsedFor(checkerBound.module());
      if (parsed == zc::none) {
        return rejectOne(checkerBound.module(),
                         checker::signature::CheckerInvariantKind::InvalidFact,
                         checker::signature::CheckerInvariantStage::Verification, 0);
      }
      ZC_IF_SOME(parsedModule, parsed) {
        auto failures = zc::mv(borrowSource).takeSourceFailures();
        ownership::emitOwnershipSourceFailures(*impl->diagnosticEngine, parsedModule,
                                               failures.facts());
      }
      return false;
    }
    if (borrowSource.isIdentityInvariantRejected()) {
      return rejectIrIdentity(zc::mv(borrowSource).takeIdentityFailures());
    }
    if (borrowSource.isIrInvariantRejected()) {
      return rejectIrInvariant(zc::mv(borrowSource).takeInvariantFailures());
    }
    auto regionCandidate = ownership::facts::ReborrowRegionBuilder::build(
        verifiedFlow.verifiedValue(), verifiedLoans.verifiedValue(),
        verifiedReferences.verifiedValue(), stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (regionCandidate.isCapabilityRejected()) {
      return rejectIrCapability(regionCandidate.capabilityFailures());
    }
    if (regionCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(regionCandidate.identityFailures());
    }
    if (regionCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(regionCandidate.invariantFailures());
    }
    auto verifiedRegions = ownership::facts::ReborrowRegionVerifier::verify(
        zc::mv(regionCandidate).takeVerified(), verifiedFlow.verifiedValue(),
        verifiedLoans.verifiedValue(), verifiedReferences.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedRegions.isCapabilityRejected()) {
      return rejectIrCapability(verifiedRegions.capabilityFailures());
    }
    if (verifiedRegions.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedRegions.identityFailures());
    }
    if (verifiedRegions.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedRegions.invariantFailures());
    }
    auto referenceStateCandidate = ownership::facts::ReborrowStateBuilder::build(
        verifiedReferences.verifiedValue(), verifiedRegions.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (referenceStateCandidate.isCapabilityRejected()) {
      return rejectIrCapability(referenceStateCandidate.capabilityFailures());
    }
    if (referenceStateCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(referenceStateCandidate.identityFailures());
    }
    if (referenceStateCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(referenceStateCandidate.invariantFailures());
    }
    auto verifiedReferenceStates = ownership::facts::ReborrowStateVerifier::verify(
        zc::mv(referenceStateCandidate).takeVerified(), verifiedReferences.verifiedValue(),
        verifiedRegions.verifiedValue(), stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedReferenceStates.isCapabilityRejected()) {
      return rejectIrCapability(verifiedReferenceStates.capabilityFailures());
    }
    if (verifiedReferenceStates.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedReferenceStates.identityFailures());
    }
    if (verifiedReferenceStates.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedReferenceStates.invariantFailures());
    }
    auto resourceCandidate = ownership::facts::OwnershipResourceBuilder::build(
        verifiedMovePaths.verifiedValue(), stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (resourceCandidate.isCapabilityRejected()) {
      return rejectIrCapability(resourceCandidate.capabilityFailures());
    }
    if (resourceCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(resourceCandidate.identityFailures());
    }
    if (resourceCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(resourceCandidate.invariantFailures());
    }
    auto verifiedResources = ownership::facts::OwnershipResourceVerifier::verify(
        zc::mv(resourceCandidate).takeVerified(), verifiedMovePaths.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedResources.isCapabilityRejected()) {
      return rejectIrCapability(verifiedResources.capabilityFailures());
    }
    if (verifiedResources.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedResources.identityFailures());
    }
    if (verifiedResources.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedResources.invariantFailures());
    }
    auto captureCandidate = ownership::facts::CaptureBuilder::build(
        verifiedMovePaths.verifiedValue(), stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (captureCandidate.isCapabilityRejected()) {
      return rejectIrCapability(captureCandidate.capabilityFailures());
    }
    if (captureCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(captureCandidate.identityFailures());
    }
    if (captureCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(captureCandidate.invariantFailures());
    }
    auto verifiedCaptures = ownership::facts::CaptureVerifier::verify(
        zc::mv(captureCandidate).takeVerified(), verifiedMovePaths.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedCaptures.isCapabilityRejected()) {
      return rejectIrCapability(verifiedCaptures.capabilityFailures());
    }
    if (verifiedCaptures.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedCaptures.identityFailures());
    }
    if (verifiedCaptures.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedCaptures.invariantFailures());
    }
    auto regionMembershipCandidate = ownership::facts::RegionMembershipBuilder::build(
        verifiedFlow.verifiedValue(), verifiedLoans.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (regionMembershipCandidate.isCapabilityRejected()) {
      return rejectIrCapability(regionMembershipCandidate.capabilityFailures());
    }
    if (regionMembershipCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(regionMembershipCandidate.identityFailures());
    }
    if (regionMembershipCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(regionMembershipCandidate.invariantFailures());
    }
    auto verifiedRegionMemberships = ownership::facts::RegionMembershipVerifier::verify(
        zc::mv(regionMembershipCandidate).takeVerified(), verifiedFlow.verifiedValue(),
        verifiedLoans.verifiedValue(), stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedRegionMemberships.isCapabilityRejected()) {
      return rejectIrCapability(verifiedRegionMemberships.capabilityFailures());
    }
    if (verifiedRegionMemberships.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedRegionMemberships.identityFailures());
    }
    if (verifiedRegionMemberships.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedRegionMemberships.invariantFailures());
    }
    auto escapeCandidate = ownership::facts::EscapeBuilder::build(
        verifiedFlow.verifiedValue(), verifiedLoans.verifiedValue(),
        verifiedReferences.verifiedValue(), verifiedResources.verifiedValue(),
        verifiedCaptures.verifiedValue(), verifiedRegionMemberships.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (escapeCandidate.isCapabilityRejected()) {
      return rejectIrCapability(escapeCandidate.capabilityFailures());
    }
    if (escapeCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(escapeCandidate.identityFailures());
    }
    if (escapeCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(escapeCandidate.invariantFailures());
    }
    auto verifiedEscapes = ownership::facts::EscapeVerifier::verify(
        zc::mv(escapeCandidate).takeVerified(), verifiedFlow.verifiedValue(),
        verifiedLoans.verifiedValue(), verifiedReferences.verifiedValue(),
        verifiedResources.verifiedValue(), verifiedCaptures.verifiedValue(),
        verifiedRegionMemberships.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedEscapes.isCapabilityRejected()) {
      return rejectIrCapability(verifiedEscapes.capabilityFailures());
    }
    if (verifiedEscapes.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedEscapes.identityFailures());
    }
    if (verifiedEscapes.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedEscapes.invariantFailures());
    }
    auto regionOutlivesCandidate = ownership::facts::RegionOutlivesBuilder::build(
        verifiedRegionMemberships.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (regionOutlivesCandidate.isCapabilityRejected()) {
      return rejectIrCapability(regionOutlivesCandidate.capabilityFailures());
    }
    if (regionOutlivesCandidate.isIdentityInvariantRejected()) {
      return rejectIrIdentity(regionOutlivesCandidate.identityFailures());
    }
    if (regionOutlivesCandidate.isIrInvariantRejected()) {
      return rejectIrInvariant(regionOutlivesCandidate.invariantFailures());
    }
    auto verifiedRegionOutlives = ownership::facts::RegionOutlivesVerifier::verify(
        zc::mv(regionOutlivesCandidate).takeVerified(), verifiedRegionMemberships.verifiedValue(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1]);
    if (verifiedRegionOutlives.isCapabilityRejected()) {
      return rejectIrCapability(verifiedRegionOutlives.capabilityFailures());
    }
    if (verifiedRegionOutlives.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedRegionOutlives.identityFailures());
    }
    if (verifiedRegionOutlives.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedRegionOutlives.invariantFailures());
    }
    auto verifiedOwnershipInputs = ownership::facts::OwnershipInputVerifier::verify(
        zc::mv(verifiedMovePaths).takeVerified(), zc::mv(verifiedFlow).takeVerified(),
        zc::mv(verifiedInitialization).takeVerified(), zc::mv(verifiedLoans).takeVerified(),
        zc::mv(verifiedReferences).takeVerified(), zc::mv(verifiedRegions).takeVerified(),
        zc::mv(verifiedReferenceStates).takeVerified(), zc::mv(verifiedResources).takeVerified(),
        zc::mv(verifiedEscapes).takeVerified(), zc::mv(verifiedCaptures).takeVerified(),
        zc::mv(verifiedRegionOutlives).takeVerified(),
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1],
        stagedOwnershipEventOverlays[stagedOwnershipEventOverlays.size() - 1],
        stagedBuiltMirModules[stagedBuiltMirModules.size() - 1].borrowEvidenceLease(),
        stagedBorrowEvidenceRepository->capability(), *impl->semanticTypeStore);
    if (verifiedOwnershipInputs.isCapabilityRejected()) {
      return rejectIrCapability(verifiedOwnershipInputs.capabilityFailures());
    }
    if (verifiedOwnershipInputs.isIdentityInvariantRejected()) {
      return rejectIrIdentity(verifiedOwnershipInputs.identityFailures());
    }
    if (verifiedOwnershipInputs.isIrInvariantRejected()) {
      return rejectIrInvariant(verifiedOwnershipInputs.invariantFailures());
    }
    auto validatedOwnershipProofs = ownership::OwnershipProofValidation::validate(
        zc::mv(verifiedOwnershipInputs).takeVerified(),
        zc::mv(verifiedRegionMemberships).takeVerified());
    if (validatedOwnershipProofs.isCapabilityRejected()) {
      return rejectIrCapability(validatedOwnershipProofs.capabilityFailures());
    }
    if (validatedOwnershipProofs.isIdentityInvariantRejected()) {
      return rejectIrIdentity(validatedOwnershipProofs.identityFailures());
    }
    if (validatedOwnershipProofs.isIrInvariantRejected()) {
      return rejectIrInvariant(validatedOwnershipProofs.invariantFailures());
    }
    stagedValidatedOwnershipProofs.add(zc::mv(validatedOwnershipProofs).takeVerified());
    stagedOwnershipAdmittedModules.add(checkerBound.retain());
  }
  // Finalize ownership, elaborate drops, elaborate coroutines, and verify
  // executable MIR: consume Built MIR, the verified event overlay, and the
  // verified facts into one fail-closed OwnershipCheckedMir wrapper per
  // module, then run the RFC 0007 successor chain (elaborateDrops,
  // elaborateCoroutines, verifyExecutableMir) so every pending drop
  // obligation is discharged by the emitted cleanup and every Positive
  // Linear obligation is consumed exactly once. A rejected stage destroys
  // its consumed local input and publishes no predecessor or partial
  // successor; the session retains its previous transaction until every
  // wrapper is committed atomically below.
  for (size_t index = 0; index < stagedBuiltMirModules.size(); ++index) {
    auto checked = ownership::OwnershipFinalizer::finalizeOwnership(
        zc::mv(stagedBuiltMirModules[index]), zc::mv(stagedOwnershipEventOverlays[index]),
        zc::mv(stagedValidatedOwnershipProofs[index]).takeInputs(),
        stagedBorrowEvidenceRepository->capability(), *impl->semanticTypeStore);
    if (checked.isCapabilityRejected()) { return rejectIrCapability(checked.capabilityFailures()); }
    if (checked.isIdentityInvariantRejected()) {
      return rejectIrIdentity(checked.identityFailures());
    }
    if (checked.isIrInvariantRejected()) { return rejectIrInvariant(checked.invariantFailures()); }
    auto elaborated = ownership::DropElaborator::elaborateDrops(
        zc::mv(checked).takeVerified(), stagedBorrowEvidenceRepository->capability());
    if (elaborated.isCapabilityRejected()) {
      return rejectIrCapability(elaborated.capabilityFailures());
    }
    if (elaborated.isIdentityInvariantRejected()) {
      return rejectIrIdentity(elaborated.identityFailures());
    }
    if (elaborated.isIrInvariantRejected()) {
      return rejectIrInvariant(elaborated.invariantFailures());
    }
    auto coroutineElaborated = ownership::CoroutineElaborator::elaborateCoroutines(
        zc::mv(elaborated).takeVerified(), stagedBorrowEvidenceRepository->capability());
    if (coroutineElaborated.isCapabilityRejected()) {
      return rejectIrCapability(coroutineElaborated.capabilityFailures());
    }
    if (coroutineElaborated.isIdentityInvariantRejected()) {
      return rejectIrIdentity(coroutineElaborated.identityFailures());
    }
    if (coroutineElaborated.isIrInvariantRejected()) {
      return rejectIrInvariant(coroutineElaborated.invariantFailures());
    }
    auto executable = ownership::ExecutableMirVerifier::verifyExecutableMir(
        zc::mv(coroutineElaborated).takeVerified(), stagedBorrowEvidenceRepository->capability());
    if (executable.isCapabilityRejected()) {
      return rejectIrCapability(executable.capabilityFailures());
    }
    if (executable.isIdentityInvariantRejected()) {
      return rejectIrIdentity(executable.identityFailures());
    }
    if (executable.isIrInvariantRejected()) {
      return rejectIrInvariant(executable.invariantFailures());
    }
    auto verified = zc::mv(executable).takeVerified();
    stagedOwnershipCheckedMir.add(zc::mv(verified).takeCheckedMir());
    stagedVerifiedExecutableMir.add(zc::mv(verified));
  }
  if (stagedHirModules.size() != ordinaryBoundModuleIndices.size() ||
      stagedOwnershipCheckedMir.size() != ordinaryBoundModuleIndices.size() ||
      stagedVerifiedExecutableMir.size() != ordinaryBoundModuleIndices.size() ||
      stagedOwnershipAdmittedModules.size() != ordinaryBoundModuleIndices.size() ||
      impl->diagnosticEngine->hasErrors()) {
    return false;
  }
  impl->markerShapes = zc::mv(stagedMarkerShapes);
  impl->markerPolicies = zc::mv(stagedMarkerPolicies);
  impl->coreLibrary = zc::mv(coreLibraries[0]);
  impl->signatureFacts = zc::mv(ordinarySignatureFacts);
  impl->importedSignatureViews = zc::mv(ordinaryImportedSignatureViews);
  impl->bodyRequirements = zc::mv(ordinaryBodyRequirements);
  impl->moduleInterfaces = zc::mv(ordinaryModuleInterfaces);
  impl->checkerIdentityAuthority = zc::mv(checkerAuthority);
  impl->coherenceView = zc::mv(stagedCoherenceView);
  impl->checkedFactsRepository = zc::mv(stagedCheckedFactsRepository);
  impl->checkedEvidence = zc::mv(ordinaryCheckedEvidence);
  impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);
  impl->borrowEvidenceRepository = zc::mv(stagedBorrowEvidenceRepository);
  impl->hirModules = zc::mv(stagedHirModules);
  impl->ownershipCheckedMirModules = zc::mv(stagedOwnershipCheckedMir);
  impl->verifiedExecutableMirModules = zc::mv(stagedVerifiedExecutableMir);
  impl->ownershipAdmittedModules = zc::mv(stagedOwnershipAdmittedModules);
  impl->validatedOwnershipProofs = zc::mv(stagedValidatedOwnershipProofs);
  impl->verifiedCheckedSources = true;
  return true;
}

basic::StringPool& CompilerSession::getStringPool() { return *impl->stringPool; }

const basic::StringPool& CompilerSession::getStringPool() const { return *impl->stringPool; }

const basic::CompilerOptions& CompilerSession::getCompilerOptions() const {
  return impl->compilerOpts;
}

const source::SourceManager& CompilerSession::getSourceManager() const {
  return *impl->sourceManager;
}

identity::SemanticContextBrand CompilerSession::getSemanticContextBrand() const noexcept {
  return impl->contextBrand;
}

zc::Maybe<const type::SemanticTypeStore&> CompilerSession::getSemanticTypeStore() const noexcept {
  if (impl->semanticTypeStore.get() == nullptr) { return zc::none; }
  return *impl->semanticTypeStore;
}

zc::Maybe<const identity::RegistryBrandIssuer&> CompilerSession::getFactStoreBrandIssuer()
    const noexcept {
  ZC_IF_SOME(issuer, impl->factStoreBrands) { return issuer; }
  return zc::none;
}

zc::MemoryResource& CompilerSession::getPackageResolutionMemoryResource() noexcept {
  return impl->packageResolutionMemory;
}

bool CompilerSession::installVerifiedPackageInput(VerifiedPackageSessionInput&& input) {
  if (input.impl.get() == nullptr || impl->packageRequest != zc::none ||
      impl->verifiedHostTarget != zc::none || impl->verifiedTarget != zc::none ||
      impl->packageGraph != zc::none || impl->buildScriptPlan != zc::none ||
      impl->packageSnapshots.size() != 0 ||
      impl->sourceManager->getManagedBufferIds().size() != 0) {
    return false;
  }

  zc::Maybe<VerifiedCrateGraph> crateGraph;
  auto graphResult = VerifiedCrateGraph::buildFinal(input.impl->request, input.impl->graph,
                                                    input.impl->buildScriptPlan);
  if (!graphResult.is<VerifiedCrateGraph>()) { return false; }
  crateGraph = zc::mv(graphResult.get<VerifiedCrateGraph>());

  impl->packageRequest = zc::mv(input.impl->request);
  impl->verifiedHostTarget = zc::mv(input.impl->hostTarget);
  impl->verifiedTarget = zc::mv(input.impl->target);
  impl->packageGraph = zc::mv(input.impl->graph);
  impl->buildScriptPlan = zc::mv(input.impl->buildScriptPlan);
  ZC_IF_SOME(graph, crateGraph) { impl->crateGraph = zc::mv(graph); }
  impl->packageSnapshots = zc::mv(input.impl->snapshots);
  return true;
}

bool CompilerSession::installVerifiedCoreDistribution(
    const source::core::VerifiedCoreDistribution& distribution) {
  if (impl->packageRequest == zc::none || impl->crateGraph == zc::none ||
      impl->coreDistributionInputs != zc::none ||
      impl->sourceManager->getManagedBufferIds().size() != 0) {
    return false;
  }
  zc::Maybe<source_query::CanonicalCompilationOptions> compilationOptions;
  ZC_IF_SOME(request, impl->packageRequest) {
    compilationOptions = source_query::CanonicalCompilationOptions::fromVerified(request);
  }
  if (compilationOptions == zc::none) { return false; }

  zc::Maybe<incremental_binding_query::PackageRootSetKey> packageRoots;
  zc::Maybe<incremental_binding_query::CanonicalPackageGraph> packageGraphInput;
  zc::Vector<identity::CrateKey> userRootCrates;
  zc::Vector<identity::CrateKey> projectedCoreCrates;
  zc::Vector<module_graph_query::CompilationOptionsEntry> optionEntries;
  zc::Vector<module_graph_query::ModuleSearchRootsEntry> searchRootEntries;
  ZC_IF_SOME(request, impl->packageRequest) {
    packageRoots = incremental_binding_query::PackageRootSetKey::fromVerified(request);
  }
  ZC_IF_SOME(resolution, impl->packageGraph) {
    ZC_IF_SOME(graph, impl->crateGraph) {
      packageGraphInput =
          incremental_binding_query::CanonicalPackageGraph::fromVerified(resolution, graph);
      ZC_IF_SOME(request, impl->packageRequest) {
        for (const auto& root : graph.roots()) {
          size_t matchingRequests = 0;
          for (const auto& requested : request.roots()) {
            if (samePackage(root.packageKey(), requested.packageKey()) &&
                root.crateKey().targetKind() == requested.targetKind() &&
                root.crateKey().targetName() == requested.targetName()) {
              ++matchingRequests;
            }
          }
          if (matchingRequests > 1) { return false; }
          if (matchingRequests == 1) { userRootCrates.add(root.crateKey().clone()); }
        }
      }
      for (const auto& consumer : graph.crates()) {
        auto projected = identity::projectToolchainCoreCrate(consumer);
        if (projected == zc::none) { return false; }
        bool retained = false;
        for (const auto& candidate : projectedCoreCrates) {
          if (candidate.encode().asPtr() == ZC_ASSERT_NONNULL(projected).encode().asPtr()) {
            retained = true;
            break;
          }
        }
        if (!retained) { projectedCoreCrates.add(zc::mv(ZC_ASSERT_NONNULL(projected))); }

        optionEntries.add(module_graph_query::CompilationOptionsEntry::from(
            consumer.clone(), ZC_ASSERT_NONNULL(compilationOptions).clone()));
        zc::Vector<binder::ModuleSearchRoot> environment;
        auto root = impl->compilationRoot(consumer);
        if (root == zc::none) { return false; }
        ZC_IF_SOME(rootValue, root) {
          const auto relativeRoot = parentDirectory(rootValue.sourcePath());
          const auto& package = consumer.unit().userPackage();
          switch (package.source().kind()) {
            case identity::PackageSourceKind::LocalPath: {
              zc::Vector<identity::CanonicalPathSegment> segments;
              for (const auto& segment : package.source().localPath().segments()) {
                segments.add(segment.clone());
              }
              for (const auto& segment : relativeRoot.segments()) { segments.add(segment.clone()); }
              environment.add(binder::ModuleSearchRoot::workspace(
                  consumer.clone(),
                  identity::CanonicalWorkspaceRelativePath::from(
                      package.source().localPath().leadingParents(), zc::mv(segments))));
              break;
            }
            case identity::PackageSourceKind::Registry:
            case identity::PackageSourceKind::Vcs:
              environment.add(binder::ModuleSearchRoot::package(consumer.clone(), package.clone(),
                                                                relativeRoot.clone()));
              break;
          }
          ZC_IF_SOME(plan, impl->buildScriptPlan) {
            for (const auto& node : plan.nodes()) {
              if (!samePackage(package, node.key().preparatory().package())) { continue; }
              zc::Vector<identity::CanonicalPathSegment> noSegments;
              environment.add(binder::ModuleSearchRoot::generated(
                  consumer.clone(), node.key().preparatory().producerKey(),
                  identity::CanonicalRelativePath::from(zc::mv(noSegments))));
            }
          }
        }
        auto roots = incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
            consumer, environment.asPtr());
        if (roots == zc::none) { return false; }
        searchRootEntries.add(module_graph_query::ModuleSearchRootsEntry::from(
            consumer.clone(), zc::mv(ZC_ASSERT_NONNULL(roots))));
      }
    }
  }
  for (const auto& core : projectedCoreCrates) {
    optionEntries.add(module_graph_query::CompilationOptionsEntry::from(
        core.clone(), ZC_ASSERT_NONNULL(compilationOptions).clone()));
    auto root =
        binder::ModuleSearchRoot::toolchainCore(core.clone(), distribution.distributionDigest());
    if (root == zc::none) { return false; }
    zc::Vector<binder::ModuleSearchRoot> environment;
    environment.add(zc::mv(ZC_ASSERT_NONNULL(root)));
    auto roots = incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
        core, environment.asPtr());
    if (roots == zc::none) { return false; }
    searchRootEntries.add(module_graph_query::ModuleSearchRootsEntry::from(
        core.clone(), zc::mv(ZC_ASSERT_NONNULL(roots))));
  }
  if (packageRoots == zc::none || packageGraphInput == zc::none || projectedCoreCrates.empty()) {
    return false;
  }

  zc::Maybe<module_graph_query::CompleteCompilationContextAuthority> contextAuthority;
  auto acceptedDistribution = source::core::initialCoreDistributionInput();
  if (acceptedDistribution == zc::none) { return false; }
  ZC_IF_SOME(request, impl->packageRequest) {
    const module_graph_query::CompleteCompilationContextSources sources{
        request,
        ZC_ASSERT_NONNULL(packageRoots),
        ZC_ASSERT_NONNULL(packageGraphInput),
        userRootCrates.asPtr(),
        projectedCoreCrates.asPtr(),
        optionEntries.asPtr(),
        searchRootEntries.asPtr(),
        ZC_ASSERT_NONNULL(acceptedDistribution),
    };
    contextAuthority =
        module_graph_query::CompleteCompilationContextAuthority::fromVerified(sources);
  }
  if (contextAuthority == zc::none) { return false; }

  const auto previousRevision = impl->queryDatabase.snapshot().revision();
  zc::Maybe<core_library_query::VerifiedCoreDistributionInputTransaction> prepared;
  ZC_IF_SOME(graph, impl->crateGraph) {
    ZC_IF_SOME(request, impl->packageRequest) {
      prepared = core_library_query::VerifiedCoreDistributionInputTransaction::prepare(
          previousRevision, distribution, request, zc::mv(ZC_ASSERT_NONNULL(contextAuthority)),
          ZC_ASSERT_NONNULL(compilationOptions), graph.crates());
    }
  }
  if (prepared == zc::none) { return false; }
  auto commit = ZC_ASSERT_NONNULL(prepared).commit(impl->queryDatabase);
  if (!commit.isCommitted()) { return false; }
  for (const auto& projection : ZC_ASSERT_NONNULL(prepared).projections()) {
    if (projection.catalog().entries().size() != distribution.snapshots().size()) { return false; }
    for (size_t index = 0; index < distribution.snapshots().size(); ++index) {
      const auto& entry = projection.catalog().entries()[index];
      const auto& snapshot = distribution.snapshots()[index];
      if (entry.contentDigest() != snapshot.contentDigest()) { return false; }
      bool added = false;
      auto registered = impl->registerSource(entry.source().clone(), entry.module(),
                                             zc::heapArray<zc::byte>(snapshot.bytes()),
                                             coreSourceIdentifier(snapshot.path()), added);
      if (registered == zc::none || !added) { return false; }
    }
  }
  impl->coreDistributionInputs = zc::mv(ZC_ASSERT_NONNULL(prepared));
  return true;
}

zc::Maybe<const package::VerifiedPackageCompilationRequest&>
CompilerSession::getPackageCompilationRequest() const noexcept {
  ZC_IF_SOME(request, impl->packageRequest) { return request; }
  return zc::none;
}

zc::ArrayPtr<const package::FinalizedCompilationRoot>
CompilerSession::getFinalizedCompilationRoots() const noexcept {
  ZC_IF_SOME(graph, impl->crateGraph) { return graph.roots(); }
  return zc::ArrayPtr<const package::FinalizedCompilationRoot>();
}

zc::Maybe<const VerifiedCrateGraph&> CompilerSession::getVerifiedCrateGraph() const noexcept {
  ZC_IF_SOME(graph, impl->crateGraph) { return graph; }
  return zc::none;
}

zc::ArrayPtr<const VerifiedPreparatoryCrateGraph>
CompilerSession::getVerifiedPreparatoryCrateGraphs() const noexcept {
  return impl->preparatoryCrateGraphs;
}

zc::Maybe<const ir::VerifiedTargetSelection&> CompilerSession::getVerifiedHostTarget()
    const noexcept {
  ZC_IF_SOME(target, impl->verifiedHostTarget) { return target; }
  return zc::none;
}

zc::Maybe<const ir::VerifiedTargetSelection&> CompilerSession::getVerifiedTarget() const noexcept {
  ZC_IF_SOME(target, impl->verifiedTarget) { return target; }
  return zc::none;
}

zc::Maybe<const package::ResolutionOutput&> CompilerSession::getResolvedPackageGraph()
    const noexcept {
  ZC_IF_SOME(graph, impl->packageGraph) { return graph; }
  return zc::none;
}

zc::ArrayPtr<const package::ResolvedPackageSourceSnapshot>
CompilerSession::getResolvedPackageSnapshots() const noexcept {
  return impl->packageSnapshots;
}

zc::Maybe<package::MaterializationIssue> CompilerSession::finishResolvedPackageSnapshots() {
  zc::Maybe<package::MaterializationIssue> firstIssue;
  for (auto& snapshot : impl->packageSnapshots) {
    ZC_IF_SOME(issue, snapshot.finish()) {
      if (firstIssue == zc::none) { firstIssue = issue; }
    }
  }
  impl->packageSnapshots.clear();
  return firstIssue;
}

zc::Maybe<package::BuildScriptIssue> CompilerSession::executeBuildScripts(
    package::BuildScriptPlanExecutor& executor) {
  if (impl->packageRequest == zc::none || impl->verifiedHostTarget == zc::none ||
      impl->verifiedTarget == zc::none || impl->packageGraph == zc::none ||
      impl->packageSnapshots.size() == 0 || impl->buildScriptPlan == zc::none ||
      impl->crateGraph == zc::none || impl->buildScriptResults != zc::none) {
    return package::BuildScriptIssue::BuildResultIntegrityViolation;
  }

  ZC_IF_SOME(plan, impl->buildScriptPlan) {
    zc::Vector<package::VerifiedBuildScriptResult> completed(plan.nodes().size());
    zc::Vector<VerifiedPreparatoryCrateGraph> preparatoryGraphs(plan.nodes().size());
    for (const auto& node : plan.nodes()) {
      bool packageFound = false;
      ZC_IF_SOME(graph, impl->packageGraph) {
        for (const auto& selected : graph.packages()) {
          if (selected.key().encode().asPtr() ==
              node.key().preparatory().package().encode().asPtr()) {
            packageFound = true;
            break;
          }
        }
      }
      if (!packageFound ||
          node.key().preparatory().targetName() != node.contract().target().name()) {
        return package::BuildScriptIssue::BuildResultIntegrityViolation;
      }
    }
    for (const auto nodeIndex : plan.executionOrder()) {
      const auto& node = plan.nodes()[nodeIndex];
      ZC_IF_SOME(request, impl->packageRequest) {
        ZC_IF_SOME(resolution, impl->packageGraph) {
          auto graph =
              VerifiedPreparatoryCrateGraph::build(request, node, resolution, plan, completed);
          if (!graph.is<VerifiedPreparatoryCrateGraph>()) {
            return package::BuildScriptIssue::BuildResultIntegrityViolation;
          }
          auto executed =
              executor.execute(node, graph.get<VerifiedPreparatoryCrateGraph>(), completed);
          if (executed.is<package::BuildScriptIssue>()) {
            return executed.get<package::BuildScriptIssue>();
          }
          auto result = zc::mv(executed.get<package::VerifiedBuildScriptResult>());
          if (result.output().producerKey().digest() !=
              node.key().preparatory().producerKey().digest()) {
            return package::BuildScriptIssue::BuildResultIntegrityViolation;
          }
          preparatoryGraphs.add(zc::mv(graph.get<VerifiedPreparatoryCrateGraph>()));
          completed.add(zc::mv(result));
        }
      }
    }

    zc::Vector<identity::PreparatoryBuildScriptKey> planKeys(plan.nodes().size());
    for (const auto& node : plan.nodes()) { planKeys.add(node.key().preparatory().clone()); }
    auto results = package::VerifiedBuildScriptResultSet::from(zc::mv(planKeys), zc::mv(completed));
    if (results.is<package::BuildResultIntegrityViolation>()) {
      return package::BuildScriptIssue::BuildResultIntegrityViolation;
    }
    auto& value = results.get<package::VerifiedBuildScriptResultSet>();
    impl->preparatoryCrateGraphs = zc::mv(preparatoryGraphs);
    impl->buildScriptResults = zc::mv(value);
    return zc::none;
  }
  return package::BuildScriptIssue::BuildResultIntegrityViolation;
}

zc::Maybe<const package::VerifiedBuildScriptPlan&> CompilerSession::getBuildScriptPlan()
    const noexcept {
  ZC_IF_SOME(plan, impl->buildScriptPlan) { return plan; }
  return zc::none;
}

zc::Maybe<const package::VerifiedBuildScriptResultSet&> CompilerSession::getBuildScriptResults()
    const noexcept {
  ZC_IF_SOME(results, impl->buildScriptResults) { return results; }
  return zc::none;
}

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
