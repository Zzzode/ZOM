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
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/driver/coherence-builder.h"
#include "zomlang/compiler/driver/compiler-session.h"
#include "zomlang/compiler/driver/imported-signature-view-projector.h"
#include "zomlang/compiler/driver/package/manifest-parser.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/driver/package/trusted-runtime-manifest.h"
#include "zomlang/compiler/ownership/surface-admission.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/tests/unittests/compiler/driver/core-library-test-fixture.h"

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

identity::CanonicalTargetSpecificationKey projection(zc::StringPtr abiProfile = "zom"_zc) {
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

ir::TargetRegistrySnapshot targetRegistry(zc::StringPtr abiProfile = "zom"_zc) {
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

package::DigestVerifiedSourceSnapshot definitionImportSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import app::child::{Item, Companion};\nlet main = 0;"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "child.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("module child;\nexport class Item {}\nexport class Companion {}"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot importedBehaviorImplementationSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import app::child::{Behavior};\nimpl Behavior for i32 { fun act() {} }"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "child.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("module child;\nexport interface Behavior { fun act(); }"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot moduleAliasSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("import app::geometry;\nlet main = 0;"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "geometry.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("module geometry = app::child;"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "child.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("module child;\nexport class Point {}"_zc);
  MemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::DigestVerifiedSourceSnapshot coherenceFailureModuleSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(
          "import app::child::{Behavior};\nimport app::peer;\nimpl Behavior for i32 { fun act() -> i32 { return 0; } }"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "child.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("module child;\nexport interface Behavior { fun act() -> i32; }"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "peer.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll(
          "module peer;\nimport app::child::{Behavior};\nimpl Behavior for i32 { fun act() -> i32 { return 0; } }"_zc);
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

zc::Vector<package::ResolvedPackageSourceSnapshot> importedBehaviorImplementationSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(
      packageBase("app"_zc), importedBehaviorImplementationSnapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> moduleAliasSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(
      package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc), moduleAliasSnapshot()));
  return snapshots;
}

zc::Vector<package::ResolvedPackageSourceSnapshot> coherenceFailureModuleSnapshots() {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc),
                                                             coherenceFailureModuleSnapshot()));
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
      "zom"_zc, "zom"_zc, zc::mv(objects),
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

void installCore(CompilerSession& session) {
  auto distribution = core_library_test::admittedCoreDistribution();
  ZC_REQUIRE(session.installVerifiedCoreDistribution(distribution));
}

bool isToolchainCore(const identity::CrateKey& crate) {
  return crate.unit().kind() == identity::CompilationUnitKind::Toolchain &&
         crate.unit().toolchain().component() == identity::ToolchainComponent::Core;
}

size_t userParsedModuleCount(const CompilerSession& session) {
  auto parsedModules = session.materializeParsedModules();
  if (parsedModules == zc::none) { ZC_FAIL_REQUIRE("missing sealed parser results"); }
  size_t count = 0;
  for (const auto& parsed : ZC_ASSERT_NONNULL(parsedModules)) {
    if (!isToolchainCore(parsed.parsedModule().source().crate())) { ++count; }
  }
  return count;
}

size_t coreParsedModuleCount(const CompilerSession& session) {
  auto parsedModules = session.materializeParsedModules();
  if (parsedModules == zc::none) { ZC_FAIL_REQUIRE("missing sealed parser results"); }
  return ZC_ASSERT_NONNULL(parsedModules).size() - userParsedModuleCount(session);
}

const module_graph_query::MaterializedModuleEntry& materializedModule(
    const module_graph_query::MaterializedModuleGraph& graph, identity::ModuleId handle) {
  for (const auto& module : graph.modules()) {
    if (module.handle() == handle) { return module; }
  }
  ZC_FAIL_REQUIRE("missing materialized module handle");
}

size_t userGraphModuleCount(const module_graph_query::MaterializedModuleGraph& graph) {
  size_t count = 0;
  for (const auto& module : graph.modules()) {
    if (!isToolchainCore(module.key().crate())) { ++count; }
  }
  return count;
}

size_t coreGraphModuleCount(const module_graph_query::MaterializedModuleGraph& graph) {
  return graph.modules().size() - userGraphModuleCount(graph);
}

size_t userGraphCrateCount(const module_graph_query::MaterializedModuleGraph& graph) {
  size_t count = 0;
  for (const auto& crate : graph.crates()) {
    if (!isToolchainCore(crate.key())) { ++count; }
  }
  return count;
}

size_t coreGraphCrateCount(const module_graph_query::MaterializedModuleGraph& graph) {
  return graph.crates().size() - userGraphCrateCount(graph);
}

size_t userGraphSourceCount(const module_graph_query::MaterializedModuleGraph& graph) {
  size_t count = 0;
  for (const auto& source : graph.sources()) {
    if (!isToolchainCore(source.key().crate())) { ++count; }
  }
  return count;
}

size_t coreGraphSourceCount(const module_graph_query::MaterializedModuleGraph& graph) {
  return graph.sources().size() - userGraphSourceCount(graph);
}

size_t userGraphEdgeCount(const module_graph_query::MaterializedModuleGraph& graph,
                          zc::Maybe<identity::ModuleDependencyKind> kind = zc::none) {
  size_t count = 0;
  for (const auto& edge : graph.requestEdges()) {
    if (isToolchainCore(materializedModule(graph, edge.requester()).key().crate())) { continue; }
    ZC_IF_SOME(expected, kind) {
      if (edge.request().dependencyKind() != expected) { continue; }
    }
    ++count;
  }
  return count;
}

size_t coreGraphEdgeCount(const module_graph_query::MaterializedModuleGraph& graph) {
  size_t count = 0;
  for (const auto& edge : graph.requestEdges()) {
    if (isToolchainCore(materializedModule(graph, edge.requester()).key().crate())) { ++count; }
  }
  return count;
}

size_t userBoundModuleCount(const checker::CheckerIdentityAuthority& authority) {
  size_t count = 0;
  for (const auto& bound : authority.modules()) {
    auto crate = authority.crate(bound.crate());
    ZC_REQUIRE(crate != zc::none);
    if (!isToolchainCore(ZC_REQUIRE_NONNULL(crate).key())) { ++count; }
  }
  return count;
}

size_t coreBoundModuleCount(const checker::CheckerIdentityAuthority& authority) {
  return authority.modules().size() - userBoundModuleCount(authority);
}

const module_graph_query::CheckerBoundModuleView& soleUserBoundModule(
    const checker::CheckerIdentityAuthority& authority) {
  zc::Maybe<const module_graph_query::CheckerBoundModuleView&> selected;
  for (const auto& bound : authority.modules()) {
    auto crate = authority.crate(bound.crate());
    ZC_REQUIRE(crate != zc::none);
    if (isToolchainCore(ZC_REQUIRE_NONNULL(crate).key())) { continue; }
    ZC_REQUIRE(selected == zc::none);
    selected = bound;
  }
  ZC_IF_SOME(bound, selected) { return bound; }
  ZC_FAIL_REQUIRE("expected exactly one user-package bound module");
}

const module_graph_query::CheckerBoundModuleView& checkerBoundModule(
    const checker::CheckerIdentityAuthority& authority, identity::ModuleId module) {
  auto view = authority.boundModule(module);
  ZC_REQUIRE(view != zc::none);
  return ZC_REQUIRE_NONNULL(view);
}

bool isUserBoundModule(const checker::CheckerIdentityAuthority& authority,
                       identity::ModuleId module) {
  const auto& bound = checkerBoundModule(authority, module);
  auto crate = authority.crate(bound.crate());
  ZC_REQUIRE(crate != zc::none);
  return !isToolchainCore(ZC_REQUIRE_NONNULL(crate).key());
}

checker::CheckerIdentityAuthority checkerIdentityAuthority(const CompilerSession& session) {
  auto authority = session.materializeCheckerIdentityAuthority();
  ZC_REQUIRE(authority != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(authority));
}

zc::Vector<size_t> dependencyOrderedBoundModuleIndices(
    const checker::CheckerIdentityAuthority& authority) {
  const auto boundModules = authority.modules();
  const auto& materializedGraph = authority.graphLease().capability();
  zc::Vector<size_t> indices(boundModules.size());
  for (const auto& component : materializedGraph.witness().scc().components()) {
    ZC_REQUIRE(component.modules().size() == 1);
    zc::Maybe<size_t> selected;
    for (size_t index = 0; index < materializedGraph.modules().size(); ++index) {
      if (materializedGraph.modules()[index].key().encode().asPtr() !=
          component.modules()[0].encode().asPtr()) {
        continue;
      }
      ZC_REQUIRE(selected == zc::none);
      selected = index;
    }
    ZC_REQUIRE(selected != zc::none);
    ZC_REQUIRE(ZC_REQUIRE_NONNULL(selected) < boundModules.size());
    indices.add(ZC_REQUIRE_NONNULL(selected));
  }
  ZC_REQUIRE(indices.size() == boundModules.size());
  return indices;
}

zc::Own<CompilerSession> preparedSession(const basic::LangOptions& languageOptions,
                                         const basic::CompilerOptions& compilerOptions,
                                         bool requiresBuildScript = false) {
  identity::SemanticContextFactory contextFactory;
  auto session = zc::heap<CompilerSession>(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  ZC_REQUIRE(session->installVerifiedPackageInput(
      packageInput(session->getPackageResolutionMemoryResource(), registry, requiresBuildScript)));
  installCore(*session);
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
  auto otherRegistry = targetRegistry("zom-alternate"_zc);
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
  installCore(session);

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
  ZC_EXPECT(graph.edges()[0].origin().kind() == identity::CrateDependencyOriginKind::UserPackage);
  ZC_EXPECT(graph.edges()[0].origin().userPackageEdge().domain() ==
            identity::DependencyDomain::Build);
  ZC_EXPECT(graph.edges()[0].consumer().targetKind() == identity::CrateTargetKind::BuildScript);
  ZC_EXPECT(graph.edges()[0].provider().targetKind() == identity::CrateTargetKind::Library);
  ZC_EXPECT(graph.edges()[0].provider().unit().userPackage().name() == "tool"_zc);
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
  installCore(session);

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
    if (graph.root().unit().userPackage().name() == "app"_zc) {
      foundAppClosure = true;
      ZC_EXPECT(graph.crates().size() == 2);
      ZC_EXPECT(graph.edges().size() == 1);
      ZC_EXPECT(graph.edges()[0].provider().unit().userPackage().name() == "tool"_zc);
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
  installCore(session);
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
  ZC_REQUIRE(userParsedModuleCount(*session) == 1);
  ZC_REQUIRE(coreParsedModuleCount(*session) == 3);
  auto parsedModules = session->materializeParsedModules();
  ZC_REQUIRE(parsedModules != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(parsedModules)[0].parsedModule().receipt().digest().bytes().size() ==
            32);
  auto graphLease = session->materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  ZC_EXPECT(userGraphModuleCount(graph) == 1);
  ZC_EXPECT(coreGraphModuleCount(graph) == 3);
  ZC_EXPECT(coreGraphEdgeCount(graph) == 1);
  ZC_EXPECT(graph.revision().value() != 0);

  ZC_EXPECT(userGraphCrateCount(graph) == 1);
  ZC_EXPECT(coreGraphCrateCount(graph) == 1);
  ZC_EXPECT(userGraphSourceCount(graph) == 1);
  ZC_EXPECT(coreGraphSourceCount(graph) == 3);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(!session.getDiagnosticEngine().hasErrors());
  ZC_REQUIRE(session.bindSources());
  auto authority = checkerIdentityAuthority(session);
  size_t userDefinitions = 0;
  size_t coreDefinitions = 0;
  size_t genericParameters = 0;
  size_t callableParameters = 0;
  for (const auto& bound : authority.modules()) {
    auto crate = authority.crate(bound.crate());
    ZC_REQUIRE(crate != zc::none);
    const auto& inventory = bound.definitions();
    if (isToolchainCore(ZC_REQUIRE_NONNULL(crate).key())) {
      coreDefinitions += inventory.definitions().size();
      continue;
    }
    userDefinitions += inventory.definitions().size();
    genericParameters += inventory.genericParameters().size();
    callableParameters += inventory.callableParameters().size();
  }
  ZC_EXPECT(userDefinitions == 15);
  ZC_EXPECT(coreDefinitions == 2);
  ZC_EXPECT(genericParameters == 2);
  ZC_EXPECT(callableParameters == 10);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(userParsedModuleCount(session) == 2);
  ZC_EXPECT(coreParsedModuleCount(session) == 3);
  auto graphLease = session.materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  ZC_EXPECT(userGraphModuleCount(graph) == 2);
  ZC_EXPECT(coreGraphModuleCount(graph) == 3);
  ZC_EXPECT(userGraphSourceCount(graph) == 2);
  ZC_EXPECT(coreGraphSourceCount(graph) == 3);
  ZC_EXPECT(userGraphEdgeCount(graph) == 3);
  ZC_EXPECT(userGraphEdgeCount(graph, identity::ModuleDependencyKind::Import) == 1);
  ZC_EXPECT(coreGraphEdgeCount(graph) == 1);
}

ZC_TEST("CompilerSession materializes imported behavior implementations before checking") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      importedBehaviorImplementationSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_EXPECT(!session.getDiagnosticEngine().hasErrors());
  auto authority = checkerIdentityAuthority(session);
  ZC_EXPECT(userBoundModuleCount(authority) == 2);
}

ZC_TEST("CompilerSession projects module aliases through retained dependency surfaces") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc), moduleAliasSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_EXPECT(!session.getDiagnosticEngine().hasErrors());
  auto authority = checkerIdentityAuthority(session);
  ZC_EXPECT(userBoundModuleCount(authority) == 3);
}

ZC_TEST("CompilerSession projects cross-module coherence failures") {
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
      coherenceFailureModuleSnapshots());
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_EXPECT(!session.checkSources());
  ZC_EXPECT(session.getDiagnosticEngine().hasErrors());
  ZC_EXPECT(session.getCheckerInvariantFailures().size() == 0);
  size_t orphanFailures = 0;
  size_t conflictingFailures = 0;
  for (const auto diagnostic : captured.ids) {
    if (diagnostic == diagnostics::DiagID::OrphanImpl) { ++orphanFailures; }
    if (diagnostic == diagnostics::DiagID::ConflictingImpl) { ++conflictingFailures; }
  }
  ZC_EXPECT(orphanFailures == 2);
  ZC_EXPECT(conflictingFailures == 1);
  ZC_EXPECT(session.getFrozenCoherenceView() == zc::none);
  ZC_EXPECT(session.getCheckedEvidenceLeases().size() == 0);
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
  installCore(session);

  RecordingPlanExecutor executor("module output_mod;\nlet generated_value = 1;"_zc);
  ZC_REQUIRE(session.executeBuildScripts(executor) == zc::none);
  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(userParsedModuleCount(session) == 2);
  ZC_EXPECT(coreParsedModuleCount(session) == 3);
  auto graphLease = session.materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  ZC_EXPECT(userGraphModuleCount(graph) == 2);
  ZC_EXPECT(coreGraphModuleCount(graph) == 3);
  ZC_EXPECT(userGraphSourceCount(graph) == 2);
  ZC_EXPECT(coreGraphSourceCount(graph) == 3);
  ZC_EXPECT(userGraphEdgeCount(graph) == 3);
  ZC_EXPECT(userGraphEdgeCount(graph, identity::ModuleDependencyKind::Import) == 1);
  ZC_EXPECT(coreGraphEdgeCount(graph) == 1);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  auto graphLease = session.materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  zc::Maybe<const module_graph_query::MaterializedModuleDependencyEdge&> explicitDependency;
  for (const auto& edge : graph.requestEdges()) {
    if (edge.request().dependencyKind() != identity::ModuleDependencyKind::Import) { continue; }
    ZC_REQUIRE(explicitDependency == zc::none);
    explicitDependency = edge;
  }
  ZC_REQUIRE(explicitDependency != zc::none);
  ZC_REQUIRE(session.bindSources());
  auto authority = session.materializeCheckerIdentityAuthority();
  ZC_REQUIRE(authority != zc::none);
  const auto& retainedAuthority = ZC_REQUIRE_NONNULL(authority);
  const auto bindings = retainedAuthority.modules();
  ZC_REQUIRE(bindings.size() == 5);
  for (const auto& binding : bindings) {
    ZC_EXPECT(binding.bindingSurface().sourceModule() == binding.module());
  }

  auto dependency = retainedAuthority.module(ZC_REQUIRE_NONNULL(explicitDependency).dependency());
  auto requester = retainedAuthority.module(ZC_REQUIRE_NONNULL(explicitDependency).requester());
  ZC_REQUIRE(dependency != zc::none);
  ZC_REQUIRE(requester != zc::none);

  const auto& materializedGraph = retainedAuthority.graphLease().capability();
  const auto& components = materializedGraph.witness().scc().components();
  zc::Maybe<size_t> dependencyComponentIndex;
  zc::Maybe<size_t> requesterComponentIndex;
  for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
    for (const auto& module : components[componentIndex].modules()) {
      if (module.encode().asPtr() == ZC_REQUIRE_NONNULL(dependency).key().encode().asPtr()) {
        ZC_REQUIRE(dependencyComponentIndex == zc::none);
        dependencyComponentIndex = componentIndex;
      }
      if (module.encode().asPtr() == ZC_REQUIRE_NONNULL(requester).key().encode().asPtr()) {
        ZC_REQUIRE(requesterComponentIndex == zc::none);
        requesterComponentIndex = componentIndex;
      }
    }
  }
  ZC_REQUIRE(dependencyComponentIndex != zc::none);
  ZC_REQUIRE(requesterComponentIndex != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(dependencyComponentIndex) <
            ZC_REQUIRE_NONNULL(requesterComponentIndex));
}

