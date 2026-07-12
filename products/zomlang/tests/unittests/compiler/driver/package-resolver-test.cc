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

#include "zomlang/compiler/driver/package/package-resolver.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/package/source-record.h"
#include "zomlang/compiler/identity/canonical-encoder.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::CanonicalWorkspaceRelativePath workspacePath(uint32_t parents,
                                                       zc::StringPtr segment = {}) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  if (segment.size() != 0) {
    auto value = identity::CanonicalPathSegment::fromCanonical(segment);
    ZC_IF_SOME(admitted, value) { segments.add(zc::mv(admitted)); }
  }
  return identity::CanonicalWorkspaceRelativePath::from(parents, zc::mv(segments));
}

identity::CanonicalRelativePath sourcePath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto directory = identity::CanonicalPathSegment::fromCanonical("src"_zc);
  auto file = identity::CanonicalPathSegment::fromCanonical("lib.zom"_zc);
  ZC_IF_SOME(value, directory) { segments.add(zc::mv(value)); }
  ZC_IF_SOME(value, file) { segments.add(zc::mv(value)); }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

NormalizedManifest parseManifest(zc::StringPtr text, bool includeLibrary = true) {
  zc::Vector<identity::CanonicalRelativePath> files;
  if (includeLibrary) { files.add(sourcePath()); }
  zc::Vector<identity::CanonicalPathSegment> mainSegments;
  auto mainDirectory = identity::CanonicalPathSegment::fromCanonical("src"_zc);
  auto mainFile = identity::CanonicalPathSegment::fromCanonical("main.zom"_zc);
  ZC_IF_SOME(value, mainDirectory) { mainSegments.add(zc::mv(value)); }
  ZC_IF_SOME(value, mainFile) { mainSegments.add(zc::mv(value)); }
  files.add(identity::CanonicalRelativePath::from(zc::mv(mainSegments)));
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_IF_SOME(inventoryValue, inventory) {
    ManifestParser parser;
    auto result =
        parser.parseWorkspaceManifest(workspacePath(0, "Zom.toml"_zc), text, inventoryValue);
    if (result.is<NormalizedManifest>()) { return zc::mv(result.get<NormalizedManifest>()); }
  }
  ZC_FAIL_REQUIRE("valid package resolver manifest fixture was rejected");
}

identity::PackageBaseKey base(zc::StringPtr name, zc::StringPtr version, uint32_t parents,
                              zc::StringPtr path = {}) {
  auto packageName = identity::PackageName::fromCanonical(name);
  auto packageVersion = identity::ResolvedVersion::fromCanonical(version);
  ZC_IF_SOME(nameValue, packageName) {
    ZC_IF_SOME(versionValue, packageVersion) {
      return identity::PackageBaseKey::from(
          identity::CanonicalPackageSource::localPath(workspacePath(parents, path)),
          zc::mv(nameValue), zc::mv(versionValue));
    }
  }
  ZC_FAIL_REQUIRE("invalid package resolver base fixture");
}

identity::SortedFeatureSet noFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(admitted, result) { return zc::mv(admitted); }
  ZC_FAIL_REQUIRE("empty feature set was rejected");
}

class MemoryFreshDirectory final : public FreshSourceDirectory {
public:
  MemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MemoryFreshDirectoryFactory final : public FreshSourceDirectoryFactory {
public:
  FreshSourceDirectoryResult create() override {
    zc::Own<FreshSourceDirectory> result = zc::heap<MemoryFreshDirectory>();
    return zc::mv(result);
  }
};

DigestVerifiedSourceSnapshot snapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "lib.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("let library = 0;"_zc);
  sourceDirectory->openFile(zc::Path({"src"_zc, "main.zom"_zc}), zc::WriteMode::CREATE)
      ->writeAll("let main = 0;"_zc);
  MemoryFreshDirectoryFactory factory;
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<DigestVerifiedSourceSnapshot>());
}

ResolverRelease release(zc::StringPtr name, zc::StringPtr version, uint32_t parents,
                        zc::StringPtr path, zc::StringPtr manifestText,
                        bool includeLibrary = true) {
  auto manifest = parseManifest(manifestText, includeLibrary);
  auto sourceSnapshot = snapshot();
  auto record = LocalPackageRecord::from(base(name, version, parents, path), zc::mv(manifest),
                                         sourceSnapshot);
  ZC_IF_SOME(value, record) { return ResolverRelease::fromLocal(value); }
  ZC_FAIL_REQUIRE("verified local package resolver fixture was rejected");
}

struct PermutationEntry final {
  identity::Sha256Digest key;
  size_t index;
};

