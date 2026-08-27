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

#include "zomlang/compiler/source/core-source-admission.h"

#include "zc/core/debug.h"
#include "zc/core/encoding.h"
#include "zomlang/compiler/identity/canonical/canonical-encoder.h"

namespace zomlang::compiler::source::core {
namespace {

using driver::package::MaterializationIssue;
using driver::package::SourceTreeFile;
using driver::package::SourceTreeRecord;

bool samePath(const identity::CanonicalRelativePath& left,
              const identity::CanonicalRelativePath& right) {
  identity::CanonicalEncoder leftEncoder;
  left.encode(leftEncoder);
  identity::CanonicalEncoder rightEncoder;
  right.encode(rightEncoder);
  return leftEncoder.finish().asPtr() == rightEncoder.finish().asPtr();
}

bool sameSourceTree(const SourceTreeRecord& left, const SourceTreeRecord& right) {
  if (left.digest() != right.digest() || left.files().size() != right.files().size()) {
    return false;
  }
  for (size_t index = 0; index < left.files().size(); ++index) {
    if (left.files()[index].encode().asPtr() != right.files()[index].encode().asPtr()) {
      return false;
    }
  }
  return true;
}

bool isPathIssue(MaterializationIssue issue) {
  switch (issue) {
    case MaterializationIssue::AbsolutePath:
    case MaterializationIssue::ParentPath:
    case MaterializationIssue::DotPath:
    case MaterializationIssue::EmptySegment:
    case MaterializationIssue::BackslashPath:
    case MaterializationIssue::PathTooLong:
    case MaterializationIssue::PathTooDeep:
    case MaterializationIssue::DuplicatePath:
    case MaterializationIssue::UnicodeCollision:
    case MaterializationIssue::CaseFoldCollision:
    case MaterializationIssue::Symlink:
    case MaterializationIssue::HardLink:
    case MaterializationIssue::SpecialFile:
    case MaterializationIssue::InvalidEntryEncoding:
      return true;
    default:
      return false;
  }
}

bool isReadIssue(MaterializationIssue issue) {
  switch (issue) {
    case MaterializationIssue::SourceReadFailed:
    case MaterializationIssue::FreshDirectoryCreateFailed:
    case MaterializationIssue::DestinationCreateFailed:
    case MaterializationIssue::DestinationWriteFailed:
    case MaterializationIssue::DestinationSyncFailed:
    case MaterializationIssue::SnapshotCleanupFailed:
      return true;
    default:
      return false;
  }
}

CoreDistributionAdmissionFailure mapMaterializationIssue(MaterializationIssue issue) {
  if (isPathIssue(issue)) { return CoreDistributionAdmissionFailure::inventoryEntry(0); }
  if (isReadIssue(issue)) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(CoreLibraryIssue::ReadFailed);
  }
  if (issue == MaterializationIssue::SourceChangedDuringSnapshot ||
      issue == MaterializationIssue::SourceTreeDigestMismatch) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(
        CoreLibraryIssue::VerifierDisagreement);
  }
  return CoreDistributionAdmissionFailure::withoutCoordinate(
      CoreLibraryIssue::DistributionMismatch);
}

bool isZomSourcePath(const identity::CanonicalRelativePath& path) {
  if (path.segments().size() == 0) { return false; }
  return path.segments().back().text().endsWith(".zom"_zc);
}

bool validSourceBytes(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0 || bytes.back() != '\n' ||
      (bytes.size() > 1 && bytes[bytes.size() - 2] == '\n')) {
    return false;
  }
  if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) {
    return false;
  }
  if (zc::encodeUtf32(bytes.asChars()) == zc::none) { return false; }
  for (size_t index = 0; index < bytes.size(); ++index) {
    const uint8_t value = bytes[index];
    if (value == '\0' || value == '\r') { return false; }
    if (value == '\n' && index != 0 && (bytes[index - 1] == ' ' || bytes[index - 1] == '\t')) {
      return false;
    }
  }
  return true;
}

zc::Maybe<const CoreSourceFile&> findExpectedFile(const CoreDistributionRecord& record,
                                                  const identity::CanonicalRelativePath& path) {
  for (const auto& file : record.files()) {
    if (samePath(file.path(), path)) { return file; }
  }
  return zc::none;
}

using SnapshotBuildResult =
    zc::OneOf<zc::Vector<VerifiedCoreSourceSnapshot>, CoreDistributionAdmissionFailure>;

