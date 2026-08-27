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

#include "compiler/driver/package/source-snapshot.h"

#include "zc/core/exception.h"
#include "compiler/driver/package/archive-reader.h"
#include "compiler/identity/canonical/canonical-encoder.h"
#include "compiler/identity/canonical/canonical-scalar.h"
#include "compiler/identity/text/unicode-normalization.h"

namespace zomlang::compiler::driver::package {
namespace {

class ReplacementFreshSourceDirectory final : public FreshSourceDirectory {
public:
  explicit ReplacementFreshSourceDirectory(
      zc::Own<const zc::Directory>&& parent,
      zc::Own<zc::Directory::Replacer<zc::Directory>>&& replacer) noexcept
      : parent(zc::mv(parent)), replacer(zc::mv(replacer)) {}
  ~ReplacementFreshSourceDirectory() noexcept override { finish(); }

  const zc::Directory& root() const override { return replacer->get(); }

  zc::Maybe<MaterializationIssue> finish() override {
    auto exception = zc::runCatchingExceptions([&]() { replacer = nullptr; });
    return exception == zc::none ? zc::Maybe<MaterializationIssue>()
                                 : MaterializationIssue::SnapshotCleanupFailed;
  }

private:
  zc::Own<const zc::Directory> parent;
  mutable zc::Own<zc::Directory::Replacer<zc::Directory>> replacer;
};

zc::Path filesystemPath(const identity::CanonicalRelativePath& path) {
  zc::Path result(nullptr);
  for (const auto& segment : path.segments()) { result = zc::mv(result).append(segment.text()); }
  return result;
}

zc::Maybe<zc::Path> canonicalFilesystemPath(zc::StringPtr raw) {
  zc::Path result(nullptr);
  size_t segmentStart = 0;
  for (size_t index = 0; index <= raw.size(); ++index) {
    if (index < raw.size() && raw[index] != '/') { continue; }
    const auto storage = zc::heapString(raw.slice(segmentStart, index));
    auto segment = identity::CanonicalPathSegment::fromSource(storage);
    if (segment == zc::none) { return zc::none; }
    ZC_IF_SOME(value, segment) { result = zc::mv(result).append(value.text()); }
    segmentStart = index + 1;
  }
  return zc::mv(result);
}

const SourceTreeFile* findFile(const SourceTreeRecord& record,
                               const identity::CanonicalRelativePath& path) {
  identity::CanonicalEncoder expectedEncoder;
  path.encode(expectedEncoder);
  const auto expected = expectedEncoder.finish();
  for (const auto& file : record.files()) {
    identity::CanonicalEncoder candidateEncoder;
    file.path().encode(candidateEncoder);
    if (candidateEncoder.finish().asPtr() == expected.asPtr()) { return &file; }
  }
  return nullptr;
}

class DirectoryArchiveOutput final : public ArchiveOutput {
public:
  explicit DirectoryArchiveOutput(const zc::Directory& root, uint64_t verificationChunkSize,
                                  zc::Maybe<SourceMaterializationObserver&> observer = zc::none)
      : root(root), verificationChunkSize(verificationChunkSize), observer(observer) {}

  zc::Maybe<MaterializationIssue> beginFile(zc::StringPtr path, uint64_t byteLength) override {
    if (verificationChunkSize == 0 || verificationChunkSize > SIZE_MAX) {
      return MaterializationIssue::LengthOverflow;
    }
    ZC_IF_SOME(issue, builder.beginFile(path, byteLength)) { return issue; }
    ZC_IF_SOME(value, observer) {
      ZC_IF_SOME(issue, value.beforeDestinationCreate()) { return issue; }
    }
    auto canonicalPath = canonicalFilesystemPath(path);
    if (canonicalPath == zc::none) { return MaterializationIssue::InvalidEntryEncoding; }
    try {
      ZC_IF_SOME(value, canonicalPath) {
        activePath = value.clone();
        file = root.openFile(
            value, zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT | zc::WriteMode::PRIVATE);
      }
    } catch (const zc::Exception&) { return MaterializationIssue::DestinationCreateFailed; }
    offset = 0;
    expectedLength = byteLength;
    expectedHasher = zc::heap<identity::Sha256Hasher>();
    return zc::none;
  }

