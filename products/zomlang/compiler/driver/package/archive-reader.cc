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

#include "zomlang/compiler/driver/package/archive-reader.h"

#include <cstdint>

#include "archive.h"
#include "archive_entry.h"
#include "zomlang/compiler/identity/canonical-scalar.h"

namespace zomlang::compiler::driver::package {
namespace {

struct ArchiveDisposer final {
  static void dispose(archive* value) { archive_read_free(value); }
};

struct InputBridge final {
  InputBridge(ArchiveInput& source, size_t chunkSize)
      : source(source), buffer(zc::heapArray<zc::byte>(chunkSize)) {}

  ArchiveInput& source;
  zc::Array<zc::byte> buffer;
  zc::Maybe<MaterializationIssue> issue;
  uint64_t suppliedBytes = 0;
  bool reachedEnd = false;
};

la_ssize_t readCallback(archive*, void* opaque, const void** output) {
  auto& bridge = *static_cast<InputBridge*>(opaque);
  auto result = bridge.source.read(bridge.buffer.asPtr());
  if (result.is<MaterializationIssue>()) {
    bridge.issue = result.get<MaterializationIssue>();
    *output = nullptr;
    return ARCHIVE_FATAL;
  }
  if (result.is<ArchiveInputEnd>()) {
    bridge.reachedEnd = true;
    *output = nullptr;
    return 0;
  }

  const size_t count = result.get<ArchiveInputData>().byteCount;
  if (count == 0 || count > bridge.buffer.size() || bridge.suppliedBytes > UINT64_MAX - count) {
    bridge.issue = count == 0 || count > bridge.buffer.size()
                       ? MaterializationIssue::SourceReadFailed
                       : MaterializationIssue::LengthOverflow;
    *output = nullptr;
    return ARCHIVE_FATAL;
  }
  bridge.suppliedBytes += count;
  *output = bridge.buffer.begin();
  return static_cast<la_ssize_t>(count);
}

zc::Maybe<MaterializationIssue> callbackIssue(const InputBridge& bridge) {
  ZC_IF_SOME(issue, bridge.issue) { return issue; }
  return zc::none;
}

zc::Maybe<MaterializationIssue> validateLimits(const SourceAdmissionLimits& limits) {
  if (limits.ioChunkBytes == 0 || limits.ioChunkBytes > static_cast<uint64_t>(SIZE_MAX)) {
    return MaterializationIssue::LengthOverflow;
  }
  return zc::none;
}

zc::Maybe<MaterializationIssue> validatePath(zc::StringPtr path,
                                             const SourceAdmissionLimits& limits) {
  if (path.size() == 0) { return MaterializationIssue::EmptySegment; }
  if (path.size() > limits.archiveMetadataBytes) {
    return MaterializationIssue::ArchiveMetadataLimit;
  }
  if (path.size() > 4096) { return MaterializationIssue::PathTooLong; }
  if (path[0] == '/') { return MaterializationIssue::AbsolutePath; }

  size_t segmentStart = 0;
  size_t depth = 0;
  for (size_t index = 0; index <= path.size(); ++index) {
    if (index < path.size() && path[index] == '\\') { return MaterializationIssue::BackslashPath; }
    if (index < path.size() && path[index] != '/') { continue; }
    if (index == segmentStart) { return MaterializationIssue::EmptySegment; }
    const zc::String segmentText = zc::heapString(path.slice(segmentStart, index));
    const zc::StringPtr segment(segmentText);
    if (segment == "."_zc) { return MaterializationIssue::DotPath; }
    if (segment == ".."_zc) { return MaterializationIssue::ParentPath; }
    if (identity::CanonicalPathSegment::fromSource(segment) == zc::none) {
      return MaterializationIssue::InvalidEntryEncoding;
    }
    ++depth;
    if (depth > 128) { return MaterializationIssue::PathTooDeep; }
    segmentStart = index + 1;
  }
  return zc::none;
}

MaterializationIssue entryKindIssue(archive_entry* entry) {
  if (archive_entry_hardlink_is_set(entry) != 0) { return MaterializationIssue::HardLink; }
  if (archive_entry_symlink(entry) != nullptr) { return MaterializationIssue::Symlink; }
  return MaterializationIssue::SpecialFile;
}

}  // namespace

struct ArchiveReader::Impl final {
  explicit Impl(SourceAdmissionLimits sourceLimits) : limits(sourceLimits) {}

