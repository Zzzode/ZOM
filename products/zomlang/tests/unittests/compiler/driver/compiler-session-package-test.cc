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

#include <stdlib.h>
#include <unistd.h>

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/package/trusted-runtime-manifest.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::driver {
namespace {

template <typename Scalar>
Scalar scalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid package-session scalar fixture");
}

identity::SortedFeatureSet emptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("empty package-session feature set was rejected");
}

zc::String writeTempSource(zc::StringPtr sourceText) {
  zc::String file = zc::str("/tmp/zom-package-session-test.XXXXXX");
  const int descriptor = mkstemp(file.begin());
  ZC_REQUIRE(descriptor >= 0);
  size_t remaining = sourceText.size();
  const char* cursor = sourceText.cStr();
  while (remaining != 0) {
    const auto written = write(descriptor, cursor, remaining);
    ZC_REQUIRE(written > 0);
    cursor += written;
    remaining -= static_cast<size_t>(written);
  }
  close(descriptor);
  return file;
}

identity::CanonicalPackageSource source() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

identity::CanonicalPackageSource dependencySource(uint32_t parents, zc::StringPtr segment) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  if (segment.size() != 0) { segments.add(scalar<identity::CanonicalPathSegment>(segment)); }
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(parents, zc::mv(segments)));
}

identity::PackageBaseKey packageBase(zc::StringPtr name) {
  return identity::PackageBaseKey::from(source(), scalar<identity::PackageName>(name),
                                        scalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageBaseKey dependencyBase(zc::StringPtr name, uint32_t parents,
                                        zc::StringPtr segment) {
  return identity::PackageBaseKey::from(dependencySource(parents, segment),
                                        scalar<identity::PackageName>(name),
                                        scalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageKey packageKey(zc::StringPtr name) {
  return identity::PackageKey::from(source(), scalar<identity::PackageName>(name),
                                    scalar<identity::ResolvedVersion>("1.0.0"_zc), emptyFeatures());
}

identity::CanonicalTargetSpecificationKey projection(zc::StringPtr abiProfile = "zom-v1"_zc) {
  zc::Vector<identity::TargetFeatureName> features;
  auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
  ZC_REQUIRE(sorted != zc::none);
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
  ZC_FAIL_REQUIRE("package-session projection was rejected");
}

package::RegisteredTargetProfileName profileName() {
  auto result = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session profile name was rejected");
}

irgen::TargetRegistrySnapshot targetRegistry(zc::StringPtr abiProfile = "zom-v1"_zc) {
  zc::Vector<irgen::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = irgen::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), abiProfile,
      irgen::BackendPanicStrategy::Unwind, irgen::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<irgen::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = irgen::RegisteredTargetProfileRecord::from(
      profileName(), projection(abiProfile), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<irgen::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = irgen::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session target registry was rejected");
}

package::RegisteredTargetSelection selection(const irgen::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto selected = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, selected) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("package-session target selection failed");
}

irgen::VerifiedTargetSelection verifiedSelection(const irgen::TargetRegistrySnapshot& registry) {
  auto verified = registry.verify(selection(registry));
  ZC_REQUIRE(verified.is<irgen::VerifiedTargetSelection>());
  return zc::mv(verified.get<irgen::VerifiedTargetSelection>());
}

identity::CanonicalRelativePath path(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

package::VerifiedPackageCompilationRequest requestForKind(
    const irgen::TargetRegistrySnapshot& registry, zc::StringPtr packageName,
    identity::CrateTargetKind kind, zc::StringPtr targetName, bool requiresBuildScript = false) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      packageKey(packageName), kind, scalar<identity::TargetName>(targetName), 2026,
      requiresBuildScript, path("src"_zc, "main.zom"_zc)));
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), selection(registry), selection(registry), package::SelectedLanguageOptions{},
      package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session request was rejected");
}

package::VerifiedPackageCompilationRequest requestForPackage(
    const irgen::TargetRegistrySnapshot& registry, zc::StringPtr packageName,
    bool requiresBuildScript = false) {
  return requestForKind(registry, packageName, identity::CrateTargetKind::Binary, packageName,
                        requiresBuildScript);
}

package::VerifiedPackageCompilationRequest request(const irgen::TargetRegistrySnapshot& registry,
                                                   bool requiresBuildScript = false) {
  return requestForPackage(registry, "app"_zc, requiresBuildScript);
}