ZC_TEST("CompilerSession projects explicit imported module targets") {
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());

  bool foundExplicitModuleTarget = false;
  for (const auto& view : session.getImportedSignatureViews()) {
    for (const auto& imported : view.modules()) {
      if (imported.origin() != checker::cross_module::SignatureViewOrigin::ExplicitImport) {
        continue;
      }
      ZC_REQUIRE(imported.moduleTargets().size() == 1);
      foundExplicitModuleTarget = true;
    }
  }
  ZC_EXPECT(foundExplicitModuleTarget);
}

ZC_TEST("CompilerSession projects explicit imported definition signatures") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(packageBase("app"_zc),
                                                             definitionImportSnapshot()));
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc), zc::mv(snapshots));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());

  bool foundExplicitDefinition = false;
  for (const auto& view : session.getImportedSignatureViews()) {
    for (const auto& imported : view.modules()) {
      if (imported.origin() != checker::cross_module::SignatureViewOrigin::ExplicitImport ||
          imported.authorizedRoots().size() == 0) {
        continue;
      }
      ZC_EXPECT(imported.lookupDefinitions().size() == 2);
      foundExplicitDefinition = true;
    }
  }
  ZC_EXPECT(foundExplicitDefinition);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  auto identities = checkerIdentityAuthority(session);
  const auto& graph = identities.graphLease().capability();
  ZC_REQUIRE(graph.sources().size() == 5);
  ZC_EXPECT(userGraphSourceCount(graph) == 2);
  ZC_EXPECT(coreGraphSourceCount(graph) == 3);
  ZC_EXPECT(identities.modules().size() == graph.modules().size());
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_EXPECT(!session.getDiagnosticEngine().hasErrors());
  auto identities = checkerIdentityAuthority(session);
  const auto boundModules = identities.modules();
  ZC_REQUIRE(identities.modules().size() == boundModules.size());
  const auto& graphModules = identities.graphLease().capability().modules();
  ZC_REQUIRE(identities.modules().size() == graphModules.size());
  for (size_t index = 0; index < graphModules.size(); ++index) {
    ZC_EXPECT(identities.modules()[index].module() == graphModules[index].handle());
  }
  for (const auto& boundModule : boundModules) {
    ZC_EXPECT(identities.boundModule(boundModule.module()) != zc::none);
  }
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
  ZC_EXPECT(isUserBoundModule(identities, hirModule.module()));
  ZC_EXPECT(hirModule.declarations().size() == 0);
  ZC_EXPECT(hirModule.borrowEvidenceLease().key().revision.digest() ==
            hirModule.borrowEvidenceRevision().digest());
  ZC_IF_SOME(repository, session.getBorrowEvidenceRepository()) {
    const auto capability = repository.capability();
    const auto evidence = capability.lookup(hirModule.borrowEvidenceLease());
    ZC_REQUIRE(evidence.isResolved());
    ZC_EXPECT(evidence.evidence().revision().digest() ==
              hirModule.borrowEvidenceRevision().digest());
  }
  ZC_REQUIRE(session.getVerifiedBuiltMirModules().size() == 1);
  const auto& builtMir = session.getVerifiedBuiltMirModules()[0];
  ZC_EXPECT(isUserBoundModule(identities, builtMir.module()));
  ZC_EXPECT(builtMir.module() == hirModule.module());
  ZC_EXPECT(builtMir.functions().size() == 0);
  ZC_EXPECT(builtMir.canonicalFunctionRecords().size() == 0);
  ZC_EXPECT(builtMir.borrowEvidenceRevision().digest() ==
            hirModule.borrowEvidenceRevision().digest());
  ZC_IF_SOME(repository, session.getBorrowEvidenceRepository()) {
    const auto capability = repository.capability();
    const auto evidence = capability.lookup(builtMir.borrowEvidenceLease());
    ZC_REQUIRE(evidence.isResolved());
    ZC_EXPECT(evidence.evidence().revision().digest() ==
              builtMir.borrowEvidenceRevision().digest());
  }
  ZC_EXPECT(session.getIrFailureGroups().size() == 0);
  ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
}

