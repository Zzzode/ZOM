// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-session.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/named-item-query.h"
#include "zomlang/compiler/driver/owner-body-query.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

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
  auto result = identity::CrateKey::from(package(packageName), identity::CrateTargetKind::Library,
                                         scalar<identity::TargetName>(packageName), compilation());
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

identity::SourceFileKey namedSource(zc::StringPtr fileName) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>(fileName));
  return identity::SourceFileKey::from(
      crate(), identity::SourceOriginKey::localFile(
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

SelectedModuleSource selectedSource() {
  auto module = semanticModule();
  auto sourceKey = source();
  auto result = SelectedModuleSource::fromVerified(module, sourceKey);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

SelectedModuleSource selectedNamedSource(zc::StringPtr moduleName, zc::StringPtr fileName) {
  auto module = namedSemanticModule(moduleName);
  auto sourceKey = namedSource(fileName);
  auto result = SelectedModuleSource::fromVerified(module, sourceKey);
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

PackageRootSetQueryKey packageRoots() {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(1);
  auto packageBytes = package().encode();
  encoder.encodeByteString(packageBytes.asPtr());
  auto result = ActiveCratesInput::decodeKey(encoder.finish().asPtr());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalCrateSet activeCrates() {
  zc::Vector<StableCrateQueryKey> values;
  values.add(stableCrate());
  auto result = CanonicalCrateSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalModuleSet activeModules(zc::StringPtr packageName = "authority"_zc) {
  zc::Vector<StableModuleQueryKey> values;
  values.add(stableModule(packageName));
  auto result = CanonicalModuleSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalSourceSet activeSources() {
  zc::Vector<source_query::StableSourceQueryKey> values;
  values.add(stableSource());
  auto result = CanonicalSourceSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

ModuleDependencySet noDependencies() {
  zc::Vector<StableModuleQueryKey> values;
  auto set = CanonicalModuleSet::from(zc::mv(values));
  return ModuleDependencySet::present(zc::mv(ZC_REQUIRE_NONNULL(set)));
}

void stageBaseInputs(ActiveDefinitionAuthorityProjectionState& state,
                     query::QueryDatabase& database, zc::StringPtr text,
                     zc::StringPtr activeModulePackage = "authority"_zc) {
  auto pending = state.beginBaseMutation(database);
  ZC_REQUIRE(pending != zc::none);
  ZC_IF_SOME(transaction, pending) {
    auto roots = packageRoots();
    auto crateKey = stableCrate();
    auto moduleKey = stableModule();
    auto sourceKey = stableSource();
    auto crates = activeCrates();
    auto modules = activeModules(activeModulePackage);
    auto sources = activeSources();
    auto dependencies = noDependencies();
    auto selected = selectedSource();
    auto snapshot = sourceSnapshot(text);
    auto options = compilationOptions();
    ZC_REQUIRE(transaction.set<ActiveCratesInput>(roots, crates));
    ZC_REQUIRE(transaction.set<ActiveModulesInput>(crateKey, modules));
    ZC_REQUIRE(transaction.set<ActiveSourcesInput>(crateKey, sources));
    ZC_REQUIRE(transaction.set<ModuleDependenciesInput>(moduleKey, dependencies));
    ZC_REQUIRE(transaction.set<SelectedModuleSourceInput>(moduleKey, selected));
    ZC_REQUIRE(transaction.set<source_query::SourceSnapshotInput>(sourceKey, snapshot));
    ZC_REQUIRE(transaction.set<source_query::CompilationOptionsInput>(
        source_query::CompilationUnitQueryKey::fixed(), options));
    ZC_REQUIRE(transaction.commit() != zc::none);
  }
}

void stageTwoModuleInputs(ActiveDefinitionAuthorityProjectionState& state,
                          query::QueryDatabase& database, zc::StringPtr rootText,
                          zc::StringPtr secondText, bool includeSecond) {
  auto pending = state.beginBaseMutation(database);
  ZC_REQUIRE(pending != zc::none);
  ZC_IF_SOME(transaction, pending) {
    auto roots = packageRoots();
    auto crateKey = stableCrate();
    auto rootModule = stableModule();
    auto secondModule = stableNamedModule("second"_zc);
    auto rootSource = stableSource();
    auto secondSource = stableNamedSource("second.zom"_zc);
    zc::Vector<StableModuleQueryKey> moduleValues;
    moduleValues.add(rootModule.clone());
    if (includeSecond) { moduleValues.add(secondModule.clone()); }
    auto modules = CanonicalModuleSet::from(zc::mv(moduleValues));
    zc::Vector<source_query::StableSourceQueryKey> sourceValues;
    sourceValues.add(rootSource.clone());
    if (includeSecond) { sourceValues.add(secondSource.clone()); }
    auto sources = CanonicalSourceSet::from(zc::mv(sourceValues));
    auto crates = activeCrates();
    auto dependencies = noDependencies();
    auto rootSelection = selectedSource();
    auto rootSnapshot = sourceSnapshot(rootText);
    auto options = compilationOptions();
    ZC_REQUIRE(modules != zc::none);
    ZC_REQUIRE(sources != zc::none);
    ZC_REQUIRE(transaction.set<ActiveCratesInput>(roots, crates));
    ZC_REQUIRE(transaction.set<ActiveModulesInput>(crateKey, ZC_REQUIRE_NONNULL(modules)));
    ZC_REQUIRE(transaction.set<ActiveSourcesInput>(crateKey, ZC_REQUIRE_NONNULL(sources)));
    ZC_REQUIRE(transaction.set<ModuleDependenciesInput>(rootModule, dependencies));
    ZC_REQUIRE(transaction.set<SelectedModuleSourceInput>(rootModule, rootSelection));
    ZC_REQUIRE(transaction.set<source_query::SourceSnapshotInput>(rootSource, rootSnapshot));
    if (includeSecond) {
      auto secondDependencies = noDependencies();
      auto secondSelection = selectedNamedSource("second"_zc, "second.zom"_zc);
      auto secondSnapshot = namedSourceSnapshot("second.zom"_zc, secondText);
      ZC_REQUIRE(transaction.set<ModuleDependenciesInput>(secondModule, secondDependencies));
      ZC_REQUIRE(transaction.set<SelectedModuleSourceInput>(secondModule, secondSelection));
      ZC_REQUIRE(transaction.set<source_query::SourceSnapshotInput>(secondSource, secondSnapshot));
    } else {
      ZC_REQUIRE(transaction.erase<ModuleDependenciesInput>(secondModule));
      ZC_REQUIRE(transaction.erase<SelectedModuleSourceInput>(secondModule));
      ZC_REQUIRE(transaction.erase<source_query::SourceSnapshotInput>(secondSource));
    }
    ZC_REQUIRE(transaction.set<source_query::CompilationOptionsInput>(
        source_query::CompilationUnitQueryKey::fixed(), options));
    ZC_REQUIRE(transaction.commit() != zc::none);
  }
}

basic::ThreadPool& scheduler() {
  static basic::ThreadPool value(4);
  return value;
}

bool containsKey(const binder::NamedDefinitionInventory& inventory,
                 const identity::DefinitionKey& key) {
  for (const auto& entry : inventory.entries()) {
    if (entry.key() == key) { return true; }
  }
  return false;
}

bool hasCurrentEvent(const query::QuerySnapshot& snapshot,
                     const query::QueryKeyFingerprint& fingerprint, query::QueryEventKind kind) {
  for (const auto& event : snapshot.events()) {
    if (event.revision() == snapshot.revision() && event.key().fingerprint() == fingerprint &&
        event.kind() == kind) {
      return true;
    }
  }
  return false;
}

bool sameDependencyShape(zc::ArrayPtr<const query::DependencyGroup> left,
                         zc::ArrayPtr<const query::DependencyGroup> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t groupIndex = 0; groupIndex < left.size(); ++groupIndex) {
    if (left[groupIndex].kind() != right[groupIndex].kind() ||
        left[groupIndex].dependencies().size() != right[groupIndex].dependencies().size()) {
      return false;
    }
    for (size_t dependencyIndex = 0; dependencyIndex < left[groupIndex].dependencies().size();
         ++dependencyIndex) {
      const auto& leftDependency = left[groupIndex].dependencies()[dependencyIndex];
      const auto& rightDependency = right[groupIndex].dependencies()[dependencyIndex];
      if (leftDependency.key().fingerprint() != rightDependency.key().fingerprint() ||
          leftDependency.durability() != rightDependency.durability() ||
          leftDependency.inputProbeObservation() != rightDependency.inputProbeObservation()) {
        return false;
      }
    }
  }
  return true;
}

bool containsFingerprint(zc::ArrayPtr<const query::QueryKeyFingerprint> fingerprints,
                         const query::QueryKeyFingerprint& candidate) {
  for (const auto& fingerprint : fingerprints) {
    if (fingerprint == candidate) { return true; }
  }
  return false;
}

zc::Array<uint8_t> trailingBytes(zc::ArrayPtr<const uint8_t> canonical) {
  auto trailing = zc::heapArray<uint8_t>(canonical.size() + 1);
  trailing.first(canonical.size()).copyFrom(canonical);
  trailing.back() = 0;
  return trailing;
}

void runDifferentialEdits(uint32_t workerCount) {
  basic::ThreadPool reusedScheduler(workerCount);
  query::QueryDatabase reusedDatabase(reusedScheduler);
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
  zc::Maybe<identity::DefinitionKey> priorKey;
  zc::Maybe<zc::Array<uint8_t>> priorSyntax;
  zc::Maybe<zc::Array<uint8_t>> priorProvenance;

  for (size_t editIndex = 0; editIndex < sizeof(edits) / sizeof(edits[0]); ++editIndex) {
    stageBaseInputs(reusedState, reusedDatabase, edits[editIndex]);
    ZC_REQUIRE(reusedState.refresh(reusedDatabase, roots));
    auto reused = reusedDatabase.snapshot();
    auto reusedInventory = reused.get<NamedDefinitionInventoryQuery>(module);
    ZC_REQUIRE(reusedInventory.kind() == query::QueryValueKind::Value);

    basic::ThreadPool freshScheduler(workerCount);
    query::QueryDatabase freshDatabase(freshScheduler);
    ZC_REQUIRE(registerIncrementalBindingQueryAdapter(freshDatabase));
    ActiveDefinitionAuthorityProjectionState freshState;
    stageBaseInputs(freshState, freshDatabase, edits[editIndex]);
    ZC_REQUIRE(freshState.refresh(freshDatabase, roots));
    auto fresh = freshDatabase.snapshot();
    auto freshInventory = fresh.get<NamedDefinitionInventoryQuery>(module);
    ZC_REQUIRE(freshInventory.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(reusedInventory.value().entries().size() == freshInventory.value().entries().size());
    zc::Maybe<identity::DefinitionKey> nextKey;
    zc::Maybe<zc::Array<uint8_t>> nextSyntax;
    zc::Maybe<zc::Array<uint8_t>> nextProvenance;

    for (size_t index = 0; index < reusedInventory.value().entries().size(); ++index) {
      const auto& reusedEntry = reusedInventory.value().entries()[index];
      const auto& freshEntry = freshInventory.value().entries()[index];
      ZC_REQUIRE(reusedEntry.key() == freshEntry.key());
      auto reusedSyntax = reused.get<NamedItemSyntaxQuery>(reusedEntry.key());
      auto freshSyntax = fresh.get<NamedItemSyntaxQuery>(freshEntry.key());
      auto reusedProvenance = reused.get<NamedItemProvenanceQuery>(reusedEntry.key());
      auto freshProvenance = fresh.get<NamedItemProvenanceQuery>(freshEntry.key());
      ZC_REQUIRE(reusedSyntax.kind() == query::QueryValueKind::Value);
      ZC_REQUIRE(freshSyntax.kind() == query::QueryValueKind::Value);
      ZC_REQUIRE(reusedProvenance.kind() == query::QueryValueKind::Value);
      ZC_REQUIRE(freshProvenance.kind() == query::QueryValueKind::Value);
      auto reusedSyntaxBytes = reusedSyntax.value().encodeCanonical();
      auto freshSyntaxBytes = freshSyntax.value().encodeCanonical();
      auto reusedProvenanceBytes = reusedProvenance.value().encodeCanonical();
      auto freshProvenanceBytes = freshProvenance.value().encodeCanonical();
      ZC_EXPECT(reusedSyntaxBytes.asPtr() == freshSyntaxBytes.asPtr());
      ZC_EXPECT(reusedProvenanceBytes.asPtr() == freshProvenanceBytes.asPtr());
      ZC_EXPECT(
          sameDependencyShape(reused.dependencies<NamedItemSyntaxQuery>(reusedEntry.key()).asPtr(),
                              fresh.dependencies<NamedItemSyntaxQuery>(freshEntry.key()).asPtr()));
      ZC_EXPECT(sameDependencyShape(
          reused.dependencies<NamedItemProvenanceQuery>(reusedEntry.key()).asPtr(),
          fresh.dependencies<NamedItemProvenanceQuery>(freshEntry.key()).asPtr()));

      auto syntaxFingerprint = reused.keyFingerprint<NamedItemSyntaxQuery>(reusedEntry.key());
      auto provenanceFingerprint =
          reused.keyFingerprint<NamedItemProvenanceQuery>(reusedEntry.key());
      ZC_REQUIRE(syntaxFingerprint != zc::none);
      ZC_REQUIRE(provenanceFingerprint != zc::none);
      if (editIndex == 1) {
        ZC_EXPECT(hasCurrentEvent(reused, ZC_REQUIRE_NONNULL(syntaxFingerprint),
                                  query::QueryEventKind::RecomputedEqual) ||
                  hasCurrentEvent(reused, ZC_REQUIRE_NONNULL(syntaxFingerprint),
                                  query::QueryEventKind::GreenReused));
        ZC_EXPECT(hasCurrentEvent(reused, ZC_REQUIRE_NONNULL(provenanceFingerprint),
                                  query::QueryEventKind::RecomputedChanged));
      } else if (editIndex == 2) {
        ZC_EXPECT(hasCurrentEvent(reused, ZC_REQUIRE_NONNULL(syntaxFingerprint),
                                  query::QueryEventKind::RecomputedChanged));
      }
      if (editIndex == 1) {
        ZC_REQUIRE(priorSyntax != zc::none);
        ZC_REQUIRE(priorProvenance != zc::none);
        ZC_EXPECT(ZC_REQUIRE_NONNULL(priorSyntax).asPtr() == reusedSyntaxBytes.asPtr());
        ZC_EXPECT(ZC_REQUIRE_NONNULL(priorProvenance).asPtr() != reusedProvenanceBytes.asPtr());
      }
      nextKey = reusedEntry.key().clone();
      nextSyntax = zc::mv(reusedSyntaxBytes);
      nextProvenance = zc::mv(reusedProvenanceBytes);
    }

    ZC_IF_SOME(previous, priorKey) {
      if (!containsKey(reusedInventory.value(), previous)) {
        auto reusedInactive = reused.get<NamedItemSyntaxQuery>(previous);
        auto freshInactive = fresh.get<NamedItemSyntaxQuery>(previous);
        ZC_REQUIRE(reusedInactive.kind() == query::QueryValueKind::SemanticFailure);
        ZC_REQUIRE(freshInactive.kind() == query::QueryValueKind::SemanticFailure);
        ZC_EXPECT(reusedInactive.semanticFailureBytes() == freshInactive.semanticFailureBytes());
      }
    }
    priorKey = zc::mv(nextKey);
    priorSyntax = zc::mv(nextSyntax);
    priorProvenance = zc::mv(nextProvenance);
  }
}

}  // namespace

ZC_TEST("Active definition authority session invalidates and atomically refreshes readiness") {
  query::QueryDatabase database(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto module = stableModule();
  const auto readyKey = source_query::CompilationUnitQueryKey::fixed();

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
  auto alpha = first.get<ActiveDefinitionAuthorityInput>(alphaKey);
  auto firstReady = first.get<ActiveDefinitionAuthorityReadyInput>(readyKey);
  auto alphaSyntax = first.get<NamedItemSyntaxQuery>(alphaKey);
  auto alphaProvenance = first.get<NamedItemProvenanceQuery>(alphaKey);
  ZC_REQUIRE(alpha.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(firstReady.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(alphaSyntax.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(alphaProvenance.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(alpha.value().encode().asPtr() ==
            firstInventory.value().entries()[0].canonicalRecord());
  ZC_EXPECT(alphaSyntax.value().detachedSyntax().rootCount() == 1);
  ZC_EXPECT(alphaSyntax.value().owningModule().encode().asPtr() ==
            semanticModule().encode().asPtr());
  ZC_EXPECT(alphaProvenance.value().detachedProvenance().entries().size() != 0);
  ZC_EXPECT(alphaProvenance.value().detachedProvenance().source().sameAs(source()));
  auto syntaxBytes = alphaSyntax.value().encodeCanonical();
  auto decodedSyntax = binder::NamedItemSyntax::decodeCanonical(syntaxBytes.asPtr());
  ZC_REQUIRE(decodedSyntax != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSyntax) == alphaSyntax.value());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSyntax).owningModule().encode().asPtr() ==
            semanticModule().encode().asPtr());
  auto provenanceBytes = alphaProvenance.value().encodeCanonical();
  auto decodedProvenance = binder::NamedItemProvenance::decodeCanonical(provenanceBytes.asPtr());
  ZC_REQUIRE(decodedProvenance != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedProvenance) == alphaProvenance.value());
  auto trailingSyntax = zc::heapArray<uint8_t>(syntaxBytes.size() + 1);
  trailingSyntax.first(syntaxBytes.size()).copyFrom(syntaxBytes.asPtr());
  trailingSyntax.back() = 0;
  ZC_EXPECT(binder::NamedItemSyntax::decodeCanonical(trailingSyntax.asPtr()) == zc::none);
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == alphaKey);
  auto firstAuthorityMetadata = first.metadata<ActiveDefinitionAuthorityInput>(alphaKey);
  auto firstSyntaxMetadata = first.metadata<NamedItemSyntaxQuery>(alphaKey);
  auto firstProvenanceMetadata = first.metadata<NamedItemProvenanceQuery>(alphaKey);
  ZC_REQUIRE(firstAuthorityMetadata != zc::none);
  ZC_REQUIRE(firstSyntaxMetadata != zc::none);
  ZC_REQUIRE(firstProvenanceMetadata != zc::none);

  auto authorityFingerprint = first.keyFingerprint<ActiveDefinitionAuthorityInput>(alphaKey);
  auto readinessFingerprint = first.keyFingerprint<ActiveDefinitionAuthorityReadyInput>(readyKey);
  auto inventoryFingerprint = first.keyFingerprint<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(authorityFingerprint != zc::none);
  ZC_REQUIRE(readinessFingerprint != zc::none);
  ZC_REQUIRE(inventoryFingerprint != zc::none);
  size_t authorityReads = 0;
  size_t inventoryReads = 0;
  size_t readinessReads = 0;
  for (const auto& group : first.dependencies<NamedItemSyntaxQuery>(alphaKey)) {
    for (const auto& dependency : group.dependencies()) {
      if (dependency.key().fingerprint() == ZC_REQUIRE_NONNULL(authorityFingerprint)) {
        ++authorityReads;
      } else if (dependency.key().fingerprint() == ZC_REQUIRE_NONNULL(inventoryFingerprint)) {
        ++inventoryReads;
      } else if (dependency.key().fingerprint() == ZC_REQUIRE_NONNULL(readinessFingerprint)) {
        ++readinessReads;
      }
    }
  }
  ZC_EXPECT(authorityReads != 0);
  ZC_EXPECT(inventoryReads != 0);
  ZC_EXPECT(readinessReads == 0);

  stageBaseInputs(state, database, "module root;\n\nclass Alpha {}\n"_zc);
  auto invalidated = database.snapshot();
  ZC_EXPECT(invalidated.probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(invalidated.get<ActiveDefinitionAuthorityInput>(alphaKey).kind() ==
            query::QueryValueKind::Value);
  ZC_EXPECT(first.get<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Value);
  ZC_REQUIRE(state.refresh(database, roots));

  auto shifted = database.snapshot();
  auto shiftedInventory = shifted.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(shiftedInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(shiftedInventory.value().entries().size() == 1);
  ZC_EXPECT(shiftedInventory.value().entries()[0].key() == alphaKey);
  auto shiftedMetadata = shifted.metadata<ActiveDefinitionAuthorityInput>(alphaKey);
  auto shiftedSyntax = shifted.get<NamedItemSyntaxQuery>(alphaKey);
  auto shiftedProvenance = shifted.get<NamedItemProvenanceQuery>(alphaKey);
  ZC_REQUIRE(shiftedMetadata != zc::none);
  ZC_REQUIRE(shiftedSyntax.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(shiftedProvenance.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(shiftedMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstAuthorityMetadata).changedAt());
  auto shiftedSyntaxMetadata = shifted.metadata<NamedItemSyntaxQuery>(alphaKey);
  auto shiftedProvenanceMetadata = shifted.metadata<NamedItemProvenanceQuery>(alphaKey);
  ZC_REQUIRE(shiftedSyntaxMetadata != zc::none);
  ZC_REQUIRE(shiftedProvenanceMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(shiftedSyntaxMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstSyntaxMetadata).changedAt());
  ZC_EXPECT(ZC_REQUIRE_NONNULL(shiftedProvenanceMetadata).changedAt() !=
            ZC_REQUIRE_NONNULL(firstProvenanceMetadata).changedAt());
  ZC_EXPECT(shiftedSyntax.value() == alphaSyntax.value());
  ZC_EXPECT(shiftedProvenance.value() != alphaProvenance.value());
}

ZC_TEST("Active definition authority session fails closed and erases stale keys on retry") {
  query::QueryDatabase database(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto module = stableModule();
  const auto readyKey = source_query::CompilationUnitQueryKey::fixed();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  ZC_REQUIRE(state.refresh(database, roots));
  auto complete = database.snapshot();
  auto alphaInventory = complete.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(alphaInventory.kind() == query::QueryValueKind::Value);
  auto alphaKey = alphaInventory.value().entries()[0].key().clone();

  stageBaseInputs(state, database, "module root;\nclass Alpha {\n"_zc);
  auto rejected = database.snapshot();
  ZC_EXPECT(rejected.probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(!state.refresh(database, roots));
  auto rejectedNamedItem = database.snapshot().get<NamedItemSyntaxQuery>(alphaKey);
  ZC_REQUIRE(rejectedNamedItem.isRuntimeFailure());
  ZC_EXPECT(rejectedNamedItem.runtimeFailure() == query::QueryRuntimeFailure::ProviderRejected);
  ZC_EXPECT(database.snapshot().probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(complete.get<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Value);
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == alphaKey);

  stageBaseInputs(state, database, "module root;\nclass Beta {}\n"_zc);
  ZC_REQUIRE(state.refresh(database, roots));
  auto recovered = database.snapshot();
  auto betaInventory = recovered.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(betaInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(betaInventory.value().entries().size() == 1);
  auto betaKey = betaInventory.value().entries()[0].key().clone();
  ZC_EXPECT(betaKey != alphaKey);
  ZC_EXPECT(recovered.probeInput<ActiveDefinitionAuthorityInput>(alphaKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(recovered.get<ActiveDefinitionAuthorityInput>(betaKey).kind() ==
            query::QueryValueKind::Value);
  ZC_EXPECT(recovered.get<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Value);
  auto inactiveAlpha = recovered.get<NamedItemSyntaxQuery>(alphaKey);
  auto betaSyntax = recovered.get<NamedItemSyntaxQuery>(betaKey);
  auto betaProvenance = recovered.get<NamedItemProvenanceQuery>(betaKey);
  ZC_REQUIRE(!inactiveAlpha.isRuntimeFailure());
  ZC_EXPECT(inactiveAlpha.kind() == query::QueryValueKind::SemanticFailure);
  ZC_EXPECT(betaSyntax.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(betaProvenance.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(state.keyLedger().size() == 1);
  ZC_EXPECT(state.keyLedger()[0] == betaKey);
}

ZC_TEST("Active definition authority session rejects modules outside their active crate") {
  query::QueryDatabase database(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  const auto readyKey = source_query::CompilationUnitQueryKey::fixed();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc, "foreign"_zc);
  ZC_EXPECT(!state.refresh(database, roots));
  ZC_EXPECT(database.snapshot().probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(state.keyLedger().size() == 0);
}

ZC_TEST("Active definition authority differential edits are deterministic across workers") {
  for (const auto workerCount : {uint32_t{1}, uint32_t{2}, uint32_t{8}}) {
    runDifferentialEdits(workerCount);
  }
}

ZC_TEST("Active definition authority isolates modules shrinks sets and tracks moves") {
  query::QueryDatabase database(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto rootModule = stableModule();
  auto secondModule = stableNamedModule("second"_zc);

  stageTwoModuleInputs(state, database, "module root;\nfun Alpha() {}\n"_zc,
                       "module second;\nfun Beta() { let value = 0; }\n"_zc, true);
  ZC_REQUIRE(state.refresh(database, roots));
  auto first = database.snapshot();
  auto rootInventory = first.get<NamedDefinitionInventoryQuery>(rootModule);
  auto secondInventory = first.get<NamedDefinitionInventoryQuery>(secondModule);
  ZC_REQUIRE(rootInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(secondInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(rootInventory.value().entries().size() == 1);
  ZC_REQUIRE(secondInventory.value().entries().size() == 1);
  auto alphaKey = rootInventory.value().entries()[0].key().clone();
  auto betaKey = secondInventory.value().entries()[0].key().clone();
  ZC_REQUIRE(first.get<NamedItemSyntaxQuery>(alphaKey).kind() == query::QueryValueKind::Value);
  auto alphaMetadata = first.metadata<NamedItemSyntaxQuery>(alphaKey);
  ZC_REQUIRE(alphaMetadata != zc::none);

  stageTwoModuleInputs(state, database, "module root;\nfun Alpha() {}\n"_zc,
                       "module second;\nfun Beta() { let value = 1; }\n"_zc, true);
  ZC_REQUIRE(state.refresh(database, roots));
  auto unrelatedEdit = database.snapshot();
  auto unchangedAlpha = unrelatedEdit.get<NamedItemSyntaxQuery>(alphaKey);
  ZC_REQUIRE(unchangedAlpha.kind() == query::QueryValueKind::Value);
  auto unchangedAlphaMetadata = unrelatedEdit.metadata<NamedItemSyntaxQuery>(alphaKey);
  ZC_REQUIRE(unchangedAlphaMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(unchangedAlphaMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(alphaMetadata).changedAt());
  auto alphaFingerprint = unrelatedEdit.keyFingerprint<NamedItemSyntaxQuery>(alphaKey);
  ZC_REQUIRE(alphaFingerprint != zc::none);
  ZC_EXPECT(hasCurrentEvent(unrelatedEdit, ZC_REQUIRE_NONNULL(alphaFingerprint),
                            query::QueryEventKind::RecomputedEqual));

  stageTwoModuleInputs(state, database, "module root;\nfun Alpha() {}\n"_zc, ""_zc, false);
  ZC_REQUIRE(state.refresh(database, roots));
  auto shrunk = database.snapshot();
  ZC_EXPECT(shrunk.get<NamedItemSyntaxQuery>(alphaKey).kind() == query::QueryValueKind::Value);
  ZC_EXPECT(shrunk.get<NamedItemSyntaxQuery>(betaKey).kind() ==
            query::QueryValueKind::SemanticFailure);

  stageTwoModuleInputs(state, database, "module root;\n"_zc, "module second;\nfun Alpha() {}\n"_zc,
                       true);
  ZC_REQUIRE(state.refresh(database, roots));
  auto moved = database.snapshot();
  auto movedInventory = moved.get<NamedDefinitionInventoryQuery>(secondModule);
  ZC_REQUIRE(movedInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(movedInventory.value().entries().size() == 1);
  auto movedAlphaKey = movedInventory.value().entries()[0].key().clone();
  ZC_EXPECT(movedAlphaKey != alphaKey);
  ZC_EXPECT(moved.get<NamedItemSyntaxQuery>(alphaKey).kind() ==
            query::QueryValueKind::SemanticFailure);
  ZC_EXPECT(moved.get<NamedItemSyntaxQuery>(movedAlphaKey).kind() == query::QueryValueKind::Value);
}

ZC_TEST("Owner body projection records exact alternative dependencies") {
  query::QueryDatabase database(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ActiveDefinitionAuthorityProjectionState state;
  auto roots = packageRoots();
  auto module = stableModule();
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
  auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(module);
  auto owners = snapshot.get<ModuleBodyOwnersQuery>(module);
  ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(owners.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(owners.value().owners().size() == 4);
  ZC_EXPECT(owners.value().owners()[0].kind() == binder::StableBodyOwnerKind::Module);
  ZC_EXPECT(owners.value().owningModule().encode().asPtr() == semanticModule().encode().asPtr());

  auto ownersBytes = owners.value().encodeCanonical();
  auto decodedOwners = binder::ModuleBodyOwners::decodeCanonical(ownersBytes.asPtr());
  ZC_REQUIRE(decodedOwners != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedOwners) == owners.value());
  auto ownersTrailing = trailingBytes(ownersBytes.asPtr());
  ZC_EXPECT(binder::ModuleBodyOwners::decodeCanonical(ownersTrailing.asPtr()) == zc::none);

  auto inventoryFingerprint = snapshot.keyFingerprint<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(inventoryFingerprint != zc::none);
  zc::Vector<query::QueryKeyFingerprint> namedItemFingerprints(inventory.value().entries().size());
  zc::Maybe<identity::DefinitionKey> bodylessDefinition;
  for (const auto& entry : inventory.value().entries()) {
    auto syntax = snapshot.get<NamedItemSyntaxQuery>(entry.key());
    ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
    auto fingerprint = snapshot.keyFingerprint<NamedItemSyntaxQuery>(entry.key());
    ZC_REQUIRE(fingerprint != zc::none);
    namedItemFingerprints.add(ZC_REQUIRE_NONNULL(fingerprint));
    const auto rootKind =
        ZC_REQUIRE_NONNULL(syntax.value().detachedSyntax().nodes()[0].syntaxKind());
    if (rootKind == ast::SyntaxKind::MethodDecl) {
      bool admitted = false;
      for (const auto& owner : owners.value().owners()) {
        ZC_IF_SOME(definition, owner.definitionKey()) {
          if (definition == entry.key()) { admitted = true; }
        }
      }
      if (!admitted) { bodylessDefinition = entry.key().clone(); }
    }
  }

  size_t sequentialInventoryReads = 0;
  size_t parallelNamedItemGroups = 0;
  for (const auto& group : snapshot.dependencies<ModuleBodyOwnersQuery>(module)) {
    if (group.kind() == query::DependencyGroup::Kind::Sequential) {
      ZC_REQUIRE(group.dependencies().size() == 1);
      ZC_EXPECT(group.dependencies()[0].key().fingerprint() ==
                ZC_REQUIRE_NONNULL(inventoryFingerprint));
      ++sequentialInventoryReads;
    } else {
      ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Parallel);
      ZC_REQUIRE(group.dependencies().size() == namedItemFingerprints.size());
      for (const auto& dependency : group.dependencies()) {
        ZC_EXPECT(
            containsFingerprint(namedItemFingerprints.asPtr(), dependency.key().fingerprint()));
      }
      ++parallelNamedItemGroups;
    }
  }
  ZC_EXPECT(sequentialInventoryReads == 2);
  ZC_EXPECT(parallelNamedItemGroups == 2);

  for (const auto& owner : owners.value().owners()) {
    auto syntax = snapshot.get<OwnerBodySyntaxQuery>(owner);
    auto provenance = snapshot.get<OwnerBodyProvenanceQuery>(owner);
    ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(provenance.kind() == query::QueryValueKind::Value);
    ZC_EXPECT(syntax.value().owner() == owner);
    ZC_EXPECT(provenance.value().owner() == owner);

    auto syntaxBytes = syntax.value().encodeCanonical();
    auto decodedSyntax = binder::OwnerBodySyntax::decodeCanonical(syntaxBytes.asPtr());
    ZC_REQUIRE(decodedSyntax != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSyntax) == syntax.value());
    auto syntaxTrailing = trailingBytes(syntaxBytes.asPtr());
    ZC_EXPECT(binder::OwnerBodySyntax::decodeCanonical(syntaxTrailing.asPtr()) == zc::none);

    auto provenanceBytes = provenance.value().encodeCanonical();
    auto decodedProvenance = binder::OwnerBodyProvenance::decodeCanonical(provenanceBytes.asPtr());
    ZC_REQUIRE(decodedProvenance != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedProvenance) == provenance.value());
    auto provenanceTrailing = trailingBytes(provenanceBytes.asPtr());
    ZC_EXPECT(binder::OwnerBodyProvenance::decodeCanonical(provenanceTrailing.asPtr()) == zc::none);

    auto ownerSyntaxFingerprint = snapshot.keyFingerprint<OwnerBodySyntaxQuery>(owner);
    ZC_REQUIRE(ownerSyntaxFingerprint != zc::none);
    size_t syntaxDependencyReads = 0;
    query::QueryKeyFingerprint expectedSyntaxDependency =
        ZC_REQUIRE_NONNULL(snapshot.keyFingerprint<ModuleBodySyntaxQuery>(module));
    if (owner.kind() == binder::StableBodyOwnerKind::Definition) {
      expectedSyntaxDependency = ZC_REQUIRE_NONNULL(
          snapshot.keyFingerprint<NamedItemSyntaxQuery>(ZC_REQUIRE_NONNULL(owner.definitionKey())));
    }
    for (const auto& group : snapshot.dependencies<OwnerBodySyntaxQuery>(owner)) {
      ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
      ZC_REQUIRE(group.dependencies().size() == 1);
      ZC_EXPECT(group.dependencies()[0].key().fingerprint() == expectedSyntaxDependency);
      ++syntaxDependencyReads;
    }
    ZC_EXPECT(syntaxDependencyReads == 2);

    size_t ownerSyntaxReads = 0;
    size_t provenanceReads = 0;
    query::QueryKeyFingerprint expectedProvenanceDependency =
        ZC_REQUIRE_NONNULL(snapshot.keyFingerprint<ModuleBodyProvenanceQuery>(module));
    if (owner.kind() == binder::StableBodyOwnerKind::Definition) {
      expectedProvenanceDependency =
          ZC_REQUIRE_NONNULL(snapshot.keyFingerprint<NamedItemProvenanceQuery>(
              ZC_REQUIRE_NONNULL(owner.definitionKey())));
    }
    for (const auto& group : snapshot.dependencies<OwnerBodyProvenanceQuery>(owner)) {
      ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
      ZC_REQUIRE(group.dependencies().size() == 1);
      const auto& fingerprint = group.dependencies()[0].key().fingerprint();
      if (fingerprint == ZC_REQUIRE_NONNULL(ownerSyntaxFingerprint)) {
        ++ownerSyntaxReads;
      } else if (fingerprint == expectedProvenanceDependency) {
        ++provenanceReads;
      } else {
        ZC_FAIL_REQUIRE("Owner body provenance recorded an undeclared dependency");
      }
    }
    ZC_EXPECT(ownerSyntaxReads == 2);
    ZC_EXPECT(provenanceReads == 2);
  }

  ZC_REQUIRE(bodylessDefinition != zc::none);
  auto bodylessOwner =
      binder::StableBodyOwnerKey::definition(ZC_REQUIRE_NONNULL(bodylessDefinition).clone());
  auto rejected = snapshot.get<OwnerBodySyntaxQuery>(bodylessOwner);
  ZC_EXPECT(rejected.kind() == query::QueryValueKind::SemanticFailure);
  const uint8_t malformedOwner[] = {0xff};
  ZC_EXPECT(OwnerBodySyntaxQuery::decodeKey(malformedOwner) == zc::none);
  ZC_EXPECT(OwnerBodyProvenanceQuery::decodeKey(malformedOwner) == zc::none);
}

ZC_TEST("Owner body projections are deterministic across workers") {
  zc::Maybe<zc::Array<uint8_t>> expectedOwners;
  zc::Maybe<zc::Array<uint8_t>> expectedSyntax;
  zc::Maybe<zc::Array<uint8_t>> expectedProvenance;
  for (const auto workerCount : {uint32_t{1}, uint32_t{2}, uint32_t{8}}) {
    basic::ThreadPool workerScheduler(workerCount);
    query::QueryDatabase database(workerScheduler);
    ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
    ActiveDefinitionAuthorityProjectionState state;
    auto roots = packageRoots();
    auto module = stableModule();
    stageBaseInputs(state, database,
                    "module root;\nlet module_value = 0;\n"
                    "fun Alpha() { let value = 0; }\n"
                    "fun Beta() { let value = 1; }\n"_zc);
    ZC_REQUIRE(state.refresh(database, roots));
    auto snapshot = database.snapshot();
    auto owners = snapshot.get<ModuleBodyOwnersQuery>(module);
    ZC_REQUIRE(owners.kind() == query::QueryValueKind::Value);

    identity::CanonicalEncoder syntaxEncoder;
    identity::CanonicalEncoder provenanceEncoder;
    syntaxEncoder.encodeSequenceSize(owners.value().owners().size());
    provenanceEncoder.encodeSequenceSize(owners.value().owners().size());
    for (const auto& owner : owners.value().owners()) {
      auto syntax = snapshot.get<OwnerBodySyntaxQuery>(owner);
      auto provenance = snapshot.get<OwnerBodyProvenanceQuery>(owner);
      ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
      ZC_REQUIRE(provenance.kind() == query::QueryValueKind::Value);
      auto syntaxBytes = syntax.value().encodeCanonical();
      auto provenanceBytes = provenance.value().encodeCanonical();
      syntaxEncoder.encodeByteString(syntaxBytes.asPtr());
      provenanceEncoder.encodeByteString(provenanceBytes.asPtr());
    }
    auto ownerBytes = owners.value().encodeCanonical();
    auto syntaxBytes = syntaxEncoder.finish();
    auto provenanceBytes = provenanceEncoder.finish();
    if (expectedOwners == zc::none) {
      expectedOwners = zc::mv(ownerBytes);
      expectedSyntax = zc::mv(syntaxBytes);
      expectedProvenance = zc::mv(provenanceBytes);
    } else {
      ZC_EXPECT(ZC_REQUIRE_NONNULL(expectedOwners).asPtr() == ownerBytes.asPtr());
      ZC_EXPECT(ZC_REQUIRE_NONNULL(expectedSyntax).asPtr() == syntaxBytes.asPtr());
      ZC_EXPECT(ZC_REQUIRE_NONNULL(expectedProvenance).asPtr() == provenanceBytes.asPtr());
    }
  }
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
