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

#include "zc/core/time.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/checker/body-checker.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/coherence-builder.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/package/trusted-runtime-manifest.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang::compiler::driver {
namespace {

struct CapturedDiagnostics final {
  zc::Vector<diagnostics::DiagID> ids;
  size_t unmanagedPrimaryLocations = 0;
};

class CaptureDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  explicit CaptureDiagnosticConsumer(CapturedDiagnostics& capture) noexcept : capture(capture) {}

  void handleDiagnostic(const source::SourceManager& sourceManager,
                        const diagnostics::Diagnostic& diagnostic) override {
    capture.ids.add(diagnostic.getId());
    if (diagnostic.getLoc().isValid() &&
        sourceManager.findBufferContainingLoc(diagnostic.getLoc()) == zc::none) {
      ++capture.unmanagedPrimaryLocations;
    }
  }

private:
  CapturedDiagnostics& capture;
};

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

ir::TargetRegistrySnapshot targetRegistry(zc::StringPtr abiProfile = "zom-v1"_zc) {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), abiProfile,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = ir::RegisteredTargetProfileRecord::from(
      profileName(), projection(abiProfile), zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registry = ir::TargetRegistrySnapshot::from(profileName(), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("package-session target registry was rejected");
}

package::RegisteredTargetSelection selection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto selected = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, selected) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("package-session target selection failed");
}

ir::VerifiedTargetSelection verifiedSelection(const ir::TargetRegistrySnapshot& registry) {
  auto verified = registry.verify(selection(registry));
  ZC_REQUIRE(verified.is<ir::VerifiedTargetSelection>());
  return zc::mv(verified.get<ir::VerifiedTargetSelection>());
}

identity::CanonicalRelativePath path(zc::StringPtr first, zc::StringPtr second) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(scalar<identity::CanonicalPathSegment>(first));
  segments.add(scalar<identity::CanonicalPathSegment>(second));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

package::VerifiedPackageCompilationRequest requestForKind(
    const ir::TargetRegistrySnapshot& registry, zc::StringPtr packageName,
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
    const ir::TargetRegistrySnapshot& registry, zc::StringPtr packageName,
    bool requiresBuildScript = false) {
  return requestForKind(registry, packageName, identity::CrateTargetKind::Binary, packageName,
                        requiresBuildScript);
}

package::VerifiedPackageCompilationRequest request(const ir::TargetRegistrySnapshot& registry,
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
        ->openFile(zc::Path({"generated"_zc, "output_mod.zom"_zc}),
                   zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
        ->writeAll(output);
  }
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot packageSourceSnapshot(zc::StringPtr mainSource) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(mainSource);
  sourceDirectory->openFile(zc::Path({"src"_zc, "lib.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("let library = 0;"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot moduleSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import app::child;\nlet main = 0;"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "child.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("module child;\nlet value = 1;"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot atomicCheckerFailureModuleSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import app::child;\nlet value = 0 + 1;"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "child.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("module child;"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot nestedModuleSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import app::area::child;\nlet main = 0;"_zc);
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "area"_zc, "child.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("module child;\nlet value = 1;"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot mismatchedModuleSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("module wrong;\nlet main = 0;"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot missingModuleSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import very::deeply::nested::module_part::path::name as mod;"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot generatedImportSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import app::generated::output_mod;\nlet main = 0;"_zc);
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
              ? "\n[build]\npath = \"tools/build.zom\"\ninputs = [\"data/input.txt\"]\noutputs = [\"generated/output_mod.zom\"]\n"_zc
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
outputs = ["generated/output_mod.zom"]

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
outputs = ["generated/output_mod.zom"]

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
outputs = ["generated/output_mod.zom"]
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

zc::Vector<package::ResolvedPackageSourceSnapshot> resolvedSourceSnapshots(
    zc::StringPtr packageName, zc::StringPtr mainSource) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase(packageName),
                                                             packageSourceSnapshot(mainSource)));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> moduleSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc), moduleSnapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> atomicCheckerFailureModuleSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc),
                                                             atomicCheckerFailureModuleSnapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> nestedModuleSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc), nestedModuleSnapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> mismatchedModuleSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc),
                                                             mismatchedModuleSnapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> missingModuleSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc), missingModuleSnapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> generatedImportSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc),
                                                             generatedImportSnapshot()));
  return snapshots;
}

