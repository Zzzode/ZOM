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

// Cover resolveWorkspacePackageInput, the single session-agnostic authority that
// materializes, resolves, and verifies one workspace's package inputs. The happy
// path reads a real in-memory workspace and returns installed inputs; each typed
// ResolveFailure / VerifyFailure arm is exercised so the CLI and the IDE service
// can locate the exact rejected step. A minimal in-memory filesystem and an
// injected in-memory snapshot factory back the tests so no disk state is touched.

#include "compiler/driver/package/workspace-package-resolver.h"

#include "compiler/driver/package/source-inventory.h"
#include "compiler/driver/package/workspace-normalizer.h"
#include "compiler/ir/target/target-registry.h"
#include "zc/core/encoding.h"
#include "zc/core/filesystem.h"
#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid workspace-package-resolver fixture scalar");
}

// A minimal filesystem over one in-memory directory rooted at the empty path.
class MemoryFilesystem final : public zc::Filesystem {
public:
  explicit MemoryFilesystem(zc::Own<const zc::Directory> root)
      : rootDir(zc::mv(root)), currentPath(zc::Path::parse(""_zc)) {}
  const zc::Directory& getRoot() const override { return *rootDir; }
  const zc::Directory& getCurrent() const override { return *rootDir; }
  zc::PathPtr getCurrentPath() const override { return currentPath; }

private:
  zc::Own<const zc::Directory> rootDir;
  zc::Path currentPath;
};

// An in-memory fresh-source-directory factory: materialization copies land in
// heap-backed directories, so teardown is a no-op and no disk state leaks.
class MemoryFreshSourceDirectory final : public FreshSourceDirectory {
public:
  MemoryFreshSourceDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MemoryFreshSourceDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MemoryFreshSourceDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  FreshSourceDirectoryResult create() override {
    zc::Own<FreshSourceDirectory> result = zc::heap<MemoryFreshSourceDirectory>();
    return zc::mv(result);
  }
};

constexpr zc::StringPtr kManifest =
    "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc;

// Builds an in-memory workspace filesystem with one library package.
zc::Own<zc::Filesystem> workspaceFilesystem() {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  root->openFile(zc::Path({"Zom.toml"_zc}), zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(kManifest);
  root->openFile(zc::Path({"src"_zc, "lib.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("fn helper() {}\n"_zc);
  return zc::heap<MemoryFilesystem>(zc::mv(root));
}

ir::TargetRegistrySnapshot targetRegistry(zc::StringPtr abiProfile = "zom"_zc) {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), abiProfile,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
  auto projection = [&]() {
    ZC_IF_SOME(values, sorted) {
      auto result = identity::CanonicalTargetSpecificationKey::from(
          scalar<identity::TargetComponentName>("x86_64"_zc),
          scalar<identity::TargetComponentName>("zom"_zc),
          scalar<identity::TargetComponentName>("none"_zc),
          scalar<identity::TargetComponentName>("unknown"_zc),
          scalar<identity::TargetComponentName>(abiProfile), 64, identity::Endianness::Little,
          zc::mv(values));
      ZC_IF_SOME(value, result) { return zc::mv(value); }
    }
    ZC_FAIL_REQUIRE("invalid resolver fixture target projection");
  }();
  auto profileName = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_REQUIRE(profileName != zc::none);
  auto profile = ir::RegisteredTargetProfileRecord::from(
      ZC_ASSERT_NONNULL(profileName).clone(), zc::mv(projection), zc::mv(semanticFeatures),
      zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry =
      ir::TargetRegistrySnapshot::from(ZC_ASSERT_NONNULL(profileName).clone(), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid resolver fixture target registry");
}

ir::VerifiedTargetSelection verifiedSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  package::RegisteredTargetSelection selection = [&]() {
    ZC_IF_SOME(targets, service) {
      auto selected = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
      ZC_IF_SOME(value, selected) { return zc::mv(value); }
    }
    ZC_FAIL_REQUIRE("resolver fixture target selection failed");
  }();
  auto verified = registry.verify(selection);
  ZC_REQUIRE(verified.is<ir::VerifiedTargetSelection>());
  return zc::mv(verified.get<ir::VerifiedTargetSelection>());
}

// The verified selection derived from a fresh, deterministic registry selection.
// Passing this for host and target byte-matches the request built with
// selection(registry), the way the driver derives the pair.
ir::VerifiedTargetSelection verifiedSelectionOf(const ir::TargetRegistrySnapshot& registry) {
  return verifiedSelection(registry);
}

// The package key for the "app" package rooted at the workspace local path, so it
// matches the package the resolution output covers.
identity::PackageKey appPackageKey() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::PackageKey::from(
      identity::CanonicalPackageSource::localPath(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments))),
      scalar<identity::PackageName>("app"_zc), scalar<identity::ResolvedVersion>("1.0.0"_zc), []() {
        zc::Vector<identity::FeatureName> values;
        auto result = identity::SortedFeatureSet::from(zc::mv(values));
        ZC_IF_SOME(value, result) { return zc::mv(value); }
        ZC_FAIL_REQUIRE("resolver fixture feature set");
      }());
}

package::RegisteredTargetSelection selectionOf(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto selected = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, selected) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("resolver fixture target selection failed");
}

