// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/driver/core-library-query-provider.h"

#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/basic/thread-pool.h"
#include "zomlang/compiler/driver/incremental-binding-query-adapter.h"
#include "zomlang/compiler/driver/incremental-package-graph-query-input.h"
#include "zomlang/compiler/driver/module-graph-query-input.h"
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/ir/target-registry.h"
#include "zomlang/compiler/source/core-source-admission.h"
#include "zomlang/tests/unittests/compiler/driver/canonical-mutation-test-helpers.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::core_library_query {
namespace {

namespace mutation = tests::canonical_mutation;

struct CorePayloadWire final {
  mutation::WireRange context;
  mutation::WireRange distribution;
  mutation::WireRange digest;
  mutation::WireRange policy;
  mutation::SequenceRange sources;
  mutation::SequenceRange options;
  mutation::SequenceRange searchRoots;
  mutation::SequenceRange inventory;
  mutation::WireRange authority;
};

CorePayloadWire corePayloadWire(zc::ArrayPtr<const uint8_t> bytes) {
  constexpr zc::StringPtr domain = "zom.query.input-transaction.core-distribution"_zc;
  size_t cursor = mutation::payloadOffset(bytes, domain);
  const auto context = mutation::consumeByteString(bytes, cursor);
  const auto distribution = mutation::consumeByteString(bytes, cursor);
  const auto digest = mutation::WireRange{cursor, cursor + 32};
  ZC_REQUIRE(digest.end <= bytes.size());
  cursor = digest.end;
  const auto policy = mutation::consumeByteString(bytes, cursor);
  const auto sources = mutation::consumeSequence(bytes, cursor, 2);
  const auto options = mutation::consumeSequence(bytes, cursor, 2);
  const auto searchRoots = mutation::consumeSequence(bytes, cursor, 2);
  const auto inventory = mutation::consumeSequence(bytes, cursor);
  const auto authority = mutation::consumeByteString(bytes, cursor);
  ZC_REQUIRE(cursor == bytes.size());
  return CorePayloadWire{context, distribution, digest,    policy,   sources,
                         options, searchRoots,  inventory, authority};
}

basic::ThreadPool& scheduler() {
  static basic::ThreadPool value(2);
  return value;
}

class QueryTestSemanticContextResources final : public query::SemanticContextCapabilityResources {};

query::QueryDatabase queryDatabase() {
  auto resources = zc::heap<QueryTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  return query::QueryDatabase(scheduler(), query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
}

query::InputTransaction transaction(query::QueryDatabase& database) {
  auto result = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(result.isOpened());
  return zc::mv(result).takeTransaction();
}

source::core::VerifiedCoreDistribution admittedDistribution() {
  return core_library_test::admittedCoreDistribution();
}

zc::Array<uint8_t> targetSelection() {
  identity::CanonicalEncoder encoder;
  encoder.encodeDigest(tests::test_identity_detail::digest(0x71));
  encoder.encodeByteString("host"_zc.asBytes());
  tests::test_identity_detail::target().encode(encoder);
  encoder.encodeUint8(static_cast<uint8_t>(package::PackagePanicStrategy::Abort));
  return encoder.finish();
}

identity::source_query::CanonicalCompilationOptions compilationOptions(bool useUnicode = true) {
  auto options = identity::source_query::CanonicalCompilationOptions::fromCanonicalSelections(
      targetSelection(), targetSelection(), useUnicode, false, true);
  return zc::mv(ZC_REQUIRE_NONNULL(options));
}

package::RegisteredTargetProfileName profileName() {
  auto value = package::RegisteredTargetProfileName::from("host"_zc);
  return zc::mv(ZC_REQUIRE_NONNULL(value));
}

ir::TargetRegistrySnapshot targetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = ir::CanonicalTargetSpec::from(
      "x-v-o-e"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(backendFeatures), "a"_zc,
      ir::BackendPanicStrategy::Abort, ir::ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(ZC_REQUIRE_NONNULL(specification)));
  auto profile =
      ir::RegisteredTargetProfileRecord::from(profileName(), tests::test_identity_detail::target(),
                                              zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  profiles.add(zc::mv(ZC_REQUIRE_NONNULL(profile)));
  auto registry = ir::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

package::RegisteredTargetSelection selectedTarget(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selected =
      ZC_REQUIRE_NONNULL(service).select(zc::none, package::PackagePanicStrategy::Abort);
  return zc::mv(ZC_REQUIRE_NONNULL(selected));
}

package::VerifiedPackageCompilationRequest packageRequest() {
  auto registry = targetRegistry();
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::CanonicalPathSegment>("src"_zc));
  path.add(tests::test_identity_detail::scalar<identity::CanonicalPathSegment>("test.zom"_zc));
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      tests::test_identity_detail::package(), identity::CrateTargetKind::Library,
      tests::test_identity_detail::scalar<identity::TargetName>("test"_zc), 2026, false,
      identity::CanonicalRelativePath::from(zc::mv(path))));
  auto request = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), selectedTarget(registry), selectedTarget(registry),
      package::SelectedLanguageOptions{true, false, true}, package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

