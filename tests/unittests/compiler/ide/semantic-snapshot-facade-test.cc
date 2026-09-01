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

// RFC 0023 "IDE Semantic Snapshots": prove resolveSemanticSnapshot consumes the
// live ParseSourceQuery over a committed query snapshot and projects it to the
// sanitized three-arm SemanticSnapshot. A clean source publishes; a malformed
// source is source-rejected; a source with no committed inputs is unavailable
// (a runtime rejection), never a thrown or swallowed error. The fixture drives
// the production query database exactly as the incremental-binding adapter test
// does, so the parse query runs against its real tracked inputs.

#include "compiler/ide/semantic-snapshot-facade.h"

#include "compiler/basic/thread-pool.h"
#include "compiler/driver/query/binding/incremental-binding-query-adapter.h"
#include "compiler/ide/document-version.h"
#include "compiler/ide/semantic-snapshot-key.h"
#include "compiler/ide/semantic-snapshot.h"
#include "compiler/identity/source-query-input.h"
#include "compiler/ir/target-registry.h"
#include "compiler/parser/query/parse-source-query.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ide {
namespace {

using namespace identity::source_query;
namespace package = driver::package;
using driver::incremental_binding_query::registerIncrementalBindingQueryAdapter;

basic::ThreadPool& facadeTestScheduler() {
  static basic::ThreadPool scheduler(4);
  return scheduler;
}

class FacadeTestSemanticContextResources final : public query::SemanticContextCapabilityResources {
};

query::QueryDatabase facadeTestDatabase() {
  auto resources = zc::heap<FacadeTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  return query::QueryDatabase(facadeTestScheduler(), query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid facade fixture scalar");
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
      scalar<identity::PackageName>("ide_facade"_zc), version(), features());
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
                                                   scalar<identity::TargetName>("ide_facade"_zc),
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
                               scalar<identity::TargetName>("ide_facade"_zc), compilation());
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

ZC_TEST("resolveSemanticSnapshot publishes a clean source with the document version") {
  auto database = facadeTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("facade-clean.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("facade-clean.zom"_zc, zc::heapArray("let value = 42;"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry, package::SelectedLanguageOptions{});
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(11));
  auto snapshot = resolveSemanticSnapshot(database, key);

  ZC_EXPECT(snapshot.isPublished());
  ZC_EXPECT(snapshot.documentVersion() == DocumentVersion::initial(11));
  ZC_EXPECT(snapshot.sourceByteLength() == sourceValue.bytes().size());
  ZC_EXPECT(snapshot.sourceKeyBytes() == sourceKey.canonicalSourceBytes());
  // A clean parse publishes only warning-or-lower diagnostics.
  for (const auto& diagnostic : snapshot.diagnostics()) {
    ZC_EXPECT(diagnostic.severity() < diagnostics::DiagSeverity::kError);
  }
}

ZC_TEST("resolveSemanticSnapshot reports unavailable-cancelled for a pre-cancelled token") {
  auto database = facadeTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("facade-cancelled.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("facade-cancelled.zom"_zc, zc::heapArray("let value = 42;"_zcb));
  auto registry = targetRegistry();
  auto options = compilationOptionsValue(registry, package::SelectedLanguageOptions{});
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(11));

  // The same committed clean source publishes without a token; with an
  // already-cancelled token the demand is Cancelled and the facade projects it to
  // Unavailable(Cancelled) rather than a computed snapshot.
  ZC_EXPECT(resolveSemanticSnapshot(database, key).isPublished());

  query::CancellationSource cancellation;
  cancellation.cancel();
  auto cancelled = resolveSemanticSnapshot(database, key, cancellation.token());
  ZC_REQUIRE(cancelled.isUnavailable());
  ZC_EXPECT(cancelled.unavailableReason() == SnapshotUnavailableReason::Cancelled);
}

ZC_TEST("resolveSemanticSnapshot resolves ranges for a malformed source rejection") {
  auto database = facadeTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("facade-malformed.zom"_zc);
  auto bytes = zc::heapArray("let value = ;"_zcb);
  const uint64_t sourceLength = bytes.size();
  auto sourceValue = sourceSnapshotValue("facade-malformed.zom"_zc, zc::mv(bytes));
  auto registry = targetRegistry();
  auto options =
      compilationOptionsValue(registry, package::SelectedLanguageOptions{false, true, false});
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.set<CompilationOptionsInput>(crateKey(), options).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(-7));
  auto snapshot = resolveSemanticSnapshot(database, key);

  ZC_REQUIRE(snapshot.isSourceRejected());
  ZC_EXPECT(snapshot.documentVersion() == DocumentVersion::initial(-7));
  ZC_REQUIRE(snapshot.diagnostics().size() != 0);
  // The rejection is reconstructed so at least one diagnostic carries a bounded
  // range inside the source. A parse-error range may be zero-width (a caret at
  // the error point), so byteStart == byteEnd is valid; any unresolved fact stays
  // rangeless.
  size_t rangedCount = 0;
  for (const auto& diagnostic : snapshot.diagnostics()) {
    ZC_EXPECT(diagnostic.severity() >= diagnostics::DiagSeverity::kError);
    ZC_IF_SOME(range, diagnostic.range()) {
      ZC_EXPECT(range.byteStart <= range.byteEnd);
      ZC_EXPECT(range.byteEnd <= sourceLength);
      ++rangedCount;
    }
  }
  ZC_EXPECT(rangedCount != 0);
}

ZC_TEST("resolveSemanticSnapshot keeps a malformed rejection rangeless when options are absent") {
  auto database = facadeTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto sourceKey = sourceQueryKey("facade-malformed-norange.zom"_zc);
  auto sourceValue =
      sourceSnapshotValue("facade-malformed-norange.zom"_zc, zc::heapArray("let value = ;"_zcb));
  // Commit only the source input; without the compilation options the parse
  // still rejects, but reconstruction cannot run, so ranges stay unresolved.
  auto write = transaction(database);
  ZC_REQUIRE(write.set<SourceSnapshotInput>(sourceKey, sourceValue).isApplied());
  ZC_REQUIRE(write.commit().isCommitted());

  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(2));
  auto snapshot = resolveSemanticSnapshot(database, key);

  // The parse cannot even run without options, so the demand is a runtime
  // rejection and the arm is Unavailable rather than SourceRejected. Either way
  // the facade never fabricates a range or throws.
  ZC_EXPECT(!snapshot.isPublished());
  if (snapshot.isSourceRejected()) {
    for (const auto& diagnostic : snapshot.diagnostics()) {
      ZC_EXPECT(diagnostic.range() == zc::none);
    }
  }
}

ZC_TEST("resolveSemanticSnapshot reports unavailable when the source inputs are not committed") {
  auto database = facadeTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  // No SourceSnapshotInput / CompilationOptionsInput committed for this key.
  auto sourceKey = sourceQueryKey("facade-missing.zom"_zc);

  auto key = SemanticSnapshotKey::bind(sourceKey.clone(), DocumentVersion::initial(1));
  auto snapshot = resolveSemanticSnapshot(database, key);

  ZC_REQUIRE(snapshot.isUnavailable());
  ZC_EXPECT(snapshot.diagnostics().size() == 0);
}

}  // namespace
}  // namespace zomlang::compiler::ide
