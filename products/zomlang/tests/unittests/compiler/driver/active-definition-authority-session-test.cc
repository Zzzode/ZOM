// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/active-definition-authority-session.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/binder/module-skeleton-query.h"
#include "zomlang/compiler/binder/stable-binding-codec.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/driver/active-definition-authority-query.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/materialized-module-graph-query.h"
#include "zomlang/compiler/driver/module-dependency-provenance-query.h"
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

source_query::CanonicalSourceSnapshot sourceSnapshotFor(identity::SourceFileKey&& source,
                                                        zc::StringPtr text) {
  auto immutable = identity::ImmutableSourceSnapshot::from(zc::mv(source),
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

graph_query::DetachedModuleDependencySiteSet moduleAliasDependencySites(
    identity::ModuleKey&& module, identity::SourceFileKey&& sourceKey, zc::StringPtr text) {
  auto immutable = identity::ImmutableSourceSnapshot::from(sourceKey.clone(),
                                                           zc::heapArray<uint8_t>(text.asBytes()));
  ZC_REQUIRE(immutable != zc::none);
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  path.add(scalar<identity::ModulePathSegment>("root"_zc));
  auto site = graph_query::DetachedModuleDependencySite::from(
      graph_query::DetachedModuleDependencySiteKind::ModuleAlias, zc::mv(path), 1);
  ZC_REQUIRE(site != zc::none);
  zc::Vector<graph_query::DetachedModuleDependencySite> sites;
  sites.add(zc::mv(ZC_REQUIRE_NONNULL(site)));
  auto result = graph_query::DetachedModuleDependencySiteSet::from(
      zc::mv(module), zc::mv(sourceKey), ZC_REQUIRE_NONNULL(immutable).contentDigest(),
      zc::mv(sites));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

graph_query::DetachedModuleDependencySiteSet dependencyBindingSites(
    identity::ModuleKey&& module, identity::SourceFileKey&& sourceKey, zc::StringPtr text,
    graph_query::DetachedModuleDependencySiteKind kind) {
  auto immutable = identity::ImmutableSourceSnapshot::from(sourceKey.clone(),
                                                           zc::heapArray<uint8_t>(text.asBytes()));
  ZC_REQUIRE(immutable != zc::none);
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  auto site = graph_query::DetachedModuleDependencySite::from(kind, zc::mv(path), 3);
  ZC_REQUIRE(site != zc::none);
  zc::Vector<graph_query::DetachedModuleDependencySite> sites;
  sites.add(zc::mv(ZC_REQUIRE_NONNULL(site)));
  auto result = graph_query::DetachedModuleDependencySiteSet::from(
      zc::mv(module), zc::mv(sourceKey), ZC_REQUIRE_NONNULL(immutable).contentDigest(),
      zc::mv(sites));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

void stageCatalogBucket(query::InputTransaction& transaction, const identity::CrateKey& crate,
                        zc::Vector<identity::ModulePathSegment>&& path,
                        zc::Maybe<identity::ModuleKey>&& selected = zc::none) {
  auto key = identity::ModuleCatalogPathBucketKey::from(crate.clone(), zc::mv(path));
  ZC_REQUIRE(key != zc::none);
  auto bucket = selected == zc::none
                    ? identity::ModuleCatalogPathBucket::absent(ZC_REQUIRE_NONNULL(key).clone())
                    : identity::ModuleCatalogPathBucket::present(
                          ZC_REQUIRE_NONNULL(key).clone(), zc::mv(ZC_REQUIRE_NONNULL(selected)));
  ZC_REQUIRE(bucket != zc::none);
  auto canonical =
      resolution_query::CanonicalModuleCatalogBucket::fromVerified(ZC_REQUIRE_NONNULL(bucket));
  ZC_REQUIRE(
      transaction
          .set<resolution_query::ModuleCatalogPathBucketInput>(ZC_REQUIRE_NONNULL(key), canonical)
          .isApplied());
}

void stageModuleSearchRoots(query::InputTransaction& transaction, identity::CrateKey&& crate,
                            identity::CanonicalWorkspaceRelativePath&& path) {
  zc::Vector<binder::ModuleSearchRoot> roots;
  roots.add(binder::ModuleSearchRoot::workspace(crate.clone(), zc::mv(path)));
  auto canonical =
      resolution_query::CanonicalModuleSearchRoots::fromVerified(crate.clone(), roots.asPtr());
  ZC_REQUIRE(canonical != zc::none);
  ZC_REQUIRE(
      transaction
          .set<resolution_query::ModuleSearchRootsInput>(crate, ZC_REQUIRE_NONNULL(canonical))
          .isApplied());
}

identity::ModuleKey dependencyAliasTarget() {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>("root"_zc));
  path.add(scalar<identity::ModulePathSegment>("root"_zc));
  auto result = identity::ModuleKey::from(crate("dependency"_zc), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

void stageModuleAliasTargetInputs(query::InputTransaction& transaction) {
  constexpr auto targetText = "module root;\n"_zc;
  auto dependencyCrate = crate("dependency"_zc);
  auto dependencyModule = semanticModule("dependency"_zc);
  auto targetModule = dependencyAliasTarget();
  auto targetSource = namedSource("alias-target.zom"_zc, "dependency"_zc);

  zc::Vector<graph_query::SelectedModuleRecord> entries;
  entries.add(graph_query::SelectedModuleRecord(dependencyModule.clone(),
                                                namedSource("root.zom"_zc, "dependency"_zc)));
  entries.add(graph_query::SelectedModuleRecord(targetModule.clone(), targetSource.clone()));
  auto catalog = graph_query::SelectedModuleCatalog::from(dependencyCrate.clone(), zc::mv(entries));
  ZC_REQUIRE(catalog != zc::none);

  auto rootSource = stableSource("dependency"_zc);
  auto targetStableSource = source_query::StableSourceQueryKey::fromVerified(targetSource.clone());
  ZC_REQUIRE(targetStableSource != zc::none);
  zc::Vector<source_query::StableSourceQueryKey> sources;
  sources.add(zc::mv(rootSource));
  sources.add(ZC_REQUIRE_NONNULL(targetStableSource).clone());
  auto active = CanonicalSourceSet::from(zc::mv(sources));
  ZC_REQUIRE(active != zc::none);

  auto targetSites = dependencySites(targetModule.clone(), targetSource.clone(), targetText);
  auto targetSnapshot = sourceSnapshotFor(targetSource.clone(), targetText);
  zc::Vector<identity::ModuleKey> ancestryPath;
  ancestryPath.add(targetModule.clone());
  ancestryPath.add(dependencyModule.clone());
  auto ancestry =
      identity::RequesterModuleAncestry::from(targetModule.clone(), zc::mv(ancestryPath));
  ZC_REQUIRE(ancestry != zc::none);

  ZC_REQUIRE(transaction
                 .set<graph_query::SelectedModuleCatalogInput>(dependencyCrate,
                                                               ZC_REQUIRE_NONNULL(catalog))
                 .isApplied());
  ZC_REQUIRE(transaction
                 .set<UserPackageActiveSourcesInput>(stableCrate("dependency"_zc),
                                                     ZC_REQUIRE_NONNULL(active))
                 .isApplied());
  ZC_REQUIRE(
      transaction.set<graph_query::ModuleDependencySiteInput>(targetModule, zc::mv(targetSites))
          .isApplied());
  ZC_REQUIRE(transaction
                 .set<resolution_query::RequesterModuleAncestryInput>(dependencyAliasTarget(),
                                                                      ZC_REQUIRE_NONNULL(ancestry))
                 .isApplied());
  ZC_REQUIRE(transaction
                 .set<source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(targetStableSource),
                                                         targetSnapshot)
                 .isApplied());
}

void stageModuleAliasResolutionInputs(query::InputTransaction& transaction) {
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  stageModuleSearchRoots(transaction, crate(),
                         identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath)));

  zc::Vector<identity::CanonicalPathSegment> dependencyWorkspacePath;
  dependencyWorkspacePath.add(scalar<identity::CanonicalPathSegment>("dependency"_zc));
  stageModuleSearchRoots(
      transaction, crate("dependency"_zc),
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(dependencyWorkspacePath)));

  zc::Vector<identity::ModulePathSegment> ancestryCandidate;
  ancestryCandidate.add(scalar<identity::ModulePathSegment>("root"_zc));
  ancestryCandidate.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  ancestryCandidate.add(scalar<identity::ModulePathSegment>("root"_zc));
  stageCatalogBucket(transaction, crate(), zc::mv(ancestryCandidate));

  zc::Vector<identity::ModulePathSegment> crateCandidate;
  crateCandidate.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  crateCandidate.add(scalar<identity::ModulePathSegment>("root"_zc));
  stageCatalogBucket(transaction, crate(), zc::mv(crateCandidate));

  zc::Vector<identity::ModulePathSegment> dependencyRoot;
  dependencyRoot.add(scalar<identity::ModulePathSegment>("root"_zc));
  dependencyRoot.add(scalar<identity::ModulePathSegment>("root"_zc));
  zc::Maybe<identity::ModuleKey> selected(dependencyAliasTarget());
  stageCatalogBucket(transaction, crate("dependency"_zc), zc::mv(dependencyRoot), zc::mv(selected));
}

void stageDependencyImportResolutionInputs(query::InputTransaction& transaction) {
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  stageModuleSearchRoots(transaction, crate(),
                         identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath)));

  zc::Vector<identity::CanonicalPathSegment> dependencyWorkspacePath;
  dependencyWorkspacePath.add(scalar<identity::CanonicalPathSegment>("dependency"_zc));
  stageModuleSearchRoots(
      transaction, crate("dependency"_zc),
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(dependencyWorkspacePath)));

  zc::Vector<identity::ModulePathSegment> ancestryCandidate;
  ancestryCandidate.add(scalar<identity::ModulePathSegment>("root"_zc));
  ancestryCandidate.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  stageCatalogBucket(transaction, crate(), zc::mv(ancestryCandidate));

  zc::Vector<identity::ModulePathSegment> crateCandidate;
  crateCandidate.add(scalar<identity::ModulePathSegment>("dependency"_zc));
  stageCatalogBucket(transaction, crate(), zc::mv(crateCandidate));

  zc::Vector<identity::ModulePathSegment> dependencyRoot;
  dependencyRoot.add(scalar<identity::ModulePathSegment>("root"_zc));
  zc::Maybe<identity::ModuleKey> selected(semanticModule("dependency"_zc));
  stageCatalogBucket(transaction, crate("dependency"_zc), zc::mv(dependencyRoot), zc::mv(selected));
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
                     bool includeDependency = false,
                     zc::StringPtr dependencyText = "module root;\nclass Dependency {}\n"_zc,
                     bool rootImportsDependency = false, bool rootReexportsDependency = false) {
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
    auto sites =
        rootImportsDependency
            ? dependencyBindingSites(module.clone(), selectedSource.clone(), text,
                                     graph_query::DetachedModuleDependencySiteKind::Import)
        : rootReexportsDependency
            ? dependencyBindingSites(module.clone(), selectedSource.clone(), text,
                                     graph_query::DetachedModuleDependencySiteKind::ForeignReexport)
            : dependencySites(module.clone(), selectedSource.clone(), text);
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
      auto dependencyCrate = crate("dependency"_zc);
      auto dependencySource = namedSource("root.zom"_zc, "dependency"_zc);
      auto dependencyModule = semanticModule("dependency"_zc);
      auto dependencyAlias = identity::DependencyAlias::fromCanonical("dependency"_zc);
      ZC_REQUIRE(dependencyAlias != zc::none);
      auto dependencyAliasKey = resolution_query::DependencyAliasRootQueryKey::from(
          crate(), zc::mv(ZC_REQUIRE_NONNULL(dependencyAlias)));
      ZC_REQUIRE(dependencyAliasKey != zc::none);
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
                     .set<resolution_query::DependencyAliasRootInput>(
                         ZC_REQUIRE_NONNULL(dependencyAliasKey),
                         resolution_query::ExplicitModuleTarget::present(dependencyModule.clone()))
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

  identity::CanonicalIdentityInternerSet& identityInterners() const override { return interners; }

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

  identity::IdentityInternResult<identity::DefId> internDefinition(
      identity::SemanticContextBrand requested, const identity::DefinitionKey& key,
      const identity::DefinitionIdentityRecord& record) const override {
    return interners.internDefinition(requested, key, record);
  }

  identity::IdentityInternResult<identity::ImplId> internImplementation(
      identity::SemanticContextBrand requested, const identity::ImplKey& key,
      const identity::ImplIdentityRecord& record) const override {
    return interners.internImplementation(requested, key, record);
  }

  identity::IdentityInternResult<identity::GenericParameterId> internGenericParameter(
      identity::SemanticContextBrand requested, const identity::GenericParameterKey& key,
      const identity::GenericParameterIdentityRecord& record) const override {
    return interners.internGenericParameter(requested, key, record);
  }

  identity::IdentityInternResult<identity::CallableParameterId> internCallableParameter(
      identity::SemanticContextBrand requested, const identity::CallableParameterKey& key,
      const identity::CallableParameterIdentityRecord& record) const override {
    return interners.internCallableParameter(requested, key, record);
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

  zc::Maybe<identity::DefinitionIdentityEntry> definition(identity::DefId handle) const override {
    return interners.definition(handle);
  }

  zc::Maybe<identity::ImplementationIdentityEntry> implementation(
      identity::ImplId handle) const override {
    return interners.implementation(handle);
  }

  zc::Maybe<identity::GenericParameterIdentityEntry> genericParameter(
      identity::GenericParameterId handle) const override {
    return interners.genericParameter(handle);
  }

  zc::Maybe<identity::CallableParameterIdentityEntry> callableParameter(
      identity::CallableParameterId handle) const override {
    return interners.callableParameter(handle);
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

ZC_TEST("MaterializedBindingCapabilityTest.SkeletonPublishesMembershipGatedIdentityWitness") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc, "authority"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(key));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_EXPECT(capability.contextRoots() == roots);
  ZC_EXPECT(capability.module().encode().asPtr() == semanticModule().encode().asPtr());
  ZC_EXPECT(capability.revision() == sealed.revision());
  ZC_EXPECT(capability.identities().definitions().size() == 1);
  ZC_REQUIRE(capability.materializedDefinitions().size() == 1);
  ZC_EXPECT(capability.materializedDefinitions()[0].identity ==
            capability.identities().definitions()[0].handle());
  ZC_EXPECT(capability.materializedDefinitions()[0].kind == identity::DefinitionKind::Class);
  ZC_EXPECT(capability.materializedDefinitions()[0].name.text() == "Alpha"_zc);
  ZC_EXPECT(capability.materializedDefinitions()[0].source.belongsTo(capability.source()));
  ZC_EXPECT(capability.materializedNodeScopes().size() ==
            capability.identities().stableWitness().nodeScopes().values().size());
  ZC_EXPECT(capability.materializedFailedLookups().size() ==
            capability.identities().stableWitness().failedLookups().values().size());
  ZC_EXPECT(capability.identities().implementations().size() == 0);
  ZC_EXPECT(capability.identities().genericParameters().size() == 0);
  ZC_EXPECT(capability.identities().callableParameters().size() == 0);
  ZC_EXPECT(capability.scopeIdentities().size() ==
            capability.identities().stableWitness().scopes().values().size());
  ZC_REQUIRE(capability.materializedScopes().size() == capability.scopeIdentities().size());
  size_t moduleScopes = 0;
  size_t alphaBindings = 0;
  for (const auto& scope : capability.materializedScopes()) {
    ZC_EXPECT(scope.id.belongsTo(capability.context()));
    ZC_EXPECT(scope.source.belongsTo(capability.source()));
    if (!scope.owner.value().is<binder::ModuleScopeOwner>()) { continue; }
    ++moduleScopes;
    for (const auto& entry : scope.bindings) {
      ZC_EXPECT(entry.name.nameSpace() == binder::Namespace::Type);
      ZC_EXPECT(entry.name.name().text() == "Alpha"_zc);
      ZC_REQUIRE(entry.binding.bindingIdentity.value().is<binder::DefinitionBindingTarget>());
      ZC_EXPECT(
          entry.binding.bindingIdentity.value().get<binder::DefinitionBindingTarget>().definition ==
          capability.materializedDefinitions()[0].identity);
      ++alphaBindings;
    }
  }
  ZC_EXPECT(moduleScopes == 1);
  ZC_EXPECT(alphaBindings == 1);
  for (size_t index = 0; index < capability.scopeIdentities().size(); ++index) {
    const auto identity = capability.scopeIdentities()[index];
    const auto& scope = capability.identities().stableWitness().scopes().values()[index];
    ZC_EXPECT(identity.belongsTo(capability.context()));
    ZC_EXPECT(identity.module() == capability.identities().module());
    ZC_EXPECT(capability.scopeFor(scope.owner()) == identity);
    ZC_IF_SOME(parent, scope.parent()) {
      auto parentIdentity = capability.scopeFor(parent);
      ZC_REQUIRE(parentIdentity != zc::none);
      ZC_EXPECT(identity.index() > ZC_REQUIRE_NONNULL(parentIdentity).index());
    }
  }
}

ZC_TEST(
    "MaterializedBindingCapabilityTest."
    "SkeletonMaterializesInterfaceMethodParametersWithoutReceiver") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "interface Mapper {\n"
                  "  fun set(index: u64, value: u64) -> unit;\n"
                  "  fun map<U>(value: U) -> U;\n"
                  "}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(key));
  ZC_REQUIRE(materialized.isPublished());
  const auto facts = materialized.lease().capability().materializedCallableParameters();
  ZC_REQUIRE(facts.size() == 3);
  for (const auto& fact : facts) {
    ZC_EXPECT(!fact.receiver);
    ZC_EXPECT(fact.name != zc::none);
  }
}