  SourceAdmissionLimits limits;
};

ArchiveReader::ArchiveReader(SourceAdmissionLimits limits) : impl(zc::heap<Impl>(limits)) {}

ArchiveReader::~ArchiveReader() noexcept(false) = default;

ArchiveReader::ArchiveReader(ArchiveReader&&) noexcept = default;

ArchiveReader& ArchiveReader::operator=(ArchiveReader&&) noexcept = default;

zc::Maybe<MaterializationIssue> ArchiveReader::read(ArchiveInput& input, ArchiveOutput& output) {
  ZC_IF_SOME(issue, validateLimits(impl->limits)) { return issue; }

  zc::Own<archive, ArchiveDisposer> archiveHandle(archive_read_new());
  if (!archiveHandle) { return MaterializationIssue::ArchiveDecodeFailed; }
  if (archive_read_support_filter_none(archiveHandle.get()) != ARCHIVE_OK ||
      archive_read_support_format_tar(archiveHandle.get()) != ARCHIVE_OK) {
    return MaterializationIssue::UnsupportedArchiveFormat;
  }

  InputBridge bridge(input, static_cast<size_t>(impl->limits.ioChunkBytes));
  const int openStatus =
      archive_read_open(archiveHandle.get(), &bridge, nullptr, readCallback, nullptr);
  ZC_IF_SOME(issue, callbackIssue(bridge)) { return issue; }
  if (openStatus != ARCHIVE_OK) { return MaterializationIssue::ArchiveDecodeFailed; }

  uint64_t headerCount = 0;
  uint64_t fileCount = 0;
  uint64_t totalFileBytes = 0;
  uint64_t metadataBytes = 0;
  bool identifiedUstar = false;

  while (true) {
    archive_entry* entry = nullptr;
    const int headerStatus = archive_read_next_header(archiveHandle.get(), &entry);
    ZC_IF_SOME(issue, callbackIssue(bridge)) { return issue; }
    if (headerStatus == ARCHIVE_EOF) { break; }
    if (headerStatus != ARCHIVE_OK || entry == nullptr) {
      return MaterializationIssue::ArchiveDecodeFailed;
    }
    if (archive_format(archiveHandle.get()) != ARCHIVE_FORMAT_TAR_USTAR) {
      return MaterializationIssue::UnsupportedArchiveFormat;
    }
    identifiedUstar = true;

    if (headerCount == UINT64_MAX) { return MaterializationIssue::LengthOverflow; }
    ++headerCount;
    if (headerCount > impl->limits.archiveHeaderCount) {
      return MaterializationIssue::ArchiveHeaderLimit;
    }

    const char* rawPath = archive_entry_pathname_utf8(entry);
    if (rawPath == nullptr) { return MaterializationIssue::InvalidEntryEncoding; }
    const zc::StringPtr path(rawPath);
    ZC_IF_SOME(issue, validatePath(path, impl->limits)) { return issue; }
    if (metadataBytes > UINT64_MAX - 512 - path.size()) {
      return MaterializationIssue::LengthOverflow;
    }
    metadataBytes += 512 + path.size();
    if (metadataBytes > impl->limits.archiveMetadataBytes) {
      return MaterializationIssue::ArchiveMetadataLimit;
    }

    const auto fileType = archive_entry_filetype(entry);
    if (fileType != AE_IFREG || archive_entry_hardlink_is_set(entry) != 0 ||
        archive_entry_symlink(entry) != nullptr) {
      return entryKindIssue(entry);
    }
    if (archive_entry_size_is_set(entry) == 0 || archive_entry_size(entry) < 0) {
      return MaterializationIssue::ArchiveDecodeFailed;
    }

    const uint64_t fileSize = static_cast<uint64_t>(archive_entry_size(entry));
    const uint64_t paddingBytes = (512 - (fileSize % 512)) % 512;
    if (metadataBytes > UINT64_MAX - paddingBytes) { return MaterializationIssue::LengthOverflow; }
    metadataBytes += paddingBytes;
    if (metadataBytes > impl->limits.archiveMetadataBytes) {
      return MaterializationIssue::ArchiveMetadataLimit;
    }
    if (fileSize > impl->limits.singleFileBytes) { return MaterializationIssue::FileTooLarge; }
    if (fileCount == UINT64_MAX || totalFileBytes > UINT64_MAX - fileSize) {
      return MaterializationIssue::LengthOverflow;
    }
    ++fileCount;
    totalFileBytes += fileSize;
    if (fileCount > impl->limits.fileCount) { return MaterializationIssue::FileCountLimit; }
    if (totalFileBytes > impl->limits.totalFileBytes) {
      return MaterializationIssue::TotalSizeLimit;
    }

    ZC_IF_SOME(issue, output.beginFile(path, fileSize)) { return issue; }
    uint64_t consumed = 0;
    while (true) {
      const void* block = nullptr;
      size_t blockSize = 0;
      la_int64_t blockOffset = 0;
      const int dataStatus =
          archive_read_data_block(archiveHandle.get(), &block, &blockSize, &blockOffset);
      ZC_IF_SOME(issue, callbackIssue(bridge)) { return issue; }
      if (dataStatus == ARCHIVE_EOF) { break; }
      if (dataStatus != ARCHIVE_OK || block == nullptr || blockOffset < 0 ||
          static_cast<uint64_t>(blockOffset) != consumed || consumed > UINT64_MAX - blockSize) {
        return MaterializationIssue::ArchiveDecodeFailed;
      }
      consumed += blockSize;
      if (consumed > fileSize) { return MaterializationIssue::ArchiveDecodeFailed; }
      const auto* bytes = static_cast<const zc::byte*>(block);
      ZC_IF_SOME(issue, output.write(zc::ArrayPtr<const zc::byte>(bytes, blockSize))) {
        return issue;
      }
    }
    if (consumed != fileSize) { return MaterializationIssue::ArchiveDecodeFailed; }
    ZC_IF_SOME(issue, output.endFile()) { return issue; }
  }

  if (!identifiedUstar) { return MaterializationIssue::UnsupportedArchiveFormat; }
  const la_int64_t consumedBytes = archive_filter_bytes(archiveHandle.get(), -1);
  if (consumedBytes < 0 || static_cast<uint64_t>(consumedBytes) > bridge.suppliedBytes) {
    return MaterializationIssue::ArchiveDecodeFailed;
  }
  if (static_cast<uint64_t>(consumedBytes) != bridge.suppliedBytes) {
    return MaterializationIssue::TrailingArchiveData;
  }
  zc::byte trailingByte = 0;
  auto trailing = input.read(zc::arrayPtr(&trailingByte, 1));
  if (trailing.is<MaterializationIssue>()) { return trailing.get<MaterializationIssue>(); }
  if (trailing.is<ArchiveInputData>()) { return MaterializationIssue::TrailingArchiveData; }
  return zc::none;
}

}  // namespace zomlang::compiler::driver::package