ZC_TEST("Checker identity authority retains materialized modules after session teardown") {
  zc::Maybe<checker::CheckerIdentityAuthority> retainedAuthority;
  {
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
    installCore(session);

    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    retainedAuthority = session.materializeCheckerIdentityAuthority();
    ZC_REQUIRE(retainedAuthority != zc::none);
  }

  const auto& authority = ZC_REQUIRE_NONNULL(retainedAuthority);
  const auto& graph = authority.graphLease().capability();
  ZC_REQUIRE(authority.modules().size() == graph.modules().size());
  for (size_t index = 0; index < authority.modules().size(); ++index) {
    const auto& boundModule = authority.modules()[index];
    ZC_EXPECT(boundModule.module() == graph.modules()[index].handle());
    ZC_EXPECT(boundModule.tree().contains(boundModule.tree().root()));
  }
}

ZC_TEST("Module interfaces retain materialized module leases after session teardown") {
  zc::Maybe<module_graph_query::CheckerBoundModuleView> retainedBoundModule;
  {
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
    installCore(session);

    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    const auto interfaces = session.getVerifiedModuleInterfaces();
    ZC_REQUIRE(interfaces.size() == 1);
    retainedBoundModule = interfaces[0].retainBoundModule();
  }

  const auto& boundModule = ZC_REQUIRE_NONNULL(retainedBoundModule);
  ZC_EXPECT(boundModule.semanticContext().isValid());
  ZC_EXPECT(boundModule.tree().contains(boundModule.tree().root()));
  ZC_EXPECT(boundModule.parsedModule().tree().contains(boundModule.tree().root()));
}