class MemoryFreshDirectory final : public package::FreshSourceDirectory {
public:
  MemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MemoryFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<package::FreshSourceDirectory> result = zc::heap<MemoryFreshDirectory>();
    return zc::mv(result);
  }
};

package::DigestVerifiedSourceSnapshot snapshot(zc::StringPtr output = zc::StringPtr()) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  if (output.size() == 0) {
    sourceDirectory
        ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                   zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
        ->writeAll("let main = 0;"_zc);
    sourceDirectory->openFile(zc::Path({"src"_zc, "lib.zom"_zc}), zc::WriteMode::CREATE)
        ->writeAll("let library = 0;"_zc);
  } else {
    sourceDirectory
        ->openFile(zc::Path({"generated"_zc, "out.zom"_zc}),
                   zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
        ->writeAll(output);
  }
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::ResolutionOutput resolution(zc::MemoryResource& resource, zc::StringPtr packageName,
                                     bool requiresBuildScript = false) {
  auto verifiedSource = snapshot();
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  if (requiresBuildScript) {
    files.add(path("tools"_zc, "build.zom"_zc));
    files.add(path("data"_zc, "input.txt"_zc));
  }
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  package::NormalizedManifest normalized = [&]() {
    ZC_IF_SOME(sourceInventory, inventory) {
      zc::Vector<identity::CanonicalPathSegment> documentSegments;
      documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
      auto manifestText = zc::str(
          "[package]\nname = \"", packageName, "\"\nversion = \"1.0.0\"\nedition = \"2026\"\n",
          requiresBuildScript
              ? "\n[build]\npath = \"tools/build.zom\"\ninputs = [\"data/input.txt\"]\noutputs = [\"generated/out.zom\"]\n"_zc
              : ""_zc);
      auto parsed = parser.parseWorkspaceManifest(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)), manifestText,
          sourceInventory);
      ZC_REQUIRE(parsed.is<package::NormalizedManifest>());
      return zc::mv(parsed.get<package::NormalizedManifest>());
    }
    ZC_UNREACHABLE
  }();
  auto record = package::LocalPackageRecord::from(packageBase(packageName), zc::mv(normalized),
                                                  verifiedSource);
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  ZC_IF_SOME(value, record) { releases.add(package::ResolverRelease::fromLocal(value)); }
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase(packageName), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

package::NormalizedManifest dependencyManifest(zc::StringPtr text) {
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(path("src"_zc, "lib.zom"_zc));
  files.add(path("src"_zc, "main.zom"_zc));
  files.add(path("tools"_zc, "build.zom"_zc));
  files.add(path("data"_zc, "input.txt"_zc));
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(sourceInventory, inventory) {
    zc::Vector<identity::CanonicalPathSegment> documentSegments;
    documentSegments.add(scalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
    auto parsed = parser.parseWorkspaceManifest(
        identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)), text,
        sourceInventory);
    ZC_REQUIRE(parsed.is<package::NormalizedManifest>());
    return zc::mv(parsed.get<package::NormalizedManifest>());
  }
  ZC_UNREACHABLE
}

package::ResolverRelease dependencyRelease(identity::PackageBaseKey&& base,
                                           zc::StringPtr manifestText) {
  auto sourceSnapshot = snapshot();
  auto record = package::LocalPackageRecord::from(zc::mv(base), dependencyManifest(manifestText),
                                                  sourceSnapshot);
  ZC_REQUIRE(record != zc::none);
  ZC_IF_SOME(value, record) { return package::ResolverRelease::fromLocal(value); }
  ZC_UNREACHABLE
}

package::ResolutionOutput dependencyResolution(zc::MemoryResource& resource) {
  constexpr zc::StringPtr appManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
math = { path = "../math", version = "^1.0.0" }
)toml"_zc;
  constexpr zc::StringPtr mathManifest = R"toml([package]
name = "math"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"
)toml"_zc;
  zc::Vector<package::ResolverRelease> releases;
  releases.add(dependencyRelease(packageBase("app"_zc), appManifest));
  releases.add(dependencyRelease(dependencyBase("math"_zc, 1, "math"_zc), mathManifest));
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase("app"_zc), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