  zc::Maybe<MaterializationIssue> write(zc::ArrayPtr<const zc::byte> bytes) override {
    if (!file) { return MaterializationIssue::DestinationWriteFailed; }
    ZC_IF_SOME(value, observer) {
      ZC_IF_SOME(issue, value.beforeDestinationWrite()) { return issue; }
    }
    try {
      file->write(offset, bytes);
    } catch (const zc::Exception&) { return MaterializationIssue::DestinationWriteFailed; }
    offset += bytes.size();
    if (!expectedHasher || !expectedHasher->update(bytes)) {
      return MaterializationIssue::LengthOverflow;
    }
    return builder.write(bytes);
  }

  zc::Maybe<MaterializationIssue> endFile() override {
    if (!file) { return MaterializationIssue::DestinationWriteFailed; }
    ZC_IF_SOME(value, observer) {
      ZC_IF_SOME(issue, value.beforeDestinationSync()) { return issue; }
    }
    try {
      file->sync();
    } catch (const zc::Exception&) { return MaterializationIssue::DestinationSyncFailed; }
    auto expectedDigest = expectedHasher->finish();
    if (expectedDigest == zc::none) { return MaterializationIssue::LengthOverflow; }
    try {
      const auto pathMetadata = root.lstat(activePath);
      const auto fileMetadata = file->stat();
      if (pathMetadata.type != zc::FsNode::Type::FILE || fileMetadata.type != pathMetadata.type ||
          pathMetadata.linkCount != 1 || fileMetadata.linkCount != pathMetadata.linkCount ||
          pathMetadata.size != expectedLength || fileMetadata.size != pathMetadata.size ||
          (pathMetadata.hashCode != 0 && fileMetadata.hashCode != pathMetadata.hashCode)) {
        return MaterializationIssue::SourceTreeDigestMismatch;
      }
      auto buffer = zc::heapArray<zc::byte>(static_cast<size_t>(verificationChunkSize));
      identity::Sha256Hasher actualHasher;
      uint64_t readOffset = 0;
      while (readOffset < expectedLength) {
        const uint64_t remaining = expectedLength - readOffset;
        const size_t request = static_cast<size_t>(zc::min(verificationChunkSize, remaining));
        const size_t count = file->read(readOffset, buffer.first(request));
        if (count == 0 || !actualHasher.update(buffer.first(count))) {
          return MaterializationIssue::SourceTreeDigestMismatch;
        }
        readOffset += count;
      }
      const auto finalMetadata = file->stat();
      if (finalMetadata.type != fileMetadata.type ||
          finalMetadata.linkCount != fileMetadata.linkCount ||
          finalMetadata.size != fileMetadata.size ||
          finalMetadata.hashCode != fileMetadata.hashCode) {
        return MaterializationIssue::SourceTreeDigestMismatch;
      }
      auto actualDigest = actualHasher.finish();
      if (actualDigest == zc::none) { return MaterializationIssue::LengthOverflow; }
      ZC_IF_SOME(expected, expectedDigest) {
        ZC_IF_SOME(actual, actualDigest) {
          if (actual != expected) { return MaterializationIssue::SourceTreeDigestMismatch; }
        }
      }
    } catch (const zc::Exception&) { return MaterializationIssue::SourceReadFailed; }
    file = nullptr;
    expectedHasher = nullptr;
    return builder.endFile();
  }

  SourceTreeBuildResult finish() { return builder.finish(); }

private:
  const zc::Directory& root;
  uint64_t verificationChunkSize;
  zc::Maybe<SourceMaterializationObserver&> observer;
  SourceTreeBuilder builder;
  zc::Own<const zc::File> file;
  zc::Path activePath = nullptr;
  uint64_t offset = 0;
  uint64_t expectedLength = 0;
  zc::Own<identity::Sha256Hasher> expectedHasher;
};

class FreshDirectoryCleanupGuard final {
public:
  explicit FreshDirectoryCleanupGuard(zc::Own<FreshSourceDirectory>&& directory)
      : directory(zc::mv(directory)) {}
  ~FreshDirectoryCleanupGuard() noexcept {
    if (!directory) { return; }
    try {
      (void)directory->finish();
    } catch (...) {}
  }

