// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-session.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/binder/module-skeleton-query.h"
#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/module-graph-query.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/driver/named-item-query.h"
#include "zomlang/compiler/driver/owner-body-query.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/parser/parse-source-query.h"
#include "zomlang/tests/unittests/compiler/driver/canonical-mutation-test-helpers.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

namespace graph_query = module_graph_query;
namespace mutation = tests::canonical_mutation;
namespace resolution_query = incremental_module_resolution_query;
namespace source_query = identity::source_query;

struct AuthorityPayloadWire final {
  mutation::WireRange context;
  mutation::SequenceRange definitions;
  mutation::SequenceRange implementations;
  mutation::SequenceRange genericParameters;
  mutation::SequenceRange callableParameters;
  mutation::WireRange readiness;
};

AuthorityPayloadWire authorityPayloadWire(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr zc::StringPtr domain = "zom.query.input-transaction.contextual-identity-authority"_zc;
  size_t cursor = mutation::payloadOffset(bytes, domain);
  const auto context = mutation::consumeByteString(bytes, cursor);
  const auto definitions = mutation::consumeSequence(bytes, cursor, 2);
  const auto implementations = mutation::consumeSequence(bytes, cursor, 2);
  const auto genericParameters = mutation::consumeSequence(bytes, cursor, 2);
  const auto callableParameters = mutation::consumeSequence(bytes, cursor, 2);
  const auto readiness = mutation::consumeByteString(bytes, cursor);
  ZC_REQUIRE(cursor == bytes.size());
  return AuthorityPayloadWire{context,           definitions,        implementations,
                              genericParameters, callableParameters, readiness};
}

struct DependencyWitnessWire final {
  mutation::WireRange requester;
  mutation::WireRange request;
  mutation::WireRange dependency;
};

DependencyWitnessWire dependencyWitnessWire(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr zc::StringPtr domain = "zom.materialized-module-dependency-witness"_zc;
  size_t cursor = mutation::payloadOffset(bytes, domain);
  const auto requester = mutation::consumeByteString(bytes, cursor);
  const auto request = mutation::consumeByteString(bytes, cursor);
  const auto dependency = mutation::consumeByteString(bytes, cursor);
  ZC_REQUIRE(cursor == bytes.size());
  return DependencyWitnessWire{requester, request, dependency};
}

struct GraphWitnessWire final {
  mutation::WireRange context;
  mutation::WireRange fingerprint;
  mutation::WireRange graph;
  mutation::WireRange scc;
  mutation::SequenceRange edges;
  mutation::WireRange revision;
};

GraphWitnessWire graphWitnessWire(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr zc::StringPtr domain = "zom.materialized-module-graph-witness"_zc;
  size_t cursor = mutation::payloadOffset(bytes, domain);
  const auto context = mutation::consumeByteString(bytes, cursor);
  const auto fingerprint = mutation::WireRange{cursor, cursor + 32};
  ZC_REQUIRE(fingerprint.end <= bytes.size());
  cursor = fingerprint.end;
  const auto graph = mutation::consumeByteString(bytes, cursor);
  const auto scc = mutation::consumeByteString(bytes, cursor);
  const auto edges = mutation::consumeSequence(bytes, cursor);
  const auto revision = mutation::WireRange{cursor, cursor + 32};
  ZC_REQUIRE(revision.end == bytes.size());
  return GraphWitnessWire{context, fingerprint, graph, scc, edges, revision};
}

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

identity::PackageDependencyEdgeKey packageDependencyEdge() {
  auto result = identity::PackageDependencyEdgeKey::from(
      package(), scalar<identity::DependencyAlias>("dependency"_zc),
      identity::DependencyDomain::Target, package("dependency"_zc));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
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

identity::CrateKey coreCrate() {
  auto result = identity::CrateKey::from(
      identity::CompilationUnitIdentity::toolchain(identity::ToolchainUnitKey::core()),
      identity::CrateTargetKind::Library, scalar<identity::TargetName>("core"_zc), compilation());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CrateDependencyEdgeKey crateDependencyEdge() {
  auto result = identity::CrateDependencyEdgeKey::from(
      identity::CrateDependencyOrigin::userPackage(packageDependencyEdge()), crate(),
      crate("dependency"_zc));
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

source_query::StableSourceQueryKey stableSource(zc::StringPtr packageName = "authority"_zc) {
  auto value = namedSource("root.zom"_zc, packageName);
  auto result = source_query::StableSourceQueryKey::fromVerified(value);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

source_query::CanonicalSourceSnapshot sourceSnapshot(zc::StringPtr text,
                                                     zc::StringPtr packageName = "authority"_zc) {
  auto immutable = identity::ImmutableSourceSnapshot::from(namedSource("root.zom"_zc, packageName),
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

package::RegisteredTargetProfileName profileName() {
  auto result = package::RegisteredTargetProfileName::from("authority-test"_zc);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none-unknown"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(ZC_REQUIRE_NONNULL(specification)));
  auto profile = ir::RegisteredTargetProfileRecord::from(
      profileName(), target(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  profiles.add(zc::mv(ZC_REQUIRE_NONNULL(profile)));
  auto registry = ir::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selected =
      ZC_REQUIRE_NONNULL(service).select(zc::none, package::PackagePanicStrategy::Unwind);
  return zc::mv(ZC_REQUIRE_NONNULL(selected));
}

package::VerifiedPackageCompilationRequest packageRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>("root.zom"_zc));
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      package(), identity::CrateTargetKind::Library, scalar<identity::TargetName>("authority"_zc),
      2026, false, identity::CanonicalRelativePath::from(zc::mv(path))));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{true, false, false}, package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

CompilationRootSetQueryKey packageRoots() {
  zc::Vector<CompilationRootKey> roots;
  auto root = CompilationRootKey::userPackage(package());
  auto core = CompilationRootKey::toolchainCore(coreCrate());
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(root)));
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(core)));
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

bool encodedLess(zc::ArrayPtr<const uint8_t> left, zc::ArrayPtr<const uint8_t> right) {
  const size_t common = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < common; ++index) {
    if (left[index] < right[index]) { return true; }
    if (left[index] > right[index]) { return false; }
  }
  return left.size() < right.size();
}

void encodeByteStringSequence(identity::CanonicalEncoder& encoder,
                              zc::Vector<zc::Array<uint8_t>>&& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && encodedLess(current.asPtr(), values[insertion - 1].asPtr())) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
  encoder.encodeSequenceSize(values.size());
  for (const auto& value : values) { encoder.encodeByteString(value.asPtr()); }
}

