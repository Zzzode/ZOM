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

#include "zomlang/compiler/driver/package/lockfile.h"

#include "zc/core/encoding.h"
#include "zc/core/time.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::driver::package {
namespace {

identity::Sha256Digest digest(zc::StringPtr text) {
  auto result = identity::sha256(text.asBytes());
  ZC_IF_SOME(value, result) { return value; }
  ZC_UNREACHABLE
}

identity::CanonicalWorkspaceRelativePath workspacePath(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical(name);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  return identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments));
}

identity::CanonicalRelativePath relativePath(zc::StringPtr name) {
  zc::Vector<identity::CanonicalPathSegment> segments;
  auto segment = identity::CanonicalPathSegment::fromCanonical(name);
  ZC_IF_SOME(value, segment) { segments.add(zc::mv(value)); }
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

identity::CanonicalUrl url(zc::StringPtr text) {
  auto result = identity::CanonicalUrl::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::SortedFeatureSet features(zc::StringPtr feature = {}) {
  zc::Vector<identity::FeatureName> values;
  if (feature.size() != 0) {
    auto admitted = identity::FeatureName::fromCanonical(feature);
    ZC_IF_SOME(value, admitted) { values.add(zc::mv(value)); }
  }
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::PackageKey packageKey(identity::CanonicalPackageSource&& source, zc::StringPtr name,
                                zc::StringPtr version, zc::StringPtr feature = {}) {
  auto packageName = identity::PackageName::fromCanonical(name);
  auto packageVersion = identity::ResolvedVersion::fromCanonical(version);
  ZC_IF_SOME(nameValue, packageName) {
    ZC_IF_SOME(versionValue, packageVersion) {
      return identity::PackageKey::from(zc::mv(source), zc::mv(nameValue), zc::mv(versionValue),
                                        features(feature));
    }
  }
  ZC_UNREACHABLE
}

LockPackageRecord localPackage() {
  auto result = LockPackageRecord::from(
      packageKey(identity::CanonicalPackageSource::localPath(workspacePath("app"_zc)), "app"_zc,
                 "1.0.0"_zc, "fast"_zc),
      digest("local manifest"_zc), digest("local tree"_zc), zc::none, zc::none, zc::none);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

LockPackageRecord vcsPackage() {
  uint8_t revisionBytes[20];
  for (size_t index = 0; index < 20; ++index) {
    revisionBytes[index] = static_cast<uint8_t>(index);
  }
  auto revision = identity::VcsRevision::from(identity::VcsRevisionAlgorithm::Sha1,
                                              zc::arrayPtr(revisionBytes));
  ZC_IF_SOME(revisionValue, revision) {
    auto result = LockPackageRecord::from(
        packageKey(
            identity::CanonicalPackageSource::vcs(url("https://example.com/repo.git"_zc),
                                                  zc::mv(revisionValue), relativePath("math"_zc)),
            "math"_zc, "1.2.3"_zc),
        digest("vcs manifest"_zc), digest("vcs tree"_zc), zc::none, zc::none, zc::none);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_UNREACHABLE
}

Ed25519PublicKey publicKey() {
  auto bytes = zc::heapArray<zc::byte>(32, static_cast<zc::byte>(0x2a));
  auto result = Ed25519PublicKey::fromBytes(bytes);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

LockPackageRecord registryPackage() {
  const auto trust = digest("registry trust"_zc);
  auto result = LockPackageRecord::from(
      packageKey(identity::CanonicalPackageSource::registry(
                     identity::RegistryIdentity::from(url("https://example.com/index"_zc), trust)),
                 "codec"_zc, "2.0.0"_zc),
      digest("registry manifest"_zc), digest("registry tree"_zc), ArchiveFormat::TarZstdV1,
      digest("archive"_zc), SigningKeyId::from(publicKey()));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_UNREACHABLE
}

identity::PackageDependencyEdgeKey edge(const identity::PackageKey& consumer, zc::StringPtr alias,
                                        identity::DependencyDomain domain,
                                        const identity::PackageKey& provider) {
  auto aliasValue = identity::DependencyAlias::fromCanonical(alias);
  ZC_IF_SOME(admitted, aliasValue) {
    auto result = identity::PackageDependencyEdgeKey::from(consumer.clone(), zc::mv(admitted),
                                                           domain, provider.clone());
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_UNREACHABLE
}

class FailNthAllocationResource final : public zc::MemoryResource {
public:
  FailNthAllocationResource(zc::MemoryResource& upstream, size_t failureIndex)
      : upstream(upstream), failureIndex(failureIndex) {}

protected:
  void* doAllocate(size_t size, size_t alignment) override {
    ++allocationCount;
    ZC_REQUIRE(allocationCount != failureIndex, "injected lock graph allocation failure");
    return upstream.allocate(size, alignment);
  }

  void doDeallocate(void* pointer, size_t size, size_t alignment) override {
    upstream.deallocate(pointer, size, alignment);
  }

private:
  zc::MemoryResource& upstream;
  size_t failureIndex;
  size_t allocationCount = 0;
};

VerifiedLockGraph graph(bool reverse = false) {
  auto local = localPackage();
  auto vcs = vcsPackage();
  auto registry = registryPackage();
  zc::Vector<identity::PackageDependencyEdgeKey> edges;
  edges.add(edge(local.key(), "math"_zc, identity::DependencyDomain::Target, vcs.key()));
  edges.add(edge(vcs.key(), "codec"_zc, identity::DependencyDomain::Build, registry.key()));
  zc::Vector<LockPackageRecord> packages;
  if (reverse) {
    packages.add(zc::mv(registry));
    packages.add(zc::mv(vcs));
    packages.add(zc::mv(local));
  } else {
    packages.add(zc::mv(local));
    packages.add(zc::mv(vcs));
    packages.add(zc::mv(registry));
  }
  auto result = VerifiedLockGraph::from(zc::mv(packages), zc::mv(edges));
  if (result.is<VerifiedLockGraph>()) { return zc::mv(result.get<VerifiedLockGraph>()); }
  ZC_UNREACHABLE
}

zc::String readLock(const zc::Directory& directory) {
  return directory.openFile(zc::Path("Zom.lock"_zc))->readAllText();
}

zc::String readGoldenLock() {
  auto filesystem = zc::newDiskFilesystem();
  const zc::StringPtr path = ZOM_LOCKFILE_GOLDEN;
  ZC_REQUIRE(path.size() > 1 && path[0] == '/');
  return filesystem->getRoot().openFile(zc::Path::parse(path.slice(1)))->readAllText();
}

zc::String mutateFirst(zc::StringPtr source, zc::StringPtr pattern, zc::StringPtr replacement) {
  ZC_REQUIRE(pattern.size() == replacement.size());
  auto result = zc::heapString(source);
  auto offset = result.find(pattern);
  ZC_REQUIRE(offset != zc::none);
  ZC_IF_SOME(value, offset) {
    for (size_t index = 0; index < replacement.size(); ++index) {
      result[value + index] = replacement[index];
    }
  }
  return result;
}

}  // namespace

ZC_TEST("LockfileTest.CanonicalGraphAndWriterIgnoreInputOrder") {
  auto first = graph(false);
  auto second = graph(true);
  ZC_EXPECT(first.encode().asPtr() == second.encode().asPtr());
  const auto firstText = LockfileCodec::write(first);
  const auto secondText = LockfileCodec::write(second);
  ZC_EXPECT(firstText == secondText);
  ZC_EXPECT(firstText.startsWith("schema = \"zom-lock-1\"\n\n[[package]]\n"_zc));
  ZC_EXPECT(firstText.endsWith("\n"_zc));
  ZC_EXPECT(firstText.contains("source-kind = \"local\""_zc));
  ZC_EXPECT(firstText.contains("source-kind = \"vcs\""_zc));
  ZC_EXPECT(firstText.contains("source-kind = \"registry\""_zc));
  ZC_EXPECT(firstText == readGoldenLock());
  auto outputDigest = identity::sha256(firstText.asBytes());
  ZC_IF_SOME(value, outputDigest) {
    ZC_EXPECT(zc::encodeHex(value.bytes()) ==
              "f87025b6c542149a01d5788f17307a44198de3caa5f71c2ecd129cd0c301b32f"_zc);
  }
}

ZC_TEST("LockfileTest.ResourceGraphRetainsEverySourceAndCanonicalByte") {
  auto source = graph(true);
  const auto expected = source.encode();
  zc::Vector<LockPackageRecord> packages;
  for (const auto& package : source.packages()) { packages.add(package.clone()); }
  zc::Vector<identity::PackageDependencyEdgeKey> edges;
  for (const auto& dependency : source.edges()) { edges.add(dependency.clone()); }

  zc::MemoryResource upstream;
  zc::CountingMemoryResource resource(upstream);
  {
    auto admitted = VerifiedLockGraph::from(resource, zc::mv(packages), zc::mv(edges));
    ZC_REQUIRE(admitted.is<VerifiedLockGraph>());
    auto retained = zc::mv(admitted.get<VerifiedLockGraph>());
    ZC_REQUIRE(retained.packages().size() == 3);
    ZC_REQUIRE(retained.edges().size() == 2);
    ZC_EXPECT(resource.peakAllocatedBytes() > resource.currentAllocatedBytes());

    size_t registryCount = 0;
    size_t vcsCount = 0;
    size_t localCount = 0;
    for (const auto& package : retained.packages()) {
      switch (package.key().source().kind()) {
        case identity::PackageSourceKind::Registry:
          ++registryCount;
          ZC_EXPECT(package.hasArchive());
          ZC_EXPECT(package.archiveDigest() == digest("archive"_zc));
          ZC_EXPECT(package.signingKey().digest() == SigningKeyId::from(publicKey()).digest());
          break;
        case identity::PackageSourceKind::Vcs:
          ++vcsCount;
          ZC_EXPECT(!package.hasArchive());
          break;
        case identity::PackageSourceKind::LocalPath:
          ++localCount;
          ZC_EXPECT(!package.hasArchive());
          break;
      }
    }
    ZC_EXPECT(registryCount == 1);
    ZC_EXPECT(vcsCount == 1);
    ZC_EXPECT(localCount == 1);

    auto encoded = retained.encode(resource);
    ZC_EXPECT(encoded.asPtr() == expected.asPtr());
    auto cloned = retained.clone(resource);
    ZC_EXPECT(cloned.encode(resource).asPtr() == expected.asPtr());
    zc::Vector<identity::RegistryIdentity> trusted;
    for (const auto& package : retained.packages()) {
      if (package.key().source().kind() == identity::PackageSourceKind::Registry) {
        trusted.add(package.key().source().registryIdentity().clone());
      }
    }
    LockReplayMetrics metrics;
    auto replay = LockedReplayVerifier::replay(resource, retained, cloned, trusted, metrics);
    ZC_REQUIRE(replay.is<VerifiedLockGraph>());
    ZC_EXPECT(replay.get<VerifiedLockGraph>().encode(resource).asPtr() == expected.asPtr());
    ZC_EXPECT(metrics.solverInvocations == 0);
    auto moved = zc::mv(retained);
    ZC_EXPECT(moved.encode(resource).asPtr() == expected.asPtr());
    ZC_EXPECT(resource.currentAllocatedBytes() > 0);
  }
  ZC_EXPECT(resource.peakAllocatedBytes() > 0);
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("LockfileTest.ResourceGraphCleansUpRejectedAndFailedConstruction") {
  {
    auto duplicatePackage = localPackage();
    zc::Vector<LockPackageRecord> packages;
    packages.add(duplicatePackage.clone());
    packages.add(zc::mv(duplicatePackage));
    zc::Vector<identity::PackageDependencyEdgeKey> edges;
    zc::MemoryResource upstream;
    zc::CountingMemoryResource resource(upstream);
    auto rejected = VerifiedLockGraph::from(resource, zc::mv(packages), zc::mv(edges));
    ZC_REQUIRE(rejected.is<LockIssue>());
    ZC_EXPECT(rejected.get<LockIssue>() == LockIssue::DuplicatePackageKey);
    ZC_EXPECT(resource.currentAllocatedBytes() == 0);
  }

  auto source = graph();
  zc::Vector<LockPackageRecord> packages;
  for (const auto& package : source.packages()) { packages.add(package.clone()); }
  zc::Vector<identity::PackageDependencyEdgeKey> edges;
  for (const auto& dependency : source.edges()) { edges.add(dependency.clone()); }
  zc::MemoryResource upstream;
  FailNthAllocationResource failing(upstream, 6);
  zc::CountingMemoryResource resource(failing);
  ZC_EXPECT_THROW_MESSAGE("injected lock graph allocation failure",
                          VerifiedLockGraph::from(resource, zc::mv(packages), zc::mv(edges)));
  ZC_EXPECT(resource.currentAllocatedBytes() == 0);
}

ZC_TEST("LockfileTest.RejectsDuplicateAndDanglingGraphEntries") {
  auto package = localPackage();
  zc::Vector<LockPackageRecord> duplicates;
  duplicates.add(package.clone());
  duplicates.add(package.clone());
  zc::Vector<identity::PackageDependencyEdgeKey> noEdges;
  auto duplicate = VerifiedLockGraph::from(zc::mv(duplicates), zc::mv(noEdges));
  ZC_REQUIRE(duplicate.is<LockIssue>());
  ZC_EXPECT(duplicate.get<LockIssue>() == LockIssue::DuplicatePackageKey);

  auto missing = vcsPackage();
  zc::Vector<identity::PackageDependencyEdgeKey> danglingEdges;
  danglingEdges.add(
      edge(package.key(), "math"_zc, identity::DependencyDomain::Target, missing.key()));
  zc::Vector<LockPackageRecord> onePackage;
  onePackage.add(zc::mv(package));
  auto dangling = VerifiedLockGraph::from(zc::mv(onePackage), zc::mv(danglingEdges));
  ZC_REQUIRE(dangling.is<LockIssue>());
  ZC_EXPECT(dangling.get<LockIssue>() == LockIssue::DanglingEdge);
}

ZC_TEST("LockfileTest.AtomicUpdatePreservesOrCommitsAtEveryStage") {
  const LockWriteStage stages[] = {LockWriteStage::TemporaryCreate, LockWriteStage::Write,
                                   LockWriteStage::FileSync, LockWriteStage::Rename,
                                   LockWriteStage::DirectorySync};
  for (LockWriteStage stage : stages) {
    auto directory = zc::newInMemoryDirectory(zc::nullClock());
    directory->openFile(zc::Path("Zom.lock"_zc), zc::WriteMode::CREATE)->writeAll("old\n"_zc);
    auto failure = AtomicLockfileWriter::write(*directory, "new\n"_zc, stage);
    ZC_REQUIRE(failure != zc::none);
    ZC_IF_SOME(value, failure) { ZC_EXPECT(value == stage); }
    if (stage == LockWriteStage::DirectorySync) {
      ZC_EXPECT(readLock(*directory) == "new\n"_zc);
    } else {
      ZC_EXPECT(readLock(*directory) == "old\n"_zc);
    }
  }

  auto directory = zc::newInMemoryDirectory(zc::nullClock());
  auto result = AtomicLockfileWriter::write(*directory, "new\n"_zc);
  ZC_EXPECT(result == zc::none);
  ZC_EXPECT(readLock(*directory) == "new\n"_zc);
}

ZC_TEST("LockfileTest.ParsesAndReencodesGoldenGraph") {
  const auto golden = readGoldenLock();
  auto result = LockfileCodec::parse(golden.asBytes());
  ZC_REQUIRE(result.is<VerifiedLockGraph>());
  const auto& parsed = result.get<VerifiedLockGraph>();
  ZC_EXPECT(parsed.packages().size() == 3);
  ZC_EXPECT(parsed.edges().size() == 2);
  ZC_EXPECT(LockfileCodec::write(parsed) == golden);
}

ZC_TEST("LockfileTest.RejectsCorruptAndNonCanonicalDocuments") {
  const auto golden = readGoldenLock();
  auto noFinalLineFeed = zc::heapString(golden.first(golden.size() - 1));
  auto missingLineFeed = LockfileCodec::parse(noFinalLineFeed.asBytes());
  ZC_REQUIRE(missingLineFeed.is<LockIssue>());
  ZC_EXPECT(missingLineFeed.get<LockIssue>() == LockIssue::NonCanonicalEncoding);

  auto unsupported = zc::heapString(golden);
  unsupported[19] = '2';
  auto unsupportedResult = LockfileCodec::parse(unsupported.asBytes());
  ZC_REQUIRE(unsupportedResult.is<LockIssue>());
  ZC_EXPECT(unsupportedResult.get<LockIssue>() == LockIssue::UnsupportedSchema);

  auto invalidHex = zc::heapString(golden);
  const auto keyOffset = invalidHex.find("key = \""_zc);
  ZC_REQUIRE(keyOffset != zc::none);
  ZC_IF_SOME(offset, keyOffset) { invalidHex[offset + 7] = 'A'; }
  auto invalidHexResult = LockfileCodec::parse(invalidHex.asBytes());
  ZC_REQUIRE(invalidHexResult.is<LockIssue>());
  ZC_EXPECT(invalidHexResult.get<LockIssue>() == LockIssue::InvalidDigest);
}

ZC_TEST("LockfileTest.LockedReplaySkipsResolverAndVisitsGraphOnce") {
  auto locked = graph();
  auto current = graph(true);
  zc::Vector<identity::RegistryIdentity> trusted;
  for (const auto& package : locked.packages()) {
    if (package.key().source().kind() == identity::PackageSourceKind::Registry) {
      trusted.add(package.key().source().registryIdentity().clone());
    }
  }
  LockReplayMetrics metrics;
  auto replay = LockedReplayVerifier::replay(locked, current, trusted, metrics);
  ZC_REQUIRE(replay.is<VerifiedLockGraph>());
  ZC_EXPECT(metrics.solverInvocations == 0);
  ZC_EXPECT(metrics.packageVisits == locked.packages().size());
  ZC_EXPECT(metrics.edgeVisits == locked.edges().size());
  ZC_EXPECT(replay.get<VerifiedLockGraph>().encode().asPtr() == locked.encode().asPtr());

  zc::Vector<identity::RegistryIdentity> noTrust;
  auto untrusted = LockedReplayVerifier::replay(locked, current, noTrust, metrics);
  ZC_REQUIRE(untrusted.is<LockIssue>());
  ZC_EXPECT(untrusted.get<LockIssue>() == LockIssue::TrustDomainMismatch);

  zc::Vector<LockPackageRecord> localOnly;
  localOnly.add(localPackage());
  zc::Vector<identity::PackageDependencyEdgeKey> noEdges;
  auto different = VerifiedLockGraph::from(zc::mv(localOnly), zc::mv(noEdges));
  ZC_REQUIRE(different.is<VerifiedLockGraph>());
  auto mismatch =
      LockedReplayVerifier::replay(locked, different.get<VerifiedLockGraph>(), trusted, metrics);
  ZC_REQUIRE(mismatch.is<LockIssue>());
  ZC_EXPECT(mismatch.get<LockIssue>() == LockIssue::CurrentInputMismatch);
}

ZC_TEST("LockfileTest.ClassifiesClosedSchemaCorruption") {
  const auto golden = readGoldenLock();
  auto unknown = mutateFirst(golden, "key = \""_zc, "bad = \""_zc);
  auto unknownResult = LockfileCodec::parse(unknown.asBytes());
  ZC_REQUIRE(unknownResult.is<LockIssue>());
  ZC_EXPECT(unknownResult.get<LockIssue>() == LockIssue::UnknownField);

  auto wrongType = mutateFirst(golden, "features = []"_zc, "features = \"\""_zc);
  auto wrongTypeResult = LockfileCodec::parse(wrongType.asBytes());
  ZC_REQUIRE(wrongTypeResult.is<LockIssue>());
  ZC_EXPECT(wrongTypeResult.get<LockIssue>() == LockIssue::WrongValueType);

  auto keyMismatch = mutateFirst(golden, "features = [\"fast\"]"_zc, "features = [\"slow\"]"_zc);
  auto keyMismatchResult = LockfileCodec::parse(keyMismatch.asBytes());
  ZC_REQUIRE(keyMismatchResult.is<LockIssue>());
  ZC_EXPECT(keyMismatchResult.get<LockIssue>() == LockIssue::PackageKeyMismatch);

  auto sourceMismatch = mutateFirst(
      golden,
      "source-key = \"01000000000000001968747470733a2f2f6578616d706c652e636f6d2f696e64657830f96117b538e8514e54ce3d9aa677d50a37980e8c40c8e4dbf3acd9645ac804\""_zc,
      "source-key = \"01000000000000001968747470733a2f2f6578616d706c652e636f6d2f696e64657830f96117b538e8514e54ce3d9aa677d50a37980e8c40c8e4dbf3acd9645ac805\""_zc);
  auto sourceMismatchResult = LockfileCodec::parse(sourceMismatch.asBytes());
  ZC_REQUIRE(sourceMismatchResult.is<LockIssue>());
  ZC_EXPECT(sourceMismatchResult.get<LockIssue>() == LockIssue::SourceKeyMismatch);

  auto sourceFieldMismatch = mutateFirst(golden, "name = \"codec\""_zc, "name = \"xodec\""_zc);
  auto sourceFieldResult = LockfileCodec::parse(sourceFieldMismatch.asBytes());
  ZC_REQUIRE(sourceFieldResult.is<LockIssue>());
  ZC_EXPECT(sourceFieldResult.get<LockIssue>() == LockIssue::SourceFieldMismatch);

  auto invalidDigest = mutateFirst(golden, "manifest-sha256 = \"b"_zc, "manifest-sha256 = \"B"_zc);
  auto invalidDigestResult = LockfileCodec::parse(invalidDigest.asBytes());
  ZC_REQUIRE(invalidDigestResult.is<LockIssue>());
  ZC_EXPECT(invalidDigestResult.get<LockIssue>() == LockIssue::InvalidDigest);

  auto emptyDirectory = zc::newInMemoryDirectory(zc::nullClock());
  auto readFailure = LockfileCodec::read(*emptyDirectory);
  ZC_REQUIRE(readFailure.is<LockIssue>());
  ZC_EXPECT(readFailure.get<LockIssue>() == LockIssue::ReadFailed);
}

}  // namespace zomlang::compiler::driver::package