VerifiedPackageSessionInput packageInput(zc::MemoryResource& resource,
                                         const ir::TargetRegistrySnapshot& registry,
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
  explicit RecordingPlanExecutor(zc::StringPtr generatedSource = "generated"_zc)
      : generatedSource(generatedSource) {}

  package::BuildScriptExecutionResult execute(
      const package::BuildScriptPlanNode& node, const VerifiedPreparatoryCrateGraph& crateGraph,
      zc::ArrayPtr<const package::VerifiedBuildScriptResult> completedResults) override {
    observedCrateCounts.add(crateGraph.crates().size());
    observedCompletedCounts.add(completedResults.size());
    auto key = executionKey(node, crateGraph);
    auto generatedSnapshot = snapshot(generatedSource);
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
        node.key().preparatory().producerKey(), zc::mv(sourceDigests), zc::mv(declaredEnvironment),
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

private:
  zc::StringPtr generatedSource;
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
  ZC_REQUIRE(session->getFinalizedCompilationRoots().size() == 1);
  const auto beforeExecutionKey = session->getFinalizedCompilationRoots()[0].crateKey().encode();
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
  ZC_EXPECT(session->getFinalizedCompilationRoots()[0].crateKey().encode().asPtr() ==
            beforeExecutionKey.asPtr());
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

ZC_TEST("CompilerSession crate identity is independent of build-script output contents") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto first = preparedSession(languageOptions, compilerOptions, true);
  auto second = preparedSession(languageOptions, compilerOptions, true);
  ZC_REQUIRE(first->getFinalizedCompilationRoots().size() == 1);
  ZC_REQUIRE(second->getFinalizedCompilationRoots().size() == 1);
  const auto firstBefore = first->getFinalizedCompilationRoots()[0].crateKey().encode();
  const auto secondBefore = second->getFinalizedCompilationRoots()[0].crateKey().encode();
  ZC_EXPECT(firstBefore.asPtr() == secondBefore.asPtr());

  RecordingPlanExecutor firstExecutor("generated-one"_zc);
  RecordingPlanExecutor secondExecutor("generated-two"_zc);
  ZC_REQUIRE(first->executeBuildScripts(firstExecutor) == zc::none);
  ZC_REQUIRE(second->executeBuildScripts(secondExecutor) == zc::none);
  ZC_EXPECT(first->getFinalizedCompilationRoots()[0].crateKey().encode().asPtr() ==
            firstBefore.asPtr());
  ZC_EXPECT(second->getFinalizedCompilationRoots()[0].crateKey().encode().asPtr() ==
            secondBefore.asPtr());
  ZC_EXPECT(first->getFinalizedCompilationRoots()[0].crateKey().encode().asPtr() ==
            second->getFinalizedCompilationRoots()[0].crateKey().encode().asPtr());

  ZC_REQUIRE(first->getBuildScriptResults() != zc::none);
  ZC_REQUIRE(second->getBuildScriptResults() != zc::none);
  ZC_IF_SOME(firstResults, first->getBuildScriptResults()) {
    ZC_IF_SOME(secondResults, second->getBuildScriptResults()) {
      ZC_REQUIRE(firstResults.results().size() == 1);
      ZC_REQUIRE(secondResults.results().size() == 1);
      ZC_EXPECT(firstResults.results()[0].output().artifactFingerprint().digest() !=
                secondResults.results()[0].output().artifactFingerprint().digest());
    }
  }
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

ZC_TEST("CompilerSession fixes crate identity before required build outputs exist") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  auto session = preparedSession(languageOptions, compilerOptions, true);

  ZC_EXPECT(session->getFinalizedCompilationRoots().size() == 1);
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

  ZC_REQUIRE(session->getPackageCompilationRequest() != zc::none);
  auto finalizedRoots = session->getFinalizedCompilationRoots();
  ZC_REQUIRE(finalizedRoots.size() == 1);
  auto admittedBuffer = session->addVerifiedPackageRoot(finalizedRoots[0]);
  ZC_REQUIRE(admittedBuffer != zc::none);
  ZC_IF_SOME(buffer, admittedBuffer) {
    ZC_EXPECT(session->getSourceManager().getEntireTextForBuffer(buffer) == "let main = 0;"_zcb);
  }
  ZC_EXPECT(session->parseSources());
  ZC_REQUIRE(session->getParsedModules().size() == 1);
  ZC_EXPECT(session->getParsedModules()[0].parsedModule().receipt().digest().bytes().size() == 32);
  ZC_REQUIRE(session->getVerifiedModuleGraph() != zc::none);
  ZC_IF_SOME(graph, session->getVerifiedModuleGraph()) {
    ZC_EXPECT(graph.modules().size() == 1);
    ZC_EXPECT(graph.revision().digest().bytes().size() == 32);
  }

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
    ZC_EXPECT(registries.definitions().size() == 1);
    ZC_EXPECT(registries.impls().isFrozen());
    ZC_EXPECT(registries.impls().size() == 0);
  }
}

ZC_TEST("CompilerSession admits contextual callable names into the frozen binding inventory") {
  constexpr zc::StringPtr sourceText = R"zom(interface LargeIface {
    type Item;
    type Iter;
    type Error : Error;
    fun size() -> u64;
    fun get(i: u64) -> Item;
    fun set(i: u64, v: Item) -> unit;
    fun contains(v: Item) -> bool;
    fun find(pred: (Item) -> bool) -> i64;
    fun map<U>(f: (Item) -> U) -> [U];
    fun filter(pred: (Item) -> bool) -> [Item];
    fun reduce<A>(acc: A, f: (A, Item) -> A) -> A;
    get isEmpty() -> bool;
    get length() -> u64;
    set length(v: u64) -> unit;
}
)zom"_zc;

  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, sourceText));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(!session.getDiagnosticEngine().hasErrors());
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.definitions().isFrozen());
    ZC_EXPECT(registries.definitions().size() == 15);
    ZC_EXPECT(registries.genericParameters().isFrozen());
    ZC_EXPECT(registries.genericParameters().size() == 2);
    ZC_EXPECT(registries.callableParameters().isFrozen());
    ZC_EXPECT(registries.callableParameters().size() == 10);
  }
}

ZC_TEST("CompilerSession discovers imported module sources before source identity freeze") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc), moduleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(session.getParsedModules().size() == 2);
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.sourceFiles().size() == 2);
    ZC_EXPECT(registries.modules().size() == 2);
  }
  ZC_REQUIRE(session.getVerifiedModuleGraph() != zc::none);
  ZC_IF_SOME(graph, session.getVerifiedModuleGraph()) {
    ZC_EXPECT(graph.modules().size() == 2);
    ZC_EXPECT(graph.edges().size() == 1);
  }
}

ZC_TEST("CompilerSession discovers generated modules from frozen build-script outputs") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry, true), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc, true),
      generatedImportSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  RecordingPlanExecutor executor("module output_mod;\nlet generated_value = 1;"_zc);
  ZC_REQUIRE(session.executeBuildScripts(executor) == zc::none);
  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(session.getParsedModules().size() == 2);
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.sourceFiles().size() == 2);
    ZC_EXPECT(registries.sourceSnapshots().size() == 2);
    ZC_EXPECT(registries.modules().size() == 2);
  }
  ZC_REQUIRE(session.getVerifiedModuleGraph() != zc::none);
  ZC_IF_SOME(graph, session.getVerifiedModuleGraph()) {
    ZC_EXPECT(graph.modules().size() == 2);
    ZC_EXPECT(graph.edges().size() == 1);
  }
}

