// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "query-test-specs.h"
#include "zc/core/mutex.h"
#include "zc/core/thread.h"
#include "zc/ztest/test.h"
#include "compiler/binder/canonical/canonical-input-payload-digest.h"
#include "compiler/driver/query/binding/active-definition-authority-query.h"
#include "compiler/driver/core/query.h"
#include "compiler/driver/query/binding/incremental-binding-query-adapter.h"
#include "compiler/driver/query/module-graph/module-graph-query-input.h"
#include "compiler/driver/query/module-graph/module-graph-query.h"
#include "compiler/identity/canonical/identity-interner-set.h"
#include "compiler/ir/target-registry.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "tests/unittests/compiler/test-semantic-identities.h"

namespace zomlang::compiler::query::test {

namespace binding_query = driver::incremental_binding_query;
namespace core_query = driver::core_library_query;
namespace graph_query = driver::module_graph_query;
namespace resolution_query = driver::incremental_module_resolution_query;
namespace source_query = identity::source_query;

using tests::test_identity_detail::coreCrate;
using tests::test_identity_detail::crate;
using tests::test_identity_detail::digest;
using tests::test_identity_detail::module;
using tests::test_identity_detail::package;
using tests::test_identity_detail::scalar;
using tests::test_identity_detail::source;
using tests::test_identity_detail::target;

class EmptySemanticContextResources final : public SemanticContextCapabilityResources {};

class TestSemanticContextResources final : public SemanticContextCapabilityResources {
public:
  TestSemanticContextResources(identity::SemanticContextFactory& factory,
                               identity::SemanticContextBrand context,
                               zc::MutexGuarded<bool>& destroyed,
                               zc::MutexGuarded<bool>& reverseLookupSucceeded)
      : interners(identity::IdentityInternerSet::create(factory, context)),
        destroyedField(destroyed),
        reverseLookupSucceededField(reverseLookupSucceeded) {
    ZC_IREQUIRE(interners != zc::none, "test semantic context has no identity interner");
    auto result = ZC_ASSERT_NONNULL(interners).internCompilationUnit(
        context, tests::test_identity_detail::userUnit());
    ZC_IREQUIRE(result.is<identity::CompilationUnitId>(),
                "test semantic context failed to intern compilation unit");
    retainedUnit = result.get<identity::CompilationUnitId>();
  }
  ~TestSemanticContextResources() noexcept(false) override {
    *reverseLookupSucceededField.lockExclusive() =
        ZC_ASSERT_NONNULL(interners).compilationUnit(retainedUnit) != zc::none;
    *destroyedField.lockExclusive() = true;
  }

private:
  zc::Maybe<identity::IdentityInternerSet> interners;
  identity::CompilationUnitId retainedUnit;
  zc::MutexGuarded<bool>& destroyedField;
  zc::MutexGuarded<bool>& reverseLookupSucceededField;
};

QueryDatabase capabilityTestDatabase() {
  auto resources = zc::heap<EmptySemanticContextResources>();
  auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
  return QueryDatabase(queryTestScheduler(), queryTestDescriptorInventory(), zc::mv(arena));
}

QueryDatabase productionFinalSealTestDatabase() {
  auto resources = zc::heap<EmptySemanticContextResources>();
  auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
  return QueryDatabase(queryTestScheduler(), queryTestDescriptorInventory(), zc::mv(arena));
}

driver::package::RegisteredTargetProfileName finalSealProfileName() {
  auto result = driver::package::RegisteredTargetProfileName::from("query-capability-test"_zc);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

ir::TargetRegistrySnapshot finalSealTargetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> backendFeatures;
  auto specification = ir::CanonicalTargetSpec::from(
      "x-v-o-e"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(backendFeatures), "a"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(specification != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  specifications.add(zc::mv(ZC_REQUIRE_NONNULL(specification)));
  auto profile = ir::RegisteredTargetProfileRecord::from(
      finalSealProfileName(), target(), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  profiles.add(zc::mv(ZC_REQUIRE_NONNULL(profile)));
  auto registry = ir::TargetRegistrySnapshot::from(finalSealProfileName(), zc::mv(profiles));
  return zc::mv(ZC_REQUIRE_NONNULL(registry));
}

driver::package::RegisteredTargetSelection finalSealTargetSelection(
    const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  auto selected =
      ZC_REQUIRE_NONNULL(service).select(zc::none, driver::package::PackagePanicStrategy::Unwind);
  return zc::mv(ZC_REQUIRE_NONNULL(selected));
}

driver::package::VerifiedPackageCompilationRequest finalSealPackageRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>("test.zom"_zc));
  zc::Vector<driver::package::VerifiedCompilationRoot> roots;
  roots.add(driver::package::VerifiedCompilationRoot::from(
      package(), identity::CrateTargetKind::Library, scalar<identity::TargetName>("test"_zc), 2026,
      false, identity::CanonicalRelativePath::from(zc::mv(path))));
  auto request = driver::package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), finalSealTargetSelection(registry), finalSealTargetSelection(registry),
      driver::package::SelectedLanguageOptions{true, false, true},
      driver::package::PackageLockMode::PreferLocked);
  return zc::mv(ZC_REQUIRE_NONNULL(request));
}

binding_query::CompilationRootSetQueryKey finalSealContextRoots() {
  zc::Vector<binding_query::CompilationRootKey> roots;
  auto user = binding_query::CompilationRootKey::userPackage(package());
  auto core = binding_query::CompilationRootKey::toolchainCore(coreCrate());
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(user)));
  roots.add(zc::mv(ZC_REQUIRE_NONNULL(core)));
  auto result = binding_query::CompilationRootSetQueryKey::from(zc::mv(roots));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

