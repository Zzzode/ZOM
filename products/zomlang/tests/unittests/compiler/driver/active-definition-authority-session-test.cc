// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-session.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/named-item-query.h"
#include "zomlang/compiler/driver/owner-body-query.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

namespace graph_query = module_graph_query;
namespace resolution_query = incremental_module_resolution_query;
namespace source_query = identity::source_query;

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

identity::ResolvedVersion version() { return scalar<identity::ResolvedVersion>("0.0.0"_zc); }

identity::SortedFeatureSet packageFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> values;
  auto result = identity::SortedTargetFeatureSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::PackageKey package(zc::StringPtr name = "authority"_zc) {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>(name), version(), packageFeatures());
}

identity::CanonicalTargetSpecificationKey target() {
  auto result = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("x86_64"_zc),
      scalar<identity::TargetComponentName>("zom"_zc),
      scalar<identity::TargetComponentName>("none"_zc),
      scalar<identity::TargetComponentName>("unknown"_zc),
      scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
      targetFeatures());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto result = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CrateKey crate(zc::StringPtr packageName = "authority"_zc) {
  auto result = identity::CrateKey::from(
      identity::CompilationUnitIdentity::userPackage(package(packageName)),
      identity::CrateTargetKind::Library, scalar<identity::TargetName>(packageName), compilation());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::ModuleKey namedSemanticModule(zc::StringPtr moduleName,
                                        zc::StringPtr packageName = "authority"_zc) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(moduleName));
  auto result = identity::ModuleKey::from(crate(packageName), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::ModuleKey semanticModule(zc::StringPtr packageName = "authority"_zc) {
  return namedSemanticModule("root"_zc, packageName);
}

StableCrateQueryKey stableCrate(zc::StringPtr packageName = "authority"_zc) {
  auto value = crate(packageName);
  auto result = StableCrateQueryKey::fromVerified(value);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

StableModuleQueryKey stableModule(zc::StringPtr packageName = "authority"_zc) {
  auto value = semanticModule(packageName);
  auto result = StableModuleQueryKey::fromVerified(value);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

StableModuleQueryKey stableNamedModule(zc::StringPtr moduleName) {
  auto value = namedSemanticModule(moduleName);
  auto result = StableModuleQueryKey::fromVerified(value);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SourceFileKey namedSource(zc::StringPtr fileName,
                                    zc::StringPtr packageName = "authority"_zc) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>(fileName));
  return identity::SourceFileKey::from(
      crate(packageName), identity::SourceOriginKey::localFile(
                              identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

identity::SourceFileKey source() { return namedSource("root.zom"_zc); }

source_query::StableSourceQueryKey stableSource() {
  auto value = source();
  auto result = source_query::StableSourceQueryKey::fromVerified(value);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

source_query::StableSourceQueryKey stableNamedSource(zc::StringPtr fileName) {
  auto value = namedSource(fileName);
  auto result = source_query::StableSourceQueryKey::fromVerified(value);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

source_query::CanonicalSourceSnapshot sourceSnapshot(zc::StringPtr text) {
  auto immutable =
      identity::ImmutableSourceSnapshot::from(source(), zc::heapArray<uint8_t>(text.asBytes()));
  auto result = source_query::CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(immutable));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

source_query::CanonicalSourceSnapshot namedSourceSnapshot(zc::StringPtr fileName,
                                                          zc::StringPtr text) {
  auto immutable = identity::ImmutableSourceSnapshot::from(namedSource(fileName),
                                                           zc::heapArray<uint8_t>(text.asBytes()));
  auto result = source_query::CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(immutable));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

source_query::CanonicalCompilationOptions compilationOptions() {
  auto digestBytes = zc::heapArray<uint8_t>(32);
  digestBytes.asPtr().fill(0x5a);
  auto digest = identity::Sha256Digest::fromBytes(digestBytes.asPtr());
  identity::CanonicalEncoder selectionEncoder;
  selectionEncoder.encodeDigest(ZC_REQUIRE_NONNULL(digest));
  selectionEncoder.encodeByteString("host"_zc.asBytes());
  target().encode(selectionEncoder);
  selectionEncoder.encodeUint8(0x02);
  auto selection = selectionEncoder.finish();

  identity::CanonicalEncoder optionsEncoder;
  optionsEncoder.encodeByteString(selection.asPtr());
  optionsEncoder.encodeByteString(selection.asPtr());
  optionsEncoder.encodeBool(true);
  optionsEncoder.encodeBool(false);
  optionsEncoder.encodeBool(false);
  auto result =
      source_query::CanonicalCompilationOptions::decodeCanonical(optionsEncoder.finish().asPtr());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CompilationRootSetQueryKey packageRoots() {
  zc::Vector<CompilationRootKey> roots;
  auto root = CompilationRootKey::userPackage(package());
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(root)));
  auto result = CompilationRootSetQueryKey::from(zc::mv(roots));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

PackageRootSetKey packageGraphRoots() {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(1);
  encoder.encodeByteString(package().encode().asPtr());
  auto result = PackageGraphInput::decodeKey(encoder.finish().asPtr());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalPackageGraph packageGraph() {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(1);
  encoder.encodeByteString(package().encode().asPtr());
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(1);
  encoder.encodeByteString(crate().encode().asPtr());
  encoder.encodeSequenceSize(0);
  auto result = PackageGraphInput::decodeValue(encoder.finish().asPtr());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

binder::StableDefinitionQueryKey stableDefinition(const identity::ModuleKey& module,
                                                  const identity::DefinitionKey& definition) {
  return binder::StableDefinitionQueryKey::from(module.clone(), definition.clone());
}

ContextualDefinitionKey contextual(const CompilationRootSetQueryKey& roots,
                                   const identity::ModuleKey& module,
                                   const identity::DefinitionKey& definition) {
  return ContextualDefinitionKey::from(roots.clone(), stableDefinition(module, definition));
}

ContextualModuleKey contextual(const CompilationRootSetQueryKey& roots,
                               const identity::ModuleKey& module) {
  return ContextualModuleKey::from(roots.clone(), module.clone());
}

ContextualBodyOwnerKey contextual(const CompilationRootSetQueryKey& roots,
                                  const identity::ModuleKey& module,
                                  const binder::StableBodyOwnerKey& owner) {
  auto body = binder::StableOwnerBodyQueryKey::from(module.clone(), owner.clone());
  return ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(body)));
}

CanonicalSourceSet activeSources() {
  zc::Vector<source_query::StableSourceQueryKey> values;
  values.add(stableSource());
  auto result = CanonicalSourceSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

graph_query::SelectedModuleCatalog selectedCatalog(bool includeSecond = false) {
  zc::Vector<graph_query::SelectedModuleRecord> entries;
  entries.add(graph_query::SelectedModuleRecord(semanticModule(), source()));
  if (includeSecond) {
    entries.add(graph_query::SelectedModuleRecord(namedSemanticModule("second"_zc),
                                                  namedSource("second.zom"_zc)));
  }
  auto result = graph_query::SelectedModuleCatalog::from(crate(), zc::mv(entries));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

graph_query::DetachedModuleDependencySiteSet dependencySites(identity::ModuleKey&& module,
                                                             identity::SourceFileKey&& sourceKey,
                                                             zc::StringPtr text) {
  auto immutable = identity::ImmutableSourceSnapshot::from(sourceKey.clone(),
                                                           zc::heapArray<uint8_t>(text.asBytes()));
  ZC_REQUIRE(immutable != zc::none);
  zc::Vector<graph_query::DetachedModuleDependencySite> sites;
  auto result = graph_query::DetachedModuleDependencySiteSet::from(
      zc::mv(module), zc::mv(sourceKey), ZC_REQUIRE_NONNULL(immutable).contentDigest(),
      zc::mv(sites));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::RequesterModuleAncestry requesterAncestry(identity::ModuleKey&& module) {
  zc::Vector<identity::ModuleKey> ancestry;
  ancestry.add(module.clone());
  auto result = identity::RequesterModuleAncestry::from(zc::mv(module), zc::mv(ancestry));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

void stageBaseInputs(ActiveDefinitionAuthorityProjectionState& state,
                     query::QueryDatabase& database, zc::StringPtr text,
                     zc::StringPtr selectedModulePackage = "authority"_zc) {
  auto pending = state.beginBaseMutation(database);
  ZC_REQUIRE(pending != zc::none);
  ZC_IF_SOME(transaction, pending) {
    auto crateKey = stableCrate();
    auto sourceKey = stableSource();
    auto graphRoots = packageGraphRoots();
    auto graph = packageGraph();
    auto selectedCrate = crate(selectedModulePackage);
    auto selectedSource = namedSource("root.zom"_zc, selectedModulePackage);
    zc::Vector<graph_query::SelectedModuleRecord> selectedEntries;
    selectedEntries.add(graph_query::SelectedModuleRecord(semanticModule(selectedModulePackage),
                                                          selectedSource.clone()));
    auto catalog =
        graph_query::SelectedModuleCatalog::from(selectedCrate.clone(), zc::mv(selectedEntries));
    auto sources = activeSources();
    auto module = semanticModule(selectedModulePackage);
    auto sites = dependencySites(module.clone(), zc::mv(selectedSource), text);
    auto ancestry = requesterAncestry(module.clone());
    auto snapshot = sourceSnapshot(text);
    auto options = compilationOptions();
    ZC_REQUIRE(catalog != zc::none);
    ZC_REQUIRE(transaction.set<PackageGraphInput>(graphRoots, graph).isApplied());
    ZC_REQUIRE(transaction
                   .set<graph_query::SelectedModuleCatalogInput>(selectedCrate,
                                                                 ZC_REQUIRE_NONNULL(catalog))
                   .isApplied());
    ZC_REQUIRE(transaction.set<UserPackageActiveSourcesInput>(crateKey, sources).isApplied());
    ZC_REQUIRE(transaction.set<graph_query::ModuleDependencySiteInput>(module, sites).isApplied());
    ZC_REQUIRE(transaction.set<resolution_query::RequesterModuleAncestryInput>(module, ancestry)
                   .isApplied());
    ZC_REQUIRE(transaction
                   .set<resolution_query::ConfiguredPreludeInput>(
                       selectedCrate, resolution_query::ExplicitModuleTarget::absent())
                   .isApplied());
    ZC_REQUIRE(transaction.set<source_query::SourceSnapshotInput>(sourceKey, snapshot).isApplied());
    ZC_REQUIRE(
        transaction.set<source_query::CompilationOptionsInput>(crate(), options).isApplied());
    ZC_REQUIRE(transaction.commit().isCommitted());
  }
}

void stageTwoModuleInputs(ActiveDefinitionAuthorityProjectionState& state,
                          query::QueryDatabase& database, zc::StringPtr rootText,
                          zc::StringPtr secondText, bool includeSecond) {
  auto pending = state.beginBaseMutation(database);
  ZC_REQUIRE(pending != zc::none);
  ZC_IF_SOME(transaction, pending) {
    auto crateKey = stableCrate();
    auto rootModule = semanticModule();
    auto secondModule = namedSemanticModule("second"_zc);
    auto rootSource = stableSource();
    auto secondSource = stableNamedSource("second.zom"_zc);
    zc::Vector<source_query::StableSourceQueryKey> sourceValues;
    sourceValues.add(rootSource.clone());
    if (includeSecond) { sourceValues.add(secondSource.clone()); }
    auto sources = CanonicalSourceSet::from(zc::mv(sourceValues));
    auto graphRoots = packageGraphRoots();
    auto graph = packageGraph();
    auto catalog = selectedCatalog(includeSecond);
    auto rootSites = dependencySites(rootModule.clone(), source(), rootText);
    auto rootAncestry = requesterAncestry(rootModule.clone());
    auto rootSnapshot = sourceSnapshot(rootText);
    auto options = compilationOptions();
    ZC_REQUIRE(sources != zc::none);
    ZC_REQUIRE(transaction.set<PackageGraphInput>(graphRoots, graph).isApplied());
    ZC_REQUIRE(
        transaction.set<graph_query::SelectedModuleCatalogInput>(crate(), catalog).isApplied());
    ZC_REQUIRE(transaction.set<UserPackageActiveSourcesInput>(crateKey, ZC_REQUIRE_NONNULL(sources))
                   .isApplied());
    ZC_REQUIRE(
        transaction.set<graph_query::ModuleDependencySiteInput>(rootModule, rootSites).isApplied());
    ZC_REQUIRE(
        transaction.set<resolution_query::RequesterModuleAncestryInput>(rootModule, rootAncestry)
            .isApplied());
    ZC_REQUIRE(transaction
                   .set<resolution_query::ConfiguredPreludeInput>(
                       crate(), resolution_query::ExplicitModuleTarget::absent())
                   .isApplied());
    ZC_REQUIRE(
        transaction.set<source_query::SourceSnapshotInput>(rootSource, rootSnapshot).isApplied());
    if (includeSecond) {
      auto secondSites =
          dependencySites(secondModule.clone(), namedSource("second.zom"_zc), secondText);
      auto secondAncestry = requesterAncestry(secondModule.clone());
      auto secondSnapshot = namedSourceSnapshot("second.zom"_zc, secondText);
      ZC_REQUIRE(transaction.set<graph_query::ModuleDependencySiteInput>(secondModule, secondSites)
                     .isApplied());
      ZC_REQUIRE(
          transaction
              .set<resolution_query::RequesterModuleAncestryInput>(secondModule, secondAncestry)
              .isApplied());
      ZC_REQUIRE(transaction.set<source_query::SourceSnapshotInput>(secondSource, secondSnapshot)
                     .isApplied());
    } else {
      ZC_REQUIRE(
          transaction.erase<graph_query::ModuleDependencySiteInput>(secondModule).isApplied());
      ZC_REQUIRE(transaction.erase<resolution_query::RequesterModuleAncestryInput>(secondModule)
                     .isApplied());
      ZC_REQUIRE(transaction.erase<source_query::SourceSnapshotInput>(secondSource).isApplied());
    }
    ZC_REQUIRE(
        transaction.set<source_query::CompilationOptionsInput>(crate(), options).isApplied());
    ZC_REQUIRE(transaction.commit().isCommitted());
  }
}

basic::ThreadPool& scheduler() {
  static basic::ThreadPool value(4);
  return value;
}

class QueryTestSemanticContextResources final : public query::SemanticContextCapabilityResources {};

query::QueryDatabase queryDatabase(basic::ThreadPool& queryScheduler) {
  auto resources = zc::heap<QueryTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  query::QueryDatabase result(queryScheduler, query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
  ZC_REQUIRE(graph_query::registerModuleGraphQueries(result));
  ZC_REQUIRE(graph_query::registerStableModuleGraphQueries(result));
  return result;
}

void runDifferentialEdits(uint32_t workerCount) {
  basic::ThreadPool reusedScheduler(workerCount);
  auto reusedDatabase = queryDatabase(reusedScheduler);
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(reusedDatabase));
  ActiveDefinitionAuthorityProjectionState reusedState;
  auto roots = packageRoots();
  auto module = stableModule();
  const zc::StringPtr edits[] = {
      "module root;\nfun Alpha() { let value = 0; }\n"_zc,
      "module root;\n\nfun Alpha() { let value = 0; }\n"_zc,
      "module root;\nfun Alpha() { let value = 1; }\n"_zc,
      "module root;\n"_zc,
      "module root;\nfun Beta() { let value = 1; }\n"_zc,
  };
  for (size_t editIndex = 0; editIndex < sizeof(edits) / sizeof(edits[0]); ++editIndex) {
    stageBaseInputs(reusedState, reusedDatabase, edits[editIndex]);
    ZC_REQUIRE(reusedState.refresh(reusedDatabase, roots));
    auto reused = reusedDatabase.snapshot();
    auto reusedInventory = reused.get<NamedDefinitionInventoryQuery>(module);
    ZC_REQUIRE(reusedInventory.kind() == query::QueryValueKind::Value);

    basic::ThreadPool freshScheduler(workerCount);
    auto freshDatabase = queryDatabase(freshScheduler);
    ZC_REQUIRE(registerIncrementalBindingQueryAdapter(freshDatabase));
    ActiveDefinitionAuthorityProjectionState freshState;
    stageBaseInputs(freshState, freshDatabase, edits[editIndex]);
    ZC_REQUIRE(freshState.refresh(freshDatabase, roots));
    auto fresh = freshDatabase.snapshot();
    auto freshInventory = fresh.get<NamedDefinitionInventoryQuery>(module);
    ZC_REQUIRE(freshInventory.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(reusedInventory.value().entries().size() == freshInventory.value().entries().size());

    for (size_t index = 0; index < reusedInventory.value().entries().size(); ++index) {
      const auto& reusedEntry = reusedInventory.value().entries()[index];
      const auto& freshEntry = freshInventory.value().entries()[index];
      ZC_REQUIRE(reusedEntry.key() == freshEntry.key());
      ZC_EXPECT(reusedEntry.canonicalRecord() == freshEntry.canonicalRecord());
      auto reusedKey = contextual(roots, semanticModule(), reusedEntry.key());
      auto freshKey = contextual(roots, semanticModule(), freshEntry.key());
      auto reusedAuthority = reused.get<ActiveDefinitionAuthorityInput>(reusedKey);
      auto freshAuthority = fresh.get<ActiveDefinitionAuthorityInput>(freshKey);
      ZC_REQUIRE(reusedAuthority.kind() == query::QueryValueKind::Value);
      ZC_REQUIRE(freshAuthority.kind() == query::QueryValueKind::Value);
      ZC_EXPECT(reusedAuthority.value().encode().asPtr() ==
                freshAuthority.value().encode().asPtr());
      if (editIndex + 1 == sizeof(edits) / sizeof(edits[0])) {
        auto reusedSyntax = reused.get<NamedItemSyntaxQuery>(reusedKey);
        auto freshSyntax = fresh.get<NamedItemSyntaxQuery>(freshKey);
        auto reusedProvenance = reused.getCapability<NamedItemProvenanceQuery>(reusedKey);
        auto freshProvenance = fresh.getCapability<NamedItemProvenanceQuery>(freshKey);
        ZC_REQUIRE(reusedSyntax.isRuntimeFailure());
        ZC_REQUIRE(freshSyntax.isRuntimeFailure());
        ZC_EXPECT(reusedSyntax.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
        ZC_EXPECT(freshSyntax.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
        ZC_REQUIRE(reusedProvenance.isRuntimeRejected());
        ZC_REQUIRE(freshProvenance.isRuntimeRejected());
        ZC_EXPECT(reusedProvenance.runtimeFailure() ==
                  query::QueryRuntimeFailure::FinalSealRequired);
        ZC_EXPECT(freshProvenance.runtimeFailure() ==
                  query::QueryRuntimeFailure::FinalSealRequired);
      }
    }
  }
}

}  // namespace

ZC_TEST("Active definition authority session invalidates and atomically refreshes readiness") {
  ZC_EXPECT(NamedItemProvenanceQuery::descriptor.retention == query::RetentionClass::Retained);
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicateProvenance = database.registerDescriptor<NamedItemProvenanceQuery>();
  ZC_EXPECT(!duplicateProvenance.isRegistered());
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto module = stableModule();
  auto readyKey = roots.clone();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  auto staged = database.snapshot();
  ZC_EXPECT(staged.probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_REQUIRE(state.refresh(database, roots));

  auto first = database.snapshot();
  auto firstInventory = first.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(firstInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(firstInventory.value().entries().size() == 1);
  auto alphaKey = firstInventory.value().entries()[0].key().clone();
  auto alphaQueryKey = contextual(roots, semanticModule(), alphaKey);
  auto alpha = first.get<ActiveDefinitionAuthorityInput>(alphaQueryKey);
  auto firstReady = first.get<ActiveDefinitionAuthorityReadyInput>(readyKey);
  ZC_REQUIRE(alpha.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(firstReady.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(!first.hasRetainedValue<NamedItemProvenanceQuery>(alphaQueryKey));
  ZC_EXPECT(alpha.value().encode().asPtr() ==
            firstInventory.value().entries()[0].canonicalRecord());
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == stableDefinition(semanticModule(), alphaKey));
  ZC_EXPECT(state.keyLedger()[0].module().encode().asPtr() == semanticModule().encode().asPtr());
  auto firstAuthorityMetadata = first.metadata<ActiveDefinitionAuthorityInput>(alphaQueryKey);
  ZC_REQUIRE(firstAuthorityMetadata != zc::none);

  stageBaseInputs(state, database, "module root;\n\nclass Alpha {}\n"_zc);
  auto invalidated = database.snapshot();
  ZC_EXPECT(invalidated.probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(invalidated.get<ActiveDefinitionAuthorityInput>(alphaQueryKey).kind() ==
            query::QueryValueKind::Value);
  ZC_EXPECT(first.get<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Value);
  ZC_REQUIRE(state.refresh(database, roots));

  auto shifted = database.snapshot();
  auto shiftedInventory = shifted.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(shiftedInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(shiftedInventory.value().entries().size() == 1);
  ZC_EXPECT(shiftedInventory.value().entries()[0].key() == alphaKey);
  auto shiftedMetadata = shifted.metadata<ActiveDefinitionAuthorityInput>(alphaQueryKey);
  auto shiftedSyntax = shifted.get<NamedItemSyntaxQuery>(alphaQueryKey);
  auto shiftedProvenance = shifted.getCapability<NamedItemProvenanceQuery>(alphaQueryKey);
  ZC_REQUIRE(shiftedMetadata != zc::none);
  ZC_REQUIRE(shiftedSyntax.isRuntimeFailure());
  ZC_EXPECT(shiftedSyntax.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  ZC_REQUIRE(shiftedProvenance.isRuntimeRejected());
  ZC_EXPECT(shiftedProvenance.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(shiftedMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstAuthorityMetadata).changedAt());
  auto shiftedSyntaxMetadata = shifted.metadata<NamedItemSyntaxQuery>(alphaQueryKey);
  auto shiftedProvenanceMetadata = shifted.metadata<NamedItemProvenanceQuery>(alphaQueryKey);
  ZC_EXPECT(shiftedSyntaxMetadata == zc::none);
  ZC_EXPECT(shiftedProvenanceMetadata == zc::none);
}

ZC_TEST("Active definition authority session fails closed and erases stale keys on retry") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto module = stableModule();
  auto readyKey = roots.clone();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  ZC_REQUIRE(state.refresh(database, roots));
  auto complete = database.snapshot();
  auto alphaInventory = complete.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(alphaInventory.kind() == query::QueryValueKind::Value);
  auto alphaKey = alphaInventory.value().entries()[0].key().clone();
  auto alphaQueryKey = contextual(roots, semanticModule(), alphaKey);

  stageBaseInputs(state, database, "module root;\nclass Alpha {\n"_zc);
  auto rejected = database.snapshot();
  ZC_EXPECT(rejected.probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(!state.refresh(database, roots));
  ZC_EXPECT(database.snapshot().probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(complete.get<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Value);
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == stableDefinition(semanticModule(), alphaKey));
  ZC_EXPECT(state.keyLedger()[0].module().encode().asPtr() == semanticModule().encode().asPtr());

  stageBaseInputs(state, database, "module root;\nclass Beta {}\n"_zc);
  auto retry = database.snapshot();
  auto retryInventory = retry.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(retryInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(retryInventory.value().entries().size() == 1);
  ZC_EXPECT(retry.probeInput<ActiveDefinitionAuthorityInput>(alphaQueryKey).kind() ==
            query::QueryValueKind::Value);
  ZC_REQUIRE(state.refresh(database, roots));
  auto recovered = database.snapshot();
  auto betaInventory = recovered.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(betaInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(betaInventory.value().entries().size() == 1);
  auto betaKey = betaInventory.value().entries()[0].key().clone();
  auto betaQueryKey = contextual(roots, semanticModule(), betaKey);
  ZC_EXPECT(betaKey != alphaKey);
  ZC_EXPECT(recovered.probeInput<ActiveDefinitionAuthorityInput>(alphaQueryKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(recovered.get<ActiveDefinitionAuthorityInput>(betaQueryKey).kind() ==
            query::QueryValueKind::Value);
  ZC_EXPECT(recovered.get<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Value);
  auto inactiveAlpha = recovered.get<NamedItemSyntaxQuery>(alphaQueryKey);
  auto betaSyntax = recovered.get<NamedItemSyntaxQuery>(betaQueryKey);
  auto betaProvenance = recovered.getCapability<NamedItemProvenanceQuery>(betaQueryKey);
  ZC_REQUIRE(!inactiveAlpha.isRuntimeFailure());
  ZC_EXPECT(inactiveAlpha.kind() == query::QueryValueKind::SemanticFailure);
  ZC_REQUIRE(betaSyntax.isRuntimeFailure());
  ZC_EXPECT(betaSyntax.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  ZC_REQUIRE(betaProvenance.isRuntimeRejected());
  ZC_EXPECT(betaProvenance.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == stableDefinition(semanticModule(), betaKey));
  ZC_EXPECT(state.keyLedger()[0].module().encode().asPtr() == semanticModule().encode().asPtr());
}

ZC_TEST("Active definition authority session rejects modules outside their active crate") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto readyKey = roots.clone();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc, "foreign"_zc);
  ZC_EXPECT(!state.refresh(database, roots));
  ZC_EXPECT(database.snapshot().probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(state.keyLedger().size() == 0);
}

ZC_TEST("Named identity inventory admits only the reserved core root mismatch") {
  auto coreDatabase = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(coreDatabase));
  ActiveDefinitionAuthorityProjectionState coreState;
  stageBaseInputs(coreState, coreDatabase, "module core;\nfun Alpha() {}\n"_zc);
  auto coreSnapshot = coreDatabase.snapshot();
  auto coreInventory = coreSnapshot.get<NamedDefinitionInventoryQuery>(stableModule());
  ZC_REQUIRE(coreInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(coreInventory.value().entries().size() == 1);

  auto mismatchDatabase = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(mismatchDatabase));
  ActiveDefinitionAuthorityProjectionState mismatchState;
  stageBaseInputs(mismatchState, mismatchDatabase, "module wrong;\nfun Alpha() {}\n"_zc);
  auto mismatchSnapshot = mismatchDatabase.snapshot();
  auto mismatchInventory = mismatchSnapshot.get<NamedDefinitionInventoryQuery>(stableModule());
  ZC_REQUIRE(mismatchInventory.isRuntimeFailure());
  ZC_EXPECT(mismatchInventory.runtimeFailure() == query::QueryRuntimeFailure::ProviderRejected);
}

ZC_TEST("Active definition authority differential edits are deterministic across workers") {
  for (const auto workerCount : {uint32_t{1}, uint32_t{2}, uint32_t{8}}) {
    runDifferentialEdits(workerCount);
  }
}

ZC_TEST("Active definition authority isolates modules shrinks sets and tracks moves") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto rootModule = semanticModule();
  auto secondModule = namedSemanticModule("second"_zc);
  auto rootModuleQueryKey = stableModule();
  auto secondModuleQueryKey = stableNamedModule("second"_zc);

  stageTwoModuleInputs(state, database, "module root;\nfun Alpha() {}\n"_zc,
                       "module second;\nfun Beta() { let value = 0; }\n"_zc, true);
  ZC_REQUIRE(state.refresh(database, roots));
  auto first = database.snapshot();
  auto rootInventory = first.get<NamedDefinitionInventoryQuery>(rootModuleQueryKey);
  auto secondInventory = first.get<NamedDefinitionInventoryQuery>(secondModuleQueryKey);
  ZC_REQUIRE(rootInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(secondInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(rootInventory.value().entries().size() == 1);
  ZC_REQUIRE(secondInventory.value().entries().size() == 1);
  auto alphaKey = rootInventory.value().entries()[0].key().clone();
  auto betaKey = secondInventory.value().entries()[0].key().clone();
  auto alphaQueryKey = contextual(roots, rootModule, alphaKey);
  auto betaQueryKey = contextual(roots, secondModule, betaKey);
  auto alphaMetadata = first.metadata<ActiveDefinitionAuthorityInput>(alphaQueryKey);
  ZC_REQUIRE(alphaMetadata != zc::none);

  stageTwoModuleInputs(state, database, "module root;\nfun Alpha() {}\n"_zc,
                       "module second;\nfun Beta() { let value = 1; }\n"_zc, true);
  ZC_REQUIRE(state.refresh(database, roots));
  auto unrelatedEdit = database.snapshot();
  auto unchangedAlpha = unrelatedEdit.get<ActiveDefinitionAuthorityInput>(alphaQueryKey);
  ZC_REQUIRE(unchangedAlpha.kind() == query::QueryValueKind::Value);
  auto unchangedAlphaMetadata =
      unrelatedEdit.metadata<ActiveDefinitionAuthorityInput>(alphaQueryKey);
  ZC_REQUIRE(unchangedAlphaMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(unchangedAlphaMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(alphaMetadata).changedAt());

  stageTwoModuleInputs(state, database, "module root;\nfun Alpha() {}\n"_zc, ""_zc, false);
  ZC_REQUIRE(state.refresh(database, roots));
  auto shrunk = database.snapshot();
  ZC_EXPECT(shrunk.probeInput<ActiveDefinitionAuthorityInput>(betaQueryKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(shrunk.get<ActiveDefinitionAuthorityInput>(alphaQueryKey).kind() ==
            query::QueryValueKind::Value);
  ZC_EXPECT(shrunk.get<NamedItemSyntaxQuery>(betaQueryKey).kind() ==
            query::QueryValueKind::SemanticFailure);
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == stableDefinition(rootModule, alphaKey));

  stageTwoModuleInputs(state, database, "module root;\n"_zc, "module second;\nfun Alpha() {}\n"_zc,
                       true);
  ZC_REQUIRE(state.refresh(database, roots));
  auto moved = database.snapshot();
  auto movedInventory = moved.get<NamedDefinitionInventoryQuery>(secondModuleQueryKey);
  ZC_REQUIRE(movedInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(movedInventory.value().entries().size() == 1);
  auto movedAlphaKey = movedInventory.value().entries()[0].key().clone();
  auto movedAlphaQueryKey = contextual(roots, secondModule, movedAlphaKey);
  ZC_EXPECT(movedAlphaKey != alphaKey);
  ZC_EXPECT(moved.probeInput<ActiveDefinitionAuthorityInput>(alphaQueryKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(moved.get<ActiveDefinitionAuthorityInput>(movedAlphaQueryKey).kind() ==
            query::QueryValueKind::Value);
  ZC_EXPECT(moved.get<NamedItemSyntaxQuery>(alphaQueryKey).kind() ==
            query::QueryValueKind::SemanticFailure);
  auto movedAlphaSyntax = moved.get<NamedItemSyntaxQuery>(movedAlphaQueryKey);
  ZC_REQUIRE(movedAlphaSyntax.isRuntimeFailure());
  ZC_EXPECT(movedAlphaSyntax.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == stableDefinition(secondModule, movedAlphaKey));
}

ZC_TEST("Owner body projection requires final admission") {
  ZC_EXPECT(OwnerBodyProvenanceQuery::descriptor.retention == query::RetentionClass::Retained);
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicateProvenance = database.registerDescriptor<OwnerBodyProvenanceQuery>();
  ZC_EXPECT(!duplicateProvenance.isRegistered());
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "let module_value = 0;\n"
                  "fun Alpha() { let value = 0; }\n"
                  "abstract class Holder {\n"
                  "  let field: i32 = 1;\n"
                  "  abstract fun pending();\n"
                  "  fun run() {}\n"
                  "}\n"_zc);
  ZC_REQUIRE(state.refresh(database, roots));

  auto snapshot = database.snapshot();
  auto moduleQueryKey = contextual(roots, semanticModule());
  auto owners = snapshot.get<ModuleBodyOwnersQuery>(moduleQueryKey);
  ZC_REQUIRE(owners.isRuntimeFailure());
  ZC_EXPECT(owners.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);

  auto owner = binder::StableBodyOwnerKey::module(semanticModule());
  auto ownerQueryKey = contextual(roots, semanticModule(), owner);
  auto syntax = snapshot.get<OwnerBodySyntaxQuery>(ownerQueryKey);
  auto provenance = snapshot.getCapability<OwnerBodyProvenanceQuery>(ownerQueryKey);
  ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(syntax.value().owner() == owner);
  ZC_REQUIRE(provenance.isRuntimeRejected());
  ZC_EXPECT(provenance.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  ZC_EXPECT(!snapshot.hasRetainedValue<OwnerBodyProvenanceQuery>(ownerQueryKey));
  const uint8_t malformedOwner[] = {0xff};
  ZC_EXPECT(OwnerBodySyntaxQuery::decodeKey(malformedOwner) == zc::none);
  ZC_EXPECT(OwnerBodyProvenanceQuery::decodeKey(malformedOwner) == zc::none);
}

ZC_TEST("Owner body final-admission rejection is deterministic across workers") {
  for (const auto workerCount : {uint32_t{1}, uint32_t{2}, uint32_t{8}}) {
    basic::ThreadPool workerScheduler(workerCount);
    auto database = queryDatabase(workerScheduler);
    ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
    ActiveDefinitionAuthorityProjectionState state;
    auto roots = packageRoots();
    stageBaseInputs(state, database,
                    "module root;\nlet module_value = 0;\n"
                    "fun Alpha() { let value = 0; }\n"
                    "fun Beta() { let value = 1; }\n"_zc);
    ZC_REQUIRE(state.refresh(database, roots));
    auto snapshot = database.snapshot();
    auto moduleQueryKey = contextual(roots, semanticModule());
    auto owners = snapshot.get<ModuleBodyOwnersQuery>(moduleQueryKey);
    ZC_REQUIRE(owners.isRuntimeFailure());
    ZC_EXPECT(owners.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  }
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
