// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/driver/core-library-query-provider.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/parser/parse-source-query.h"
#include "zomlang/compiler/source/core-distribution.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

using namespace identity::source_query;

basic::ThreadPool& queryTestScheduler() {
  static basic::ThreadPool scheduler(4);
  return scheduler;
}

class QueryTestSemanticContextResources final : public query::SemanticContextCapabilityResources {};

query::QueryDatabase queryTestDatabase() {
  auto resources = zc::heap<QueryTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  return query::QueryDatabase(queryTestScheduler(), query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid incremental binding query scalar");
}

identity::ResolvedVersion version() { return scalar<identity::ResolvedVersion>("0.0.0"_zc); }

identity::SortedFeatureSet features() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid feature fixture");
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> values;
  auto result = identity::SortedTargetFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target feature fixture");
}

identity::PackageKey packageKey(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>(name), version(), features());
}

identity::PackageKey packageKey() { return packageKey("incremental_binding_query"_zc); }

identity::CanonicalTargetSpecificationKey target() {
  auto result = identity::CanonicalTargetSpecificationKey::from(
      scalar<identity::TargetComponentName>("x86_64"_zc),
      scalar<identity::TargetComponentName>("zom"_zc),
      scalar<identity::TargetComponentName>("none"_zc),
      scalar<identity::TargetComponentName>("unknown"_zc),
      scalar<identity::TargetComponentName>("zom"_zc), 64, identity::Endianness::Little,
      targetFeatures());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid target fixture");
}

package::RegisteredTargetProfileName profileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(backendFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = ir::RegisteredTargetProfileRecord::from(
      profileName(), target(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = ir::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

package::RegisteredTargetSelection targetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto selected = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    return zc::mv(ZC_REQUIRE_NONNULL(selected));
  }
  ZC_UNREACHABLE
}

identity::CanonicalRelativePath compilationRootPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>("main.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalRelativePath libraryCompilationRootPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>("lib.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

package::VerifiedPackageCompilationRequest compilationRequest(
    const ir::TargetRegistrySnapshot& registry,
    package::SelectedLanguageOptions language = package::SelectedLanguageOptions{}) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      packageKey(), identity::CrateTargetKind::Binary,
      scalar<identity::TargetName>("incremental_binding_query"_zc), 2026, false,
      compilationRootPath()));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry), language,
      package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

package::VerifiedPackageCompilationRequest multiTargetCompilationRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      packageKey(), identity::CrateTargetKind::Binary,
      scalar<identity::TargetName>("incremental_binding_query"_zc), 2026, false,
      compilationRootPath()));
  roots.add(package::VerifiedCompilationRoot::from(
      packageKey(), identity::CrateTargetKind::Library,
      scalar<identity::TargetName>("incremental_binding_query_library"_zc), 2026, false,
      libraryCompilationRootPath()));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), targetSelection(registry), targetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto result = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid compilation fixture");
}

identity::CrateKey crateKey(zc::StringPtr targetName) {
  auto result = identity::CrateKey::from(
      identity::CompilationUnitIdentity::userPackage(packageKey()),
      identity::CrateTargetKind::Library, scalar<identity::TargetName>(targetName), compilation());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid crate fixture");
}

identity::CrateKey crateKey() { return crateKey("incremental_binding_query"_zc); }