identity::source_query::CanonicalCompilationOptions verifiedCompilationOptions() {
  auto request = packageRequest();
  auto options = identity::source_query::CanonicalCompilationOptions::fromVerified(request);
  return zc::mv(ZC_REQUIRE_NONNULL(options));
}

module_graph_query::CompleteCompilationContextAuthority contextAuthority(
    const package::VerifiedPackageCompilationRequest& request,
    const identity::source_query::CanonicalCompilationOptions& options,
    const source::core::CoreDistributionInputRecord& distribution) {
  auto user = tests::test_identity_detail::crate();
  auto core = identity::projectToolchainCoreCrate(user);
  auto roots = incremental_binding_query::PackageRootSetKey::fromVerified(request);
  ZC_REQUIRE(core != zc::none);
  ZC_REQUIRE(roots != zc::none);

  identity::CanonicalEncoder graphEncoder;
  graphEncoder.encodeSequenceSize(1);
  graphEncoder.encodeByteString(tests::test_identity_detail::package().encode().asPtr());
  graphEncoder.encodeSequenceSize(0);
  graphEncoder.encodeSequenceSize(0);
  graphEncoder.encodeSequenceSize(1);
  graphEncoder.encodeByteString(user.encode().asPtr());
  graphEncoder.encodeSequenceSize(0);
  auto graphBytes = graphEncoder.finish();
  auto graph = incremental_binding_query::PackageGraphInput::decodeValue(graphBytes.asPtr());
  ZC_REQUIRE(graph != zc::none);

  zc::Vector<identity::CrateKey> userRoots;
  userRoots.add(user.clone());
  zc::Vector<identity::CrateKey> coreRoots;
  coreRoots.add(ZC_REQUIRE_NONNULL(core).clone());
  zc::Vector<module_graph_query::CompilationOptionsEntry> optionEntries;
  optionEntries.add(
      module_graph_query::CompilationOptionsEntry::from(user.clone(), options.clone()));
  optionEntries.add(module_graph_query::CompilationOptionsEntry::from(
      ZC_REQUIRE_NONNULL(core).clone(), options.clone()));

  zc::Vector<binder::ModuleSearchRoot> userEnvironment;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  userEnvironment.add(binder::ModuleSearchRoot::workspace(
      user.clone(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  auto userSearch = incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
      user, userEnvironment.asPtr());
  auto coreRoot = binder::ModuleSearchRoot::toolchainCore(ZC_REQUIRE_NONNULL(core).clone(),
                                                          distribution.digest());
  ZC_REQUIRE(userSearch != zc::none);
  ZC_REQUIRE(coreRoot != zc::none);
  zc::Vector<binder::ModuleSearchRoot> coreEnvironment;
  coreEnvironment.add(zc::mv(ZC_REQUIRE_NONNULL(coreRoot)));
  auto coreSearch = incremental_module_resolution_query::CanonicalModuleSearchRoots::fromVerified(
      ZC_REQUIRE_NONNULL(core), coreEnvironment.asPtr());
  ZC_REQUIRE(coreSearch != zc::none);
  zc::Vector<module_graph_query::ModuleSearchRootsEntry> searchEntries;
  searchEntries.add(module_graph_query::ModuleSearchRootsEntry::from(
      user.clone(), zc::mv(ZC_REQUIRE_NONNULL(userSearch))));
  searchEntries.add(module_graph_query::ModuleSearchRootsEntry::from(
      ZC_REQUIRE_NONNULL(core).clone(), zc::mv(ZC_REQUIRE_NONNULL(coreSearch))));

  const module_graph_query::CompleteCompilationContextSources sources{
      request,           ZC_REQUIRE_NONNULL(roots), ZC_REQUIRE_NONNULL(graph), userRoots.asPtr(),
      coreRoots.asPtr(), optionEntries.asPtr(),     searchEntries.asPtr(),     distribution,
  };
  auto authority = module_graph_query::CompleteCompilationContextAuthority::fromVerified(sources);
  return zc::mv(ZC_REQUIRE_NONNULL(authority));
}

zc::Maybe<VerifiedCoreDistributionInputTransaction> prepareCoreTransaction(
    query::DatabaseRevision expectedPreviousRevision,
    const source::core::VerifiedCoreDistribution& distribution,
    const identity::source_query::CanonicalCompilationOptions& options,
    zc::ArrayPtr<const identity::CrateKey> consumers) {
  auto request = packageRequest();
  auto accepted = source::core::initialCoreDistributionInput();
  if (accepted == zc::none) { return zc::none; }
  auto authorityOptions =
      identity::source_query::CanonicalCompilationOptions::fromVerified(request);
  if (authorityOptions == zc::none) { return zc::none; }
  auto authority =
      contextAuthority(request, ZC_ASSERT_NONNULL(authorityOptions), ZC_ASSERT_NONNULL(accepted));
  return VerifiedCoreDistributionInputTransaction::prepare(
      expectedPreviousRevision, distribution, request, zc::mv(authority), options, consumers);
}

identity::ModuleKey coreModule(const identity::CrateKey& crate, zc::StringPtr name) {
  zc::Vector<identity::ModulePathSegment> path;
  path.add(tests::test_identity_detail::scalar<identity::ModulePathSegment>(name));
  auto module = identity::ModuleKey::from(crate.clone(), zc::mv(path));
  return zc::mv(ZC_REQUIRE_NONNULL(module));
}

identity::CrateKey alternateCoreCrate() {
  zc::Maybe<identity::BuildScriptProducerKey> noBuildScript;
  auto compilation = identity::CompilationConfigKey::from(
      identity::CompilationDomain::Target, tests::test_identity_detail::target(),
      identity::SemanticCompilerOptionsKey::from(2026, false, false, true), zc::mv(noBuildScript));
  ZC_REQUIRE(compilation != zc::none);
  auto crate = identity::CrateKey::from(
      identity::CompilationUnitIdentity::toolchain(identity::ToolchainUnitKey::core()),
      identity::CrateTargetKind::Library,
      tests::test_identity_detail::scalar<identity::TargetName>("core"_zc),
      zc::mv(ZC_REQUIRE_NONNULL(compilation)));
  return zc::mv(ZC_REQUIRE_NONNULL(crate));
}

void stageCoreSourceInputs(query::InputTransaction& write, const identity::CrateKey& crate,
                           const source::core::CoreDistributionInputRecord& distribution) {
  const zc::StringPtr sourceBytes[] = {
      "module core;\n"_zc,
      "module marker;\n\nexport interface Copy {}\nexport interface Linear {}\n"_zc,
      "module prelude;\n\nexport core::marker::{Copy, Linear};\n"_zc,
  };
  ZC_REQUIRE(distribution.record().files().size() == 3);
  for (size_t index = 0; index < distribution.record().files().size(); ++index) {
    const auto& file = distribution.record().files()[index];
    auto sourceKey = identity::SourceFileKey::from(
        crate.clone(), identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                                           file.path().clone()));
    auto immutable = identity::ImmutableSourceSnapshot::from(
        sourceKey.clone(), zc::heapArray<uint8_t>(sourceBytes[index].asBytes()));
    ZC_REQUIRE(immutable != zc::none);
    auto snapshot = identity::source_query::CanonicalSourceSnapshot::fromVerified(
        ZC_REQUIRE_NONNULL(immutable));
    auto stable = identity::source_query::StableSourceQueryKey::fromVerified(sourceKey);
    ZC_REQUIRE(snapshot != zc::none);
    ZC_REQUIRE(stable != zc::none);
    ZC_REQUIRE(write
                   .set<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(stable),
                                                                     ZC_REQUIRE_NONNULL(snapshot))
                   .isApplied());
  }
}

}  // namespace