CanonicalPackageGraph packageGraph(bool includeDependency = false) {
  identity::CanonicalEncoder encoder;
  zc::Vector<zc::Array<uint8_t>> packages;
  packages.add(package().encode());
  zc::Vector<zc::Array<uint8_t>> resolvedPackageEdges;
  zc::Vector<zc::Array<uint8_t>> selectedPackageEdges;
  zc::Vector<zc::Array<uint8_t>> crates;
  crates.add(crate().encode());
  zc::Vector<zc::Array<uint8_t>> crateEdges;
  if (includeDependency) {
    packages.add(package("dependency"_zc).encode());
    resolvedPackageEdges.add(packageDependencyEdge().encode());
    selectedPackageEdges.add(packageDependencyEdge().encode());
    crates.add(crate("dependency"_zc).encode());
    crateEdges.add(crateDependencyEdge().encode());
  }
  encodeByteStringSequence(encoder, zc::mv(packages));
  encodeByteStringSequence(encoder, zc::mv(resolvedPackageEdges));
  encodeByteStringSequence(encoder, zc::mv(selectedPackageEdges));
  encodeByteStringSequence(encoder, zc::mv(crates));
  encodeByteStringSequence(encoder, zc::mv(crateEdges));
  auto result = PackageGraphInput::decodeValue(encoder.finish().asPtr());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

binder::StableDefinitionQueryKey stableDefinition(const identity::ModuleKey& module,
                                                  const identity::DefinitionKey& definition) {
  return binder::StableDefinitionQueryKey::from(module.clone(), definition.clone());
}

bool containsStableDefinition(zc::ArrayPtr<const binder::StableDefinitionQueryKey> entries,
                              const binder::StableDefinitionQueryKey& expected) {
  for (const auto& entry : entries) {
    if (entry == expected) { return true; }
  }
  return false;
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

identity::ModuleResolutionPolicyKey resolutionPolicy() {
  auto result = identity::ModuleResolutionPolicyKey::from(
      identity::UnicodeNormalizationPolicy::Nfc, identity::CaseComparisonPolicy::CaseSensitive,
      identity::SymlinkHandlingPolicy::ResolveThenConfine,
      identity::ModuleContainmentPolicy::DeclaredRootsOnly,
      identity::LocalModuleLookupPolicy::RequesterAncestryAndCrateRoot,
      identity::DependencyAliasLookupPolicy::ExactFirstSegment,
      identity::PreludeLookupPolicy::ConfiguredCratePrelude,
      identity::ModuleCandidateSelectionPolicy::AllDistinctMatchesNoPrecedence);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalSourceSet activeSources(zc::StringPtr packageName = "authority"_zc) {
  zc::Vector<source_query::StableSourceQueryKey> values;
  values.add(stableSource(packageName));
  auto result = CanonicalSourceSet::from(zc::mv(values));
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

void stageCoreGraphInputs(query::InputTransaction& transaction) {
  auto admitted = core_library_test::admittedCoreDistribution();
  auto distribution = source::core::CoreDistributionInputRecord::from(
      admitted.record().clone(), admitted.distributionDigest(), admitted.policyTemplate().clone());
  ZC_REQUIRE(distribution != zc::none);
  auto core = coreCrate();
  const zc::StringPtr moduleNames[] = {"core"_zc, "marker"_zc, "prelude"_zc};
  zc::Vector<graph_query::SelectedModuleRecord> catalogEntries;
  for (size_t index = 0; index < admitted.snapshots().size(); ++index) {
    zc::Vector<identity::ModulePathSegment> path;
    path.add(scalar<identity::ModulePathSegment>("core"_zc));
    if (index != 0) { path.add(scalar<identity::ModulePathSegment>(moduleNames[index])); }
    auto module = identity::ModuleKey::from(core.clone(), zc::mv(path));
    ZC_REQUIRE(module != zc::none);
    auto sourceKey = identity::SourceFileKey::from(
        core.clone(),
        identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                            admitted.snapshots()[index].path().clone()));
    auto immutable = identity::ImmutableSourceSnapshot::from(
        sourceKey.clone(), zc::heapArray<uint8_t>(admitted.snapshots()[index].bytes()));
    auto stableSource = source_query::StableSourceQueryKey::fromVerified(sourceKey);
    auto snapshot =
        source_query::CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(immutable));
    ZC_REQUIRE(stableSource != zc::none);
    ZC_REQUIRE(snapshot != zc::none);
    zc::Vector<graph_query::DetachedModuleDependencySite> noSites;
    auto sites = graph_query::DetachedModuleDependencySiteSet::from(
        ZC_REQUIRE_NONNULL(module).clone(), sourceKey.clone(),
        ZC_REQUIRE_NONNULL(immutable).contentDigest(), zc::mv(noSites));
    ZC_REQUIRE(sites != zc::none);
    auto ancestry = requesterAncestry(ZC_REQUIRE_NONNULL(module).clone());
    ZC_REQUIRE(transaction
                   .set<source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(stableSource),
                                                           ZC_REQUIRE_NONNULL(snapshot))
                   .isApplied());
    ZC_REQUIRE(transaction
                   .set<graph_query::ModuleDependencySiteInput>(ZC_REQUIRE_NONNULL(module),
                                                                ZC_REQUIRE_NONNULL(sites))
                   .isApplied());
    ZC_REQUIRE(transaction
                   .set<resolution_query::RequesterModuleAncestryInput>(ZC_REQUIRE_NONNULL(module),
                                                                        ancestry)
                   .isApplied());
    catalogEntries.add(
        graph_query::SelectedModuleRecord(zc::mv(ZC_REQUIRE_NONNULL(module)), zc::mv(sourceKey)));
  }
  auto catalog = graph_query::SelectedModuleCatalog::from(core.clone(), zc::mv(catalogEntries));
  ZC_REQUIRE(catalog != zc::none);
  auto options = compilationOptions();
  ZC_REQUIRE(transaction
                 .set<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                                                 ZC_REQUIRE_NONNULL(distribution))
                 .isApplied());
  ZC_REQUIRE(
      transaction.set<graph_query::SelectedModuleCatalogInput>(core, ZC_REQUIRE_NONNULL(catalog))
          .isApplied());
  ZC_REQUIRE(transaction
                 .set<resolution_query::ConfiguredPreludeInput>(
                     core, resolution_query::ExplicitModuleTarget::absent())
                 .isApplied());
  ZC_REQUIRE(transaction.set<source_query::CompilationOptionsInput>(core, options).isApplied());
}

bool commitAuthority(ContextualIdentityAuthorityInputLedger& ledger, query::QueryDatabase& database,
                     const CompilationRootSetQueryKey& roots) {
  auto staging = database.snapshot();
  auto prepared = ContextualIdentityAuthorityInputTransaction::prepare(staging, staging.revision(),
                                                                       roots, ledger);
  if (prepared == zc::none) { return false; }
  auto result = ZC_ASSERT_NONNULL(prepared).commit(database);
  if (!result.isCommitted()) { return false; }
  ledger = zc::mv(ZC_ASSERT_NONNULL(prepared)).takeNextLedger();
  return true;
}

void stageBaseInputs(ContextualIdentityAuthorityInputLedger& state, query::QueryDatabase& database,
                     zc::StringPtr text, zc::StringPtr selectedModulePackage = "authority"_zc,
                     bool includeDependency = false) {
  auto pending = state.beginBaseMutation(database);
  ZC_REQUIRE(pending != zc::none);
  ZC_IF_SOME(transaction, pending) {
    stageCoreGraphInputs(transaction);
    auto crateKey = stableCrate();
    auto sourceKey = stableSource();
    auto graphRoots = packageGraphRoots();
    auto graph = packageGraph(includeDependency);
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
    if (includeDependency) {
      const zc::StringPtr dependencyText = "module root;\nclass Dependency {}\n"_zc;
      auto dependencyCrate = crate("dependency"_zc);
      auto dependencySource = namedSource("root.zom"_zc, "dependency"_zc);
      auto dependencyModule = semanticModule("dependency"_zc);
      zc::Vector<graph_query::SelectedModuleRecord> dependencyEntries;
      dependencyEntries.add(
          graph_query::SelectedModuleRecord(dependencyModule.clone(), dependencySource.clone()));
      auto dependencyCatalog = graph_query::SelectedModuleCatalog::from(dependencyCrate.clone(),
                                                                        zc::mv(dependencyEntries));
      auto dependencySiteSet =
          dependencySites(dependencyModule.clone(), dependencySource.clone(), dependencyText);
      auto dependencyAncestry = requesterAncestry(dependencyModule.clone());
      auto dependencySnapshot = sourceSnapshot(dependencyText, "dependency"_zc);
      ZC_REQUIRE(dependencyCatalog != zc::none);
      ZC_REQUIRE(transaction
                     .set<graph_query::SelectedModuleCatalogInput>(
                         dependencyCrate, ZC_REQUIRE_NONNULL(dependencyCatalog))
                     .isApplied());
      ZC_REQUIRE(transaction
                     .set<UserPackageActiveSourcesInput>(stableCrate("dependency"_zc),
                                                         activeSources("dependency"_zc))
                     .isApplied());
      ZC_REQUIRE(
          transaction
              .set<graph_query::ModuleDependencySiteInput>(dependencyModule, dependencySiteSet)
              .isApplied());
      ZC_REQUIRE(transaction
                     .set<resolution_query::RequesterModuleAncestryInput>(dependencyModule,
                                                                          dependencyAncestry)
                     .isApplied());
      ZC_REQUIRE(transaction
                     .set<resolution_query::ConfiguredPreludeInput>(
                         dependencyCrate, resolution_query::ExplicitModuleTarget::absent())
                     .isApplied());
      ZC_REQUIRE(transaction
                     .set<source_query::SourceSnapshotInput>(stableSource("dependency"_zc),
                                                             dependencySnapshot)
                     .isApplied());
      ZC_REQUIRE(
          transaction.set<source_query::CompilationOptionsInput>(crate("dependency"_zc), options)
              .isApplied());
    }
    ZC_REQUIRE(transaction.commit().isCommitted());
  }
}

basic::ThreadPool& scheduler() {
  static basic::ThreadPool value(4);
  return value;
}