zc::Vector<ResolverRelease> permute(uint64_t seed, zc::ArrayPtr<const ResolverRelease> releases) {
  zc::Vector<PermutationEntry> order;
  for (size_t index = 0; index < releases.size(); ++index) {
    identity::Sha256Hasher hasher;
    ZC_REQUIRE(hasher.update("zom.permutation.v0"_zc.asBytes()));
    const uint8_t separator = 0;
    ZC_REQUIRE(hasher.update(zc::arrayPtr(separator)));
    uint8_t seedBytes[8];
    for (size_t byte = 0; byte < 8; ++byte) {
      seedBytes[byte] = static_cast<uint8_t>(seed >> ((7 - byte) * 8));
    }
    ZC_REQUIRE(hasher.update(zc::arrayPtr(seedBytes)));
    identity::CanonicalEncoder encoder;
    releases[index].encode(encoder);
    ZC_REQUIRE(hasher.update(encoder.finish().asPtr()));
    auto digest = hasher.finish();
    ZC_IF_SOME(value, digest) { order.add(PermutationEntry{value, index}); }
  }
  for (size_t index = 1; index < order.size(); ++index) {
    const auto current = order[index];
    size_t insertion = index;
    while (insertion != 0 && current.key.bytes() < order[insertion - 1].key.bytes()) {
      order[insertion] = order[insertion - 1];
      --insertion;
    }
    order[insertion] = current;
  }
  zc::Vector<ResolverRelease> result;
  for (const auto& entry : order) { result.add(releases[entry.index].clone()); }
  return result;
}

constexpr zc::StringPtr kRootManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
math = { path = "../math", version = "^1.0.0", features = ["fast"] }
)toml"_zc;

constexpr zc::StringPtr kMath12 = R"toml([package]
name = "math"
version = "1.2.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[features]
fast = []
)toml"_zc;

constexpr zc::StringPtr kMath13 = R"toml([package]
name = "math"
version = "1.3.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[features]
fast = []
)toml"_zc;

}  // namespace

ZC_TEST("PackageResolverTest.SelectsGreatestEligibleReleaseAndEmitsGraph") {
  zc::Vector<ResolverRelease> releases;
  releases.add(release("math"_zc, "1.2.0"_zc, 1, "math"_zc, kMath12));
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, kRootManifest));
  releases.add(release("math"_zc, "1.3.0"_zc, 1, "math"_zc, kMath13));
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));

  auto result = PackageResolver::resolve(roots, releases);
  ZC_REQUIRE(result.is<ResolutionOutput>());
  const auto& resolution = result.get<ResolutionOutput>();
  ZC_REQUIRE(resolution.packages().size() == 2);
  ZC_EXPECT(resolution.packages()[0].key().name() == "app"_zc);
  ZC_EXPECT(resolution.packages()[1].key().name() == "math"_zc);
  ZC_EXPECT(resolution.packages()[1].key().version() == "1.3.0"_zc);
  ZC_REQUIRE(resolution.featureSets().size() == 2);
  ZC_EXPECT(resolution.featureSets()[1].domain() == FeatureActivationDomain::Target);
  ZC_REQUIRE(resolution.featureSets()[1].features().size() == 1);
  ZC_EXPECT(resolution.featureSets()[1].features()[0].text() == "fast"_zc);
  ZC_EXPECT(resolution.lockGraph().packages().size() == resolution.packages().size());
  ZC_EXPECT(resolution.edges().size() == 1);
  const auto encoded = resolution.encode();
  const auto domain = "zom.resolution-output.v0"_zc.asBytes();
  ZC_REQUIRE(encoded.size() > domain.size());
  for (size_t index = 0; index < domain.size(); ++index) {
    ZC_EXPECT(encoded[index] == domain[index]);
  }
  ZC_EXPECT(encoded[domain.size()] == 0);
  auto outputDigest = identity::sha256(encoded);
  ZC_IF_SOME(value, outputDigest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) ==
              "21640c513f23e7b7e9c51c02d8303ee8d035a0ca6840f05b4428c1351327f9f8"_zc);
  }
}

ZC_TEST("PackageResolverTest.IsInvariantToReleaseEnumeration") {
  zc::Vector<ResolverRelease> first;
  first.add(release("app"_zc, "1.0.0"_zc, 0, {}, kRootManifest));
  first.add(release("math"_zc, "1.2.0"_zc, 1, "math"_zc, kMath12));
  first.add(release("math"_zc, "1.3.0"_zc, 1, "math"_zc, kMath13));
  zc::Vector<ResolverRelease> second;
  second.add(first[2].clone());
  second.add(first[0].clone());
  second.add(first[1].clone());
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
  auto left = PackageResolver::resolve(roots, first);
  auto right = PackageResolver::resolve(roots, second);
  ZC_REQUIRE(left.is<ResolutionOutput>());
  ZC_REQUIRE(right.is<ResolutionOutput>());
  ZC_EXPECT(left.get<ResolutionOutput>().encode().asPtr() ==
            right.get<ResolutionOutput>().encode().asPtr());
}

