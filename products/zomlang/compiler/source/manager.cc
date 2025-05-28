// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/source/manager.h"

#include <algorithm>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace source {

// ================================================================================
// BufferId::Impl

struct BufferId::Impl {
  uint64_t value;
  Impl(uint64_t val) : value(val) {}
};

// ================================================================================
// BufferId

BufferId::BufferId(uint64_t val) noexcept : impl(zc::heap<Impl>(val)) {}
BufferId::~BufferId() noexcept(false) = default;

BufferId::BufferId(const BufferId& other) : impl(zc::heap<Impl>(*other.impl)) {}
BufferId& BufferId::operator=(const BufferId& other) {
  if (this != &other) { *impl = *other.impl; }
  return *this;
}

BufferId::BufferId(BufferId&& other) noexcept = default;
BufferId& BufferId::operator=(BufferId&& other) noexcept = default;

BufferId::operator uint64_t() const { return impl->value; }

bool BufferId::operator==(const BufferId& other) const { return impl->value == other.impl->value; }
bool BufferId::operator!=(const BufferId& other) const { return !(*this == other); }
bool BufferId::operator<(const BufferId& other) const { return impl->value < other.impl->value; }
bool BufferId::operator>(const BufferId& other) const { return other < *this; }
bool BufferId::operator<=(const BufferId& other) const { return !(*this > other); }
bool BufferId::operator>=(const BufferId& other) const { return !(*this < other); }

bool BufferId::isValid() const { return impl->value != 0; }

// ================================================================================
// SourceManager::Impl

struct GeneratedSourceInfo {
  CharSourceRange originalSourceRange;
  CharSourceRange generatedSourceRange;
};

struct Buffer {
  /// Unique Id
  const BufferId id;
  /// Path in file system
  zc::String identifier;
  /// Content of buffer
  zc::Array<zc::byte> data;
  /// The original source location of this buffer.
  GeneratedSourceInfo generatedInfo;
  /// The offset in bytes of the first character in the buffer.
  mutable zc::Vector<unsigned> lineStartOffsets;

  Buffer(const BufferId id, zc::String identifier, zc::Array<zc::byte> data)
      : id(id), identifier(zc::mv(identifier)), data(zc::mv(data)) {}

  const zc::byte* getBufferStart() const { return data.begin(); }
  const zc::byte* getBufferEnd() const { return data.end(); }

  ZC_NODISCARD size_t getBufferSize() const { return data.size(); }
};

/// Compare the source location ranges for two impl->buffers, as an ordering to
/// use for fast searches.
struct BufferIDRangeComparator {
  const SourceManager& sourceManager;

  bool operator()(BufferId lhsID, BufferId rhsID) const {
    auto lhsRange = sourceManager.getRangeForBuffer(lhsID);
    auto rhsRange = sourceManager.getRangeForBuffer(rhsID);

    // If the source impl->buffers are identical, we want the higher-numbered
    // source impl->buffers to occur first. This is important when uniquing.
    if (lhsRange == rhsRange) return lhsID > rhsID;

    std::less<const zc::byte*> pointerCompare;
    return pointerCompare(lhsRange.getStart().getOpaqueValue(),
                          rhsRange.getStart().getOpaqueValue());
  }

  bool operator()(BufferId lhsID, SourceLoc rhsLoc) const {
    auto lhsRange = sourceManager.getRangeForBuffer(lhsID);

    std::less<const zc::byte*> pointerCompare;
    return pointerCompare(lhsRange.getEnd().getOpaqueValue(), rhsLoc.getOpaqueValue());
  }

  bool operator()(SourceLoc lhsLoc, BufferId rhsID) const {
    auto rhsRange = sourceManager.getRangeForBuffer(rhsID);

    std::less<const zc::byte*> pointerCompare;
    return pointerCompare(lhsLoc.getOpaqueValue(), rhsRange.getEnd().getOpaqueValue());
  }
};

/// Determine whether the source ranges for two impl->buffers are equivalent.
struct BufferIDSameRange {
  const SourceManager& sourceMgr;

  bool operator()(BufferId lhsID, BufferId rhsID) const {
    auto lhsRange = sourceMgr.getRangeForBuffer(lhsID);
    auto rhsRange = sourceMgr.getRangeForBuffer(rhsID);

    return lhsRange == rhsRange;
  }
};

struct BufferLocCache {
  zc::Vector<BufferId> sortedBuffers;
  uint64_t numBuffersOriginal = 0;
  zc::Maybe<BufferId> lastBufferId;
};

struct SourceManager::Impl {
  Impl() noexcept : fs(zc::newDiskFilesystem()) {}
  ~Impl() noexcept(false) = default;

  /// The filesystem to use for reading files.
  zc::Own<const zc::Filesystem> fs;
  /// File a path to BufferID mapping cache
  zc::HashMap<zc::String, BufferId> pathToBufferId;
  /// Whether to open in volatile mode (disallow memory mappings)
  bool openAsVolatile = false;

  zc::Vector<VirtualFile> virtualFiles;
  zc::Vector<SourceLoc> regexLiteralStartLocs;

