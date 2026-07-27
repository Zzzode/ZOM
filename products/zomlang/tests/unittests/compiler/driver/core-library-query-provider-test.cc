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
#include "zomlang/compiler/driver/package/package-compilation-request.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/compiler/source/core-source-admission.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::driver::core_library_query {
namespace {

basic::ThreadPool& scheduler() {
  static basic::ThreadPool value(2);
  return value;
}

class QueryTestSemanticContextResources final : public query::SemanticContextCapabilityResources {};

query::QueryDatabase queryDatabase() {
  auto resources = zc::heap<QueryTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  return query::QueryDatabase(scheduler(), zc::mv(arena));
}

query::InputTransaction transaction(query::QueryDatabase& database) {
  auto result = database.beginInputTransaction();
  return zc::mv(ZC_REQUIRE_NONNULL(result));
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
    ZC_REQUIRE(write.set<identity::source_query::SourceSnapshotInput>(
        ZC_REQUIRE_NONNULL(stable), ZC_REQUIRE_NONNULL(snapshot)));
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
  auto contract = CoreDistributionInput::contract();
  ZC_EXPECT(contract.domain() == "zom.query.core-distribution"_zc);
  ZC_EXPECT(contract.isInput());
  ZC_EXPECT(contract.inputDurability() == query::Durability::High);

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
  ZC_EXPECT(database.registerInputKind<CoreDistributionInput>() == zc::none);

  auto value = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(value != zc::none);
  auto key = identity::ToolchainUnitKey::core();
  auto write = transaction(database);
  ZC_REQUIRE(write.set<CoreDistributionInput>(key, ZC_REQUIRE_NONNULL(value)));
  ZC_REQUIRE(write.commit() != zc::none);

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
  ZC_REQUIRE(write.set<CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                              ZC_REQUIRE_NONNULL(value)));
  ZC_REQUIRE(write.commit() != zc::none);

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
  ZC_REQUIRE(write.set<CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                              ZC_REQUIRE_NONNULL(distribution)));
  stageCoreSourceInputs(write, crate, ZC_REQUIRE_NONNULL(distribution));
  ZC_REQUIRE(write.commit() != zc::none);

  auto snapshot = database.snapshot();
  auto active =
      snapshot.get<incremental_binding_query::ActiveSourcesQuery>(ZC_REQUIRE_NONNULL(stableCrate));
  ZC_REQUIRE(!active.isRuntimeFailure());
  ZC_REQUIRE(active.kind() == query::QueryValueKind::Value);
  ZC_REQUIRE(active.value().sources().size() == 3);
  auto dependencyGroups = snapshot.dependencies<incremental_binding_query::ActiveSourcesQuery>(
      ZC_REQUIRE_NONNULL(stableCrate));
  ZC_REQUIRE(dependencyGroups.size() == 4);
  size_t distributionGroups = 0;
  size_t sourceGroups = 0;
  for (const auto& group : dependencyGroups) {
    if (group.dependencies().size() == 1) {
      ++distributionGroups;
    } else if (group.dependencies().size() == 3) {
      ++sourceGroups;
    }
  }
  ZC_EXPECT(distributionGroups == 2);
  ZC_EXPECT(sourceGroups == 2);
}

ZC_TEST("Verified core distribution input transaction commits the complete pre-parse root once") {
  auto database = queryDatabase();
  ZC_REQUIRE(incremental_binding_query::registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(registerCoreLibraryQueryProvider(database));
  auto distribution = admittedDistribution();
  auto options = compilationOptions();
  zc::Vector<identity::CrateKey> consumers;
  consumers.add(tests::test_identity_detail::crate());
  consumers.add(tests::test_identity_detail::crate());
  auto prepared =
      VerifiedCoreDistributionInputTransaction::prepare(distribution, options, consumers.asPtr());
  ZC_REQUIRE(prepared != zc::none);
  auto input = zc::mv(ZC_REQUIRE_NONNULL(prepared));
  ZC_REQUIRE(input.projections().size() == 1);
  const auto& projection = input.projections()[0];
  ZC_REQUIRE(projection.catalog().entries().size() == 3);
  ZC_REQUIRE(input.commit(database));
  ZC_EXPECT(!input.commit(database));

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

ZC_TEST(
    "Verified core distribution input transaction rejects context drift without partial writes") {
  auto distribution = admittedDistribution();
  auto options = compilationOptions(false);
  zc::Vector<identity::CrateKey> mismatchedConsumers;
  mismatchedConsumers.add(tests::test_identity_detail::crate());
  ZC_EXPECT(VerifiedCoreDistributionInputTransaction::prepare(
                distribution, options, mismatchedConsumers.asPtr()) == zc::none);

  auto matchingOptions = compilationOptions();
  zc::Vector<identity::CrateKey> invalidConsumers;
  invalidConsumers.add(tests::test_identity_detail::coreCrate());
  ZC_EXPECT(VerifiedCoreDistributionInputTransaction::prepare(
                distribution, matchingOptions, invalidConsumers.asPtr()) == zc::none);

  auto database = queryDatabase();
  ZC_REQUIRE(incremental_binding_query::registerIncrementalBindingQueryAdapter(database));
  ZC_REQUIRE(registerCoreLibraryQueryProvider(database));
  auto accepted = source::core::initialCoreDistributionInput();
  ZC_REQUIRE(accepted != zc::none);
  auto existing = transaction(database);
  ZC_REQUIRE(existing.set<CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                                 ZC_REQUIRE_NONNULL(accepted)));
  ZC_REQUIRE(existing.commit() != zc::none);

  zc::Vector<identity::CrateKey> consumers;
  consumers.add(tests::test_identity_detail::crate());
  auto prepared = VerifiedCoreDistributionInputTransaction::prepare(distribution, matchingOptions,
                                                                    consumers.asPtr());
  ZC_REQUIRE(prepared != zc::none);
  auto input = zc::mv(ZC_REQUIRE_NONNULL(prepared));
  const auto projected = input.projections()[0].crate().clone();
  ZC_EXPECT(!input.commit(database));
  auto absentOptions =
      database.snapshot().probeInput<identity::source_query::CompilationOptionsInput>(projected);
  ZC_REQUIRE(!absentOptions.isRuntimeFailure());
  ZC_EXPECT(absentOptions.kind() == query::QueryValueKind::Absence);
}

}  // namespace zomlang::compiler::driver::core_library_query