ZC_TEST("CompilerSession binds dependency modules before their requesters") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc), moduleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.getVerifiedModuleGraph() != zc::none);
  ZC_IF_SOME(graph, session.getVerifiedModuleGraph()) {
    ZC_REQUIRE(graph.edges().size() == 1);
    ZC_REQUIRE(session.bindSources());
    const auto bindings = session.getVerifiedBindingOutputs();
    ZC_REQUIRE(bindings.size() == 2);
    ZC_EXPECT(bindings[0].metadata.module() == graph.edges()[0].target());
    ZC_EXPECT(bindings[1].metadata.module() == graph.edges()[0].request().requester());
    ZC_EXPECT(bindings[0].surface.sourceModule() == bindings[0].metadata.module());
    ZC_EXPECT(bindings[1].surface.sourceModule() == bindings[1].metadata.module());
  }
}

ZC_TEST("CompilerSession stages the complete source snapshot root with module topology") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc), moduleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_REQUIRE(registries.sourceSnapshots().size() == 2);
    for (const auto& snapshot : registries.sourceSnapshots()) {
      auto recomputed = identity::sha256(snapshot.bytes());
      ZC_REQUIRE(recomputed != zc::none);
      ZC_EXPECT(ZC_REQUIRE_NONNULL(recomputed) == snapshot.contentDigest());
    }
  }
  ZC_REQUIRE(session.bindSources());
  ZC_EXPECT(session.getVerifiedBindingOutputs().size() == 2);
  ZC_EXPECT(!session.getDiagnosticEngine().hasErrors());
}