ZC_TEST("CompilerSession retains empty prelude signature lineage after local shadowing") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots("app"_zc, "interface Copy {}\ninterface Linear {}\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
  {
    auto identities = checkerIdentityAuthority(session);
    const auto& bound = soleUserBoundModule(identities);
    const auto& boundView = checkerBoundModule(identities, bound.module());
    ZC_REQUIRE(boundView.preludeSurface() != zc::none);
    const auto& fingerprint = identities.fingerprint();
    const auto boundModuleIndices = dependencyOrderedBoundModuleIndices(identities);
    ZC_IF_SOME(constSemanticTypes, session.getSemanticTypeStore()) {
      auto& semanticTypes = const_cast<type::SemanticTypeStore&>(constSemanticTypes);
      zc::Vector<ownership::OwnershipAdmittedBoundModule> admittedMarkerModules(
          boundModuleIndices.size());
      zc::Vector<checker::signature::MarkerShapeModuleInput> markerInputs(
          boundModuleIndices.size());
      for (const auto candidateIndex : boundModuleIndices) {
        const auto& candidate = identities.modules()[candidateIndex];
        auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(
            checkerBoundModule(identities, candidate.module()).retain());
        ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
        admittedMarkerModules.add(zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>());
        markerInputs.add(checker::signature::MarkerShapeModuleInput{admittedMarkerModules.back()});
      }
      auto shapeResult = checker::signature::MarkerShapeInventoryBuilder::build(
          session.getSemanticContextBrand(), fingerprint, bound.module(), markerInputs.asPtr(),
          identities);
      ZC_REQUIRE(shapeResult.is<checker::signature::VerifiedMarkerShapeInventory>());
      auto shapes = zc::mv(shapeResult).get<checker::signature::VerifiedMarkerShapeInventory>();

      zc::Vector<identity::ModuleId> authorizedPreludeModules;
      ZC_IF_SOME(surface, bound.preludeSurface()) { authorizedPreludeModules.add(surface.module); }
      auto policyResult = checker::signature::MarkerPolicyRegistryBuilder::build(
          bound.module(), checker::signature::MarkerPolicyConfiguration::explicitOnly(), shapes,
          authorizedPreludeModules.asPtr(), identities);
      ZC_REQUIRE(policyResult.is<checker::signature::VerifiedMarkerPolicyRegistry>());
      auto policies = zc::mv(policyResult).get<checker::signature::VerifiedMarkerPolicyRegistry>();

      zc::Vector<checker::signature::VerifiedSignatureFacts> signatures;
      zc::Vector<checker::cross_module::ImportedSignatureView> importedViews;
      zc::Vector<VerifiedModuleInterface> interfaces;
      zc::Maybe<size_t> userModuleIndex;
      for (const auto candidateIndex : boundModuleIndices) {
        const auto& candidate = identities.modules()[candidateIndex];
        auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(
            checkerBoundModule(identities, candidate.module()).retain());
        ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
        auto admitted = zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>();
        auto signatureResult = checker::signature::SignatureFactsBuilder::build(
            checker::signature::SignatureFactsBuildInput{admitted, semanticTypes, shapes, policies,
                                                         identities});
        ZC_REQUIRE(signatureResult.is<checker::signature::VerifiedSignatureFacts>());
        signatures.add(zc::mv(signatureResult).get<checker::signature::VerifiedSignatureFacts>());

        zc::Vector<VerifiedInterfaceSource> interfaceSources(interfaces.size());
        for (const auto& interface : interfaces) {
          interfaceSources.add(VerifiedInterfaceSource(UserVerifiedInterfaceSource{interface}));
        }
        auto importedResult = ImportedSignatureViewProjector::build(
            admitted, interfaceSources.asPtr(), semanticTypes, identities);
        ZC_REQUIRE(importedResult != zc::none);
        ZC_IF_SOME(view, importedResult) { importedViews.add(zc::mv(view)); }

        const auto& moduleSignatures = signatures.back();
        const auto& imported = importedViews.back();
        auto borrowResult = checker::borrow::BorrowInterfaceBuilder::build(
            checker::borrow::BorrowInterfaceBuildInput{
                session.getSemanticContextBrand(), fingerprint, candidate.module(),
                moduleSignatures.revision(), imported.revision(), moduleSignatures.signatures(),
                zc::ArrayPtr<const checker::signature::SemanticSignature>(), identities,
                semanticTypes});
        ZC_REQUIRE(borrowResult.is<checker::borrow::VerifiedBorrowInterfaceSurface>());
        auto interfaceResult = ModuleInterfaceVerifier::build(ModuleInterfaceBuildInput{
            admitted, moduleSignatures, imported, policies,
            zc::mv(borrowResult).get<checker::borrow::VerifiedBorrowInterfaceSurface>(),
            semanticTypes, identities});
        ZC_REQUIRE(interfaceResult.is<VerifiedModuleInterface>());
        interfaces.add(zc::mv(interfaceResult).get<VerifiedModuleInterface>());
        if (candidate.module() == bound.module()) {
          ZC_REQUIRE(userModuleIndex == zc::none);
          userModuleIndex = importedViews.size() - 1;
        }
      }

      ZC_REQUIRE(userModuleIndex != zc::none);
      ZC_IF_SOME(index, userModuleIndex) {
        const auto& view = importedViews[index];
        ZC_EXPECT(view.requester() == bound.module());
        ZC_REQUIRE(view.modules().size() == 1);
        const auto& prelude = view.modules()[0];
        ZC_IF_SOME(surface, boundView.preludeSurface()) {
          ZC_EXPECT(prelude.sourceModule() == surface.module);
          const auto& surfaceRevision = prelude.bindingSurfaceRevision().variant();
          ZC_REQUIRE(surfaceRevision.is<module_interface::UserImportedBindingSurfaceRevision>());
          ZC_EXPECT(surfaceRevision.get<module_interface::UserImportedBindingSurfaceRevision>()
                        .value.digest() == surface.surface.revision().digest());
        }
        ZC_EXPECT(prelude.origin() == checker::cross_module::SignatureViewOrigin::Prelude);
        ZC_EXPECT(prelude.authorizedRoots().size() == 0);
        ZC_EXPECT(prelude.lookupDefinitions().size() == 0);
        ZC_EXPECT(prelude.supportDefinitions().size() == 0);
        ZC_EXPECT(prelude.moduleTargets().size() == 0);
      }
    }
  }
}