ZC_TEST("MaterializedBindingCapabilityTest.SkeletonMaterializesHeaderScopeBindings") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "fun identity<T>(value: T) -> T { return value; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(key));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.materializedGenericParameters().size() == 1);
  ZC_REQUIRE(capability.materializedCallableParameters().size() == 1);

  size_t genericBindings = 0;
  size_t callableBindings = 0;
  for (const auto& scope : capability.materializedScopes()) {
    ZC_EXPECT(scope.source.belongsTo(capability.source()));
    for (const auto& entry : scope.bindings) {
      const auto& target = entry.binding.bindingIdentity.value();
      if (target.is<binder::GenericParameterBindingTarget>()) {
        ZC_EXPECT(entry.name.nameSpace() == binder::Namespace::Type);
        ZC_EXPECT(target.get<binder::GenericParameterBindingTarget>().parameter ==
                  capability.materializedGenericParameters()[0].identity);
        ++genericBindings;
      }
      if (target.is<binder::CallableParameterBindingTarget>()) {
        ZC_EXPECT(entry.name.nameSpace() == binder::Namespace::Value);
        ZC_EXPECT(target.get<binder::CallableParameterBindingTarget>().parameter ==
                  capability.materializedCallableParameters()[0].identity);
        ++callableBindings;
      }
    }
  }
  ZC_EXPECT(genericBindings == 1);
  ZC_EXPECT(callableBindings == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.SkeletonMaterializesDirectLocalExports") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database, "module root;\nexport class Alpha {}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(key));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.identities().stableWitness().localExports().values().size() == 1);
  ZC_REQUIRE(capability.materializedDefinitions().size() == 1);
  ZC_REQUIRE(capability.materializedLocalExports().size() == 1);
  const auto& localExport = capability.materializedLocalExports()[0];
  ZC_EXPECT(static_cast<bool>(localExport.node));
  ZC_REQUIRE(localExport.sourceBinding.value().is<binder::DefinitionBindingTarget>());
  ZC_REQUIRE(localExport.canonicalTarget.value().is<binder::DefinitionBindingTarget>());
  ZC_EXPECT(localExport.sourceBinding.value().get<binder::DefinitionBindingTarget>().definition ==
            capability.materializedDefinitions()[0].identity);
  ZC_EXPECT(localExport.canonicalTarget.value().get<binder::DefinitionBindingTarget>().definition ==
            capability.materializedDefinitions()[0].identity);
  ZC_EXPECT(localExport.bindingSpan.belongsTo(capability.source()));
  ZC_EXPECT(localExport.canonicalDeclarationSpan.belongsTo(capability.source()));
  ZC_EXPECT(localExport.aliasSpan == zc::none);
  ZC_EXPECT(localExport.exportSpan.belongsTo(capability.source()));
  ZC_EXPECT(localExport.reexportChain.size() == 0);
  const auto& surface = capability.bindingSurface();
  ZC_REQUIRE(surface.sourceModule() == capability.identities().module());
  ZC_REQUIRE(surface.visibleEntries().size() == 1);
  ZC_REQUIRE(surface.exports().size() == 1);
  ZC_EXPECT(surface.visibleEntries()[0].name.name().text() == "Alpha"_zc);
  ZC_EXPECT(surface.visibleEntries()[0].exported);
  ZC_EXPECT(surface.visibleEntries()[0].visibility.value().is<binder::ExternalVisibility>());
  ZC_EXPECT(surface.exports()[0]
                .canonicalTarget.value()
                .get<binder::DefinitionBindingTarget>()
                .definition == capability.materializedDefinitions()[0].identity);

  auto verifiedKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(verifiedKey));
  ZC_REQUIRE(verified.isPublished());
  ZC_REQUIRE(verified.lease()
                 .capability()
                 .skeletonLease()
                 .capability()
                 .materializedLocalExports()
                 .size() == 1);
  ZC_EXPECT(verified.lease().capability().bindingSurface().revision().digest() ==
            capability.bindingSurface().revision().digest());
  ZC_EXPECT(verified.lease()
                .capability()
                .skeletonLease()
                .capability()
                .materializedLocalExports()[0]
                .canonicalTarget.value()
                .get<binder::DefinitionBindingTarget>()
                .definition == capability.materializedDefinitions()[0].identity);
}

ZC_TEST("MaterializedBindingCapabilityTest.SkeletonMaterializesLocalReexports") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Gamma {}\n"
                  "export { Gamma as PublicGamma };\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(key));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.materializedDefinitions().size() == 1);
  ZC_REQUIRE(capability.materializedLocalExports().size() == 1);
  const auto& localExport = capability.materializedLocalExports()[0];
  ZC_REQUIRE(localExport.sourceBinding.value().is<binder::DefinitionBindingTarget>());
  ZC_REQUIRE(localExport.canonicalTarget.value().is<binder::DefinitionBindingTarget>());
  ZC_EXPECT(localExport.sourceBinding.value().get<binder::DefinitionBindingTarget>().definition ==
            capability.materializedDefinitions()[0].identity);
  ZC_EXPECT(localExport.canonicalTarget.value().get<binder::DefinitionBindingTarget>().definition ==
            capability.materializedDefinitions()[0].identity);
  ZC_REQUIRE(localExport.aliasSpan != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(localExport.aliasSpan).belongsTo(capability.source()));
  ZC_EXPECT(ZC_ASSERT_NONNULL(localExport.aliasSpan).byteStart() >
            localExport.exportSpan.byteStart());
  ZC_EXPECT(ZC_ASSERT_NONNULL(localExport.aliasSpan).byteEnd() < localExport.exportSpan.byteEnd());
  ZC_EXPECT(localExport.reexportChain.size() == 1);
  ZC_REQUIRE(localExport.reexportChain.size() == 1);
  ZC_EXPECT(localExport.reexportChain[0].module == capability.identities().module());
  ZC_REQUIRE(
      localExport.reexportChain[0].bindingIdentity.value().is<binder::DefinitionBindingTarget>());
  ZC_EXPECT(localExport.reexportChain[0]
                .bindingIdentity.value()
                .get<binder::DefinitionBindingTarget>()
                .definition == capability.materializedDefinitions()[0].identity);
  const auto& surface = capability.bindingSurface();
  ZC_REQUIRE(surface.visibleEntries().size() == 2);
  ZC_REQUIRE(surface.exports().size() == 1);
  ZC_EXPECT(surface.visibleEntries()[0].name.name().text() == "Gamma"_zc);
  ZC_EXPECT(!surface.visibleEntries()[0].exported);
  ZC_EXPECT(surface.visibleEntries()[1].name.name().text() == "PublicGamma"_zc);
  ZC_EXPECT(surface.visibleEntries()[1].exported);
  ZC_EXPECT(surface.visibleEntries()[1].aliasSpan != zc::none);
  ZC_EXPECT(surface.visibleEntries()[1].reexportChain.size() == 1);
  ZC_EXPECT(surface.exports()[0].name.name().text() == "PublicGamma"_zc);

  auto verifiedKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(verifiedKey));
  ZC_REQUIRE(verified.isPublished());
  ZC_REQUIRE(verified.lease()
                 .capability()
                 .skeletonLease()
                 .capability()
                 .materializedLocalExports()
                 .size() == 1);
  ZC_EXPECT(verified.lease()
                .capability()
                .skeletonLease()
                .capability()
                .materializedLocalExports()[0]
                .reexportChain.size() == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.SkeletonRejectsPermissionAndLineageMutations") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha<T> {}\n"
                  "impl Trait for Alpha {}\n"
                  "fun Beta<T>(value: T) -> T { return value; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  const auto encodedKey = key.encodeCanonical();
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(key));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_EXPECT(capability.identities().definitions().size() == 2);
  ZC_EXPECT(capability.materializedDefinitions().size() == 2);
  ZC_EXPECT(capability.identities().implementations().size() == 1);
  ZC_EXPECT(capability.materializedImplementations().size() == 1);
  ZC_EXPECT(capability.materializedGenericParameters().size() == 2);
  ZC_EXPECT(capability.materializedCallableParameters().size() == 1);
  ZC_EXPECT(capability.materializedCallableParameters()[0].identity ==
            capability.identities().callableParameters()[0].handle());
  ZC_EXPECT(capability.materializedCallableParameters()[0].source.source().encode().asPtr() ==
            capability.source().encode().asPtr());
  for (const auto& parameter : capability.materializedGenericParameters()) {
    ZC_EXPECT(parameter.identity.belongsTo(capability.context()));
    ZC_EXPECT(parameter.source.source().encode().asPtr() == capability.source().encode().asPtr());
    size_t matches = 0;
    for (const auto& identity : capability.identities().genericParameters()) {
      if (identity.handle() == parameter.identity) { ++matches; }
    }
    ZC_EXPECT(matches == 1);
  }
  const auto& implementation = capability.materializedImplementations()[0];
  ZC_EXPECT(implementation.occurrence.belongsTo(capability.context()));
  ZC_EXPECT(implementation.occurrence.belongsTo(capability.identities().module()));
  ZC_EXPECT(implementation.authority == capability.identities().implementations()[0].handle());
  ZC_EXPECT(implementation.source.source().encode().asPtr() ==
            capability.source().encode().asPtr());
  ZC_EXPECT(implementation.members.size() == capability.materializedDefinitions().size());
  ZC_EXPECT(implementation.members[0] == capability.materializedDefinitions()[0].identity);
  ZC_EXPECT(implementation.members[1] == capability.materializedDefinitions()[1].identity);
  ZC_EXPECT(capability.identities().genericParameters().size() == 2);
  ZC_EXPECT(capability.identities().callableParameters().size() == 1);
  ZC_EXPECT(capability.source().belongsTo(capability.module().crate()));
  ZC_EXPECT(capability.provenance().source().encode().asPtr() ==
            capability.source().encode().asPtr());
  ZC_EXPECT(capability.provenance().entries().size() != 0);
  ZC_EXPECT(capability.dependencyProvenanceLease().revision() == capability.revision());
  ZC_EXPECT(capability.dependencyProvenanceLease().capability().module().encode().asPtr() ==
            capability.module().encode().asPtr());
  ZC_EXPECT(capability.dependencyProvenanceLease().capability().source().encode().asPtr() ==
            capability.source().encode().asPtr());

  auto foreignModule = namedSemanticModule("foreign"_zc);
  auto foreignKey = ContextualModuleKey::from(roots.clone(), foreignModule.clone());
  auto rejected =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(foreignKey));
  ZC_REQUIRE(rejected.isRuntimeRejected());
  ZC_EXPECT(rejected.runtimeFailure() == query::QueryRuntimeFailure::InvariantViolation);

  zc::Vector<graph_query::MaterializedModuleSkeleton::DefinitionProvenanceLease>
      invalidDefinitionProvenances;
  for (const auto& provenance : capability.definitionProvenanceLeases()) {
    invalidDefinitionProvenances.add(provenance.retain());
  }
  auto sourceKey = identity::source_query::StableSourceQueryKey::fromVerified(capability.source());
  ZC_REQUIRE(sourceKey != zc::none);
  auto parsed =
      sealed.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_REQUIRE_NONNULL(sourceKey)));
  ZC_REQUIRE(parsed.isPublished());
  auto invalidCandidate = graph_query::MaterializedModuleSkeleton::from(
      ContextualModuleKey::from(roots.clone(), zc::mv(foreignModule)),
      capability.graphLease().retain(),
      zc::Vector<graph_query::MaterializedModuleSkeleton::DependencySkeletonLease>(),
      capability.source().clone(), capability.provenance().clone(),
      capability.dependencyProvenanceLease().retain(), capability.identities().clone(),
      capability.identityAdmissionLease().retain(), capability.definitionSitesLease().retain(),
      capability.implementationSitesLease().retain(), zc::mv(invalidDefinitionProvenances),
      parsed.lease().capability());
  ZC_EXPECT(invalidCandidate == zc::none);

  auto wrongDomain = mutation::flipByte(encodedKey.asPtr(), 0);
  ZC_EXPECT(graph_query::MaterializeModuleSkeletonQuery::decodeKey(wrongDomain.asPtr()) ==
            zc::none);
  auto trailing = mutation::withTrailingByte(encodedKey.asPtr());
  ZC_EXPECT(graph_query::MaterializeModuleSkeletonQuery::decodeKey(trailing.asPtr()) == zc::none);
  ZC_EXPECT(graph_query::MaterializeModuleSkeletonQuery::decodeKey(
                encodedKey.asPtr().slice(0, encodedKey.size() - 1)) == zc::none);
}