ZC_TEST("CompilerSession publishes the complete canonical Checker rail for an empty module") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, ""_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_EXPECT(!session.getDiagnosticEngine().hasErrors());
  ZC_EXPECT(session.getCheckerInvariantFailures().size() == 0);
  ZC_EXPECT(session.getVerifiedSignatureFacts().size() == 1);
  ZC_EXPECT(session.getImportedSignatureViews().size() == 1);
  ZC_EXPECT(session.getVerifiedModuleInterfaces().size() == 1);
  ZC_EXPECT(session.getFrozenCoherenceView() != zc::none);
  ZC_EXPECT(session.getCheckedFactsRepository() != zc::none);
  ZC_EXPECT(session.getCheckedEvidenceLeases().size() == 1);
  ZC_REQUIRE(session.getVerifiedDispatchFacts().size() == 1);
  ZC_EXPECT(session.getVerifiedDispatchFacts()[0].facts().size() == 0);
  ZC_REQUIRE(session.getBorrowEvidenceRepository() != zc::none);
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  const auto& hirModule = session.getVerifiedHirModules()[0];
  ZC_EXPECT(hirModule.declarations().size() == 0);
  ZC_EXPECT(hirModule.borrowEvidenceLease().key().revision.digest() ==
            hirModule.borrowEvidenceRevision().digest());
  ZC_IF_SOME(repository, session.getBorrowEvidenceRepository()) {
    const auto evidence = repository.lookup(hirModule.borrowEvidenceLease());
    ZC_REQUIRE(evidence.isResolved());
    ZC_EXPECT(evidence.evidence().revision().digest() ==
              hirModule.borrowEvidenceRevision().digest());
  }
  ZC_REQUIRE(session.getVerifiedBuiltMirModules().size() == 1);
  const auto& builtMir = session.getVerifiedBuiltMirModules()[0];
  ZC_EXPECT(builtMir.module() == hirModule.module());
  ZC_EXPECT(builtMir.functions().size() == 0);
  ZC_EXPECT(builtMir.canonicalFunctionRecords().size() == 0);
  ZC_EXPECT(builtMir.borrowEvidenceRevision().digest() ==
            hirModule.borrowEvidenceRevision().digest());
  ZC_IF_SOME(repository, session.getBorrowEvidenceRepository()) {
    const auto evidence = repository.lookup(builtMir.borrowEvidenceLease());
    ZC_REQUIRE(evidence.isResolved());
    ZC_EXPECT(evidence.evidence().revision().digest() ==
              builtMir.borrowEvidenceRevision().digest());
  }
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("MarkerProofEngine resolves explicit builtin and structural evidence") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots(
          "app"_zc,
          "interface Structural {}\ninterface Explicit {}\ninterface Negative {}\n"
          "unsafe impl Explicit for i32;\nimpl !Negative for bool;\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
  const auto boundModules = session.getVerifiedBoundModules();
  ZC_REQUIRE(boundModules.size() == 1);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_IF_SOME(fingerprint, session.getSemanticContextFingerprint()) {
      ZC_IF_SOME(constSemanticTypes, session.getSemanticTypeStore()) {
        auto& semanticTypes = const_cast<type::SemanticTypeStore&>(constSemanticTypes);
        zc::Vector<checker::signature::MarkerShapeModuleInput> shapeInputs;
        shapeInputs.add(checker::signature::MarkerShapeModuleInput{boundModules[0]});
        auto shapeResult = checker::signature::MarkerShapeInventoryBuilder::build(
            session.getSemanticContextBrand(), fingerprint, shapeInputs.asPtr(), registries);
        ZC_REQUIRE(shapeResult.is<checker::signature::VerifiedMarkerShapeInventory>());
        auto shapes = zc::mv(shapeResult).get<checker::signature::VerifiedMarkerShapeInventory>();

        identity::DefId structuralMarker;
        for (const auto& signatureDefinition : boundModules[0].definitions().definitions()) {
          auto record = registries.definitions().lookupRecord(signatureDefinition.definition);
          ZC_IF_SOME(value, record) {
            if (value.name() == "Structural"_zc) {
              structuralMarker = signatureDefinition.definition;
            }
          }
        }
        ZC_REQUIRE(structuralMarker.isValid());
        auto structuralKey = registries.definitions().lookup(structuralMarker);
        ZC_REQUIRE(structuralKey != zc::none);
        zc::Vector<checker::signature::MarkerPolicyConfigurationEntry> policyEntries;
        ZC_IF_SOME(key, structuralKey) {
          zc::Vector<checker::signature::MarkerStructuralSubject> structuralSubjects;
          structuralSubjects.add(checker::signature::MarkerStructuralSubject::Tuple);
          structuralSubjects.add(checker::signature::MarkerStructuralSubject::Object);
          structuralSubjects.add(checker::signature::MarkerStructuralSubject::FixedArray);
          zc::Vector<checker::signature::PrimitiveKind> builtinPrimitives;
          builtinPrimitives.add(checker::signature::PrimitiveKind::I32);
          zc::Vector<checker::signature::MarkerPolicyReferenceConfiguration> referenceRequirements;
          referenceRequirements.add(checker::signature::MarkerPolicyReferenceConfiguration{
              checker::signature::Mutability::Const, key.clone()});
          policyEntries.add(checker::signature::MarkerPolicyConfigurationEntry{
              key.clone(), zc::mv(structuralSubjects), zc::mv(builtinPrimitives),
              zc::mv(referenceRequirements)});
        }
        auto configuration =
            checker::signature::MarkerPolicyConfiguration::from(zc::mv(policyEntries));
        ZC_REQUIRE(configuration != zc::none);
        zc::Vector<identity::ModuleId> authorizedPreludeModules;
        authorizedPreludeModules.add(boundModules[0].module());
        zc::Maybe<checker::signature::VerifiedMarkerPolicyRegistry> policies;
        ZC_IF_SOME(value, configuration) {
          auto policyResult = checker::signature::MarkerPolicyRegistryBuilder::build(
              value, shapes, authorizedPreludeModules.asPtr(), registries);
          ZC_REQUIRE(policyResult.is<checker::signature::VerifiedMarkerPolicyRegistry>());
          policies = zc::mv(policyResult).get<checker::signature::VerifiedMarkerPolicyRegistry>();
        }
        ZC_IF_SOME(policy, policies) {
          auto signatureResult = checker::signature::SignatureFactsBuilder::build(
              checker::signature::SignatureFactsBuildInput{boundModules[0], registries,
                                                           semanticTypes, shapes, policy});
          ZC_REQUIRE(signatureResult.is<checker::signature::VerifiedSignatureFacts>());
          auto signatures =
              zc::mv(signatureResult).get<checker::signature::VerifiedSignatureFacts>();

          zc::Vector<checker::cross_module::ImportedSignatureModule> noImportedModules;
          auto importedResult = checker::cross_module::ImportedSignatureViewBuilder::build(
              session.getSemanticContextBrand(), fingerprint, boundModules[0].module(),
              zc::mv(noImportedModules), registries);
          ZC_REQUIRE(importedResult != zc::none);
          ZC_IF_SOME(imported, importedResult) {
            auto borrowResult = checker::borrow::BorrowInterfaceBuilder::build(
                checker::borrow::BorrowInterfaceBuildInput{
                    session.getSemanticContextBrand(), fingerprint, boundModules[0].module(),
                    signatures.revision(), imported.revision(), signatures.signatures(),
                    zc::ArrayPtr<const checker::signature::SemanticSignature>(), registries,
                    semanticTypes});
            ZC_REQUIRE(borrowResult.is<checker::borrow::VerifiedBorrowInterfaceSurface>());
            auto interfaceResult = ModuleInterfaceVerifier::build(ModuleInterfaceBuildInput{
                boundModules[0], signatures, imported, policy,
                zc::mv(borrowResult).get<checker::borrow::VerifiedBorrowInterfaceSurface>(),
                registries, semanticTypes});
            ZC_REQUIRE(interfaceResult.is<VerifiedModuleInterface>());
            zc::Vector<VerifiedModuleInterface> interfaces;
            interfaces.add(zc::mv(interfaceResult).get<VerifiedModuleInterface>());
            auto coherenceResult = CoherenceBuilder::build(
                CoherenceBuildInput{session.getSemanticContextBrand(), fingerprint, policy,
                                    interfaces.asPtr(), registries});
            ZC_REQUIRE(coherenceResult.is<checker::coherence::CoherenceFrozen>());
            auto coherence = zc::mv(coherenceResult).get<checker::coherence::CoherenceFrozen>();

            auto admittedI32 = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
                type::semantic::PrimitiveTypeData{type::semantic::PrimitiveKind::I32}));
            ZC_REQUIRE(admittedI32.is<type::semantic::CanonicalTypeData>());
            auto internedI32 =
                semanticTypes.intern(zc::mv(admittedI32).get<type::semantic::CanonicalTypeData>());
            ZC_REQUIRE(internedI32.is<type::SemanticTypeInterned>());
            const auto i32 = internedI32.get<type::SemanticTypeInterned>().id;
            zc::Vector<identity::SemanticTypeId> tupleElements;
            tupleElements.add(i32);
            tupleElements.add(i32);
            auto admittedTuple = semanticTypes.canonicalizeClosed(
                type::semantic::TypeData(type::semantic::TupleTypeData{zc::mv(tupleElements)}));
            ZC_REQUIRE(admittedTuple.is<type::semantic::CanonicalTypeData>());
            auto internedTuple = semanticTypes.intern(
                zc::mv(admittedTuple).get<type::semantic::CanonicalTypeData>());
            ZC_REQUIRE(internedTuple.is<type::SemanticTypeInterned>());
            const auto tuple = internedTuple.get<type::SemanticTypeInterned>().id;
            zc::Vector<type::semantic::ObjectFieldData> objectFields;
            objectFields.add(type::semantic::ObjectFieldData{
                scalar<identity::SemanticIdentifier>("field"_zc), i32,
                type::semantic::Mutability::Const, type::semantic::FieldPresence::Required});
            auto admittedObject = semanticTypes.canonicalizeClosed(
                type::semantic::TypeData(type::semantic::ObjectTypeData{zc::mv(objectFields)}));
            ZC_REQUIRE(admittedObject.is<type::semantic::CanonicalTypeData>());
            auto internedObject = semanticTypes.intern(
                zc::mv(admittedObject).get<type::semantic::CanonicalTypeData>());
            ZC_REQUIRE(internedObject.is<type::SemanticTypeInterned>());
            const auto object = internedObject.get<type::SemanticTypeInterned>().id;
            auto admittedArray = semanticTypes.canonicalizeClosed(
                type::semantic::TypeData(type::semantic::FixedArrayTypeData{i32, 4}));
            ZC_REQUIRE(admittedArray.is<type::semantic::CanonicalTypeData>());
            auto internedArray = semanticTypes.intern(
                zc::mv(admittedArray).get<type::semantic::CanonicalTypeData>());
            ZC_REQUIRE(internedArray.is<type::SemanticTypeInterned>());
            const auto array = internedArray.get<type::SemanticTypeInterned>().id;
            auto admittedReference = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
                type::semantic::ReferenceTypeData{type::semantic::Mutability::Const, i32}));
            ZC_REQUIRE(admittedReference.is<type::semantic::CanonicalTypeData>());
            auto internedReference = semanticTypes.intern(
                zc::mv(admittedReference).get<type::semantic::CanonicalTypeData>());
            ZC_REQUIRE(internedReference.is<type::SemanticTypeInterned>());
            const auto reference = internedReference.get<type::SemanticTypeInterned>().id;
            auto admittedMutableReference =
                semanticTypes.canonicalizeClosed(type::semantic::TypeData(
                    type::semantic::ReferenceTypeData{type::semantic::Mutability::Mutable, i32}));
            ZC_REQUIRE(admittedMutableReference.is<type::semantic::CanonicalTypeData>());
            auto internedMutableReference = semanticTypes.intern(
                zc::mv(admittedMutableReference).get<type::semantic::CanonicalTypeData>());
            ZC_REQUIRE(internedMutableReference.is<type::SemanticTypeInterned>());
            const auto mutableReference =
                internedMutableReference.get<type::SemanticTypeInterned>().id;

            auto inventoryResult =
                checker::body::BodyFactRequirementInventoryBuilder::build(boundModules[0]);
            ZC_REQUIRE(inventoryResult.is<checker::body::VerifiedBodyFactRequirementInventory>());
            auto inventory =
                zc::mv(inventoryResult).get<checker::body::VerifiedBodyFactRequirementInventory>();
            auto crateKey = registries.crates().lookup(boundModules[0].crate());
            ZC_REQUIRE(crateKey != zc::none);
            ZC_IF_SOME(crate, crateKey) {
              checker::body::BodyCheckingInput bodyInput{
                  boundModules[0], signatures,    imported,  coherence.view,
                  registries,      semanticTypes, inventory, crate.semanticOptions()};
              auto proofInput = checker::marker::MarkerProofInput::from(bodyInput, policy);
              ZC_REQUIRE(proofInput != zc::none);
              ZC_IF_SOME(value, proofInput) {
                checker::marker::MarkerProofEngine engine(zc::mv(value));
                auto builtin = engine.prove(structuralMarker, i32);
                ZC_REQUIRE(builtin.is<checker::marker::MarkerProofPositive>());
                ZC_EXPECT(builtin.get<checker::marker::MarkerProofPositive>()
                              .proof.evidence.variant()
                              .is<checker::signature::BuiltinMarkerEvidence>());

                auto structural = engine.prove(structuralMarker, tuple);
                ZC_REQUIRE(structural.is<checker::marker::MarkerProofPositive>());
                const auto& structuralEvidence =
                    structural.get<checker::marker::MarkerProofPositive>()
                        .proof.evidence.variant()
                        .get<checker::signature::StructuralMarkerEvidence>();
                ZC_EXPECT(structuralEvidence.components.size() == 2);

                auto objectProof = engine.prove(structuralMarker, object);
                ZC_REQUIRE(objectProof.is<checker::marker::MarkerProofPositive>());
                ZC_EXPECT(objectProof.get<checker::marker::MarkerProofPositive>()
                              .proof.evidence.variant()
                              .get<checker::signature::StructuralMarkerEvidence>()
                              .components.size() == 1);

                auto arrayProof = engine.prove(structuralMarker, array);
                ZC_REQUIRE(arrayProof.is<checker::marker::MarkerProofPositive>());
                ZC_EXPECT(arrayProof.get<checker::marker::MarkerProofPositive>()
                              .proof.evidence.variant()
                              .get<checker::signature::StructuralMarkerEvidence>()
                              .components.size() == 1);

                auto referenceProof = engine.prove(structuralMarker, reference);
                ZC_REQUIRE(referenceProof.is<checker::marker::MarkerProofPositive>());
                const auto& referenceEvidence =
                    referenceProof.get<checker::marker::MarkerProofPositive>()
                        .proof.evidence.variant()
                        .get<checker::signature::StructuralMarkerEvidence>();
                ZC_REQUIRE(referenceEvidence.components.size() == 1);
                ZC_EXPECT(referenceEvidence.components[0].supportingFact.marker ==
                          structuralMarker);
                ZC_EXPECT(referenceEvidence.components[0].supportingFact.subject == i32);

                auto unsupportedReference = engine.prove(structuralMarker, mutableReference);
                ZC_EXPECT(unsupportedReference.is<checker::marker::MarkerProofUnsatisfied>());

                for (const auto& fact : signatures.markerFacts()) {
                  auto explicitResult = engine.prove(fact.key.marker, fact.key.subject);
                  if (fact.polarity == checker::signature::Polarity::Positive) {
                    ZC_EXPECT(explicitResult.is<checker::marker::MarkerProofPositive>());
                  } else {
                    ZC_EXPECT(explicitResult.is<checker::marker::MarkerProofNegative>());
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

ZC_TEST("CompilerSession publishes scalar initializer definition and pattern facts") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "let value = 0;"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
  const auto leases = session.getCheckedEvidenceLeases();
  ZC_REQUIRE(leases.size() == 1);
  ZC_REQUIRE(session.getVerifiedDispatchFacts().size() == 1);
  ZC_EXPECT(session.getVerifiedDispatchFacts()[0].facts().size() == 0);
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_EXPECT(session.getVerifiedHirModules()[0].declarations().size() == 1);
  ZC_REQUIRE(session.getVerifiedBuiltMirModules().size() == 1);
  const auto& builtMir = session.getVerifiedBuiltMirModules()[0];
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_EXPECT(function.owner == session.getVerifiedHirModules()[0].declarations()[0].definition);
  ZC_EXPECT(function.kind == mir::MirFunctionKind::ModuleInitializer);
  ZC_REQUIRE(function.sourceScopes.size() == 1);
  ZC_REQUIRE(function.locals.size() == 1);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_REQUIRE(function.blocks[0].statements.size() == 2);
  ZC_EXPECT(function.blocks[0].statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(function.blocks[0].statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(function.blocks[0].terminator.kind() == mir::MirTerminatorKind::Return);
  auto codecCandidate = mir::BuiltMirBuilder::build(session.getVerifiedHirModules()[0]);
  ZC_REQUIRE(codecCandidate.isVerified());
  auto corruptedCodec = zc::mv(codecCandidate).takeVerified();
  ZC_REQUIRE(corruptedCodec.canonicalFunctions.size() == 1);
  ZC_REQUIRE(corruptedCodec.canonicalFunctions[0].size() != 0);
  corruptedCodec.canonicalFunctions[0][0] ^= 0x01;
  auto codecRejected = mir::BuiltMirVerifier::verify(zc::mv(corruptedCodec));
  ZC_REQUIRE(codecRejected.isIrInvariantRejected());
  ZC_REQUIRE(codecRejected.invariantFailures().facts().size() == 1);
  ZC_EXPECT(codecRejected.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::CanonicalCodecMismatch);

  auto structureCandidate = mir::BuiltMirBuilder::build(session.getVerifiedHirModules()[0]);
  ZC_REQUIRE(structureCandidate.isVerified());
  auto corruptedStructure = zc::mv(structureCandidate).takeVerified();
  ZC_REQUIRE(corruptedStructure.functions.size() == 1);
  ZC_REQUIRE(corruptedStructure.functions[0].locals.size() == 1);
  corruptedStructure.functions[0].locals[0].id = mir::MirLocalId();
  auto structureRejected = mir::BuiltMirVerifier::verify(zc::mv(corruptedStructure));
  ZC_REQUIRE(structureRejected.isIrInvariantRejected());
  ZC_REQUIRE(structureRejected.invariantFailures().facts().size() == 1);
  ZC_EXPECT(structureRejected.invariantFailures().facts()[0].kind() ==
            ir::IrFailureKind::InvalidFact);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
  auto repository = session.getCheckedFactsRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto facts = value.lookup(leases[0]);
    ZC_REQUIRE(facts != zc::none);
    ZC_IF_SOME(checked, facts) {
      ZC_EXPECT(checked.nodeTypes().size() == 1);
      ZC_EXPECT(checked.definitionTypes().size() == 1);
      ZC_EXPECT(checked.literals().size() == 1);
      ZC_EXPECT(checked.patterns().size() == 1);
      ZC_EXPECT(checked.constants().size() == 0);
    }
  }
}

ZC_TEST("CompilerSession publishes a checked scalar-return function through HIR and Built MIR") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "fun answer() -> i32 { return 42; }"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
  ZC_REQUIRE(session.getVerifiedSignatureFacts().size() == 1);
  const auto signatures = session.getVerifiedSignatureFacts()[0].signatures();
  ZC_REQUIRE(signatures.size() == 1);
  ZC_REQUIRE(signatures[0].payload.variant().is<checker::signature::CallableSignature>());
  const auto& callable =
      signatures[0].payload.variant().get<checker::signature::CallableSignature>();
  ZC_EXPECT(callable.parameters.size() == 0);
  ZC_EXPECT(callable.raises == zc::none);

  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  const auto& hirModule = session.getVerifiedHirModules()[0];
  ZC_EXPECT(hirModule.declarations().size() == 0);
  ZC_REQUIRE(hirModule.functions().size() == 1);
  ZC_REQUIRE(hirModule.blocks().size() == 1);
  ZC_REQUIRE(hirModule.returns().size() == 1);
  ZC_REQUIRE(hirModule.expressions().size() == 1);
  ZC_EXPECT(hirModule.functions()[0].resultType == callable.success);
  ZC_EXPECT(hirModule.functions()[0].body == hirModule.blocks()[0].node);
  ZC_EXPECT(hirModule.blocks()[0].statements[0] == hirModule.returns()[0].node);
  ZC_EXPECT(hirModule.returns()[0].value == hirModule.expressions()[0].node);

  ZC_REQUIRE(session.getVerifiedBuiltMirModules().size() == 1);
  const auto& builtMir = session.getVerifiedBuiltMirModules()[0];
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_EXPECT(function.owner == hirModule.functions()[0].definition);
  ZC_EXPECT(function.kind == mir::MirFunctionKind::Function);
  ZC_EXPECT(function.resultType == callable.success);
  ZC_EXPECT(function.locals.size() == 0);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_EXPECT(function.blocks[0].statements.size() == 0);
  ZC_EXPECT(function.blocks[0].terminator.kind() == mir::MirTerminatorKind::Return);
  ZC_REQUIRE(function.blocks[0].terminator.returnValue().value != zc::none);
  ZC_IF_SOME(value, function.blocks[0].terminator.returnValue().value) {
    ZC_EXPECT(value.kind() == mir::MirOperandKind::Constant);
    ZC_EXPECT(value.constantValue().type == callable.success);
  }
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession rejects a scalar return whose type differs from the signature") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  CapturedDiagnostics captured;
  session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "fun answer() -> bool { return 42; }"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_EXPECT(!session.checkSources());
  ZC_EXPECT(session.getCheckerInvariantFailures().size() == 0);
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::TypeCheckerTypeMismatch);
  ZC_EXPECT(session.getVerifiedHirModules().size() == 0);
  ZC_EXPECT(session.getVerifiedBuiltMirModules().size() == 0);
}

ZC_TEST("CompilerSession publishes canonical constant facts for scalar const initializers") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "const value = 42;"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
  const auto leases = session.getCheckedEvidenceLeases();
  ZC_REQUIRE(leases.size() == 1);
  ZC_REQUIRE(session.getVerifiedDispatchFacts().size() == 1);
  ZC_EXPECT(session.getVerifiedDispatchFacts()[0].facts().size() == 0);
  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  ZC_EXPECT(session.getVerifiedHirModules()[0].declarations().size() == 1);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
  auto repository = session.getCheckedFactsRepository();
  ZC_REQUIRE(repository != zc::none);
  ZC_IF_SOME(value, repository) {
    auto facts = value.lookup(leases[0]);
    ZC_REQUIRE(facts != zc::none);
    ZC_IF_SOME(checked, facts) {
      ZC_EXPECT(checked.nodeTypes().size() == 1);
      ZC_EXPECT(checked.definitionTypes().size() == 1);
      ZC_EXPECT(checked.literals().size() == 1);
      ZC_EXPECT(checked.patterns().size() == 1);
      ZC_EXPECT(checked.constants().size() == 1);
    }
  }
}

ZC_TEST("CompilerSession verifies recovered literal failures without publishing Checker facts") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "let value = 18446744073709551616;"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_EXPECT(!session.checkSources());
  ZC_EXPECT(session.getDiagnosticEngine().hasErrors());
  ZC_EXPECT(session.getCheckerInvariantFailures().size() == 0);
  ZC_EXPECT(session.getVerifiedSignatureFacts().size() == 0);
  ZC_EXPECT(session.getImportedSignatureViews().size() == 0);
  ZC_EXPECT(session.getVerifiedModuleInterfaces().size() == 0);
  ZC_EXPECT(session.getFrozenCoherenceView() == zc::none);
  ZC_EXPECT(session.getCheckedFactsRepository() == zc::none);
  ZC_EXPECT(session.getCheckedEvidenceLeases().size() == 0);
  ZC_EXPECT(session.getVerifiedDispatchFacts().size() == 0);
  ZC_EXPECT(session.getBorrowEvidenceRepository() == zc::none);
  ZC_EXPECT(session.getVerifiedHirModules().size() == 0);
  ZC_EXPECT(session.getVerifiedBuiltMirModules().size() == 0);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession routes short and qualified safe marker candidates to ZOM4091") {
  for (const auto sourceText : {
           "interface Marker {}\nimpl Marker for i32;\n"_zc,
           "interface Marker {}\nimpl app::Marker for i32;\n"_zc,
       }) {
    basic::LangOptions languageOptions;
    basic::CompilerOptions compilerOptions;
    identity::SemanticContextFactory contextFactory;
    CompilerSession session(contextFactory, languageOptions, compilerOptions);
    CapturedDiagnostics captured;
    session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
    auto registry = targetRegistry();
    auto input = VerifiedPackageSessionInput::from(
        request(registry), verifiedSelection(registry), verifiedSelection(registry),
        resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
        resolvedSourceSnapshots("app"_zc, sourceText));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_EXPECT(!session.checkSources());
    ZC_EXPECT(session.getCheckerInvariantFailures().size() == 0);
    ZC_REQUIRE(captured.ids.size() == 1);
    ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::PositiveMarkerImplRequiresUnsafe);
  }
}