  ZC_DISALLOW_COPY_AND_MOVE(FreshDirectoryCleanupGuard);

  const zc::Directory& root() const { return directory->root(); }
  zc::Own<FreshSourceDirectory> release() { return zc::mv(directory); }

private:
  zc::Own<FreshSourceDirectory> directory;
};

VerifiedFileReadResult readVerified(const zc::ReadableDirectory& root,
                                    const SourceTreeFile& expected, uint64_t chunkSize) {
  const auto path = filesystemPath(expected.path());
  try {
    const auto metadata = root.lstat(path);
    if (metadata.type != zc::FsNode::Type::FILE || metadata.linkCount != 1 ||
        metadata.size != expected.byteLength()) {
      return MaterializationIssue::SourceChangedDuringSnapshot;
    }
    auto file = root.openFile(path);
    const auto openedMetadata = file->stat();
    if (openedMetadata.type != zc::FsNode::Type::FILE || openedMetadata.linkCount != 1 ||
        openedMetadata.size != expected.byteLength() || expected.byteLength() > SIZE_MAX) {
      return MaterializationIssue::SourceChangedDuringSnapshot;
    }
    auto bytes = zc::heapArray<zc::byte>(static_cast<size_t>(expected.byteLength()));
    identity::Sha256Hasher hasher;
    uint64_t offset = 0;
    while (offset < expected.byteLength()) {
      const uint64_t remaining = expected.byteLength() - offset;
      const size_t request = static_cast<size_t>(zc::min(chunkSize, remaining));
      const size_t count = file->read(
          offset, bytes.slice(static_cast<size_t>(offset), static_cast<size_t>(offset) + request));
      if (count == 0 || !hasher.update(bytes.slice(static_cast<size_t>(offset),
                                                   static_cast<size_t>(offset) + count))) {
        return MaterializationIssue::SourceChangedDuringSnapshot;
      }
      offset += count;
    }
    auto digest = hasher.finish();
    ZC_IF_SOME(value, digest) {
      if (value == expected.contentDigest()) { return zc::mv(bytes); }
    }
    return MaterializationIssue::SourceChangedDuringSnapshot;
  } catch (const zc::Exception&) { return MaterializationIssue::SourceReadFailed; }
}

struct WalkEntry final {
  zc::String raw;
  zc::String canonical;
  zc::String folded;
  zc::FsNode::Type type;
};

using WalkEntriesResult = zc::OneOf<zc::Vector<WalkEntry>, MaterializationIssue>;

WalkEntriesResult admittedEntries(const zc::ReadableDirectory& directory) {
  zc::Vector<WalkEntry> result;
  try {
    auto entries = directory.listEntries();
    for (auto& entry : entries) {
      auto canonical = identity::CanonicalPathSegment::fromSource(entry.name);
      if (canonical == zc::none) { return MaterializationIssue::InvalidEntryEncoding; }
      zc::String canonicalText;
      zc::String foldedText;
      ZC_IF_SOME(value, canonical) {
        canonicalText = zc::str(value.text());
        auto folded = identity::fullCaseFold(value.text());
        if (folded == zc::none) { return MaterializationIssue::InvalidEntryEncoding; }
        ZC_IF_SOME(foldedValue, folded) { foldedText = zc::mv(foldedValue); }
      }
      for (const auto& prior : result) {
        if (prior.raw == entry.name) { return MaterializationIssue::DuplicatePath; }
        if (prior.canonical == canonicalText) { return MaterializationIssue::UnicodeCollision; }
        if (prior.folded == foldedText) { return MaterializationIssue::CaseFoldCollision; }
      }
      result.add(
          WalkEntry{zc::mv(entry.name), zc::mv(canonicalText), zc::mv(foldedText), entry.type});
    }
  } catch (const zc::Exception&) { return MaterializationIssue::SourceReadFailed; }

  for (size_t index = 1; index < result.size(); ++index) {
    auto current = zc::mv(result[index]);
    size_t insertion = index;
    while (insertion != 0 && current.canonical < result[insertion - 1].canonical) {
      result[insertion] = zc::mv(result[insertion - 1]);
      --insertion;
    }
    result[insertion] = zc::mv(current);
  }
  return zc::mv(result);
}

struct WalkCounts final {
  uint64_t files = 0;
  uint64_t bytes = 0;
};

zc::Maybe<MaterializationIssue> scanDirectory(const zc::ReadableDirectory& directory,
                                              zc::StringPtr prefix, ArchiveOutput& output,
                                              const SourceAdmissionLimits& limits,
                                              WalkCounts& counts, size_t depth = 0,
                                              size_t canonicalPrefixLength = 0) {
  auto admitted = admittedEntries(directory);
  if (admitted.is<MaterializationIssue>()) { return admitted.get<MaterializationIssue>(); }
  auto entries = zc::mv(admitted.get<zc::Vector<WalkEntry>>());
  for (const auto& entry : entries) {
    if (depth == 128) { return MaterializationIssue::PathTooDeep; }
    const size_t separatorLength = canonicalPrefixLength == 0 ? 0 : 1;
    if (canonicalPrefixLength > 4096 - separatorLength ||
        entry.canonical.size() > 4096 - canonicalPrefixLength - separatorLength) {
      return MaterializationIssue::PathTooLong;
    }
    const size_t canonicalLength = canonicalPrefixLength + separatorLength + entry.canonical.size();
    const auto relativePath =
        prefix.size() == 0 ? zc::str(entry.raw) : zc::str(prefix, "/"_zc, entry.raw);
    try {
      const zc::Path childPath(entry.raw);
      const auto metadata = directory.lstat(childPath);
      if (metadata.type == zc::FsNode::Type::DIRECTORY) {
        auto child = directory.openSubdir(childPath);
        const auto opened = child->stat();
        if (opened.type != zc::FsNode::Type::DIRECTORY ||
            (metadata.hashCode != 0 && opened.hashCode != metadata.hashCode)) {
          return MaterializationIssue::SourceChangedDuringSnapshot;
        }
        ZC_IF_SOME(issue, scanDirectory(*child, relativePath, output, limits, counts, depth + 1,
                                        canonicalLength)) {
          return issue;
        }
        continue;
      }
      if (metadata.type == zc::FsNode::Type::SYMLINK) { return MaterializationIssue::Symlink; }
      if (metadata.type != zc::FsNode::Type::FILE) { return MaterializationIssue::SpecialFile; }
      if (metadata.linkCount != 1) { return MaterializationIssue::HardLink; }
      if (metadata.size > limits.singleFileBytes) { return MaterializationIssue::FileTooLarge; }
      if (counts.files == UINT64_MAX || counts.bytes > UINT64_MAX - metadata.size) {
        return MaterializationIssue::LengthOverflow;
      }
      ++counts.files;
      counts.bytes += metadata.size;
      if (counts.files > limits.fileCount) { return MaterializationIssue::FileCountLimit; }
      if (counts.bytes > limits.totalFileBytes) { return MaterializationIssue::TotalSizeLimit; }

      auto file = directory.openFile(childPath);
      const auto opened = file->stat();
      if (opened.type != zc::FsNode::Type::FILE || opened.linkCount != 1 ||
          opened.size != metadata.size ||
          (metadata.hashCode != 0 && opened.hashCode != metadata.hashCode)) {
        return MaterializationIssue::SourceChangedDuringSnapshot;
      }
      ZC_IF_SOME(issue, output.beginFile(relativePath, metadata.size)) { return issue; }
      auto buffer = zc::heapArray<zc::byte>(static_cast<size_t>(limits.ioChunkBytes));
      uint64_t offset = 0;
      while (offset < metadata.size) {
        const uint64_t remaining = metadata.size - offset;
        const size_t request = static_cast<size_t>(zc::min(limits.ioChunkBytes, remaining));
        const size_t count = file->read(offset, buffer.first(request));
        if (count == 0) { return MaterializationIssue::SourceChangedDuringSnapshot; }
        ZC_IF_SOME(issue, output.write(buffer.first(count))) { return issue; }
        offset += count;
      }
      if (file->stat().size != metadata.size) {
        return MaterializationIssue::SourceChangedDuringSnapshot;
      }
      ZC_IF_SOME(issue, output.endFile()) { return issue; }
    } catch (const zc::Exception&) { return MaterializationIssue::SourceReadFailed; }
  }
  return zc::none;
}

bool sameRecord(const SourceTreeRecord& left, const SourceTreeRecord& right) {
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

zc::String canonicalPathText(const identity::CanonicalRelativePath& path) {
  zc::Vector<char> text;
  for (size_t index = 0; index < path.segments().size(); ++index) {
    if (index != 0) { text.add('/'); }
    text.addAll(path.segments()[index].text());
  }
  return zc::str(text.releaseAsArray());
}

zc::Maybe<MaterializationIssue> copyVerifiedFile(const zc::ReadableDirectory& source,
                                                 const SourceTreeFile& expected,
                                                 ArchiveOutput& destination, uint64_t chunkSize) {
  const auto path = filesystemPath(expected.path());
  try {
    const auto metadata = source.lstat(path);
    if (metadata.type != zc::FsNode::Type::FILE || metadata.linkCount != 1 ||
        metadata.size != expected.byteLength()) {
      return MaterializationIssue::SourceChangedDuringSnapshot;
    }
    auto file = source.openFile(path);
    const auto opened = file->stat();
    if (opened.type != zc::FsNode::Type::FILE || opened.linkCount != 1 ||
        opened.size != expected.byteLength() ||
        (metadata.hashCode != 0 && opened.hashCode != metadata.hashCode)) {
      return MaterializationIssue::SourceChangedDuringSnapshot;
    }
    auto pathText = canonicalPathText(expected.path());
    ZC_IF_SOME(issue, destination.beginFile(pathText, expected.byteLength())) { return issue; }
    auto buffer = zc::heapArray<zc::byte>(static_cast<size_t>(chunkSize));
    identity::Sha256Hasher hasher;
    uint64_t offset = 0;
    while (offset < expected.byteLength()) {
      const uint64_t remaining = expected.byteLength() - offset;
      const size_t request = static_cast<size_t>(zc::min(chunkSize, remaining));
      const size_t count = file->read(offset, buffer.first(request));
      if (count == 0 || !hasher.update(buffer.first(count))) {
        return MaterializationIssue::SourceChangedDuringSnapshot;
      }
      ZC_IF_SOME(issue, destination.write(buffer.first(count))) { return issue; }
      offset += count;
    }
    if (file->stat().size != expected.byteLength()) {
      return MaterializationIssue::SourceChangedDuringSnapshot;
    }
    auto digest = hasher.finish();
    if (digest == zc::none) { return MaterializationIssue::LengthOverflow; }
    ZC_IF_SOME(value, digest) {
      if (value != expected.contentDigest()) {
        return MaterializationIssue::SourceChangedDuringSnapshot;
      }
    }
    return destination.endFile();
  } catch (const zc::Exception&) { return MaterializationIssue::SourceReadFailed; }
}

}  // namespace

ReplacementFreshSourceDirectoryFactory::ReplacementFreshSourceDirectoryFactory(
    const zc::Directory& directory)
    : parent(directory.clone()) {}

FreshSourceDirectoryResult ReplacementFreshSourceDirectoryFactory::create() {
  try {
    auto owner = parent->clone();
    auto replacer = owner->replaceSubdir(zc::Path("zom-source-snapshot"_zc), zc::WriteMode::CREATE);
    zc::Own<FreshSourceDirectory> result =
        zc::heap<ReplacementFreshSourceDirectory>(zc::mv(owner), zc::mv(replacer));
    return zc::mv(result);
  } catch (const zc::Exception&) { return MaterializationIssue::FreshDirectoryCreateFailed; }
}

struct DigestVerifiedSourceSnapshot::Impl final {
  Impl(zc::Own<FreshSourceDirectory>&& directory, SourceTreeRecord&& record)
      : directory(zc::mv(directory)), record(zc::mv(record)) {}