class QueryTestSemanticContextResources final
    : public graph_query::ModuleGraphIdentityMaterializationResources {
public:
  QueryTestSemanticContextResources()
      : context(issueContext(factory)), interners(createInterners(factory, context)) {}

  identity::SemanticContextBrand semanticContext() const noexcept override { return context; }

  identity::IdentityInternResult<identity::CompilationUnitId> internCompilationUnit(
      identity::SemanticContextBrand requested,
      const identity::CompilationUnitIdentity& key) const override {
    return interners.internCompilationUnit(requested, key);
  }

  identity::IdentityInternResult<identity::CrateId> internCrate(
      identity::SemanticContextBrand requested, const identity::CrateKey& key) const override {
    return interners.internCrate(requested, key);
  }

  identity::IdentityInternResult<identity::SourceFileId> internSourceFile(
      identity::SemanticContextBrand requested, const identity::SourceFileKey& key) const override {
    return interners.internSourceFile(requested, key);
  }

  identity::IdentityInternResult<identity::ModuleId> internModule(
      identity::SemanticContextBrand requested, const identity::ModuleKey& key) const override {
    return interners.internModule(requested, key);
  }

  zc::Maybe<identity::CompilationUnitIdentityEntry> compilationUnit(
      identity::CompilationUnitId handle) const override {
    return interners.compilationUnit(handle);
  }

  zc::Maybe<identity::CrateIdentityEntry> crate(identity::CrateId handle) const override {
    return interners.crate(handle);
  }

  zc::Maybe<identity::SourceFileIdentityEntry> sourceFile(
      identity::SourceFileId handle) const override {
    return interners.sourceFile(handle);
  }

  zc::Maybe<identity::ModuleIdentityEntry> module(identity::ModuleId handle) const override {
    return interners.module(handle);
  }

private:
  static identity::SemanticContextBrand issueContext(identity::SemanticContextFactory& factory) {
    auto issued = factory.issue();
    ZC_IREQUIRE(issued != zc::none, "test materialization context allocation failed");
    return ZC_ASSERT_NONNULL(issued);
  }

  static identity::CanonicalIdentityInternerSet createInterners(
      identity::SemanticContextFactory& factory, identity::SemanticContextBrand context) {
    auto created = identity::CanonicalIdentityInternerSet::create(factory, context);
    ZC_IREQUIRE(created != zc::none, "test materialization interner allocation failed");
    return zc::mv(ZC_ASSERT_NONNULL(created));
  }

  identity::SemanticContextFactory factory;
  identity::SemanticContextBrand context;
  mutable identity::CanonicalIdentityInternerSet interners;
};

query::QueryDatabase queryDatabase(basic::ThreadPool& queryScheduler) {
  auto resources = zc::heap<QueryTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  query::QueryDatabase result(queryScheduler, query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
  ZC_REQUIRE(graph_query::registerModuleGraphQueries(result));
  ZC_REQUIRE(graph_query::registerStableModuleGraphQueries(result));
  ZC_REQUIRE(core_library_query::registerCoreLibraryQueryProvider(result));
  return result;
}

using FinalQuerySnapshot =
    query::SealedQuerySnapshot<CompilationRootSetQueryKey, identity::Sha256Digest>;

graph_query::CompleteCompilationContextAuthority completeContextAuthority(
    bool includeDependency = false) {
  auto registry = targetRegistry();
  auto request = packageRequest(registry);
  auto rootSet = PackageRootSetKey::fromVerified(request);
  auto graph = packageGraph(includeDependency);
  auto distribution = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(rootSet != zc::none);
  ZC_REQUIRE(distribution != zc::none);

  auto user = crate();
  auto core = coreCrate();
  auto canonicalOptions = source_query::CanonicalCompilationOptions::fromVerified(request);
  ZC_REQUIRE(canonicalOptions != zc::none);
  auto options = zc::mv(ZC_REQUIRE_NONNULL(canonicalOptions));
  zc::Vector<identity::CrateKey> userRoots;
  userRoots.add(user.clone());
  zc::Vector<identity::CrateKey> coreRoots;
  coreRoots.add(core.clone());
  zc::Vector<graph_query::CompilationOptionsEntry> optionEntries;
  optionEntries.add(graph_query::CompilationOptionsEntry::from(user.clone(), options.clone()));
  if (includeDependency) {
    optionEntries.add(
        graph_query::CompilationOptionsEntry::from(crate("dependency"_zc), options.clone()));
  }
  optionEntries.add(graph_query::CompilationOptionsEntry::from(core.clone(), options.clone()));

  zc::Vector<binder::ModuleSearchRoot> userSearchRootValues;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  userSearchRootValues.add(binder::ModuleSearchRoot::workspace(
      user.clone(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  auto userSearchRoots = resolution_query::CanonicalModuleSearchRoots::fromVerified(
      user, userSearchRootValues.asPtr());
  auto coreSearchRoot = binder::ModuleSearchRoot::toolchainCore(
      core.clone(), ZC_REQUIRE_NONNULL(distribution).digest());
  ZC_REQUIRE(userSearchRoots != zc::none);
  ZC_REQUIRE(coreSearchRoot != zc::none);
  zc::Vector<binder::ModuleSearchRoot> coreSearchRootValues;
  coreSearchRootValues.add(zc::mv(ZC_REQUIRE_NONNULL(coreSearchRoot)));
  auto coreSearchRoots = resolution_query::CanonicalModuleSearchRoots::fromVerified(
      core, coreSearchRootValues.asPtr());
  ZC_REQUIRE(coreSearchRoots != zc::none);
  zc::Vector<graph_query::ModuleSearchRootsEntry> searchEntries;
  searchEntries.add(graph_query::ModuleSearchRootsEntry::from(
      user.clone(), zc::mv(ZC_REQUIRE_NONNULL(userSearchRoots))));
  if (includeDependency) {
    auto dependency = crate("dependency"_zc);
    zc::Vector<binder::ModuleSearchRoot> dependencySearchRootValues;
    zc::Vector<identity::CanonicalPathSegment> dependencyPath;
    dependencyPath.add(scalar<identity::CanonicalPathSegment>("dependency"_zc));
    dependencySearchRootValues.add(binder::ModuleSearchRoot::workspace(
        dependency.clone(),
        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(dependencyPath))));
    auto dependencySearchRoots = resolution_query::CanonicalModuleSearchRoots::fromVerified(
        dependency, dependencySearchRootValues.asPtr());
    ZC_REQUIRE(dependencySearchRoots != zc::none);
    searchEntries.add(graph_query::ModuleSearchRootsEntry::from(
        dependency.clone(), zc::mv(ZC_REQUIRE_NONNULL(dependencySearchRoots))));
  }
  searchEntries.add(graph_query::ModuleSearchRootsEntry::from(
      core.clone(), zc::mv(ZC_REQUIRE_NONNULL(coreSearchRoots))));

  const graph_query::CompleteCompilationContextSources sources{
      request,
      ZC_REQUIRE_NONNULL(rootSet),
      graph,
      userRoots.asPtr(),
      coreRoots.asPtr(),
      optionEntries.asPtr(),
      searchEntries.asPtr(),
      ZC_REQUIRE_NONNULL(distribution),
  };
  auto authority = graph_query::CompleteCompilationContextAuthority::fromVerified(sources);
  return zc::mv(ZC_REQUIRE_NONNULL(authority));
}

FinalQuerySnapshot sealDatabase(query::QueryDatabase& database,
                                const CompilationRootSetQueryKey& roots,
                                bool includeDependency = false) {
  auto authority = completeContextAuthority(includeDependency);
  ZC_REQUIRE(authority.contextRoots() == roots);
  auto distribution = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(distribution != zc::none);
  auto opened = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  auto authorityBytes = authority.encodeCanonical();
  auto coreWitness = graph_query::computeCanonicalInputPayloadDigest(
      "zom.query.input-transaction.core-distribution"_zc, authorityBytes.asPtr());
  auto structureWitness = graph_query::computeCanonicalInputPayloadDigest(
      "zom.query.input-transaction.module-structure"_zc, authorityBytes.asPtr());
  ZC_REQUIRE(coreWitness != zc::none);
  ZC_REQUIRE(structureWitness != zc::none);
  ZC_REQUIRE(transaction
                 .set<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                                                 ZC_REQUIRE_NONNULL(distribution))
                 .isApplied());
  ZC_REQUIRE(
      transaction.set<graph_query::CompleteCompilationContextAuthorityInput>(roots, authority)
          .isApplied());
  for (const auto& entry : authority.compilationOptions()) {
    ZC_REQUIRE(transaction.set<source_query::CompilationOptionsInput>(entry.key(), entry.value())
                   .isApplied());
  }
  for (const auto& entry : authority.moduleSearchRoots()) {
    ZC_REQUIRE(transaction.set<resolution_query::ModuleSearchRootsInput>(entry.key(), entry.value())
                   .isApplied());
  }
  ZC_REQUIRE(transaction
                 .set<graph_query::CoreDistributionTransactionWitnessInput>(
                     roots, ZC_REQUIRE_NONNULL(coreWitness))
                 .isApplied());
  ZC_REQUIRE(transaction
                 .set<graph_query::ModuleStructureTransactionWitnessInput>(
                     roots, ZC_REQUIRE_NONNULL(structureWitness))
                 .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto finalSnapshot = database.snapshot();
  auto witness = graph_query::computeFinalSnapshotWitness(finalSnapshot, roots);
  ZC_REQUIRE(witness != zc::none);
  auto seal = database.sealInputs<graph_query::CompleteCompilationContextAuthorityInput>(
      finalSnapshot, roots, ZC_REQUIRE_NONNULL(witness));
  ZC_REQUIRE(seal.isSealed());
  auto admitted =
      database.admitFinalSnapshot<graph_query::CompleteCompilationContextAuthorityInput>(
          database.snapshot(), seal.seal());
  ZC_REQUIRE(admitted.isAdmitted());
  return zc::mv(admitted).takeSnapshot();
}

graph_query::MaterializedModuleGraph materializedGraphFixture(bool includeDependency = false) {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc, "authority"_zc,
                  includeDependency);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots, includeDependency);
  auto materialized = sealed.getCapability<graph_query::MaterializeModuleGraphQuery>(roots);
  ZC_REQUIRE(materialized.isPublished());
  return materialized.lease().capability().clone();
}

ZC_TEST("MaterializedModuleGraphCapabilityTest.MaterializesNonRootDependencyPackage") {
  auto materialized = materializedGraphFixture(true);
  const auto dependencyBytes = package("dependency"_zc).encode();
  size_t dependencyUnits = 0;
  for (const auto& unit : materialized.units()) {
    if (unit.key().kind() == identity::CompilationUnitKind::UserPackage &&
        unit.key().userPackage().encode().asPtr() == dependencyBytes.asPtr()) {
      ++dependencyUnits;
    }
  }
  size_t dependencyRoots = 0;
  for (const auto& root : materialized.witness().contextRoots().roots()) {
    if (root.kind() == CompilationRootKind::UserPackage &&
        root.userPackage().canonicalPackageBytes() == dependencyBytes.asPtr()) {
      ++dependencyRoots;
    }
  }
  ZC_EXPECT(dependencyUnits == 1);
  ZC_EXPECT(dependencyRoots == 0);
}

template <typename Descriptor>
void expectExactHeaderReadSet(const query::QuerySnapshot& snapshot,
                              const typename Descriptor::Key& key, size_t definitionReads,
                              size_t implementationReads) {
  const auto moduleKey = stableModule();
  auto definitionFingerprint = snapshot.keyFingerprint<NamedDefinitionInventoryQuery>(moduleKey);
  auto implementationFingerprint =
      snapshot.keyFingerprint<NamedImplementationInventoryQuery>(moduleKey);
  auto selectedFingerprint =
      snapshot.keyFingerprint<graph_query::SelectedModuleSourceQuery>(semanticModule());
  auto parseFingerprint = snapshot.keyFingerprint<parser::ParseSourceQuery>(stableSource());
  auto definitionSitesFingerprint =
      snapshot.keyFingerprint<RevisionLocalDefinitionSitesQuery>(moduleKey);
  auto implementationSitesFingerprint =
      snapshot.keyFingerprint<RevisionLocalImplementationSitesQuery>(moduleKey);
  ZC_REQUIRE(definitionFingerprint != zc::none);
  ZC_REQUIRE(implementationFingerprint != zc::none);
  ZC_REQUIRE(selectedFingerprint != zc::none);
  ZC_REQUIRE(parseFingerprint != zc::none);
  ZC_REQUIRE(definitionSitesFingerprint != zc::none);
  ZC_REQUIRE(implementationSitesFingerprint != zc::none);

  size_t observedDefinitionReads = 0;
  size_t observedImplementationReads = 0;
  size_t observedSelectedReads = 0;
  size_t observedParseReads = 0;
  size_t observedDefinitionSiteReads = 0;
  size_t observedImplementationSiteReads = 0;
  const auto dependencies = snapshot.dependencies<Descriptor>(key);
  for (const auto& group : dependencies) {
    ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
    ZC_REQUIRE(group.dependencies().size() == 1);
    const auto& fingerprint = group.dependencies()[0].key().fingerprint();
    if (fingerprint == ZC_REQUIRE_NONNULL(definitionFingerprint)) {
      ++observedDefinitionReads;
    } else if (fingerprint == ZC_REQUIRE_NONNULL(implementationFingerprint)) {
      ++observedImplementationReads;
    } else if (fingerprint == ZC_REQUIRE_NONNULL(selectedFingerprint)) {
      ++observedSelectedReads;
    } else if (fingerprint == ZC_REQUIRE_NONNULL(parseFingerprint)) {
      ++observedParseReads;
    } else if (fingerprint == ZC_REQUIRE_NONNULL(definitionSitesFingerprint)) {
      ++observedDefinitionSiteReads;
    } else if (fingerprint == ZC_REQUIRE_NONNULL(implementationSitesFingerprint)) {
      ++observedImplementationSiteReads;
    } else {
      ZC_FAIL_REQUIRE("Header query recorded an undeclared dependency");
    }
  }
  ZC_EXPECT(observedDefinitionReads == definitionReads);
  ZC_EXPECT(observedImplementationReads == implementationReads);
  ZC_EXPECT(observedSelectedReads == 2);
  ZC_EXPECT(observedParseReads == 2);
  ZC_EXPECT(observedDefinitionSiteReads == 2);
  ZC_EXPECT(observedImplementationSiteReads == 2);
  ZC_EXPECT(dependencies.size() == definitionReads + implementationReads + 8);
}

}  // namespace