ZC_TEST("Core query contextual keys require exact core membership") {
  auto core = tests::test_identity_detail::coreCrate();
  auto roots = incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(core);
  ZC_REQUIRE(roots != zc::none);

  auto crateKey = ContextualCoreCrateKey::from(ZC_REQUIRE_NONNULL(roots).clone(), core.clone());
  ZC_REQUIRE(crateKey != zc::none);
  auto encodedCrate = ZC_REQUIRE_NONNULL(crateKey).encodeCanonical();
  auto decodedCrate = ContextualCoreCrateKey::decodeCanonical(encodedCrate.asPtr());
  ZC_REQUIRE(decodedCrate != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedCrate) == ZC_REQUIRE_NONNULL(crateKey));

  auto marker = coreModule(core, "marker"_zc);
  auto moduleKey = ContextualCoreModuleKey::from(ZC_REQUIRE_NONNULL(roots).clone(), marker.clone());
  ZC_REQUIRE(moduleKey != zc::none);
  auto encodedModule = ZC_REQUIRE_NONNULL(moduleKey).encodeCanonical();
  auto decodedModule = ContextualCoreModuleKey::decodeCanonical(encodedModule.asPtr());
  ZC_REQUIRE(decodedModule != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedModule) == ZC_REQUIRE_NONNULL(moduleKey));

  auto user = tests::test_identity_detail::crate();
  ZC_EXPECT(ContextualCoreCrateKey::from(ZC_REQUIRE_NONNULL(roots).clone(), user.clone()) ==
            zc::none);
  ZC_EXPECT(ContextualCoreModuleKey::from(ZC_REQUIRE_NONNULL(roots).clone(),
                                          tests::test_identity_detail::module()) == zc::none);

  auto foreignCore = alternateCoreCrate();
  auto foreignRoots =
      incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(foreignCore);
  ZC_REQUIRE(foreignRoots != zc::none);
  auto mismatchedCrate = tests::test_identity_detail::coreCrate();
  ZC_EXPECT(ContextualCoreCrateKey::from(zc::mv(ZC_REQUIRE_NONNULL(foreignRoots)),
                                         zc::mv(mismatchedCrate)) == zc::none);

  auto trailing = zc::heapArray<uint8_t>(encodedCrate.size() + 1);
  for (size_t index = 0; index < encodedCrate.size(); ++index) {
    trailing[index] = encodedCrate[index];
  }
  trailing.back() = 0;
  ZC_EXPECT(ContextualCoreCrateKey::decodeCanonical(trailing.asPtr()) == zc::none);
}