ZC_TEST("PackageResolverTest.MatchesAllCanonicalPermutationSeeds") {
  zc::Vector<ResolverRelease> releases;
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, kRootManifest));
  releases.add(release("math"_zc, "1.2.0"_zc, 1, "math"_zc, kMath12));
  releases.add(release("math"_zc, "1.3.0"_zc, 1, "math"_zc, kMath13));
  zc::Array<uint8_t> expected;
  for (uint64_t seed = 0; seed < 256; ++seed) {
    auto input = permute(seed, releases);
    zc::Vector<ResolverRoot> roots;
    roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
    auto result = PackageResolver::resolve(roots, input);
    ZC_REQUIRE(result.is<ResolutionOutput>());
    auto encoded = result.get<ResolutionOutput>().encode();
    if (seed == 0) {
      expected = zc::mv(encoded);
    } else {
      ZC_EXPECT(encoded.asPtr() == expected.asPtr());
    }
  }
}

ZC_TEST("PackageResolverTest.ProducesCanonicalConflictExplanation") {
  constexpr zc::StringPtr conflictRoot = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
math = { path = "../math", version = ">=2.0.0" }
)toml"_zc;
  zc::Vector<ResolverRelease> releases;
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, conflictRoot));
  releases.add(release("math"_zc, "1.3.0"_zc, 1, "math"_zc, kMath13));
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
  auto result = PackageResolver::resolve(roots, releases);
  ZC_REQUIRE(result.is<PackageResolverFailure>());
  const auto& failed = result.get<PackageResolverFailure>();
  ZC_EXPECT(failed.issue() == ResolverIssue::NoVersionSatisfiesConstraints);
  ZC_EXPECT(failed.causes().size() == 1);
  ZC_REQUIRE(failed.hasIncompatibilityGraph());
  ZC_EXPECT(failed.incompatibilityGraph().records().size() == 3);
  ZC_EXPECT(failed.incompatibilityGraph().encode().size() != 0);
  auto graphDigest = identity::sha256(failed.incompatibilityGraph().encode());
  ZC_IF_SOME(value, graphDigest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) ==
              "f9baa53243bb4c42f9700484b2af10593b2e8ea609213d9b01618ba8882d2a35"_zc);
  }
  ZC_EXPECT(failed.encode().size() != 0);
}

ZC_TEST("PackageResolverTest.RejectsStaleLockedPackageRecord") {
  zc::Vector<ResolverRelease> releases;
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, kRootManifest));
  releases.add(release("math"_zc, "1.3.0"_zc, 1, "math"_zc, kMath13));
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
  auto current = PackageResolver::resolve(roots, releases);
  ZC_REQUIRE(current.is<ResolutionOutput>());

  zc::Vector<ResolverRelease> staleReleases;
  staleReleases.add(release("app"_zc, "1.0.0"_zc, 0, {}, kRootManifest));
  staleReleases.add(release("math"_zc, "1.2.0"_zc, 1, "math"_zc, kMath12));
  LockReplayMetrics metrics;
  auto replayed = PackageResolver::resolveLocked(
      roots, staleReleases, current.get<ResolutionOutput>().lockGraph(), metrics);
  ZC_REQUIRE(replayed.is<PackageResolverFailure>());
  ZC_EXPECT(replayed.get<PackageResolverFailure>().issue() == ResolverIssue::LockInputMismatch);
  ZC_EXPECT(metrics.solverInvocations == 0);
}

ZC_TEST("PackageResolverTest.SeparatesTargetAndBuildFeatureDomains") {
  constexpr zc::StringPtr rootManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
math = { path = "../math", features = ["fast"] }

[build-dependencies]
build_math = { package = "math", path = "../math", features = ["safe"] }
)toml"_zc;
  constexpr zc::StringPtr providerManifest = R"toml([package]