ZC_TEST("CompilerSession gives behavior body diagnostics precedence over marker safety") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  CapturedDiagnostics captured;
  session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc,
                              "interface Behavior { fun act(); }\nimpl Behavior for i32;\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_EXPECT(!session.checkSources());
  ZC_EXPECT(session.getCheckerInvariantFailures().size() == 0);
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::BehaviorInterfaceRequiresImplBody);
}

ZC_TEST("CompilerSession publishes no partial Checker rail when a later module is rejected") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      atomicCheckerFailureModuleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_EXPECT(!session.checkSources());
  ZC_EXPECT(session.getCheckerInvariantFailures().size() != 0);
  ZC_EXPECT(session.getVerifiedSignatureFacts().size() == 0);
  ZC_EXPECT(session.getImportedSignatureViews().size() == 0);
  ZC_EXPECT(session.getVerifiedModuleInterfaces().size() == 0);
  ZC_EXPECT(session.getFrozenCoherenceView() == zc::none);
  ZC_EXPECT(session.getCheckedFactsRepository() == zc::none);
  ZC_EXPECT(session.getCheckedEvidenceLeases().size() == 0);
  ZC_EXPECT(session.getVerifiedDispatchFacts().size() == 0);
  ZC_EXPECT(session.getBorrowEvidenceRepository() == zc::none);
  ZC_EXPECT(session.getVerifiedHirModules().size() == 0);
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("CompilerSession retains structural ancestry without intermediate module sources") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc), nestedModuleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(session.getParsedModules().size() == 2);
  ZC_REQUIRE(session.getVerifiedModuleGraph() != zc::none);
  ZC_IF_SOME(graph, session.getVerifiedModuleGraph()) {
    ZC_EXPECT(graph.modules().size() == 2);
    ZC_EXPECT(graph.edges().size() == 1);
  }
}