package::ResolutionOutput developmentDependencyResolution(zc::MemoryResource& resource) {
  constexpr zc::StringPtr appManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[dev-dependencies]
math = { path = "../math", version = "^1.0.0" }
)toml"_zc;
  constexpr zc::StringPtr mathManifest = R"toml([package]
name = "math"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"
)toml"_zc;
  zc::Vector<package::ResolverRelease> releases;
  releases.add(dependencyRelease(packageBase("app"_zc), appManifest));
  releases.add(dependencyRelease(dependencyBase("math"_zc, 1, "math"_zc), mathManifest));
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase("app"_zc), emptyFeatures(), false, true));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

package::ResolutionOutput buildDependencyResolution(zc::MemoryResource& resource) {
  constexpr zc::StringPtr appManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build-dependencies]
tool = { path = "../tool", version = "^1.0.0" }
)toml"_zc;
  constexpr zc::StringPtr toolManifest = R"toml([package]
name = "tool"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"
)toml"_zc;
  zc::Vector<package::ResolverRelease> releases;
  releases.add(dependencyRelease(packageBase("app"_zc), appManifest));
  releases.add(dependencyRelease(dependencyBase("tool"_zc, 1, "tool"_zc), toolManifest));
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase("app"_zc), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

package::ResolutionOutput preparatoryDependencyResolution(zc::MemoryResource& resource) {
  constexpr zc::StringPtr appManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = ["data/input.txt"]
outputs = ["generated/out.zom"]

[build-dependencies]
tool = { path = "../tool", version = "^1.0.0" }
)toml"_zc;
  constexpr zc::StringPtr toolManifest = R"toml([package]
name = "tool"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"
)toml"_zc;
  zc::Vector<package::ResolverRelease> releases;
  releases.add(dependencyRelease(packageBase("app"_zc), appManifest));
  releases.add(dependencyRelease(dependencyBase("tool"_zc, 1, "tool"_zc), toolManifest));
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase("app"_zc), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

package::ResolutionOutput builtDependencyResolution(zc::MemoryResource& resource) {
  constexpr zc::StringPtr appManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[build]
path = "tools/build.zom"
inputs = ["data/input.txt"]
outputs = ["generated/out.zom"]

[build-dependencies]
tool = { path = "../tool", version = "^1.0.0" }
)toml"_zc;
  constexpr zc::StringPtr toolManifest = R"toml([package]
name = "tool"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[build]
path = "tools/build.zom"
inputs = ["data/input.txt"]
outputs = ["generated/out.zom"]
)toml"_zc;
  zc::Vector<package::ResolverRelease> releases;
  releases.add(dependencyRelease(packageBase("app"_zc), appManifest));
  releases.add(dependencyRelease(dependencyBase("tool"_zc, 1, "tool"_zc), toolManifest));
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(packageBase("app"_zc), emptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

zc::Vector<package::ResolvedPackageSourceSnapshot> dependencySnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc), snapshot()));
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(
      dependencyBase("math"_zc, 1, "math"_zc), snapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> buildDependencySnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc), snapshot()));
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(
      dependencyBase("tool"_zc, 1, "tool"_zc), snapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> resolvedSnapshots(zc::StringPtr packageName) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase(packageName), snapshot()));
  return snapshots;
}

VerifiedPackageSessionInput packageInput(zc::MemoryResource& resource,
                                         const irgen::TargetRegistrySnapshot& registry,
                                         bool requiresBuildScript = false) {
  auto input = VerifiedPackageSessionInput::from(
      request(registry, requiresBuildScript), verifiedSelection(registry),
      verifiedSelection(registry), resolution(resource, "app"_zc, requiresBuildScript),
      resolvedSnapshots("app"_zc));
  ZC_IF_SOME(value, input) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("valid atomic package-session input was rejected");
}

identity::Sha256Digest digest(zc::StringPtr text) {
  auto result = identity::sha256(text.asBytes());
  ZC_REQUIRE(result != zc::none);
  ZC_IF_SOME(value, result) { return value; }
  ZC_UNREACHABLE;
}

zc::Array<uint8_t> bytes(zc::StringPtr text) {
  zc::Vector<uint8_t> result(text.size());
  result.addAll(text.asBytes());
  return result.releaseAsArray();
}