ZC_TEST("MaterializedBindingCapabilityTest.SkeletonRejectsHeaderMembershipWithdrawal") {
  enum class MembershipDomain : uint8_t {
    Definition,
    Implementation,
    GenericParameter,
    CallableParameter,
  };
  const auto assertMembershipWithdrawalRejected = [](MembershipDomain domain) {
    auto database = queryDatabase(scheduler());
    ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
    ContextualIdentityAuthorityInputLedger state;
    auto roots = packageRoots();
    stageBaseInputs(state, database,
                    "module root;\n"
                    "class Alpha<T> {}\n"
                    "impl Trait for Alpha {}\n"
                    "fun Beta<T>(value: T) -> T { return value; }\n"_zc);
    ZC_REQUIRE(commitAuthority(state, database, roots));

    auto opened = database.beginInputTransaction(database.snapshot().revision());
    ZC_REQUIRE(opened.isOpened());
    auto transaction = zc::mv(opened).takeTransaction();
    switch (domain) {
      case MembershipDomain::Definition: {
        zc::Maybe<size_t> selectedIndex;
        const auto module = semanticModule();
        for (size_t index = 0; index < state.definitionKeys().size(); ++index) {
          if (state.definitionKeys()[index].module().encode().asPtr() != module.encode().asPtr()) {
            continue;
          }
          selectedIndex = index;
          break;
        }
        ZC_REQUIRE(selectedIndex != zc::none);
        auto key = ContextualDefinitionKey::from(
            roots.clone(), state.definitionKeys()[ZC_REQUIRE_NONNULL(selectedIndex)].clone());
        ZC_REQUIRE(transaction.erase<ActiveDefinitionAuthorityInput>(key).isApplied());
      } break;
      case MembershipDomain::Implementation:
        ZC_REQUIRE(state.implementationKeys().size() == 1);
        ZC_REQUIRE(
            transaction.erase<ActiveImplementationAuthorityInput>(state.implementationKeys()[0])
                .isApplied());
        break;
      case MembershipDomain::GenericParameter:
        ZC_REQUIRE(state.genericParameterKeys().size() == 2);
        ZC_REQUIRE(
            transaction.erase<ActiveGenericParameterAuthorityInput>(state.genericParameterKeys()[0])
                .isApplied());
        break;
      case MembershipDomain::CallableParameter:
        ZC_REQUIRE(state.callableParameterKeys().size() == 1);
        ZC_REQUIRE(
            transaction
                .erase<ActiveCallableParameterAuthorityInput>(state.callableParameterKeys()[0])
                .isApplied());
        break;
    }
    ZC_REQUIRE(transaction.commit().isCommitted());

    auto sealed = sealDatabase(database, roots);
    auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
    auto rejected = sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(key));
    ZC_REQUIRE(rejected.isRuntimeRejected());
    ZC_EXPECT(rejected.runtimeFailure() == query::QueryRuntimeFailure::InvariantViolation);
  };

  assertMembershipWithdrawalRejected(MembershipDomain::Definition);
  assertMembershipWithdrawalRejected(MembershipDomain::Implementation);
  assertMembershipWithdrawalRejected(MembershipDomain::GenericParameter);
  assertMembershipWithdrawalRejected(MembershipDomain::CallableParameter);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyRejectsPermissionAndLineageMutations") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "let module_value = 0;\n"
                  "fun Alpha<T>(value: T) -> T { return value; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto owner = binder::StableOwnerBodyQueryKey::from(
      semanticModule(), binder::StableBodyOwnerKey::module(semanticModule()));
  ZC_REQUIRE(owner != zc::none);
  auto key = ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(owner)));
  const auto encodedKey = key.encodeCanonical();
  auto materialized = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(zc::mv(key));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_EXPECT(capability.contextRoots() == roots);
  ZC_EXPECT(capability.owner().module().encode().asPtr() == semanticModule().encode().asPtr());
  ZC_EXPECT(capability.context().isValid());
  ZC_EXPECT(capability.revision() == sealed.revision());
  ZC_EXPECT(capability.stableWitness().bindings().values().size() == 1);
  ZC_EXPECT(capability.module().belongsTo(capability.context()));
  ZC_EXPECT(capability.provenanceLease().revision() == capability.revision());
  ZC_EXPECT(capability.provenanceLease().capability().owner() == capability.owner().owner());
  ZC_EXPECT(
      capability.provenanceLease().capability().detachedProvenance().source().encode().asPtr() ==
      capability.source().encode().asPtr());
  ZC_EXPECT(capability.scopeIdentities().size() ==
            capability.stableWitness().scopes().values().size());
  for (size_t index = 0; index < capability.scopeIdentities().size(); ++index) {
    const auto identity = capability.scopeIdentities()[index];
    ZC_EXPECT(identity.belongsTo(capability.context()));
    ZC_EXPECT(identity.module() == capability.module());
    ZC_EXPECT(identity.index() == capability.allocation().scopeBegin() + index);
    ZC_EXPECT(capability.scopeFor(capability.stableWitness().scopes().values()[index].scope()) ==
              identity);
  }
  ZC_EXPECT(capability.materializedNodeScopes().size() ==
            capability.stableWitness().nodeScopes().values().size());
  for (const auto& nodeScope : capability.materializedNodeScopes()) {
    ZC_EXPECT(static_cast<bool>(nodeScope.node));
    ZC_EXPECT(nodeScope.scope.belongsTo(capability.context()));
    ZC_EXPECT(nodeScope.scope.module() == capability.module());
  }
  ZC_EXPECT(capability.materializedOwnerLocalBindings().size() ==
            capability.stableWitness().bindings().values().size());
  for (size_t index = 0; index < capability.materializedOwnerLocalBindings().size(); ++index) {
    const auto& binding = capability.materializedOwnerLocalBindings()[index];
    const auto identity = binding.identity;
    ZC_EXPECT(identity.belongsTo(capability.context()));
    ZC_EXPECT(identity.belongsTo(capability.module()));
    ZC_EXPECT(identity.index() == capability.allocation().ownerLocalBegin() + index);
    ZC_EXPECT(binding.kind == binder::OwnerLocalBindingKind::Local);
    ZC_EXPECT(binding.name.text() == "module_value"_zc);
    ZC_EXPECT(binding.site.value().is<binder::PatternBindingSite>());
    ZC_EXPECT(binding.source.belongsTo(capability.source()));
    ZC_EXPECT(binding.declaringScope.belongsTo(capability.context()));
    ZC_EXPECT(binding.declaringScope.module() == capability.module());
  }
  ZC_EXPECT(capability.allocation().owner().encodeCanonical().asPtr() ==
            capability.owner().encodeCanonical().asPtr());
  ZC_EXPECT(capability.allocation().ownerLocalCount() ==
            capability.stableWitness().bindings().values().size());

  auto foreignModule = namedSemanticModule("foreign"_zc);
  auto foreignOwner = binder::StableOwnerBodyQueryKey::from(
      foreignModule.clone(), binder::StableBodyOwnerKey::module(foreignModule.clone()));
  ZC_REQUIRE(foreignOwner != zc::none);
  auto foreignKey =
      ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(foreignOwner)));
  auto rejected = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(zc::mv(foreignKey));
  ZC_REQUIRE(rejected.isRuntimeRejected());
  ZC_EXPECT(rejected.runtimeFailure() == query::QueryRuntimeFailure::InvariantViolation);

  auto invalidOwner = binder::StableOwnerBodyQueryKey::from(
      foreignModule.clone(), binder::StableBodyOwnerKey::module(zc::mv(foreignModule)));
  ZC_REQUIRE(invalidOwner != zc::none);
  auto syntaxKey = ContextualBodyOwnerKey::from(roots.clone(), capability.owner().clone());
  auto syntax = sealed.get<OwnerBodySyntaxQuery>(zc::mv(syntaxKey));
  auto parsedKey = identity::source_query::StableSourceQueryKey::fromVerified(capability.source());
  ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(parsedKey != zc::none);
  auto parsed =
      sealed.getCapability<parser::ParseSourceQuery>(zc::mv(ZC_REQUIRE_NONNULL(parsedKey)));
  ZC_REQUIRE(parsed.isPublished());
  auto invalidCandidate = graph_query::MaterializedOwnerBody::from(
      ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(invalidOwner))),
      capability.context(), capability.revision(), capability.fingerprint().clone(),
      capability.module(), capability.skeletonLease().retain(), capability.source().clone(),
      capability.provenanceLease().retain(), capability.stableWitness().clone(),
      capability.allocation().clone(), syntax.value(), parsed.lease().capability());
  ZC_EXPECT(invalidCandidate == zc::none);

  auto wrongDomain = mutation::flipByte(encodedKey.asPtr(), 0);
  ZC_EXPECT(graph_query::MaterializeOwnerBodyQuery::decodeKey(wrongDomain.asPtr()) == zc::none);
  auto trailing = mutation::withTrailingByte(encodedKey.asPtr());
  ZC_EXPECT(graph_query::MaterializeOwnerBodyQuery::decodeKey(trailing.asPtr()) == zc::none);
  ZC_EXPECT(graph_query::MaterializeOwnerBodyQuery::decodeKey(
                encodedKey.asPtr().slice(0, encodedKey.size() - 1)) == zc::none);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesClosureFreeVariables") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\nlet captured = 1;\nlet transform = () => captured;\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto owner = binder::StableOwnerBodyQueryKey::from(
      semanticModule(), binder::StableBodyOwnerKey::module(semanticModule()));
  ZC_REQUIRE(owner != zc::none);
  auto materialized = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(
      ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(owner))));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.stableWitness().closureFreeVariables().values().size() == 1);
  ZC_REQUIRE(capability.materializedClosureFreeVariables().size() == 1);
  const auto& stable = capability.stableWitness().closureFreeVariables().values()[0];
  const auto& runtime = capability.materializedClosureFreeVariables()[0];
  ZC_EXPECT(runtime.closure == stable.closure());
  ZC_REQUIRE(runtime.variables.size() == 1);
  ZC_REQUIRE(stable.variables().values().size() == 1);
  ZC_EXPECT(runtime.variables[0].target.value().is<binder::OwnerLocalBindingTarget>());
  ZC_EXPECT(runtime.variables[0].referenceSites.size() == 1);
  ZC_EXPECT(static_cast<bool>(runtime.variables[0].referenceSites[0]));
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesFunctionExpressionFreeVariables") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\nlet captured = 1;\nlet transform = fun () { captured; };\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto owner = binder::StableOwnerBodyQueryKey::from(
      semanticModule(), binder::StableBodyOwnerKey::module(semanticModule()));
  ZC_REQUIRE(owner != zc::none);
  auto materialized = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(
      ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(owner))));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.stableWitness().closures().values().size() == 1);
  ZC_EXPECT(capability.stableWitness().closures().values()[0].closure().role() ==
            binder::AnonymousOwnerLocalRole::Closure);
  ZC_REQUIRE(capability.stableWitness().closureFreeVariables().values().size() == 1);
  ZC_REQUIRE(capability.materializedClosureFreeVariables().size() == 1);
  const auto& stable = capability.stableWitness().closureFreeVariables().values()[0];
  const auto& runtime = capability.materializedClosureFreeVariables()[0];
  ZC_EXPECT(runtime.closure == stable.closure());
  ZC_REQUIRE(stable.variables().values().size() == 1);
  ZC_REQUIRE(runtime.variables.size() == 1);
  ZC_EXPECT(runtime.variables[0].referenceSites.size() == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyProjectsExplicitClosureCaptures") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\nlet captured = 1;\n"
                  "let transform = fun () use [captured] { captured; };\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto owner = binder::StableOwnerBodyQueryKey::from(
      semanticModule(), binder::StableBodyOwnerKey::module(semanticModule()));
  ZC_REQUIRE(owner != zc::none);
  auto materialized = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(
      ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(owner))));
  ZC_REQUIRE(materialized.isPublished());
  const auto& captures =
      materialized.lease().capability().stableWitness().explicitClosureCaptures();
  const auto runtime = materialized.lease().capability().materializedExplicitClosureCaptures();
  ZC_REQUIRE(captures.values().size() == 1);
  ZC_REQUIRE(runtime.size() == 1);
  ZC_REQUIRE(captures.values()[0].captures().values().size() == 1);
  ZC_REQUIRE(runtime[0].captures.size() == 1);
  ZC_EXPECT(captures.values()[0].closure().role() ==
            binder::AnonymousOwnerLocalRole::FunctionExpression);
  ZC_EXPECT(captures.values()[0].captures().values()[0].mode() ==
            binder::StableExplicitCaptureMode::ByValue);
  ZC_EXPECT(runtime[0].source.belongsTo(materialized.lease().capability().source()));
  const auto anonymous = materialized.lease().capability().materializedAnonymousEntities();
  ZC_REQUIRE(anonymous.size() == 2);
  size_t closures = 0;
  size_t functionExpressions = 0;
  for (size_t index = 0; index < anonymous.size(); ++index) {
    const auto& entry = anonymous[index];
    ZC_EXPECT(entry.identity.index() ==
              materialized.lease().capability().allocation().anonymousBegin() + index);
    ZC_EXPECT(entry.site.value().is<binder::DeclarationDefinitionSite>());
    ZC_EXPECT(entry.site.value().get<binder::DeclarationDefinitionSite>().node == entry.node);
    if (entry.key.role() == binder::AnonymousOwnerLocalRole::Closure) {
      ++closures;
    } else {
      ++functionExpressions;
    }
  }
  ZC_EXPECT(closures == 1);
  ZC_EXPECT(functionExpressions == 1);

  const auto encoded = binder::StableBindingCodec<binder::BoundOwnerBody>::encode(
      materialized.lease().capability().stableWitness());
  auto decoded = binder::StableBindingCodec<binder::BoundOwnerBody>::decode(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == materialized.lease().capability().stableWitness());
  ZC_EXPECT(binder::StableBindingCodec<binder::BoundOwnerBody>::decode(
                mutation::withTrailingByte(encoded.asPtr()).asPtr()) == zc::none);
  ZC_EXPECT(binder::StableBindingCodec<binder::BoundOwnerBody>::decode(
                encoded.asPtr().slice(0, encoded.size() - 1)) == zc::none);
  const auto rejectsMutation = [&](size_t byteIndex) {
    auto mutated = mutation::flipByte(encoded.asPtr(), byteIndex);
    auto candidate = binder::StableBindingCodec<binder::BoundOwnerBody>::decode(mutated.asPtr());
    return candidate == zc::none ||
           ZC_REQUIRE_NONNULL(candidate) != materialized.lease().capability().stableWitness();
  };
  ZC_EXPECT(rejectsMutation(0));
  ZC_EXPECT(rejectsMutation(encoded.size() / 2));
  ZC_EXPECT(rejectsMutation(encoded.size() - 1));
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesReferenceAndEmptyCaptures") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\nlet value = 1;\nlet reference = 2;\n"
                  "let transform = fun () use [value, &reference] { value; reference; };\n"
                  "let empty = fun () use [] {};\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto owner = binder::StableOwnerBodyQueryKey::from(
      semanticModule(), binder::StableBodyOwnerKey::module(semanticModule()));
  ZC_REQUIRE(owner != zc::none);
  auto materialized = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(
      ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(owner))));
  ZC_REQUIRE(materialized.isPublished());
  const auto& stable = materialized.lease().capability().stableWitness().explicitClosureCaptures();
  const auto runtime = materialized.lease().capability().materializedExplicitClosureCaptures();
  ZC_REQUIRE(stable.values().size() == 2);
  ZC_REQUIRE(runtime.size() == 2);
  ZC_REQUIRE(stable.values()[0].captures().values().size() == 2);
  ZC_REQUIRE(runtime[0].captures.size() == 2);
  ZC_EXPECT(stable.values()[0].captures().values()[0].mode() ==
            binder::StableExplicitCaptureMode::ByValue);
  ZC_EXPECT(stable.values()[0].captures().values()[1].mode() ==
            binder::StableExplicitCaptureMode::ByReference);
  ZC_EXPECT(stable.values()[1].captures().values().size() == 0);
  ZC_EXPECT(runtime[1].captures.size() == 0);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyPropagatesNestedClosureFreeVariables") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\nlet captured = 1;\nlet transform = () => (() => captured);\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto owner = binder::StableOwnerBodyQueryKey::from(
      semanticModule(), binder::StableBodyOwnerKey::module(semanticModule()));
  ZC_REQUIRE(owner != zc::none);
  auto materialized = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(
      ContextualBodyOwnerKey::from(roots.clone(), zc::mv(ZC_REQUIRE_NONNULL(owner))));
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.stableWitness().closures().values().size() == 2);
  ZC_REQUIRE(capability.stableWitness().closureFreeVariables().values().size() == 2);
  ZC_REQUIRE(capability.materializedClosureFreeVariables().size() == 2);
  for (const auto& stable : capability.stableWitness().closureFreeVariables().values()) {
    ZC_REQUIRE(stable.variables().values().size() == 1);
  }
  for (const auto& runtime : capability.materializedClosureFreeVariables()) {
    ZC_REQUIRE(runtime.variables.size() == 1);
    ZC_EXPECT(runtime.variables[0].referenceSites.size() == 1);
  }
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyPublishesDeferredMemberFacts") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database, "module root;\nlet result = value.member;\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t deferredMembers = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    ZC_EXPECT(body.capability().stableWitness().deferredMembers().values().size() == 1);
    for (const auto& fact : body.capability().materializedDeferredMembers()) {
      ZC_EXPECT(fact.source.belongsTo(body.capability().source()));
      ZC_EXPECT(fact.node != fact.base);
      ZC_EXPECT(fact.expectedNamespaces.size() == 1);
      ZC_EXPECT(fact.expectedNamespaces[0] == binder::Namespace::Value);
      ++deferredMembers;
    }
  }
  ZC_EXPECT(deferredMembers == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesLabelsAndControlTransfers") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "outer: while (true) { continue outer; break; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t labels = 0;
  size_t labelResolutions = 0;
  size_t explicitTransfers = 0;
  size_t loopTransfers = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    for (const auto& label : body.capability().materializedLabels()) {
      ZC_EXPECT(label.identity.belongsTo(body.capability().context()));
      ZC_EXPECT(label.name.text() == "outer"_zc);
      ZC_EXPECT(label.target.value().is<binder::LoopLabelTarget>());
      ZC_EXPECT(label.source.belongsTo(body.capability().source()));
      ++labels;
    }
    for (const auto& resolution : body.capability().materializedResolutions()) {
      if (!resolution.value.is<binder::BoundLabelResolution>()) { continue; }
      const auto& target = resolution.value.get<binder::BoundLabelResolution>();
      ZC_EXPECT(target.label.belongsTo(body.capability().context()));
      ZC_EXPECT(target.target.value().is<binder::LoopLabelTarget>());
      ++labelResolutions;
    }
    for (const auto& transfer : body.capability().materializedControlTransfers()) {
      ZC_EXPECT(transfer.source.belongsTo(body.capability().source()));
      if (transfer.target.is<binder::ExplicitLabelControlTarget>()) {
        ZC_EXPECT(transfer.kind == binder::ControlTransferKind::Continue);
        ++explicitTransfers;
        continue;
      }
      ZC_EXPECT(transfer.kind == binder::ControlTransferKind::Break);
      ZC_EXPECT(transfer.target.is<binder::LoopControlTarget>());
      ++loopTransfers;
    }
  }
  ZC_EXPECT(labels == 1);
  ZC_EXPECT(labelResolutions == 1);
  ZC_EXPECT(explicitTransfers == 1);
  ZC_EXPECT(loopTransfers == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesClosureOwnedLabels") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "let runner = () => { inner: while (true) { break inner; } };\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t closureLabels = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    for (const auto& label : body.capability().materializedLabels()) {
      ZC_EXPECT(label.name.text() == "inner"_zc);
      ZC_EXPECT(label.owner.value().is<binder::AnonymousLabelOwner>());
      ZC_EXPECT(label.identity.owner().value().is<binder::AnonymousLabelOwner>());
      ++closureLabels;
    }
  }
  ZC_EXPECT(closureLabels == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesShadowTargets") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "let value = 1;\n"
                  "{ let value = 2; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t stableShadows = 0;
  size_t materializedShadows = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    for (const auto& shadow : body.capability().shadowTargets().values()) {
      ZC_EXPECT(shadow.binding().value().is<binder::StableOwnerLocalBindingTarget>());
      ZC_EXPECT(shadow.shadowed().value().is<binder::StableOwnerLocalBindingTarget>());
      ++stableShadows;
    }
    for (const auto& shadow : body.capability().materializedShadowTargets()) {
      ZC_EXPECT(shadow.binding.value().is<binder::OwnerLocalBindingTarget>());
      ZC_EXPECT(shadow.target.value().is<binder::OwnerLocalBindingTarget>());
      ZC_EXPECT(shadow.binding.value().get<binder::OwnerLocalBindingTarget>().binding !=
                shadow.target.value().get<binder::OwnerLocalBindingTarget>().binding);
      ++materializedShadows;
    }
  }
  ZC_EXPECT(stableShadows == 1);
  ZC_EXPECT(materializedShadows == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyPublishesLexicalLookupFacts") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "let first = 1;\n"
                  "let result = first;\n"
                  "let absent = missing;\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t resolutions = 0;
  size_t materializedResolutions = 0;
  size_t failedLookups = 0;
  size_t materializedFailedLookups = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    for (const auto& resolution : body.capability().resolutions().values()) {
      ZC_EXPECT(resolution.nameSpace() == binder::Namespace::Value);
      ZC_EXPECT(resolution.origin() == binder::BindingOrigin::LocalDeclaration);
      ZC_EXPECT(resolution.binding().value().is<binder::StableOwnerLocalBindingTarget>());
      ZC_EXPECT(resolution.canonicalTarget().value().is<binder::StableOwnerLocalBindingTarget>());
      ++resolutions;
    }
    for (const auto& resolution : body.capability().materializedResolutions()) {
      ZC_REQUIRE(resolution.value.is<binder::BoundNameResolution>());
      const auto& name = resolution.value.get<binder::BoundNameResolution>();
      ZC_EXPECT(name.nameSpace == binder::Namespace::Value);
      ZC_EXPECT(name.origin == binder::BindingOrigin::LocalDeclaration);
      ZC_EXPECT(name.bindingIdentity.value().is<binder::OwnerLocalBindingTarget>());
      ZC_EXPECT(name.canonicalTarget.value().is<binder::OwnerLocalBindingTarget>());
      ++materializedResolutions;
    }
    for (const auto& failed : body.capability().failedLookups().values()) {
      ZC_EXPECT(failed.nameSpace() == binder::Namespace::Value);
      ZC_EXPECT(failed.name().text() == "missing"_zc);
      ZC_EXPECT(failed.outcome().value().is<binder::StableMissingLookupOutcome>());
      ++failedLookups;
    }
    for (const auto& failed : body.capability().materializedFailedLookups()) {
      ZC_EXPECT(failed.nameSpace == binder::Namespace::Value);
      ZC_EXPECT(failed.name.text() == "missing"_zc);
      ZC_EXPECT(failed.outcome.value().is<binder::StableMissingLookupOutcome>());
      ++materializedFailedLookups;
    }
  }
  ZC_EXPECT(resolutions == 1);
  ZC_EXPECT(materializedResolutions == 1);
  ZC_EXPECT(failedLookups == 1);
  ZC_EXPECT(materializedFailedLookups == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesThisBindings") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Host { fun make(this) { this; } }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t stableBindings = 0;
  size_t materializedBindings = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    const auto stable = body.capability().thisBindings().values();
    const auto materialized = body.capability().materializedThisBindings();
    ZC_EXPECT(stable.size() == materialized.size());
    for (size_t index = 0; index < stable.size(); ++index) {
      bool receiverMatches = false;
      for (const auto& receiver :
           body.capability().skeletonLease().capability().identities().callableParameters()) {
        if (receiver.key() != stable[index].receiver().parameter()) { continue; }
        ZC_EXPECT(materialized[index].binding.receiverParameter == receiver.handle());
        receiverMatches = true;
      }
      ZC_EXPECT(receiverMatches);
      ZC_EXPECT(materialized[index].source.belongsTo(body.capability().source()));
      ++stableBindings;
      ++materializedBindings;
    }
  }
  ZC_EXPECT(stableBindings == 1);
  ZC_EXPECT(materializedBindings == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyMaterializesThisCaptures") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(
      state, database,
      "module root;\n"
      "class Host { fun make(this) { let closure = fun() use [this] { this; }; } }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t stableCaptures = 0;
  size_t materializedCaptures = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    const auto stable = body.capability().explicitClosureCaptures().values();
    const auto materialized = body.capability().materializedExplicitClosureCaptures();
    ZC_EXPECT(stable.size() == materialized.size());
    for (size_t index = 0; index < stable.size(); ++index) {
      ZC_REQUIRE(stable[index].captures().values().size() == 1);
      ZC_REQUIRE(materialized[index].captures.size() == 1);
      ZC_EXPECT(stable[index].captures().values()[0].mode() ==
                binder::StableExplicitCaptureMode::This);
      ZC_EXPECT(materialized[index].source.belongsTo(body.capability().source()));
      ++stableCaptures;
      ++materializedCaptures;
    }
  }
  ZC_EXPECT(stableCaptures == 1);
  ZC_EXPECT(materializedCaptures == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyProjectsContextualSelfTypes") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Host { fun make(this) { let value: Self = this; } }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t contextualSelfTypes = 0;
  size_t materializedSelfTypes = 0;
  size_t failedSelfLookups = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    const auto stableSelfTypes = body.capability().selfTypes().values();
    const auto runtimeSelfTypes = body.capability().materializedSelfTypes();
    ZC_EXPECT(stableSelfTypes.size() == runtimeSelfTypes.size());
    for (const auto& selfType : stableSelfTypes) {
      ZC_EXPECT(selfType.selfOwner().value().is<binder::StableNominalSelfOwner>());
      ++contextualSelfTypes;
    }
    for (const auto& selfType : runtimeSelfTypes) {
      ZC_EXPECT(selfType.owner.is<binder::NominalSelfOwner>());
      ZC_EXPECT(selfType.source.belongsTo(body.capability().source()));
      ++materializedSelfTypes;
    }
    for (const auto& failed : body.capability().failedLookups().values()) {
      if (failed.nameSpace() == binder::Namespace::Type && failed.name().text() == "Self"_zc) {
        ++failedSelfLookups;
      }
    }
  }
  ZC_EXPECT(contextualSelfTypes == 1);
  ZC_EXPECT(materializedSelfTypes == 1);
  ZC_EXPECT(failedSelfLookups == 0);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyPublishesSingleSegmentTypeLookupFacts") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha {}\n"
                  "let typed: Alpha = 1;\n"
                  "let absent: Missing = 1;\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t resolutions = 0;
  size_t materializedResolutions = 0;
  size_t failedLookups = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    for (const auto& resolution : body.capability().resolutions().values()) {
      ZC_EXPECT(resolution.nameSpace() == binder::Namespace::Type);
      ++resolutions;
    }
    for (const auto& resolution : body.capability().materializedResolutions()) {
      ZC_REQUIRE(resolution.value.is<binder::BoundNameResolution>());
      const auto& name = resolution.value.get<binder::BoundNameResolution>();
      ZC_EXPECT(name.nameSpace == binder::Namespace::Type);
      ZC_EXPECT(name.bindingIdentity.value().is<binder::DefinitionBindingTarget>());
      ZC_EXPECT(name.canonicalTarget.value().is<binder::DefinitionBindingTarget>());
      ++materializedResolutions;
    }
    for (const auto& failed : body.capability().failedLookups().values()) {
      ZC_EXPECT(failed.nameSpace() == binder::Namespace::Type);
      ZC_EXPECT(failed.name().text() == "Missing"_zc);
      ZC_EXPECT(failed.outcome().value().is<binder::StableMissingLookupOutcome>());
      ++failedLookups;
    }
  }
  ZC_EXPECT(resolutions == 1);
  ZC_EXPECT(materializedResolutions == 1);
  ZC_EXPECT(failedLookups == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyPublishesQualifiedTypePathFacts") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha {}\n"
                  "let typed: Alpha::Item = 1;\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t rootResolutions = 0;
  size_t qualifiedMembers = 0;
  const auto& tree = verified.lease().capability().parsedSourceLease().capability().tree();
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    for (const auto& resolution : body.capability().materializedResolutions()) {
      if (!resolution.value.is<binder::BoundNameResolution>()) { continue; }
      const auto& name = resolution.value.get<binder::BoundNameResolution>();
      ZC_EXPECT(name.nameSpace == binder::Namespace::Type);
      ZC_EXPECT(name.bindingIdentity.value().is<binder::DefinitionBindingTarget>());
      ++rootResolutions;
    }
    for (const auto& member : body.capability().materializedDeferredMembers()) {
      ZC_EXPECT(tree.node(member.node).kind == ast::SyntaxKind::NamedTypeExpr);
      ZC_EXPECT(tree.node(member.base).kind == ast::SyntaxKind::ModulePath);
      ZC_EXPECT(member.member.text() == "Item"_zc);
      ZC_REQUIRE(member.expectedNamespaces.size() == 1);
      ZC_EXPECT(member.expectedNamespaces[0] == binder::Namespace::Type);
      ZC_EXPECT(member.genericArguments.size() == 0);
      ++qualifiedMembers;
    }
  }
  ZC_EXPECT(rootResolutions == 1);
  ZC_EXPECT(qualifiedMembers == 1);
}