ZC_TEST("CompilerSession rejects a source declaration that differs from its selected module") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  CapturedDiagnostics captured;
  session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      mismatchedModuleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_EXPECT(session.hasVerifiedParsedSyntax());
  ZC_EXPECT(session.getParsedModules().size() == 1);
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::ModuleDeclarationNameMismatch);
  ZC_EXPECT(captured.unmanagedPrimaryLocations == 0);
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.modules().isFrozen());
    ZC_EXPECT(registries.modules().size() == 0);
  }
}

ZC_TEST("CompilerSession retains verified syntax when a structural module is missing") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  CapturedDiagnostics captured;
  session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc), missingModuleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_EXPECT(session.hasVerifiedParsedSyntax());
  ZC_EXPECT(session.getParsedModules().size() == 1);
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::ImportModuleNotFound);
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
    ZC_REQUIRE(graph.roots().size() == 2);
    ZC_REQUIRE(graph.crates().size() == 2);
    ZC_REQUIRE(graph.edges().size() == 1);
    bool foundProviderRoot = false;
    for (size_t index = 0; index < graph.roots().size(); ++index) {
      ZC_EXPECT(graph.roots()[index].crateKey().encode().asPtr() ==
                graph.crates()[index].encode().asPtr());
      if (graph.roots()[index].packageKey().name() != "math"_zc) { continue; }
      foundProviderRoot = true;
      ZC_EXPECT(graph.roots()[index].crateKey().targetKind() == identity::CrateTargetKind::Library);
      ZC_EXPECT(graph.roots()[index].crateKey().targetName() == "math"_zc);
      ZC_REQUIRE(graph.roots()[index].sourcePath().segments().size() == 2);
      ZC_EXPECT(graph.roots()[index].sourcePath().segments()[0].text() == "src"_zc);
      ZC_EXPECT(graph.roots()[index].sourcePath().segments()[1].text() == "lib.zom"_zc);
    }
    ZC_EXPECT(foundProviderRoot);
    ZC_EXPECT(graph.edges()[0].consumer().targetKind() == identity::CrateTargetKind::Binary);
    ZC_EXPECT(graph.edges()[0].provider().targetKind() == identity::CrateTargetKind::Library);
    ZC_EXPECT(graph.edges()[0].provider().package().name() == "math"_zc);
  }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 2);
  for (const auto& root : roots) {
    auto admitted = session.addVerifiedPackageRoot(root);
    ZC_REQUIRE(admitted != zc::none);
    if (root.packageKey().name() == "math"_zc) {
      ZC_IF_SOME(buffer, admitted) {
        ZC_EXPECT(session.getSourceManager().getEntireTextForBuffer(buffer) ==
                  "let library = 0;"_zcb);
      }
    }
  }
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.getParsedModules().size() == 2);
  ZC_REQUIRE(session.getSemanticContextFingerprint() != zc::none);
  ZC_REQUIRE(session.getVerifiedModuleGraph() != zc::none);
  ZC_IF_SOME(graph, session.getVerifiedModuleGraph()) { ZC_EXPECT(graph.modules().size() == 2); }
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

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(registries.packages().size() == 1);
    ZC_EXPECT(registries.crates().size() == 1);
    ZC_EXPECT(registries.packages().find(packageKey("tool"_zc)) == zc::none);
  }
}