  zc::Own<FreshSourceDirectory> directory;
  SourceTreeRecord record;
  bool finished = false;
};

DigestVerifiedSourceSnapshot::DigestVerifiedSourceSnapshot(
    zc::Own<FreshSourceDirectory>&& directory, SourceTreeRecord&& record)
    : impl(zc::heap<Impl>(zc::mv(directory), zc::mv(record))) {}

DigestVerifiedSourceSnapshot::~DigestVerifiedSourceSnapshot() noexcept {
  if (!impl || impl->finished) { return; }
  try {
    (void)impl->directory->finish();
  } catch (...) {}
}

DigestVerifiedSourceSnapshot::DigestVerifiedSourceSnapshot(
    DigestVerifiedSourceSnapshot&&) noexcept = default;
DigestVerifiedSourceSnapshot& DigestVerifiedSourceSnapshot::operator=(
    DigestVerifiedSourceSnapshot&&) noexcept = default;

const SourceTreeRecord& DigestVerifiedSourceSnapshot::record() const noexcept {
  return impl->record;
}

VerifiedFileReadResult DigestVerifiedSourceSnapshot::readVerifiedFile(
    const identity::CanonicalRelativePath& path) const {
  if (impl->finished) { return MaterializationIssue::SourceReadFailed; }
  const auto* expected = findFile(impl->record, path);
  if (expected == nullptr) { return MaterializationIssue::SourceReadFailed; }
  return readVerified(impl->directory->root(), *expected, 1048576);
}

SourceSnapshotCopyResult DigestVerifiedSourceSnapshot::materializeVerifiedCopy(
    FreshSourceDirectoryFactory& factory) const {
  if (impl->finished) { return MaterializationIssue::SourceReadFailed; }
  auto fresh = factory.create();
  if (fresh.is<MaterializationIssue>()) { return fresh.get<MaterializationIssue>(); }
  FreshDirectoryCleanupGuard directory(zc::mv(fresh.get<zc::Own<FreshSourceDirectory>>()));
  DirectoryArchiveOutput output(directory.root(), 1048576);
  for (const auto& file : impl->record.files()) {
    ZC_IF_SOME(issue, copyVerifiedFile(impl->directory->root(), file, output, 1048576)) {
      return issue;
    }
  }
  auto record = output.finish();
  if (record.is<MaterializationIssue>()) { return record.get<MaterializationIssue>(); }
  auto value = zc::mv(record.get<SourceTreeRecord>());
  if (!sameRecord(impl->record, value)) { return MaterializationIssue::SourceTreeDigestMismatch; }
  try {
    directory.root().sync();
  } catch (const zc::Exception&) { return MaterializationIssue::DestinationSyncFailed; }
  auto snapshot = DigestVerifiedSourceSnapshot(directory.release(), zc::mv(value));
  return zc::heap<DigestVerifiedSourceSnapshot>(zc::mv(snapshot));
}

zc::Maybe<MaterializationIssue> DigestVerifiedSourceSnapshot::finish() {
  if (impl->finished) { return zc::none; }
  try {
    ZC_IF_SOME(issue, impl->directory->finish()) { return issue; }
  } catch (...) { return MaterializationIssue::SnapshotCleanupFailed; }
  impl->finished = true;
  return zc::none;
}

ResolvedPackageSourceSnapshot::ResolvedPackageSourceSnapshot(
    identity::PackageBaseKey&& package, DigestVerifiedSourceSnapshot&& snapshot) noexcept
    : packageValue(zc::mv(package)), snapshotValue(zc::mv(snapshot)) {}

ResolvedPackageSourceSnapshot ResolvedPackageSourceSnapshot::from(
    identity::PackageBaseKey&& package, DigestVerifiedSourceSnapshot&& snapshot) {
  return ResolvedPackageSourceSnapshot(zc::mv(package), zc::mv(snapshot));
}

const identity::PackageBaseKey& ResolvedPackageSourceSnapshot::package() const noexcept {
  return packageValue;
}

const DigestVerifiedSourceSnapshot& ResolvedPackageSourceSnapshot::snapshot() const noexcept {
  return snapshotValue;
}

zc::Maybe<MaterializationIssue> ResolvedPackageSourceSnapshot::finish() {
  return snapshotValue.finish();
}

SourceArchiveMaterializer::SourceArchiveMaterializer(SourceAdmissionLimits limits)
    : limits(limits) {}

SourceSnapshotResult SourceArchiveMaterializer::materialize(
    ZstdInput& input, FreshSourceDirectoryFactory& factory,
    zc::Maybe<SourceMaterializationObserver&> observer) {
  auto fresh = factory.create();
  if (fresh.is<MaterializationIssue>()) { return fresh.get<MaterializationIssue>(); }
  FreshDirectoryCleanupGuard directory(zc::mv(fresh.get<zc::Own<FreshSourceDirectory>>()));

  ZstdDecoder decoder(limits);
  auto decoded = decoder.openDecodedInput(input);
  if (decoded.is<MaterializationIssue>()) { return decoded.get<MaterializationIssue>(); }
  auto archiveInput = zc::mv(decoded.get<zc::Own<ArchiveInput>>());
  ArchiveReader reader(limits);
  DirectoryArchiveOutput output(directory.root(), limits.ioChunkBytes, observer);
  ZC_IF_SOME(issue, reader.read(*archiveInput, output)) { return issue; }
  try {
    ZC_IF_SOME(value, observer) {
      ZC_IF_SOME(issue, value.beforeDestinationSync()) { return issue; }
    }
    directory.root().sync();
  } catch (const zc::Exception&) { return MaterializationIssue::DestinationSyncFailed; }
  auto record = output.finish();
  if (record.is<MaterializationIssue>()) { return record.get<MaterializationIssue>(); }
  auto value = zc::mv(record.get<SourceTreeRecord>());
  return DigestVerifiedSourceSnapshot(directory.release(), zc::mv(value));
}

SourceDirectoryMaterializer::SourceDirectoryMaterializer(SourceAdmissionLimits limits)
    : limits(limits) {}

SourceTreeBuildResult inspectSourceDirectory(const zc::ReadableDirectory& source,
                                             SourceAdmissionLimits limits) {
  if (limits.ioChunkBytes == 0 || limits.ioChunkBytes > SIZE_MAX) {
    return MaterializationIssue::LengthOverflow;
  }
  SourceTreeBuilder builder;
  WalkCounts counts;
  ZC_IF_SOME(issue, scanDirectory(source, ""_zc, builder, limits, counts)) { return issue; }
  return builder.finish();
}

SourceSnapshotResult SourceDirectoryMaterializer::materialize(
    const zc::ReadableDirectory& source, FreshSourceDirectoryFactory& factory,
    zc::Maybe<SourceMaterializationObserver&> observer) {
  if (limits.ioChunkBytes == 0 || limits.ioChunkBytes > SIZE_MAX) {
    return MaterializationIssue::LengthOverflow;
  }
  SourceTreeBuilder firstBuilder;
  WalkCounts firstCounts;
  ZC_IF_SOME(issue, scanDirectory(source, ""_zc, firstBuilder, limits, firstCounts)) {
    return issue;
  }
  auto firstResult = firstBuilder.finish();
  if (firstResult.is<MaterializationIssue>()) { return firstResult.get<MaterializationIssue>(); }
  auto first = zc::mv(firstResult.get<SourceTreeRecord>());
  ZC_IF_SOME(value, observer) {
    ZC_IF_SOME(issue, value.afterFirstInventory()) { return issue; }
  }

  auto fresh = factory.create();
  if (fresh.is<MaterializationIssue>()) { return fresh.get<MaterializationIssue>(); }
  FreshDirectoryCleanupGuard directory(zc::mv(fresh.get<zc::Own<FreshSourceDirectory>>()));
  DirectoryArchiveOutput secondOutput(directory.root(), limits.ioChunkBytes, observer);
  WalkCounts secondCounts;
  ZC_IF_SOME(issue, scanDirectory(source, ""_zc, secondOutput, limits, secondCounts)) {
    return issue;
  }
  auto secondResult = secondOutput.finish();
  if (secondResult.is<MaterializationIssue>()) { return secondResult.get<MaterializationIssue>(); }
  auto second = zc::mv(secondResult.get<SourceTreeRecord>());
  if (!sameRecord(first, second)) { return MaterializationIssue::SourceChangedDuringSnapshot; }
  try {
    ZC_IF_SOME(value, observer) {
      ZC_IF_SOME(issue, value.beforeDestinationSync()) { return issue; }
    }
    directory.root().sync();
  } catch (const zc::Exception&) { return MaterializationIssue::DestinationSyncFailed; }
  return DigestVerifiedSourceSnapshot(directory.release(), zc::mv(second));
}

}  // namespace zomlang::compiler::driver::package