ZC_TEST("MaterializedBindingCapabilityTest.OwnerBodyProjectsNamespaceMismatchFacts") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha {}\n"
                  "let value = Alpha;\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());

  size_t mismatches = 0;
  size_t materializedMismatches = 0;
  for (const auto& body : verified.lease().capability().ownerBodyLeases()) {
    for (const auto& failed : body.capability().failedLookups().values()) {
      ZC_EXPECT(failed.nameSpace() == binder::Namespace::Value);
      ZC_EXPECT(failed.name().text() == "Alpha"_zc);
      ZC_REQUIRE(failed.outcome().value().is<binder::StableNamespaceMismatchLookupOutcome>());
      const auto& available = failed.outcome()
                                  .value()
                                  .get<binder::StableNamespaceMismatchLookupOutcome>()
                                  .availableNamespaces;
      ZC_REQUIRE(available.values().size() == 1);
      ZC_EXPECT(available.values()[0] == binder::Namespace::Type);
      ++mismatches;
    }
    for (const auto& failed : body.capability().materializedFailedLookups()) {
      ZC_EXPECT(failed.nameSpace == binder::Namespace::Value);
      ZC_EXPECT(failed.name.text() == "Alpha"_zc);
      ZC_REQUIRE(failed.outcome.value().is<binder::StableNamespaceMismatchLookupOutcome>());
      const auto& available = failed.outcome.value()
                                  .get<binder::StableNamespaceMismatchLookupOutcome>()
                                  .availableNamespaces;
      ZC_REQUIRE(available.values().size() == 1);
      ZC_EXPECT(available.values()[0] == binder::Namespace::Type);
      ++materializedMismatches;
    }
  }
  ZC_EXPECT(mismatches == 1);
  ZC_EXPECT(materializedMismatches == 1);
}