package::TrustedBuildRuntimeKey trustedRuntime() {
  zc::Vector<zc::Array<uint8_t>> objects;
  objects.add(bytes("runtime-object"_zc));
  zc::Vector<package::TrustedRuntimeSymbolRecord> declaredSymbols;
  zc::Vector<package::TrustedRuntimeRelocationRecord> declaredRelocations;
  zc::Vector<package::TrustedRuntimeOperationRecord> declaredOperations;
  zc::Vector<package::TrustedRuntimeSymbolId> requiredOperations;
  uint32_t sectionCount[] = {1};
  auto declared = package::TrustedRuntimeManifestSet::verify(
      zc::arrayPtr(sectionCount), zc::mv(declaredSymbols), zc::mv(declaredRelocations),
      zc::mv(declaredOperations), requiredOperations);
  ZC_REQUIRE(declared.is<package::TrustedRuntimeManifestSet>());
  zc::Vector<package::TrustedRuntimeSymbolRecord> observedSymbols;
  zc::Vector<package::TrustedRuntimeRelocationRecord> observedRelocations;
  zc::Vector<package::TrustedRuntimeOperationRecord> observedOperations;
  zc::Vector<package::TrustedRuntimeSymbolId> observedRequiredOperations;
  auto observed = package::TrustedRuntimeManifestSet::verify(
      zc::arrayPtr(sectionCount), zc::mv(observedSymbols), zc::mv(observedRelocations),
      zc::mv(observedOperations), observedRequiredOperations);
  ZC_REQUIRE(observed.is<package::TrustedRuntimeManifestSet>());
  zc::Vector<identity::Sha256Digest> objectDigests;
  objectDigests.add(digest("runtime-object"_zc));
  auto evidence = package::TrustedRuntimeVerificationEvidence::verify(
      zc::mv(objectDigests), zc::mv(declared.get<package::TrustedRuntimeManifestSet>()),
      observed.get<package::TrustedRuntimeManifestSet>());
  ZC_REQUIRE(evidence.is<package::TrustedRuntimeVerificationEvidence>());
  auto result = package::TrustedBuildRuntimeKey::verifyEvidence(
      "zom-v1"_zc, "zom-v1"_zc, zc::mv(objects),
      zc::mv(evidence.get<package::TrustedRuntimeVerificationEvidence>()));
  ZC_REQUIRE(result.is<package::TrustedBuildRuntimeKey>());
  return zc::mv(result.get<package::TrustedBuildRuntimeKey>());
}

package::BuildScriptLimitKey buildScriptLimits() {
  auto result = package::BuildScriptLimitKey::verify(package::BuildScriptLimitKey::defaults());
  ZC_REQUIRE(result.is<package::BuildScriptLimitKey>());
  return zc::mv(result.get<package::BuildScriptLimitKey>());
}

package::BuildScriptExecutionKey executionKey(const package::BuildScriptPlanNode& node,
                                              const VerifiedPreparatoryCrateGraph& graph) {
  auto registry = targetRegistry();
  zc::Vector<identity::CrateKey> crates(graph.crates().size());
  for (const auto& value : graph.crates()) { crates.add(value.clone()); }
  zc::Vector<identity::CrateDependencyEdgeKey> edges(graph.edges().size());
  for (const auto& value : graph.edges()) { edges.add(value.clone()); }
  zc::Vector<identity::BuildScriptDigestEntry> inputs(node.contract().inputs().size());
  for (const auto& value : node.contract().inputs()) {
    inputs.add(identity::BuildScriptDigestEntry::from(value.clone(), digest("input"_zc)));
  }
  zc::Vector<identity::BuildScriptEnvironmentEntry> environment;
  auto result = package::BuildScriptExecutionKey::from(
      node.key().preparatory().clone(), graph.fingerprint().clone(),
      package::BuildScriptExecutableKey::from(selection(registry), digest("image"_zc)),
      trustedRuntime(), node.contract().clone(), graph.root().clone(), zc::mv(crates),
      zc::mv(edges), zc::mv(inputs), zc::mv(environment), buildScriptLimits());
  ZC_REQUIRE(result != zc::none);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE;
}