ZC_TEST("CompilerSession projects core prelude re-exports through the prelude surface") {
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  CompilerSession session(contextFactory, languageOptions, compilerOptions);
  auto registry = targetRegistry();
  auto input = VerifiedPackageSessionInput::from(
      request(registry), verifiedSelection(registry), verifiedSelection(registry),
      resolution(session.getPackageResolutionMemoryResource(), "app"_zc),
      resolvedSourceSnapshots(
          "app"_zc, "import core::prelude::{Copy, Linear};\n\nconst value: i32 = 1;\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());

  auto identities = checkerIdentityAuthority(session);
  const auto& bound = soleUserBoundModule(identities);
  const auto& boundView = checkerBoundModule(identities, bound.module());
  const auto views = session.getImportedSignatureViews();
  ZC_REQUIRE(views.size() == 1);
  ZC_REQUIRE(views[0].modules().size() == 1);
  const auto& prelude = views[0].modules()[0];
  ZC_REQUIRE(boundView.preludeSurface() != zc::none);
  ZC_IF_SOME(surface, boundView.preludeSurface()) {
    ZC_EXPECT(prelude.sourceModule() == surface.module);
    const auto& importedSurfaceRevision = prelude.bindingSurfaceRevision().variant();
    ZC_REQUIRE(importedSurfaceRevision
                   .is<module_interface::ToolchainCoreImportedBindingSurfaceRevision>());
    const auto& preludeRevision =
        importedSurfaceRevision.get<module_interface::ToolchainCoreImportedBindingSurfaceRevision>()
            .value;
    ZC_REQUIRE(prelude.authorizedRoots().size() == 2);
    ZC_REQUIRE(prelude.lookupDefinitions().size() == 2);
    for (const auto& root : prelude.authorizedRoots()) {
      ZC_EXPECT(root.sourceModule != prelude.sourceModule());
      const auto& rootSurfaceRevision = root.bindingSurfaceRevision.variant();
      ZC_REQUIRE(
          rootSurfaceRevision.is<module_interface::ToolchainCoreImportedBindingSurfaceRevision>());
      ZC_EXPECT(
          rootSurfaceRevision.get<module_interface::ToolchainCoreImportedBindingSurfaceRevision>()
              .value.digest() == preludeRevision.digest());
    }
  }
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
  installCore(session);
  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());
  auto identities = checkerIdentityAuthority(session);
  {
    ZC_REQUIRE(userBoundModuleCount(identities) == 1);
    ZC_REQUIRE(coreBoundModuleCount(identities) == 3);
    const auto& boundModule = soleUserBoundModule(identities);
    ZC_REQUIRE(session.checkSources());
    auto coreLibrary = core_library_test::materializeCoreLibrary(session, identities);
    ZC_REQUIRE(coreLibrary != zc::none);
    auto checkerBound = identities.boundModule(boundModule.module());
    ZC_REQUIRE(checkerBound != zc::none);
    const auto& boundView = ZC_REQUIRE_NONNULL(checkerBound);
    const auto& fingerprint = identities.fingerprint();
    const auto boundModuleIndices = dependencyOrderedBoundModuleIndices(identities);
    ZC_IF_SOME(constSemanticTypes, session.getSemanticTypeStore()) {
      auto& semanticTypes = const_cast<type::SemanticTypeStore&>(constSemanticTypes);
      zc::Vector<ownership::OwnershipAdmittedBoundModule> admittedShapeModules(
          boundModuleIndices.size());
      zc::Vector<checker::signature::MarkerShapeModuleInput> shapeInputs(boundModuleIndices.size());
      for (const auto candidateIndex : boundModuleIndices) {
        const auto& candidate = identities.modules()[candidateIndex];
        auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(
            checkerBoundModule(identities, candidate.module()).retain());
        ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
        admittedShapeModules.add(zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>());
        shapeInputs.add(checker::signature::MarkerShapeModuleInput{admittedShapeModules.back()});
      }
      auto shapeResult = checker::signature::MarkerShapeInventoryBuilder::build(
          session.getSemanticContextBrand(), fingerprint, boundModule.module(), shapeInputs.asPtr(),
          identities);
      ZC_REQUIRE(shapeResult.is<checker::signature::VerifiedMarkerShapeInventory>());
      auto shapes = zc::mv(shapeResult).get<checker::signature::VerifiedMarkerShapeInventory>();

      identity::DefId structuralMarker;
      zc::Maybe<identity::DefinitionKey> structuralMarkerKey;
      for (const auto& signatureDefinition : boundModule.definitions().definitions()) {
        if (signatureDefinition.record.name() == "Structural"_zc) {
          ZC_REQUIRE(!structuralMarker.isValid());
          auto authorityDefinition = identities.definition(signatureDefinition.key);
          ZC_REQUIRE(authorityDefinition != zc::none);
          ZC_IF_SOME(entry, authorityDefinition) { structuralMarker = entry.handle(); }
          structuralMarkerKey = signatureDefinition.key.clone();
        }
      }
      ZC_REQUIRE(structuralMarker.isValid());
      ZC_REQUIRE(structuralMarkerKey != zc::none);
      zc::Vector<checker::signature::MarkerPolicyConfigurationEntry> policyEntries;
      ZC_IF_SOME(key, structuralMarkerKey) {
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
            zc::mv(referenceRequirements), zc::Vector<type::semantic::Mutability>()});
      }
      auto configuration =
          checker::signature::MarkerPolicyConfiguration::from(zc::mv(policyEntries));
      ZC_REQUIRE(configuration != zc::none);
      zc::Vector<identity::ModuleId> authorizedPreludeModules;
      authorizedPreludeModules.add(boundModule.module());
      zc::Maybe<checker::signature::VerifiedMarkerPolicyRegistry> policies;
      ZC_IF_SOME(value, configuration) {
        auto policyResult = checker::signature::MarkerPolicyRegistryBuilder::build(
            boundModule.module(), value, shapes, authorizedPreludeModules.asPtr(), identities);
        ZC_REQUIRE(policyResult.is<checker::signature::VerifiedMarkerPolicyRegistry>());
        policies = zc::mv(policyResult).get<checker::signature::VerifiedMarkerPolicyRegistry>();
      }
      ZC_IF_SOME(policy, policies) {
        zc::Vector<checker::signature::VerifiedSignatureFacts> signatureFacts;
        zc::Vector<checker::cross_module::ImportedSignatureView> importedViews;
        zc::Vector<VerifiedModuleInterface> interfaces;
        zc::Maybe<size_t> userModuleIndex;
        for (const auto candidateIndex : boundModuleIndices) {
          const auto& candidate = identities.modules()[candidateIndex];
          auto admission = ownership::OwnershipSurfaceAdmissionBuilder::admit(
              checkerBoundModule(identities, candidate.module()).retain());
          ZC_REQUIRE(admission.is<ownership::OwnershipAdmittedBoundModule>());
          auto admitted = zc::mv(admission).get<ownership::OwnershipAdmittedBoundModule>();
          auto signatureResult = checker::signature::SignatureFactsBuilder::build(
              checker::signature::SignatureFactsBuildInput{admitted, semanticTypes, shapes, policy,
                                                           identities});
          ZC_REQUIRE(signatureResult.is<checker::signature::VerifiedSignatureFacts>());
          signatureFacts.add(
              zc::mv(signatureResult).get<checker::signature::VerifiedSignatureFacts>());

          zc::Vector<VerifiedInterfaceSource> interfaceSources(interfaces.size());
          for (const auto& interface : interfaces) {
            interfaceSources.add(VerifiedInterfaceSource(UserVerifiedInterfaceSource{interface}));
          }
          auto importedResult = ImportedSignatureViewProjector::build(
              admitted, interfaceSources.asPtr(), semanticTypes, identities);
          ZC_REQUIRE(importedResult != zc::none);
          ZC_IF_SOME(imported, importedResult) { importedViews.add(zc::mv(imported)); }
          const auto& signatures = signatureFacts.back();
          const auto& imported = importedViews.back();
          auto borrowResult = checker::borrow::BorrowInterfaceBuilder::build(
              checker::borrow::BorrowInterfaceBuildInput{
                  session.getSemanticContextBrand(), fingerprint, candidate.module(),
                  signatures.revision(), imported.revision(), signatures.signatures(),
                  zc::ArrayPtr<const checker::signature::SemanticSignature>(), identities,
                  semanticTypes});
          ZC_REQUIRE(borrowResult.is<checker::borrow::VerifiedBorrowInterfaceSurface>());
          auto interfaceResult = ModuleInterfaceVerifier::build(ModuleInterfaceBuildInput{
              admitted, signatures, imported, policy,
              zc::mv(borrowResult).get<checker::borrow::VerifiedBorrowInterfaceSurface>(),
              semanticTypes, identities});
          ZC_REQUIRE(interfaceResult.is<VerifiedModuleInterface>());
          interfaces.add(zc::mv(interfaceResult).get<VerifiedModuleInterface>());
          if (candidate.module() == boundModule.module()) {
            ZC_REQUIRE(userModuleIndex == zc::none);
            userModuleIndex = interfaces.size() - 1;
          }
        }
        ZC_REQUIRE(interfaces.size() == identities.modules().size());
        ZC_REQUIRE(signatureFacts.size() == interfaces.size());
        ZC_REQUIRE(importedViews.size() == interfaces.size());
        ZC_REQUIRE(userModuleIndex != zc::none);
        auto coherenceResult = CoherenceBuilder::build(
            CoherenceBuildInput{session.getSemanticContextBrand(), fingerprint, policy,
                                interfaces.asPtr(), identities});
        ZC_REQUIRE(coherenceResult.is<checker::coherence::CoherenceFrozen>());
        auto coherence = zc::mv(coherenceResult).get<checker::coherence::CoherenceFrozen>();

        ZC_IF_SOME(index, userModuleIndex) {
          const auto& signatures = signatureFacts[index];
          const auto& imported = importedViews[index];

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
          auto internedTuple =
              semanticTypes.intern(zc::mv(admittedTuple).get<type::semantic::CanonicalTypeData>());
          ZC_REQUIRE(internedTuple.is<type::SemanticTypeInterned>());
          const auto tuple = internedTuple.get<type::SemanticTypeInterned>().id;
          zc::Vector<type::semantic::ObjectFieldData> objectFields;
          objectFields.add(type::semantic::ObjectFieldData{
              scalar<identity::SemanticIdentifier>("field"_zc), i32,
              type::semantic::Mutability::Const, type::semantic::FieldPresence::Required});
          auto admittedObject = semanticTypes.canonicalizeClosed(
              type::semantic::TypeData(type::semantic::ObjectTypeData{zc::mv(objectFields)}));
          ZC_REQUIRE(admittedObject.is<type::semantic::CanonicalTypeData>());
          auto internedObject =
              semanticTypes.intern(zc::mv(admittedObject).get<type::semantic::CanonicalTypeData>());
          ZC_REQUIRE(internedObject.is<type::SemanticTypeInterned>());
          const auto object = internedObject.get<type::SemanticTypeInterned>().id;
          auto admittedArray = semanticTypes.canonicalizeClosed(
              type::semantic::TypeData(type::semantic::FixedArrayTypeData{i32, 4}));
          ZC_REQUIRE(admittedArray.is<type::semantic::CanonicalTypeData>());
          auto internedArray =
              semanticTypes.intern(zc::mv(admittedArray).get<type::semantic::CanonicalTypeData>());
          ZC_REQUIRE(internedArray.is<type::SemanticTypeInterned>());
          const auto array = internedArray.get<type::SemanticTypeInterned>().id;
          auto admittedReference = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
              type::semantic::ReferenceTypeData{type::semantic::Mutability::Const, i32}));
          ZC_REQUIRE(admittedReference.is<type::semantic::CanonicalTypeData>());
          auto internedReference = semanticTypes.intern(
              zc::mv(admittedReference).get<type::semantic::CanonicalTypeData>());
          ZC_REQUIRE(internedReference.is<type::SemanticTypeInterned>());
          const auto reference = internedReference.get<type::SemanticTypeInterned>().id;
          auto admittedMutableReference = semanticTypes.canonicalizeClosed(type::semantic::TypeData(
              type::semantic::ReferenceTypeData{type::semantic::Mutability::Mutable, i32}));
          ZC_REQUIRE(admittedMutableReference.is<type::semantic::CanonicalTypeData>());
          auto internedMutableReference = semanticTypes.intern(
              zc::mv(admittedMutableReference).get<type::semantic::CanonicalTypeData>());
          ZC_REQUIRE(internedMutableReference.is<type::SemanticTypeInterned>());
          const auto mutableReference =
              internedMutableReference.get<type::SemanticTypeInterned>().id;

          auto inventoryResult =
              checker::body::BodyFactRequirementInventoryBuilder::build(boundView);
          ZC_REQUIRE(inventoryResult.is<checker::body::VerifiedBodyFactRequirementInventory>());
          auto inventory =
              zc::mv(inventoryResult).get<checker::body::VerifiedBodyFactRequirementInventory>();
          auto crate = identities.crate(boundModule.crate());
          ZC_REQUIRE(crate != zc::none);
          ZC_IF_SOME(value, crate) {
            checker::body::BodyCheckingInput bodyInput{
                boundView.retain(),
                identities,
                policy,
                ZC_REQUIRE_NONNULL(coreLibrary).authorityLease().capability().authority(),
                signatures,
                imported,
                coherence.view,
                semanticTypes,
                inventory,
                value.key().semanticOptions()};
            auto proofInput = checker::marker::MarkerProofInput::from(bodyInput);
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
              ZC_EXPECT(referenceEvidence.components[0].supportingFact.marker == structuralMarker);
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
  installCore(session);

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
  auto overlayInput =
      session.getOwnershipEventOverlayInput(session.getVerifiedHirModules()[0].module());
  ZC_REQUIRE(overlayInput != zc::none);
  ZC_IF_SOME(input, overlayInput) {
    const mir::BuiltMirInput mirInput{session.getVerifiedHirModules()[0], input.body};
    auto codecCandidate = mir::BuiltMirBuilder::build(mirInput);
    ZC_REQUIRE(codecCandidate.isVerified());
    auto corruptedCodec = zc::mv(codecCandidate).takeVerified();
    ZC_REQUIRE(corruptedCodec.canonicalFunctions.size() == 1);
    ZC_REQUIRE(corruptedCodec.canonicalFunctions[0].size() != 0);
    corruptedCodec.canonicalFunctions[0][0] ^= 0x01;
    auto codecRejected = mir::BuiltMirVerifier::verify(zc::mv(corruptedCodec), mirInput);
    ZC_REQUIRE(codecRejected.isIrInvariantRejected());
    ZC_REQUIRE(codecRejected.invariantFailures().facts().size() == 1);
    ZC_EXPECT(codecRejected.invariantFailures().facts()[0].kind() ==
              ir::IrFailureKind::CanonicalCodecMismatch);

    auto structureCandidate = mir::BuiltMirBuilder::build(mirInput);
    ZC_REQUIRE(structureCandidate.isVerified());
    auto corruptedStructure = zc::mv(structureCandidate).takeVerified();
    ZC_REQUIRE(corruptedStructure.functions.size() == 1);
    ZC_REQUIRE(corruptedStructure.functions[0].locals.size() == 1);
    corruptedStructure.functions[0].locals[0].id = mir::MirLocalId();
    auto structureRejected = mir::BuiltMirVerifier::verify(zc::mv(corruptedStructure), mirInput);
    ZC_REQUIRE(structureRejected.isIrInvariantRejected());
    ZC_REQUIRE(structureRejected.invariantFailures().facts().size() == 1);
    ZC_EXPECT(structureRejected.invariantFailures().facts()[0].kind() ==
              ir::IrFailureKind::InvalidFact);

    auto markerCandidate = mir::BuiltMirBuilder::build(mirInput);
    ZC_REQUIRE(markerCandidate.isVerified());
    auto corruptedMarker = zc::mv(markerCandidate).takeVerified();
    auto& returnTerminator = corruptedMarker.functions[0].blocks[0].terminator;
    ZC_REQUIRE(returnTerminator.returnValue().value != zc::none);
    ZC_IF_SOME(value, returnTerminator.returnValue().value) {
      returnTerminator = mir::MirTerminator::returnValue(
          mir::MirOperand::move(value.place().clone()), returnTerminator.sourceSpan().clone());
    }
    auto markerRejected = mir::BuiltMirVerifier::verify(zc::mv(corruptedMarker), mirInput);
    ZC_REQUIRE(markerRejected.isIrInvariantRejected());
    ZC_REQUIRE(markerRejected.invariantFailures().facts().size() == 1);
    ZC_EXPECT(markerRejected.invariantFailures().facts()[0].kind() ==
              ir::IrFailureKind::InvalidFact);
    ZC_EXPECT(session.getIrFailureGroups().size() == 0);
    ZC_EXPECT(session.getIrIdentityInvariantFailures().size() == 0);
  }
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
  installCore(session);

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

ZC_TEST("CompilerSession lowers a sequential local copy through HIR and Built MIR") {
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
          "fun answer() -> i32 { let first = 1; let second = first; return second; }"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_REQUIRE(session.bindSources());
  ZC_REQUIRE(session.checkSources());
  ZC_REQUIRE(!session.getDiagnosticEngine().hasErrors());

  ZC_REQUIRE(session.getVerifiedHirModules().size() == 1);
  const auto& hirModule = session.getVerifiedHirModules()[0];
  ZC_REQUIRE(hirModule.functions().size() == 1);
  ZC_REQUIRE(hirModule.blocks().size() == 1);
  ZC_REQUIRE(hirModule.locals().size() == 2);
  ZC_REQUIRE(hirModule.localReferences().size() == 2);
  ZC_REQUIRE(hirModule.expressions().size() == 1);
  const auto& block = hirModule.blocks()[0];
  ZC_REQUIRE(block.statements.size() == 3);
  ZC_EXPECT(block.statements[0] == hirModule.locals()[0].node);
  ZC_EXPECT(block.statements[1] == hirModule.locals()[1].node);
  ZC_EXPECT(hirModule.locals()[0].local.ordinal() == 1);
  ZC_EXPECT(hirModule.locals()[1].local.ordinal() == 2);
  ZC_EXPECT(hirModule.localReferences()[0].local == hirModule.locals()[0].local);
  ZC_EXPECT(hirModule.localReferences()[1].local == hirModule.locals()[1].local);

  ZC_REQUIRE(session.getVerifiedBuiltMirModules().size() == 1);
  const auto& builtMir = session.getVerifiedBuiltMirModules()[0];
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_REQUIRE(function.locals.size() == 2);
  ZC_EXPECT(function.locals[0].id.ordinal() == 1);
  ZC_EXPECT(function.locals[1].id.ordinal() == 2);
  ZC_REQUIRE(function.blocks.size() == 1);
  const auto& mirBlock = function.blocks[0];
  ZC_REQUIRE(mirBlock.statements.size() == 4);
  ZC_EXPECT(mirBlock.statements[0].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(mirBlock.statements[1].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(mirBlock.statements[2].kind() == mir::MirStatementKind::StorageLive);
  ZC_EXPECT(mirBlock.statements[3].kind() == mir::MirStatementKind::Assign);
  ZC_EXPECT(mirBlock.statements[3].assignmentValue().value.kind() == mir::MirRvalueKind::Use);
  ZC_EXPECT(mirBlock.statements[3].assignmentValue().value.useValue().operand.kind() ==
            mir::MirOperandKind::Copy);
  ZC_EXPECT(mirBlock.terminator.kind() == mir::MirTerminatorKind::Return);
  const auto ownershipInputs = session.getVerifiedOwnershipInputs();
  ZC_REQUIRE(ownershipInputs.size() == 1);
  ZC_EXPECT(ownershipInputs[0].module() == hirModule.module());
  ZC_EXPECT(ownershipInputs[0].builtRevision().digest() == builtMir.revision().digest());
  ZC_EXPECT(ownershipInputs[0].resources().functions().size() == 1);
  ZC_EXPECT(ownershipInputs[0].resources().functions()[0].facts.size() == 0);
  ZC_EXPECT(ownershipInputs[0].resources().functions()[0].transfers.size() == 0);
  auto overlayInput = session.getOwnershipEventOverlayInput(hirModule.module());
  ZC_REQUIRE(overlayInput != zc::none);
  ZC_IF_SOME(input, overlayInput) {
    const mir::BuiltMirInput mirInput{hirModule, input.body};
    auto candidate = mir::BuiltMirBuilder::build(mirInput);
    ZC_REQUIRE(candidate.isVerified());
    auto corrupted = zc::mv(candidate).takeVerified();
    ZC_REQUIRE(corrupted.functions.size() == 1);
    ZC_REQUIRE(corrupted.functions[0].blocks.size() == 1);
    auto& statements = corrupted.functions[0].blocks[0].statements;
    ZC_REQUIRE(statements.size() == 4);
    statements[3] = statements[1].clone();
    auto rejected = mir::BuiltMirVerifier::verify(zc::mv(corrupted), mirInput);
    ZC_REQUIRE(rejected.isIrInvariantRejected());
    ZC_REQUIRE(rejected.invariantFailures().facts().size() == 1);
    ZC_EXPECT(rejected.invariantFailures().facts()[0].kind() == ir::IrFailureKind::InvalidFact);
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
  installCore(session);

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

ZC_TEST("CompilerSession rejects unadmitted frontend syntax before Checker publication") {
  const auto rejects = [](zc::StringPtr sourceText, diagnostics::DiagID expectedDiagnostic,
                          size_t expectedDiagnosticCount) {
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
    installCore(session);

    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_EXPECT(!session.checkSources());
    ZC_EXPECT(session.getCheckerInvariantFailures().size() == 0);
    ZC_REQUIRE(captured.ids.size() == expectedDiagnosticCount);
    for (const auto id : captured.ids) { ZC_EXPECT(id == expectedDiagnostic); }
    ZC_EXPECT(captured.unmanagedPrimaryLocations == 0);
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
  };

  rejects("fun entry() { spawn {}; }"_zc, diagnostics::DiagID::ConcurrencySemanticsUnavailable, 1);
  rejects("fun entry() { suspend; }"_zc, diagnostics::DiagID::ConcurrencySemanticsUnavailable, 1);
  rejects("fun entry() { suspend; spawn {}; }"_zc,
          diagnostics::DiagID::ConcurrencySemanticsUnavailable, 2);
  rejects("fun entry() { return; }"_zc, diagnostics::DiagID::VoidReturnSemanticsUnavailable, 1);
  rejects("fun entry() { 1; }"_zc, diagnostics::DiagID::ExpressionStatementSemanticsUnavailable, 1);
  rejects("fun entry() -> i32 { mut value = 1; value = value; return value; }"_zc,
          diagnostics::DiagID::ExpressionStatementSemanticsUnavailable, 1);
  rejects("fun entry() {}"_zc, diagnostics::DiagID::FunctionBodySemanticsUnavailable, 1);
  rejects("fun entry() -> i32 { let value = 1; }"_zc,
          diagnostics::DiagID::FunctionBodySemanticsUnavailable, 1);
  rejects("fun entry() -> i32 { let first = 1; let second = 2; return first; }"_zc,
          diagnostics::DiagID::FunctionBodySemanticsUnavailable, 1);
  rejects("fun entry() -> i32 { return 1 + 2; }"_zc,
          diagnostics::DiagID::FunctionBodySemanticsUnavailable, 1);
  rejects("fun entry() -> i32 { return unsafe { 1 }; }"_zc,
          diagnostics::DiagID::ExpressionStatementSemanticsUnavailable, 1);
  rejects("fun entry() -> i64 { return 1 as i64; }"_zc,
          diagnostics::DiagID::FunctionBodySemanticsUnavailable, 1);
  rejects(
      "class Cell { value: i32 }\n"
      "fun entry() -> i32 { let cell = Cell { value: 0 }; return cell.value; }"_zc,
      diagnostics::DiagID::FunctionBodySemanticsUnavailable, 1);
  rejects("fun entry() -> i32 { if (true) { return 1; } else { return 2; } }"_zc,
          diagnostics::DiagID::ControlFlowSemanticsUnavailable, 1);
  rejects("fun entry() -> i32 { while (false) { return 1; } return 2; }"_zc,
          diagnostics::DiagID::ControlFlowSemanticsUnavailable, 1);
  rejects("fun entry() { for (;;) { break; } }"_zc,
          diagnostics::DiagID::ControlFlowSemanticsUnavailable, 2);
  rejects("fun entry() { label: while (true) { continue label; } }"_zc,
          diagnostics::DiagID::ControlFlowSemanticsUnavailable, 3);
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
  installCore(session);

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
  installCore(session);

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
    installCore(session);

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
  installCore(session);

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
  installCore(session);

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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_REQUIRE(session.parseSources());
  ZC_EXPECT(userParsedModuleCount(session) == 2);
  ZC_EXPECT(coreParsedModuleCount(session) == 3);
  auto graphLease = session.materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  ZC_EXPECT(userGraphModuleCount(graph) == 2);
  ZC_EXPECT(coreGraphModuleCount(graph) == 3);
  ZC_EXPECT(userGraphEdgeCount(graph, identity::ModuleDependencyKind::Import) == 1);
  ZC_EXPECT(userGraphEdgeCount(graph, identity::ModuleDependencyKind::Prelude) == 2);
  ZC_EXPECT(coreGraphEdgeCount(graph) == 1);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_EXPECT(session.hasVerifiedParsedSyntax());
  ZC_EXPECT(session.materializeParsedModules() == zc::none);
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::ModuleDeclarationNameMismatch);
  ZC_EXPECT(captured.unmanagedPrimaryLocations == 0);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_EXPECT(session.hasVerifiedParsedSyntax());
  ZC_EXPECT(session.materializeParsedModules() == zc::none);
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
  installCore(session);

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
    ZC_EXPECT(graph.edges()[0].provider().unit().userPackage().name() == "math"_zc);
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
  ZC_REQUIRE(userParsedModuleCount(session) == 2);
  ZC_REQUIRE(coreParsedModuleCount(session) == 3);
  auto checkerAuthority = session.materializeCheckerIdentityAuthority();
  ZC_REQUIRE(checkerAuthority != zc::none);
  const auto& actualFingerprint = ZC_REQUIRE_NONNULL(checkerAuthority).fingerprint();
  auto graphLease = session.materializeModuleGraph();
  ZC_REQUIRE(graphLease != zc::none);
  const auto& graph = ZC_REQUIRE_NONNULL(graphLease).capability();
  ZC_EXPECT(userGraphModuleCount(graph) == 2);
  ZC_EXPECT(coreGraphModuleCount(graph) == 3);
  ZC_EXPECT(actualFingerprint.digest() != identity::Sha256Digest());
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
    installCore(session);
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
    installCore(session);
    ZC_REQUIRE(session.getVerifiedCrateGraph() != zc::none);
    ZC_IF_SOME(graph, session.getVerifiedCrateGraph()) {
      ZC_EXPECT(graph.crates().size() == 2);
      ZC_REQUIRE(graph.edges().size() == 1);
      ZC_EXPECT(graph.edges()[0].origin().kind() ==
                identity::CrateDependencyOriginKind::UserPackage);
      ZC_EXPECT(graph.edges()[0].origin().userPackageEdge().domain() ==
                identity::DependencyDomain::Development);
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
  installCore(session);

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
      resolvedSourceSnapshots("app"_zc, "fun value() {}\nfun value() {}\n"_zc));
  ZC_REQUIRE(input != zc::none);
  ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::RedeclareFunction);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::DuplicateIdentifier);
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
  installCore(session);

  const auto roots = session.getFinalizedCompilationRoots();
  ZC_REQUIRE(roots.size() == 1);
  ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
  ZC_EXPECT(!session.parseSources());
  ZC_REQUIRE(captured.ids.size() == 1);
  ZC_EXPECT(captured.ids[0] == diagnostics::DiagID::ConstantExpressionNotAllowed);
}

}  // namespace zomlang::compiler::driver
