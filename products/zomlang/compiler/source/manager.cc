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
#include <cstdio>
#include <functional>

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

struct VirtualFile {
  CharSourceRange range;
  zc::StringPtr name;
  int lineOffset;
};

class SourceManager::Impl {
public:
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

  Impl() noexcept;
  ~Impl() noexcept(false);

  /// Buffer management
  BufferId addNewSourceBuffer(zc::Array<zc::byte> inputData, zc::StringPtr bufIdentifier);
  BufferId addMemBufferCopy(zc::ArrayPtr<const zc::byte> inputData, zc::StringPtr bufIdentifier);

  /// Virtual file management
  void createVirtualFile(const SourceLoc& loc, zc::StringPtr name, int lineOffset, unsigned length);
  const zc::Maybe<const VirtualFile&> getVirtualFile(const SourceLoc& loc) const;

  /// Generated source info
  void setGeneratedSourceInfo(BufferId bufferId, const GeneratedSourceInfo& info);
  const GeneratedSourceInfo* getGeneratedSourceInfo(BufferId bufferId) const;

  SourceLoc getLocForBufferStart(BufferId bufferId) const;

  /// Returns the offset in bytes for the given valid source location.
  unsigned getLocOffsetInBuffer(SourceLoc loc, BufferId bufferId) const;

  /// Location and range operations
  SourceLoc getLocForOffset(BufferId bufferId, unsigned offset) const;
  LineAndColumn getLineAndColumn(const SourceLoc& loc) const;
  LineAndColumn getPresumedLineAndColumnForLoc(SourceLoc Loc,
                                               BufferId bufferId = BufferId(0)) const;
  unsigned getLineNumber(const SourceLoc& loc) const;
  bool isBefore(const SourceLoc& first, const SourceLoc& second) const;
  bool isAtOrBefore(const SourceLoc& first, const SourceLoc& second) const;
  bool containsTokenLoc(const SourceRange& range, const SourceLoc& loc) const;
  bool encloses(const SourceRange& enclosing, const SourceRange& inner) const;

  /// Returns a buffer identifier for the given location.
  zc::StringPtr getDisplayNameForLoc(const SourceLoc& loc) const;

  /// Content retrieval
  zc::ArrayPtr<const zc::byte> getEntireTextForBuffer(BufferId bufferId) const;
  zc::ArrayPtr<const zc::byte> extractText(const SourceRange& range) const;

  /// Buffer identification
  zc::Maybe<BufferId> findBufferContainingLoc(const SourceLoc& loc) const;
  zc::StringPtr getFilename(BufferId bufferId) const;

  /// Line and column operations
  zc::Maybe<unsigned> resolveFromLineCol(BufferId bufferId, unsigned line, unsigned col) const;
  zc::Maybe<unsigned> resolveOffsetForEndOfLine(BufferId bufferId, unsigned line) const;
  zc::Maybe<unsigned> getLineLength(BufferId bufferId, unsigned line) const;
  SourceLoc getLocForLineCol(BufferId bufferId, unsigned line, unsigned col) const;

  zc::StringPtr getIdentifierForBuffer(BufferId bufferId) const;

  CharSourceRange getRangeForBuffer(BufferId bufferId) const;

  /// External source support
  zc::Maybe<BufferId> getFileSystemSourceBufferID(zc::StringPtr path);
  SourceLoc getLocFromExternalSource(zc::StringPtr path, unsigned line, unsigned col);

  /// Verification
  void verifyAllBuffers() const;

  /// Regex literal support
  void recordRegexLiteralStartLoc(const SourceLoc loc);
  bool isRegexLiteralStart(const SourceLoc& loc) const;

private:
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

  mutable struct BufferLocCache {
    zc::Vector<BufferId> sortedBuffers;
    uint64_t numBuffersOriginal = 0;
    zc::Maybe<BufferId> lastBufferId;
  } locCache;