class RecordingPlanExecutor final : public package::BuildScriptPlanExecutor {
public:
  package::BuildScriptExecutionResult execute(
      const package::BuildScriptPlanNode& node, const VerifiedPreparatoryCrateGraph& crateGraph,
      zc::ArrayPtr<const package::VerifiedBuildScriptResult> completedResults) override {
    observedCrateCounts.add(crateGraph.crates().size());
    observedCompletedCounts.add(completedResults.size());
    auto key = executionKey(node, crateGraph);
    auto generatedSnapshot = snapshot("generated"_zc);
    zc::Vector<identity::BuildScriptDigestEntry> sourceDigests(key.inputDigests().size());
    for (const auto& value : key.inputDigests()) { sourceDigests.add(value.clone()); }
    zc::Vector<identity::BuildScriptEnvironmentEntry> declaredEnvironment(
        key.declaredEnvironment().size());
    for (const auto& value : key.declaredEnvironment()) { declaredEnvironment.add(value.clone()); }
    zc::Vector<identity::BuildScriptDigestEntry> generatedSources;
    generatedSources.add(identity::BuildScriptDigestEntry::from(
        generatedSnapshot.record().files()[0].path().clone(),
        generatedSnapshot.record().files()[0].contentDigest()));
    zc::Vector<identity::BuildScriptEnvironmentEntry> exportedEnvironment;
    auto output = identity::BuildScriptOutputRecord::from(
        node.key().preparatory().clone(), zc::mv(sourceDigests), zc::mv(declaredEnvironment),
        zc::mv(generatedSources), zc::mv(exportedEnvironment));
    ZC_REQUIRE(output != zc::none);
    zc::Vector<package::BuildScriptEnvironmentValue> responseEnvironment;
    ZC_IF_SOME(value, output) {
      auto publication = package::VerifiedBuildScriptResult::publishDeterministicExecution(
          key,
          package::VerifiedBuildScriptRun::from(
              zc::mv(generatedSnapshot),
              package::BuildScriptResponse::success(zc::mv(responseEnvironment))),
          zc::mv(value));
      if (publication.is<package::BuildResultIntegrityViolation>()) {
        return package::BuildScriptIssue::BuildResultIntegrityViolation;
      }
      return zc::mv(publication.get<package::VerifiedBuildScriptResult>());
    }
    ZC_UNREACHABLE;
  }

  zc::Vector<size_t> observedCompletedCounts;
  zc::Vector<size_t> observedCrateCounts;
};

zc::Own<CompilerSession> preparedSession(const basic::LangOptions& languageOptions,
                                         const basic::CompilerOptions& compilerOptions,
                                         bool requiresBuildScript = false) {
  identity::SemanticContextFactory contextFactory;
  auto session = zc::heap<CompilerSession>(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  ZC_REQUIRE(session->installVerifiedPackageInput(
      packageInput(session->getPackageResolutionMemoryResource(), registry, requiresBuildScript)));
  return session;
}

}  // namespace

ZC_TEST("Verified package input rejects a request root outside the resolved graph") {
  zc::MemoryResource resource;
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(resource, "other"_zc), resolvedSnapshots("other"_zc));
  ZC_EXPECT(input == zc::none);
}

ZC_TEST("Verified package input rejects target selections from another registry revision") {
  zc::MemoryResource resource;
  auto registry = targetRegistry();
  auto otherRegistry = targetRegistry("zom-v2"_zc);
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(otherRegistry),
      resolution(resource, "app"_zc), resolvedSnapshots("app"_zc));
  ZC_EXPECT(input == zc::none);
}

ZC_TEST("Verified package input rejects snapshots outside the resolved graph") {
  zc::MemoryResource resource;
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(resource, "app"_zc), resolvedSnapshots("other"_zc));
  ZC_EXPECT(input == zc::none);
}

ZC_TEST("CompilerSession installs one atomic package input exactly once") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();

  ZC_EXPECT(session.installVerifiedPackageInput(
      packageInput(session.getPackageResolutionMemoryResource(), registry)));
  ZC_EXPECT(!session.installVerifiedPackageInput(
      packageInput(session.getPackageResolutionMemoryResource(), registry)));
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(!registries.packages().isFrozen());
    ZC_EXPECT(registries.packages().size() == 0);
  }
}

ZC_TEST("CompilerSession rejects a moved-from atomic package input") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto original = packageInput(session.getPackageResolutionMemoryResource(), registry);
  auto retained = zc::mv(original);

  ZC_EXPECT(!session.installVerifiedPackageInput(zc::mv(original)));
  ZC_EXPECT(session.installVerifiedPackageInput(zc::mv(retained)));
}