SnapshotBuildResult buildSnapshots(const driver::package::DigestVerifiedSourceSnapshot& source) {
  zc::Vector<VerifiedCoreSourceSnapshot> snapshots(source.record().files().size());
  for (const auto& file : source.record().files()) {
    auto read = source.readVerifiedFile(file.path());
    if (read.is<MaterializationIssue>()) {
      return CoreDistributionAdmissionFailure::file(CoreLibraryIssue::ReadFailed,
                                                    file.path().clone());
    }
    auto bytes = zc::mv(read.get<zc::Array<zc::byte>>());
    if (!validSourceBytes(bytes.asPtr())) {
      return CoreDistributionAdmissionFailure::file(CoreLibraryIssue::InvalidSourceBytes,
                                                    file.path().clone());
    }
    auto snapshot = VerifiedCoreSourceSnapshot::from(file.path().clone(),
                                                     zc::heapArray<uint8_t>(bytes.asPtr()));
    if (snapshot == zc::none) {
      return CoreDistributionAdmissionFailure::file(CoreLibraryIssue::InvalidSourceBytes,
                                                    file.path().clone());
    }
    ZC_IF_SOME(value, snapshot) { snapshots.add(zc::mv(value)); }
  }
  return zc::mv(snapshots);
}

bool sameSnapshots(zc::ArrayPtr<const VerifiedCoreSourceSnapshot> left,
                   zc::ArrayPtr<const VerifiedCoreSourceSnapshot> right) {
  if (left.size() != right.size()) { return false; }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!samePath(left[index].path(), right[index].path()) ||
        left[index].contentDigest() != right[index].contentDigest() ||
        left[index].bytes() != right[index].bytes()) {
      return false;
    }
  }
  return true;
}

zc::Maybe<CoreDistributionAdmissionFailure> validateInventoryPaths(const SourceTreeRecord& tree) {
  for (size_t index = 0; index < tree.files().size(); ++index) {
    if (!isZomSourcePath(tree.files()[index].path())) {
      return CoreDistributionAdmissionFailure::inventoryEntry(index);
    }
  }
  return zc::none;
}

bool inventoryMatches(const SourceTreeRecord& tree, const CoreDistributionRecord& expected) {
  if (tree.files().size() != expected.files().size()) { return false; }
  for (size_t index = 0; index < tree.files().size(); ++index) {
    const auto& actual = tree.files()[index];
    auto expectedFile = findExpectedFile(expected, actual.path());
    if (expectedFile == zc::none) { return false; }
    ZC_IF_SOME(value, expectedFile) {
      if (actual.contentDigest() != value.digest()) { return false; }
    }
  }
  return true;
}

bool expectedAuthorityMatchesInitial(const CoreDistributionInputRecord& expected) {
  auto initialRecord = initialCoreDistributionRecord();
  auto initialPolicy = initialCoreMarkerPolicyTemplate();
  if (initialRecord == zc::none || initialPolicy == zc::none) { return false; }
  auto digest = computeCoreDistributionDigest(expected.record());
  return digest != zc::none && ZC_ASSERT_NONNULL(digest) == expected.digest() &&
         expected.record().encode().asPtr() == ZC_ASSERT_NONNULL(initialRecord).encode().asPtr() &&
         expected.policyTemplate().encode().asPtr() ==
             ZC_ASSERT_NONNULL(initialPolicy).encode().asPtr() &&
         expected.policyTemplate().revision() == ZC_ASSERT_NONNULL(initialPolicy).revision();
}

}  // namespace

CoreDistributionAdmissionFailure::CoreDistributionAdmissionFailure(
    CoreLibraryIssue issue, zc::Maybe<uint64_t> inventoryOrdinal,
    zc::Maybe<identity::CanonicalRelativePath>&& path) noexcept
    : issueValue(issue), inventoryOrdinalValue(inventoryOrdinal), pathValue(zc::mv(path)) {}

CoreDistributionAdmissionFailure CoreDistributionAdmissionFailure::withoutCoordinate(
    CoreLibraryIssue issue) {
  zc::Maybe<identity::CanonicalRelativePath> path;
  return CoreDistributionAdmissionFailure(issue, zc::none, zc::mv(path));
}

CoreDistributionAdmissionFailure CoreDistributionAdmissionFailure::inventoryEntry(
    uint64_t ordinal) {
  zc::Maybe<identity::CanonicalRelativePath> path;
  return CoreDistributionAdmissionFailure(CoreLibraryIssue::InvalidPath, ordinal, zc::mv(path));
}

CoreDistributionAdmissionFailure CoreDistributionAdmissionFailure::file(
    CoreLibraryIssue issue, identity::CanonicalRelativePath&& path) {
  return CoreDistributionAdmissionFailure(issue, zc::none, zc::mv(path));
}