ZC_TEST("Contextual identity authority transaction commits complete readiness atomically") {
  ZC_EXPECT(NamedItemProvenanceQuery::descriptor.retention == query::RetentionClass::Retained);
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicateProvenance = database.registerDescriptor<NamedItemProvenanceQuery>();
  ZC_EXPECT(!duplicateProvenance.isRegistered());
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto module = stableModule();
  auto readyKey = roots.clone();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  auto staged = database.snapshot();
  ZC_EXPECT(staged.probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_REQUIRE(commitAuthority(state, database, roots));

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
            firstInventory.value().entries()[0].record().encode().asPtr());
  ZC_REQUIRE(state.definitionKeys().size() == 3);
  ZC_EXPECT(containsStableDefinition(state.definitionKeys(),
                                     stableDefinition(semanticModule(), alphaKey)));
  auto firstAuthorityMetadata = first.metadata<ActiveDefinitionAuthorityInput>(alphaQueryKey);
  ZC_REQUIRE(firstAuthorityMetadata != zc::none);
}

ZC_TEST("Contextual identity authority transaction rejects stale and malformed staging") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  auto staging = database.snapshot();
  ZC_EXPECT(ContextualIdentityAuthorityInputTransaction::prepare(staging, query::DatabaseRevision(),
                                                                 roots, state) == zc::none);

  auto prepared = ContextualIdentityAuthorityInputTransaction::prepare(staging, staging.revision(),
                                                                       roots, state);
  ZC_REQUIRE(prepared != zc::none);
  auto advance = database.beginInputTransaction(staging.revision());
  ZC_REQUIRE(advance.isOpened());
  auto transaction = zc::mv(advance).takeTransaction();
  ZC_REQUIRE(transaction
                 .set<source_query::SourceSnapshotInput>(
                     stableSource(), sourceSnapshot("module root;\nclass Beta {}\n"_zc))
                 .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());
  auto staleResult = ZC_REQUIRE_NONNULL(prepared).commit(database);
  ZC_REQUIRE(!staleResult.isCommitted());
  ZC_EXPECT(staleResult.failure() == query::InputTransactionFailure::StaleBaseRevision);
  ZC_EXPECT(database.snapshot().probeInput<ActiveDefinitionAuthorityReadyInput>(roots).kind() ==
            query::QueryValueKind::Absence);

  auto malformedDatabase = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(malformedDatabase));
  ContextualIdentityAuthorityInputLedger malformedState;
  stageBaseInputs(malformedState, malformedDatabase, "module root;\nclass Alpha {\n"_zc);
  auto malformed = malformedDatabase.snapshot();
  ZC_EXPECT(ContextualIdentityAuthorityInputTransaction::prepare(
                malformed, malformed.revision(), roots, malformedState) == zc::none);
  ZC_EXPECT(
      malformedDatabase.snapshot().probeInput<ActiveDefinitionAuthorityReadyInput>(roots).kind() ==
      query::QueryValueKind::Absence);
}

