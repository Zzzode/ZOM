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
// See the License for the specific language governing permissions and
// limitations under the License.

// RFC 0023 "IDE Semantic Snapshots": prove the single-hop, non-transitive
// freshness stamp reuses the engine's real changedAt/dependency predicate rather
// than a coarse revision proxy. The four discriminating cases are the point: a
// value-equal rewrite stays fresh, an edit to the tracked source goes stale, an
// edit to an unrelated source stays fresh, and erasing the options input goes
// stale. Cases 1 and 3 are exactly the ones a database-revision comparison gets
// wrong. This is not the RFC Analysis Lease and claims no InputsCurrent/Changed
// semantics.

#include "compiler/ide/semantic-snapshot-freshness.h"

#include "compiler/basic/thread-pool.h"
#include "compiler/driver/query/binding/incremental-binding-query-adapter.h"
#include "compiler/ide/document-version.h"
#include "compiler/ide/semantic-snapshot-key.h"
#include "compiler/identity/source-query-input.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ide {
namespace {

using namespace identity::source_query;
namespace package = driver::package;
using driver::incremental_binding_query::registerIncrementalBindingQueryAdapter;

basic::ThreadPool& freshnessTestScheduler() {
  static basic::ThreadPool scheduler(4);
  return scheduler;
}

class FreshnessTestSemanticContextResources final
    : public query::SemanticContextCapabilityResources {};

query::QueryDatabase freshnessTestDatabase() {
  auto resources = zc::heap<FreshnessTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  return query::QueryDatabase(freshnessTestScheduler(), query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid freshness fixture scalar");
}

identity::ResolvedVersion version() { return scalar<identity::ResolvedVersion>("0.0.0"_zc); }

identity::SortedFeatureSet features() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SortedTargetFeatureSet targetFeatures() {
  zc::Vector<identity::TargetFeatureName> values;
  auto result = identity::SortedTargetFeatureSet::from(zc::mv(values));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::PackageKey packageKey() {
  zc::Vector<identity::CanonicalPathSegment> path;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))),
      scalar<identity::PackageName>("ide_freshness"_zc), version(), features());
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
    const ir::TargetRegistrySnapshot& registry, package::SelectedLanguageOptions language) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(packageKey(), identity::CrateTargetKind::Binary,
                                                   scalar<identity::TargetName>("ide_freshness"_zc),
                                                   2026, false, compilationRootPath()));
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
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CrateKey crateKey() {
  auto result =
      identity::CrateKey::from(identity::CompilationUnitIdentity::userPackage(packageKey()),
                               identity::CrateTargetKind::Library,
                               scalar<identity::TargetName>("ide_freshness"_zc), compilation());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::SourceFileKey source(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> path;
  path.add(scalar<identity::CanonicalPathSegment>(name));
  return identity::SourceFileKey::from(
      crateKey(), identity::SourceOriginKey::localFile(
                      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(path))));
}

