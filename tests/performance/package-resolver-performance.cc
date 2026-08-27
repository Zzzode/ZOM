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

#include "zc/core/filesystem.h"
#include "zc/core/string-tree.h"
#include "zc/ztest/test.h"
#include "compiler/driver/package/lockfile.h"
#include "compiler/driver/package/package-resolver.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/crypto/sha256.h"

namespace zomlang::compiler::driver::package {
namespace {

constexpr uint32_t kPackageCount = 10'000;
constexpr uint32_t kReleaseCount = 4;
constexpr uint32_t kEdgeCount = 50'000;
constexpr uint8_t kFixtureDigest[32] = {
    0x25, 0x64, 0xb5, 0x35, 0x11, 0xaa, 0x1b, 0xf6, 0x93, 0x65, 0x4f, 0x02, 0x72, 0xa7, 0xd5, 0x62,
    0x01, 0x21, 0x1d, 0xdc, 0xd6, 0x59, 0x68, 0x85, 0x95, 0x88, 0x58, 0xec, 0xfe, 0x54, 0x32, 0xed};

uint32_t readUint32(zc::ArrayPtr<const zc::byte> bytes, size_t offset) {
  return (static_cast<uint32_t>(bytes[offset]) << 24U) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 16U) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 8U) |
         static_cast<uint32_t>(bytes[offset + 3]);
}

zc::String decimalName(uint32_t index) {
  char text[7] = {'p', '0', '0', '0', '0', '0', '\0'};
  for (size_t digit = 0; digit < 5; ++digit) {
    text[5 - digit] = static_cast<char>('0' + index % 10);
    index /= 10;
  }
  return zc::heapString(zc::StringPtr(text, 6));
}

zc::String releaseVersion(uint32_t minor) {
  char text[6] = {'1', '.', static_cast<char>('0' + minor), '.', '0', '\0'};
  return zc::heapString(zc::StringPtr(text, 5));
}