ZC_TEST("Core distribution query input has exact high durability codecs") {
  ZC_EXPECT(CoreDistributionInput::descriptor.domain == "zom.query.core-distribution"_zc);
  ZC_EXPECT(CoreDistributionInput::descriptor.durability == query::Durability::High);

  auto key = identity::ToolchainUnitKey::core();
  auto encodedKey = CoreDistributionInput::encodeKey(key);
  auto decodedKey = CoreDistributionInput::decodeKey(encodedKey.asPtr());
  ZC_REQUIRE(decodedKey != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedKey).encode().asPtr() == key.encode().asPtr());
  ZC_EXPECT(CoreDistributionInput::decodeKey(encodedKey.slice(1, encodedKey.size())) == zc::none);
  auto trailingKey = zc::heapArray<uint8_t>(encodedKey.size() + 1);
  for (size_t index = 0; index < encodedKey.size(); ++index) {
    trailingKey[index] = encodedKey[index];
  }
  trailingKey.back() = 0;
  ZC_EXPECT(CoreDistributionInput::decodeKey(trailingKey.asPtr()) == zc::none);

  auto value = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(value != zc::none);
  auto encodedValue = CoreDistributionInput::encodeValue(ZC_REQUIRE_NONNULL(value));
  auto decodedValue = CoreDistributionInput::decodeValue(encodedValue.asPtr());
  ZC_REQUIRE(decodedValue != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decodedValue).encode().asPtr() ==
            ZC_REQUIRE_NONNULL(value).encode().asPtr());
  ZC_EXPECT(CoreDistributionInput::decodeValue(encodedValue.slice(0, encodedValue.size() - 1)) ==
            zc::none);
  auto mutatedValue = zc::heapArray<uint8_t>(encodedValue.asPtr());
  mutatedValue.back() ^= 0x01;
  ZC_EXPECT(CoreDistributionInput::decodeValue(mutatedValue.asPtr()) == zc::none);
}

