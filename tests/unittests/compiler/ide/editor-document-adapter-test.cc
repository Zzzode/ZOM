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

// RFC 0023 "IDE Semantic Snapshots" (SEAM B adapter core): prove the in-process
// editor-document adapter drives the overlay rail end to end. Opening a document
// makes the semantic facade resolve over the overlay bytes; a whole-document
// change with a strictly greater version updates them; a non-increasing version,
// an unknown id, malformed UTF-8, an invalid URI, and a duplicate open all reject
// without committing; and closing falls back to the workspace source. No protocol
// transport, no range edits: the adapter is constructed with a stable crate and
// maps a URI within it deterministically.

#include "compiler/ide/editor-document-adapter.h"

#include "compiler/basic/thread-pool.h"
#include "compiler/driver/query/binding/incremental-binding-query-adapter.h"
#include "compiler/ide/document-version.h"
#include "compiler/ide/semantic-snapshot-facade.h"
#include "compiler/ide/semantic-snapshot-key.h"
#include "compiler/identity/canonical/canonical-decoder.h"
#include "compiler/identity/key/source-key.h"
#include "compiler/identity/source-query-input.h"
#include "compiler/identity/source-snapshot.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ide {
namespace {

using namespace identity::source_query;
namespace package = driver::package;
using driver::incremental_binding_query::registerIncrementalBindingQueryAdapter;

basic::ThreadPool& adapterTestScheduler() {
  static basic::ThreadPool scheduler(4);
  return scheduler;
}

class AdapterTestSemanticContextResources final : public query::SemanticContextCapabilityResources {
};

query::QueryDatabase adapterTestDatabase() {
  auto resources = zc::heap<AdapterTestSemanticContextResources>();
  auto arena = zc::arc<query::SemanticContextCapabilityArena>(zc::mv(resources));
  return query::QueryDatabase(adapterTestScheduler(), query::productionQueryDescriptorInventory(),
                              zc::mv(arena));
}

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto value = Scalar::fromCanonical(text);
  ZC_IF_SOME(admitted, value) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("invalid adapter fixture scalar");
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
      scalar<identity::PackageName>("ide_adapter"_zc), version(), features());
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
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(packageKey(), identity::CrateTargetKind::Binary,
                                                   scalar<identity::TargetName>("ide_adapter"_zc),
                                                   2026, false, compilationRootPath()));
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
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

identity::CrateKey crateKey() {
  auto result =
      identity::CrateKey::from(identity::CompilationUnitIdentity::userPackage(packageKey()),
                               identity::CrateTargetKind::Library,
                               scalar<identity::TargetName>("ide_adapter"_zc), compilation());
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

CanonicalCompilationOptions optionsValue(const ir::TargetRegistrySnapshot& registry) {
  auto request = compilationRequest(registry);
  auto result = CanonicalCompilationOptions::fromVerified(request);
  return zc::mv(ZC_REQUIRE_NONNULL(result));
}

EditorDocumentAdapter makeAdapter(query::QueryDatabase& database,
                                  const ir::TargetRegistrySnapshot& registry) {
  return EditorDocumentAdapter(database, crateKey(), optionsValue(registry));
}

zc::Array<uint8_t> bytes(zc::StringPtr text) { return zc::heapArray<uint8_t>(text.asBytes()); }

ZC_TEST("EditorDocumentAdapter opens a document and the facade resolves over the overlay bytes") {
  auto database = adapterTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto adapter = makeAdapter(database, registry);

  auto opened = adapter.openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(1),
                                     bytes("let a = 1;"_zc).asPtr());
  ZC_REQUIRE(opened.isOpened());

  auto sourceKey = adapter.sourceKeyForUri("file:///src/main.zom"_zc);
  ZC_REQUIRE(sourceKey != zc::none);
  auto key =
      SemanticSnapshotKey::bind(ZC_ASSERT_NONNULL(sourceKey).clone(), DocumentVersion::initial(1));
  auto snapshot = resolveSemanticSnapshot(database, key);
  ZC_REQUIRE(snapshot.isPublished());
  ZC_EXPECT(snapshot.sourceByteLength() == bytes("let a = 1;"_zc).size());
}