ZC_TEST("SessionInputTransactionTest.IdentityPayloadRejectsAuthorityAndReadinessMutations") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha {}\n"
                  "class Gamma {}\n"
                  "impl Trait for Alpha {}\n"
                  "fun Beta<T>(value: T) -> T { return value; }\n"_zc);
  auto staging = database.snapshot();
  auto prepared = ContextualIdentityAuthorityInputTransaction::prepare(staging, staging.revision(),
                                                                       roots, state);
  ZC_REQUIRE(prepared != zc::none);
  const auto& payload = ZC_REQUIRE_NONNULL(prepared).payload();
  ZC_EXPECT(ContextualIdentityAuthorityInputVerifier::verify(staging, payload));

  auto encoded = payload.encodeCanonical();
  auto decoded = ContextualIdentityAuthorityInputPayload::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == payload);
  const auto wire = authorityPayloadWire(encoded.asPtr());
  ZC_REQUIRE(wire.definitions.count >= 2);
  ZC_REQUIRE(wire.implementations.count != 0);
  ZC_REQUIRE(wire.genericParameters.count != 0);
  ZC_REQUIRE(wire.callableParameters.count != 0);
  const auto expectRejected = [&](zc::Array<uint8_t>&& bytes) {
    auto candidate = ContextualIdentityAuthorityInputPayload::decodeCanonical(bytes.asPtr());
    if (candidate != zc::none) {
      ZC_EXPECT(!ContextualIdentityAuthorityInputVerifier::verify(staging,
                                                                  ZC_REQUIRE_NONNULL(candidate)));
    }
  };

  auto wrongDomain = mutation::flipByte(encoded.asPtr(), 0);
  ZC_EXPECT(ContextualIdentityAuthorityInputPayload::decodeCanonical(wrongDomain.asPtr()) ==
            zc::none);

  expectRejected(mutation::flipPayloadByte(encoded.asPtr(), wire.context));
  const mutation::SequenceRange authorityFields[] = {
      wire.definitions, wire.implementations, wire.genericParameters, wire.callableParameters};
  for (const auto& sequence : authorityFields) {
    expectRejected(mutation::flipPayloadByte(
        encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), sequence, 0, 0)));
    expectRejected(mutation::flipPayloadByte(
        encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), sequence, 0, 1)));
  }
  expectRejected(mutation::flipPayloadByte(encoded.asPtr(), wire.readiness));

  auto duplicate = mutation::duplicateFirstElement(encoded.asPtr(), wire.definitions);
  ZC_EXPECT(ContextualIdentityAuthorityInputPayload::decodeCanonical(duplicate.asPtr()) ==
            zc::none);
  auto reordered = mutation::swapFirstTwoElements(encoded.asPtr(), wire.definitions);
  ZC_EXPECT(ContextualIdentityAuthorityInputPayload::decodeCanonical(reordered.asPtr()) ==
            zc::none);
  expectRejected(mutation::removeFirstElement(encoded.asPtr(), wire.definitions));
  auto excessiveCount = mutation::setSequenceCount(encoded.asPtr(), wire.definitions, UINT64_MAX);
  ZC_EXPECT(ContextualIdentityAuthorityInputPayload::decodeCanonical(excessiveCount.asPtr()) ==
            zc::none);
  auto excessiveBytes = mutation::setByteStringSize(encoded.asPtr(), wire.context, UINT64_MAX);
  ZC_EXPECT(ContextualIdentityAuthorityInputPayload::decodeCanonical(excessiveBytes.asPtr()) ==
            zc::none);

  auto trailing = mutation::withTrailingByte(encoded.asPtr());
  ZC_EXPECT(ContextualIdentityAuthorityInputPayload::decodeCanonical(trailing.asPtr()) == zc::none);
  ZC_EXPECT(ContextualIdentityAuthorityInputPayload::decodeCanonical(
                encoded.asPtr().slice(0, encoded.size() - 1)) == zc::none);
}

ZC_TEST("Active definition authority session rejects modules outside their active crate") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto readyKey = roots.clone();

  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc, "foreign"_zc);
  ZC_EXPECT(!commitAuthority(state, database, roots));
  ZC_EXPECT(database.snapshot().probeInput<ActiveDefinitionAuthorityReadyInput>(readyKey).kind() ==
            query::QueryValueKind::Absence);
  ZC_EXPECT(state.definitionKeys().size() == 0);
}

ZC_TEST("Named identity inventory admits only the reserved core root mismatch") {
  auto coreDatabase = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(coreDatabase));
  ContextualIdentityAuthorityInputLedger coreState;
  stageBaseInputs(coreState, coreDatabase, "module core;\nfun Alpha() {}\n"_zc);
  auto coreSnapshot = coreDatabase.snapshot();
  auto coreInventory = coreSnapshot.get<NamedDefinitionInventoryQuery>(stableModule());
  ZC_REQUIRE(coreInventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(coreInventory.value().entries().size() == 1);

  auto mismatchDatabase = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(mismatchDatabase));
  ContextualIdentityAuthorityInputLedger mismatchState;
  stageBaseInputs(mismatchState, mismatchDatabase, "module wrong;\nfun Alpha() {}\n"_zc);
  auto mismatchSnapshot = mismatchDatabase.snapshot();
  auto mismatchInventory = mismatchSnapshot.get<NamedDefinitionInventoryQuery>(stableModule());
  ZC_REQUIRE(mismatchInventory.isRuntimeFailure());
  ZC_EXPECT(mismatchInventory.runtimeFailure() == query::QueryRuntimeFailure::ProviderRejected);
}

ZC_TEST("Owner body projection requires final admission") {
  ZC_EXPECT(OwnerBodyProvenanceQuery::descriptor.retention == query::RetentionClass::Retained);
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicateProvenance = database.registerDescriptor<OwnerBodyProvenanceQuery>();
  ZC_EXPECT(!duplicateProvenance.isRegistered());
  ContextualIdentityAuthorityInputLedger state;
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
  ZC_REQUIRE(commitAuthority(state, database, roots));

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
    ContextualIdentityAuthorityInputLedger state;
    auto roots = packageRoots();
    stageBaseInputs(state, database,
                    "module root;\nlet module_value = 0;\n"
                    "fun Alpha() { let value = 0; }\n"
                    "fun Beta() { let value = 1; }\n"_zc);
    ZC_REQUIRE(commitAuthority(state, database, roots));
    auto snapshot = database.snapshot();
    auto moduleQueryKey = contextual(roots, semanticModule());
    auto owners = snapshot.get<ModuleBodyOwnersQuery>(moduleQueryKey);
    ZC_REQUIRE(owners.isRuntimeFailure());
    ZC_EXPECT(owners.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  }
}

