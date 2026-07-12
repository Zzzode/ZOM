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
#include "zc/ztest/test.h"
#include "zomlang/compiler/driver/package/lockfile.h"
#include "zomlang/compiler/driver/package/package-resolver.h"
#include "zomlang/compiler/identity/canonical-encoder.h"
#include "zomlang/compiler/identity/sha256.h"

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

identity::SortedFeatureSet noFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::PackageBaseKey packageBase(zc::StringPtr name, zc::StringPtr version) {
  auto packageName = identity::PackageName::fromCanonical(name);
  auto packageVersion = identity::ResolvedVersion::fromCanonical(version);
  ZC_IF_SOME(nameValue, packageName) {
    ZC_IF_SOME(versionValue, packageVersion) {
      return identity::PackageBaseKey::from(
          identity::CanonicalPackageSource::localPath(workspacePath(name)), zc::mv(nameValue),
          zc::mv(versionValue));
    }
  }
  ZC_UNREACHABLE
}

DependencyRequirementWithoutOrigin dependency(uint32_t provider) {
  auto name = decimalName(provider);
  auto alias = identity::DependencyAlias::fromCanonical(name);
  auto packageName = identity::PackageName::fromCanonical(name);
  auto constraint = SemVerConstraint::parse("^1.0.0"_zc);
  ZC_IF_SOME(aliasValue, alias) {
    ZC_IF_SOME(packageNameValue, packageName) {
      ZC_IF_SOME(constraintValue, constraint) {
        auto result = DependencyRequirementWithoutOrigin::from(
            zc::mv(aliasValue), zc::mv(packageNameValue), identity::DependencyDomain::Target,
            PackageSourceConstraint::localPath(workspacePath(name)), zc::mv(constraintValue),
            noFeatures(), true, false);
        ZC_IF_SOME(value, result) { return zc::mv(value); }
      }
    }
  }
  ZC_UNREACHABLE
}

void sortDependencies(zc::Vector<DependencyRequirementWithoutOrigin>& dependencies) {
  for (size_t index = 1; index < dependencies.size(); ++index) {
    auto current = zc::mv(dependencies[index]);
    const auto currentBytes = current.encode();
    size_t insertion = index;
    while (insertion != 0 && currentBytes.asPtr() < dependencies[insertion - 1].encode().asPtr()) {
      dependencies[insertion] = zc::mv(dependencies[insertion - 1]);
      --insertion;
    }
    dependencies[insertion] = zc::mv(current);
  }
}

CanonicalManifestRecord manifest(uint32_t package, uint32_t minor,
                                 zc::ArrayPtr<const uint32_t> providers) {
  auto name = decimalName(package);
  auto version = releaseVersion(minor);
  auto packageName = identity::PackageName::fromCanonical(name);
  auto packageVersion = identity::ResolvedVersion::fromCanonical(version);
  auto targetName = identity::TargetName::fromCanonical(name);
  ZC_IF_SOME(packageNameValue, packageName) {
    ZC_IF_SOME(packageVersionValue, packageVersion) {
      ZC_IF_SOME(targetNameValue, targetName) {
        auto library = CanonicalTargetManifest::from(identity::CrateTargetKind::Library,
                                                     zc::mv(targetNameValue), libraryPath(), false);
        ZC_IF_SOME(libraryValue, library) {
          zc::Vector<DependencyRequirementWithoutOrigin> dependencies;
          for (uint32_t provider : providers) { dependencies.add(dependency(provider)); }
          sortDependencies(dependencies);
          zc::Vector<DependencyRequirementWithoutOrigin> development;
          zc::Vector<DependencyRequirementWithoutOrigin> build;
          zc::Vector<CanonicalFeatureManifest> features;
          return CanonicalManifestRecord::forResolver(
              PackageManifest::from(zc::mv(packageNameValue), zc::mv(packageVersionValue), 2026),
              zc::mv(libraryValue), zc::mv(dependencies), zc::mv(development), zc::mv(build),
              zc::mv(features));
        }
      }
    }
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
  zc::Vector<ResolverRelease> releases;
  for (uint32_t package = 0; package < kPackageCount; ++package) {
    auto name = decimalName(package);
    for (uint32_t minor = 0; minor < kReleaseCount; ++minor) {
      auto version = releaseVersion(minor);
      releases.add(ResolverRelease::from(PackageSourceConstraint::localPath(workspacePath(name)),
                                         packageBase(name, version),
                                         manifest(package, minor, outgoing[package]), false));
    }
  }
  zc::Vector<ResolverRoot> roots;
  roots.add(ResolverRoot::from(packageBase("p00000"_zc, "1.3.0"_zc), noFeatures(), true, false));
  PackageResolverMetrics metrics;
  auto result = PackageResolver::resolve(roots, releases, metrics);
  ZC_REQUIRE(result.is<PackageResolution>());
  ZC_EXPECT(result.get<PackageResolution>().packages().size() == kPackageCount);
  ZC_EXPECT(result.get<PackageResolution>().edges().size() == kEdgeCount);
  ZC_EXPECT(metrics.decisions <= 40'000);
  ZC_EXPECT(metrics.selectedPackages == kPackageCount);
  ZC_EXPECT(metrics.emittedEdges == kEdgeCount);

  const auto& resolution = result.get<PackageResolution>();
  zc::Vector<LockPackageRecord> lockPackages(resolution.packages().size());
  for (const auto& package : resolution.packages()) {
    auto packageKey = package.packageKey();
    auto digest = identity::sha256(packageKey.encode());
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(value, digest) {
      zc::Maybe<ArchiveFormat> noArchiveFormat;
      zc::Maybe<identity::Sha256Digest> noArchiveDigest;
      zc::Maybe<SigningKeyId> noSigningKey;
      auto record =
          LockPackageRecord::from(package.packageKey(), value, value, zc::mv(noArchiveFormat),
                                  zc::mv(noArchiveDigest), zc::mv(noSigningKey));
      ZC_REQUIRE(record != zc::none);
      ZC_IF_SOME(recordValue, record) { lockPackages.add(zc::mv(recordValue)); }
    }
  }
  zc::Vector<identity::PackageDependencyEdgeKey> lockEdges(resolution.edges().size());
  for (const auto& edge : resolution.edges()) { lockEdges.add(edge.clone()); }

  releases.clear();
  roots.clear();
  outgoing.clear();

  auto locked = VerifiedLockGraph::from(zc::mv(lockPackages), zc::mv(lockEdges));
  ZC_REQUIRE(locked.is<VerifiedLockGraph>());
  auto current = locked.get<VerifiedLockGraph>().clone();
  zc::Vector<identity::RegistryIdentity> noRegistries;
  LockReplayMetrics replayMetrics;
  auto replayed = LockedReplayVerifier::replay(locked.get<VerifiedLockGraph>(), current,
                                               noRegistries, replayMetrics);
  ZC_REQUIRE(replayed.is<VerifiedLockGraph>());
  ZC_EXPECT(replayMetrics.solverInvocations == 0);
  ZC_EXPECT(replayMetrics.packageVisits == kPackageCount);
  ZC_EXPECT(replayMetrics.edgeVisits == kEdgeCount);
  ZC_EXPECT(replayed.get<VerifiedLockGraph>().encode().asPtr() ==
            locked.get<VerifiedLockGraph>().encode().asPtr());
}

}  // namespace zomlang::compiler::driver::package