ZC_TEST("CompilerSession executes and freezes one exact build-plan result map") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto withoutBuildScript = preparedSession(languageOptions, compilerOptions);
  ZC_REQUIRE(withoutBuildScript->getFinalizedCompilationRoots().size() == 1);
  const auto withoutBuildOutputKey =
      withoutBuildScript->getFinalizedCompilationRoots()[0].crateKey().encode();
  auto session = preparedSession(languageOptions, compilerOptions, true);
  ZC_EXPECT(session->getFinalizedCompilationRoots().size() == 0);
  ZC_REQUIRE(session->getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session->getIdentityRegistries()) {
    ZC_EXPECT(!registries.packages().isFrozen());
    ZC_EXPECT(registries.packages().size() == 0);
    ZC_EXPECT(registries.crates().size() == 0);
  }
  RecordingPlanExecutor executor;
  ZC_EXPECT(session->executeBuildScripts(executor) == zc::none);
  ZC_REQUIRE(session->getFinalizedCompilationRoots().size() == 1);
  ZC_EXPECT(session->getFinalizedCompilationRoots()[0].crateKey().encode().size() != 0);
  ZC_EXPECT(session->getFinalizedCompilationRoots()[0].crateKey().encode().asPtr() !=
            withoutBuildOutputKey.asPtr());
  ZC_REQUIRE(session->getBuildScriptPlan() != zc::none);
  ZC_REQUIRE(session->getBuildScriptResults() != zc::none);
  ZC_IF_SOME(results, session->getBuildScriptResults()) {
    ZC_EXPECT(results.results().size() == 1);
    ZC_EXPECT(results.planKeys().size() == 1);
  }
  ZC_REQUIRE(executor.observedCompletedCounts.size() == 1);
  ZC_EXPECT(executor.observedCompletedCounts[0] == 0);
  ZC_REQUIRE(executor.observedCrateCounts.size() == 1);
  ZC_EXPECT(executor.observedCrateCounts[0] == 1);
  ZC_REQUIRE(session->getVerifiedPreparatoryCrateGraphs().size() == 1);
  ZC_EXPECT(session->getVerifiedPreparatoryCrateGraphs()[0].root().targetKind() ==
            identity::CrateTargetKind::BuildScript);
  ZC_EXPECT(session->executeBuildScripts(executor) ==
            package::BuildScriptIssue::BuildResultIntegrityViolation);
}

ZC_TEST("CompilerSession expands one isolated preparatory host dependency closure") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry, true), verifiedSelection(registry), verifiedSelection(registry),
      preparatoryDependencyResolution(session.getPackageResolutionMemoryResource()),
      buildDependencySnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  RecordingPlanExecutor executor;
  ZC_REQUIRE(session.executeBuildScripts(executor) == zc::none);
  ZC_REQUIRE(executor.observedCrateCounts.size() == 1);
  ZC_EXPECT(executor.observedCrateCounts[0] == 2);
  ZC_REQUIRE(session.getVerifiedPreparatoryCrateGraphs().size() == 1);
  const auto& graph = session.getVerifiedPreparatoryCrateGraphs()[0];
  ZC_EXPECT(graph.packages().size() == 2);
  ZC_EXPECT(graph.packageEdges().size() == 1);
  ZC_EXPECT(graph.crates().size() == 2);
  ZC_EXPECT(graph.edges().size() == 1);
  ZC_EXPECT(graph.edges()[0].packageEdge().domain() == identity::DependencyDomain::Build);
  ZC_EXPECT(graph.edges()[0].consumer().targetKind() == identity::CrateTargetKind::BuildScript);
  ZC_EXPECT(graph.edges()[0].provider().targetKind() == identity::CrateTargetKind::Library);
  ZC_EXPECT(graph.edges()[0].provider().package().name() == "tool"_zc);
  ZC_REQUIRE(session.getVerifiedCrateGraph() != zc::none);
  ZC_IF_SOME(finalGraph, session.getVerifiedCrateGraph()) {
    ZC_EXPECT(finalGraph.crates().size() == 1);
    ZC_EXPECT(finalGraph.packageEdges().size() == 0);
    ZC_EXPECT(finalGraph.edges().size() == 0);
  }
}