name = "math"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[features]
fast = []
safe = []
)toml"_zc;
  zc::Vector<ResolverRelease> releases;
  releases.add(release("math"_zc, "1.0.0"_zc, 1, "math"_zc, providerManifest));
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, rootManifest));
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
  auto result = PackageResolver::resolve(roots, releases);
  ZC_REQUIRE(result.is<ResolutionOutput>());
  const auto& featureSets = result.get<ResolutionOutput>().featureSets();
  ZC_REQUIRE(result.get<ResolutionOutput>().packages().size() == 3);
  ZC_REQUIRE(featureSets.size() == 3);
  bool foundTarget = false;
  bool foundBuild = false;
  for (const auto& featureSet : featureSets) {
    if (featureSet.base().name() != "math"_zc) { continue; }
    ZC_REQUIRE(featureSet.features().size() == 1);
    if (featureSet.domain() == FeatureActivationDomain::Target) {
      foundTarget = true;
      ZC_EXPECT(featureSet.features()[0].text() == "fast"_zc);
    } else {
      foundBuild = true;
      ZC_EXPECT(featureSet.features()[0].text() == "safe"_zc);
    }
  }
  ZC_EXPECT(foundTarget);
  ZC_EXPECT(foundBuild);
  ZC_EXPECT(result.get<ResolutionOutput>().edges().size() == 2);
  LockReplayMetrics replayMetrics;
  auto replayed = PackageResolver::resolveLocked(
      roots, releases, result.get<ResolutionOutput>().lockGraph(), replayMetrics);
  ZC_REQUIRE(replayed.is<ResolutionOutput>());
  ZC_EXPECT(replayMetrics.solverInvocations == 0);
  ZC_EXPECT(replayed.get<ResolutionOutput>().encode().asPtr() ==
            result.get<ResolutionOutput>().encode().asPtr());
}

ZC_TEST("PackageResolverTest.RejectsDependencyProviderWithoutLibrary") {
  constexpr zc::StringPtr binaryProvider = R"toml([package]
name = "math"
version = "1.3.0"
edition = "2026"

[[bin]]
name = "math"
path = "src/main.zom"

[features]
fast = []
)toml"_zc;
  zc::Vector<ResolverRelease> releases;
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, kRootManifest));
  releases.add(release("math"_zc, "1.3.0"_zc, 1, "math"_zc, binaryProvider, false));
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
  auto result = PackageResolver::resolve(roots, releases);
  ZC_REQUIRE(result.is<PackageResolverFailure>());
  ZC_EXPECT(result.get<PackageResolverFailure>().issue() ==
            ResolverIssue::DependencyLibraryTargetMissing);
}

ZC_TEST("PackageResolverTest.RejectsCanonicalDependencyCycle") {
  constexpr zc::StringPtr rootManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
math = { path = "../math" }
)toml"_zc;
  constexpr zc::StringPtr providerManifest = R"toml([package]
name = "math"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
app = { path = "../app" }
)toml"_zc;
  zc::Vector<ResolverRelease> releases;
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, rootManifest));
  releases.add(release("math"_zc, "1.0.0"_zc, 1, "math"_zc, providerManifest));
  releases.add(release("app"_zc, "1.0.0"_zc, 1, "app"_zc, rootManifest));
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
  auto result = PackageResolver::resolve(roots, releases);
  ZC_REQUIRE(result.is<PackageResolverFailure>());
  ZC_EXPECT(result.get<PackageResolverFailure>().issue() == ResolverIssue::DependencyCycle);
}

ZC_TEST("PackageResolverTest.BacktracksFromHighestConflictingRelease") {
  constexpr zc::StringPtr rootManifest = R"toml([package]
name = "app"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
solver = { path = "../solver", version = "^1.0.0" }
)toml"_zc;
  constexpr zc::StringPtr solver10 = R"toml([package]
name = "solver"
version = "1.0.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
util = { path = "../util", version = "^1.0.0" }
)toml"_zc;
  constexpr zc::StringPtr solver11 = R"toml([package]
name = "solver"
version = "1.1.0"
edition = "2026"

[lib]
path = "src/lib.zom"

[dependencies]
util = { path = "../util", version = ">=2.0.0" }
)toml"_zc;
  constexpr zc::StringPtr util15 = R"toml([package]
name = "util"
version = "1.5.0"
edition = "2026"

[lib]
path = "src/lib.zom"
)toml"_zc;
  zc::Vector<ResolverRelease> releases;
  releases.add(release("solver"_zc, "1.1.0"_zc, 1, "solver"_zc, solver11));
  releases.add(release("util"_zc, "1.5.0"_zc, 1, "util"_zc, util15));
  releases.add(release("app"_zc, "1.0.0"_zc, 0, {}, rootManifest));
  releases.add(release("solver"_zc, "1.0.0"_zc, 1, "solver"_zc, solver10));
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(base("app"_zc, "1.0.0"_zc, 0), noFeatures(), false, false));
  auto result = PackageResolver::resolve(roots, releases);
  ZC_REQUIRE(result.is<ResolutionOutput>());
  const auto& packages = result.get<ResolutionOutput>().packages();
  ZC_REQUIRE(packages.size() == 3);
  bool foundSolver = false;
  for (const auto& package : packages) {
    if (package.key().name() != "solver"_zc) { continue; }
    foundSolver = true;
    ZC_EXPECT(package.key().version() == "1.0.0"_zc);
  }
  ZC_EXPECT(foundSolver);
}

}  // namespace zomlang::compiler::driver::package