ZC_TEST("VerifiedBoundModuleCapabilityTest.RejectsChildFailureAndLeaseLineageMutations") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "let module_value = 0;\n"
                  "fun Alpha<T>(value: T) -> T { return value; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  const auto encodedKey = key.encodeCanonical();
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());
  const auto& capability = verified.lease().capability();
  const auto& skeleton = capability.skeletonLease().capability();
  auto checkerView = graph_query::CheckerBoundModuleView::from(verified.lease().retain());
  ZC_REQUIRE(checkerView != zc::none);
  const auto& view = ZC_REQUIRE_NONNULL(checkerView);
  ZC_EXPECT(view.semanticContext() == capability.context());
  ZC_EXPECT(view.compilationUnit() == capability.compilationUnit());
  ZC_EXPECT(view.crate() == capability.crate());
  ZC_EXPECT(view.module() == capability.definitions().module());
  ZC_EXPECT(view.sourceFile().belongsTo(capability.context()));
  ZC_EXPECT(view.semanticFingerprint().digest() == capability.fingerprint().digest());
  ZC_EXPECT(view.tree().root() == capability.parsedSourceLease().capability().tree().root());
  ZC_EXPECT(view.parsedModule().receipt().digest() ==
            view.retain().parsedModule().receipt().digest());
  ZC_EXPECT(&view.definitions() == &capability.definitions());
  ZC_EXPECT(&view.bindings() == &capability.bindings());
  ZC_EXPECT(&view.bindingSurface() == &capability.bindingSurface());
  ZC_EXPECT(view.boundModuleLease().stableWitness() == verified.lease().stableWitness());
  ZC_EXPECT(capability.contextRoots() == roots);
  ZC_EXPECT(capability.module().encode().asPtr() == semanticModule().encode().asPtr());
  ZC_EXPECT(capability.revision() == sealed.revision());
  ZC_EXPECT(capability.graphLease().revision() == sealed.revision());
  ZC_EXPECT(capability.skeletonLease().revision() == sealed.revision());
  ZC_EXPECT(capability.compilationUnit().belongsTo(capability.context()));
  ZC_EXPECT(capability.crate().belongsTo(capability.context()));
  ZC_EXPECT(capability.source().belongsTo(semanticModule().crate()));
  ZC_EXPECT(capability.parsedSourceLease().revision() == sealed.revision());
  ZC_EXPECT(capability.parsedSourceLease().capability().tree().contains(
      capability.parsedSourceLease().capability().tree().root()));
  auto canonicalParsed = binder::CanonicalParsedModule::fromQueryResult(
      capability.parsedSourceLease().capability().clone());
  ZC_REQUIRE(canonicalParsed != zc::none);
  const auto& parsed = ZC_REQUIRE_NONNULL(canonicalParsed);
  ZC_EXPECT(parsed.source().encode().asPtr() == capability.source().encode().asPtr());
  ZC_EXPECT(parsed.contentDigest() == capability.parsedSourceLease().capability().contentDigest());
  ZC_EXPECT(parsed.byteLength() ==
            capability.parsedSourceLease().capability().sourceBytes().size());
  ZC_EXPECT(parsed.receipt().digest() == parsed.clone().receipt().digest());
  const auto rootSpan = parsed.rootSpan();
  ZC_EXPECT(rootSpan.belongsTo(parsed.source()));
  ZC_EXPECT(parsed.sourceLocFor(rootSpan) != zc::none);
  ZC_EXPECT(capability.ownerBodyLeases().size() == 2);
  ZC_EXPECT(capability.bindings().allocationPlan().owners().values().size() ==
            capability.ownerBodyLeases().size());
  ZC_EXPECT(capability.bindings().definitions().size() ==
            skeleton.materializedDefinitions().size());
  ZC_EXPECT(capability.bindings().genericParameters().size() ==
            skeleton.materializedGenericParameters().size());
  ZC_EXPECT(capability.bindings().callableParameters().size() ==
            skeleton.materializedCallableParameters().size());
  ZC_EXPECT(capability.bindings().implementations().size() ==
            skeleton.materializedImplementations().size());
  ZC_EXPECT(capability.bindings().scopes().size() == skeleton.materializedScopes().size());
  ZC_EXPECT(capability.bindings().moduleAliases().size() ==
            skeleton.materializedModuleAliases().size());
  ZC_EXPECT(capability.bindings().imports().size() == skeleton.materializedImports().size());
  ZC_EXPECT(capability.bindings().localExports().size() ==
            skeleton.materializedLocalExports().size());
  size_t ownerNodeScopes = 0;
  size_t ownerBindings = 0;
  size_t ownerSelfTypes = 0;
  size_t ownerThisBindings = 0;
  size_t ownerDeferredMembers = 0;
  size_t ownerLabels = 0;
  size_t ownerControlTransfers = 0;
  size_t ownerShadowTargets = 0;
  size_t ownerClosureFreeVariables = 0;
  size_t ownerExplicitClosureCaptures = 0;
  size_t ownerLocalBindings = 0;
  size_t ownerFailedLookups = 0;
  for (const auto& ownerBody : capability.ownerBodyLeases()) {
    const auto& body = ownerBody.capability();
    ownerNodeScopes += body.materializedNodeScopes().size();
    ownerBindings += body.materializedResolutions().size();
    ownerSelfTypes += body.materializedSelfTypes().size();
    ownerThisBindings += body.materializedThisBindings().size();
    ownerDeferredMembers += body.materializedDeferredMembers().size();
    ownerLabels += body.materializedLabels().size();
    ownerControlTransfers += body.materializedControlTransfers().size();
    ownerShadowTargets += body.materializedShadowTargets().size();
    ownerClosureFreeVariables += body.materializedClosureFreeVariables().size();
    ownerExplicitClosureCaptures += body.materializedExplicitClosureCaptures().size();
    ownerLocalBindings += body.materializedOwnerLocalBindings().size();
    ownerFailedLookups += body.materializedFailedLookups().size();
  }
  ZC_EXPECT(capability.bindings().nodeScopes().size() ==
            skeleton.materializedNodeScopes().size() + ownerNodeScopes);
  ZC_EXPECT(capability.bindings().nodeBindings().size() == ownerBindings);
  ZC_EXPECT(capability.bindings().selfTypes().size() == ownerSelfTypes);
  ZC_EXPECT(capability.bindings().thisBindings().size() == ownerThisBindings);
  ZC_EXPECT(capability.bindings().deferredMembers().size() == ownerDeferredMembers);
  ZC_EXPECT(capability.bindings().labels().size() == ownerLabels);
  ZC_EXPECT(capability.bindings().controlTransfers().size() == ownerControlTransfers);
  ZC_EXPECT(capability.bindings().shadowTargets().size() == ownerShadowTargets);
  ZC_EXPECT(capability.bindings().closureFreeVariables().size() == ownerClosureFreeVariables);
  ZC_EXPECT(capability.bindings().explicitClosureCaptures().size() == ownerExplicitClosureCaptures);
  ZC_EXPECT(capability.bindings().ownerLocalBindings().size() == ownerLocalBindings);
  ZC_EXPECT(capability.bindings().failedLookups().size() ==
            skeleton.materializedFailedLookups().size() + ownerFailedLookups);
  for (size_t index = 0; index < capability.ownerBodyLeases().size(); ++index) {
    ZC_EXPECT(capability.ownerBodyLeases()[index].capability().allocation() ==
              capability.bindings().allocationPlan().owners().values()[index]);
  }
  ZC_EXPECT(capability.definitions().ownerBodies().size() == 2);
  ZC_EXPECT(capability.definitions().identities().definitions().size() == 2);
  ZC_REQUIRE(capability.definitions().identities().definitions().size() == 2);
  ZC_EXPECT(capability.definitions().definition(
                capability.definitions().identities().definitions()[0].handle()) != zc::none);
  for (const auto& entry : capability.definitions().identities().implementations()) {
    ZC_EXPECT(capability.definitions().implementation(entry.handle()) != zc::none);
  }
  for (const auto& entry : capability.definitions().identities().genericParameters()) {
    ZC_EXPECT(capability.definitions().genericParameter(entry.handle()) != zc::none);
  }
  for (const auto& entry : capability.definitions().identities().callableParameters()) {
    ZC_EXPECT(capability.definitions().callableParameter(entry.handle()) != zc::none);
  }
  ZC_EXPECT(capability.definitions().definitions().size() ==
            skeleton.materializedDefinitions().size());
  ZC_EXPECT(capability.definitions().genericParameters().size() ==
            skeleton.materializedGenericParameters().size());
  ZC_EXPECT(capability.definitions().callableParameters().size() ==
            skeleton.materializedCallableParameters().size());
  ZC_EXPECT(capability.definitions().implAuthorities().size() ==
            capability.definitions().identities().implementations().size());
  ZC_EXPECT(capability.definitions().impls().size() ==
            skeleton.materializedImplementations().size());
  for (const auto& fact : skeleton.materializedDefinitions()) {
    ZC_REQUIRE(fact.site.value().is<binder::DeclarationDefinitionSite>());
    ZC_EXPECT(capability.definitions().definitionAt(
                  fact.site.value().get<binder::DeclarationDefinitionSite>().node) ==
              fact.identity);
  }
  for (const auto& fact : skeleton.materializedGenericParameters()) {
    ZC_REQUIRE(fact.site.value().is<binder::DeclarationDefinitionSite>());
    ZC_EXPECT(capability.definitions().genericParameterAt(
                  fact.site.value().get<binder::DeclarationDefinitionSite>().node) ==
              fact.identity);
  }
  for (const auto& fact : skeleton.materializedCallableParameters()) {
    ZC_REQUIRE(fact.site.value().is<binder::DeclarationDefinitionSite>());
    ZC_EXPECT(capability.definitions().callableParameterAt(
                  fact.site.value().get<binder::DeclarationDefinitionSite>().node) ==
              fact.identity);
  }
  for (const auto& fact : skeleton.materializedImplementations()) {
    ZC_EXPECT(capability.definitions().implementationAt(fact.node) == fact.occurrence);
    ZC_EXPECT(capability.definitions().implementationAuthority(fact.occurrence) == fact.authority);
  }
  size_t materializedOwnerLocalCount = 0;
  for (const auto& ownerBody : capability.ownerBodyLeases()) {
    materializedOwnerLocalCount += ownerBody.capability().materializedOwnerLocalBindings().size();
    for (const auto& fact : ownerBody.capability().materializedOwnerLocalBindings()) {
      ZC_EXPECT(capability.definitions().ownerLocalBindingAt(fact.node) == fact.identity);
    }
    for (const auto& fact : ownerBody.capability().materializedAnonymousEntities()) {
      auto entry = capability.definitions().anonymousEntityAt(fact.node, fact.key.role());
      ZC_REQUIRE(entry != zc::none);
      ZC_EXPECT(ZC_REQUIRE_NONNULL(entry).entity == fact.identity);
      ZC_EXPECT(ZC_REQUIRE_NONNULL(entry).key == fact.key);
    }
  }
  ZC_EXPECT(capability.definitions().ownerLocalBindings().size() == materializedOwnerLocalCount);
  ZC_EXPECT(capability.bindings().ownerBodies().size() == 2);
  ZC_EXPECT(capability.bindings().skeleton().module().encode().asPtr() ==
            semanticModule().encode().asPtr());
  ZC_EXPECT(capability.bindings().allocationPlan().key().encode().asPtr() ==
            semanticModule().encode().asPtr());
  ZC_EXPECT(capability.bindings().allocationPlan().owners().values().size() ==
            capability.ownerBodyLeases().size());
  zc::Vector<binder::BoundOwnerBody> missingDefinitionBodies;
  zc::Vector<binder::OwnerLocalBindingFact> missingOwnerLocalBindings;
  zc::Vector<binder::AnonymousEntityFact> missingAnonymousEntities;
  auto incompleteDefinitions = binder::ImmutableDefinitionInventory::from(
      capability.definitions().identities().clone(), zc::mv(missingDefinitionBodies),
      capability.skeletonLease().capability().materializedDefinitions(),
      capability.skeletonLease().capability().materializedGenericParameters(),
      capability.skeletonLease().capability().materializedCallableParameters(),
      capability.skeletonLease().capability().materializedImplementations(),
      missingOwnerLocalBindings.asPtr(), missingAnonymousEntities.asPtr());
  ZC_EXPECT(incompleteDefinitions == zc::none);
  zc::Vector<binder::BoundOwnerBody> missingBindingBodies;
  const binder::MaterializedBindingFacts emptyBindingFacts{};
  auto incompleteBindings = binder::ImmutableBindingMetadata::from(
      capability.context(), capability.revision(), capability.fingerprint(),
      capability.bindings().skeleton().clone(), zc::mv(missingBindingBodies), emptyBindingFacts);
  ZC_EXPECT(incompleteBindings == zc::none);

  zc::Vector<graph_query::VerifiedBoundModule::OwnerBodyLease> missingOwners;
  auto invalidCandidate = graph_query::VerifiedBoundModule::from(
      capability.graphLease().retain(), capability.skeletonLease().retain(),
      capability.source().clone(), capability.parsedSourceLease().retain(), zc::mv(missingOwners));
  ZC_EXPECT(invalidCandidate == zc::none);

  auto foreignKey = ContextualModuleKey::from(roots.clone(), namedSemanticModule("foreign"_zc));
  auto rejected = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(foreignKey));
  ZC_REQUIRE(rejected.isRuntimeRejected());
  ZC_EXPECT(rejected.runtimeFailure() == query::QueryRuntimeFailure::InvariantViolation);

  auto wrongDomain = mutation::flipByte(encodedKey.asPtr(), 0);
  ZC_EXPECT(graph_query::VerifyBoundModuleQuery::decodeKey(wrongDomain.asPtr()) == zc::none);
  auto trailing = mutation::withTrailingByte(encodedKey.asPtr());
  ZC_EXPECT(graph_query::VerifyBoundModuleQuery::decodeKey(trailing.asPtr()) == zc::none);
  ZC_EXPECT(graph_query::VerifyBoundModuleQuery::decodeKey(
                encodedKey.asPtr().slice(0, encodedKey.size() - 1)) == zc::none);
}