// Builds the verified request directly (the light path the driver and the proven
// session suite use), so its host/target selections byte-match verifiedSelection.
VerifiedPackageCompilationRequest verifiedRequest(
    const ir::TargetRegistrySnapshot& registry,
    PackageLockMode lockMode = PackageLockMode::PreferLocked) {
  zc::Vector<VerifiedCompilationRoot> roots;
  zc::Vector<identity::CanonicalPathSegment> sourceSegments;
  sourceSegments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  sourceSegments.add(scalar<identity::CanonicalPathSegment>("lib.zom"_zc));
  roots.add(VerifiedCompilationRoot::from(
      appPackageKey(), identity::CrateTargetKind::Library, scalar<identity::TargetName>("app"_zc),
      2026, false, identity::CanonicalRelativePath::from(zc::mv(sourceSegments))));
  auto result = VerifiedPackageCompilationRequest::from(
      zc::mv(roots), selectionOf(registry), selectionOf(registry),
      package::SelectedLanguageOptions{}, lockMode);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("resolver fixture verified request was rejected");
}

NormalizedPackageCompilationRequest normalizedRequest(const ir::TargetRegistrySnapshot& registry,
                                                      uint32_t lockedCount = 0) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  RawPackageCompilationRequest raw;
  raw.packageSelections.add(zc::str("app"));
  raw.lockedCount = lockedCount;
  zc::Maybe<identity::TargetName> noName;
  raw.targetSelections.add(
      RequestedTargetSelection(identity::CrateTargetKind::Library, zc::mv(noName)));
  ZC_IF_SOME(targets, service) {
    auto result = normalizePackageCompilationRequest(zc::mv(raw), targets);
    ZC_REQUIRE(result.is<NormalizedPackageCompilationRequest>());
    return zc::mv(result.get<NormalizedPackageCompilationRequest>());
  }
  ZC_FAIL_REQUIRE("resolver fixture request normalization failed");
}

NormalizedWorkspace loadedWorkspace() {
  zc::Vector<identity::CanonicalRelativePath> files;
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>("lib.zom"_zc));
  files.add(identity::CanonicalRelativePath::from(zc::mv(segments)));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(inventoryValue, inventory) {
    zc::Vector<WorkspaceMemberInput> members;
    auto result = normalizeWorkspace(kManifest, inventoryValue, zc::mv(members));
    ZC_REQUIRE(result.is<NormalizedWorkspace>());
    return zc::mv(result.get<NormalizedWorkspace>());
  }
  ZC_FAIL_REQUIRE("resolver fixture workspace normalization failed");
}

// --- Lockfile trust test fixtures ---

identity::Sha256Digest fixtureDigest(zc::StringPtr text) {
  auto result = identity::sha256(text.asBytes());
  ZC_IF_SOME(value, result) { return value; }
  ZC_UNREACHABLE
}