ZC_TEST("MaterializedModuleGraphCapabilityTest.DependencyWitnessRejectsEdgeMutations") {
  auto requester = semanticModule();
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  zc::Maybe<zc::Vector<identity::ModulePathSegment>> retainedPath(zc::mv(path));
  zc::Maybe<identity::DependencyAlias> noAlias;
  auto request = identity::ModuleResolutionKey::from(
      requester.clone(), identity::ModuleDependencyKind::Import, zc::mv(retainedPath),
      zc::mv(noAlias), resolutionPolicy());
  ZC_REQUIRE(request != zc::none);
  auto witness = graph_query::StableMaterializedDependencyWitness::from(
      requester.clone(), ZC_REQUIRE_NONNULL(request).clone(), requester.clone());
  ZC_REQUIRE(witness != zc::none);
  auto decoded = graph_query::StableMaterializedDependencyWitness::decodeCanonical(
      ZC_REQUIRE_NONNULL(witness).encodeCanonical().asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == ZC_REQUIRE_NONNULL(witness));

  auto wrongRequester = namedSemanticModule("other"_zc);
  ZC_EXPECT(graph_query::StableMaterializedDependencyWitness::from(
                zc::mv(wrongRequester), ZC_REQUIRE_NONNULL(request).clone(), requester.clone()) ==
            zc::none);

  auto encoded = ZC_REQUIRE_NONNULL(witness).encodeCanonical();
  const auto wire = dependencyWitnessWire(encoded.asPtr());
  const auto expectRejectedOrChanged = [&](zc::Array<uint8_t>&& bytes) {
    auto candidate =
        graph_query::StableMaterializedDependencyWitness::decodeCanonical(bytes.asPtr());
    if (candidate != zc::none) {
      ZC_EXPECT(!(ZC_REQUIRE_NONNULL(candidate) == ZC_REQUIRE_NONNULL(witness)));
    }
  };
  auto wrongDomain = mutation::flipByte(encoded.asPtr(), 0);
  ZC_EXPECT(graph_query::StableMaterializedDependencyWitness::decodeCanonical(
                wrongDomain.asPtr()) == zc::none);
  expectRejectedOrChanged(mutation::flipPayloadByte(encoded.asPtr(), wire.requester));
  expectRejectedOrChanged(mutation::flipPayloadByte(encoded.asPtr(), wire.request));
  expectRejectedOrChanged(mutation::flipPayloadByte(encoded.asPtr(), wire.dependency));
  auto reordered = mutation::swapAdjacentRanges(encoded.asPtr(), wire.request, wire.dependency);
  ZC_EXPECT(graph_query::StableMaterializedDependencyWitness::decodeCanonical(reordered.asPtr()) ==
            zc::none);
  auto excessiveRequester =
      mutation::setByteStringSize(encoded.asPtr(), wire.requester, UINT64_MAX);
  ZC_EXPECT(graph_query::StableMaterializedDependencyWitness::decodeCanonical(
                excessiveRequester.asPtr()) == zc::none);
  auto excessiveRequest = mutation::setByteStringSize(encoded.asPtr(), wire.request, UINT64_MAX);
  ZC_EXPECT(graph_query::StableMaterializedDependencyWitness::decodeCanonical(
                excessiveRequest.asPtr()) == zc::none);

  auto trailing = mutation::withTrailingByte(encoded.asPtr());
  ZC_EXPECT(graph_query::StableMaterializedDependencyWitness::decodeCanonical(trailing.asPtr()) ==
            zc::none);
  ZC_EXPECT(graph_query::StableMaterializedDependencyWitness::decodeCanonical(
                encoded.asPtr().slice(0, encoded.size() - 1)) == zc::none);
}

ZC_TEST("MaterializedModuleGraphCapabilityTest.GraphWitnessRejectsClosureAndRevisionMutations") {
  auto materialized = materializedGraphFixture();
  const auto& witness = materialized.witness();
  auto encoded = witness.encodeCanonical();
  auto decoded = graph_query::MaterializedModuleGraphWitness::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).sameAs(witness));
  const auto wire = graphWitnessWire(encoded.asPtr());

  auto changedContext = mutation::flipPayloadByte(encoded.asPtr(), wire.context);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(changedContext.asPtr()) ==
            zc::none);
  auto changedFingerprint = mutation::flipByte(encoded.asPtr(), wire.fingerprint.begin);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(
                changedFingerprint.asPtr()) == zc::none);
  auto changedGraph = mutation::flipPayloadByte(encoded.asPtr(), wire.graph);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(changedGraph.asPtr()) ==
            zc::none);
  auto changedScc = mutation::flipPayloadByte(encoded.asPtr(), wire.scc);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(changedScc.asPtr()) ==
            zc::none);
  auto changedRevision = mutation::flipByte(encoded.asPtr(), wire.revision.begin);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(changedRevision.asPtr()) ==
            zc::none);
  auto reordered = mutation::swapAdjacentRanges(encoded.asPtr(), wire.graph, wire.scc);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(reordered.asPtr()) ==
            zc::none);
  auto excessiveCount = mutation::setSequenceCount(encoded.asPtr(), wire.edges, UINT64_MAX);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(excessiveCount.asPtr()) ==
            zc::none);
  auto excessiveBytes = mutation::setByteStringSize(encoded.asPtr(), wire.context, UINT64_MAX);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(excessiveBytes.asPtr()) ==
            zc::none);
  if (wire.edges.count != 0) {
    auto changedEdge = mutation::flipPayloadByte(
        encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.edges, 0, 0));
    ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(changedEdge.asPtr()) ==
              zc::none);
    auto duplicateEdge = mutation::duplicateFirstElement(encoded.asPtr(), wire.edges);
    ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(duplicateEdge.asPtr()) ==
              zc::none);
  }
  auto trailing = mutation::withTrailingByte(encoded.asPtr());
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(trailing.asPtr()) ==
            zc::none);
  ZC_EXPECT(graph_query::MaterializedModuleGraphWitness::decodeCanonical(
                encoded.asPtr().slice(0, encoded.size() - 1)) == zc::none);

  zc::Vector<graph_query::StableMaterializedDependencyWitness> edges;
  for (const auto& edge : witness.requestEdges()) { edges.add(edge.clone()); }
  auto wrongRevisionBytes = zc::heapArray<uint8_t>(32);
  wrongRevisionBytes.asPtr().fill(0xa5);
  auto wrongRevisionDigest = identity::Sha256Digest::fromBytes(wrongRevisionBytes.asPtr());
  ZC_REQUIRE(wrongRevisionDigest != zc::none);
  auto wrongRevision = graph_query::MaterializedModuleGraphWitness::from(
      witness.contextRoots().clone(),
      identity::SemanticContextFingerprint::fromCanonicalDigest(witness.fingerprint().digest()),
      witness.graph().clone(), witness.scc().clone(), zc::mv(edges),
      binder::ModuleGraphRevision::fromCanonicalDigest(ZC_REQUIRE_NONNULL(wrongRevisionDigest)));
  ZC_EXPECT(wrongRevision == zc::none);

  ZC_REQUIRE(witness.graph().modules().size() > 1);
  zc::Vector<identity::ModuleKey> incompleteModules;
  for (size_t index = 0; index + 1 < witness.graph().modules().size(); ++index) {
    incompleteModules.add(witness.graph().modules()[index].clone());
  }
  zc::Vector<graph_query::ModuleDependencyEdgeKey> noEdges;
  auto incompleteGraph =
      graph_query::ModuleGraphRecord::from(zc::mv(incompleteModules), zc::mv(noEdges));
  ZC_REQUIRE(incompleteGraph != zc::none);
  zc::Vector<graph_query::StableMaterializedDependencyWitness> incompleteWitnessEdges;
  auto wrongClosure = graph_query::MaterializedModuleGraphWitness::from(
      witness.contextRoots().clone(),
      identity::SemanticContextFingerprint::fromCanonicalDigest(witness.fingerprint().digest()),
      zc::mv(ZC_REQUIRE_NONNULL(incompleteGraph)), witness.scc().clone(),
      zc::mv(incompleteWitnessEdges),
      binder::ModuleGraphRevision::fromCanonicalDigest(witness.graphRevision().digest()));
  ZC_EXPECT(wrongClosure == zc::none);

  zc::Vector<graph_query::StableMaterializedDependencyWitness> changedEdges;
  if (witness.requestEdges().size() != 0) {
    const auto& first = witness.requestEdges()[0];
    auto changed = graph_query::StableMaterializedDependencyWitness::from(
        first.requester().clone(), first.request().clone(), first.requester().clone());
    ZC_REQUIRE(changed != zc::none);
    changedEdges.add(zc::mv(ZC_REQUIRE_NONNULL(changed)));
    for (size_t index = 1; index < witness.requestEdges().size(); ++index) {
      changedEdges.add(witness.requestEdges()[index].clone());
    }
  } else {
    ZC_REQUIRE(witness.graph().modules().size() >= 2);
    auto changedRequester = witness.graph().modules()[0].clone();
    zc::Vector<identity::ModulePathSegment> changedPath;
    for (const auto& segment : witness.graph().modules()[1].path()) {
      changedPath.add(segment.clone());
    }
    zc::Maybe<zc::Vector<identity::ModulePathSegment>> retainedPath(zc::mv(changedPath));
    zc::Maybe<identity::DependencyAlias> noAlias;
    auto changedRequest = identity::ModuleResolutionKey::from(
        changedRequester.clone(), identity::ModuleDependencyKind::Import, zc::mv(retainedPath),
        zc::mv(noAlias), resolutionPolicy());
    ZC_REQUIRE(changedRequest != zc::none);
    auto changed = graph_query::StableMaterializedDependencyWitness::from(
        zc::mv(changedRequester), zc::mv(ZC_REQUIRE_NONNULL(changedRequest)),
        witness.graph().modules()[1].clone());
    ZC_REQUIRE(changed != zc::none);
    changedEdges.add(zc::mv(ZC_REQUIRE_NONNULL(changed)));
  }
  auto wrongEdges = graph_query::MaterializedModuleGraphWitness::from(
      witness.contextRoots().clone(),
      identity::SemanticContextFingerprint::fromCanonicalDigest(witness.fingerprint().digest()),
      witness.graph().clone(), witness.scc().clone(), zc::mv(changedEdges),
      binder::ModuleGraphRevision::fromCanonicalDigest(witness.graphRevision().digest()));
  ZC_EXPECT(wrongEdges == zc::none);
}