identity::CanonicalWorkspaceRelativePath workspacePath(zc::MemoryResource& resource,
                                                       zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments(resource);
  auto segment = identity::CanonicalPathSegment::fromCanonical(resource, name);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

identity::CanonicalWorkspaceRelativePath workspacePath(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical(name);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

identity::CanonicalRelativePath libraryPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto directory = identity::CanonicalPathSegment::fromCanonical("src"_zc);
  auto file = identity::CanonicalPathSegment::fromCanonical("lib.zom"_zc);
  ZC_IF_SOME(value, directory) { segments.add(zc::mv(value)); }
  ZC_IF_SOME(value, file) { segments.add(zc::mv(value)); }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::SortedFeatureSet noFeatures(zc::MemoryResource& resource) {
  zc::Vector<identity::FeatureName> values(resource);
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
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

DigestVerifiedSourceSnapshot sourceSnapshot() {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "lib.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll("let library = 0;"_zc);
  MemoryFreshDirectoryFactory factory;
  SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<DigestVerifiedSourceSnapshot>());
}

identity::PackageBaseKey packageBase(zc::MemoryResource& resource, zc::StringPtr name,
                                     zc::StringPtr version) {
  auto packageName = identity::PackageName::fromCanonical(resource, name);
  auto packageVersion = identity::ResolvedVersion::fromCanonical(resource, version);
  ZC_IF_SOME(nameValue, packageName) {
    ZC_IF_SOME(versionValue, packageVersion) {
      return identity::PackageBaseKey::from(
          identity::CanonicalPackageSource::localPath(workspacePath(resource, name)),
          zc::mv(nameValue), zc::mv(versionValue));
    }
  }
  ZC_UNREACHABLE
}

NormalizedManifest manifest(uint32_t package, uint32_t minor,
                            zc::ArrayPtr<const uint32_t> providers) {
  auto name = decimalName(package);
  auto version = releaseVersion(minor);
  zc::Vector<zc::StringTree> pieces;
  pieces.add(zc::strTree("[package]\nname = \"", name, "\"\nversion = \"", version,
                         "\"\nedition = \"2026\"\n\n[lib]\nname = \"", name,
                         "\"\npath = \"src/lib.zom\"\n"));
  if (providers.size() != 0) { pieces.add(zc::strTree("\n[dependencies]\n")); }
  for (uint32_t provider : providers) {
    auto providerName = decimalName(provider);
    pieces.add(
        zc::strTree(providerName, " = { path = \"", providerName, "\", version = \"^1.0.0\" }\n"));
  }
  zc::StringTree text(pieces.releaseAsArray(), zc::StringPtr());
  zc::Vector<identity::CanonicalRelativePath> files;
  files.add(libraryPath());
  auto inventory = PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  ZC_IF_SOME(sourceInventory, inventory) {
    ManifestParser parser;
    auto parsed = parser.parseWorkspaceManifest(workspacePath("Zom.toml"_zc), text.flatten(),
                                                sourceInventory);
    ZC_REQUIRE(parsed.is<NormalizedManifest>());
    return zc::mv(parsed.get<NormalizedManifest>());
  }
  ZC_UNREACHABLE
}

zc::Vector<zc::Vector<uint32_t>> loadEdges() {
  auto filesystem = zc::newDiskFilesystem();
  const zc::StringPtr fixturePath = ZOM_PACKAGE_RESOLVER_PERFORMANCE_FIXTURE;
  ZC_REQUIRE(fixturePath.size() > 1 && fixturePath[0] == '/');
  auto file = filesystem->getRoot().openFile(zc::Path::parse(fixturePath.slice(1)));
  const auto bytes = file->readAllBytes();
  auto digest = identity::sha256(bytes.asPtr());
  ZC_REQUIRE(digest != zc::none);
  ZC_IF_SOME(value, digest) { ZC_EXPECT(value.bytes() == zc::arrayPtr(kFixtureDigest)); }
  ZC_REQUIRE(bytes.size() == 8 + static_cast<size_t>(kEdgeCount) * 8);
  ZC_REQUIRE(readUint32(bytes, 0) == kPackageCount);
  ZC_REQUIRE(readUint32(bytes, 4) == kEdgeCount);
  zc::Vector<zc::Vector<uint32_t>> outgoing(kPackageCount);
  for (uint32_t package = 0; package < kPackageCount; ++package) {
    outgoing.add(zc::Vector<uint32_t>());
  }
  for (size_t offset = 8; offset < bytes.size(); offset += 8) {
    const uint32_t consumer = readUint32(bytes, offset);
    const uint32_t provider = readUint32(bytes, offset + 4);
    ZC_REQUIRE(consumer < provider && provider < kPackageCount);
    outgoing[consumer].add(provider);
  }
  return outgoing;
}

}  // namespace

ZC_TEST("PackageResolverPerformance.ResolvesCanonicalTenThousandPackageGraph") {
  auto outgoing = loadEdges();
  auto verifiedSource = sourceSnapshot();
  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  size_t observedPeak = 0;
  {
    zc::Vector<ResolverRelease> releases(resource);
    for (uint32_t package = 0; package < kPackageCount; ++package) {
      auto name = decimalName(package);
      for (uint32_t minor = 0; minor < kReleaseCount; ++minor) {
        auto version = releaseVersion(minor);
        auto record =
            LocalPackageRecord::from(packageBase(resource, name, version),
                                     manifest(package, minor, outgoing[package]), verifiedSource);
        ZC_REQUIRE(record != zc::none);
        ZC_IF_SOME(value, record) { releases.add(ResolverRelease::fromLocal(resource, value)); }
      }
    }
    zc::Vector<ResolverRoot> roots(resource);
    roots.add(ResolverRoot::from(packageBase(resource, "p00000"_zc, "1.3.0"_zc),
                                 noFeatures(resource), true, false));
    PackageResolverMetrics metrics;
    zc::Array<uint8_t> expectedResolution;
    zc::Maybe<VerifiedLockGraph> replayLock;
    {
      auto result = PackageResolver::resolve(resource, roots, releases, metrics);
      ZC_REQUIRE(result.is<ResolutionOutput>());
      const auto& resolution = result.get<ResolutionOutput>();
      ZC_EXPECT(resolution.packages().size() == kPackageCount);
      ZC_EXPECT(resolution.edges().size() == kEdgeCount);
      expectedResolution = resolution.encode(resource);
      replayLock = resolution.lockGraph().clone(resource);
    }
    ZC_EXPECT(metrics.decisions <= 40'000);
    ZC_EXPECT(metrics.selectedPackages == kPackageCount);
    ZC_EXPECT(metrics.emittedEdges == kEdgeCount);

    LockReplayMetrics replayMetrics;
    ZC_IF_SOME(locked, replayLock) {
      auto replayed =
          PackageResolver::resolveLocked(resource, roots, releases, locked, replayMetrics);
      ZC_REQUIRE(replayed.is<ResolutionOutput>());
      ZC_EXPECT(replayed.get<ResolutionOutput>().encode(resource).asPtr() ==
                expectedResolution.asPtr());
    }
    else { ZC_FAIL_REQUIRE("resolver lock replay fixture was not retained"); }
    ZC_EXPECT(replayMetrics.solverInvocations == 0);
    ZC_EXPECT(replayMetrics.packageVisits == kPackageCount);
    ZC_EXPECT(replayMetrics.edgeVisits == kEdgeCount);
    observedPeak = resource.peakAllocatedBytes();
    ZC_EXPECT(observedPeak <= uint64_t{1} << 30U);
    ZC_LOG(WARNING, "package resolver resource metrics", observedPeak,
           resource.currentAllocatedBytes(), metrics.decisions, metrics.selectedPackages,
           metrics.emittedEdges, replayMetrics.solverInvocations, replayMetrics.packageVisits,
           replayMetrics.edgeVisits);
  }
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  ZC_LOG(WARNING, "package resolver resource released", observedPeak,
         resource.currentAllocatedBytes());
  outgoing.clear();
}

}  // namespace zomlang::compiler::driver::package