identity::CanonicalUrl fixtureUrl(zc::StringPtr text) {
  auto result = identity::CanonicalUrl::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::SortedFeatureSet fixtureEmptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::PackageKey fixtureRegistryPackageKey() {
  auto name = identity::PackageName::fromCanonical("codec"_zc);
  auto version = identity::ResolvedVersion::fromCanonical("2.0.0"_zc);
  ZC_IF_SOME(nameValue, name) {
    ZC_IF_SOME(versionValue, version) {
      return identity::PackageKey::from(
          identity::CanonicalPackageSource::registry(identity::RegistryIdentity::from(
              fixtureUrl("https://example.com/index"_zc), fixtureDigest("registry trust"_zc))),
          zc::mv(nameValue), zc::mv(versionValue), fixtureEmptyFeatures());
    }
  }
  ZC_UNREACHABLE
}

Ed25519PublicKey fixturePublicKey() {
  auto bytes = zc::heapArray<zc::byte>(32, static_cast<zc::byte>(0x2a));
  auto result = Ed25519PublicKey::fromBytes(bytes);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

// Builds a minimal Zom.lock containing one registry-sourced package whose
// registry identity is https://example.com/index with trust-domain-sha256
// derived from "registry trust".
zc::String fixtureRegistryLockToml() {
  auto key = fixtureRegistryPackageKey();
  auto record = LockPackageRecord::from(
      zc::mv(key), fixtureDigest("registry manifest"_zc), fixtureDigest("registry tree"_zc),
      ArchiveFormat::TarZstd, fixtureDigest("archive"_zc), SigningKeyId::from(fixturePublicKey()));
  ZC_IF_SOME(recordValue, record) {
    zc::Vector<LockPackageRecord> packages;
    packages.add(zc::mv(recordValue));
    zc::Vector<identity::PackageDependencyEdgeKey> edges;
    auto graph = VerifiedLockGraph::from(zc::mv(packages), zc::mv(edges));
    if (graph.is<VerifiedLockGraph>()) {
      return LockfileCodec::write(graph.get<VerifiedLockGraph>());
    }
  }
  ZC_UNREACHABLE
}

// Builds a manifest that declares a registry dependency on the same registry
// identity as fixtureRegistryLockToml, so the trusted set includes it.
zc::String fixtureRegistryDepManifest() {
  auto trustHex = zc::encodeHex(fixtureDigest("registry trust"_zc).bytes());
  return zc::str(
      "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n\n"
      "[dependencies]\ncodec = { registry = \"https://example.com/index\", "
      "trust-domain-sha256 = \"",
      trustHex, "\", version = \"2.0.0\" }\n");
}

// Builds an in-memory workspace filesystem with the given manifest and lockfile.
zc::Own<zc::Filesystem> workspaceFilesystemWithLock(zc::StringPtr manifest,
                                                    zc::StringPtr lockToml) {
  auto root = zc::newInMemoryDirectory(zc::nullClock());
  root->openFile(zc::Path({"Zom.toml"_zc}), zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(manifest);
  root->openFile(zc::Path({"src"_zc, "lib.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("fn helper() {}\n"_zc);
  root->openFile(zc::Path({"Zom.lock"_zc}), zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(lockToml);
  return zc::heap<MemoryFilesystem>(zc::mv(root));
}

NormalizedWorkspace loadedWorkspaceFromManifest(zc::StringPtr manifest) {
  zc::Vector<identity::CanonicalRelativePath> files;
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(scalar<identity::CanonicalPathSegment>("lib.zom"_zc));
  files.add(identity::CanonicalRelativePath::from(zc::mv(segments)));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(inventoryValue, inventory) {
    zc::Vector<WorkspaceMemberInput> members;
    auto result = normalizeWorkspace(manifest, inventoryValue, zc::mv(members));
    ZC_REQUIRE(result.is<NormalizedWorkspace>());
    return zc::mv(result.get<NormalizedWorkspace>());
  }
  ZC_FAIL_REQUIRE("resolver fixture workspace normalization failed");
}

ZC_TEST("resolveWorkspacePackageInput returns installed inputs for a real single-root workspace") {
  auto filesystem = workspaceFilesystem();
  auto registry = targetRegistry();
  zc::MemoryResource resource;
  auto request = normalizedRequest(registry);
  auto workspace = loadedWorkspace();
  MemoryFreshSourceDirectoryFactory factory;
  auto result = resolveWorkspacePackageInput(
      *filesystem, zc::Path::parse(""_zc), resource, request, verifiedRequest(registry),
      verifiedSelectionOf(registry), verifiedSelectionOf(registry), workspace, factory);
  ZC_REQUIRE(result.is<InstalledPackageInputs>());
  ZC_EXPECT(result.get<InstalledPackageInputs>().crateGraph.roots().size() == 1);
}

ZC_TEST("resolveWorkspacePackageInput reports a typed registry mismatch for a foreign selection") {
  auto filesystem = workspaceFilesystem();
  auto registry = targetRegistry();
  auto foreignRegistry = targetRegistry("zom-alternate"_zc);
  zc::MemoryResource resource;
  auto request = normalizedRequest(registry);
  auto workspace = loadedWorkspace();
  MemoryFreshSourceDirectoryFactory factory;
  // The verified request came from `registry`, but the target selection is
  // verified against a different registry revision.
  auto result = resolveWorkspacePackageInput(
      *filesystem, zc::Path::parse(""_zc), resource, request, verifiedRequest(registry),
      verifiedSelectionOf(registry), verifiedSelectionOf(foreignRegistry), workspace, factory);
  ZC_REQUIRE(result.is<VerifyFailure>());
  ZC_EXPECT(result.get<VerifyFailure>().is<RegistryRevisionMismatch>());
}

ZC_TEST("resolveWorkspacePackageInput rejects a locked graph naming an untrusted registry") {
  auto lockToml = fixtureRegistryLockToml();
  auto filesystem = workspaceFilesystemWithLock(kManifest, lockToml);
  auto registry = targetRegistry();
  zc::MemoryResource resource;
  auto request = normalizedRequest(registry, 1);
  auto workspace = loadedWorkspaceFromManifest(kManifest);
  MemoryFreshSourceDirectoryFactory factory;
  // The workspace manifest declares no registry dependencies, so the trusted
  // set is empty and every registry identity in the lockfile is untrusted.
  auto result = resolveWorkspacePackageInput(*filesystem, zc::Path::parse(""_zc), resource, request,
                                             verifiedRequest(registry, PackageLockMode::LockedOnly),
                                             verifiedSelectionOf(registry),
                                             verifiedSelectionOf(registry), workspace, factory);
  ZC_REQUIRE(result.is<ResolveFailure>());
  ZC_EXPECT(result.get<ResolveFailure>().is<LockTrustDomainMismatch>());
}

ZC_TEST("resolveWorkspacePackageInput admits a locked graph naming a trusted registry") {
  auto lockToml = fixtureRegistryLockToml();
  auto manifest = fixtureRegistryDepManifest();
  auto filesystem = workspaceFilesystemWithLock(manifest, lockToml);
  auto registry = targetRegistry();
  zc::MemoryResource resource;
  auto request = normalizedRequest(registry, 1);
  auto workspace = loadedWorkspaceFromManifest(manifest);
  MemoryFreshSourceDirectoryFactory factory;
  // The manifest declares a registry dependency on the same registry identity
  // as the lockfile, so the trust check passes. Resolution then fails because
  // production admits only local-path packages, leaving the registry package
  // without a release.
  auto result = resolveWorkspacePackageInput(*filesystem, zc::Path::parse(""_zc), resource, request,
                                             verifiedRequest(registry, PackageLockMode::LockedOnly),
                                             verifiedSelectionOf(registry),
                                             verifiedSelectionOf(registry), workspace, factory);
  ZC_REQUIRE(result.is<ResolveFailure>());
  ZC_EXPECT(result.get<ResolveFailure>().is<LockedResolveFailed>());
}

}  // namespace
}  // namespace zomlang::compiler::driver::package