ZC_TEST("MaterializedModuleGraphCapabilityTest.GraphRejectsWitnessAndMembershipMutations") {
  auto materialized = materializedGraphFixture();
  const auto cloneUnits = [&] {
    zc::Vector<graph_query::MaterializedCompilationUnitEntry> values;
    for (const auto& entry : materialized.units()) { values.add(entry.clone()); }
    return values;
  };
  const auto cloneCrates = [&] {
    zc::Vector<graph_query::MaterializedCrateEntry> values;
    for (const auto& entry : materialized.crates()) { values.add(entry.clone()); }
    return values;
  };
  const auto cloneSources = [&] {
    zc::Vector<graph_query::MaterializedSourceEntry> values;
    for (const auto& entry : materialized.sources()) { values.add(entry.clone()); }
    return values;
  };
  const auto cloneModules = [&] {
    zc::Vector<graph_query::MaterializedModuleEntry> values;
    for (const auto& entry : materialized.modules()) { values.add(entry.clone()); }
    return values;
  };
  const auto cloneEdges = [&] {
    zc::Vector<graph_query::MaterializedModuleDependencyEdge> values;
    for (const auto& edge : materialized.requestEdges()) { values.add(edge.clone()); }
    return values;
  };
  const auto expectRejected =
      [&](graph_query::MaterializedModuleGraphWitness&& witness,
          zc::Vector<graph_query::MaterializedCompilationUnitEntry>&& units,
          zc::Vector<graph_query::MaterializedCrateEntry>&& crates,
          zc::Vector<graph_query::MaterializedSourceEntry>&& sources,
          zc::Vector<graph_query::MaterializedModuleEntry>&& modules,
          zc::Vector<graph_query::MaterializedModuleDependencyEdge>&& edges) {
        auto rejected = graph_query::MaterializedModuleGraph::from(
            materialized.context(), materialized.revision(), zc::mv(witness), zc::mv(units),
            zc::mv(crates), zc::mv(sources), zc::mv(modules), zc::mv(edges));
        ZC_EXPECT(rejected == zc::none);
      };

  ZC_REQUIRE(materialized.units().size() >= 2);
  ZC_REQUIRE(materialized.crates().size() >= 2);
  ZC_REQUIRE(materialized.sources().size() >= 2);
  ZC_REQUIRE(materialized.modules().size() > 1);

  zc::Vector<graph_query::MaterializedModuleEntry> incompleteModules;
  for (size_t index = 0; index + 1 < materialized.modules().size(); ++index) {
    incompleteModules.add(materialized.modules()[index].clone());
  }
  expectRejected(materialized.witness().clone(), cloneUnits(), cloneCrates(), cloneSources(),
                 zc::mv(incompleteModules), cloneEdges());

  zc::Vector<CompilationRootKey> foreignRootValues;
  auto foreignUserRoot = CompilationRootKey::userPackage(package("foreign"_zc));
  auto retainedCoreRoot = CompilationRootKey::toolchainCore(coreCrate());
  ZC_REQUIRE(foreignUserRoot != zc::none);
  ZC_REQUIRE(retainedCoreRoot != zc::none);
  foreignRootValues.add(zc::mv(ZC_REQUIRE_NONNULL(foreignUserRoot)));
  foreignRootValues.add(zc::mv(ZC_REQUIRE_NONNULL(retainedCoreRoot)));
  auto foreignRoots = CompilationRootSetQueryKey::from(zc::mv(foreignRootValues));
  ZC_REQUIRE(foreignRoots != zc::none);
  zc::Vector<graph_query::StableMaterializedDependencyWitness> witnessEdges;
  for (const auto& edge : materialized.witness().requestEdges()) { witnessEdges.add(edge.clone()); }
  auto foreignWitness = graph_query::MaterializedModuleGraphWitness::from(
      zc::mv(ZC_REQUIRE_NONNULL(foreignRoots)),
      identity::SemanticContextFingerprint::fromCanonicalDigest(
          materialized.witness().fingerprint().digest()),
      materialized.witness().graph().clone(), materialized.witness().scc().clone(),
      zc::mv(witnessEdges),
      binder::ModuleGraphRevision::fromCanonicalDigest(
          materialized.witness().graphRevision().digest()));
  ZC_REQUIRE(foreignWitness != zc::none);
  expectRejected(zc::mv(ZC_REQUIRE_NONNULL(foreignWitness)), cloneUnits(), cloneCrates(),
                 cloneSources(), cloneModules(), cloneEdges());

  auto invalidUnits = cloneUnits();
  invalidUnits[0] = graph_query::MaterializedCompilationUnitEntry::fromVerified(
      invalidUnits[0].key().clone(), invalidUnits[0].record().clone(),
      identity::CompilationUnitId());
  expectRejected(materialized.witness().clone(), zc::mv(invalidUnits), cloneCrates(),
                 cloneSources(), cloneModules(), cloneEdges());

  auto invalidCrates = cloneCrates();
  invalidCrates[0] = graph_query::MaterializedCrateEntry::fromVerified(
      invalidCrates[0].key().clone(), invalidCrates[0].record().clone(), identity::CrateId());
  expectRejected(materialized.witness().clone(), cloneUnits(), zc::mv(invalidCrates),
                 cloneSources(), cloneModules(), cloneEdges());

  auto invalidSources = cloneSources();
  invalidSources[0] = graph_query::MaterializedSourceEntry::fromVerified(
      invalidSources[0].key().clone(), invalidSources[0].record().clone(),
      identity::SourceFileId());
  expectRejected(materialized.witness().clone(), cloneUnits(), cloneCrates(),
                 zc::mv(invalidSources), cloneModules(), cloneEdges());

  auto invalidModules = cloneModules();
  invalidModules[0] = graph_query::MaterializedModuleEntry::fromVerified(
      invalidModules[0].key().clone(), invalidModules[0].record().clone(), identity::ModuleId());
  expectRejected(materialized.witness().clone(), cloneUnits(), cloneCrates(), cloneSources(),
                 zc::mv(invalidModules), cloneEdges());

  auto duplicateUnits = cloneUnits();
  duplicateUnits.add(duplicateUnits[0].clone());
  expectRejected(materialized.witness().clone(), zc::mv(duplicateUnits), cloneCrates(),
                 cloneSources(), cloneModules(), cloneEdges());

  auto reorderedCrates = cloneCrates();
  auto firstCrate = zc::mv(reorderedCrates[0]);
  reorderedCrates[0] = zc::mv(reorderedCrates[1]);
  reorderedCrates[1] = zc::mv(firstCrate);
  expectRejected(materialized.witness().clone(), cloneUnits(), zc::mv(reorderedCrates),
                 cloneSources(), cloneModules(), cloneEdges());

  if (materialized.requestEdges().size() != 0) {
    auto invalidEdges = cloneEdges();
    invalidEdges[0] = graph_query::MaterializedModuleDependencyEdge(
        identity::ModuleId(), invalidEdges[0].request().clone(), invalidEdges[0].dependency());
    expectRejected(materialized.witness().clone(), cloneUnits(), cloneCrates(), cloneSources(),
                   cloneModules(), zc::mv(invalidEdges));
  }
}