ZC_TEST("CheckerBoundModuleView retains its verified bound-module lease") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha {}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);

  auto view = [&]() {
    auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
    auto demand = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
    ZC_REQUIRE(demand.isPublished());
    auto candidate = graph_query::CheckerBoundModuleView::from(zc::mv(demand).takeLease());
    ZC_REQUIRE(candidate != zc::none);
    return zc::mv(ZC_REQUIRE_NONNULL(candidate));
  }();

  auto retained = view.retain();
  ZC_EXPECT(view.module() == retained.module());
  ZC_EXPECT(view.sourceFile() == retained.sourceFile());
  ZC_EXPECT(view.semanticFingerprint().digest() == retained.semanticFingerprint().digest());
  ZC_EXPECT(view.tree().root() == retained.tree().root());
  ZC_EXPECT(view.parsedModule().receipt().digest() == retained.parsedModule().receipt().digest());
  ZC_EXPECT(&view.definitions() == &retained.definitions());
  ZC_EXPECT(&view.bindings() == &retained.bindings());
  ZC_EXPECT(&view.bindingSurface() == &retained.bindingSurface());
  ZC_EXPECT(view.boundModuleLease().stableWitness() == retained.boundModuleLease().stableWitness());
}

ZC_TEST("CheckerIdentityAuthority resolves every retained identity domain") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "class Alpha<T> {}\n"
                  "impl Trait for Alpha {}\n"
                  "fun Beta<T>(value: T) -> T { return value; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);

  zc::Vector<checker::CheckerIdentityAuthority::BoundModuleView> modules;
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto demand = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(demand.isPublished());
  auto view = graph_query::CheckerBoundModuleView::from(zc::mv(demand).takeLease());
  ZC_REQUIRE(view != zc::none);
  modules.add(zc::mv(ZC_REQUIRE_NONNULL(view)));

  const auto& supplied = modules[0];
  const auto& suppliedBound = supplied.boundModuleLease().capability();
  const auto& suppliedGraph = suppliedBound.graphLease().capability();
  const size_t graphModuleCount = suppliedGraph.modules().size();
  const auto suppliedModule = supplied.module();
  zc::Vector<identity::ModuleId> graphModules;
  for (const auto& entry : suppliedGraph.modules()) { graphModules.add(entry.handle()); }
  ZC_EXPECT(supplied.semanticContext() == suppliedGraph.context());
  ZC_EXPECT(supplied.semanticFingerprint().digest() ==
            suppliedGraph.witness().fingerprint().digest());
  ZC_EXPECT(suppliedBound.graphLease().stableWitness() ==
            supplied.boundModuleLease().capability().graphLease().stableWitness());
  ZC_EXPECT(suppliedBound.revision() == suppliedGraph.revision());
  bool foundCompilationUnitMembership = false;
  bool foundCrateMembership = false;
  bool foundModuleMembership = false;
  bool foundSourceMembership = false;
  for (const auto& entry : suppliedGraph.units()) {
    if (entry.handle() == supplied.compilationUnit()) { foundCompilationUnitMembership = true; }
  }
  for (const auto& entry : suppliedGraph.crates()) {
    if (entry.handle() == supplied.crate()) { foundCrateMembership = true; }
  }
  for (const auto& entry : suppliedGraph.modules()) {
    if (entry.handle() != supplied.module()) { continue; }
    ZC_EXPECT(entry.key().encode().asPtr() == suppliedBound.module().encode().asPtr());
    foundModuleMembership = true;
  }
  for (const auto& entry : suppliedGraph.sources()) {
    if (entry.handle() != supplied.sourceFile()) { continue; }
    ZC_EXPECT(entry.key().encode().asPtr() == suppliedBound.source().encode().asPtr());
    foundSourceMembership = true;
  }
  ZC_REQUIRE(foundCompilationUnitMembership);
  ZC_REQUIRE(foundCrateMembership);
  ZC_REQUIRE(foundModuleMembership);
  ZC_REQUIRE(foundSourceMembership);

  for (const auto& entry : suppliedGraph.modules()) {
    if (entry.handle() == supplied.module()) { continue; }
    auto moduleKey = ContextualModuleKey::from(roots.clone(), entry.key().clone());
    auto moduleDemand =
        sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(moduleKey));
    ZC_REQUIRE(moduleDemand.isPublished());
    auto moduleView = graph_query::CheckerBoundModuleView::from(zc::mv(moduleDemand).takeLease());
    ZC_REQUIRE(moduleView != zc::none);
    modules.add(zc::mv(ZC_REQUIRE_NONNULL(moduleView)));
  }
  ZC_REQUIRE(modules.size() == graphModuleCount);

  auto candidate = checker::CheckerIdentityAuthority::from(zc::mv(modules));
  ZC_REQUIRE(candidate != zc::none);
  const auto& authority = ZC_REQUIRE_NONNULL(candidate);
  ZC_REQUIRE(authority.modules().size() == graphModuleCount);
  for (size_t index = 0; index < authority.modules().size(); ++index) {
    ZC_EXPECT(authority.modules()[index].module() == graphModules[index]);
  }
  const auto& module = ZC_REQUIRE_NONNULL(authority.boundModule(suppliedModule));
  ZC_EXPECT(authority.semanticContext() == module.semanticContext());
  ZC_EXPECT(authority.revision() == module.boundModuleLease().capability().revision());
  ZC_EXPECT(authority.fingerprint().digest() == module.semanticFingerprint().digest());
  ZC_EXPECT(authority.compilationUnit(module.compilationUnit()) != zc::none);
  ZC_EXPECT(authority.crate(module.crate()) != zc::none);
  ZC_EXPECT(authority.sourceFile(module.sourceFile()) != zc::none);
  ZC_EXPECT(authority.module(module.module()) != zc::none);
  ZC_EXPECT(authority.compilationUnit(
                ZC_REQUIRE_NONNULL(authority.compilationUnit(module.compilationUnit())).key()) !=
            zc::none);
  ZC_EXPECT(authority.crate(ZC_REQUIRE_NONNULL(authority.crate(module.crate())).key()) != zc::none);
  ZC_EXPECT(authority.sourceFile(
                ZC_REQUIRE_NONNULL(authority.sourceFile(module.sourceFile())).key()) != zc::none);
  ZC_EXPECT(authority.module(ZC_REQUIRE_NONNULL(authority.module(module.module())).key()) !=
            zc::none);
  ZC_EXPECT(&ZC_REQUIRE_NONNULL(authority.boundModule(module.module())) == &module);
  for (const auto& entry : module.definitions().identities().definitions()) {
    ZC_EXPECT(authority.definition(entry.handle()) != zc::none);
    ZC_EXPECT(authority.definition(entry.key()) != zc::none);
  }
  bool foundCallableAuthority = false;
  for (const auto& entry : module.definitions().identities().definitions()) {
    if (entry.record().name() != "Beta"_zc) { continue; }
    ZC_REQUIRE(!foundCallableAuthority);
    auto definitionAuthority = authority.definitionAuthority(entry.handle());
    ZC_REQUIRE(definitionAuthority != zc::none);
    ZC_IF_SOME(value, definitionAuthority) {
      ZC_EXPECT(value.verify());
      ZC_REQUIRE(value.overloadHeaderAuthority() != zc::none);
      ZC_IF_SOME(header, value.overloadHeaderAuthority()) {
        ZC_EXPECT(header.verify());
        ZC_EXPECT(header.header().parameters().size() == 1);
      }
    }
    foundCallableAuthority = true;
  }
  ZC_REQUIRE(foundCallableAuthority);
  for (const auto& entry : module.definitions().identities().implementations()) {
    ZC_EXPECT(authority.implementation(entry.handle()) != zc::none);
    ZC_EXPECT(authority.implementation(entry.key()) != zc::none);
  }
  for (const auto& entry : module.definitions().identities().genericParameters()) {
    ZC_EXPECT(authority.genericParameter(entry.handle()) != zc::none);
    ZC_EXPECT(authority.genericParameter(entry.key()) != zc::none);
  }
  for (const auto& entry : module.definitions().identities().callableParameters()) {
    ZC_EXPECT(authority.callableParameter(entry.handle()) != zc::none);
    ZC_EXPECT(authority.callableParameter(entry.key()) != zc::none);
  }

  ZC_REQUIRE(authority.modules().size() > 1);
  zc::Vector<checker::CheckerIdentityAuthority::BoundModuleView> incompleteModules;
  zc::Vector<checker::CheckerIdentityAuthority::BoundModuleView> repeatedModules;
  zc::Vector<checker::CheckerIdentityAuthority::BoundModuleView> reorderedModules;
  for (size_t index = 0; index + 1 < authority.modules().size(); ++index) {
    incompleteModules.add(authority.modules()[index].retain());
    repeatedModules.add(authority.modules()[index].retain());
  }
  for (size_t index = 1; index < authority.modules().size(); ++index) {
    reorderedModules.add(authority.modules()[index].retain());
  }
  repeatedModules.add(authority.modules()[0].retain());
  reorderedModules.add(authority.modules()[0].retain());
  ZC_EXPECT(checker::CheckerIdentityAuthority::from(zc::mv(incompleteModules)) == zc::none);
  ZC_EXPECT(checker::CheckerIdentityAuthority::from(zc::mv(repeatedModules)) == zc::none);
  ZC_EXPECT(checker::CheckerIdentityAuthority::from(zc::mv(reorderedModules)) == zc::none);
}

ZC_TEST("CheckerIdentityAuthority rejects a repeated module in a complete graph") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  constexpr auto source = "module root;\nimport dependency::{Dependency};\n"_zc;
  constexpr auto dependencySource = "module root;\nexport class Dependency {}\n"_zc;
  stageBaseInputs(state, database, source, "authority"_zc, true, dependencySource, true);

  auto opened = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  stageDependencyImportResolutionInputs(transaction);
  ZC_REQUIRE(transaction.commit().isCommitted());
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots, true);

  zc::Vector<checker::CheckerIdentityAuthority::BoundModuleView> repeated;
  for (size_t index = 0; index < 2; ++index) {
    auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
    auto demand = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
    ZC_REQUIRE(demand.isPublished());
    auto view = graph_query::CheckerBoundModuleView::from(zc::mv(demand).takeLease());
    ZC_REQUIRE(view != zc::none);
    repeated.add(zc::mv(ZC_REQUIRE_NONNULL(view)));
  }
  ZC_EXPECT(checker::CheckerIdentityAuthority::from(zc::mv(repeated)) == zc::none);
}