ZC_TEST("CompilerSession carries completed provider build identity into a host closure") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry, true), verifiedSelection(registry), verifiedSelection(registry),
      builtDependencyResolution(session.getPackageResolutionMemoryResource()),
      buildDependencySnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  RecordingPlanExecutor executor;
  ZC_REQUIRE(session.executeBuildScripts(executor) == zc::none);
  ZC_REQUIRE(executor.observedCompletedCounts.size() == 2);
  ZC_EXPECT(executor.observedCompletedCounts[0] == 0);
  ZC_EXPECT(executor.observedCompletedCounts[1] == 1);
  ZC_REQUIRE(executor.observedCrateCounts.size() == 2);
  ZC_EXPECT(executor.observedCrateCounts[0] == 1);
  ZC_EXPECT(executor.observedCrateCounts[1] == 2);
  ZC_REQUIRE(session.getVerifiedPreparatoryCrateGraphs().size() == 2);
  bool foundAppClosure = false;
  for (const auto& graph : session.getVerifiedPreparatoryCrateGraphs()) {
    if (graph.root().package().name() == "app"_zc) {
      foundAppClosure = true;
      ZC_EXPECT(graph.crates().size() == 2);
      ZC_EXPECT(graph.edges().size() == 1);
      ZC_EXPECT(graph.edges()[0].provider().package().name() == "tool"_zc);
    }
  }
  ZC_EXPECT(foundAppClosure);
}

ZC_TEST("CompilerSession rejects identity freeze before required build outputs exist") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto session = preparedSession(languageOptions, compilerOptions, true);

  ZC_EXPECT(session->getFinalizedCompilationRoots().size() == 0);
  ZC_EXPECT(!session->parseSources());
  ZC_EXPECT(session->getDiagnosticEngine().hasErrors());
}

ZC_TEST("CompilerSession does not execute an unselected build-only dependency") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      buildDependencyResolution(session.getPackageResolutionMemoryResource()),
      buildDependencySnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  RecordingPlanExecutor executor;
  ZC_EXPECT(session.executeBuildScripts(executor) == zc::none);
  ZC_EXPECT(executor.observedCompletedCounts.size() == 0);
  ZC_REQUIRE(session.getBuildScriptResults() != zc::none);
  ZC_IF_SOME(results, session.getBuildScriptResults()) {
    ZC_EXPECT(results.planKeys().size() == 0);
    ZC_EXPECT(results.results().size() == 0);
  }
}

ZC_TEST("CompilerSession freezes canonical crate and source identities before parsing") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto session = preparedSession(languageOptions, compilerOptions);
  auto sourceFile =
      writeTempSource("interface Runnable { }\nclass App { }\nimpl Runnable for App { }\n"_zc);
  ZC_DEFER(unlink(sourceFile.cStr()));

  ZC_REQUIRE(session->getPackageCompilationRequest() != zc::none);
  auto finalizedRoots = session->getFinalizedCompilationRoots();
  ZC_REQUIRE(finalizedRoots.size() == 1);
  {
    ZC_EXPECT(session->addPackageSourceFile(sourceFile, "src/main.zom"_zc, finalizedRoots[0]) !=
              zc::none);
  }
  ZC_EXPECT(session->parseSources());

  ZC_REQUIRE(session->getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session->getIdentityRegistries()) {
    ZC_EXPECT(registries.crates().isFrozen());
    ZC_EXPECT(registries.crates().size() == 1);
    ZC_EXPECT(registries.sourceFiles().isFrozen());
    ZC_EXPECT(registries.sourceFiles().size() == 1);
    ZC_EXPECT(registries.sourceSnapshots().size() == 1);
    ZC_EXPECT(registries.modules().isFrozen());
    ZC_EXPECT(registries.modules().size() == 1);
    ZC_EXPECT(registries.definitions().isFrozen());
    ZC_EXPECT(registries.definitions().size() == 2);
    ZC_EXPECT(registries.impls().isFrozen());
    ZC_EXPECT(registries.impls().size() == 1);
  }
}