ZC_TEST("Final-sealed identity and named-item capabilities publish verified values") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto module = stableModule();
  zc::Vector<identity::DefinitionKey> definitionKeys;
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha {}\n"
                  "impl Trait for Alpha {}\n"
                  "fun Beta() { let value = 1; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  zc::Maybe<identity::DefinitionKey> alphaKey;
  {
    auto staging = database.snapshot();
    auto inventory = staging.get<NamedDefinitionInventoryQuery>(module);
    ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(inventory.value().entries().size() == 2);
    alphaKey = inventory.value().entries()[0].key().clone();
    for (const auto& entry : inventory.value().entries()) {
      definitionKeys.add(entry.key().clone());
    }
  }

  auto sealed = sealDatabase(database, roots);
  auto materialized = sealed.getCapability<graph_query::MaterializeModuleGraphQuery>(roots);
  auto sites = sealed.getCapability<IdentitySyntaxSiteInventoryQuery>(module);
  auto admission = sealed.getCapability<StableIdentityAdmissionQuery>(module);
  auto definitions = sealed.getCapability<RevisionLocalDefinitionSitesQuery>(module);
  auto implementations = sealed.getCapability<RevisionLocalImplementationSitesQuery>(module);
  auto moduleProvenance = sealed.getCapability<ModuleBodyProvenanceQuery>(module);
  ZC_REQUIRE(!materialized.isRuntimeRejected());
  ZC_REQUIRE(!materialized.isSourceRejected());
  ZC_REQUIRE(!materialized.isKeyRejected());
  ZC_REQUIRE(materialized.isPublished());
  ZC_REQUIRE(materialized.lease().capability().modules().size() > 0);
  ZC_EXPECT(materialized.lease().capability().witness().contextRoots() == roots);
  ZC_REQUIRE(sites.isPublished());
  ZC_REQUIRE(admission.isPublished());
  ZC_REQUIRE(definitions.isPublished());
  ZC_REQUIRE(implementations.isPublished());
  ZC_REQUIRE(moduleProvenance.isPublished());
  ZC_EXPECT(sites.lease().capability().module().encode().asPtr() ==
            semanticModule().encode().asPtr());
  ZC_EXPECT(admission.lease().capability().module().encode().asPtr() ==
            semanticModule().encode().asPtr());
  ZC_EXPECT(definitions.lease().capability().entries().size() == 2);
  ZC_EXPECT(implementations.lease().capability().entries().size() == 1);
  ZC_EXPECT(moduleProvenance.lease().capability().entries().size() > 0);

  for (const auto& definition : definitionKeys) {
    auto key = binder::StableDefinitionQueryKey::from(semanticModule(), definition.clone());
    auto header = sealed.get<binder::DefinitionHeaderSyntax>(key);
    ZC_REQUIRE(header.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(
        header.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>());
    const auto& value =
        header.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
    ZC_EXPECT(value.value.queryKey() == key);
    ZC_EXPECT(value.diagnostics.values().size() == 0);
    expectExactHeaderReadSet<binder::DefinitionHeaderSyntax>(database.snapshot(), key, 2, 1);
  }
  for (const auto& site : implementations.lease().capability().entries()) {
    auto key = binder::StableImplementationOccurrenceQueryKey::from(semanticModule(),
                                                                    site.occurrence().clone());
    ZC_REQUIRE(key != zc::none);
    auto header = sealed.get<binder::ImplementationOccurrenceHeaderSyntax>(ZC_REQUIRE_NONNULL(key));
    ZC_REQUIRE(header.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(header.value()
                   .storage()
                   .is<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>());
    const auto& value =
        header.value()
            .storage()
            .get<binder::BinderQueryValue<binder::StableImplementationOccurrenceHeader>>();
    ZC_EXPECT(value.value.queryKey() == ZC_REQUIRE_NONNULL(key));
    ZC_EXPECT(value.diagnostics.values().size() == 0);
    expectExactHeaderReadSet<binder::ImplementationOccurrenceHeaderSyntax>(
        database.snapshot(), ZC_REQUIRE_NONNULL(key), 1, 2);
  }

  ZC_REQUIRE(alphaKey != zc::none);
  auto itemKey = contextual(roots, semanticModule(), ZC_REQUIRE_NONNULL(alphaKey));
  auto syntax = sealed.get<NamedItemSyntaxQuery>(itemKey);
  auto provenance = sealed.getCapability<NamedItemProvenanceQuery>(itemKey);
  ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(provenance.isPublished());
  ZC_EXPECT(syntax.value().owningModule().encode().asPtr() == semanticModule().encode().asPtr());
  ZC_EXPECT(provenance.lease().capability().detachedProvenance().entries().size() > 0);

  bool foundDefinitionWithoutBody = false;
  for (const auto& definition : definitionKeys) {
    auto bodyOwner = binder::StableBodyOwnerKey::definition(definition.clone());
    auto bodyKey = contextual(roots, semanticModule(), bodyOwner);
    auto bodySyntax = sealed.get<OwnerBodySyntaxQuery>(bodyKey);
    if (bodySyntax.kind() != query::QueryValueKind::SemanticFailure) { continue; }
    auto bodyProvenance = sealed.getCapability<OwnerBodyProvenanceQuery>(bodyKey);
    ZC_REQUIRE(bodyProvenance.isKeyRejected());
    ZC_EXPECT(bodyProvenance.keyFailure().kind() ==
              binder::BinderKeyFailureKind::DefinitionWithoutBody);
    foundDefinitionWithoutBody = true;
  }
  ZC_EXPECT(foundDefinitionWithoutBody);
}

ZC_TEST("Final-sealed identity queries verify absent module key rejections") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto sealed = sealDatabase(database, roots);
  auto missingModule = stableNamedModule("missing"_zc);
  auto sites = sealed.getCapability<IdentitySyntaxSiteInventoryQuery>(missingModule);
  auto admission = sealed.getCapability<StableIdentityAdmissionQuery>(missingModule);
  auto definitions = sealed.getCapability<RevisionLocalDefinitionSitesQuery>(missingModule);
  auto implementations = sealed.getCapability<RevisionLocalImplementationSitesQuery>(missingModule);
  auto moduleProvenance = sealed.getCapability<ModuleBodyProvenanceQuery>(missingModule);
  ZC_REQUIRE(sites.isKeyRejected());
  ZC_REQUIRE(admission.isKeyRejected());
  ZC_REQUIRE(definitions.isKeyRejected());
  ZC_REQUIRE(implementations.isKeyRejected());
  ZC_REQUIRE(moduleProvenance.isKeyRejected());
  ZC_EXPECT(sites.keyFailure().kind() == binder::BinderKeyFailureKind::MissingSelectedModuleSource);
}

ZC_TEST("Final-sealed named-item queries reject contradictory authority records") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto module = stableModule();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha {}\n"
                  "class Beta {}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto snapshot = database.snapshot();
  auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(inventory.value().entries().size() == 2);
  auto firstKey = inventory.value().entries()[0].key().clone();
  auto secondKey = inventory.value().entries()[1].key().clone();
  auto firstQueryKey = contextual(roots, semanticModule(), firstKey);
  auto secondQueryKey = contextual(roots, semanticModule(), secondKey);
  auto secondAuthority = snapshot.get<ActiveDefinitionAuthorityInput>(secondQueryKey);
  ZC_REQUIRE(secondAuthority.kind() == query::QueryValueKind::Value);

  auto opened = database.beginInputTransaction(snapshot.revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  ZC_REQUIRE(
      transaction
          .set<ActiveDefinitionAuthorityInput>(firstQueryKey, secondAuthority.value().clone())
          .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto sealed = sealDatabase(database, roots);
  auto syntax = sealed.get<NamedItemSyntaxQuery>(firstQueryKey);
  auto provenance = sealed.getCapability<NamedItemProvenanceQuery>(firstQueryKey);
  ZC_REQUIRE(syntax.isRuntimeFailure());
  ZC_EXPECT(syntax.runtimeFailure() == query::QueryRuntimeFailure::InvariantViolation);
  ZC_REQUIRE(provenance.isRuntimeRejected());
  ZC_EXPECT(provenance.runtimeFailure() == query::QueryRuntimeFailure::InvariantViolation);
}

ZC_TEST("Final-sealed owner-body capabilities publish every active body") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "let module_value = 0;\n"
                  "fun Alpha() { let value = 0; }\n"
                  "class Holder {\n"
                  "  let field: i32 = 1;\n"
                  "  fun run() {}\n"
                  "}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto sealed = sealDatabase(database, roots);
  auto moduleKey = contextual(roots, semanticModule());
  auto owners = sealed.get<ModuleBodyOwnersQuery>(moduleKey);
  ZC_REQUIRE(owners.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(owners.value().owners().size() >= 3);
  for (const auto& owner : owners.value().owners()) {
    auto ownerKey = contextual(roots, semanticModule(), owner);
    auto syntax = sealed.get<OwnerBodySyntaxQuery>(ownerKey);
    auto provenance = sealed.getCapability<OwnerBodyProvenanceQuery>(ownerKey);
    ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(provenance.isPublished());
    ZC_EXPECT(syntax.value().owner() == owner);
    ZC_EXPECT(provenance.lease().capability().owner() == owner);
  }
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