identity::CrateKey crateKey(identity::PackageKey&& package, zc::StringPtr targetName) {
  auto result = identity::CrateKey::from(
      identity::CompilationUnitIdentity::userPackage(zc::mv(package)),
      identity::CrateTargetKind::Library, scalar<identity::TargetName>(targetName), compilation());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::PackageDependencyEdgeKey packageEdge(zc::StringPtr consumer, zc::StringPtr alias,
                                               zc::StringPtr provider) {
  auto result = identity::PackageDependencyEdgeKey::from(
      packageKey(consumer), scalar<identity::DependencyAlias>(alias),
      identity::DependencyDomain::Target, packageKey(provider));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CrateDependencyEdgeKey crateEdge(zc::StringPtr consumer, zc::StringPtr alias,
                                           zc::StringPtr provider) {
  auto result = identity::CrateDependencyEdgeKey::from(
      identity::CrateDependencyOrigin::userPackage(packageEdge(consumer, alias, provider)),
      crateKey(packageKey(consumer), consumer), crateKey(packageKey(provider), provider));
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

void sortEncoded(zc::Vector<zc::Array<uint8_t>>& values) {
  for (size_t index = 1; index < values.size(); ++index) {
    auto current = zc::mv(values[index]);
    size_t insertion = index;
    while (insertion != 0 && encodedLess(current.asPtr(), values[insertion - 1].asPtr())) {
      values[insertion] = zc::mv(values[insertion - 1]);
      --insertion;
    }
    values[insertion] = zc::mv(current);
  }
}

zc::Array<uint8_t> packageGraphBytes(zc::Vector<zc::Array<uint8_t>>&& packages,
                                     zc::Vector<zc::Array<uint8_t>>&& resolvedEdges,
                                     zc::Vector<zc::Array<uint8_t>>&& selectedEdges,
                                     zc::Vector<zc::Array<uint8_t>>&& crates,
                                     zc::Vector<zc::Array<uint8_t>>&& crateEdges) {
  sortEncoded(packages);
  sortEncoded(resolvedEdges);
  sortEncoded(selectedEdges);
  sortEncoded(crates);
  sortEncoded(crateEdges);
  identity::CanonicalEncoder encoder;
  const auto encode = [&](const zc::Vector<zc::Array<uint8_t>>& values) {
    encoder.encodeSequenceSize(values.size());
    for (const auto& value : values) { encoder.encodeByteString(value.asPtr()); }
  };
  encode(packages);
  encode(resolvedEdges);
  encode(selectedEdges);
  encode(crates);
  encode(crateEdges);
  return encoder.finish();
}

zc::Array<uint8_t> singlePackageGraphBytes(zc::StringPtr packageName, zc::StringPtr targetName) {
  zc::Vector<zc::Array<uint8_t>> packages;
  packages.add(packageKey(packageName).encode());
  zc::Vector<zc::Array<uint8_t>> resolvedEdges;
  zc::Vector<zc::Array<uint8_t>> selectedEdges;
  zc::Vector<zc::Array<uint8_t>> crates;
  crates.add(crateKey(packageKey(packageName), targetName).encode());
  zc::Vector<zc::Array<uint8_t>> crateEdges;
  return packageGraphBytes(zc::mv(packages), zc::mv(resolvedEdges), zc::mv(selectedEdges),
                           zc::mv(crates), zc::mv(crateEdges));
}

CanonicalPackageGraph singlePackageGraph(zc::StringPtr packageName, zc::StringPtr targetName) {
  auto bytes = singlePackageGraphBytes(packageName, targetName);
  auto graph = PackageGraphInput::decodeValue(bytes.asPtr());
  return zc::mv(ZC_REQUIRE_NONNULL(graph));
}

identity::SourceFileKey source(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      crateKey(), identity::SourceOriginKey::localFile(
                      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

identity::ModuleKey semanticModule(zc::StringPtr name) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(scalar<identity::ModulePathSegment>(name));
  auto semantic = identity::ModuleKey::from(crateKey(), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(semantic));
}

StableModuleQueryKey module(zc::StringPtr name) {
  auto semantic = semanticModule(name);
  auto projected = StableModuleQueryKey::fromVerified(semantic);
  ZC_IF_SOME(key, projected) { return zc::mv(key); }
  ZC_FAIL_REQUIRE("invalid module query key fixture");
}

module_graph_query::SelectedModuleCatalog selectedModuleCatalog(zc::StringPtr moduleName,
                                                                zc::StringPtr sourceName) {
  zc::Vector<module_graph_query::SelectedModuleRecord> records;
  records.add(
      module_graph_query::SelectedModuleRecord(semanticModule(moduleName), source(sourceName)));
  auto catalog = module_graph_query::SelectedModuleCatalog::from(crateKey(), zc::mv(records));
  return zc::mv(ZC_REQUIRE_NONNULL(catalog));
}

identity::ImmutableSourceSnapshot immutableSnapshot(zc::StringPtr sourceName,
                                                    zc::Array<uint8_t>&& bytes) {
  auto result = identity::ImmutableSourceSnapshot::from(source(sourceName), zc::mv(bytes));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

StableSourceQueryKey sourceQueryKey(zc::StringPtr sourceName) {
  auto sourceKey = source(sourceName);
  auto result = StableSourceQueryKey::fromVerified(sourceKey);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalSourceSnapshot sourceSnapshotValue(zc::StringPtr sourceName, zc::Array<uint8_t>&& bytes) {
  auto snapshot = immutableSnapshot(sourceName, zc::mv(bytes));
  auto result = CanonicalSourceSnapshot::fromVerified(snapshot);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalCompilationOptions compilationOptionsValue(
    const ir::TargetRegistrySnapshot& registry,
    package::SelectedLanguageOptions language = package::SelectedLanguageOptions{}) {
  auto request = compilationRequest(registry, language);
  auto result = CanonicalCompilationOptions::fromVerified(request);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

query::InputTransaction transaction(query::QueryDatabase& database) {
  auto current = database.snapshot();
  auto result = database.beginInputTransaction(current.revision());
  ZC_REQUIRE(result.isOpened());
  return zc::mv(result).takeTransaction();
}

PackageRootSetKey packageRootSet(const package::VerifiedPackageCompilationRequest& request) {
  auto result = PackageRootSetKey::fromVerified(request);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CompilationRootSetQueryKey compilationRootSet(
    const package::VerifiedPackageCompilationRequest& request) {
  auto result = CompilationRootSetQueryKey::fromVerified(request);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

StableCrateQueryKey stableCrate(zc::StringPtr name) {
  auto semantic = crateKey(name);
  auto result = StableCrateQueryKey::fromVerified(semantic);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

template <typename... Rest>
CanonicalCrateSet crateSet(const StableCrateQueryKey& first, const Rest&... rest) {
  zc::Vector<StableCrateQueryKey> crates;
  crates.add(first.clone());
  (crates.add(rest.clone()), ...);
  auto result = CanonicalCrateSet::from(zc::mv(crates));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

template <typename... Rest>
CanonicalSourceSet sourceSet(const StableSourceQueryKey& first, const Rest&... rest) {
  zc::Vector<StableSourceQueryKey> sources;
  sources.add(first.clone());
  (sources.add(rest.clone()), ...);
  auto result = CanonicalSourceSet::from(zc::mv(sources));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

}  // namespace

ZC_TEST("Incremental binding query registers low durability source snapshot inputs") {
  ZC_EXPECT(SourceSnapshotInput::descriptor.domain == "zom.query.source-snapshot"_zc);
  ZC_EXPECT(SourceSnapshotInput::descriptor.durability == query::Durability::Low);

  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicate = database.registerDescriptor<SourceSnapshotInput>();
  ZC_EXPECT(!duplicate.isRegistered());
  ZC_EXPECT(duplicate.failure() == query::DescriptorRegistrationFailure::SlotAlreadyRegistered);
}

ZC_TEST("Incremental binding query registers medium durability compilation options") {
  ZC_EXPECT(CompilationOptionsInput::descriptor.domain == "zom.query.compilation-options"_zc);
  ZC_EXPECT(CompilationOptionsInput::descriptor.durability == query::Durability::Medium);

  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicate = database.registerDescriptor<CompilationOptionsInput>();
  ZC_EXPECT(!duplicate.isRegistered());
  ZC_EXPECT(duplicate.failure() == query::DescriptorRegistrationFailure::SlotAlreadyRegistered);
}

ZC_TEST("Incremental binding query registers retained revision local source parsing") {
  ZC_EXPECT(parser::ParseSourceQuery::descriptor.domain == "zom.query.parse-source"_zc);
  ZC_EXPECT(parser::ParseSourceQuery::descriptor.retention == query::RetentionClass::Retained);

  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicate = database.registerDescriptor<parser::ParseSourceQuery>();
  ZC_EXPECT(!duplicate.isRegistered());
  ZC_EXPECT(duplicate.failure() == query::DescriptorRegistrationFailure::SlotAlreadyRegistered);
}

ZC_TEST("Incremental binding query parses one source from its exact tracked inputs") {
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("parse-success.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("parse-success.zom"_zc, zc::heapArray("let value = 42;"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto snapshot = database.snapshot();
  auto result = snapshot.getCapability<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(result.isPublished());
  const auto& parsed = result.lease().capability();
  ZC_EXPECT(parsed.canonicalSourceKey() == sourceKey.canonicalSourceBytes());
  ZC_EXPECT(parsed.contentDigest() == sourceValue.contentDigest());
  ZC_EXPECT(parsed.sourceBytes() == sourceValue.bytes());
  ZC_EXPECT(parsed.logicalName() == "parse-success.zom"_zc);
  ZC_EXPECT(parsed.options() == (parser::CanonicalParserOptions{true, false, true}));
  ZC_EXPECT(parsed.tree().node(parsed.tree().root()).kind == ast::SyntaxKind::SourceFile);
  ZC_REQUIRE(parsed.tokens().size() != 0);
  ZC_EXPECT(parsed.tokens().back().kind == ast::SyntaxKind::EndOfFile);

  auto encoded = parsed.encodeCanonical();
  auto decoded = parser::CanonicalParsedSource::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded).encodeCanonical().asPtr() == encoded.asPtr());
  auto trailing = zc::heapArray<uint8_t>(encoded.size() + 1);
  for (size_t index = 0; index < encoded.size(); ++index) { trailing[index] = encoded[index]; }
  trailing.back() = 0;
  ZC_EXPECT(parser::CanonicalParsedSource::decodeCanonical(trailing.asPtr()) == zc::none);

  auto sourceFingerprint = snapshot.keyFingerprint<SourceSnapshotInput>(sourceKey);
  auto optionsFingerprint = snapshot.keyFingerprint<CompilationOptionsInput>(crateKey());
  ZC_REQUIRE(sourceFingerprint != zc::none);
  ZC_REQUIRE(optionsFingerprint != zc::none);
  auto groups = snapshot.dependencies<parser::ParseSourceQuery>(sourceKey);
  size_t sourceReads = 0;
  size_t optionReads = 0;
  for (const auto& group : groups) {
    ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
    ZC_REQUIRE(group.dependencies().size() == 1);
    const auto& fingerprint = group.dependencies()[0].key().fingerprint();
    if (fingerprint == ZC_REQUIRE_NONNULL(sourceFingerprint)) {
      ++sourceReads;
    } else if (fingerprint == ZC_REQUIRE_NONNULL(optionsFingerprint)) {
      ++optionReads;
    } else {
      ZC_FAIL_REQUIRE("ParseSource recorded an undeclared dependency");
    }
  }
  ZC_EXPECT(sourceReads == 2);
  ZC_EXPECT(optionReads == 2);

  ZC_REQUIRE(snapshot.hasRetainedValue<parser::ParseSourceQuery>(sourceKey));
  ZC_EXPECT(!snapshot.evictValue<parser::ParseSourceQuery>(sourceKey));
  auto repeated = snapshot.getCapability<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(repeated.isPublished());
  ZC_EXPECT(repeated.lease().stableWitness() == encoded.asPtr());
  ZC_EXPECT(repeated.lease().revision() == result.lease().revision());
  ZC_EXPECT(repeated.lease().arenaRevision() == result.lease().arenaRevision());
  ZC_EXPECT(snapshot.hasRetainedValue<parser::ParseSourceQuery>(sourceKey));

  auto firstLease = zc::mv(result).takeLease();
  auto equalWrite = transaction(database);
  ZC_REQUIRE(equalWrite.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(equalWrite.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(equalWrite.commit().isCommitted());
  auto nextSnapshot = database.snapshot();
  ZC_EXPECT(!nextSnapshot.hasRetainedValue<parser::ParseSourceQuery>(sourceKey));
  auto next = nextSnapshot.getCapability<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(next.isPublished());
  ZC_EXPECT(firstLease.revision() != next.lease().revision());
  ZC_EXPECT(firstLease.arenaRevision() != next.lease().arenaRevision());
  ZC_EXPECT(firstLease.stableWitness() == next.lease().stableWitness());
  ZC_EXPECT(firstLease.capability().sourceBytes() == sourceValue.bytes());
  ZC_EXPECT(firstLease.capability().tree().node(firstLease.capability().tree().root()).kind ==
            ast::SyntaxKind::SourceFile);
}

ZC_TEST("Incremental binding query publishes strict deterministic parse rejection") {
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("parse-rejected.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("parse-rejected.zom"_zc, zc::heapArray("let value = ;"_zcb));
  auto registry = targetRegistry();
  auto options =
      compilationOptionsValue(registry, package::SelectedLanguageOptions{false, true, false});
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto result = database.snapshot().getCapability<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(result.isSourceRejected());
  ZC_REQUIRE(result.diagnostics().values().size() != 0);
}

ZC_TEST("Incremental binding query publishes verified stable named inventories") {
  ZC_EXPECT(RevisionLocalDefinitionSitesQuery::descriptor.retention ==
            query::RetentionClass::Retained);
  ZC_EXPECT(RevisionLocalImplementationSitesQuery::descriptor.retention ==
            query::RetentionClass::Retained);
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(module_graph_query::registerModuleGraphQueries(database));
  auto duplicateDefinitions = database.registerDescriptor<RevisionLocalDefinitionSitesQuery>();
  ZC_EXPECT(!duplicateDefinitions.isRegistered());
  auto duplicateImplementations =
      database.registerDescriptor<RevisionLocalImplementationSitesQuery>();
  ZC_EXPECT(!duplicateImplementations.isRegistered());
  auto moduleKey = module("root"_zc);
  auto catalog = selectedModuleCatalog("root"_zc, "root.zom"_zc);
  auto sourceKey = sourceQueryKey("root.zom"_zc);
  auto sourceValue = sourceSnapshotValue(
      "root.zom"_zc,
      zc::heapArray("module root;\nclass Alpha {}\nstruct Beta {}\nimpl Trait for i32 {}\n"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto write = transaction(database);
  ZC_REQUIRE(
      write.set<module_graph_query::SelectedModuleCatalogInput>(crateKey(), catalog).isApplied());
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto snapshot = database.snapshot();
  auto identitySites = snapshot.getCapability<IdentitySyntaxSiteInventoryQuery>(moduleKey);
  ZC_REQUIRE(identitySites.isPublished());
  auto admission = snapshot.getCapability<StableIdentityAdmissionQuery>(moduleKey);
  ZC_REQUIRE(admission.isPublished());
  ZC_EXPECT(admission.lease().capability().definitions().size() == 2);
  zc::Vector<binder::RevisionLocalDefinitionSite> reconstructedSites;
  for (const auto& definition : admission.lease().capability().definitions()) {
    auto site = binder::RevisionLocalDefinitionSite::from(
        definition.node, definition.authority.key().clone(), definition.site.key().clone(),
        definition.site.range().byteStart(), definition.site.range().byteEnd());
    ZC_REQUIRE(site != zc::none);
    reconstructedSites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  auto definitions = snapshot.get<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(!definitions.isRuntimeFailure());
  ZC_REQUIRE(definitions.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(definitions.value().entries().size() == 2);
  auto reconstructedDefinitionSites = binder::RevisionLocalDefinitionSites::fromVerified(
      admission.lease().capability().module(), admission.lease().capability().source(),
      definitions.value(), zc::mv(reconstructedSites));
  ZC_REQUIRE(reconstructedDefinitionSites != zc::none);
  auto implementations = snapshot.get<NamedImplementationInventoryQuery>(moduleKey);
  ZC_REQUIRE(!implementations.isRuntimeFailure());
  ZC_REQUIRE(implementations.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(implementations.value().keys().size() == 1);
  zc::Vector<binder::RevisionLocalImplementationSite> reconstructedImplementationSites;
  for (const auto& implementation : admission.lease().capability().implementations()) {
    auto occurrence = binder::ImplSourceOccurrenceKey::from(implementation.authority.key().clone(),
                                                            implementation.site.key().clone());
    auto site = binder::RevisionLocalImplementationSite::from(
        implementation.node, zc::mv(occurrence), implementation.site.range().byteStart(),
        implementation.site.range().byteEnd());
    ZC_REQUIRE(site != zc::none);
    reconstructedImplementationSites.add(zc::mv(ZC_ASSERT_NONNULL(site)));
  }
  auto reconstructedImplementationSiteInventory =
      binder::RevisionLocalImplementationSites::fromVerified(
          admission.lease().capability().module(), admission.lease().capability().source(),
          implementations.value(), zc::mv(reconstructedImplementationSites));
  ZC_REQUIRE(reconstructedImplementationSiteInventory != zc::none);
  auto definitionSites = snapshot.getCapability<RevisionLocalDefinitionSitesQuery>(moduleKey);
  ZC_REQUIRE(definitionSites.isRuntimeRejected());
  ZC_EXPECT(definitionSites.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  auto implementationSites =
      snapshot.getCapability<RevisionLocalImplementationSitesQuery>(moduleKey);
  ZC_REQUIRE(implementationSites.isRuntimeRejected());
  ZC_EXPECT(implementationSites.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  auto moduleBodySyntax = snapshot.get<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(!moduleBodySyntax.isRuntimeFailure());
  ZC_REQUIRE(moduleBodySyntax.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(moduleBodySyntax.value().rootCount() == 3);
  auto moduleBodyProvenance = snapshot.getCapability<ModuleBodyProvenanceQuery>(moduleKey);
  ZC_REQUIRE(moduleBodyProvenance.isRuntimeRejected());
  ZC_EXPECT(moduleBodyProvenance.runtimeFailure() == query::QueryRuntimeFailure::FinalSealRequired);
  ZC_EXPECT(ModuleBodyProvenanceQuery::descriptor.retention == query::RetentionClass::Retained);

  auto definitionBytes = definitions.value().encodeCanonical();
  auto decodedDefinitions =
      binder::NamedDefinitionInventory::decodeCanonical(definitionBytes.asPtr());
  ZC_REQUIRE(decodedDefinitions != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedDefinitions).sameAs(definitions.value()));
  auto malformedDefinitions = zc::heapArray<uint8_t>(definitionBytes.asPtr());
  malformedDefinitions.back() ^= 0x01;
  ZC_EXPECT(binder::NamedDefinitionInventory::decodeCanonical(malformedDefinitions.asPtr()) ==
            zc::none);

  auto implementationBytes = implementations.value().encodeCanonical();
  auto decodedImplementations =
      binder::NamedImplementationInventory::decodeCanonical(implementationBytes.asPtr());
  ZC_REQUIRE(decodedImplementations != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedImplementations).sameAs(implementations.value()));
  auto trailingImplementations = zc::heapArray<uint8_t>(implementationBytes.size() + 1);
  for (size_t index = 0; index < implementationBytes.size(); ++index) {
    trailingImplementations[index] = implementationBytes[index];
  }
  trailingImplementations.back() = 0;
  ZC_EXPECT(binder::NamedImplementationInventory::decodeCanonical(
                trailingImplementations.asPtr()) == zc::none);

  auto definitionSiteBytes = ZC_ASSERT_NONNULL(reconstructedDefinitionSites).encodeCanonical();
  auto decodedDefinitionSites =
      binder::RevisionLocalDefinitionSites::decodeCanonical(definitionSiteBytes.asPtr());
  ZC_REQUIRE(decodedDefinitionSites != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedDefinitionSites)
                .sameAs(ZC_ASSERT_NONNULL(reconstructedDefinitionSites)));
  auto implementationSiteBytes =
      ZC_ASSERT_NONNULL(reconstructedImplementationSiteInventory).encodeCanonical();
  auto decodedImplementationSites =
      binder::RevisionLocalImplementationSites::decodeCanonical(implementationSiteBytes.asPtr());
  ZC_REQUIRE(decodedImplementationSites != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedImplementationSites)
                .sameAs(ZC_ASSERT_NONNULL(reconstructedImplementationSiteInventory)));
  auto moduleBodySyntaxBytes = moduleBodySyntax.value().encodeCanonical();
  auto decodedModuleBodySyntax =
      binder::ModuleBodySyntax::decodeCanonical(moduleBodySyntaxBytes.asPtr());
  ZC_REQUIRE(decodedModuleBodySyntax != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedModuleBodySyntax) == moduleBodySyntax.value());
  auto trailingModuleBodySyntax = zc::heapArray<uint8_t>(moduleBodySyntaxBytes.size() + 1);
  for (size_t index = 0; index < moduleBodySyntaxBytes.size(); ++index) {
    trailingModuleBodySyntax[index] = moduleBodySyntaxBytes[index];
  }
  trailingModuleBodySyntax.back() = 0;
  ZC_EXPECT(binder::ModuleBodySyntax::decodeCanonical(trailingModuleBodySyntax.asPtr()) ==
            zc::none);
  auto admissionFingerprint = snapshot.keyFingerprint<StableIdentityAdmissionQuery>(moduleKey);
  ZC_REQUIRE(admissionFingerprint != zc::none);
  for (const auto& groups : {snapshot.dependencies<NamedDefinitionInventoryQuery>(moduleKey),
                             snapshot.dependencies<NamedImplementationInventoryQuery>(moduleKey)}) {
    size_t admissionReads = 0;
    for (const auto& group : groups) {
      ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
      ZC_REQUIRE(group.dependencies().size() == 1);
      const auto& fingerprint = group.dependencies()[0].key().fingerprint();
      if (fingerprint == ZC_ASSERT_NONNULL(admissionFingerprint)) {
        ++admissionReads;
      } else {
        ZC_FAIL_REQUIRE("Named inventory recorded an undeclared dependency");
      }
    }
    ZC_EXPECT(admissionReads == 2);
  }

  auto selectedFingerprint = snapshot.keyFingerprint<module_graph_query::SelectedModuleSourceQuery>(
      ZC_REQUIRE_NONNULL(module_graph_query::SelectedModuleSourceQuery::decodeKey(
          moduleKey.canonicalModuleBytes())));
  auto parseFingerprint = snapshot.keyFingerprint<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(selectedFingerprint != zc::none);
  ZC_REQUIRE(parseFingerprint != zc::none);

  size_t syntaxSelectedReads = 0;
  size_t syntaxParseReads = 0;
  size_t syntaxAdmissionReads = 0;
  for (const auto& group : snapshot.dependencies<ModuleBodySyntaxQuery>(moduleKey)) {
    ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
    ZC_REQUIRE(group.dependencies().size() == 1);
    const auto& fingerprint = group.dependencies()[0].key().fingerprint();
    if (fingerprint == ZC_ASSERT_NONNULL(selectedFingerprint)) {
      ++syntaxSelectedReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(parseFingerprint)) {
      ++syntaxParseReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(admissionFingerprint)) {
      ++syntaxAdmissionReads;
    } else {
      ZC_FAIL_REQUIRE("Module body syntax recorded an undeclared dependency");
    }
  }
  ZC_EXPECT(syntaxSelectedReads == 2);
  ZC_EXPECT(syntaxParseReads == 2);
  ZC_EXPECT(syntaxAdmissionReads == 2);
}

ZC_TEST("Incremental binding query keeps module item let syntax in the module body") {
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(module_graph_query::registerModuleGraphQueries(database));
  auto moduleKey = module("root"_zc);
  auto catalog = selectedModuleCatalog("root"_zc, "module-body-let.zom"_zc);
  auto sourceKey = sourceQueryKey("module-body-let.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("module-body-let.zom"_zc, zc::heapArray("let root = 0;"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto write = transaction(database);
  ZC_REQUIRE(
      write.set<module_graph_query::SelectedModuleCatalogInput>(crateKey(), catalog).isApplied());
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto snapshot = database.snapshot();
  auto definitions = snapshot.get<NamedDefinitionInventoryQuery>(moduleKey);
  auto admission = snapshot.getCapability<StableIdentityAdmissionQuery>(moduleKey);
  auto bodySyntax = snapshot.get<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(definitions.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(admission.isPublished());
  ZC_REQUIRE(bodySyntax.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(definitions.value().entries().size() == 1);
  ZC_EXPECT(admission.lease().capability().definitions().size() == 1);
  ZC_EXPECT(bodySyntax.value().rootCount() == 1);
  for (const auto& node : bodySyntax.value().nodes()) {
    ZC_EXPECT(node.kind() != binder::DetachedModuleBodyNodeKind::DefinitionBoundary);
  }
}

ZC_TEST("Incremental binding query backdates semantic inventory and tracks range-only sites") {
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(module_graph_query::registerModuleGraphQueries(database));
  auto moduleKey = module("root"_zc);
  auto catalog = selectedModuleCatalog("root"_zc, "backdate.zom"_zc);
  auto sourceKey = sourceQueryKey("backdate.zom"_zc);
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto firstSource =
      sourceSnapshotValue("backdate.zom"_zc, zc::heapArray("module root;\nclass Alpha {}\n"_zcb));
  auto firstWrite = transaction(database);
  ZC_REQUIRE(firstWrite.set<module_graph_query::SelectedModuleCatalogInput>(crateKey(), catalog)
                 .isApplied());
  ZC_REQUIRE(firstWrite.set<SourceSnapshotInput>(sourceKey, firstSource).isApplied());
  ZC_REQUIRE(firstWrite.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(firstWrite.commit().isCommitted());
  auto first = database.snapshot();
  auto firstResult = first.get<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(firstResult.kind() == query::QueryValueKind::Value);
  auto firstSites = first.getCapability<IdentitySyntaxSiteInventoryQuery>(moduleKey);
  ZC_REQUIRE(firstSites.isPublished());
  auto firstBodySyntax = first.get<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(firstBodySyntax.kind() == query::QueryValueKind::Value);
  auto firstMetadata = first.metadata<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(firstMetadata != zc::none);
  auto firstBodySyntaxMetadata = first.metadata<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(firstBodySyntaxMetadata != zc::none);

  auto shiftedSource = sourceSnapshotValue("backdate.zom"_zc,
                                           zc::heapArray("module root;\n\n\nclass Alpha {}\n"_zcb));
  auto shiftedWrite = transaction(database);
  ZC_REQUIRE(shiftedWrite.set<SourceSnapshotInput>(sourceKey, shiftedSource).isApplied());
  ZC_REQUIRE(shiftedWrite.commit().isCommitted());
  auto shifted = database.snapshot();
  auto shiftedResult = shifted.get<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(shiftedResult.kind() == query::QueryValueKind::Value);
  auto shiftedSites = shifted.getCapability<IdentitySyntaxSiteInventoryQuery>(moduleKey);
  ZC_REQUIRE(shiftedSites.isPublished());
  auto shiftedBodySyntax = shifted.get<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(shiftedBodySyntax.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(firstResult.value().sameAs(shiftedResult.value()));
  ZC_EXPECT(firstSites.lease().capability() != shiftedSites.lease().capability());
  ZC_EXPECT(firstBodySyntax.value() == shiftedBodySyntax.value());
  auto shiftedMetadata = shifted.metadata<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(shiftedMetadata != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(shiftedMetadata).changedAt() ==
            ZC_ASSERT_NONNULL(firstMetadata).changedAt());
  auto shiftedSiteMetadata = shifted.metadata<IdentitySyntaxSiteInventoryQuery>(moduleKey);
  ZC_REQUIRE(shiftedSiteMetadata != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(shiftedSiteMetadata).changedAt() == shifted.revision());
  auto shiftedBodySyntaxMetadata = shifted.metadata<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(shiftedBodySyntaxMetadata != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(shiftedBodySyntaxMetadata).changedAt() ==
            ZC_ASSERT_NONNULL(firstBodySyntaxMetadata).changedAt());
}

ZC_TEST("Incremental binding query compilation options codec is exact bounded and closed") {
  auto registry = targetRegistry();
  auto request = compilationRequest(registry, package::SelectedLanguageOptions{false, true, false});
  auto value = CanonicalCompilationOptions::fromVerified(request);
  ZC_REQUIRE(value != zc::none);
  auto key = crateKey();
  auto otherKey = crateKey("incremental_binding_query_aux"_zc);
  ZC_EXPECT(key.encode().asPtr() != otherKey.encode().asPtr());
  auto encodedKey = CompilationOptionsInput::encodeKey(key);
  auto decodedKey = CompilationOptionsInput::decodeKey(encodedKey.asPtr());
  ZC_REQUIRE(decodedKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedKey).encode().asPtr() == key.encode().asPtr());
  ZC_EXPECT(CompilationOptionsInput::encodeKey(otherKey).asPtr() != encodedKey.asPtr());
  ZC_EXPECT(CompilationOptionsInput::decodeKey(encodedKey.slice(0, encodedKey.size() - 1)) ==
            zc::none);
  auto trailingKey = zc::heapArray<uint8_t>(encodedKey.size() + 1);
  for (size_t index = 0; index < encodedKey.size(); ++index) {
    trailingKey[index] = encodedKey[index];
  }
  trailingKey.back() = 0;
  ZC_EXPECT(CompilationOptionsInput::decodeKey(trailingKey.asPtr()) == zc::none);
  ZC_IF_SOME(options, value) {
    ZC_EXPECT(!options.useUnicode());
    ZC_EXPECT(options.allowDollarIdentifiers());
    ZC_EXPECT(!options.supportRegexLiterals());
    auto encoded = CompilationOptionsInput::encodeValue(options);
    auto decoded = CompilationOptionsInput::decodeValue(encoded.asPtr());
    ZC_REQUIRE(decoded != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == options);

    auto invalidPanic = zc::heapArray<uint8_t>(encoded.asPtr());
    const size_t panicOffset = 8 + options.hostTargetBytes().size() - 1;
    ZC_REQUIRE(panicOffset < invalidPanic.size());
    invalidPanic[panicOffset] = 0xff;
    ZC_EXPECT(CompilationOptionsInput::decodeValue(invalidPanic.asPtr()) == zc::none);

    auto invalidBool = zc::heapArray<uint8_t>(encoded.asPtr());
    invalidBool.back() = 0x02;
    ZC_EXPECT(CompilationOptionsInput::decodeValue(invalidBool.asPtr()) == zc::none);
    ZC_EXPECT(CompilationOptionsInput::decodeValue(encoded.slice(0, encoded.size() - 1)) ==
              zc::none);

    auto trailing = zc::heapArray<uint8_t>(encoded.size() + 1);
    for (size_t index = 0; index < encoded.size(); ++index) { trailing[index] = encoded[index]; }
    trailing.back() = 0;
    ZC_EXPECT(CompilationOptionsInput::decodeValue(trailing.asPtr()) == zc::none);

    identity::CanonicalEncoder oversized;
    oversized.encodeByteString(zc::heapArray<uint8_t>(32 * 1024 + 1).asPtr());
    oversized.encodeByteString(options.targetBytes());
    oversized.encodeBool(true);
    oversized.encodeBool(false);
    oversized.encodeBool(true);
    ZC_EXPECT(CompilationOptionsInput::decodeValue(oversized.finish().asPtr()) == zc::none);
  }
}

ZC_TEST("Incremental binding query compilation options backdate equals and replace changes") {
  auto registry = targetRegistry();
  auto firstRequest = compilationRequest(registry);
  auto firstValue = CanonicalCompilationOptions::fromVerified(firstRequest);
  ZC_REQUIRE(firstValue != zc::none);
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  const auto root = crateKey();
  ZC_IF_SOME(value, firstValue) {
    auto initialWrite = transaction(database);
    ZC_REQUIRE(initialWrite.set<CompilationOptionsInput>(root, value).isApplied());
    ZC_REQUIRE(initialWrite.commit().isCommitted());
    auto initial = database.snapshot();
    auto initialMetadata = initial.metadata<CompilationOptionsInput>(root);
    ZC_REQUIRE(initialMetadata != zc::none);

    auto equalWrite = transaction(database);
    ZC_REQUIRE(equalWrite.set<CompilationOptionsInput>(root, value).isApplied());
    ZC_REQUIRE(equalWrite.commit().isCommitted());
    auto equal = database.snapshot();
    auto equalMetadata = equal.metadata<CompilationOptionsInput>(root);
    ZC_REQUIRE(equalMetadata != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
              ZC_REQUIRE_NONNULL(initialMetadata).changedAt());

    auto changedRequest =
        compilationRequest(registry, package::SelectedLanguageOptions{false, false, true});
    auto changedValue = CanonicalCompilationOptions::fromVerified(changedRequest);
    ZC_REQUIRE(changedValue != zc::none);
    ZC_IF_SOME(changedOptions, changedValue) {
      auto changedWrite = transaction(database);
      ZC_REQUIRE(changedWrite.set<CompilationOptionsInput>(root, changedOptions).isApplied());
      ZC_REQUIRE(changedWrite.commit().isCommitted());
      auto changed = database.snapshot();
      auto result = changed.get<CompilationOptionsInput>(root);
      ZC_REQUIRE(result.kind() == query::QueryValueKind::Value);
      ZC_EXPECT(result.value() == changedOptions);
      ZC_EXPECT(result.value() != value);
      auto metadata = changed.metadata<CompilationOptionsInput>(root);
      ZC_REQUIRE(metadata != zc::none);
      ZC_EXPECT(ZC_REQUIRE_NONNULL(metadata).changedAt() == changed.revision());
    }
  }
}

ZC_TEST("Incremental binding query active crates use a canonical compilation root set") {
  ZC_EXPECT(ActiveCratesQuery::descriptor.domain == "zom.query.active-crates"_zc);
  ZC_EXPECT(ActiveCratesQuery::descriptor.reuse == query::ReuseClass::Semantic);
  ZC_EXPECT(ActiveCratesQuery::descriptor.retention == query::RetentionClass::Retained);

  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicate = database.registerDescriptor<ActiveCratesQuery>();
  ZC_EXPECT(!duplicate.isRegistered());
  ZC_EXPECT(duplicate.failure() == query::DescriptorRegistrationFailure::SlotAlreadyRegistered);

  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto roots = compilationRootSet(request);
  auto encodedRoots = ActiveCratesQuery::encodeKey(roots);
  auto decodedRoots = ActiveCratesQuery::decodeKey(encodedRoots.asPtr());
  ZC_REQUIRE(decodedRoots != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedRoots) == roots);

  auto multiTargetRoots = compilationRootSet(multiTargetCompilationRequest(registry));
  ZC_REQUIRE(multiTargetRoots.roots().size() == 1);
  ZC_EXPECT(multiTargetRoots == roots);

  auto projectedCore = identity::projectToolchainCoreCrate(crateKey());
  ZC_REQUIRE(projectedCore != zc::none);
  zc::Vector<identity::CrateKey> coreCrates;
  coreCrates.add(zc::mv(ZC_REQUIRE_NONNULL(projectedCore)));
  auto completeRoots = CompilationRootSetQueryKey::fromVerified(request, coreCrates.asPtr());
  ZC_REQUIRE(completeRoots != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(completeRoots).roots().size() == 2);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(completeRoots).roots()[0].kind() !=
            ZC_REQUIRE_NONNULL(completeRoots).roots()[1].kind());

  auto first = stableCrate("incremental_binding_query"_zc);
  auto second = stableCrate("incremental_binding_query_aux"_zc);
  auto crates = crateSet(second, first, first);
  ZC_REQUIRE(crates.crates().size() == 2);
  auto encodedCrates = ActiveCratesQuery::encodeValue(crates);
  auto decodedCrates = ActiveCratesQuery::decodeValue(encodedCrates.asPtr());
  ZC_REQUIRE(decodedCrates != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedCrates) == crates);

  const uint8_t malformed[] = {0xff};
  ZC_EXPECT(ActiveCratesQuery::decodeKey(zc::arrayPtr(malformed)) == zc::none);
  ZC_EXPECT(ActiveCratesQuery::decodeValue(zc::arrayPtr(malformed)) == zc::none);

  auto trailingRoot = zc::heapArray<uint8_t>(encodedRoots.size() + 1);
  for (size_t index = 0; index < encodedRoots.size(); ++index) {
    trailingRoot[index] = encodedRoots[index];
  }
  trailingRoot.back() = 0;
  ZC_EXPECT(ActiveCratesQuery::decodeKey(trailingRoot.asPtr()) == zc::none);

  zc::Vector<CompilationRootKey> duplicateRoots;
  duplicateRoots.add(roots.roots()[0].clone());
  duplicateRoots.add(roots.roots()[0].clone());
  ZC_EXPECT(CompilationRootSetQueryKey::from(zc::mv(duplicateRoots)) == zc::none);

  identity::CanonicalEncoder duplicateCrate;
  duplicateCrate.encodeSequenceSize(2);
  duplicateCrate.encodeByteString(first.canonicalCrateBytes());
  duplicateCrate.encodeByteString(first.canonicalCrateBytes());
  ZC_EXPECT(ActiveCratesQuery::decodeValue(duplicateCrate.finish().asPtr()) == zc::none);
}

ZC_TEST("Incremental binding query active crates derive and shield package graph changes") {
  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto roots = compilationRootSet(request);
  auto packageRoots = packageRootSet(request);
  auto first = stableCrate("incremental_binding_query"_zc);
  auto second = stableCrate("incremental_binding_query_aux"_zc);
  auto firstSet = crateSet(first);
  auto firstGraph =
      singlePackageGraph("incremental_binding_query"_zc, "incremental_binding_query"_zc);

  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto initialWrite = transaction(database);
  ZC_REQUIRE(initialWrite.set<PackageGraphInput>(packageRoots, firstGraph).isApplied());
  ZC_REQUIRE(initialWrite.commit().isCommitted());
  auto initial = database.snapshot();
  auto initialResult = initial.get<ActiveCratesQuery>(roots);
  ZC_REQUIRE(initialResult.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(initialResult.value() == firstSet);
  auto initialMetadata = initial.metadata<ActiveCratesQuery>(roots);
  ZC_REQUIRE(initialMetadata != zc::none);

  auto equalWrite = transaction(database);
  ZC_REQUIRE(equalWrite.set<PackageGraphInput>(packageRoots, firstGraph).isApplied());
  ZC_REQUIRE(equalWrite.commit().isCommitted());
  auto equal = database.snapshot();
  auto equalResult = equal.get<ActiveCratesQuery>(roots);
  ZC_REQUIRE(equalResult.kind() == query::QueryValueKind::Value);
  auto equalMetadata = equal.metadata<ActiveCratesQuery>(roots);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).changedAt());

  auto replacement = crateSet(first, second);
  zc::Vector<zc::Array<uint8_t>> packages;
  packages.add(packageKey().encode());
  zc::Vector<zc::Array<uint8_t>> resolvedEdges;
  zc::Vector<zc::Array<uint8_t>> selectedEdges;
  zc::Vector<zc::Array<uint8_t>> crates;
  crates.add(crateKey().encode());
  crates.add(crateKey("incremental_binding_query_aux"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> crateEdges;
  auto replacementBytes =
      packageGraphBytes(zc::mv(packages), zc::mv(resolvedEdges), zc::mv(selectedEdges),
                        zc::mv(crates), zc::mv(crateEdges));
  auto replacementGraph = PackageGraphInput::decodeValue(replacementBytes.asPtr());
  ZC_REQUIRE(replacementGraph != zc::none);
  auto changedWrite = transaction(database);
  ZC_REQUIRE(changedWrite.set<PackageGraphInput>(packageRoots, ZC_REQUIRE_NONNULL(replacementGraph))
                 .isApplied());
  ZC_REQUIRE(changedWrite.commit().isCommitted());
  auto changed = database.snapshot();
  auto result = changed.get<ActiveCratesQuery>(roots);
  ZC_REQUIRE(result.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(result.value() == replacement);
  auto changedMetadata = changed.metadata<ActiveCratesQuery>(roots);
  ZC_REQUIRE(changedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(changedMetadata).changedAt() == changed.revision());
}

ZC_TEST("Incremental binding query active crates unite user and toolchain core roots") {
  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto packageRoots = packageRootSet(request);
  auto user = stableCrate("incremental_binding_query"_zc);
  auto core = identity::projectToolchainCoreCrate(crateKey());
  ZC_REQUIRE(core != zc::none);
  auto stableCore = StableCrateQueryKey::fromVerified(ZC_REQUIRE_NONNULL(core));
  ZC_REQUIRE(stableCore != zc::none);
  zc::Vector<identity::CrateKey> coreCrates;
  coreCrates.add(ZC_REQUIRE_NONNULL(core).clone());
  auto roots = CompilationRootSetQueryKey::fromVerified(request, coreCrates.asPtr());
  ZC_REQUIRE(roots != zc::none);

  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(core_library_query::registerCoreLibraryQueryProvider(database));
  auto distribution = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(distribution != zc::none);
  auto write = transaction(database);
  ZC_REQUIRE(
      write
          .set<PackageGraphInput>(packageRoots, singlePackageGraph("incremental_binding_query"_zc,
                                                                   "incremental_binding_query"_zc))
          .isApplied());
  ZC_REQUIRE(write
                 .set<core_library_query::CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                                                 ZC_REQUIRE_NONNULL(distribution))
                 .isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto active = database.snapshot().get<ActiveCratesQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!active.isRuntimeFailure());
  ZC_REQUIRE(active.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(active.value() == crateSet(user, ZC_REQUIRE_NONNULL(stableCore)));
}

ZC_TEST("Incremental package graph input admits only a closed canonical graph") {
  ZC_EXPECT(PackageGraphInput::descriptor.domain == "zom.query.package-graph"_zc);
  ZC_EXPECT(PackageGraphInput::descriptor.durability == query::Durability::Medium);

  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto roots = packageRootSet(request);
  auto encodedKey = PackageGraphInput::encodeKey(roots);
  auto decodedKey = PackageGraphInput::decodeKey(encodedKey.asPtr());
  ZC_REQUIRE(decodedKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedKey) == roots);

  auto validBytes =
      singlePackageGraphBytes("incremental_binding_query"_zc, "incremental_binding_query"_zc);
  auto valid = PackageGraphInput::decodeValue(validBytes.asPtr());
  ZC_REQUIRE(valid != zc::none);
  ZC_IF_SOME(value, valid) {
    ZC_EXPECT(value.resolvedPackages().size() == 1);
    ZC_EXPECT(value.resolvedPackageEdges().size() == 0);
    ZC_EXPECT(value.selectedPackageEdges().size() == 0);
    ZC_EXPECT(value.crates().size() == 1);
    ZC_EXPECT(value.crateEdges().size() == 0);
    auto roundTrip = PackageGraphInput::encodeValue(value);
    ZC_EXPECT(roundTrip.asPtr() == validBytes.asPtr());
  }
  ZC_EXPECT(PackageGraphInput::decodeValue(validBytes.slice(0, validBytes.size() - 1)) == zc::none);
  auto trailing = zc::heapArray<uint8_t>(validBytes.size() + 1);
  for (size_t index = 0; index < validBytes.size(); ++index) {
    trailing[index] = validBytes[index];
  }
  trailing.back() = 0;
  ZC_EXPECT(PackageGraphInput::decodeValue(trailing.asPtr()) == zc::none);

  zc::Vector<zc::Array<uint8_t>> duplicatePackages;
  duplicatePackages.add(packageKey("app"_zc).encode());
  duplicatePackages.add(packageKey("app"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> noResolvedEdges;
  zc::Vector<zc::Array<uint8_t>> noSelectedEdges;
  zc::Vector<zc::Array<uint8_t>> oneCrate;
  oneCrate.add(crateKey(packageKey("app"_zc), "app"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> noCrateEdges;
  auto duplicate =
      packageGraphBytes(zc::mv(duplicatePackages), zc::mv(noResolvedEdges), zc::mv(noSelectedEdges),
                        zc::mv(oneCrate), zc::mv(noCrateEdges));
  ZC_EXPECT(PackageGraphInput::decodeValue(duplicate.asPtr()) == zc::none);

  auto dependency = packageEdge("app"_zc, "dep"_zc, "dep"_zc);
  auto dependencyBytes = dependency.encode();
  auto expanded = crateEdge("app"_zc, "dep"_zc, "dep"_zc);
  auto expandedBytes = expanded.encode();
  zc::Vector<zc::Array<uint8_t>> danglingPackages;
  danglingPackages.add(packageKey("app"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> danglingResolved;
  danglingResolved.add(zc::heapArray<uint8_t>(dependencyBytes.asPtr()));
  zc::Vector<zc::Array<uint8_t>> danglingSelected;
  zc::Vector<zc::Array<uint8_t>> danglingCrates;
  danglingCrates.add(crateKey(packageKey("app"_zc), "app"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> danglingCrateEdges;
  auto dangling = packageGraphBytes(zc::mv(danglingPackages), zc::mv(danglingResolved),
                                    zc::mv(danglingSelected), zc::mv(danglingCrates),
                                    zc::mv(danglingCrateEdges));
  ZC_EXPECT(PackageGraphInput::decodeValue(dangling.asPtr()) == zc::none);

  zc::Vector<zc::Array<uint8_t>> selectedPackages;
  selectedPackages.add(packageKey("app"_zc).encode());
  selectedPackages.add(packageKey("dep"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> selectedResolved;
  zc::Vector<zc::Array<uint8_t>> selectedOutsideResolution;
  selectedOutsideResolution.add(zc::heapArray<uint8_t>(dependencyBytes.asPtr()));
  zc::Vector<zc::Array<uint8_t>> selectedCrates;
  selectedCrates.add(crateKey(packageKey("app"_zc), "app"_zc).encode());
  selectedCrates.add(crateKey(packageKey("dep"_zc), "dep"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> selectedCrateEdges;
  selectedCrateEdges.add(zc::heapArray<uint8_t>(expandedBytes.asPtr()));
  auto outsideResolution = packageGraphBytes(zc::mv(selectedPackages), zc::mv(selectedResolved),
                                             zc::mv(selectedOutsideResolution),
                                             zc::mv(selectedCrates), zc::mv(selectedCrateEdges));
  ZC_EXPECT(PackageGraphInput::decodeValue(outsideResolution.asPtr()) == zc::none);

  zc::Vector<zc::Array<uint8_t>> unprojectedPackages;
  unprojectedPackages.add(packageKey("app"_zc).encode());
  unprojectedPackages.add(packageKey("dep"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> unprojectedResolved;
  unprojectedResolved.add(zc::heapArray<uint8_t>(dependencyBytes.asPtr()));
  zc::Vector<zc::Array<uint8_t>> unprojectedSelected;
  unprojectedSelected.add(zc::heapArray<uint8_t>(dependencyBytes.asPtr()));
  zc::Vector<zc::Array<uint8_t>> unprojectedCrates;
  unprojectedCrates.add(crateKey(packageKey("app"_zc), "app"_zc).encode());
  unprojectedCrates.add(crateKey(packageKey("dep"_zc), "dep"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> missingProjection;
  auto unprojected = packageGraphBytes(zc::mv(unprojectedPackages), zc::mv(unprojectedResolved),
                                       zc::mv(unprojectedSelected), zc::mv(unprojectedCrates),
                                       zc::mv(missingProjection));
  ZC_EXPECT(PackageGraphInput::decodeValue(unprojected.asPtr()) == zc::none);

  auto reverseDependency = packageEdge("dep"_zc, "app"_zc, "app"_zc);
  auto reverseExpanded = crateEdge("dep"_zc, "app"_zc, "app"_zc);
  zc::Vector<zc::Array<uint8_t>> cyclicPackages;
  cyclicPackages.add(packageKey("app"_zc).encode());
  cyclicPackages.add(packageKey("dep"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> cyclicResolved;
  cyclicResolved.add(zc::heapArray<uint8_t>(dependencyBytes.asPtr()));
  cyclicResolved.add(reverseDependency.encode());
  zc::Vector<zc::Array<uint8_t>> cyclicSelected;
  cyclicSelected.add(zc::heapArray<uint8_t>(dependencyBytes.asPtr()));
  cyclicSelected.add(reverseDependency.encode());
  zc::Vector<zc::Array<uint8_t>> cyclicCrates;
  cyclicCrates.add(crateKey(packageKey("app"_zc), "app"_zc).encode());
  cyclicCrates.add(crateKey(packageKey("dep"_zc), "dep"_zc).encode());
  zc::Vector<zc::Array<uint8_t>> cyclicEdges;
  cyclicEdges.add(zc::heapArray<uint8_t>(expandedBytes.asPtr()));
  cyclicEdges.add(reverseExpanded.encode());
  auto cycle = packageGraphBytes(zc::mv(cyclicPackages), zc::mv(cyclicResolved),
                                 zc::mv(cyclicSelected), zc::mv(cyclicCrates), zc::mv(cyclicEdges));
  ZC_EXPECT(PackageGraphInput::decodeValue(cycle.asPtr()) == zc::none);
}

ZC_TEST("Incremental package graph input backdates equals and replaces changes") {
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicate = database.registerDescriptor<PackageGraphInput>();
  ZC_EXPECT(!duplicate.isRegistered());
  ZC_EXPECT(duplicate.failure() == query::DescriptorRegistrationFailure::SlotAlreadyRegistered);
  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto roots = packageRootSet(request);
  auto first = singlePackageGraph("incremental_binding_query"_zc, "incremental_binding_query"_zc);

  auto firstWrite = transaction(database);
  ZC_REQUIRE(firstWrite.set<PackageGraphInput>(roots, first).isApplied());
  ZC_REQUIRE(firstWrite.commit().isCommitted());
  auto firstSnapshot = database.snapshot();
  auto firstMetadata = firstSnapshot.metadata<PackageGraphInput>(roots);
  ZC_REQUIRE(firstMetadata != zc::none);

  auto equalWrite = transaction(database);
  ZC_REQUIRE(equalWrite.set<PackageGraphInput>(roots, first).isApplied());
  ZC_REQUIRE(equalWrite.commit().isCommitted());
  auto equalSnapshot = database.snapshot();
  auto equalMetadata = equalSnapshot.metadata<PackageGraphInput>(roots);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstMetadata).changedAt());

  auto changed =
      singlePackageGraph("incremental_binding_query"_zc, "incremental_binding_query_aux"_zc);
  auto changedWrite = transaction(database);
  ZC_REQUIRE(changedWrite.set<PackageGraphInput>(roots, changed).isApplied());
  ZC_REQUIRE(changedWrite.commit().isCommitted());
  auto changedSnapshot = database.snapshot();
  auto result = changedSnapshot.get<PackageGraphInput>(roots);
  ZC_REQUIRE(result.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(result.value() == changed);
  ZC_EXPECT(result.value() != first);
  auto changedMetadata = changedSnapshot.metadata<PackageGraphInput>(roots);
  ZC_REQUIRE(changedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(changedMetadata).changedAt() == changedSnapshot.revision());
}

ZC_TEST("Incremental binding query source snapshot codec is fixed bounded and self checking") {
  auto key = sourceQueryKey("codec.zom"_zc);
  auto encodedKey = SourceSnapshotInput::encodeKey(key);
  auto decodedKey = SourceSnapshotInput::decodeKey(encodedKey.asPtr());
  ZC_REQUIRE(decodedKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedKey) == key);

  auto value = sourceSnapshotValue("codec.zom"_zc, zc::heapArray("abc"_zcb));
  auto encoded = SourceSnapshotInput::encodeValue(value);
  const uint8_t abcDigest[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                               0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                               0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  ZC_REQUIRE(encoded.size() == 43);
  ZC_EXPECT(encoded.slice(0, 32) == zc::arrayPtr(abcDigest));
  for (size_t index = 32; index < 39; ++index) { ZC_EXPECT(encoded[index] == 0); }
  ZC_EXPECT(encoded[39] == 3);
  ZC_EXPECT(encoded.slice(40, 43) == "abc"_zcb);
  auto decoded = SourceSnapshotInput::decodeValue(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == value);

  auto corruptDigest = zc::heapArray<uint8_t>(encoded.asPtr());
  corruptDigest[0] ^= 0x01;
  ZC_EXPECT(SourceSnapshotInput::decodeValue(corruptDigest.asPtr()) == zc::none);

  auto corruptBytes = zc::heapArray<uint8_t>(encoded.asPtr());
  corruptBytes.back() ^= 0x01;
  ZC_EXPECT(SourceSnapshotInput::decodeValue(corruptBytes.asPtr()) == zc::none);
  ZC_EXPECT(SourceSnapshotInput::decodeValue(encoded.slice(0, encoded.size() - 1)) == zc::none);

  auto trailing = zc::heapArray<uint8_t>(encoded.size() + 1);
  for (size_t index = 0; index < encoded.size(); ++index) { trailing[index] = encoded[index]; }
  trailing.back() = 0;
  ZC_EXPECT(SourceSnapshotInput::decodeValue(trailing.asPtr()) == zc::none);

  identity::CanonicalEncoder oversized;
  identity::Sha256Digest zeroDigest;
  oversized.encodeDigest(zeroDigest);
  oversized.encodeUint64(64 * 1024 * 1024 + 1);
  ZC_EXPECT(SourceSnapshotInput::decodeValue(oversized.finish().asPtr()) == zc::none);

  auto oversizedKey = zc::heapArray<uint8_t>(64 * 1024 + 1);
  ZC_EXPECT(SourceSnapshotInput::decodeKey(oversizedKey.asPtr()) == zc::none);
  const uint8_t malformedSource[] = {0xff};
  ZC_EXPECT(SourceSnapshotInput::decodeKey(zc::arrayPtr(malformedSource)) == zc::none);
  auto trailingKey = zc::heapArray<uint8_t>(encodedKey.size() + 1);
  for (size_t index = 0; index < encodedKey.size(); ++index) {
    trailingKey[index] = encodedKey[index];
  }
  trailingKey.back() = 0;
  ZC_EXPECT(SourceSnapshotInput::decodeKey(trailingKey.asPtr()) == zc::none);
}

ZC_TEST("Incremental binding query per crate source roots are strict and replaceable") {
  ZC_EXPECT(UserPackageActiveSourcesInput::descriptor.domain ==
            "zom.query.user-package-active-sources"_zc);
  ZC_EXPECT(UserPackageActiveSourcesInput::descriptor.durability == query::Durability::Low);
  ZC_EXPECT(ActiveSourcesQuery::descriptor.domain == "zom.query.active-sources"_zc);
  ZC_EXPECT(ActiveSourcesQuery::descriptor.reuse == query::ReuseClass::Semantic);
  auto crate = stableCrate("incremental_binding_query"_zc);
  auto encodedCrate = UserPackageActiveSourcesInput::encodeKey(crate);
  auto decodedCrate = UserPackageActiveSourcesInput::decodeKey(encodedCrate.asPtr());
  ZC_REQUIRE(decodedCrate != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedCrate) == crate);
  auto firstSource = sourceQueryKey("first.zom"_zc);
  auto secondSource = sourceQueryKey("second.zom"_zc);
  auto sources = sourceSet(secondSource, firstSource, firstSource);
  ZC_REQUIRE(sources.sources().size() == 2);
  auto encodedSources = UserPackageActiveSourcesInput::encodeValue(sources);
  auto decodedSources = UserPackageActiveSourcesInput::decodeValue(encodedSources.asPtr());
  ZC_REQUIRE(decodedSources != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSources) == sources);

  identity::CanonicalEncoder duplicateSources;
  duplicateSources.encodeSequenceSize(2);
  duplicateSources.encodeByteString(firstSource.canonicalSourceBytes());
  duplicateSources.encodeByteString(firstSource.canonicalSourceBytes());
  ZC_EXPECT(UserPackageActiveSourcesInput::decodeValue(duplicateSources.finish().asPtr()) ==
            zc::none);
  const uint8_t malformed[] = {0xff};
  ZC_EXPECT(UserPackageActiveSourcesInput::decodeKey(zc::arrayPtr(malformed)) == zc::none);
  ZC_EXPECT(UserPackageActiveSourcesInput::decodeValue(zc::arrayPtr(malformed)) == zc::none);

  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto duplicateInput = database.registerDescriptor<UserPackageActiveSourcesInput>();
  ZC_EXPECT(!duplicateInput.isRegistered());
  auto duplicateQuery = database.registerDescriptor<ActiveSourcesQuery>();
  ZC_EXPECT(!duplicateQuery.isRegistered());
  auto firstWrite = transaction(database);
  ZC_REQUIRE(firstWrite.set<UserPackageActiveSourcesInput>(crate, sources).isApplied());
  ZC_REQUIRE(firstWrite.commit().isCommitted());
  auto first = database.snapshot();
  auto firstDerived = first.get<ActiveSourcesQuery>(crate);
  ZC_REQUIRE(firstDerived.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(firstDerived.value() == sources);
  auto firstMetadata = first.metadata<UserPackageActiveSourcesInput>(crate);
  ZC_REQUIRE(firstMetadata != zc::none);

  auto equalWrite = transaction(database);
  ZC_REQUIRE(equalWrite.set<UserPackageActiveSourcesInput>(crate, sources).isApplied());
  ZC_REQUIRE(equalWrite.commit().isCommitted());
  auto equal = database.snapshot();
  auto equalMetadata = equal.metadata<UserPackageActiveSourcesInput>(crate);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstMetadata).changedAt());

  auto replacement = sourceSet(secondSource);
  auto changedWrite = transaction(database);
  ZC_REQUIRE(changedWrite.set<UserPackageActiveSourcesInput>(crate, replacement).isApplied());
  ZC_REQUIRE(changedWrite.commit().isCommitted());
  auto changed = database.snapshot();
  auto changedResult = changed.get<UserPackageActiveSourcesInput>(crate);
  ZC_REQUIRE(changedResult.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(changedResult.value() == replacement);
  auto changedMetadata = changed.metadata<UserPackageActiveSourcesInput>(crate);
  ZC_REQUIRE(changedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(changedMetadata).changedAt() == changed.revision());
}

ZC_TEST(
    "Incremental binding query source snapshots preserve equals replace changes and erase stale") {
  auto database = queryTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto firstKey = sourceQueryKey("first.zom"_zc);
  auto staleKey = sourceQueryKey("stale.zom"_zc);
  auto firstValue = sourceSnapshotValue("first.zom"_zc, zc::heapArray("first"_zcb));
  auto staleValue = sourceSnapshotValue("stale.zom"_zc, zc::heapArray("stale"_zcb));

  auto initialWrite = transaction(database);
  ZC_REQUIRE(initialWrite.set<SourceSnapshotInput>(firstKey, firstValue).isApplied());
  ZC_REQUIRE(initialWrite.set<SourceSnapshotInput>(staleKey, staleValue).isApplied());
  ZC_REQUIRE(initialWrite.commit().isCommitted());
  auto initial = database.snapshot();
  auto initialMetadata = initial.metadata<SourceSnapshotInput>(firstKey);
  ZC_REQUIRE(initialMetadata != zc::none);

  auto equalReplacement = transaction(database);
  ZC_REQUIRE(equalReplacement.set<SourceSnapshotInput>(firstKey, firstValue).isApplied());
  ZC_REQUIRE(equalReplacement.set<SourceSnapshotInput>(staleKey, staleValue).isApplied());
  ZC_REQUIRE(equalReplacement.commit().isCommitted());
  auto equal = database.snapshot();
  auto equalMetadata = equal.metadata<SourceSnapshotInput>(firstKey);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).changedAt());

  auto changedValue = sourceSnapshotValue("first.zom"_zc, zc::heapArray("changed"_zcb));
  auto changedRoot = transaction(database);
  ZC_REQUIRE(changedRoot.erase<SourceSnapshotInput>(staleKey).isApplied());
  ZC_REQUIRE(changedRoot.set<SourceSnapshotInput>(firstKey, changedValue).isApplied());
  ZC_REQUIRE(changedRoot.commit().isCommitted());
  auto changed = database.snapshot();
  auto changedResult = changed.get<SourceSnapshotInput>(firstKey);
  ZC_REQUIRE(changedResult.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(changedResult.value() == changedValue);
  auto changedMetadata = changed.metadata<SourceSnapshotInput>(firstKey);
  ZC_REQUIRE(changedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(changedMetadata).changedAt() == changed.revision());

  auto missing = changed.get<SourceSnapshotInput>(staleKey);
  ZC_REQUIRE(missing.isRuntimeFailure());
  ZC_EXPECT(missing.runtimeFailure() == query::QueryRuntimeFailure::MissingInput);
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