ZC_TEST("VerifiedBoundModuleCapabilityTest.MaterializesGenericNominalDeclarations") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  stageBaseInputs(state, database,
                  "module root;\n"
                  "interface Structural {}\n"
                  "struct GenericBox<T> { pair: (T, T); }\n"
                  "enum GenericChoice<T> { Single(T), Pair(T, T), Empty }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));
  auto sealed = sealDatabase(database, roots);
  auto key = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(key));
  ZC_REQUIRE(verified.isPublished());
  const auto& capability = verified.lease().capability();
  ZC_EXPECT(capability.definitions().definitions().size() == 7);
  ZC_EXPECT(capability.bindings().genericParameters().size() == 2);
  ZC_EXPECT(capability.skeletonLease().capability().materializedDefinitions().size() == 7);
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
                  "export class Alpha {}\n"
                  "class Gamma {}\n"
                  "export { Gamma as PublicGamma };\n"
                  "impl Trait for Alpha {}\n"
                  "fun Beta() { let value = 1; }\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  zc::Maybe<identity::DefinitionKey> alphaKey;
  {
    auto staging = database.snapshot();
    auto inventory = staging.get<NamedDefinitionInventoryQuery>(module);
    ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(inventory.value().entries().size() == 3);
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
  ZC_EXPECT(definitions.lease().capability().entries().size() == 3);
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

  auto skeleton = sealed.get<binder::BindModuleSkeleton>(semanticModule());
  ZC_REQUIRE(skeleton.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(
      skeleton.value().storage().is<binder::BinderQueryValue<binder::BoundModuleSkeleton>>());
  const auto& skeletonValue =
      skeleton.value().storage().get<binder::BinderQueryValue<binder::BoundModuleSkeleton>>();
  ZC_EXPECT(skeletonValue.diagnostics.values().size() == 0);
  ZC_EXPECT(skeletonValue.value.module().encode().asPtr() == semanticModule().encode().asPtr());
  ZC_EXPECT(skeletonValue.value.declarations().values().size() == 3);
  ZC_EXPECT(skeletonValue.value.implementationOccurrences().values().size() == 1);
  ZC_EXPECT(skeletonValue.value.bodyOwners().values().size() == 2);
  ZC_EXPECT(skeletonValue.value.scopes().values().size() > 0);
  ZC_EXPECT(skeletonValue.value.nodeScopes().values().size() ==
            moduleProvenance.lease().capability().entries().size());
  auto skeletonBytes = binder::BindModuleSkeleton::encodeValue(skeleton.value());
  auto decodedSkeleton = binder::BindModuleSkeleton::decodeValue(skeletonBytes.asPtr());
  ZC_REQUIRE(decodedSkeleton != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSkeleton) == skeleton.value());

  auto exportNames = sealed.get<binder::ModuleExportNames>(semanticModule());
  ZC_REQUIRE(exportNames.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(exportNames.value().values().size() == 2);
  bool foundAlpha = false;
  bool foundPublicGamma = false;
  for (const auto& name : exportNames.value().values()) {
    foundAlpha = foundAlpha || name.name().text() == "Alpha"_zc;
    foundPublicGamma = foundPublicGamma || name.name().text() == "PublicGamma"_zc;
  }
  ZC_EXPECT(foundAlpha);
  ZC_EXPECT(foundPublicGamma);
  auto exported = sealed.get<binder::ExportedBinding>(binder::StableExportedBindingQueryKey::from(
      semanticModule(), exportNames.value().values()[0].clone()));
  ZC_REQUIRE(exported.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(exported.value().name().nameSpace() == exportNames.value().values()[0].nameSpace());
  ZC_EXPECT(exported.value().name().name().text() == exportNames.value().values()[0].name().text());
  ZC_EXPECT(exported.value().exported());
  auto exportNamesBytes = binder::ModuleExportNames::encodeValue(exportNames.value());
  auto decodedExportNames = binder::ModuleExportNames::decodeValue(exportNamesBytes.asPtr());
  ZC_REQUIRE(decodedExportNames != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedExportNames) == exportNames.value());

  const auto& implementationOccurrence =
      skeletonValue.value.implementationOccurrences().values()[0];
  auto implementationBinding =
      sealed.get<binder::ImplementationBindingHeader>(implementationOccurrence.authority().clone());
  ZC_REQUIRE(implementationBinding.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(implementationBinding.value().values().size() == 1);
  ZC_EXPECT(implementationBinding.value().values()[0] == implementationOccurrence);
  auto implementationBindingBytes =
      binder::ImplementationBindingHeader::encodeValue(implementationBinding.value());
  auto decodedImplementationBinding =
      binder::ImplementationBindingHeader::decodeValue(implementationBindingBytes.asPtr());
  ZC_REQUIRE(decodedImplementationBinding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedImplementationBinding) == implementationBinding.value());

  const auto& declaration = skeletonValue.value.declarations().values()[0];
  auto definitionBinding =
      sealed.get<binder::DefinitionBindingHeader>(declaration.queryKey().clone());
  ZC_REQUIRE(definitionBinding.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(definitionBinding.value() == declaration);
  auto definitionBindingBytes =
      binder::DefinitionBindingHeader::encodeValue(definitionBinding.value());
  auto decodedDefinitionBinding =
      binder::DefinitionBindingHeader::decodeValue(definitionBindingBytes.asPtr());
  ZC_REQUIRE(decodedDefinitionBinding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedDefinitionBinding) == definitionBinding.value());

  auto definitionVisibility = sealed.get<binder::BindingVisibility>(
      binder::StableBindingTargetKey::definition(declaration.queryKey().clone()));
  ZC_REQUIRE(definitionVisibility.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(definitionVisibility.value() == declaration.visibility());

  auto moduleVisibility = sealed.get<binder::BindingVisibility>(
      binder::StableBindingTargetKey::module(semanticModule()));
  ZC_REQUIRE(moduleVisibility.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(moduleVisibility.value() != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(moduleVisibility.value()) == binder::MemberVisibility::Public);

  auto bucketName =
      binder::BindingNameKey::from(declaration.nameSpace(), declaration.name().clone());
  ZC_REQUIRE(bucketName != zc::none);
  auto bucketKey = binder::StableScopeNameBucketQueryKey::from(
      binder::StableScopeOwnerKey::module(semanticModule()),
      zc::mv(ZC_REQUIRE_NONNULL(bucketName)));
  ZC_REQUIRE(bucketKey != zc::none);
  auto bucket = sealed.get<binder::ScopeNameBucket>(ZC_REQUIRE_NONNULL(bucketKey));
  ZC_REQUIRE(bucket.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(bucket.value().values().size() == 1);
  ZC_EXPECT(bucket.value().values()[0] ==
            binder::StableBindingTargetKey::definition(declaration.queryKey().clone()));

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
    auto boundBody = sealed.get<binder::BindOwnerBody>(bodyKey);
    ZC_REQUIRE(bodyProvenance.isKeyRejected());
    ZC_EXPECT(bodyProvenance.keyFailure().kind() ==
              binder::BinderKeyFailureKind::DefinitionWithoutBody);
    ZC_REQUIRE(boundBody.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(boundBody.value().storage().is<binder::BinderKeyRejected>());
    ZC_EXPECT(boundBody.value().storage().get<binder::BinderKeyRejected>().failure.kind() ==
              binder::BinderKeyFailureKind::DefinitionWithoutBody);
    foundDefinitionWithoutBody = true;
  }
  ZC_EXPECT(foundDefinitionWithoutBody);

  auto allocation =
      sealed.get<binder::ModuleBindingAllocationPlanQuery>(contextual(roots, semanticModule()));
  ZC_REQUIRE(allocation.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(allocation.value()
                 .storage()
                 .is<binder::BinderQueryValue<binder::ModuleBindingAllocationPlan>>());
  const auto& allocationValue =
      allocation.value()
          .storage()
          .get<binder::BinderQueryValue<binder::ModuleBindingAllocationPlan>>();
  ZC_EXPECT(allocationValue.diagnostics.values().size() == 0);
  ZC_EXPECT(allocationValue.value.owners().values().size() ==
            skeletonValue.value.bodyOwners().values().size());
}

ZC_TEST("Final authority admits module alias source dependencies") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  constexpr auto source = "module root = dependency::root;\n"_zc;
  stageBaseInputs(state, database, source, "authority"_zc, true);

  auto opened = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  stageModuleAliasResolutionInputs(transaction);
  stageModuleAliasTargetInputs(transaction);
  auto sites = moduleAliasDependencySites(semanticModule(),
                                          namedSource("root.zom"_zc, "authority"_zc), source);
  ZC_REQUIRE(
      transaction.set<graph_query::ModuleDependencySiteInput>(semanticModule(), zc::mv(sites))
          .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto module = stableModule();
  auto staging = database.snapshot();
  auto dependencies = staging.get<graph_query::ModuleDependenciesQuery>(semanticModule());
  ZC_REQUIRE(dependencies.kind() == query::QueryValueKind::Value);
  auto admission = staging.getCapability<StableIdentityAdmissionQuery>(module);
  ZC_REQUIRE(!admission.isRuntimeRejected());
  ZC_REQUIRE(!admission.isKeyRejected());
  ZC_REQUIRE(!admission.isSourceRejected());
  ZC_REQUIRE(admission.isPublished());
  auto definitions = staging.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(definitions.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto sealed = sealDatabase(database, roots, true);
  auto targetExports = sealed.get<binder::ModuleExportNames>(dependencyAliasTarget());
  ZC_REQUIRE(!targetExports.isRuntimeFailure());
  ZC_REQUIRE(targetExports.kind() == query::QueryValueKind::Value);
  auto dependencyProvenance =
      sealed.getCapability<graph_query::ModuleDependencyProvenanceQuery>(semanticModule());
  ZC_REQUIRE(dependencyProvenance.isPublished());
  ZC_REQUIRE(dependencyProvenance.lease().capability().entries().size() == 1);
  ZC_REQUIRE(dependencyProvenance.lease().capability().entries()[0].origin().sites().size() == 1);
  auto definitionSites = sealed.getCapability<RevisionLocalDefinitionSitesQuery>(module);
  ZC_REQUIRE(definitionSites.isPublished());
  ZC_REQUIRE(definitionSites.lease().capability().entries().size() == 1);
  ZC_EXPECT(dependencyProvenance.lease().capability().entries()[0].origin().sites()[0].node() ==
            definitionSites.lease().capability().entries()[0].node());
  auto sealedDefinitions = sealed.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(sealedDefinitions.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(sealedDefinitions.value().entries().size() == 1);
  auto aliasKey = binder::StableDefinitionQueryKey::from(
      semanticModule(), sealedDefinitions.value().entries()[0].key().clone());
  auto aliasHeader = sealed.get<binder::DefinitionHeaderSyntax>(aliasKey);
  ZC_REQUIRE(!aliasHeader.isRuntimeFailure());
  ZC_REQUIRE(aliasHeader.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(
      aliasHeader.value().storage().is<binder::BinderQueryValue<binder::StableDefinitionHeader>>());
  const auto& header =
      aliasHeader.value().storage().get<binder::BinderQueryValue<binder::StableDefinitionHeader>>();
  ZC_REQUIRE(header.diagnostics.values().size() == 0);
  ZC_REQUIRE(header.value.kind() == identity::DefinitionKind::ModuleAlias);
  ZC_REQUIRE(header.value.activation() == binder::DefinitionActivation::ImportSurface);
  ZC_REQUIRE(header.value.declaredScopeRoles().values().size() == 1);
  ZC_EXPECT(header.value.declaredScopeRoles().values()[0] == binder::ScopeRole::Declaration);
  auto moduleProvenance = sealed.getCapability<ModuleBodyProvenanceQuery>(module);
  ZC_REQUIRE(moduleProvenance.isPublished());
  ZC_REQUIRE(moduleProvenance.lease().capability().entries().size() == 0);
  auto skeleton = sealed.get<binder::BindModuleSkeleton>(semanticModule());
  ZC_REQUIRE(!skeleton.isRuntimeFailure());
  ZC_REQUIRE(skeleton.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(
      skeleton.value().storage().is<binder::BinderQueryValue<binder::BoundModuleSkeleton>>());
  const auto& value =
      skeleton.value().storage().get<binder::BinderQueryValue<binder::BoundModuleSkeleton>>();
  ZC_REQUIRE(value.diagnostics.values().size() == 0);
  ZC_REQUIRE(value.value.moduleAliases().values().size() == 1);
  ZC_EXPECT(value.value.moduleAliases().values()[0].canonicalModule().encode().asPtr() ==
            dependencyAliasTarget().encode().asPtr());

  auto aliasMembershipKey = ContextualDefinitionKey::from(
      roots.clone(), value.value.declarations().values()[0].queryKey().clone());
  auto aliasMembership = sealed.get<ActiveDefinitionMembershipQuery>(zc::mv(aliasMembershipKey));
  ZC_REQUIRE(aliasMembership.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(aliasMembership.value().isActive());

  auto materializedKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(materializedKey));
  ZC_EXPECT(!materialized.isSourceRejected());
  ZC_EXPECT(!materialized.isKeyRejected());
  ZC_EXPECT(!materialized.isRuntimeRejected());
  ZC_REQUIRE(materialized.isPublished());
  const auto& materializedCapability = materialized.lease().capability();
  ZC_REQUIRE(materializedCapability.dependencySkeletonLeases().size() == 1);
  ZC_EXPECT(materializedCapability.dependencySkeletonLeases()[0].revision() ==
            materializedCapability.revision());
  ZC_EXPECT(materializedCapability.dependencySkeletonLeases()[0].capability().contextRoots() ==
            roots);
  ZC_EXPECT(
      materializedCapability.dependencySkeletonLeases()[0].capability().module().encode().asPtr() ==
      dependencyAliasTarget().encode().asPtr());
  const auto& materializedAliases = materialized.lease().capability().materializedModuleAliases();
  ZC_REQUIRE(materializedAliases.size() == 1);
  ZC_EXPECT(materializedAliases[0].canonicalTarget.belongsTo(
      materialized.lease().capability().context()));
  ZC_EXPECT(materializedAliases[0].targetExportNamesRevision.digest() ==
            value.value.moduleAliases().values()[0].targetExportNamesRevision().digest());
  bool aliasScopeBinding = false;
  for (const auto& scope : materialized.lease().capability().materializedScopes()) {
    for (const auto& binding : scope.bindings) {
      const auto& bindingIdentity = binding.binding.bindingIdentity.value();
      const auto& canonicalTarget = binding.binding.canonicalTarget.value();
      if (binding.name.nameSpace() == binder::Namespace::Module &&
          bindingIdentity.is<binder::DefinitionBindingTarget>() &&
          canonicalTarget.is<binder::ModuleBindingTarget>() &&
          bindingIdentity.get<binder::DefinitionBindingTarget>().definition ==
              materializedAliases[0].alias &&
          canonicalTarget.get<binder::ModuleBindingTarget>().module ==
              materializedAliases[0].canonicalTarget) {
        aliasScopeBinding = true;
      }
    }
  }
  ZC_EXPECT(aliasScopeBinding);

  auto verifiedKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(verifiedKey));
  ZC_REQUIRE(verified.isPublished());
  auto view = graph_query::CheckerBoundModuleView::from(verified.lease().retain());
  ZC_REQUIRE(view != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(view).dependencySurfaces().size() == 1);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(view).resolvedImports().size() == 0);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(view).resolvedModuleAliases().size() == 1);
  ZC_REQUIRE(verified.lease().capability().bindings().skeleton().moduleAliases().values().size() ==
             1);
  ZC_EXPECT(verified.lease()
                .capability()
                .bindings()
                .skeleton()
                .moduleAliases()
                .values()[0]
                .canonicalModule()
                .encode()
                .asPtr() == dependencyAliasTarget().encode().asPtr());
  ZC_EXPECT(verified.lease().capability().definitions().definition(materializedAliases[0].alias) !=
            zc::none);
}

ZC_TEST("Materialized skeleton retains dependency import surface") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  constexpr auto source = "module root;\nimport dependency::{Dependency};\n"_zc;
  constexpr auto dependencySource = "module root;\nexport class Dependency {}\n"_zc;
  stageBaseInputs(state, database, source, "authority"_zc, true, dependencySource, true);

  auto opened = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  stageDependencyImportResolutionInputs(transaction);
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto staging = database.snapshot();
  auto dependencies = staging.get<graph_query::ModuleDependenciesQuery>(semanticModule());
  ZC_REQUIRE(dependencies.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(dependencies.value().dependencies().size() == 1);
  ZC_EXPECT(dependencies.value().dependencies()[0].encode().asPtr() ==
            semanticModule("dependency"_zc).encode().asPtr());
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto sealed = sealDatabase(database, roots, true);
  auto dependencyExports = sealed.get<binder::ModuleExportNames>(semanticModule("dependency"_zc));
  ZC_REQUIRE(dependencyExports.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(dependencyExports.value().values().size() == 1);
  auto dependencyName = binder::BindingNameKey::from(
      binder::Namespace::Type, scalar<identity::DeclaredDefinitionName>("Dependency"_zc));
  ZC_REQUIRE(dependencyName != zc::none);
  auto dependencyBindingKey = binder::StableExportedBindingQueryKey::from(
      semanticModule("dependency"_zc), zc::mv(ZC_REQUIRE_NONNULL(dependencyName)));
  auto dependencyBinding = sealed.get<binder::ExportedBinding>(zc::mv(dependencyBindingKey));
  ZC_REQUIRE(dependencyBinding.kind() == query::QueryValueKind::Value);
  auto stableSkeleton = sealed.get<binder::BindModuleSkeleton>(semanticModule());
  ZC_EXPECT(!stableSkeleton.isRuntimeFailure());
  ZC_EXPECT(stableSkeleton.kind() != query::QueryValueKind::SemanticFailure);
  ZC_REQUIRE(stableSkeleton.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(
      stableSkeleton.value().storage().is<binder::BinderQueryValue<binder::BoundModuleSkeleton>>());
  ZC_REQUIRE(stableSkeleton.value()
                 .storage()
                 .get<binder::BinderQueryValue<binder::BoundModuleSkeleton>>()
                 .value.imports()
                 .values()
                 .size() == 1);
  auto dependencyKey = ContextualModuleKey::from(roots.clone(), semanticModule("dependency"_zc));
  auto dependency =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(dependencyKey));
  ZC_REQUIRE(dependency.isPublished());
  auto materializedKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(materializedKey));
  ZC_EXPECT(!materialized.isSourceRejected());
  ZC_EXPECT(!materialized.isKeyRejected());
  ZC_EXPECT(!materialized.isRuntimeRejected());
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.dependencySkeletonLeases().size() == 1);
  ZC_REQUIRE(capability.dependencySurfaces().size() == 1);
  ZC_EXPECT(capability.dependencySurfaces()[0].moduleKey.encode().asPtr() ==
            semanticModule("dependency"_zc).encode().asPtr());
  ZC_EXPECT(
      capability.dependencySurfaces()[0].surface.revision().digest() ==
      capability.dependencySkeletonLeases()[0].capability().bindingSurface().revision().digest());
  ZC_REQUIRE(capability.materializedImports().size() == 1);
  const auto& import = capability.materializedImports()[0];
  ZC_EXPECT(import.kind == binder::ImportBindingKind::Import);
  ZC_EXPECT(import.sourceModule ==
            capability.dependencySkeletonLeases()[0].capability().identities().module());
  ZC_EXPECT(import.canonicalTarget.value().is<binder::DefinitionBindingTarget>());

  auto verifiedKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto verified = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(verifiedKey));
  ZC_REQUIRE(verified.isPublished());
  auto view = graph_query::CheckerBoundModuleView::from(verified.lease().retain());
  ZC_REQUIRE(view != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(view).dependencySurfaces().size() == 1);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(view).preludeSurface() == zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(view).resolvedImports().size() == 1);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(view).resolvedModuleAliases().size() == 0);
  auto retained = ZC_REQUIRE_NONNULL(view).retain();
  ZC_EXPECT(retained.dependencySurfaces().size() == 1);
  ZC_EXPECT(retained.preludeSurface() == zc::none);
  ZC_EXPECT(retained.resolvedImports().size() == 1);
  ZC_EXPECT(retained.resolvedModuleAliases().size() == 0);

  bool importScopeBinding = false;
  for (const auto& scope : capability.materializedScopes()) {
    for (const auto& binding : scope.bindings) {
      const auto& bindingIdentity = binding.binding.bindingIdentity.value();
      if (binding.binding.origin != binder::BindingOrigin::ImportAlias ||
          !bindingIdentity.is<binder::SemanticImportBindingTarget>()) {
        continue;
      }
      ZC_EXPECT(bindingIdentity.get<binder::SemanticImportBindingTarget>().binding ==
                import.binding);
      ZC_EXPECT(binding.binding.canonicalTarget.value().is<binder::DefinitionBindingTarget>());
      importScopeBinding = true;
    }
  }
  ZC_EXPECT(importScopeBinding);

  const auto& aggregate = verified.lease().capability();
  ZC_EXPECT(aggregate.skeletonLease().stableWitness() == materialized.lease().stableWitness());
  ZC_REQUIRE(aggregate.skeletonLease().capability().dependencySkeletonLeases().size() == 1);
  ZC_EXPECT(aggregate.skeletonLease().capability().dependencySkeletonLeases()[0].stableWitness() ==
            dependency.lease().stableWitness());
}

ZC_TEST("Verified bound module materializes an imported behavior implementation") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  constexpr auto source =
      "module root;\n"
      "import dependency::{Behavior};\n"
      "impl Behavior for i32 { fun act() {} }\n"_zc;
  constexpr auto dependencySource =
      "module root;\n"
      "export interface Behavior { fun act(); }\n"_zc;
  stageBaseInputs(state, database, source, "authority"_zc, true, dependencySource, true);

  auto opened = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  stageDependencyImportResolutionInputs(transaction);
  ZC_REQUIRE(transaction.commit().isCommitted());
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto sealed = sealDatabase(database, roots, true);
  auto skeletonKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto skeleton =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(skeletonKey));
  ZC_REQUIRE(skeleton.isPublished());
  const auto& owners = skeleton.lease().capability().identities().stableWitness().bodyOwners();
  for (const auto& owner : owners.values()) {
    auto bodyKey = ContextualBodyOwnerKey::from(roots.clone(), owner.clone());
    auto stableBody = sealed.get<binder::BindOwnerBody>(bodyKey.clone());
    ZC_REQUIRE(stableBody.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(stableBody.value().storage().is<binder::BinderQueryValue<binder::BoundOwnerBody>>());
    ZC_EXPECT(stableBody.value()
                  .storage()
                  .get<binder::BinderQueryValue<binder::BoundOwnerBody>>()
                  .diagnostics.values()
                  .size() == 0);
    auto syntax = sealed.get<OwnerBodySyntaxQuery>(bodyKey.clone());
    ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
    auto allocation = sealed.get<binder::ModuleBindingAllocationPlanQuery>(
        ContextualModuleKey::from(roots.clone(), semanticModule()));
    ZC_REQUIRE(allocation.kind() == query::QueryValueKind::Value);
    auto body = sealed.getCapability<graph_query::MaterializeOwnerBodyQuery>(zc::mv(bodyKey));
    ZC_REQUIRE(body.isPublished());
  }
  auto boundKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto bound = sealed.getCapability<graph_query::VerifyBoundModuleQuery>(zc::mv(boundKey));
  ZC_REQUIRE(bound.isPublished());
}

ZC_TEST("Materialized skeleton retains dependency foreign re-export surface") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  constexpr auto source = "module root;\nexport dependency::{Dependency};\n"_zc;
  constexpr auto dependencySource = "module root;\nexport class Dependency {}\n"_zc;
  stageBaseInputs(state, database, source, "authority"_zc, true, dependencySource, false, true);

  auto opened = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  stageDependencyImportResolutionInputs(transaction);
  ZC_REQUIRE(transaction.commit().isCommitted());
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto sealed = sealDatabase(database, roots, true);
  auto stableSkeleton = sealed.get<binder::BindModuleSkeleton>(semanticModule());
  ZC_REQUIRE(stableSkeleton.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(
      stableSkeleton.value().storage().is<binder::BinderQueryValue<binder::BoundModuleSkeleton>>());
  const auto& stable =
      stableSkeleton.value().storage().get<binder::BinderQueryValue<binder::BoundModuleSkeleton>>();
  ZC_REQUIRE(stable.value.imports().values().size() == 1);
  ZC_REQUIRE(stable.value.localExports().values().size() == 1);
  auto materializedKey = ContextualModuleKey::from(roots.clone(), semanticModule());
  auto materialized =
      sealed.getCapability<graph_query::MaterializeModuleSkeletonQuery>(zc::mv(materializedKey));
  ZC_EXPECT(!materialized.isSourceRejected());
  ZC_EXPECT(!materialized.isKeyRejected());
  ZC_EXPECT(!materialized.isRuntimeRejected());
  ZC_REQUIRE(materialized.isPublished());
  const auto& capability = materialized.lease().capability();
  ZC_REQUIRE(capability.dependencySkeletonLeases().size() == 1);
  ZC_REQUIRE(capability.materializedImports().size() == 1);
  const auto& reexport = capability.materializedImports()[0];
  ZC_EXPECT(reexport.kind == binder::ImportBindingKind::ForeignReexport);
  ZC_EXPECT(reexport.reexportChain.size() == 1);
  ZC_EXPECT(reexport.sourceModule ==
            capability.dependencySkeletonLeases()[0].capability().identities().module());
  ZC_EXPECT(capability.bindingSurface().exports().size() == 1);

  bool reexportScopeBinding = false;
  for (const auto& scope : capability.materializedScopes()) {
    for (const auto& binding : scope.bindings) {
      const auto& bindingIdentity = binding.binding.bindingIdentity.value();
      if (binding.binding.origin != binder::BindingOrigin::ReexportAlias ||
          !bindingIdentity.is<binder::SemanticImportBindingTarget>()) {
        continue;
      }
      ZC_EXPECT(bindingIdentity.get<binder::SemanticImportBindingTarget>().binding ==
                reexport.binding);
      reexportScopeBinding = true;
    }
  }
  ZC_EXPECT(reexportScopeBinding);
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
  auto missingSemanticModule = namedSemanticModule("missing"_zc);
  auto missingOwner = binder::StableBodyOwnerKey::module(missingSemanticModule.clone());
  auto missingOwnerKey = contextual(roots, missingSemanticModule, missingOwner);
  auto ownerProvenance = sealed.getCapability<OwnerBodyProvenanceQuery>(missingOwnerKey);
  auto boundOwner = sealed.get<binder::BindOwnerBody>(missingOwnerKey);
  auto allocation = sealed.get<binder::ModuleBindingAllocationPlanQuery>(
      contextual(roots, missingSemanticModule));
  ZC_REQUIRE(sites.isKeyRejected());
  ZC_REQUIRE(admission.isKeyRejected());
  ZC_REQUIRE(definitions.isKeyRejected());
  ZC_REQUIRE(implementations.isKeyRejected());
  ZC_REQUIRE(moduleProvenance.isKeyRejected());
  ZC_EXPECT(sites.keyFailure().kind() == binder::BinderKeyFailureKind::MissingSelectedModuleSource);
  ZC_REQUIRE(ownerProvenance.isKeyRejected());
  ZC_EXPECT(ownerProvenance.keyFailure() == moduleProvenance.keyFailure());
  ZC_REQUIRE(boundOwner.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(boundOwner.value().storage().is<binder::BinderKeyRejected>());
  ZC_EXPECT(boundOwner.value().storage().get<binder::BinderKeyRejected>().failure ==
            moduleProvenance.keyFailure());
  ZC_REQUIRE(allocation.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(allocation.value().storage().is<binder::BinderKeyRejected>());
  ZC_EXPECT(allocation.value().storage().get<binder::BinderKeyRejected>().failure ==
            moduleProvenance.keyFailure());
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

ZC_TEST("Final-sealed named-item queries reject inactive authority records") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto module = stableModule();
  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto snapshot = database.snapshot();
  auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(inventory.value().entries().size() == 1);
  auto definition = inventory.value().entries()[0].key().clone();
  auto itemKey = contextual(roots, semanticModule(), definition);

  auto opened = database.beginInputTransaction(snapshot.revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  ZC_REQUIRE(transaction.erase<ActiveDefinitionAuthorityInput>(itemKey).isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto sealed = sealDatabase(database, roots);
  auto syntax = sealed.get<NamedItemSyntaxQuery>(itemKey);
  auto provenance = sealed.getCapability<NamedItemProvenanceQuery>(itemKey);
  ZC_REQUIRE(syntax.kind() == query::QueryValueKind::SemanticFailure);
  ZC_REQUIRE(provenance.isKeyRejected());
  ZC_EXPECT(provenance.keyFailure().kind() == binder::BinderKeyFailureKind::InactiveOwner);
}

ZC_TEST("Named-item syntax forwards selected-source rejection") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto module = stableModule();
  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto snapshot = database.snapshot();
  auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(inventory.value().entries().size() == 1);
  auto itemKey = contextual(roots, semanticModule(), inventory.value().entries()[0].key().clone());

  auto opened = database.beginInputTransaction(snapshot.revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  ZC_REQUIRE(transaction
                 .set<source_query::SourceSnapshotInput>(
                     stableSource(), sourceSnapshot("module root;\nclass Alpha {\n"_zc))
                 .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto syntax = database.snapshot().get<NamedItemSyntaxQuery>(itemKey);
  ZC_REQUIRE(syntax.kind() == query::QueryValueKind::SemanticFailure);
}

ZC_TEST("Named-item syntax projects a missing selected module source") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto module = stableModule();
  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);
  ZC_REQUIRE(commitAuthority(state, database, roots));

  auto snapshot = database.snapshot();
  auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(inventory.value().entries().size() == 1);
  auto itemKey = contextual(roots, semanticModule(), inventory.value().entries()[0].key().clone());

  auto opened = database.beginInputTransaction(snapshot.revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  zc::Vector<graph_query::SelectedModuleRecord> entries;
  entries.add(graph_query::SelectedModuleRecord(namedSemanticModule("other"_zc),
                                                namedSource("other.zom"_zc)));
  auto catalog = graph_query::SelectedModuleCatalog::from(crate(), zc::mv(entries));
  ZC_REQUIRE(catalog != zc::none);
  ZC_REQUIRE(
      transaction.set<graph_query::SelectedModuleCatalogInput>(crate(), ZC_REQUIRE_NONNULL(catalog))
          .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());

  auto result = database.snapshot().get<NamedItemSyntaxQuery>(itemKey);
  ZC_REQUIRE(result.kind() == query::QueryValueKind::SemanticFailure);
}

ZC_TEST("Named-item syntax requires a committed authority transaction") {
  auto database = queryDatabase(scheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ContextualIdentityAuthorityInputLedger state;
  auto roots = packageRoots();
  auto module = stableModule();
  stageBaseInputs(state, database, "module root;\nclass Alpha {}\n"_zc);

  auto snapshot = database.snapshot();
  auto inventory = snapshot.get<NamedDefinitionInventoryQuery>(module);
  ZC_REQUIRE(inventory.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(inventory.value().entries().size() == 1);
  auto itemKey = contextual(roots, semanticModule(), inventory.value().entries()[0].key().clone());

  auto syntax = snapshot.get<NamedItemSyntaxQuery>(itemKey);
  ZC_REQUIRE(syntax.isRuntimeFailure());
  ZC_EXPECT(syntax.runtimeFailure() == query::QueryRuntimeFailure::ProviderRejected);
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
    auto bound = sealed.get<binder::BindOwnerBody>(ownerKey);
    ZC_REQUIRE(syntax.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(provenance.isPublished());
    ZC_REQUIRE(bound.kind() == query::QueryValueKind::Value);
    ZC_REQUIRE(bound.value().storage().is<binder::BinderQueryValue<binder::BoundOwnerBody>>());
    ZC_EXPECT(syntax.value().owner() == owner);
    ZC_EXPECT(provenance.lease().capability().owner() == owner);
    const auto& boundValue =
        bound.value().storage().get<binder::BinderQueryValue<binder::BoundOwnerBody>>();
    ZC_EXPECT(boundValue.diagnostics.values().size() == 0);
    ZC_EXPECT(boundValue.value.owner() == ownerKey.body());
    auto boundBytes = binder::BindOwnerBody::encodeValue(bound.value());
    auto decodedBound = binder::BindOwnerBody::decodeValue(boundBytes.asPtr());
    ZC_REQUIRE(decodedBound != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedBound) == bound.value());
    if (owner.kind() == binder::StableBodyOwnerKind::Definition) {
      const auto& detached = syntax.value().detachedSyntax();
      ZC_REQUIRE(detached.rootCount() == 1 && detached.nodes().size() != 0);
      ZC_REQUIRE(detached.nodes()[0].kind() == binder::DetachedModuleBodyNodeKind::Syntax);
      const auto kind = ZC_ASSERT_NONNULL(detached.nodes()[0].syntaxKind());
      ZC_EXPECT(kind == ast::SyntaxKind::BlockStmt || kind == ast::SyntaxKind::IntLiteral);
      ZC_REQUIRE(provenance.lease().capability().detachedProvenance().entries().size() != 0);
      uint32_t rootPath[] = {0};
      ZC_EXPECT(
          provenance.lease().capability().detachedProvenance().entries()[0].path.components() ==
          zc::arrayPtr(rootPath));
    }
  }
  auto allocation = sealed.get<binder::ModuleBindingAllocationPlanQuery>(moduleKey);
  ZC_REQUIRE(allocation.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(allocation.value()
                 .storage()
                 .is<binder::BinderQueryValue<binder::ModuleBindingAllocationPlan>>());
  const auto& allocationValue =
      allocation.value()
          .storage()
          .get<binder::BinderQueryValue<binder::ModuleBindingAllocationPlan>>();
  ZC_EXPECT(allocationValue.diagnostics.values().size() == 0);
  ZC_EXPECT(allocationValue.value.owners().values().size() == owners.value().owners().size());
  auto allocationBytes = binder::ModuleBindingAllocationPlanQuery::encodeValue(allocation.value());
  auto decodedAllocation =
      binder::ModuleBindingAllocationPlanQuery::decodeValue(allocationBytes.asPtr());
  ZC_REQUIRE(decodedAllocation != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedAllocation) == allocation.value());
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