ZC_TEST("CompilerSession expands final dependency crates and publishes semantic fingerprint") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      dependencyResolution(session.getPackageResolutionMemoryResource()), dependencySnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  ZC_REQUIRE(session.getVerifiedCrateGraph() != zc::none);
  ZC_IF_SOME(graph, session.getVerifiedCrateGraph()) {
    ZC_REQUIRE(graph.roots().size() == 1);
    ZC_REQUIRE(graph.crates().size() == 2);
    ZC_REQUIRE(graph.edges().size() == 1);
    ZC_EXPECT(graph.edges()[0].consumer().targetKind() == identity::CrateTargetKind::Binary);
    ZC_EXPECT(graph.edges()[0].provider().targetKind() == identity::CrateTargetKind::Library);
    ZC_EXPECT(graph.edges()[0].provider().package().name() == "math"_zc);
  }

  auto sourceFile = writeTempSource("module app;\nfun main() {}\n"_zc);
  ZC_DEFER(unlink(sourceFile.cStr()));
  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addPackageSourceFile(sourceFile, "src/main.zom"_zc, roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.getSemanticContextFingerprint() != zc::none);
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.packages().size() == 2);
    ZC_EXPECT(registries.crates().size() == 2);
    ZC_IF_SOME(crates, session.getVerifiedCrateGraph()) {
      auto expected = identity::SemanticContextFingerprint::compute(
          registries, crates.packageEdges(), crates.edges());
      ZC_REQUIRE(expected != zc::none);
      ZC_IF_SOME(expectedValue, expected) {
        ZC_IF_SOME(actual, session.getSemanticContextFingerprint()) {
          ZC_EXPECT(actual.digest() == expectedValue.digest());
        }
      }
    }
  }
}

ZC_TEST("CompilerSession applies development dependencies only to development target kinds") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto registry = targetRegistry();
  {
    identity::SemanticContextFactory contextFactory;
    CompilerSession session(contextFactory, languageOptions, compilerOptions);
    auto input = VerifiedPackageSessionInput::from(
        request(registry), verifiedSelection(registry), verifiedSelection(registry),
        developmentDependencyResolution(session.getPackageResolutionMemoryResource()),
        dependencySnapshots());
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    ZC_REQUIRE(session.getVerifiedCrateGraph() != zc::none);
    ZC_IF_SOME(graph, session.getVerifiedCrateGraph()) {
      ZC_EXPECT(graph.crates().size() == 1);
      ZC_EXPECT(graph.edges().size() == 0);
    }
  }
  {
    identity::SemanticContextFactory contextFactory;
    CompilerSession session(contextFactory, languageOptions, compilerOptions);
    auto input = VerifiedPackageSessionInput::from(
        requestForKind(registry, "app"_zc, identity::CrateTargetKind::Test, "integration"_zc),
        verifiedSelection(registry), verifiedSelection(registry),
        developmentDependencyResolution(session.getPackageResolutionMemoryResource()),
        dependencySnapshots());
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    ZC_REQUIRE(session.getVerifiedCrateGraph() != zc::none);
    ZC_IF_SOME(graph, session.getVerifiedCrateGraph()) {
      ZC_EXPECT(graph.crates().size() == 2);
      ZC_REQUIRE(graph.edges().size() == 1);
      ZC_EXPECT(graph.edges()[0].packageEdge().domain() == identity::DependencyDomain::Development);
      ZC_EXPECT(graph.edges()[0].consumer().targetKind() == identity::CrateTargetKind::Test);
      ZC_EXPECT(graph.edges()[0].provider().targetKind() == identity::CrateTargetKind::Library);
    }
  }
}

ZC_TEST("CompilerSession excludes build-only dependencies from the final semantic context") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      buildDependencyResolution(session.getPackageResolutionMemoryResource()),
      buildDependencySnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  ZC_REQUIRE(session.getVerifiedCrateGraph() != zc::none);
  ZC_IF_SOME(graph, session.getVerifiedCrateGraph()) {
    ZC_EXPECT(graph.crates().size() == 1);
    ZC_EXPECT(graph.packageEdges().size() == 0);
    ZC_EXPECT(graph.edges().size() == 0);
  }

  auto sourceFile = writeTempSource("module app;\nfun main() {}\n"_zc);
  ZC_DEFER(unlink(sourceFile.cStr()));
  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addPackageSourceFile(sourceFile, "src/main.zom"_zc, roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.packages().size() == 1);
    ZC_EXPECT(registries.crates().size() == 1);
    ZC_EXPECT(registries.packages().find(packageKey("tool"_zc)) == zc::none);
  }
}

}  // namespace zomlang::compiler::driver