binding_query::PackageRootSetKey finalSealPackageGraphRoots() {
  auto registry = finalSealTargetRegistry();
  auto request = finalSealPackageRequest(registry);
  auto result = binding_query::PackageRootSetKey::fromVerified(request);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

binding_query::CanonicalPackageGraph finalSealPackageGraph() {
  identity::CanonicalEncoder encoder;
  encoder.encodeSequenceSize(1);
  encoder.encodeByteString(package().encode().asPtr());
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(0);
  encoder.encodeSequenceSize(1);
  encoder.encodeByteString(crate().encode().asPtr());
  encoder.encodeSequenceSize(0);
  auto result = binding_query::PackageGraphInput::decodeValue(encoder.finish().asPtr());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

graph_query::CompleteCompilationContextAuthority finalSealAuthority() {
  auto registry = finalSealTargetRegistry();
  auto request = finalSealPackageRequest(registry);
  auto packageRoots = binding_query::PackageRootSetKey::fromVerified(request);
  auto distribution = source::core::initialCoreDistributionInput();
  auto options = source_query::CanonicalCompilationOptions::fromVerified(request);
  ZC_REQUIRE(packageRoots != zc::none);
  ZC_REQUIRE(distribution != zc::none);
  ZC_REQUIRE(options != zc::none);

  auto user = crate();
  auto core = coreCrate();
  zc::Vector<identity::CrateKey> userRoots;
  userRoots.add(user.clone());
  zc::Vector<identity::CrateKey> coreRoots;
  coreRoots.add(core.clone());
  zc::Vector<graph_query::CompilationOptionsEntry> optionEntries;
  optionEntries.add(graph_query::CompilationOptionsEntry::from(
      user.clone(), ZC_REQUIRE_NONNULL(options).clone()));
  optionEntries.add(graph_query::CompilationOptionsEntry::from(
      core.clone(), ZC_REQUIRE_NONNULL(options).clone()));

  zc::Vector<binder::ModuleSearchRoot> userEnvironment;
  zc::Vector<identity::CanonicalPathSegment> workspacePath;
  userEnvironment.add(binder::ModuleSearchRoot::workspace(
      user.clone(), identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(workspacePath))));
  auto userSearch =
      resolution_query::CanonicalModuleSearchRoots::fromVerified(user, userEnvironment.asPtr());
  auto coreRoot = binder::ModuleSearchRoot::toolchainCore(
      core.clone(), ZC_REQUIRE_NONNULL(distribution).digest());
  ZC_REQUIRE(userSearch != zc::none);
  ZC_REQUIRE(coreRoot != zc::none);
  zc::Vector<binder::ModuleSearchRoot> coreEnvironment;
  coreEnvironment.add(zc::mv(ZC_REQUIRE_NONNULL(coreRoot)));
  auto coreSearch =
      resolution_query::CanonicalModuleSearchRoots::fromVerified(core, coreEnvironment.asPtr());
  ZC_REQUIRE(coreSearch != zc::none);
  zc::Vector<graph_query::ModuleSearchRootsEntry> searchEntries;
  searchEntries.add(graph_query::ModuleSearchRootsEntry::from(
      user.clone(), zc::mv(ZC_REQUIRE_NONNULL(userSearch))));
  searchEntries.add(graph_query::ModuleSearchRootsEntry::from(
      core.clone(), zc::mv(ZC_REQUIRE_NONNULL(coreSearch))));

  auto graph = finalSealPackageGraph();
  const graph_query::CompleteCompilationContextSources sources{
      request,
      ZC_REQUIRE_NONNULL(packageRoots),
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

identity::RequesterModuleAncestry finalSealAncestry(identity::ModuleKey&& requester) {
  zc::Vector<identity::ModuleKey> ancestry;
  ancestry.add(requester.clone());
  auto result = identity::RequesterModuleAncestry::from(zc::mv(requester), zc::mv(ancestry));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

void stageFinalSealModule(query::InputTransaction& transaction, identity::ModuleKey&& moduleKey,
                          identity::SourceFileKey&& sourceKey, zc::Array<uint8_t>&& sourceBytes) {
  auto immutable = identity::ImmutableSourceSnapshot::from(sourceKey.clone(), zc::mv(sourceBytes));
  ZC_REQUIRE(immutable != zc::none);
  auto stableSource = source_query::StableSourceQueryKey::fromVerified(sourceKey);
  auto snapshot =
      source_query::CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(immutable));
  ZC_REQUIRE(stableSource != zc::none);
  ZC_REQUIRE(snapshot != zc::none);
  zc::Vector<graph_query::DetachedModuleDependencySite> noSites;
  auto sites = graph_query::DetachedModuleDependencySiteSet::from(
      moduleKey.clone(), sourceKey.clone(), ZC_REQUIRE_NONNULL(immutable).contentDigest(),
      zc::mv(noSites));
  ZC_REQUIRE(sites != zc::none);
  ZC_REQUIRE(transaction
                 .set<source_query::SourceSnapshotInput>(ZC_REQUIRE_NONNULL(stableSource),
                                                         ZC_REQUIRE_NONNULL(snapshot))
                 .isApplied());
  ZC_REQUIRE(
      transaction.set<graph_query::ModuleDependencySiteInput>(moduleKey, ZC_REQUIRE_NONNULL(sites))
          .isApplied());
  ZC_REQUIRE(transaction
                 .set<resolution_query::RequesterModuleAncestryInput>(
                     moduleKey, finalSealAncestry(moduleKey.clone()))
                 .isApplied());
}

void stageFinalSealCoreInputs(query::InputTransaction& transaction) {
  auto admitted = driver::core_library_test::admittedCoreDistribution();
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
    auto moduleKey = identity::ModuleKey::from(core.clone(), zc::mv(path));
    ZC_REQUIRE(moduleKey != zc::none);
    auto sourceKey = identity::SourceFileKey::from(
        core.clone(),
        identity::SourceOriginKey::coreFile(identity::ToolchainUnitKey::core(),
                                            admitted.snapshots()[index].path().clone()));
    stageFinalSealModule(transaction, ZC_REQUIRE_NONNULL(moduleKey).clone(), sourceKey.clone(),
                         zc::heapArray<uint8_t>(admitted.snapshots()[index].bytes()));
    catalogEntries.add(graph_query::SelectedModuleRecord(zc::mv(ZC_REQUIRE_NONNULL(moduleKey)),
                                                         zc::mv(sourceKey)));
  }
  auto catalog = graph_query::SelectedModuleCatalog::from(core.clone(), zc::mv(catalogEntries));
  ZC_REQUIRE(catalog != zc::none);
  ZC_REQUIRE(transaction
                 .set<core_query::CoreDistributionInput>(identity::ToolchainUnitKey::core(),
                                                         ZC_REQUIRE_NONNULL(distribution))
                 .isApplied());
  ZC_REQUIRE(
      transaction.set<graph_query::SelectedModuleCatalogInput>(core, ZC_REQUIRE_NONNULL(catalog))
          .isApplied());
  ZC_REQUIRE(transaction
                 .set<resolution_query::ConfiguredPreludeInput>(
                     core, resolution_query::ExplicitModuleTarget::absent())
                 .isApplied());
}

binding_query::CanonicalSourceSet finalSealUserSources() {
  auto stable = source_query::StableSourceQueryKey::fromVerified(source());
  ZC_REQUIRE(stable != zc::none);
  zc::Vector<source_query::StableSourceQueryKey> sources;
  sources.add(zc::mv(ZC_REQUIRE_NONNULL(stable)));
  auto result = binding_query::CanonicalSourceSet::from(zc::mv(sources));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

void stageFinalSealUserInputs(query::InputTransaction& transaction) {
  auto user = crate();
  auto moduleKey = module();
  auto sourceKey = source();
  zc::Vector<graph_query::SelectedModuleRecord> entries;
  entries.add(graph_query::SelectedModuleRecord(moduleKey.clone(), sourceKey.clone()));
  auto catalog = graph_query::SelectedModuleCatalog::from(user.clone(), zc::mv(entries));
  auto stableCrate = binding_query::StableCrateQueryKey::fromVerified(user);
  ZC_REQUIRE(catalog != zc::none);
  ZC_REQUIRE(stableCrate != zc::none);
  stageFinalSealModule(transaction, moduleKey.clone(), zc::mv(sourceKey),
                       zc::heapArray<uint8_t>("module test;\n"_zc.asBytes()));
  ZC_REQUIRE(transaction
                 .set<binding_query::UserPackageActiveSourcesInput>(ZC_REQUIRE_NONNULL(stableCrate),
                                                                    finalSealUserSources())
                 .isApplied());
  ZC_REQUIRE(
      transaction.set<graph_query::SelectedModuleCatalogInput>(user, ZC_REQUIRE_NONNULL(catalog))
          .isApplied());
  ZC_REQUIRE(transaction
                 .set<resolution_query::ConfiguredPreludeInput>(
                     user, resolution_query::ExplicitModuleTarget::absent())
                 .isApplied());
}

void registerProductionFinalSealQueries(QueryDatabase& database) {
  ZC_REQUIRE(database.registerDescriptor<graph_query::CoreDistributionTransactionWitnessInput>()
                 .isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::ModuleStructureTransactionWitnessInput>()
                 .isRegistered());
  ZC_REQUIRE(
      database.registerDescriptor<graph_query::ContextualIdentityAuthorityTransactionWitnessInput>()
          .isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::CompleteCompilationContextAuthorityInput>()
                 .isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::SelectedModuleCatalogInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::ModuleDependencySiteInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::SelectedModuleSource>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::ActiveModules>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::ModuleDependencySites>().isRegistered());
  ZC_REQUIRE(
      database.registerDescriptor<graph_query::ModuleDependencyRequests>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::ModuleDependencies>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::ModuleGraph>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<graph_query::ModuleGraphScc>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<core_query::CoreDistributionInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<binding_query::PackageGraphInput>().isRegistered());
  ZC_REQUIRE(
      database.registerDescriptor<binding_query::UserPackageActiveSourcesInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<binding_query::ActiveSources>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<binding_query::ActiveCrates>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<source_query::SourceSnapshotInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<source_query::CompilationOptionsInput>().isRegistered());
  ZC_REQUIRE(
      database.registerDescriptor<resolution_query::RequesterModuleAncestryInput>().isRegistered());
  ZC_REQUIRE(
      database.registerDescriptor<resolution_query::ModuleSearchRootsInput>().isRegistered());
  ZC_REQUIRE(
      database.registerDescriptor<resolution_query::ConfiguredPreludeInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<binding_query::ActiveDefinitionAuthorityReadyInput>()
                 .isRegistered());
  ZC_REQUIRE(database.registerDescriptor<binding_query::CompleteRootIdentityReadinessInput>()
                 .isRegistered());
}

binding_query::CompilationRootSetQueryKey installProductionFinalSealInputs(
    QueryDatabase& database, uint32_t capabilityKey, uint32_t capabilityValue) {
  auto roots = finalSealContextRoots();
  auto authority = finalSealAuthority();
  ZC_REQUIRE(authority.contextRoots() == roots);
  auto opened = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(opened.isOpened());
  auto transaction = zc::mv(opened).takeTransaction();
  stageFinalSealCoreInputs(transaction);
  stageFinalSealUserInputs(transaction);
  auto graphRoots = finalSealPackageGraphRoots();
  auto graph = finalSealPackageGraph();
  ZC_REQUIRE(transaction.set<binding_query::PackageGraphInput>(graphRoots, graph).isApplied());
  ZC_REQUIRE(transaction.set<LowInput>(capabilityKey, capabilityValue).isApplied());
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
  auto authorityBytes = authority.encodeCanonical();
  auto coreWitness = graph_query::computeCanonicalInputPayloadDigest(
      "zom.query.input-transaction.core-distribution"_zc, authorityBytes.asPtr());
  auto structureWitness = graph_query::computeCanonicalInputPayloadDigest(
      "zom.query.input-transaction.module-structure"_zc, authorityBytes.asPtr());
  auto identityWitness = graph_query::computeCanonicalInputPayloadDigest(
      "zom.query.input-transaction.contextual-identity-authority"_zc, authorityBytes.asPtr());
  ZC_REQUIRE(coreWitness != zc::none);
  ZC_REQUIRE(structureWitness != zc::none);
  ZC_REQUIRE(identityWitness != zc::none);
  ZC_REQUIRE(transaction
                 .set<graph_query::CoreDistributionTransactionWitnessInput>(
                     roots, ZC_REQUIRE_NONNULL(coreWitness))
                 .isApplied());
  ZC_REQUIRE(transaction
                 .set<graph_query::ModuleStructureTransactionWitnessInput>(
                     roots, ZC_REQUIRE_NONNULL(structureWitness))
                 .isApplied());
  ZC_REQUIRE(transaction
                 .set<graph_query::ContextualIdentityAuthorityTransactionWitnessInput>(
                     roots, ZC_REQUIRE_NONNULL(identityWitness))
                 .isApplied());
  zc::Vector<binding_query::ActiveDefinitionAuthorityRecord> noDefinitions;
  auto projection =
      binding_query::ActiveDefinitionAuthorityProjection::from(roots, zc::mv(noDefinitions));
  ZC_REQUIRE(projection != zc::none);
  auto readiness = binding_query::CompleteRootIdentityReadiness::from(
      roots.clone(), digest(0xd1), digest(0xd2), digest(0xd3), digest(0xd4));
  ZC_REQUIRE(transaction
                 .set<binding_query::ActiveDefinitionAuthorityReadyInput>(
                     roots, ZC_REQUIRE_NONNULL(projection).fingerprint())
                 .isApplied());
  ZC_REQUIRE(transaction.set<binding_query::CompleteRootIdentityReadinessInput>(roots, readiness)
                 .isApplied());
  ZC_REQUIRE(transaction.commit().isCommitted());
  return roots;
}

ZC_TEST("Canonical input payload digest preserves canonical bytes") {
  auto expectedDigest = digest(0xa5);
  auto bytes = expectedDigest.bytes();
  auto value = binder::CanonicalInputPayloadDigest::fromBytes(bytes);
  ZC_REQUIRE(value != zc::none);
  auto clone = ZC_ASSERT_NONNULL(value).clone();
  ZC_EXPECT(clone == ZC_ASSERT_NONNULL(value));
  ZC_EXPECT(clone.bytes() == bytes);

  auto malformed = zc::heapArray<uint8_t>(bytes.size() - 1);
  ZC_EXPECT(binder::CanonicalInputPayloadDigest::fromBytes(malformed.asPtr()) == zc::none);
}

void registerCapabilityKinds(QueryDatabase& database) {
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<LeafCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<ParentCapabilityQuery>().isRegistered());
}

}  // namespace zomlang::compiler::query::test

namespace zomlang::compiler::query {

template <>
class CapabilityCandidateContract<test::LeafCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::ParentCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::ParentCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::ParentCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::ParentCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::SlowCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::RejectedCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::TerminalCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::FinalSealedCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::LeafCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::LeafCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::LeafCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityCandidateContract<test::FinalSealedParentCapabilityQuery> final {
public:
  static StableWitnessBytes encode(const test::ParentCapability& candidate) {
    return StableWitnessBytes(test::encodeUint32(candidate.value()));
  }
  static zc::Maybe<zc::Own<test::ParentCapability>> decode(zc::ArrayPtr<const uint8_t> bytes) {
    auto value = test::decodeUint32(bytes);
    if (value == zc::none) { return zc::none; }
    return zc::heap<test::ParentCapability>(ZC_ASSERT_NONNULL(value), 0);
  }
};

template <>
class CapabilityFailureContract<test::TerminalCapabilityQuery, KeyRejection<uint32_t>> final {
public:
  static zc::Array<uint8_t> encode(const uint32_t& failure) { return test::encodeUint32(failure); }
  static zc::Maybe<uint32_t> decode(zc::ArrayPtr<const uint8_t> bytes) {
    return test::decodeUint32(bytes);
  }
  static CapabilityRejectionCheck verify(
      CapabilityQueryContext<test::TerminalCapabilityQuery>& context,
      const test::TerminalCapabilityQuery::Key& key, const uint32_t& failure) {
    auto input = context.probeInput<test::LowInput>(key);
    return key == failure && !input.isRuntimeFailure() && input.kind() == QueryValueKind::Absence
               ? CapabilityRejectionCheck::Verified
               : CapabilityRejectionCheck::Rejected;
  }
};

}  // namespace zomlang::compiler::query

namespace zomlang::compiler::query::test {

CapabilityProviderResult<LeafCapabilityQuery> LeafCapabilityQuery::provide(
    CapabilityQueryContext<LeafCapabilityQuery>& context, const Key& key) {
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<LeafCapabilityQuery>::runtimeRejected(input.runtimeFailure());
  }
  auto generation = context.probeInput<LowInput>(capabilityGenerationInputKey(key));
  if (generation.isRuntimeFailure()) {
    return CapabilityProviderResult<LeafCapabilityQuery>::runtimeRejected(
        generation.runtimeFailure());
  }
  const uint32_t generationValue =
      generation.kind() == QueryValueKind::Value ? generation.value() : 0;
  auto candidate = zc::heap<Capability>(input.value(), generationValue);
  auto witness = CapabilityCandidateContract<LeafCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<LeafCapabilityQuery>::candidate(zc::mv(candidate),
                                                                  zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> LeafCapabilityQuery::verify(
    CapabilityQueryContext<LeafCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<ParentCapabilityQuery> ParentCapabilityQuery::provide(
    CapabilityQueryContext<ParentCapabilityQuery>& context, const Key& key) {
  auto dependency = context.getCapability<LeafCapabilityQuery>(key);
  if (dependency.isRuntimeRejected()) {
    return CapabilityProviderResult<ParentCapabilityQuery>::runtimeRejected(
        dependency.runtimeFailure());
  }
  if (!dependency.isPublished()) {
    return CapabilityProviderResult<ParentCapabilityQuery>::runtimeRejected(
        QueryRuntimeFailure::InvariantViolation);
  }
  const auto& child = dependency.lease().capability();
  auto candidate = zc::heap<Capability>(child.value(), child.generation());
  auto witness = CapabilityCandidateContract<ParentCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<ParentCapabilityQuery>::candidate(zc::mv(candidate),
                                                                    zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> ParentCapabilityQuery::verify(
    CapabilityQueryContext<ParentCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<SlowCapabilityQuery> SlowCapabilityQuery::provide(
    CapabilityQueryContext<SlowCapabilityQuery>& context, const Key& key) {
  usleep(20000);
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<SlowCapabilityQuery>::runtimeRejected(input.runtimeFailure());
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<SlowCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<SlowCapabilityQuery>::candidate(zc::mv(candidate),
                                                                  zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> SlowCapabilityQuery::verify(
    CapabilityQueryContext<SlowCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<RejectedCapabilityQuery> RejectedCapabilityQuery::provide(
    CapabilityQueryContext<RejectedCapabilityQuery>& context, const Key& key) {
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<RejectedCapabilityQuery>::runtimeRejected(
        input.runtimeFailure());
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<RejectedCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<RejectedCapabilityQuery>::candidate(zc::mv(candidate),
                                                                      zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> RejectedCapabilityQuery::verify(
    CapabilityQueryContext<RejectedCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value() + 1);
}

CapabilityProviderResult<TerminalCapabilityQuery> TerminalCapabilityQuery::provide(
    CapabilityQueryContext<TerminalCapabilityQuery>& context, const Key& key) {
  auto input = context.probeInput<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<TerminalCapabilityQuery>::runtimeRejected(
        input.runtimeFailure());
  }
  if (input.kind() == QueryValueKind::Absence) {
    return CapabilityProviderResult<TerminalCapabilityQuery>::keyRejected(uint32_t{key});
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<TerminalCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<TerminalCapabilityQuery>::candidate(zc::mv(candidate),
                                                                      zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> TerminalCapabilityQuery::verify(
    CapabilityQueryContext<TerminalCapabilityQuery>& context, const Key& key,
    const Capability& candidate) {
  auto input = context.probeInput<LowInput>(key);
  if (input.isRuntimeFailure() || input.kind() != QueryValueKind::Value ||
      input.value() != candidate.value()) {
    return zc::none;
  }
  return encodeUint32(candidate.value());
}

TypedQueryResult<CapabilityRejectionProjectionQuery::Value>
CapabilityRejectionProjectionQuery::provide(QueryContext& context, const Key& key) {
  auto capability = context.getCapability<TerminalCapabilityQuery>(key);
  if (capability.isRuntimeRejected()) {
    return TypedQueryResult<Value>::runtimeFailure(capability.runtimeFailure());
  }
  if (capability.isKeyRejected()) {
    return TypedQueryResult<Value>::semanticFailure(encodeUint32(capability.keyFailure()));
  }
  if (!capability.isPublished()) {
    return TypedQueryResult<Value>::runtimeFailure(QueryRuntimeFailure::InvariantViolation);
  }
  return TypedQueryResult<Value>::value(capability.lease().capability().value());
}

bool CapabilityRejectionProjectionQuery::verify(QueryContext& context, const Key& key,
                                                const TypedQueryResult<Value>& result) {
  auto capability = context.getCapability<TerminalCapabilityQuery>(key);
  if (capability.isKeyRejected()) {
    return result.kind() == QueryValueKind::SemanticFailure &&
           result.semanticFailureBytes() == encodeUint32(capability.keyFailure()).asPtr();
  }
  return capability.isPublished() && result.kind() == QueryValueKind::Value &&
         result.value() == capability.lease().capability().value();
}

CapabilityProviderResult<FinalSealedCapabilityQuery> FinalSealedCapabilityQuery::provide(
    CapabilityQueryContext<FinalSealedCapabilityQuery>& context, const Key& key) {
  auto input = context.get<LowInput>(key);
  if (input.isRuntimeFailure()) {
    return CapabilityProviderResult<FinalSealedCapabilityQuery>::runtimeRejected(
        input.runtimeFailure());
  }
  auto candidate = zc::heap<Capability>(input.value(), 0);
  auto witness = CapabilityCandidateContract<FinalSealedCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<FinalSealedCapabilityQuery>::candidate(zc::mv(candidate),
                                                                         zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> FinalSealedCapabilityQuery::verify(
    CapabilityQueryContext<FinalSealedCapabilityQuery>&, const Key&, const Capability& candidate) {
  return encodeUint32(candidate.value());
}

CapabilityProviderResult<FinalSealedParentCapabilityQuery>
FinalSealedParentCapabilityQuery::provide(
    CapabilityQueryContext<FinalSealedParentCapabilityQuery>& context, const Key& key) {
  auto dependency = context.getCapability<FinalSealedCapabilityQuery>(key);
  if (dependency.isRuntimeRejected()) {
    return CapabilityProviderResult<FinalSealedParentCapabilityQuery>::runtimeRejected(
        dependency.runtimeFailure());
  }
  if (!dependency.isPublished()) {
    return CapabilityProviderResult<FinalSealedParentCapabilityQuery>::runtimeRejected(
        QueryRuntimeFailure::InvariantViolation);
  }
  auto candidate = zc::heap<Capability>(dependency.lease().capability().value(),
                                        dependency.lease().capability().generation());
  auto witness = CapabilityCandidateContract<FinalSealedParentCapabilityQuery>::encode(*candidate);
  return CapabilityProviderResult<FinalSealedParentCapabilityQuery>::candidate(zc::mv(candidate),
                                                                               zc::mv(witness));
}

zc::Maybe<zc::Array<uint8_t>> FinalSealedParentCapabilityQuery::verify(
    CapabilityQueryContext<FinalSealedParentCapabilityQuery>&, const Key&,
    const Capability& candidate) {
  return encodeUint32(candidate.value());
}

ZC_TEST("QueryCapabilityTest.SameSnapshotReturnsOneRetainedMemoGeneration") {
  auto database = capabilityTestDatabase();
  registerCapabilityKinds(database);
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(7, 70).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto snapshot = database.snapshot();

  auto first = snapshot.getCapability<LeafCapabilityQuery>(7);
  auto second = snapshot.getCapability<LeafCapabilityQuery>(7);
  ZC_REQUIRE(first.isPublished());
  ZC_REQUIRE(second.isPublished());
  ZC_EXPECT(first.lease().capability().value() == 70);
  ZC_EXPECT(first.lease().capability().generation() == second.lease().capability().generation());
  ZC_EXPECT(first.lease().revision() == DatabaseRevision(1));
  ZC_EXPECT(first.lease().arenaRevision() == DatabaseRevision(1));
  ZC_EXPECT(first.lease().stableWitness() == encodeUint32(70).asPtr());
  ZC_EXPECT(snapshot.hasRetainedValue<LeafCapabilityQuery>(7));
  ZC_EXPECT(!snapshot.evictValue<LeafCapabilityQuery>(7));

  size_t executed = 0;
  for (const auto& event : snapshot.events()) {
    if (event.kind() == QueryEventKind::Executed) { ++executed; }
  }
  ZC_EXPECT(executed == 1);
}

ZC_TEST("QueryCapabilityTest.NewRevisionCreatesDistinctGenerationWithoutBackdating") {
  auto database = capabilityTestDatabase();
  registerCapabilityKinds(database);
  auto firstWrite = beginTransaction(database);
  ZC_REQUIRE(firstWrite.set<LowInput>(3, 30).isApplied());
  ZC_REQUIRE(firstWrite.set<LowInput>(capabilityGenerationInputKey(3), 1).isApplied());
  ZC_REQUIRE(firstWrite.commit().isCommitted());
  auto firstSnapshot = database.snapshot();
  auto first = firstSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(first.isPublished());
  auto firstLease = first.lease().retain();

  auto secondWrite = beginTransaction(database);
  ZC_REQUIRE(secondWrite.set<LowInput>(3, 30).isApplied());
  ZC_REQUIRE(secondWrite.set<LowInput>(capabilityGenerationInputKey(3), 2).isApplied());
  ZC_REQUIRE(secondWrite.commit().isCommitted());
  auto secondSnapshot = database.snapshot();
  ZC_EXPECT(!secondSnapshot.hasRetainedValue<LeafCapabilityQuery>(3));
  auto second = secondSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(second.isPublished());
  ZC_EXPECT(second.lease().revision() == DatabaseRevision(2));
  ZC_EXPECT(firstLease.revision() == DatabaseRevision(1));
  ZC_EXPECT(firstLease.capability().generation() != second.lease().capability().generation());
  ZC_EXPECT(firstLease.stableWitness() == second.lease().stableWitness());
}

ZC_TEST("QueryCapabilityTest.ConcurrentDemandsJoinOneCapabilityFlight") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<SlowCapabilityQuery>().isRegistered());
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(8, 80).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto snapshot = database.snapshot();
  zc::MutexGuarded<zc::Vector<uint32_t>> generations;

  {
    zc::Thread first([&]() {
      auto result = snapshot.getCapability<SlowCapabilityQuery>(8);
      if (result.isPublished()) {
        generations.lockExclusive()->add(result.lease().capability().generation());
      }
    });
    zc::Thread second([&]() {
      auto result = snapshot.getCapability<SlowCapabilityQuery>(8);
      if (result.isPublished()) {
        generations.lockExclusive()->add(result.lease().capability().generation());
      }
    });
  }

  auto retained = generations.lockShared();
  ZC_REQUIRE(retained->size() == 2);
  ZC_EXPECT((*retained)[0] == (*retained)[1]);
  retained.release();
  ZC_EXPECT(hasEvent(snapshot.events().asPtr(), QueryEventKind::SingleFlightJoined));
}

ZC_TEST("QueryCapabilityTest.ParentMemoRetainsCapabilityDependencyTransitively") {
  zc::Maybe<QueryCapabilityLease<const ParentCapability>> survivingLease;
  {
    auto database = capabilityTestDatabase();
    registerCapabilityKinds(database);
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(11, 110).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
    auto snapshot = database.snapshot();

    auto parent = snapshot.getCapability<ParentCapabilityQuery>(11);
    ZC_REQUIRE(parent.isPublished());
    ZC_EXPECT(parent.lease().retainedDependencyCount() == 1);
    auto dependencies = snapshot.dependencies<ParentCapabilityQuery>(11);
    ZC_REQUIRE(dependencies.size() == 1);
    ZC_REQUIRE(dependencies[0].dependencies().size() == 1);
    auto witness = dependencies[0].dependencies()[0].stableWitness();
    ZC_REQUIRE(witness != zc::none);
    ZC_EXPECT(ZC_REQUIRE_NONNULL(witness) == encodeUint32(110).asPtr());
    survivingLease = parent.lease().retain();
  }

  ZC_REQUIRE(survivingLease != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(survivingLease).capability().value() == 110);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(survivingLease).retainedDependencyCount() == 1);
}

ZC_TEST("QueryCapabilityTest.SurvivingLeaseRetainsSessionAndSnapshotArenas") {
  zc::MutexGuarded<bool> resourcesDestroyed(false);
  zc::MutexGuarded<bool> reverseLookupSucceeded(false);
  zc::Maybe<QueryCapabilityLease<const LeafCapability>> survivingLease;
  {
    identity::SemanticContextFactory contextFactory;
    auto issuedContext = contextFactory.issue();
    ZC_REQUIRE(issuedContext != zc::none);
    auto resources =
        zc::heap<TestSemanticContextResources>(contextFactory, ZC_REQUIRE_NONNULL(issuedContext),
                                               resourcesDestroyed, reverseLookupSucceeded);
    auto arena = zc::arc<SemanticContextCapabilityArena>(zc::mv(resources));
    QueryDatabase database(queryTestScheduler(), queryTestDescriptorInventory(), zc::mv(arena));
    ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
    ZC_REQUIRE(database.registerDescriptor<LeafCapabilityQuery>().isRegistered());
    auto write = beginTransaction(database);
    ZC_REQUIRE(write.set<LowInput>(12, 120).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
    auto snapshot = database.snapshot();
    auto result = snapshot.getCapability<LeafCapabilityQuery>(12);
    ZC_REQUIRE(result.isPublished());
    survivingLease = result.lease().retain();
  }

  ZC_EXPECT(!*resourcesDestroyed.lockShared());
  ZC_EXPECT(!*reverseLookupSucceeded.lockShared());
  survivingLease = zc::none;
  ZC_EXPECT(*resourcesDestroyed.lockShared());
  ZC_EXPECT(*reverseLookupSucceeded.lockShared());
}

ZC_TEST("QueryCapabilityTest.VerifierMismatchAndTypedKeyRejectionPublishExactly") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<RejectedCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<TerminalCapabilityQuery>().isRegistered());
  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(5, 50).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto snapshot = database.snapshot();

  auto rejected = snapshot.getCapability<RejectedCapabilityQuery>(5);
  ZC_REQUIRE(rejected.isRuntimeRejected());
  ZC_EXPECT(rejected.runtimeFailure() == QueryRuntimeFailure::VerifierRejected);
  ZC_EXPECT(snapshot.metadata<RejectedCapabilityQuery>(5) == zc::none);

  auto terminal = snapshot.getCapability<TerminalCapabilityQuery>(9);
  ZC_REQUIRE(terminal.isKeyRejected());
  ZC_EXPECT(terminal.keyFailure() == 9);
}

ZC_TEST("QueryCapabilityTest.SemanticParentInvalidatesAfterCapabilityRejection") {
  auto database = capabilityTestDatabase();
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<TerminalCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<CapabilityRejectionProjectionQuery>().isRegistered());

  auto rejectedSnapshot = database.snapshot();
  auto rejected = rejectedSnapshot.get<CapabilityRejectionProjectionQuery>(9);
  ZC_REQUIRE(rejected.kind() == QueryValueKind::SemanticFailure);
  auto dependencies = rejectedSnapshot.dependencies<CapabilityRejectionProjectionQuery>(9);
  ZC_REQUIRE(dependencies.size() == 2);
  for (const auto& group : dependencies) {
    ZC_REQUIRE(group.dependencies().size() == 1);
    ZC_EXPECT(group.dependencies()[0].key().fingerprint() ==
              ZC_REQUIRE_NONNULL(rejectedSnapshot.keyFingerprint<TerminalCapabilityQuery>(9)));
  }

  auto write = beginTransaction(database);
  ZC_REQUIRE(write.set<LowInput>(9, 90).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());
  auto recovered = database.snapshot().get<CapabilityRejectionProjectionQuery>(9);
  ZC_REQUIRE(recovered.kind() == QueryValueKind::Value);
  ZC_EXPECT(recovered.value() == 90);
}

ZC_TEST("QueryCapabilityTest.FinalSealedCapabilityRequiresMatchingAdmission") {
  using CompleteContext = graph_query::CompleteCompilationContextAuthorityInput;

  auto database = productionFinalSealTestDatabase();
  registerProductionFinalSealQueries(database);
  ZC_REQUIRE(database.registerDescriptor<LowInput>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<FinalSealedCapabilityQuery>().isRegistered());
  ZC_REQUIRE(database.registerDescriptor<FinalSealedParentCapabilityQuery>().isRegistered());
  auto roots = installProductionFinalSealInputs(database, 7, 70);
  auto ordinary = database.snapshot();

  auto rejected = ordinary.getCapability<FinalSealedCapabilityQuery>(7);
  ZC_REQUIRE(rejected.isRuntimeRejected());
  ZC_EXPECT(rejected.runtimeFailure() == QueryRuntimeFailure::FinalSealRequired);

  auto witness = graph_query::computeFinalSnapshotWitness(ordinary, roots);
  ZC_REQUIRE(witness != zc::none);
  auto seal = database.sealInputs<CompleteContext>(ordinary, roots, ZC_REQUIRE_NONNULL(witness));
  ZC_REQUIRE(seal.isSealed());
  auto admitted = database.admitFinalSnapshot<CompleteContext>(database.snapshot(), seal.seal());
  ZC_REQUIRE(admitted.isAdmitted());
  auto sealed = zc::mv(admitted).takeSnapshot();
  auto published = sealed.getCapability<FinalSealedCapabilityQuery>(7);
  ZC_REQUIRE(published.isPublished());
  ZC_EXPECT(published.lease().capability().value() == 70);
  auto nested = sealed.getCapability<FinalSealedParentCapabilityQuery>(7);
  ZC_REQUIRE(nested.isPublished());
  ZC_EXPECT(nested.lease().capability().value() == 70);
}

ZC_TEST("QueryCapabilityTest.RealErasedResultsRejectForeignCoordinates") {
  auto firstDatabase = capabilityTestDatabase();
  auto secondDatabase = capabilityTestDatabase();
  for (auto* database : {&firstDatabase, &secondDatabase}) {
    ZC_REQUIRE(database->registerDescriptor<LowInput>().isRegistered());
    ZC_REQUIRE(database->registerDescriptor<LeafCapabilityQuery>().isRegistered());
    ZC_REQUIRE(database->registerDescriptor<SlowCapabilityQuery>().isRegistered());
    auto write = beginTransaction(*database);
    ZC_REQUIRE(write.set<LowInput>(3, 30).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto firstSnapshot = firstDatabase.snapshot();
  auto secondSnapshot = secondDatabase.snapshot();

  auto foreignDatabase = QueryRuntimeTestAccess::evaluate<LeafCapabilityQuery>(firstSnapshot, 3);
  auto foreignDatabaseResult =
      QueryRuntimeTestAccess::decode<LeafCapabilityQuery>(zc::mv(foreignDatabase), secondSnapshot);
  ZC_REQUIRE(foreignDatabaseResult.isRuntimeRejected());
  ZC_EXPECT(foreignDatabaseResult.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

  auto wrongDescriptor = QueryRuntimeTestAccess::evaluate<LeafCapabilityQuery>(firstSnapshot, 3);
  auto wrongDescriptorResult =
      QueryRuntimeTestAccess::decode<SlowCapabilityQuery>(zc::mv(wrongDescriptor), firstSnapshot);
  ZC_REQUIRE(wrongDescriptorResult.isRuntimeRejected());
  ZC_EXPECT(wrongDescriptorResult.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

  auto staleRevision = QueryRuntimeTestAccess::evaluate<LeafCapabilityQuery>(firstSnapshot, 3);
  auto nextWrite = beginTransaction(firstDatabase);
  ZC_REQUIRE(nextWrite.set<LowInput>(3, 31).isApplied());
  ZC_REQUIRE(nextWrite.commit().isCommitted());
  auto nextSnapshot = firstDatabase.snapshot();
  auto staleRevisionResult =
      QueryRuntimeTestAccess::decode<LeafCapabilityQuery>(zc::mv(staleRevision), nextSnapshot);
  ZC_REQUIRE(staleRevisionResult.isRuntimeRejected());
  ZC_EXPECT(staleRevisionResult.runtimeFailure() == QueryRuntimeFailure::InvariantViolation);

  auto original = firstSnapshot.getCapability<LeafCapabilityQuery>(3);
  ZC_REQUIRE(original.isPublished());
  ZC_EXPECT(original.lease().capability().value() == 30);
}

}  // namespace zomlang::compiler::query::test