ZC_TEST("EditorDocumentAdapter applies a whole-document change with a greater version") {
  auto database = adapterTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto adapter = makeAdapter(database, registry);
  auto opened = adapter.openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(1),
                                     bytes("let a = 1;"_zc).asPtr());
  ZC_REQUIRE(opened.isOpened());
  const auto id = opened.document();

  ZC_EXPECT(adapter.changeDocument(id, DocumentVersion::initial(2),
                                   bytes("let longer = 4242;"_zc).asPtr()) ==
            ChangeDocumentResult::Applied);
  auto sourceKey = adapter.sourceKeyForUri("file:///src/main.zom"_zc);
  auto key =
      SemanticSnapshotKey::bind(ZC_ASSERT_NONNULL(sourceKey).clone(), DocumentVersion::initial(2));
  auto snapshot = resolveSemanticSnapshot(database, key);
  ZC_REQUIRE(snapshot.isPublished());
  ZC_EXPECT(snapshot.sourceByteLength() == bytes("let longer = 4242;"_zc).size());
}

ZC_TEST("EditorDocumentAdapter rejects a non-increasing change version and commits nothing") {
  auto database = adapterTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto adapter = makeAdapter(database, registry);
  auto opened = adapter.openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(5),
                                     bytes("let a = 1;"_zc).asPtr());
  ZC_REQUIRE(opened.isOpened());
  const auto id = opened.document();

  // Equal and lower versions both reject.
  ZC_EXPECT(
      adapter.changeDocument(id, DocumentVersion::initial(5), bytes("let bb = 2;"_zc).asPtr()) ==
      ChangeDocumentResult::NonIncreasingVersion);
  ZC_EXPECT(
      adapter.changeDocument(id, DocumentVersion::initial(4), bytes("let bb = 2;"_zc).asPtr()) ==
      ChangeDocumentResult::NonIncreasingVersion);
  // The overlay still reflects the original bytes.
  auto sourceKey = adapter.sourceKeyForUri("file:///src/main.zom"_zc);
  auto key =
      SemanticSnapshotKey::bind(ZC_ASSERT_NONNULL(sourceKey).clone(), DocumentVersion::initial(5));
  auto snapshot = resolveSemanticSnapshot(database, key);
  ZC_REQUIRE(snapshot.isPublished());
  ZC_EXPECT(snapshot.sourceByteLength() == bytes("let a = 1;"_zc).size());
}

ZC_TEST("EditorDocumentAdapter rejects an unknown document, a duplicate open, and invalid inputs") {
  auto database = adapterTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto adapter = makeAdapter(database, registry);

  // Change/close on an id that was never opened.
  const auto bogus = EditorDocumentId::fromRaw(999);
  ZC_EXPECT(
      adapter.changeDocument(bogus, DocumentVersion::initial(1), bytes("let a = 1;"_zc).asPtr()) ==
      ChangeDocumentResult::UnknownDocument);
  ZC_EXPECT(adapter.closeDocument(bogus) == CloseDocumentResult::UnknownDocument);

  // An unnormalizable URI.
  ZC_EXPECT(
      adapter
          .openDocument("not a uri"_zc, DocumentVersion::initial(1), bytes("let a = 1;"_zc).asPtr())
          .kind() == OpenDocumentResult::Kind::InvalidUri);

  // Malformed UTF-8 (a lone continuation byte).
  auto invalidUtf8 = zc::heapArray<uint8_t>({0x80});
  ZC_EXPECT(
      adapter
          .openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(1), invalidUtf8.asPtr())
          .kind() == OpenDocumentResult::Kind::MalformedUtf8);

  // A successful open, then a duplicate open of the same URI.
  auto opened = adapter.openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(1),
                                     bytes("let a = 1;"_zc).asPtr());
  ZC_REQUIRE(opened.isOpened());
  ZC_EXPECT(adapter
                .openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(2),
                              bytes("let a = 1;"_zc).asPtr())
                .kind() == OpenDocumentResult::Kind::DuplicateOpen);
}

