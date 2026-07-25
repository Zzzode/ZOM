// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"

#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/named-identity-inventory-query.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/parser/parse-source-query.h"

namespace zomlang::compiler::driver::incremental_binding_query {
namespace {

using namespace identity::source_query;

basic::ThreadPool& queryTestScheduler() {
  static basic::ThreadPool scheduler(4);
  return scheduler;
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

identity::CompilationConfigKey compilation() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto result = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, target(),
      identity::SemanticCompilerOptionsKey::from(2026, true, false, false), zc::mv(noBuildScript));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid compilation fixture");
}

identity::CrateKey crateKey(zc::StringPtr targetName) {
  auto result = identity::CrateKey::from(packageKey(), identity::CrateTargetKind::Library,
                                         scalar<identity::TargetName>(targetName), compilation());
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid crate fixture");
}

identity::CrateKey crateKey() { return crateKey("incremental_binding_query"_zc); }

identity::CrateKey crateKey(identity::PackageKey&& package, zc::StringPtr targetName) {
  auto result = identity::CrateKey::from(zc::mv(package), identity::CrateTargetKind::Library,
                                         scalar<identity::TargetName>(targetName), compilation());
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
  auto result = identity::CrateDependencyEdgeKey::from(packageEdge(consumer, alias, provider),
                                                       crateKey(packageKey(consumer), consumer),
                                                       crateKey(packageKey(provider), provider));
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

SelectedModuleSource selectedSource(zc::StringPtr moduleName, zc::StringPtr sourceName) {
  auto semantic = semanticModule(moduleName);
  auto sourceKey = source(sourceName);
  auto projected = SelectedModuleSource::fromVerified(semantic, sourceKey);
  return zc::mv(ZC_REQUIRE_NONNULL(projected));
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

CanonicalModuleSet setOf() {
  zc::Vector<StableModuleQueryKey> modules;
  auto result = CanonicalModuleSet::from(zc::mv(modules));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

template <typename... Rest>
CanonicalModuleSet setOf(const StableModuleQueryKey& first, const Rest&... rest) {
  zc::Vector<StableModuleQueryKey> modules;
  modules.add(first.clone());
  (modules.add(rest.clone()), ...);
  auto result = CanonicalModuleSet::from(zc::mv(modules));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

template <typename... Modules>
ModuleDependencySet dependencies(const Modules&... modules) {
  return ModuleDependencySet::present(setOf(modules...));
}

query::InputTransaction transaction(query::QueryDatabase& database) {
  auto result = database.beginInputTransaction();
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

PackageRootSetQueryKey packageRootSet(const package::VerifiedPackageCompilationRequest& request) {
  auto result = PackageRootSetQueryKey::fromVerified(request);
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

PackageRootSetQueryKey topologyRoot() {
  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  return packageRootSet(request);
}

void stageActiveModules(query::InputTransaction& write, const PackageRootSetQueryKey& root,
                        const CanonicalModuleSet& modules) {
  auto crate = stableCrate("incremental_binding_query"_zc);
  auto crates = crateSet(crate);
  ZC_REQUIRE(write.set<ActiveCratesInput>(root, crates));
  ZC_REQUIRE(write.set<ActiveModulesInput>(crate, modules));
}

bool hasEvent(zc::ArrayPtr<const query::QueryEvent> events, query::QueryEventKind kind) {
  for (const auto& event : events) {
    if (event.kind() == kind) { return true; }
  }
  return false;
}

ModuleBindingOrderFailure requireFailure(
    const query::TypedQueryResult<ModuleBindingOrder>& result) {
  ZC_REQUIRE(!result.isRuntimeFailure());
  ZC_REQUIRE(result.kind() == query::QueryValueKind::SemanticFailure);
  auto failure = ModuleBindingOrderFailure::decode(result.semanticFailureBytes());
  return zc::mv(ZC_REQUIRE_NONNULL(failure));
}

void expectOrder(const ModuleBindingOrder& order,
                 zc::ArrayPtr<const StableModuleQueryKey> expected) {
  ZC_REQUIRE(order.modules().size() == expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    ZC_EXPECT(order.modules()[index] == expected[index]);
  }
}

struct Modules final {
  Modules() : a(module("a"_zc)), b(module("b"_zc)), c(module("c"_zc)), d(module("d"_zc)) {}

  StableModuleQueryKey a;
  StableModuleQueryKey b;
  StableModuleQueryKey c;
  StableModuleQueryKey d;
};

}  // namespace

ZC_TEST("Incremental binding query canonicalizes stable module keys and sets") {
  Modules modules;
  auto encoded = ModuleDependenciesInput::encodeKey(modules.a);
  auto decoded = ModuleDependenciesInput::decodeKey(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == modules.a);

  auto canonical = setOf(modules.c, modules.a, modules.b, modules.a);
  ZC_REQUIRE(canonical.modules().size() == 3);
  ZC_EXPECT(canonical.modules()[0] == modules.a);
  ZC_EXPECT(canonical.modules()[1] == modules.b);
  ZC_EXPECT(canonical.modules()[2] == modules.c);

  auto oversized = zc::heapArray<uint8_t>(16 * 1024 + 1);
  ZC_EXPECT(ModuleDependenciesInput::decodeKey(oversized.asPtr()) == zc::none);
  const uint8_t malformedModule[] = {0xff};
  ZC_EXPECT(ModuleDependenciesInput::decodeKey(zc::arrayPtr(malformedModule)) == zc::none);
  auto trailingModule = zc::heapArray<uint8_t>(encoded.size() + 1);
  for (size_t index = 0; index < encoded.size(); ++index) {
    trailingModule[index] = encoded[index];
  }
  trailingModule.back() = 0;
  ZC_EXPECT(ModuleDependenciesInput::decodeKey(trailingModule.asPtr()) == zc::none);

  identity::CanonicalEncoder malformed;
  malformed.encodeSequenceSize(2);
  malformed.encodeByteString(modules.b.canonicalModuleBytes());
  malformed.encodeByteString(modules.a.canonicalModuleBytes());
  ZC_EXPECT(ActiveModulesInput::decodeValue(malformed.finish().asPtr()) == zc::none);
}

ZC_TEST("Incremental binding query tracks the exact selected source per module") {
  Modules modules;
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));

  auto firstSource = selectedSource("a"_zc, "a.zom"_zc);
  auto encoded = SelectedModuleSourceInput::encodeValue(firstSource);
  auto decoded = SelectedModuleSourceInput::decodeValue(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == firstSource);

  auto firstWrite = transaction(database);
  ZC_REQUIRE(firstWrite.set<SelectedModuleSourceInput>(modules.a, firstSource));
  ZC_REQUIRE(firstWrite.commit() != zc::none);
  auto first = database.snapshot().get<SelectedModuleSourceInput>(modules.a);
  ZC_REQUIRE(first.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(first.value() == firstSource);

  auto secondSource = selectedSource("a"_zc, "replacement.zom"_zc);
  auto replacement = transaction(database);
  ZC_REQUIRE(replacement.set<SelectedModuleSourceInput>(modules.a, secondSource));
  ZC_REQUIRE(replacement.commit() != zc::none);
  auto second = database.snapshot().get<SelectedModuleSourceInput>(modules.a);
  ZC_REQUIRE(second.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(second.value() == secondSource);
  ZC_EXPECT(second.value() != firstSource);

  auto erase = transaction(database);
  ZC_REQUIRE(erase.erase<SelectedModuleSourceInput>(modules.a));
  ZC_REQUIRE(erase.commit() != zc::none);
  auto missing = database.snapshot().get<SelectedModuleSourceInput>(modules.a);
  ZC_REQUIRE(missing.isRuntimeFailure());
  ZC_EXPECT(missing.runtimeFailure() == query::QueryRuntimeFailure::MissingInput);

  auto oversized = zc::heapArray<uint8_t>(64 * 1024 + 1);
  ZC_EXPECT(SelectedModuleSourceInput::decodeValue(oversized.asPtr()) == zc::none);
  const uint8_t malformedSource[] = {0xff};
  ZC_EXPECT(SelectedModuleSourceInput::decodeValue(zc::arrayPtr(malformedSource)) == zc::none);
  auto trailingSource = zc::heapArray<uint8_t>(encoded.size() + 1);
  for (size_t index = 0; index < encoded.size(); ++index) {
    trailingSource[index] = encoded[index];
  }
  trailingSource.back() = 0;
  ZC_EXPECT(SelectedModuleSourceInput::decodeValue(trailingSource.asPtr()) == zc::none);
}

ZC_TEST("Incremental binding query registers low durability source snapshot inputs") {
  auto contract = SourceSnapshotInput::contract();
  ZC_EXPECT(contract.domain() == "zom.query.source-snapshot"_zc);
  ZC_EXPECT(contract.isInput());
  ZC_EXPECT(contract.inputDurability() == query::Durability::Low);

  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_EXPECT(database.registerInputKind<SourceSnapshotInput>() == zc::none);
}

ZC_TEST("Incremental binding query registers medium durability compilation options") {
  auto contract = CompilationOptionsInput::contract();
  ZC_EXPECT(contract.domain() == "zom.query.compilation-options"_zc);
  ZC_EXPECT(contract.isInput());
  ZC_EXPECT(contract.inputDurability() == query::Durability::Medium);

  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_EXPECT(database.registerInputKind<CompilationOptionsInput>() == zc::none);
}

ZC_TEST("Incremental binding query registers revision local evictable source parsing") {
  auto contract = parser::ParseSourceQuery::contract();
  ZC_EXPECT(contract.domain() == "zom.query.parse-source"_zc);
  ZC_EXPECT(!contract.isInput());
  ZC_EXPECT(contract.reuseClass() == query::ReuseClass::RevisionLocal);
  ZC_EXPECT(contract.retention() == query::RetentionClass::Evictable);

  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_EXPECT(database.registerDerivedKind<parser::ParseSourceQuery>() == zc::none);
}

ZC_TEST("Incremental binding query parses one source from its exact tracked inputs") {
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("parse-success.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("parse-success.zom"_zc, zc::heapArray("let value = 42;"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue));
  ZC_REQUIRE(write.set<CompilationOptionsInput>(CompilationUnitQueryKey::fixed(), options));
  ZC_REQUIRE(write.commit() != zc::none);

  auto snapshot = database.snapshot();
  auto result = snapshot.get<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(!result.isRuntimeFailure());
  ZC_REQUIRE(result.kind() == query::QueryValueKind::Value);
  const auto& parsed = result.value();
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
  auto optionsFingerprint =
      snapshot.keyFingerprint<CompilationOptionsInput>(CompilationUnitQueryKey::fixed());
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
  ZC_REQUIRE(snapshot.evictValue<parser::ParseSourceQuery>(sourceKey));
  ZC_EXPECT(!snapshot.hasRetainedValue<parser::ParseSourceQuery>(sourceKey));
  auto rebuilt = snapshot.get<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(!rebuilt.isRuntimeFailure());
  ZC_REQUIRE(rebuilt.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(rebuilt.value().encodeCanonical().asPtr() == encoded.asPtr());
  ZC_EXPECT(snapshot.hasRetainedValue<parser::ParseSourceQuery>(sourceKey));
}

ZC_TEST("Incremental binding query publishes strict deterministic parse rejection") {
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("parse-rejected.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("parse-rejected.zom"_zc, zc::heapArray("let value = ;"_zcb));
  auto registry = targetRegistry();
  auto options =
      compilationOptionsValue(registry, package::SelectedLanguageOptions{false, true, false});
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue));
  ZC_REQUIRE(write.set<CompilationOptionsInput>(CompilationUnitQueryKey::fixed(), options));
  ZC_REQUIRE(write.commit() != zc::none);

  auto result = database.snapshot().get<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(!result.isRuntimeFailure());
  ZC_REQUIRE(result.kind() == query::QueryValueKind::SemanticFailure);
  auto rejected = parser::ParseRejected::decodeCanonical(result.semanticFailureBytes());
  ZC_REQUIRE(rejected != zc::none);
  const auto& failure = ZC_REQUIRE_NONNULL(rejected);
  ZC_EXPECT(failure.canonicalSourceKey() == sourceKey.canonicalSourceBytes());
  ZC_EXPECT(failure.contentDigest() == sourceValue.contentDigest());
  ZC_EXPECT(failure.sourceByteLength() == sourceValue.bytes().size());
  ZC_EXPECT(failure.options() == (parser::CanonicalParserOptions{false, true, false}));
  ZC_REQUIRE(failure.facts().size() != 0);

  auto encoded = failure.encodeCanonical();
  auto trailing = zc::heapArray<uint8_t>(encoded.size() + 1);
  for (size_t index = 0; index < encoded.size(); ++index) { trailing[index] = encoded[index]; }
  trailing.back() = 0;
  ZC_EXPECT(parser::ParseRejected::decodeCanonical(trailing.asPtr()) == zc::none);
  auto malformed = zc::heapArray<uint8_t>(encoded.asPtr());
  malformed[0] ^= 0x01;
  ZC_EXPECT(parser::ParseRejected::decodeCanonical(malformed.asPtr()) == zc::none);
}

ZC_TEST("Incremental binding query publishes verified stable named inventories") {
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto moduleKey = module("root"_zc);
  auto selected = selectedSource("root"_zc, "root.zom"_zc);
  auto sourceKey = sourceQueryKey("root.zom"_zc);
  auto sourceValue = sourceSnapshotValue(
      "root.zom"_zc,
      zc::heapArray("module root;\nclass Alpha {}\nstruct Beta {}\nimpl Trait for i32 {}\n"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SelectedModuleSourceInput>(moduleKey, selected));
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue));
  ZC_REQUIRE(write.set<CompilationOptionsInput>(CompilationUnitQueryKey::fixed(), options));
  ZC_REQUIRE(write.commit() != zc::none);

  auto snapshot = database.snapshot();
  auto definitions = snapshot.get<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(!definitions.isRuntimeFailure());
  ZC_REQUIRE(definitions.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(definitions.value().entries().size() == 2);
  auto implementations = snapshot.get<NamedImplementationInventoryQuery>(moduleKey);
  ZC_REQUIRE(!implementations.isRuntimeFailure());
  ZC_REQUIRE(implementations.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(implementations.value().keys().size() == 1);
  auto definitionSites = snapshot.get<RevisionLocalDefinitionSitesQuery>(moduleKey);
  ZC_REQUIRE(!definitionSites.isRuntimeFailure());
  ZC_REQUIRE(definitionSites.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(definitionSites.value().entries().size() == 2);
  auto implementationSites = snapshot.get<RevisionLocalImplementationSitesQuery>(moduleKey);
  ZC_REQUIRE(!implementationSites.isRuntimeFailure());
  ZC_REQUIRE(implementationSites.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(implementationSites.value().entries().size() == 1);
  auto moduleBodySyntax = snapshot.get<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(!moduleBodySyntax.isRuntimeFailure());
  ZC_REQUIRE(moduleBodySyntax.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(moduleBodySyntax.value().rootCount() == 3);
  auto moduleBodyProvenance = snapshot.get<ModuleBodyProvenanceQuery>(moduleKey);
  ZC_REQUIRE(!moduleBodyProvenance.isRuntimeFailure());
  ZC_REQUIRE(moduleBodyProvenance.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(moduleBodyProvenance.value().entries().size() != 0);

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

  auto definitionSiteBytes = definitionSites.value().encodeCanonical();
  auto decodedDefinitionSites =
      binder::RevisionLocalDefinitionSites::decodeCanonical(definitionSiteBytes.asPtr());
  ZC_REQUIRE(decodedDefinitionSites != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedDefinitionSites).sameAs(definitionSites.value()));
  auto implementationSiteBytes = implementationSites.value().encodeCanonical();
  auto decodedImplementationSites =
      binder::RevisionLocalImplementationSites::decodeCanonical(implementationSiteBytes.asPtr());
  ZC_REQUIRE(decodedImplementationSites != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedImplementationSites).sameAs(implementationSites.value()));
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
  auto moduleBodyProvenanceBytes = moduleBodyProvenance.value().encodeCanonical();
  auto decodedModuleBodyProvenance =
      binder::ModuleBodyProvenance::decodeCanonical(moduleBodyProvenanceBytes.asPtr());
  ZC_REQUIRE(decodedModuleBodyProvenance != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(decodedModuleBodyProvenance) == moduleBodyProvenance.value());
  auto trailingModuleBodyProvenance = zc::heapArray<uint8_t>(moduleBodyProvenanceBytes.size() + 1);
  for (size_t index = 0; index < moduleBodyProvenanceBytes.size(); ++index) {
    trailingModuleBodyProvenance[index] = moduleBodyProvenanceBytes[index];
  }
  trailingModuleBodyProvenance.back() = 0;
  ZC_EXPECT(binder::ModuleBodyProvenance::decodeCanonical(trailingModuleBodyProvenance.asPtr()) ==
            zc::none);

  auto selectedFingerprint = snapshot.keyFingerprint<SelectedModuleSourceInput>(moduleKey);
  auto parseFingerprint = snapshot.keyFingerprint<parser::ParseSourceQuery>(sourceKey);
  ZC_REQUIRE(selectedFingerprint != zc::none);
  ZC_REQUIRE(parseFingerprint != zc::none);
  for (const auto& groups : {snapshot.dependencies<NamedDefinitionInventoryQuery>(moduleKey),
                             snapshot.dependencies<NamedImplementationInventoryQuery>(moduleKey)}) {
    size_t selectedReads = 0;
    size_t parseReads = 0;
    for (const auto& group : groups) {
      ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
      ZC_REQUIRE(group.dependencies().size() == 1);
      const auto& fingerprint = group.dependencies()[0].key().fingerprint();
      if (fingerprint == ZC_ASSERT_NONNULL(selectedFingerprint)) {
        ++selectedReads;
      } else if (fingerprint == ZC_ASSERT_NONNULL(parseFingerprint)) {
        ++parseReads;
      } else {
        ZC_FAIL_REQUIRE("Named inventory recorded an undeclared dependency");
      }
    }
    ZC_EXPECT(selectedReads == 2);
    ZC_EXPECT(parseReads == 2);
  }

  auto definitionFingerprint = snapshot.keyFingerprint<NamedDefinitionInventoryQuery>(moduleKey);
  auto implementationFingerprint =
      snapshot.keyFingerprint<NamedImplementationInventoryQuery>(moduleKey);
  auto definitionSitesFingerprint =
      snapshot.keyFingerprint<RevisionLocalDefinitionSitesQuery>(moduleKey);
  auto implementationSitesFingerprint =
      snapshot.keyFingerprint<RevisionLocalImplementationSitesQuery>(moduleKey);
  auto moduleBodySyntaxFingerprint = snapshot.keyFingerprint<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(definitionFingerprint != zc::none);
  ZC_REQUIRE(implementationFingerprint != zc::none);
  ZC_REQUIRE(definitionSitesFingerprint != zc::none);
  ZC_REQUIRE(implementationSitesFingerprint != zc::none);
  ZC_REQUIRE(moduleBodySyntaxFingerprint != zc::none);

  size_t syntaxSelectedReads = 0;
  size_t syntaxParseReads = 0;
  size_t syntaxDefinitionReads = 0;
  size_t syntaxImplementationReads = 0;
  size_t syntaxDefinitionSiteReads = 0;
  size_t syntaxImplementationSiteReads = 0;
  for (const auto& group : snapshot.dependencies<ModuleBodySyntaxQuery>(moduleKey)) {
    ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
    ZC_REQUIRE(group.dependencies().size() == 1);
    const auto& fingerprint = group.dependencies()[0].key().fingerprint();
    if (fingerprint == ZC_ASSERT_NONNULL(selectedFingerprint)) {
      ++syntaxSelectedReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(parseFingerprint)) {
      ++syntaxParseReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(definitionFingerprint)) {
      ++syntaxDefinitionReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(implementationFingerprint)) {
      ++syntaxImplementationReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(definitionSitesFingerprint)) {
      ++syntaxDefinitionSiteReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(implementationSitesFingerprint)) {
      ++syntaxImplementationSiteReads;
    } else {
      ZC_FAIL_REQUIRE("Module body syntax recorded an undeclared dependency");
    }
  }
  ZC_EXPECT(syntaxSelectedReads == 2);
  ZC_EXPECT(syntaxParseReads == 2);
  ZC_EXPECT(syntaxDefinitionReads == 2);
  ZC_EXPECT(syntaxImplementationReads == 2);
  ZC_EXPECT(syntaxDefinitionSiteReads == 2);
  ZC_EXPECT(syntaxImplementationSiteReads == 2);

  size_t provenanceSelectedReads = 0;
  size_t provenanceParseReads = 0;
  size_t provenanceSyntaxReads = 0;
  size_t provenanceDefinitionSiteReads = 0;
  size_t provenanceImplementationSiteReads = 0;
  for (const auto& group : snapshot.dependencies<ModuleBodyProvenanceQuery>(moduleKey)) {
    ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
    ZC_REQUIRE(group.dependencies().size() == 1);
    const auto& fingerprint = group.dependencies()[0].key().fingerprint();
    if (fingerprint == ZC_ASSERT_NONNULL(selectedFingerprint)) {
      ++provenanceSelectedReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(parseFingerprint)) {
      ++provenanceParseReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(moduleBodySyntaxFingerprint)) {
      ++provenanceSyntaxReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(definitionSitesFingerprint)) {
      ++provenanceDefinitionSiteReads;
    } else if (fingerprint == ZC_ASSERT_NONNULL(implementationSitesFingerprint)) {
      ++provenanceImplementationSiteReads;
    } else {
      ZC_FAIL_REQUIRE("Module body provenance recorded an undeclared dependency");
    }
  }
  ZC_EXPECT(provenanceSelectedReads == 2);
  ZC_EXPECT(provenanceParseReads == 2);
  ZC_EXPECT(provenanceSyntaxReads == 2);
  ZC_EXPECT(provenanceDefinitionSiteReads == 2);
  ZC_EXPECT(provenanceImplementationSiteReads == 2);
}

ZC_TEST("Incremental binding query keeps module item let syntax in the module body") {
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto moduleKey = module("root"_zc);
  auto selected = selectedSource("root"_zc, "module-body-let.zom"_zc);
  auto sourceKey = sourceQueryKey("module-body-let.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("module-body-let.zom"_zc, zc::heapArray("let root = 0;"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SelectedModuleSourceInput>(moduleKey, selected));
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue));
  ZC_REQUIRE(write.set<CompilationOptionsInput>(CompilationUnitQueryKey::fixed(), options));
  ZC_REQUIRE(write.commit() != zc::none);

  auto snapshot = database.snapshot();
  auto definitions = snapshot.get<NamedDefinitionInventoryQuery>(moduleKey);
  auto definitionSites = snapshot.get<RevisionLocalDefinitionSitesQuery>(moduleKey);
  auto bodySyntax = snapshot.get<ModuleBodySyntaxQuery>(moduleKey);
  auto bodyProvenance = snapshot.get<ModuleBodyProvenanceQuery>(moduleKey);
  ZC_REQUIRE(definitions.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(definitionSites.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(bodySyntax.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(bodyProvenance.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(definitions.value().entries().size() == 1);
  ZC_EXPECT(definitionSites.value().entries().size() == 1);
  ZC_EXPECT(bodySyntax.value().rootCount() == 1);
  for (const auto& node : bodySyntax.value().nodes()) {
    ZC_EXPECT(node.kind() != binder::DetachedModuleBodyNodeKind::DefinitionBoundary);
  }
  ZC_EXPECT(bodyProvenance.value().entries().size() == bodySyntax.value().nodes().size());
}

ZC_TEST("Incremental binding query backdates range-only named inventory edits") {
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto moduleKey = module("root"_zc);
  auto selected = selectedSource("root"_zc, "backdate.zom"_zc);
  auto sourceKey = sourceQueryKey("backdate.zom"_zc);
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry);
  auto firstSource =
      sourceSnapshotValue("backdate.zom"_zc, zc::heapArray("module root;\nclass Alpha {}\n"_zcb));
  auto firstWrite = transaction(database);
  ZC_REQUIRE(firstWrite.set<SelectedModuleSourceInput>(moduleKey, selected));
  ZC_REQUIRE(firstWrite.set<SourceSnapshotInput>(sourceKey, firstSource));
  ZC_REQUIRE(firstWrite.set<CompilationOptionsInput>(CompilationUnitQueryKey::fixed(), options));
  ZC_REQUIRE(firstWrite.commit() != zc::none);
  auto first = database.snapshot();
  auto firstResult = first.get<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(firstResult.kind() == query::QueryValueKind::Value);
  auto firstSites = first.get<RevisionLocalDefinitionSitesQuery>(moduleKey);
  ZC_REQUIRE(firstSites.kind() == query::QueryValueKind::Value);
  auto firstBodySyntax = first.get<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(firstBodySyntax.kind() == query::QueryValueKind::Value);
  auto firstBodyProvenance = first.get<ModuleBodyProvenanceQuery>(moduleKey);
  ZC_REQUIRE(firstBodyProvenance.kind() == query::QueryValueKind::Value);
  auto firstMetadata = first.metadata<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(firstMetadata != zc::none);
  auto firstBodySyntaxMetadata = first.metadata<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(firstBodySyntaxMetadata != zc::none);

  auto shiftedSource = sourceSnapshotValue("backdate.zom"_zc,
                                           zc::heapArray("module root;\n\n\nclass Alpha {}\n"_zcb));
  auto shiftedWrite = transaction(database);
  ZC_REQUIRE(shiftedWrite.set<SourceSnapshotInput>(sourceKey, shiftedSource));
  ZC_REQUIRE(shiftedWrite.commit() != zc::none);
  auto shifted = database.snapshot();
  auto shiftedResult = shifted.get<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(shiftedResult.kind() == query::QueryValueKind::Value);
  auto shiftedSites = shifted.get<RevisionLocalDefinitionSitesQuery>(moduleKey);
  ZC_REQUIRE(shiftedSites.kind() == query::QueryValueKind::Value);
  auto shiftedBodySyntax = shifted.get<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(shiftedBodySyntax.kind() == query::QueryValueKind::Value);
  auto shiftedBodyProvenance = shifted.get<ModuleBodyProvenanceQuery>(moduleKey);
  ZC_REQUIRE(shiftedBodyProvenance.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(firstResult.value().sameAs(shiftedResult.value()));
  ZC_EXPECT(!firstSites.value().sameAs(shiftedSites.value()));
  ZC_EXPECT(firstBodySyntax.value() == shiftedBodySyntax.value());
  ZC_EXPECT(firstBodyProvenance.value() != shiftedBodyProvenance.value());
  auto shiftedMetadata = shifted.metadata<NamedDefinitionInventoryQuery>(moduleKey);
  ZC_REQUIRE(shiftedMetadata != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(shiftedMetadata).changedAt() ==
            ZC_ASSERT_NONNULL(firstMetadata).changedAt());
  auto shiftedSiteMetadata = shifted.metadata<RevisionLocalDefinitionSitesQuery>(moduleKey);
  ZC_REQUIRE(shiftedSiteMetadata != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(shiftedSiteMetadata).changedAt() == shifted.revision());
  auto shiftedBodySyntaxMetadata = shifted.metadata<ModuleBodySyntaxQuery>(moduleKey);
  ZC_REQUIRE(shiftedBodySyntaxMetadata != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(shiftedBodySyntaxMetadata).changedAt() ==
            ZC_ASSERT_NONNULL(firstBodySyntaxMetadata).changedAt());
  auto shiftedBodyProvenanceMetadata = shifted.metadata<ModuleBodyProvenanceQuery>(moduleKey);
  ZC_REQUIRE(shiftedBodyProvenanceMetadata != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(shiftedBodyProvenanceMetadata).changedAt() == shifted.revision());
}

ZC_TEST("Incremental binding query compilation options codec is exact bounded and closed") {
  auto registry = targetRegistry();
  auto request = compilationRequest(registry, package::SelectedLanguageOptions{false, true, false});
  auto value = CanonicalCompilationOptions::fromVerified(request);
  ZC_REQUIRE(value != zc::none);
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
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  const auto root = CompilationUnitQueryKey::fixed();
  ZC_IF_SOME(value, firstValue) {
    auto initialWrite = transaction(database);
    ZC_REQUIRE(initialWrite.set<CompilationOptionsInput>(root, value));
    ZC_REQUIRE(initialWrite.commit() != zc::none);
    auto initial = database.snapshot();
    auto initialMetadata = initial.metadata<CompilationOptionsInput>(root);
    ZC_REQUIRE(initialMetadata != zc::none);

    auto equalWrite = transaction(database);
    ZC_REQUIRE(equalWrite.set<CompilationOptionsInput>(root, value));
    ZC_REQUIRE(equalWrite.commit() != zc::none);
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
      ZC_REQUIRE(changedWrite.set<CompilationOptionsInput>(root, changedOptions));
      ZC_REQUIRE(changedWrite.commit() != zc::none);
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

ZC_TEST("Incremental binding query active crates use a canonical package root set") {
  auto contract = ActiveCratesInput::contract();
  ZC_EXPECT(contract.domain() == "zom.query.active-crates"_zc);
  ZC_EXPECT(contract.isInput());
  ZC_EXPECT(contract.inputDurability() == query::Durability::Medium);

  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_EXPECT(database.registerInputKind<ActiveCratesInput>() == zc::none);

  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto roots = packageRootSet(request);
  auto encodedRoots = ActiveCratesInput::encodeKey(roots);
  auto decodedRoots = ActiveCratesInput::decodeKey(encodedRoots.asPtr());
  ZC_REQUIRE(decodedRoots != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedRoots) == roots);

  auto first = stableCrate("incremental_binding_query"_zc);
  auto second = stableCrate("incremental_binding_query_aux"_zc);
  auto crates = crateSet(second, first, first);
  ZC_REQUIRE(crates.crates().size() == 2);
  auto encodedCrates = ActiveCratesInput::encodeValue(crates);
  auto decodedCrates = ActiveCratesInput::decodeValue(encodedCrates.asPtr());
  ZC_REQUIRE(decodedCrates != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedCrates) == crates);

  const uint8_t malformed[] = {0xff};
  ZC_EXPECT(ActiveCratesInput::decodeKey(zc::arrayPtr(malformed)) == zc::none);
  ZC_EXPECT(ActiveCratesInput::decodeValue(zc::arrayPtr(malformed)) == zc::none);

  auto trailingRoot = zc::heapArray<uint8_t>(encodedRoots.size() + 1);
  for (size_t index = 0; index < encodedRoots.size(); ++index) {
    trailingRoot[index] = encodedRoots[index];
  }
  trailingRoot.back() = 0;
  ZC_EXPECT(ActiveCratesInput::decodeKey(trailingRoot.asPtr()) == zc::none);

  identity::CanonicalEncoder duplicateRoot;
  duplicateRoot.encodeSequenceSize(2);
  duplicateRoot.encodeByteString(roots.packages()[0].canonicalPackageBytes());
  duplicateRoot.encodeByteString(roots.packages()[0].canonicalPackageBytes());
  ZC_EXPECT(ActiveCratesInput::decodeKey(duplicateRoot.finish().asPtr()) == zc::none);

  identity::CanonicalEncoder duplicateCrate;
  duplicateCrate.encodeSequenceSize(2);
  duplicateCrate.encodeByteString(first.canonicalCrateBytes());
  duplicateCrate.encodeByteString(first.canonicalCrateBytes());
  ZC_EXPECT(ActiveCratesInput::decodeValue(duplicateCrate.finish().asPtr()) == zc::none);
}

ZC_TEST("Incremental binding query active crates backdate equals and replace changes") {
  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto roots = packageRootSet(request);
  auto first = stableCrate("incremental_binding_query"_zc);
  auto second = stableCrate("incremental_binding_query_aux"_zc);
  auto firstSet = crateSet(first);

  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto initialWrite = transaction(database);
  ZC_REQUIRE(initialWrite.set<ActiveCratesInput>(roots, firstSet));
  ZC_REQUIRE(initialWrite.commit() != zc::none);
  auto initial = database.snapshot();
  auto initialMetadata = initial.metadata<ActiveCratesInput>(roots);
  ZC_REQUIRE(initialMetadata != zc::none);

  auto equalWrite = transaction(database);
  ZC_REQUIRE(equalWrite.set<ActiveCratesInput>(roots, firstSet));
  ZC_REQUIRE(equalWrite.commit() != zc::none);
  auto equal = database.snapshot();
  auto equalMetadata = equal.metadata<ActiveCratesInput>(roots);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).changedAt());

  auto replacement = crateSet(first, second);
  auto changedWrite = transaction(database);
  ZC_REQUIRE(changedWrite.set<ActiveCratesInput>(roots, replacement));
  ZC_REQUIRE(changedWrite.commit() != zc::none);
  auto changed = database.snapshot();
  auto result = changed.get<ActiveCratesInput>(roots);
  ZC_REQUIRE(result.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(result.value() == replacement);
  auto changedMetadata = changed.metadata<ActiveCratesInput>(roots);
  ZC_REQUIRE(changedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(changedMetadata).changedAt() == changed.revision());
}

ZC_TEST("Incremental package graph input admits only a closed canonical graph") {
  auto contract = PackageGraphInput::contract();
  ZC_EXPECT(contract.domain() == "zom.query.package-graph"_zc);
  ZC_EXPECT(contract.isInput());
  ZC_EXPECT(contract.inputDurability() == query::Durability::Medium);

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
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_EXPECT(database.registerInputKind<PackageGraphInput>() == zc::none);
  auto registry = targetRegistry();
  auto request = compilationRequest(registry);
  auto roots = packageRootSet(request);
  auto first = singlePackageGraph("incremental_binding_query"_zc, "incremental_binding_query"_zc);

  auto firstWrite = transaction(database);
  ZC_REQUIRE(firstWrite.set<PackageGraphInput>(roots, first));
  ZC_REQUIRE(firstWrite.commit() != zc::none);
  auto firstSnapshot = database.snapshot();
  auto firstMetadata = firstSnapshot.metadata<PackageGraphInput>(roots);
  ZC_REQUIRE(firstMetadata != zc::none);

  auto equalWrite = transaction(database);
  ZC_REQUIRE(equalWrite.set<PackageGraphInput>(roots, first));
  ZC_REQUIRE(equalWrite.commit() != zc::none);
  auto equalSnapshot = database.snapshot();
  auto equalMetadata = equalSnapshot.metadata<PackageGraphInput>(roots);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstMetadata).changedAt());

  auto changed =
      singlePackageGraph("incremental_binding_query"_zc, "incremental_binding_query_aux"_zc);
  auto changedWrite = transaction(database);
  ZC_REQUIRE(changedWrite.set<PackageGraphInput>(roots, changed));
  ZC_REQUIRE(changedWrite.commit() != zc::none);
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

ZC_TEST("Incremental binding query per crate source and module roots are strict and replaceable") {
  auto sourceContract = ActiveSourcesInput::contract();
  ZC_EXPECT(sourceContract.domain() == "zom.query.active-sources"_zc);
  ZC_EXPECT(sourceContract.isInput());
  ZC_EXPECT(sourceContract.inputDurability() == query::Durability::Low);
  auto moduleContract = ActiveModulesInput::contract();
  ZC_EXPECT(moduleContract.domain() == "zom.query.active-modules"_zc);
  ZC_EXPECT(moduleContract.isInput());
  ZC_EXPECT(moduleContract.inputDurability() == query::Durability::Low);

  auto crate = stableCrate("incremental_binding_query"_zc);
  auto encodedCrate = ActiveSourcesInput::encodeKey(crate);
  auto decodedCrate = ActiveSourcesInput::decodeKey(encodedCrate.asPtr());
  ZC_REQUIRE(decodedCrate != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedCrate) == crate);
  ZC_EXPECT(ActiveModulesInput::decodeKey(encodedCrate.asPtr()) != zc::none);
  identity::CanonicalEncoder emptyModules;
  emptyModules.encodeSequenceSize(0);
  ZC_EXPECT(ActiveModulesInput::decodeValue(emptyModules.finish().asPtr()) == zc::none);

  auto firstSource = sourceQueryKey("first.zom"_zc);
  auto secondSource = sourceQueryKey("second.zom"_zc);
  auto sources = sourceSet(secondSource, firstSource, firstSource);
  ZC_REQUIRE(sources.sources().size() == 2);
  auto encodedSources = ActiveSourcesInput::encodeValue(sources);
  auto decodedSources = ActiveSourcesInput::decodeValue(encodedSources.asPtr());
  ZC_REQUIRE(decodedSources != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedSources) == sources);

  identity::CanonicalEncoder duplicateSources;
  duplicateSources.encodeSequenceSize(2);
  duplicateSources.encodeByteString(firstSource.canonicalSourceBytes());
  duplicateSources.encodeByteString(firstSource.canonicalSourceBytes());
  ZC_EXPECT(ActiveSourcesInput::decodeValue(duplicateSources.finish().asPtr()) == zc::none);
  const uint8_t malformed[] = {0xff};
  ZC_EXPECT(ActiveSourcesInput::decodeKey(zc::arrayPtr(malformed)) == zc::none);
  ZC_EXPECT(ActiveSourcesInput::decodeValue(zc::arrayPtr(malformed)) == zc::none);

  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  ZC_EXPECT(database.registerInputKind<ActiveSourcesInput>() == zc::none);
  auto firstWrite = transaction(database);
  ZC_REQUIRE(firstWrite.set<ActiveSourcesInput>(crate, sources));
  ZC_REQUIRE(firstWrite.commit() != zc::none);
  auto first = database.snapshot();
  auto firstMetadata = first.metadata<ActiveSourcesInput>(crate);
  ZC_REQUIRE(firstMetadata != zc::none);

  auto equalWrite = transaction(database);
  ZC_REQUIRE(equalWrite.set<ActiveSourcesInput>(crate, sources));
  ZC_REQUIRE(equalWrite.commit() != zc::none);
  auto equal = database.snapshot();
  auto equalMetadata = equal.metadata<ActiveSourcesInput>(crate);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstMetadata).changedAt());

  auto replacement = sourceSet(secondSource);
  auto changedWrite = transaction(database);
  ZC_REQUIRE(changedWrite.set<ActiveSourcesInput>(crate, replacement));
  ZC_REQUIRE(changedWrite.commit() != zc::none);
  auto changed = database.snapshot();
  auto changedResult = changed.get<ActiveSourcesInput>(crate);
  ZC_REQUIRE(changedResult.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(changedResult.value() == replacement);
  auto changedMetadata = changed.metadata<ActiveSourcesInput>(crate);
  ZC_REQUIRE(changedMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(changedMetadata).changedAt() == changed.revision());
}

ZC_TEST("Incremental binding query closes selected sources over the exact snapshot root") {
  zc::Vector<SelectedModuleSource> selected;
  selected.add(selectedSource("a"_zc, "a.zom"_zc));
  zc::Vector<StableSourceQueryKey> exactSnapshots;
  exactSnapshots.add(sourceQueryKey("a.zom"_zc));
  ZC_EXPECT(verifySelectedSourceSnapshotClosure(selected.asPtr(), exactSnapshots.asPtr()));

  zc::Vector<StableSourceQueryKey> missingSnapshots;
  ZC_EXPECT(!verifySelectedSourceSnapshotClosure(selected.asPtr(), missingSnapshots.asPtr()));

  zc::Vector<StableSourceQueryKey> replacedSnapshots;
  replacedSnapshots.add(sourceQueryKey("replacement.zom"_zc));
  ZC_EXPECT(!verifySelectedSourceSnapshotClosure(selected.asPtr(), replacedSnapshots.asPtr()));

  zc::Vector<StableSourceQueryKey> duplicateSnapshots;
  duplicateSnapshots.add(sourceQueryKey("a.zom"_zc));
  duplicateSnapshots.add(sourceQueryKey("a.zom"_zc));
  ZC_EXPECT(!verifySelectedSourceSnapshotClosure(selected.asPtr(), duplicateSnapshots.asPtr()));
}

ZC_TEST(
    "Incremental binding query source snapshots preserve equals replace changes and erase stale") {
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto firstKey = sourceQueryKey("first.zom"_zc);
  auto staleKey = sourceQueryKey("stale.zom"_zc);
  auto firstValue = sourceSnapshotValue("first.zom"_zc, zc::heapArray("first"_zcb));
  auto staleValue = sourceSnapshotValue("stale.zom"_zc, zc::heapArray("stale"_zcb));

  auto initialWrite = transaction(database);
  ZC_REQUIRE(initialWrite.set<SourceSnapshotInput>(firstKey, firstValue));
  ZC_REQUIRE(initialWrite.set<SourceSnapshotInput>(staleKey, staleValue));
  ZC_REQUIRE(initialWrite.commit() != zc::none);
  auto initial = database.snapshot();
  auto initialMetadata = initial.metadata<SourceSnapshotInput>(firstKey);
  ZC_REQUIRE(initialMetadata != zc::none);

  auto equalReplacement = transaction(database);
  ZC_REQUIRE(equalReplacement.erase<SourceSnapshotInput>(firstKey));
  ZC_REQUIRE(equalReplacement.erase<SourceSnapshotInput>(staleKey));
  ZC_REQUIRE(equalReplacement.set<SourceSnapshotInput>(firstKey, firstValue));
  ZC_REQUIRE(equalReplacement.set<SourceSnapshotInput>(staleKey, staleValue));
  ZC_REQUIRE(equalReplacement.commit() != zc::none);
  auto equal = database.snapshot();
  auto equalMetadata = equal.metadata<SourceSnapshotInput>(firstKey);
  ZC_REQUIRE(equalMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(equalMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(initialMetadata).changedAt());

  auto changedValue = sourceSnapshotValue("first.zom"_zc, zc::heapArray("changed"_zcb));
  auto changedRoot = transaction(database);
  ZC_REQUIRE(changedRoot.erase<SourceSnapshotInput>(firstKey));
  ZC_REQUIRE(changedRoot.erase<SourceSnapshotInput>(staleKey));
  ZC_REQUIRE(changedRoot.set<SourceSnapshotInput>(firstKey, changedValue));
  ZC_REQUIRE(changedRoot.commit() != zc::none);
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

ZC_TEST("Incremental binding query produces canonical chain and diamond orders") {
  Modules modules;
  const auto root = topologyRoot();

  query::QueryDatabase chainDatabase(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(chainDatabase));
  auto chainWrite = transaction(chainDatabase);
  auto chainActive = setOf(modules.c, modules.a, modules.b);
  auto noDependencies = dependencies();
  auto dependsOnA = dependencies(modules.a);
  auto dependsOnB = dependencies(modules.b);
  stageActiveModules(chainWrite, root, chainActive);
  ZC_REQUIRE(chainWrite.set<ModuleDependenciesInput>(modules.a, noDependencies));
  ZC_REQUIRE(chainWrite.set<ModuleDependenciesInput>(modules.b, dependsOnA));
  ZC_REQUIRE(chainWrite.set<ModuleDependenciesInput>(modules.c, dependsOnB));
  ZC_REQUIRE(chainWrite.commit() != zc::none);
  auto chain = chainDatabase.snapshot().get<ModuleBindingOrderQuery>(root);
  ZC_REQUIRE(!chain.isRuntimeFailure());
  ZC_REQUIRE(chain.kind() == query::QueryValueKind::Value);
  StableModuleQueryKey chainExpected[] = {modules.a.clone(), modules.b.clone(), modules.c.clone()};
  expectOrder(chain.value(), zc::arrayPtr(chainExpected));

  query::QueryDatabase diamondDatabase(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(diamondDatabase));
  auto diamondWrite = transaction(diamondDatabase);
  auto diamondActive = setOf(modules.d, modules.c, modules.b, modules.a);
  auto diamondA = dependencies();
  auto diamondB = dependencies(modules.a);
  auto diamondC = dependencies(modules.a);
  auto diamondD = dependencies(modules.c, modules.b);
  stageActiveModules(diamondWrite, root, diamondActive);
  ZC_REQUIRE(diamondWrite.set<ModuleDependenciesInput>(modules.a, diamondA));
  ZC_REQUIRE(diamondWrite.set<ModuleDependenciesInput>(modules.b, diamondB));
  ZC_REQUIRE(diamondWrite.set<ModuleDependenciesInput>(modules.c, diamondC));
  ZC_REQUIRE(diamondWrite.set<ModuleDependenciesInput>(modules.d, diamondD));
  ZC_REQUIRE(diamondWrite.commit() != zc::none);
  auto diamondSnapshot = diamondDatabase.snapshot();
  auto diamond = diamondSnapshot.get<ModuleBindingOrderQuery>(root);
  ZC_REQUIRE(!diamond.isRuntimeFailure());
  ZC_REQUIRE(diamond.kind() == query::QueryValueKind::Value);
  StableModuleQueryKey diamondExpected[] = {modules.a.clone(), modules.b.clone(), modules.c.clone(),
                                            modules.d.clone()};
  expectOrder(diamond.value(), zc::arrayPtr(diamondExpected));

  auto groups = diamondSnapshot.dependencies<ModuleBindingOrderQuery>(root);
  size_t crateMembershipGroups = 0;
  size_t moduleDependencyGroups = 0;
  for (const auto& group : groups) {
    if (group.kind() == query::DependencyGroup::Kind::Parallel) {
      if (group.dependencies().size() == 1) {
        ++crateMembershipGroups;
      } else if (group.dependencies().size() == 4) {
        ++moduleDependencyGroups;
      }
    }
  }
  ZC_EXPECT(crateMembershipGroups == 2);
  ZC_EXPECT(moduleDependencyGroups == 2);
}

ZC_TEST("Incremental binding query publishes deterministic invalid-topology failures") {
  Modules modules;
  const auto root = topologyRoot();

  query::QueryDatabase cycleDatabase(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(cycleDatabase));
  auto cycleWrite = transaction(cycleDatabase);
  auto cycleActive = setOf(modules.b, modules.a);
  auto aToB = dependencies(modules.b);
  auto bToA = dependencies(modules.a);
  stageActiveModules(cycleWrite, root, cycleActive);
  ZC_REQUIRE(cycleWrite.set<ModuleDependenciesInput>(modules.a, aToB));
  ZC_REQUIRE(cycleWrite.set<ModuleDependenciesInput>(modules.b, bToA));
  ZC_REQUIRE(cycleWrite.commit() != zc::none);
  auto cycle = requireFailure(cycleDatabase.snapshot().get<ModuleBindingOrderQuery>(root));
  ZC_EXPECT(cycle.kind() == ModuleBindingOrderFailureKind::Cycle);
  ZC_EXPECT(cycle.requester() == modules.a);

  query::QueryDatabase outsideDatabase(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(outsideDatabase));
  auto outsideWrite = transaction(outsideDatabase);
  auto outsideActive = setOf(modules.a);
  auto outsideDependency = dependencies(modules.d);
  stageActiveModules(outsideWrite, root, outsideActive);
  ZC_REQUIRE(outsideWrite.set<ModuleDependenciesInput>(modules.a, outsideDependency));
  ZC_REQUIRE(outsideWrite.commit() != zc::none);
  auto outside = requireFailure(outsideDatabase.snapshot().get<ModuleBindingOrderQuery>(root));
  ZC_EXPECT(outside.kind() == ModuleBindingOrderFailureKind::DependencyOutsideActiveSet);
  ZC_EXPECT(outside.requester() == modules.a);
  ZC_REQUIRE(outside.dependency() != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(outside.dependency()) == modules.d);

  query::QueryDatabase selfDatabase(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(selfDatabase));
  auto selfWrite = transaction(selfDatabase);
  auto selfActive = setOf(modules.a);
  auto selfDependency = dependencies(modules.a);
  stageActiveModules(selfWrite, root, selfActive);
  ZC_REQUIRE(selfWrite.set<ModuleDependenciesInput>(modules.a, selfDependency));
  ZC_REQUIRE(selfWrite.commit() != zc::none);
  auto self = requireFailure(selfDatabase.snapshot().get<ModuleBindingOrderQuery>(root));
  ZC_EXPECT(self.kind() == ModuleBindingOrderFailureKind::SelfDependency);

  query::QueryDatabase missingDatabase(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(missingDatabase));
  auto missingWrite = transaction(missingDatabase);
  auto missingActive = setOf(modules.a);
  auto missingDependency = ModuleDependencySet::missing();
  stageActiveModules(missingWrite, root, missingActive);
  ZC_REQUIRE(missingWrite.set<ModuleDependenciesInput>(modules.a, missingDependency));
  ZC_REQUIRE(missingWrite.commit() != zc::none);
  auto missing = requireFailure(missingDatabase.snapshot().get<ModuleBindingOrderQuery>(root));
  ZC_EXPECT(missing.kind() == ModuleBindingOrderFailureKind::MissingDependencies);

  query::QueryDatabase uncommittedDatabase(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(uncommittedDatabase));
  auto uncommittedWrite = transaction(uncommittedDatabase);
  auto uncommittedActive = setOf(modules.a);
  stageActiveModules(uncommittedWrite, root, uncommittedActive);
  ZC_REQUIRE(uncommittedWrite.commit() != zc::none);
  auto uncommitted = uncommittedDatabase.snapshot().get<ModuleBindingOrderQuery>(root);
  ZC_REQUIRE(uncommitted.isRuntimeFailure());
  ZC_EXPECT(uncommitted.runtimeFailure() == query::QueryRuntimeFailure::MissingInput);
}

ZC_TEST("Incremental binding query green-reuses equal recommitted topology inputs") {
  Modules modules;
  const auto root = topologyRoot();
  query::QueryDatabase database(queryTestScheduler());
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));

  auto firstWrite = transaction(database);
  auto active = setOf(modules.b, modules.a);
  auto aDependencies = dependencies();
  auto bDependencies = dependencies(modules.a);
  stageActiveModules(firstWrite, root, active);
  ZC_REQUIRE(firstWrite.set<ModuleDependenciesInput>(modules.a, aDependencies));
  ZC_REQUIRE(firstWrite.set<ModuleDependenciesInput>(modules.b, bDependencies));
  ZC_REQUIRE(firstWrite.commit() != zc::none);
  auto first = database.snapshot();
  auto firstResult = first.get<ModuleBindingOrderQuery>(root);
  ZC_REQUIRE(!firstResult.isRuntimeFailure());
  auto firstMetadata = first.metadata<ModuleBindingOrderQuery>(root);
  ZC_REQUIRE(firstMetadata != zc::none);

  auto equalWrite = transaction(database);
  stageActiveModules(equalWrite, root, active);
  ZC_REQUIRE(equalWrite.set<ModuleDependenciesInput>(modules.a, aDependencies));
  ZC_REQUIRE(equalWrite.set<ModuleDependenciesInput>(modules.b, bDependencies));
  auto revision = equalWrite.commit();
  ZC_REQUIRE(revision != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(revision) == query::DatabaseRevision(2));
  auto second = database.snapshot();
  auto secondResult = second.get<ModuleBindingOrderQuery>(root);
  ZC_REQUIRE(!secondResult.isRuntimeFailure());
  auto secondMetadata = second.metadata<ModuleBindingOrderQuery>(root);
  ZC_REQUIRE(secondMetadata != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(secondMetadata).changedAt() ==
            ZC_REQUIRE_NONNULL(firstMetadata).changedAt());
  ZC_EXPECT(hasEvent(second.events().asPtr(), query::QueryEventKind::GreenReused));
}

}  // namespace zomlang::compiler::driver::incremental_binding_query