CoreLibraryIssue CoreDistributionAdmissionFailure::issue() const noexcept { return issueValue; }
zc::Maybe<uint64_t> CoreDistributionAdmissionFailure::inventoryOrdinal() const noexcept {
  return inventoryOrdinalValue;
}
zc::Maybe<const identity::CanonicalRelativePath&> CoreDistributionAdmissionFailure::path()
    const noexcept {
  ZC_IF_SOME(value, pathValue) { return value; }
  return zc::none;
}

struct VerifiedCoreSourceRoot::Impl final {};
VerifiedCoreSourceRoot::VerifiedCoreSourceRoot(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreSourceRoot::~VerifiedCoreSourceRoot() noexcept(false) = default;
VerifiedCoreSourceRoot::VerifiedCoreSourceRoot(VerifiedCoreSourceRoot&&) noexcept = default;
VerifiedCoreSourceRoot& VerifiedCoreSourceRoot::operator=(VerifiedCoreSourceRoot&&) noexcept =
    default;

struct VerifiedCoreSourceSnapshot::Impl final {
  Impl(identity::CanonicalRelativePath&& path, zc::Array<uint8_t>&& bytes,
       const identity::Sha256Digest& contentDigest)
      : path(zc::mv(path)), bytes(zc::mv(bytes)), contentDigest(contentDigest) {}
  identity::CanonicalRelativePath path;
  zc::Array<uint8_t> bytes;
  identity::Sha256Digest contentDigest;
};
VerifiedCoreSourceSnapshot::VerifiedCoreSourceSnapshot(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreSourceSnapshot::~VerifiedCoreSourceSnapshot() noexcept(false) = default;
VerifiedCoreSourceSnapshot::VerifiedCoreSourceSnapshot(VerifiedCoreSourceSnapshot&&) noexcept =
    default;
VerifiedCoreSourceSnapshot& VerifiedCoreSourceSnapshot::operator=(
    VerifiedCoreSourceSnapshot&&) noexcept = default;
zc::Maybe<VerifiedCoreSourceSnapshot> VerifiedCoreSourceSnapshot::from(
    identity::CanonicalRelativePath&& path, zc::Array<uint8_t>&& bytes) {
  auto digest = identity::sha256(bytes.asPtr());
  if (digest == zc::none) { return zc::none; }
  return VerifiedCoreSourceSnapshot(
      zc::heap<Impl>(zc::mv(path), zc::mv(bytes), ZC_ASSERT_NONNULL(digest)));
}
VerifiedCoreSourceSnapshot VerifiedCoreSourceSnapshot::clone() const {
  auto result = from(impl->path.clone(), zc::heapArray<uint8_t>(impl->bytes.asPtr()));
  return zc::mv(ZC_ASSERT_NONNULL(result));
}
const identity::CanonicalRelativePath& VerifiedCoreSourceSnapshot::path() const noexcept {
  return impl->path;
}
zc::ArrayPtr<const uint8_t> VerifiedCoreSourceSnapshot::bytes() const noexcept {
  return impl->bytes.asPtr();
}
const identity::Sha256Digest& VerifiedCoreSourceSnapshot::contentDigest() const noexcept {
  return impl->contentDigest;
}

struct VerifiedCoreDistribution::Impl final {
  Impl(CoreDistributionRecord&& record, const identity::Sha256Digest& distributionDigest,
       CoreStandardMarkerPolicyTemplate&& policyTemplate, VerifiedCoreSourceRoot&& sourceRoot,
       zc::Vector<VerifiedCoreSourceSnapshot>&& snapshots)
      : record(zc::mv(record)),
        distributionDigest(distributionDigest),
        policyTemplate(zc::mv(policyTemplate)),
        sourceRoot(zc::mv(sourceRoot)),
        snapshots(zc::mv(snapshots)) {}
  CoreDistributionRecord record;
  identity::Sha256Digest distributionDigest;
  CoreStandardMarkerPolicyTemplate policyTemplate;
  VerifiedCoreSourceRoot sourceRoot;
  zc::Vector<VerifiedCoreSourceSnapshot> snapshots;
};
VerifiedCoreDistribution::VerifiedCoreDistribution(zc::Own<Impl>&& value) noexcept
    : impl(zc::mv(value)) {}
VerifiedCoreDistribution::~VerifiedCoreDistribution() noexcept(false) = default;
VerifiedCoreDistribution::VerifiedCoreDistribution(VerifiedCoreDistribution&&) noexcept = default;
VerifiedCoreDistribution& VerifiedCoreDistribution::operator=(VerifiedCoreDistribution&&) noexcept =
    default;
const CoreDistributionRecord& VerifiedCoreDistribution::record() const noexcept {
  return impl->record;
}
const identity::Sha256Digest& VerifiedCoreDistribution::distributionDigest() const noexcept {
  return impl->distributionDigest;
}
const CoreStandardMarkerPolicyTemplate& VerifiedCoreDistribution::policyTemplate() const noexcept {
  return impl->policyTemplate;
}
const VerifiedCoreSourceRoot& VerifiedCoreDistribution::sourceRoot() const noexcept {
  return impl->sourceRoot;
}
zc::ArrayPtr<const VerifiedCoreSourceSnapshot> VerifiedCoreDistribution::snapshots()
    const noexcept {
  return impl->snapshots.asPtr();
}

CoreDistributionAdmission::CoreDistributionAdmission(
    driver::package::SourceAdmissionLimits sourceLimits)
    : limits(sourceLimits) {}

CoreDistributionAdmissionResult CoreDistributionAdmission::admit(
    const zc::ReadableDirectory& sourceRoot,
    driver::package::FreshSourceDirectoryFactory& snapshotFactory,
    const CoreDistributionInputRecord& expected, uint32_t projectedEditionYear) const {
  auto builderInventory = driver::package::inspectSourceDirectory(sourceRoot, limits);
  if (builderInventory.is<MaterializationIssue>()) {
    return mapMaterializationIssue(builderInventory.get<MaterializationIssue>());
  }
  auto verifierInventory = driver::package::inspectSourceDirectory(sourceRoot, limits);
  if (verifierInventory.is<MaterializationIssue>()) {
    return mapMaterializationIssue(verifierInventory.get<MaterializationIssue>());
  }
  auto builderTree = zc::mv(builderInventory.get<SourceTreeRecord>());
  auto verifierTree = zc::mv(verifierInventory.get<SourceTreeRecord>());
  if (!sameSourceTree(builderTree, verifierTree)) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(
        CoreLibraryIssue::VerifierDisagreement);
  }
  ZC_IF_SOME(failure, validateInventoryPaths(builderTree)) { return zc::mv(failure); }

  driver::package::SourceDirectoryMaterializer materializer(limits);
  auto materialized = materializer.materialize(sourceRoot, snapshotFactory);
  if (materialized.is<MaterializationIssue>()) {
    return mapMaterializationIssue(materialized.get<MaterializationIssue>());
  }
  auto snapshot = zc::mv(materialized.get<driver::package::DigestVerifiedSourceSnapshot>());
  if (!sameSourceTree(builderTree, snapshot.record())) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(
        CoreLibraryIssue::VerifierDisagreement);
  }

  auto builderSnapshots = buildSnapshots(snapshot);
  if (builderSnapshots.is<CoreDistributionAdmissionFailure>()) {
    return zc::mv(builderSnapshots.get<CoreDistributionAdmissionFailure>());
  }
  auto verifierSnapshots = buildSnapshots(snapshot);
  if (verifierSnapshots.is<CoreDistributionAdmissionFailure>()) {
    return zc::mv(verifierSnapshots.get<CoreDistributionAdmissionFailure>());
  }
  auto admittedSnapshots = zc::mv(builderSnapshots.get<zc::Vector<VerifiedCoreSourceSnapshot>>());
  auto independentlyVerifiedSnapshots =
      zc::mv(verifierSnapshots.get<zc::Vector<VerifiedCoreSourceSnapshot>>());
  if (!sameSnapshots(admittedSnapshots.asPtr(), independentlyVerifiedSnapshots.asPtr())) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(
        CoreLibraryIssue::VerifierDisagreement);
  }

  if (!inventoryMatches(builderTree, expected.record()) ||
      !expectedAuthorityMatchesInitial(expected)) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(
        CoreLibraryIssue::DistributionMismatch);
  }
  if (expected.record().editionYear() != projectedEditionYear) {
    return CoreDistributionAdmissionFailure::withoutCoordinate(CoreLibraryIssue::EditionMismatch);
  }
  ZC_IF_SOME(issue, snapshot.finish()) { return mapMaterializationIssue(issue); }

  auto capability = VerifiedCoreSourceRoot(zc::heap<VerifiedCoreSourceRoot::Impl>());
  return VerifiedCoreDistribution(zc::heap<VerifiedCoreDistribution::Impl>(
      expected.record().clone(), expected.digest(), expected.policyTemplate().clone(),
      zc::mv(capability), zc::mv(admittedSnapshots)));
}

}  // namespace zomlang::compiler::source::core