ZC_TEST("EditorDocumentAdapter rejects unsafe file URIs without aliasing a source key") {
  auto database = adapterTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto adapter = makeAdapter(database, registry);

  const zc::StringPtr unsafe[] = {
      // A non-empty, non-localhost authority: dropping it would alias distinct
      // hosts onto one key.
      "file://evil.example/src/main.zom"_zc,
      // A relative path (no leading slash after the authority).
      "file://relative/path"_zc,  // authority "relative", no path slash handled below
      // Dot and dot-dot traversal segments.
      "file:///src/../main.zom"_zc,
      "file:///./main.zom"_zc,
      // Percent-encoded dot-dot and dot, which this non-decoding parser must not
      // admit as literal segments.
      "file:///src/%2e%2e/main.zom"_zc,
      "file:///%2E/main.zom"_zc,
      // A path that is only a trailing slash yields no segment.
      "file:///"_zc,
      // A non-file scheme.
      "https://example.com/main.zom"_zc,
  };
  for (const auto& uri : unsafe) {
    ZC_EXPECT(adapter.sourceKeyForUri(uri) == zc::none, uri);
    ZC_EXPECT(adapter.openDocument(uri, DocumentVersion::initial(1), bytes("let a = 1;"_zc).asPtr())
                      .kind() == OpenDocumentResult::Kind::InvalidUri,
              uri);
  }

  // localhost is admitted and maps to the SAME key as an empty authority, so the
  // two spellings of one local file do not produce two distinct documents.
  auto emptyAuthority = adapter.sourceKeyForUri("file:///src/main.zom"_zc);
  auto localhost = adapter.sourceKeyForUri("file://localhost/src/main.zom"_zc);
  ZC_REQUIRE(emptyAuthority != zc::none);
  ZC_REQUIRE(localhost != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(emptyAuthority) == ZC_ASSERT_NONNULL(localhost));

  // Two different real paths map to two different keys (no accidental aliasing).
  auto other = adapter.sourceKeyForUri("file:///src/other.zom"_zc);
  ZC_REQUIRE(other != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(emptyAuthority) != ZC_ASSERT_NONNULL(other));
}

ZC_TEST("EditorDocumentAdapter close falls back to the workspace source") {
  auto database = adapterTestDatabase();
  ZC_REQUIRE(registerIncrementalBindingQueryAdapter(database));
  auto registry = targetRegistry();
  auto adapter = makeAdapter(database, registry);
  auto sourceKey = adapter.sourceKeyForUri("file:///src/main.zom"_zc);
  ZC_REQUIRE(sourceKey != zc::none);

  // Commit a workspace source directly (as a non-IDE producer would), then open an
  // overlay with different, longer bytes.
  {
    auto opened = database.beginInputTransaction(database.snapshot().revision());
    ZC_REQUIRE(opened.isOpened());
    auto write = zc::mv(opened).takeTransaction();
    auto immutable = identity::ImmutableSourceSnapshot::from(
        [&] {
          identity::CanonicalDecoder decoder(ZC_ASSERT_NONNULL(sourceKey).canonicalSourceBytes());
          auto file = identity::SourceFileKey::decodeCanonical(decoder);
          return ZC_REQUIRE_NONNULL(file).clone();
        }(),
        bytes("let w = 1;"_zc));
    auto workspace = CanonicalSourceSnapshot::fromVerified(ZC_REQUIRE_NONNULL(immutable));
    ZC_REQUIRE(
        write.set<SourceSnapshotInput>(ZC_ASSERT_NONNULL(sourceKey), ZC_REQUIRE_NONNULL(workspace))
            .isApplied());
    ZC_REQUIRE(write.commit().isCommitted());
  }
  auto opened = adapter.openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(1),
                                     bytes("let overlay = 4242;"_zc).asPtr());
  ZC_REQUIRE(opened.isOpened());

  auto key =
      SemanticSnapshotKey::bind(ZC_ASSERT_NONNULL(sourceKey).clone(), DocumentVersion::initial(1));
  auto overlaySnapshot = resolveSemanticSnapshot(database, key);
  ZC_REQUIRE(overlaySnapshot.isPublished());
  ZC_EXPECT(overlaySnapshot.sourceByteLength() == bytes("let overlay = 4242;"_zc).size());

  // Close: the effective source falls back to the workspace bytes.
  ZC_EXPECT(adapter.closeDocument(opened.document()) == CloseDocumentResult::Closed);
  auto workspaceSnapshot = resolveSemanticSnapshot(database, key);
  ZC_REQUIRE(workspaceSnapshot.isPublished());
  ZC_EXPECT(workspaceSnapshot.sourceByteLength() == bytes("let w = 1;"_zc).size());

  // Reopen after close issues a new id.
  auto reopened = adapter.openDocument("file:///src/main.zom"_zc, DocumentVersion::initial(1),
                                       bytes("let overlay = 4242;"_zc).asPtr());
  ZC_REQUIRE(reopened.isOpened());
  ZC_EXPECT(reopened.document() != opened.document());
}

}  // namespace
}  // namespace zomlang::compiler::ide