ZC_TEST("CompilerSession rejects duplicate stable definitions before registry mutation") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  CapturedDiagnostics captured;
  session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "fun value();\nfun value();\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::RedeclareFunction);
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(!registries.definitions().isFrozen());
    ZC_EXPECT(registries.definitions().size() == 0);
  }
}

ZC_TEST("CompilerSession rejects duplicate generic binders before registry mutation") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  CapturedDiagnostics captured;
  session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "fun run<T, T>();\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::DuplicateIdentifier);
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(!registries.definitions().isFrozen());
    ZC_EXPECT(registries.definitions().size() == 0);
    ZC_EXPECT(registries.impls().size() == 0);
    ZC_EXPECT(registries.genericParameters().size() == 0);
    ZC_EXPECT(registries.callableParameters().size() == 0);
  }
}

ZC_TEST("CompilerSession reports non-literal stable array lengths as source failures") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  CapturedDiagnostics captured;
  session.getDiagnosticEngine().addConsumer(zc::heap<CaptureDiagnosticConsumer>(captured));
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "fun run(size: i32) -> [i32; size] {}\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::ConstantExpressionNotAllowed);
  ZC_REQUIRE(session.getIdentityRegistries() != zc::none);
  ZC_IF_SOME(registries, session.getIdentityRegistries()) {
    ZC_EXPECT(!registries.definitions().isFrozen());
    ZC_EXPECT(registries.definitions().size() == 0);
  }
}

}  // namespace zomlang::compiler::driver