  zc::Vector<zc::Own<Buffer>> buffers;
  /// Fast lookup from buffer ID to buffer.
  zc::HashMap<BufferId, const Buffer&> idToBuffer;

  mutable BufferLocCache locCache;
};

// ================================================================================
// SourceManager

SourceManager::SourceManager() noexcept : impl(zc::heap<Impl>()) {}
SourceManager::~SourceManager() noexcept(false) = default;

LineAndColumn SourceManager::getPresumedLineAndColumnForLoc(SourceLoc loc,
                                                            BufferId bufferId) const {
  ZC_IREQUIRE(loc.isValid(), "Invalid source location");

  // Handle line number offset of virtual files
  int lineOffset = 0;
  ZC_IF_SOME(vf, getVirtualFile(loc)) { lineOffset = vf.lineOffset; }

  // Get the actual buffer ID
  ZC_IF_SOME(actualBufferId, bufferId.isValid() ? bufferId : findBufferContainingLoc(loc)) {
    const Buffer& buffer = ZC_ASSERT_NONNULL(impl->idToBuffer.find(actualBufferId));

    // Calculate the row number and column number
    unsigned line = 1;
    unsigned column = 1;
    const zc::byte* ptr = buffer.getBufferStart();
    const zc::byte* locPtr = loc.getOpaqueValue();

    while (ptr < locPtr && ptr < buffer.getBufferEnd()) {
      if (*ptr == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
      ++ptr;
    }

    ZC_ASSERT(line + lineOffset > 0, "bogus line offset");

    // Apply virtual file offsets
    return {line + lineOffset, column};
  }

  ZC_UNREACHABLE;
}

BufferId SourceManager::addNewSourceBuffer(zc::Array<zc::byte> inputData,
                                           const zc::StringPtr bufIdentifier) {
  const BufferId bufferId(impl->buffers.size() + 1);
  zc::Own<Buffer> buffer =
      zc::heap<Buffer>(bufferId, zc::heapString(bufIdentifier), zc::mv(inputData));
  impl->buffers.add(zc::mv(buffer));
  impl->idToBuffer.insert(bufferId, *impl->buffers.back());
  return bufferId;
}

BufferId SourceManager::addMemBufferCopy(const zc::ArrayPtr<const zc::byte> inputData,
                                         const zc::StringPtr bufIdentifier) {
  const BufferId bufferId(impl->buffers.size() + 1);
  zc::Own<Buffer> buffer =
      zc::heap<Buffer>(bufferId, zc::heapString(bufIdentifier), zc::heapArray(inputData));
  impl->buffers.add(zc::mv(buffer));
  impl->idToBuffer.insert(bufferId, *impl->buffers.back());
  return bufferId;
}

void SourceManager::createVirtualFile(const SourceLoc& loc, zc::StringPtr name, int lineOffset,
                                      unsigned length) {
  VirtualFile vf;
  vf.range = CharSourceRange{loc, length};
  vf.name = name;
  vf.lineOffset = lineOffset;
  impl->virtualFiles.add(zc::mv(vf));
}

const zc::Maybe<const VirtualFile&> SourceManager::getVirtualFile(const SourceLoc& loc) const {
  if (loc.isInvalid()) { return zc::none; }

  for (const VirtualFile& vf : impl->virtualFiles) {
    if (vf.range.contains(loc)) return vf;
  }

  return zc::none;
}

SourceLoc SourceManager::getLocForBufferStart(BufferId bufferId) const {
  return getRangeForBuffer(bufferId).getStart();
}

unsigned SourceManager::getLocOffsetInBuffer(SourceLoc loc, BufferId bufferId) const {
  ZC_ASSERT(loc.isValid(), "invalid loc");
  return 0;
}

SourceLoc SourceManager::getLocForOffset(BufferId bufferId, unsigned offset) const {
  return getLocForBufferStart(bufferId).getAdvancedLoc(offset);
}

/// Returns a buffer identifier for the given location.
zc::StringPtr SourceManager::getDisplayNameForLoc(const SourceLoc& loc) const {
  // Respect #line first
  ZC_IF_SOME(vf, getVirtualFile(loc)) { return vf.name; }

  const BufferId bufferId = ZC_ASSERT_NONNULL(findBufferContainingLoc(loc));
  // If we have a buffer ID, return the buffer identifier.
  return getIdentifierForBuffer(bufferId);
}

zc::ArrayPtr<const zc::byte> SourceManager::getEntireTextForBuffer(BufferId bufferId) const {
  return ZC_ASSERT_NONNULL(impl->idToBuffer.find(bufferId)).data;
}

zc::Maybe<BufferId> SourceManager::findBufferContainingLoc(const SourceLoc& loc) const {
  if (loc.isInvalid()) return zc::none;

  const zc::byte* ptr = loc.getOpaqueValue();
  const uint64_t numBuffers = impl->buffers.size();

  // If the cache is out-of-date, update it now.
  if (numBuffers != impl->locCache.numBuffersOriginal) {
    impl->locCache.sortedBuffers.clear();
    for (const zc::Own<Buffer>& buf : impl->buffers) { impl->locCache.sortedBuffers.add(buf->id); }
    impl->locCache.numBuffersOriginal = numBuffers;

    // Sort the buffer IDs by source range.
    std::sort(impl->locCache.sortedBuffers.begin(), impl->locCache.sortedBuffers.end(),
              BufferIDRangeComparator{*this});

    // Remove lower-numbered impl->buffers with the same source ranges as higher-
    // numbered impl->buffers. We want later alias impl->buffers to be found first.
    BufferId* newEnd = std::unique(impl->locCache.sortedBuffers.begin(),
                                   impl->locCache.sortedBuffers.end(), BufferIDSameRange{*this});
    // Calculate the number of unique elements (i.e., the new size)
    auto newSize = std::distance(impl->locCache.sortedBuffers.begin(), newEnd);
    // Truncate the vector to the new size
    impl->locCache.sortedBuffers.truncate(newSize);
    // Forget the last buffer we looked at; it might have been replaced.
    impl->locCache.lastBufferId = zc::none;
  }

  // Check the last buffer we looked in.
  ZC_IF_SOME(lastId, impl->locCache.lastBufferId) {
    const Buffer& lastBuf = ZC_ASSERT_NONNULL(impl->idToBuffer.find(lastId));
    if (ptr >= lastBuf.data.begin() && ptr < lastBuf.data.end()) { return lastId; }
  }

  // Search the sorted list of buffer IDs.
  auto it =
      std::upper_bound(impl->locCache.sortedBuffers.begin(), impl->locCache.sortedBuffers.end(),
                       loc, BufferIDRangeComparator{*this});

  if (it != impl->locCache.sortedBuffers.begin()) {
    const BufferId candidateId = *(it - 1);
    ZC_IF_SOME(candidate, impl->idToBuffer.find(candidateId)) {
      if (ptr >= candidate.data.begin() && ptr < candidate.data.end()) {
        impl->locCache.lastBufferId = candidateId;
        return candidateId;
      }
    }
  }

  return zc::none;
}

zc::Maybe<unsigned> SourceManager::resolveFromLineCol(BufferId bufferId, unsigned line,
                                                      unsigned col) const {
  const zc::ArrayPtr<const zc::byte> buffer = getEntireTextForBuffer(bufferId);

  unsigned currentLine = 1;
  unsigned currentCol = 1;
  for (size_t offset = 0; offset < buffer.size(); ++offset) {
    if (currentLine == line && currentCol == col) { return offset; }

    if (const char ch = static_cast<char>(buffer[offset]); ch == '\n') {
      ++currentLine;
      currentCol = 1;
    } else {
      ++currentCol;
    }
  }

  return zc::none;
}

zc::StringPtr SourceManager::getIdentifierForBuffer(BufferId bufferId) const {
  return ZC_ASSERT_NONNULL(impl->idToBuffer.find(bufferId)).identifier;
}

CharSourceRange SourceManager::getRangeForBuffer(BufferId bufferId) const {
  const Buffer& buffer = ZC_ASSERT_NONNULL(impl->idToBuffer.find(bufferId));
  const SourceLoc start{buffer.getBufferStart()};
  return CharSourceRange(start, buffer.getBufferSize());
}

zc::Maybe<BufferId> SourceManager::getFileSystemSourceBufferID(const zc::StringPtr path) {
  const zc::PathPtr cwd = impl->fs->getCurrentPath();
  zc::Path nativePath = cwd.evalNative(path);
  ZC_REQUIRE(path.size() > 0);

  const zc::ReadableDirectory& dir =
      nativePath.startsWith(cwd) ? impl->fs->getCurrent() : impl->fs->getRoot();
  const zc::Path sourcePath = nativePath.startsWith(cwd)
                                  ? nativePath.slice(cwd.size(), nativePath.size()).clone()
                                  : zc::mv(nativePath);

  // Check if the path is already in the cache
  ZC_IF_SOME(bufferId, impl->pathToBufferId.find(path)) { return bufferId; }

  ZC_IF_SOME(file, dir.tryOpenFile(sourcePath)) {
    zc::Array<zc::byte> data = file->readAllBytes();
    const BufferId bufferId = addNewSourceBuffer(zc::mv(data), sourcePath.toString());
    impl->pathToBufferId.insert(sourcePath.toString(), bufferId);
    return bufferId;
  }

  // If the file is not found, return none
  return zc::none;
}

SourceLoc SourceManager::getLocFromExternalSource(const zc::StringPtr path, const unsigned line,
                                                  const unsigned col) {
  ZC_IF_SOME(bufferId, getFileSystemSourceBufferID(path)) {
    ZC_IF_SOME(offset, resolveFromLineCol(bufferId, line, col)) {
      return getLocForOffset(bufferId, offset);
    }
  }
  return {};
}

const zc::Vector<BufferId> SourceManager::getManagedBufferIds() const {
  zc::Vector<BufferId> ids;
  ids.reserve(impl->buffers.size());
  for (const auto& buffer : impl->buffers) { ids.add(buffer->id); }
  return ids;
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