  /// Compare the source location ranges for two buffers, as an ordering to
  /// use for fast searches.
  struct BufferIDRangeComparator {
    const SourceManager::Impl& sourceManager;

    bool operator()(BufferId lhsID, BufferId rhsID) const {
      auto lhsRange = sourceManager.getRangeForBuffer(lhsID);
      auto rhsRange = sourceManager.getRangeForBuffer(rhsID);

      // If the source buffers are identical, we want the higher-numbered
      // source buffers to occur first. This is important when uniquing.
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

  /// Determine whether the source ranges for two buffers are equivalent.
  struct BufferIDSameRange {
    const SourceManager::Impl& sourceMgr;

    bool operator()(BufferId lhsID, BufferId rhsID) const {
      auto lhsRange = sourceMgr.getRangeForBuffer(lhsID);
      auto rhsRange = sourceMgr.getRangeForBuffer(rhsID);

      return lhsRange == rhsRange;
    }
  };
};

// ================================================================================
// SourceManager::Impl

SourceManager::Impl::Impl() noexcept : fs(zc::newDiskFilesystem()) {}
SourceManager::Impl::~Impl() noexcept(false) = default;

LineAndColumn SourceManager::Impl::getPresumedLineAndColumnForLoc(SourceLoc loc,
                                                                  BufferId bufferId) const {
  ZC_IREQUIRE(loc.isValid(), "Invalid source location");

  // Handle line number offset of virtual files
  int lineOffset = 0;
  ZC_IF_SOME(vf, getVirtualFile(loc)) { lineOffset = vf.lineOffset; }

  // Get the actual buffer ID
  ZC_IF_SOME(actualBufferId, bufferId.isValid() ? bufferId : findBufferContainingLoc(loc)) {
    const Buffer& buffer = ZC_ASSERT_NONNULL(idToBuffer.find(actualBufferId));

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

BufferId SourceManager::Impl::addNewSourceBuffer(zc::Array<zc::byte> inputData,
                                                 const zc::StringPtr bufIdentifier) {
  const BufferId bufferId(buffers.size() + 1);
  zc::Own<Buffer> buffer =
      zc::heap<Buffer>(bufferId, zc::heapString(bufIdentifier), zc::mv(inputData));
  buffers.add(zc::mv(buffer));
  idToBuffer.insert(bufferId, *buffers.back());
  return bufferId;
}

BufferId SourceManager::Impl::addMemBufferCopy(const zc::ArrayPtr<const zc::byte> inputData,
                                               const zc::StringPtr bufIdentifier) {
  const BufferId bufferId(buffers.size() + 1);
  zc::Own<Buffer> buffer =
      zc::heap<Buffer>(bufferId, zc::heapString(bufIdentifier), zc::heapArray(inputData));
  buffers.add(zc::mv(buffer));
  idToBuffer.insert(bufferId, *buffers.back());
  return buffer->id;
}

void SourceManager::Impl::createVirtualFile(const SourceLoc& loc, zc::StringPtr name,
                                            int lineOffset, unsigned length) {
  VirtualFile vf;
  vf.range = CharSourceRange{loc, length};
  vf.name = name;
  vf.lineOffset = lineOffset;
  virtualFiles.add(zc::mv(vf));
}

const zc::Maybe<const VirtualFile&> SourceManager::Impl::getVirtualFile(
    const SourceLoc& loc) const {
  if (loc.isInvalid()) { return zc::none; }

  for (const VirtualFile& vf : virtualFiles) {
    if (vf.range.contains(loc)) return vf;
  }

  return zc::none;
}

SourceLoc SourceManager::Impl::getLocForBufferStart(BufferId bufferId) const {
  return getRangeForBuffer(bufferId).getStart();
}

unsigned SourceManager::Impl::getLocOffsetInBuffer(SourceLoc loc, BufferId bufferId) const {
  ZC_ASSERT(loc.isValid(), "invalid loc");
  return 0;
}

SourceLoc SourceManager::Impl::getLocForOffset(BufferId bufferId, unsigned offset) const {
  return getLocForBufferStart(bufferId).getAdvancedLoc(offset);
}

/// Returns a buffer identifier for the given location.
zc::StringPtr SourceManager::Impl::getDisplayNameForLoc(const SourceLoc& loc) const {
  // Respect #line first
  ZC_IF_SOME(vf, getVirtualFile(loc)) { return vf.name; }

  const BufferId bufferId = ZC_ASSERT_NONNULL(findBufferContainingLoc(loc));
  // If we have a buffer ID, return the buffer identifier.
  return getIdentifierForBuffer(bufferId);
}

zc::ArrayPtr<const zc::byte> SourceManager::Impl::getEntireTextForBuffer(BufferId bufferId) const {
  return ZC_ASSERT_NONNULL(idToBuffer.find(bufferId)).data;
}

zc::Maybe<BufferId> SourceManager::Impl::findBufferContainingLoc(const SourceLoc& loc) const {
  if (loc.isInvalid()) return zc::none;

  const zc::byte* ptr = loc.getOpaqueValue();
  const uint64_t numBuffers = buffers.size();

  // If the cache is out-of-date, update it now.
  if (numBuffers != locCache.numBuffersOriginal) {
    locCache.sortedBuffers.clear();
    for (const zc::Own<Buffer>& buf : buffers) { locCache.sortedBuffers.add(buf->id); }
    locCache.numBuffersOriginal = numBuffers;

    // Sort the buffer IDs by source range.
    std::sort(locCache.sortedBuffers.begin(), locCache.sortedBuffers.end(),
              BufferIDRangeComparator{*this});

    // Remove lower-numbered buffers with the same source ranges as higher-
    // numbered buffers. We want later alias buffers to be found first.
    BufferId* newEnd = std::unique(locCache.sortedBuffers.begin(), locCache.sortedBuffers.end(),
                                   BufferIDSameRange{*this});
    locCache.sortedBuffers.truncate(locCache.sortedBuffers.end() - newEnd - 1);
    // Forget the last buffer we looked at; it might have been replaced.
    locCache.lastBufferId = zc::none;
  }

  // Check the last buffer we looked in.
  ZC_IF_SOME(lastId, locCache.lastBufferId) {
    const Buffer& lastBuf = ZC_ASSERT_NONNULL(idToBuffer.find(lastId));
    if (ptr >= lastBuf.data.begin() && ptr < lastBuf.data.end()) { return lastId; }
  }

  // Search the sorted list of buffer IDs.
  auto it = std::upper_bound(locCache.sortedBuffers.begin(), locCache.sortedBuffers.end(), loc,
                             BufferIDRangeComparator{*this});

  if (it != locCache.sortedBuffers.begin()) {
    const BufferId candidateId = *(it - 1);
    ZC_IF_SOME(candidate, idToBuffer.find(candidateId)) {
      if (ptr >= candidate.data.begin() && ptr < candidate.data.end()) {
        locCache.lastBufferId = candidateId;
        return candidateId;
      }
    }
  }

  return zc::none;
}

zc::Maybe<unsigned> SourceManager::Impl::resolveFromLineCol(BufferId bufferId, unsigned line,
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

zc::StringPtr SourceManager::Impl::getIdentifierForBuffer(BufferId bufferId) const {
  return ZC_ASSERT_NONNULL(idToBuffer.find(bufferId)).identifier;
}

CharSourceRange SourceManager::Impl::getRangeForBuffer(BufferId bufferId) const {
  const Buffer& buffer = ZC_ASSERT_NONNULL(idToBuffer.find(bufferId));
  const SourceLoc start{buffer.getBufferStart()};
  return CharSourceRange(start, buffer.getBufferSize());
}

zc::Maybe<BufferId> SourceManager::Impl::getFileSystemSourceBufferID(const zc::StringPtr path) {
  const zc::PathPtr cwd = fs->getCurrentPath();
  zc::Path nativePath = cwd.evalNative(path);
  ZC_REQUIRE(path.size() > 0);

  const zc::ReadableDirectory& dir = nativePath.startsWith(cwd) ? fs->getCurrent() : fs->getRoot();
  const zc::Path sourcePath = nativePath.startsWith(cwd)
                                  ? nativePath.slice(cwd.size(), nativePath.size()).clone()
                                  : zc::mv(nativePath);

  // Check if the path is already in the cache
  ZC_IF_SOME(bufferId, pathToBufferId.find(path)) { return bufferId; }

  ZC_IF_SOME(file, dir.tryOpenFile(sourcePath)) {
    zc::Array<zc::byte> data = file->readAllBytes();
    const BufferId bufferId = addNewSourceBuffer(zc::mv(data), sourcePath.toString());
    pathToBufferId.insert(sourcePath.toString(), bufferId);
    return bufferId;
  }

  // If the file is not found, return none
  return zc::none;
}

SourceLoc SourceManager::Impl::getLocFromExternalSource(const zc::StringPtr path,
                                                        const unsigned line, const unsigned col) {
  ZC_IF_SOME(bufferId, getFileSystemSourceBufferID(path)) {
    ZC_IF_SOME(offset, resolveFromLineCol(bufferId, line, col)) {
      return getLocForOffset(bufferId, offset);
    }
  }
  return {};
}

// ================================================================================
// SourceManager

SourceManager::SourceManager() noexcept : impl(zc::heap<Impl>()) {}
SourceManager::~SourceManager() noexcept(false) = default;

LineAndColumn SourceManager::getPresumedLineAndColumnForLoc(SourceLoc Loc,
                                                            BufferId bufferId) const {
  return impl->getPresumedLineAndColumnForLoc(Loc, bufferId);
}

zc::Maybe<BufferId> SourceManager::getFileSystemSourceBufferID(const zc::StringPtr path) {
  return impl->getFileSystemSourceBufferID(path);
}

SourceLoc SourceManager::getLocFromExternalSource(zc::StringPtr path, unsigned line, unsigned col) {
  return impl->getLocFromExternalSource(path, line, col);
}

zc::StringPtr SourceManager::getIdentifierForBuffer(BufferId bufferId) const {
  return impl->getIdentifierForBuffer(bufferId);
}

CharSourceRange SourceManager::getRangeForBuffer(BufferId bufferId) const {
  return impl->getRangeForBuffer(bufferId);
}

BufferId SourceManager::addNewSourceBuffer(zc::Array<zc::byte> inputData,
                                           const zc::StringPtr bufIdentifier) {
  return impl->addNewSourceBuffer(zc::mv(inputData), bufIdentifier);
}

BufferId SourceManager::addMemBufferCopy(const zc::ArrayPtr<const zc::byte> inputData,
                                         const zc::StringPtr bufIdentifier) {
  return impl->addMemBufferCopy(inputData, bufIdentifier);
}

zc::ArrayPtr<const zc::byte> SourceManager::getEntireTextForBuffer(BufferId bufferId) const {
  return impl->getEntireTextForBuffer(bufferId);
}

zc::Maybe<BufferId> SourceManager::findBufferContainingLoc(const SourceLoc& loc) const {
  return impl->findBufferContainingLoc(loc);
}

void SourceManager::createVirtualFile(const SourceLoc& loc, const zc::StringPtr name,
                                      const int lineOffset, const unsigned length) {
  impl->createVirtualFile(loc, name, lineOffset, length);
}

unsigned SourceManager::getLocOffsetInBuffer(SourceLoc Loc, BufferId bufferId) const {
  return impl->getLocOffsetInBuffer(Loc, bufferId);
}

SourceLoc SourceManager::getLocForBufferStart(BufferId bufferId) const {
  return impl->getLocForBufferStart(bufferId);
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