StableSourceQueryKey sourceQueryKey(zc::StringPtr name) {
  auto result = StableSourceQueryKey::fromVerified(source(name));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalSourceSnapshot sourceSnapshotValue(zc::StringPtr name, zc::Array<uint8_t>&& bytes) {
  auto snapshot = identity::ImmutableSourceSnapshot::from(source(name), zc::mv(bytes));
  auto result = CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(snapshot));
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalCompilationOptions compilationOptionsValue(const ir::TargetRegistrySnapshot& registry,
                                                    package::SelectedLanguageOptions language) {
  auto request = compilationRequest(registry, language);
  auto result = CanonicalCompilationOptions::fromVerified(request);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

query::InputTransaction transaction(query::QueryDatabase& database) {
  auto result = database.beginInputTransaction(database.snapshot().revision());
  ZC_REQUIRE(result.isOpened());
  return zc::mv(result).takeTransaction();
}

}  // namespace

ZC_TEST("Freshness stamp stays fresh across a value-equal source rewrite") {
  auto database = freshnessTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto sourceKey = sourceQueryKey("freshness-equal.zom"_zc);
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-equal.zom"_zc,
                                                                 zc::heapArray("let a = 1;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(
        write
            .set<CompilationOptionsInput>(
                crateKey(), compilationOptionsValue(registry, package::SelectedLanguageOptions{}))
            .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(1));
  auto tracked = resolveSemanticSnapshotTracked(database, key);
  ZC_REQUIRE(tracked.snapshot.isPublished());
  ZC_REQUIRE(tracked.freshness != zc::none);

  // Rewrite the same source with byte-identical content: the input value does
  // not change, so changedAt does not move and the stamp stays fresh.
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-equal.zom"_zc,
                                                                 zc::heapArray("let a = 1;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  ZC_EXPECT(ZC_REQUIRE_NONNULL(tracked.freshness).isFreshAgainst(database.snapshot()));
}

ZC_TEST("Freshness stamp goes stale when the tracked source is edited") {
  auto database = freshnessTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto sourceKey = sourceQueryKey("freshness-edit.zom"_zc);
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-edit.zom"_zc,
                                                                 zc::heapArray("let a = 1;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(
        write
            .set<CompilationOptionsInput>(
                crateKey(), compilationOptionsValue(registry, package::SelectedLanguageOptions{}))
            .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(1));
  auto tracked = resolveSemanticSnapshotTracked(database, key);
  ZC_REQUIRE(tracked.snapshot.isPublished());
  ZC_REQUIRE(tracked.freshness != zc::none);

  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-edit.zom"_zc,
                                                                 zc::heapArray("let a = 2;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(tracked.freshness).isFreshAgainst(database.snapshot()));
}

ZC_TEST("Freshness stamp goes stale when an editor overlay is committed over the source") {
  auto database = freshnessTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto sourceKey = sourceQueryKey("freshness-overlay.zom"_zc);
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-overlay.zom"_zc,
                                                                 zc::heapArray("let a = 1;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(
        write
            .set<CompilationOptionsInput>(
                crateKey(), compilationOptionsValue(registry, package::SelectedLanguageOptions{}))
            .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(1));
  auto tracked = resolveSemanticSnapshotTracked(database, key);
  ZC_REQUIRE(tracked.snapshot.isPublished());
  ZC_REQUIRE(tracked.freshness != zc::none);

  // Commit an editor overlay plus an OpenOverlay selection. The workspace source
  // input's changedAt does not move, but the overlay and selection inputs are
  // newly present, so the four-input stamp goes stale even though a workspace-only
  // stamp would wrongly report fresh.
  {
    auto overlay =
        sourceSnapshotValue("freshness-overlay.zom"_zc, zc::heapArray("let overlay = 4242;"_zcb));
    auto write = transaction(database);
    ZC_REQUIRE(write.set<EditorDocumentInput>(sourceKey, overlay).isApplied());
    ZC_REQUIRE(write
                   .set<IdeSourceSelectionInput>(
                       sourceKey, IdeSourceSelection::openOverlay(overlay.contentDigest()))
                   .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(tracked.freshness).isFreshAgainst(database.snapshot()));
}

ZC_TEST("Tracked resolve reports unavailable-cancelled with no freshness for a cancelled token") {
  auto database = freshnessTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto sourceKey = sourceQueryKey("freshness-cancelled.zom"_zc);
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-cancelled.zom"_zc,
                                                                 zc::heapArray("let a = 1;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(
        write
            .set<CompilationOptionsInput>(
                crateKey(), compilationOptionsValue(registry, package::SelectedLanguageOptions{}))
            .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(1));

  // Without a token the tracked resolve publishes and seals a freshness stamp.
  ZC_EXPECT(resolveSemanticSnapshotTracked(database, key).snapshot.isPublished());

  // A pre-cancelled token yields Unavailable(Cancelled) and no freshness stamp,
  // so a cancelled tracked result is never mistaken for fresh.
  query::CancellationSource cancellation;
  cancellation.cancel();
  auto tracked = resolveSemanticSnapshotTracked(database, key, cancellation.token());
  ZC_REQUIRE(tracked.snapshot.isUnavailable());
  ZC_EXPECT(tracked.snapshot.unavailableReason() == SnapshotUnavailableReason::Cancelled);
  ZC_EXPECT(tracked.freshness == zc::none);
}

ZC_TEST("Freshness stamp stays fresh when an unrelated source is edited") {
  auto database = freshnessTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto sourceKey = sourceQueryKey("freshness-tracked.zom"_zc);
  auto otherKey = sourceQueryKey("freshness-other.zom"_zc);
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-tracked.zom"_zc,
                                                                 zc::heapArray("let a = 1;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(
        write
            .set<CompilationOptionsInput>(
                crateKey(), compilationOptionsValue(registry, package::SelectedLanguageOptions{}))
            .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(1));
  auto tracked = resolveSemanticSnapshotTracked(database, key);
  ZC_REQUIRE(tracked.snapshot.isPublished());
  ZC_REQUIRE(tracked.freshness != zc::none);

  // Commit an unrelated source. The database revision advances, but the tracked
  // inputs' changedAt do not, so a coarse revision check would wrongly report
  // stale while this stays fresh.
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(otherKey,
                                             sourceSnapshotValue("freshness-other.zom"_zc,
                                                                 zc::heapArray("let b = 9;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  ZC_EXPECT(ZC_REQUIRE_NONNULL(tracked.freshness).isFreshAgainst(database.snapshot()));
}

ZC_TEST("Freshness stamp goes stale when the options input is erased") {
  auto database = freshnessTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto sourceKey = sourceQueryKey("freshness-erase.zom"_zc);
  {
    auto write = transaction(database);
    ZC_REQUIRE(write
                   .set<SourceSnapshotInput>(sourceKey,
                                             sourceSnapshotValue("freshness-erase.zom"_zc,
                                                                 zc::heapArray("let a = 1;"_zcb)))
                   .isApplied());
    ZC_REQUIRE(
        write
            .set<CompilationOptionsInput>(
                crateKey(), compilationOptionsValue(registry, package::SelectedLanguageOptions{}))
            .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(1));
  auto tracked = resolveSemanticSnapshotTracked(database, key);
  ZC_REQUIRE(tracked.snapshot.isPublished());
  ZC_REQUIRE(tracked.freshness != zc::none);

  {
    auto write = transaction(database);
    ZC_REQUIRE(write.erase<CompilationOptionsInput>(crateKey()).isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  ZC_EXPECT(!ZC_REQUIRE_NONNULL(tracked.freshness).isFreshAgainst(database.snapshot()));
}

}  // namespace zomlang::compiler::ide