ZC_TEST("Core distribution query input round trips through the query database") {
  auto database = queryDatabase();
  ZC_REQUIRE(registerCoreLibraryQueryProvider(database));
  auto duplicate = database.registerDescriptor<CoreDistributionInput>();
  ZC_EXPECT(!duplicate.isRegistered());

  auto value = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(value != zc::none);
  auto key = identity::ToolchainUnitKey::core();
  auto write = transaction(database);
  ZC_REQUIRE(write.set<CoreDistributionInput>(key, ZC_REQUIRE_NONNULL(value)).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto snapshot = database.snapshot();
  auto retained = snapshot.get<CoreDistributionInput>(key);
  ZC_REQUIRE(!retained.isRuntimeFailure());
  ZC_REQUIRE(retained.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(retained.value().encode().asPtr() == ZC_REQUIRE_NONNULL(value).encode().asPtr());
}

ZC_TEST("Active crates derive a singleton toolchain core from the distribution input") {
  auto database = queryDatabase();
  ZC_REQUIRE(incremental_binding_query::registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(registerCoreLibraryQueryProvider(database));

  auto crate = tests::test_identity_detail::coreCrate();
  auto roots = incremental_binding_query::CompilationRootSetQueryKey::singletonToolchainCore(crate);
  ZC_REQUIRE(roots != zc::none);
  auto missing = database.snapshot().get<incremental_binding_query::ActiveCratesQuery>(
      ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(missing.isRuntimeFailure());
  ZC_EXPECT(missing.runtimeFailure() == query::QueryRuntimeFailure::MissingInput);

  auto value = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(value != zc::none);
  auto write = transaction(database);
  ZC_REQUIRE(
      write
          .set<CoreDistributionInput>(identity::ToolchainUnitKey::core(), ZC_REQUIRE_NONNULL(value))
          .isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto snapshot = database.snapshot();
  auto active =
      snapshot.get<incremental_binding_query::ActiveCratesQuery>(ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(!active.isRuntimeFailure());
  ZC_REQUIRE(active.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(active.value().crates().size() == 1);
  ZC_EXPECT(active.value().crates()[0].canonicalCrateBytes() == crate.encode().asPtr());
  auto dependencies = snapshot.dependencies<incremental_binding_query::ActiveCratesQuery>(
      ZC_REQUIRE_NONNULL(roots));
  ZC_REQUIRE(dependencies.size() == 2);
  const auto expectedDistributionKey =
      CoreDistributionInput::encodeKey(identity::ToolchainUnitKey::core());
  for (const auto& dependencyGroup : dependencies) {
    ZC_REQUIRE(dependencyGroup.dependencies().size() == 1);
    ZC_EXPECT(dependencyGroup.dependencies()[0].key().canonicalBytes() ==
              expectedDistributionKey.asPtr());
  }
}

ZC_TEST("Active sources derive exact toolchain core membership and source dependencies") {
  auto database = queryDatabase();
  ZC_REQUIRE(incremental_binding_query::registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(registerCoreLibraryQueryProvider(database));

  auto crate = tests::test_identity_detail::coreCrate();
  auto stableCrate = incremental_binding_query::StableCrateQueryKey::fromVerified(crate);
  auto distribution = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(stableCrate != zc::none);
  ZC_REQUIRE(distribution != zc::none);
  auto write = transaction(database);
  ZC_REQUIRE(write
                 .set<CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                             ZC_REQUIRE_NONNULL(distribution))
                 .isApplied());
  stageCoreSourceInputs(write, crate, ZC_REQUIRE_NONNULL(distribution));
  ZC_REQUIRE(write.commit().isCommitted());

  auto snapshot = database.snapshot();
  auto active =
      snapshot.get<incremental_binding_query::ActiveSourcesQuery>(ZC_REQUIRE_NONNULL(stableCrate));
  ZC_REQUIRE(!active.isRuntimeFailure());
  ZC_REQUIRE(active.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(active.value().sources().size() == 3);
  auto dependencyGroups = snapshot.dependencies<incremental_binding_query::ActiveSourcesQuery>(
      ZC_REQUIRE_NONNULL(stableCrate));
  ZC_REQUIRE(dependencyGroups.size() == 8);
  const auto expectedDistributionKey =
      CoreDistributionInput::encodeKey(identity::ToolchainUnitKey::core());
  for (size_t index = 0; index < dependencyGroups.size(); ++index) {
    const auto& group = dependencyGroups[index];
    ZC_REQUIRE(group.kind() == query::DependencyGroup::Kind::Sequential);
    ZC_REQUIRE(group.dependencies().size() == 1);
    const size_t read = index % 4;
    if (read == 0) {
      ZC_EXPECT(group.dependencies()[0].key().canonicalBytes() == expectedDistributionKey.asPtr());
      continue;
    }
    const auto& file = ZC_REQUIRE_NONNULL(distribution).record().files()[read - 1];
    auto source = identity::SourceFileKey::from(
        crate.clone(), identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                                           file.path().clone()));
    auto stableSource = identity::source_query::StableSourceQueryKey::fromVerified(source);
    ZC_REQUIRE(stableSource != zc::none);
    const auto expectedSourceKey =
        identity::source_query::SourceSnapshotInput::encodeKey(ZC_REQUIRE_NONNULL(stableSource));
    ZC_EXPECT(group.dependencies()[0].key().canonicalBytes() == expectedSourceKey.asPtr());
  }
}

ZC_TEST("Verified core distribution input transaction commits the complete pre-parse root once") {
  auto database = queryDatabase();
  ZC_REQUIRE(incremental_binding_query::registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(module_graph_query::registerModuleGraphQueries(database));
  ZC_REQUIRE(registerCoreLibraryQueryProvider(database));
  auto distribution = admittedDistribution();
  auto options = verifiedCompilationOptions();
  zc::Vector<identity::CrateKey> consumers;
  consumers.add(tests::test_identity_detail::crate());
  consumers.add(tests::test_identity_detail::crate());
  const auto expectedPreviousRevision = database.snapshot().revision();
  auto prepared =
      prepareCoreTransaction(expectedPreviousRevision, distribution, options, consumers.asPtr());
  ZC_REQUIRE(prepared != zc::none);
  auto input = zc::mv(ZC_REQUIRE_NONNULL(prepared));
  ZC_REQUIRE(input.projections().size() == 1);
  const auto& projection = input.projections()[0];
  ZC_REQUIRE(projection.catalog().entries().size() == 3);
  ZC_REQUIRE(input.commit(database).isCommitted());
  ZC_EXPECT(!input.commit(database).isCommitted());

  auto snapshot = database.snapshot();
  auto retainedDistribution =
      snapshot.get<CoreDistributionInput>(identity::ToolchainUnitKey::core());
  ZC_REQUIRE(!retainedDistribution.isRuntimeFailure());
  ZC_REQUIRE(retainedDistribution.kind() == query::QueryValueKind::Value);
  auto expectedDistribution = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(expectedDistribution != zc::none);
  ZC_EXPECT(retainedDistribution.value().encode().asPtr() ==
            ZC_REQUIRE_NONNULL(expectedDistribution).encode().asPtr());
  auto retainedOptions =
      snapshot.get<identity::source_query::CompilationOptionsInput>(projection.crate());
  auto retainedRoots =
      snapshot.get<incremental_module_resolution_query::ModuleSearchRootsInput>(projection.crate());
  ZC_REQUIRE(!retainedOptions.isRuntimeFailure());
  ZC_REQUIRE(retainedOptions.kind() == query::QueryValueKind::Value);
  ZC_EXPECT(retainedOptions.value() == options);
  ZC_REQUIRE(!retainedRoots.isRuntimeFailure());
  ZC_REQUIRE(retainedRoots.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(retainedRoots.value().roots().size() == 1);
  ZC_EXPECT(retainedRoots.value().roots()[0].kind() == binder::ModuleSearchRootKind::ToolchainCore);
  for (const auto& entry : projection.catalog().entries()) {
    auto key = identity::source_query::StableSourceQueryKey::fromVerified(entry.source());
    ZC_REQUIRE(key != zc::none);
    auto retained =
        snapshot.get<identity::source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(key));
    ZC_REQUIRE(!retained.isRuntimeFailure());
    ZC_REQUIRE(retained.kind() == query::QueryValueKind::Value);
    ZC_EXPECT(retained.value().contentDigest() == entry.contentDigest());
  }
}

ZC_TEST("SessionInputTransactionTest.CorePayloadRejectsAuthorityAndProjectionMutations") {
  auto distribution = admittedDistribution();
  auto options = verifiedCompilationOptions();
  auto request = packageRequest();
  zc::Vector<identity::CrateKey> consumers;
  consumers.add(tests::test_identity_detail::crate());
  auto prepared =
      prepareCoreTransaction(query::DatabaseRevision(), distribution, options, consumers.asPtr());
  ZC_REQUIRE(prepared != zc::none);
  const auto& payload = ZC_REQUIRE_NONNULL(prepared).payload();
  ZC_EXPECT(VerifiedCoreDistributionInputVerifier::verify(payload, distribution, request, options,
                                                          consumers.asPtr()));

  auto encoded = payload.encodeCanonical();
  auto decoded = VerifiedCoreDistributionInputPayload::decodeCanonical(encoded.asPtr());
  ZC_REQUIRE(decoded != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(decoded) == payload);
  const auto wire = corePayloadWire(encoded.asPtr());
  ZC_REQUIRE(wire.sources.count >= 2);
  ZC_REQUIRE(wire.options.count >= 2);
  const auto expectRejected = [&](zc::Array<uint8_t>&& bytes) {
    auto candidate = VerifiedCoreDistributionInputPayload::decodeCanonical(bytes.asPtr());
    if (candidate != zc::none) {
      ZC_EXPECT(!VerifiedCoreDistributionInputVerifier::verify(
          ZC_REQUIRE_NONNULL(candidate), distribution, request, options, consumers.asPtr()));
    }
  };

  auto wrongDomain = mutation::flipByte(encoded.asPtr(), 0);
  ZC_EXPECT(VerifiedCoreDistributionInputPayload::decodeCanonical(wrongDomain.asPtr()) == zc::none);
  const mutation::WireRange scalarFields[] = {wire.context, wire.distribution, wire.digest,
                                              wire.policy, wire.authority};
  for (const auto field : scalarFields) {
    auto changed = field.begin == wire.digest.begin
                       ? mutation::flipByte(encoded.asPtr(), field.begin)
                       : mutation::flipPayloadByte(encoded.asPtr(), field);
    expectRejected(zc::mv(changed));
  }
  const mutation::SequenceRange projectionFields[] = {wire.sources, wire.options, wire.searchRoots,
                                                      wire.inventory};
  for (const auto& sequence : projectionFields) {
    const auto field = mutation::sequenceField(encoded.asPtr(), sequence, 0, 0);
    auto changed = mutation::flipPayloadByte(encoded.asPtr(), field);
    expectRejected(zc::mv(changed));
  }
  auto changedSource = mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.sources, 0, 1));
  expectRejected(zc::mv(changedSource));
  auto changedOptions = mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.options, 0, 1));
  expectRejected(zc::mv(changedOptions));
  auto changedSearchRoots = mutation::flipPayloadByte(
      encoded.asPtr(), mutation::sequenceField(encoded.asPtr(), wire.searchRoots, 0, 1));
  expectRejected(zc::mv(changedSearchRoots));

  auto duplicate = mutation::duplicateFirstElement(encoded.asPtr(), wire.sources);
  ZC_EXPECT(VerifiedCoreDistributionInputPayload::decodeCanonical(duplicate.asPtr()) == zc::none);
  auto reordered = mutation::swapFirstTwoElements(encoded.asPtr(), wire.sources);
  ZC_EXPECT(VerifiedCoreDistributionInputPayload::decodeCanonical(reordered.asPtr()) == zc::none);
  auto missing = mutation::removeFirstElement(encoded.asPtr(), wire.inventory);
  expectRejected(zc::mv(missing));
  auto excessiveCount = mutation::setSequenceCount(encoded.asPtr(), wire.sources, UINT64_MAX);
  ZC_EXPECT(VerifiedCoreDistributionInputPayload::decodeCanonical(excessiveCount.asPtr()) ==
            zc::none);
  auto excessiveBytes = mutation::setByteStringSize(encoded.asPtr(), wire.context, UINT64_MAX);
  ZC_EXPECT(VerifiedCoreDistributionInputPayload::decodeCanonical(excessiveBytes.asPtr()) ==
            zc::none);

  auto trailing = mutation::withTrailingByte(encoded.asPtr());
  ZC_EXPECT(VerifiedCoreDistributionInputPayload::decodeCanonical(trailing.asPtr()) == zc::none);
  ZC_EXPECT(VerifiedCoreDistributionInputPayload::decodeCanonical(
                encoded.asPtr().slice(0, encoded.size() - 1)) == zc::none);

  auto differentOptions = compilationOptions(false);
  ZC_EXPECT(!VerifiedCoreDistributionInputVerifier::verify(payload, distribution, request,
                                                           differentOptions, consumers.asPtr()));
  zc::Vector<identity::CrateKey> incompleteConsumers;
  ZC_EXPECT(!VerifiedCoreDistributionInputVerifier::verify(payload, distribution, request, options,
                                                           incompleteConsumers.asPtr()));
}

ZC_TEST(
    "Verified core distribution input transaction rejects context drift without partial writes") {
  auto distribution = admittedDistribution();
  auto options = compilationOptions(false);
  zc::Vector<identity::CrateKey> mismatchedConsumers;
  mismatchedConsumers.add(tests::test_identity_detail::crate());
  ZC_EXPECT(prepareCoreTransaction(query::DatabaseRevision(), distribution, options,
                                   mismatchedConsumers.asPtr()) == zc::none);

  auto matchingOptions = verifiedCompilationOptions();
  zc::Vector<identity::CrateKey> invalidConsumers;
  invalidConsumers.add(tests::test_identity_detail::coreCrate());
  ZC_EXPECT(prepareCoreTransaction(query::DatabaseRevision(), distribution, matchingOptions,
                                   invalidConsumers.asPtr()) == zc::none);

  auto database = queryDatabase();
  ZC_REQUIRE(incremental_binding_query::registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(module_graph_query::registerModuleGraphQueries(database));
  ZC_REQUIRE(registerCoreLibraryQueryProvider(database));
  auto accepted = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(accepted != zc::none);
  auto existing = transaction(database);
  ZC_REQUIRE(existing
                 .set<CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                             ZC_REQUIRE_NONNULL(accepted))
                 .isApplied());
  ZC_REQUIRE(existing.commit().isCommitted());

  zc::Vector<identity::CrateKey> consumers;
  consumers.add(tests::test_identity_detail::crate());
  auto prepared = prepareCoreTransaction(query::DatabaseRevision(), distribution, matchingOptions,
                                         consumers.asPtr());
  ZC_REQUIRE(prepared != zc::none);
  auto input = zc::mv(ZC_REQUIRE_NONNULL(prepared));
  const auto projected = input.projections()[0].crate().clone();
  ZC_EXPECT(!input.commit(database).isCommitted());
  auto absentOptions =
      database.snapshot().probeInput<identity::source_query::CompilationOptionsInput>(projected);
  ZC_REQUIRE(!absentOptions.isRuntimeFailure());
  ZC_EXPECT(absentOptions.kind() == query::QueryValueKind::Absence);
}

}  // namespace zomlang::compiler::driver::core_library_query
