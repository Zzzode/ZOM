// Copyright (c) 2025 Zode.Z. All rights reserved
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
#include <functional>

#include "zc/core/debug.h"
#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "zc/core/string.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace source {

// ================================================================================
// SourceManager::Impl

class SourceManager::Impl {
public:
  struct VirtualFile {
    CharSourceRange range;
    zc::StringPtr name;
    int lineOffset;
  };

  struct GeneratedSourceInfo {
    zc::String originalSource;
    zc::String generatedSource;
    zc::Array<diagnostics::FixIt> fixIts;
  };

  struct Buffer {
    const uint64_t id;
    zc::String identifier;
    zc::Array<zc::byte> data;
    /// The original source location of this buffer.
    GeneratedSourceInfo generatedInfo;
    /// The virtual files associated with this buffer.
    zc::Vector<VirtualFile> virtualFiles;
    /// The offset in bytes of the first character in the buffer.
    mutable zc::Vector<unsigned> lineStartOffsets;

    Buffer(const uint64_t id, zc::String identifier, zc::Array<zc::byte> data)
        : id(id), identifier(zc::mv(identifier)), data(zc::mv(data)) {}

    const zc::byte* getBufferStart() const { return data.begin(); }
    const zc::byte* getBufferEnd() const { return data.end(); }

    ZC_NODISCARD size_t getBufferSize() const { return data.size(); }
  };

  Impl() noexcept;
  ~Impl() noexcept(false);

  // Buffer management
  uint64_t addNewSourceBuffer(zc::Array<zc::byte> inputData, zc::StringPtr bufIdentifier);
  uint64_t addMemBufferCopy(zc::ArrayPtr<const zc::byte> inputData, zc::StringPtr bufIdentifier);

  // Virtual file management
  void createVirtualFile(const SourceLoc& loc, zc::StringPtr name, int lineOffset, unsigned length);
  const VirtualFile* getVirtualFile(const SourceLoc& loc) const;

  // Generated source info
  void setGeneratedSourceInfo(uint64_t bufferId, const GeneratedSourceInfo& info);
  const GeneratedSourceInfo* getGeneratedSourceInfo(uint64_t bufferId) const;

  SourceLoc getLocForBufferStart(uint64_t bufferId) const;

  /// Returns the offset in bytes for the given valid source location.
  unsigned getLocOffsetInBuffer(SourceLoc loc, uint64_t bufferId) const;

  // Location and range operations
  SourceLoc getLocForOffset(uint64_t bufferId, unsigned offset) const;
  LineAndColumn getLineAndColumn(const SourceLoc& loc) const;
  unsigned getLineNumber(const SourceLoc& loc) const;
  bool isBefore(const SourceLoc& first, const SourceLoc& second) const;
  bool isAtOrBefore(const SourceLoc& first, const SourceLoc& second) const;
  bool containsTokenLoc(const SourceRange& range, const SourceLoc& loc) const;
  bool encloses(const SourceRange& enclosing, const SourceRange& inner) const;

  // Content retrieval
  zc::ArrayPtr<const zc::byte> getEntireTextForBuffer(uint64_t bufferId) const;
  zc::ArrayPtr<const zc::byte> extractText(const SourceRange& range) const;

  // Buffer identification
  zc::Maybe<uint64_t> findBufferContainingLoc(const SourceLoc& loc) const;
  zc::StringPtr getFilename(uint64_t bufferId) const;

  // Line and column operations
  zc::Maybe<unsigned> resolveFromLineCol(uint64_t bufferId, unsigned line, unsigned col) const;
  zc::Maybe<unsigned> resolveOffsetForEndOfLine(uint64_t bufferId, unsigned line) const;
  zc::Maybe<unsigned> getLineLength(uint64_t bufferId, unsigned line) const;
  SourceLoc getLocForLineCol(uint64_t bufferId, unsigned line, unsigned col) const;

  zc::StringPtr getIdentifierForBuffer(uint64_t bufferId);

  CharSourceRange getRangeForBuffer(uint64_t bufferId) const;

  // External source support
  zc::Maybe<uint64_t> getExternalSourceBufferID(zc::StringPtr path);
  SourceLoc getLocFromExternalSource(zc::StringPtr path, unsigned line, unsigned col);

  // Verification
  void verifyAllBuffers() const;

  // Regex literal support
  void recordRegexLiteralStartLoc(const SourceLoc loc);
  bool isRegexLiteralStart(const SourceLoc& loc) const;

private:
  /// The filesystem to use for reading files.
  zc::Own<const zc::Filesystem> fs;
  /// File a path to BufferID mapping cache
  zc::HashMap<zc::String, uint64_t> pathToBufferId;
  /// Whether to open in volatile mode (disallow memory mappings)
  bool openAsVolatile = false;

  zc::Vector<VirtualFile> virtualFiles;
  zc::Vector<SourceLoc> regexLiteralStartLocs;

  zc::Vector<zc::Own<Buffer>> buffers;
  /// Fast lookup from buffer ID to buffer.
  zc::HashMap<uint64_t, const Buffer&> idToBuffer;

  mutable struct BufferLocCache {
    zc::Vector<uint64_t> sortedBuffers;
    uint64_t numBuffersOriginal = 0;
    zc::Maybe<uint64_t> lastBufferId;
  } locCache;

  /// Compare the source location ranges for two buffers, as an ordering to
  /// use for fast searches.
  struct BufferIDRangeComparator {
    const SourceManager::Impl& sourceManager;

    bool operator()(uint64_t lhsID, uint64_t rhsID) const {
      auto lhsRange = sourceManager.getRangeForBuffer(lhsID);
      auto rhsRange = sourceManager.getRangeForBuffer(rhsID);

      // If the source buffers are identical, we want the higher-numbered
      // source buffers to occur first. This is important when uniquing.
      if (lhsRange == rhsRange) return lhsID > rhsID;

      std::less<const zc::byte*> pointerCompare;
      return pointerCompare(lhsRange.getStart().getOpaqueValue(),
                            rhsRange.getStart().getOpaqueValue());
    }

    bool operator()(unsigned lhsID, SourceLoc rhsLoc) const {
      auto lhsRange = sourceManager.getRangeForBuffer(lhsID);

      std::less<const zc::byte*> pointerCompare;
      return pointerCompare(lhsRange.getEnd().getOpaqueValue(), rhsLoc.getOpaqueValue());
    }

    bool operator()(SourceLoc lhsLoc, unsigned rhsID) const {
      auto rhsRange = sourceManager.getRangeForBuffer(rhsID);

      std::less<const zc::byte*> pointerCompare;
      return pointerCompare(lhsLoc.getOpaqueValue(), rhsRange.getEnd().getOpaqueValue());
    }
  };

  /// Determine whether the source ranges for two buffers are equivalent.
  struct BufferIDSameRange {
    const SourceManager::Impl& sourceMgr;

    bool operator()(uint64_t lhsID, uint64_t rhsID) const {
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

uint64_t SourceManager::Impl::addNewSourceBuffer(zc::Array<zc::byte> inputData,
                                                 const zc::StringPtr bufIdentifier) {
  const uint64_t bufferId = buffers.size() + 1;
  auto buffer = zc::heap<Buffer>(bufferId, zc::heapString(bufIdentifier), zc::mv(inputData));
  buffers.add(zc::mv(buffer));
  idToBuffer.insert(bufferId, *buffers.back());
  return bufferId;
}

uint64_t SourceManager::Impl::addMemBufferCopy(const zc::ArrayPtr<const zc::byte> inputData,
                                               const zc::StringPtr bufIdentifier) {
  const uint64_t bufferId = buffers.size() + 1;
  auto buffer = zc::heap<Buffer>(bufferId, zc::heapString(bufIdentifier), zc::heapArray(inputData));
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

SourceLoc SourceManager::Impl::getLocForBufferStart(uint64_t bufferId) const {
  return getRangeForBuffer(bufferId).getStart();
}

unsigned SourceManager::Impl::getLocOffsetInBuffer(SourceLoc loc, uint64_t bufferId) const {
  ZC_ASSERT(loc.isValid(), "invalid loc");
  return 0;
}

SourceLoc SourceManager::Impl::getLocForOffset(uint64_t bufferId, unsigned offset) const {
  return getLocForBufferStart(bufferId).getAdvancedLoc(offset);
}

zc::ArrayPtr<const zc::byte> SourceManager::Impl::getEntireTextForBuffer(uint64_t bufferId) const {
  return ZC_ASSERT_NONNULL(idToBuffer.find(bufferId)).data;
}

zc::Maybe<uint64_t> SourceManager::Impl::findBufferContainingLoc(const SourceLoc& loc) const {
  if (loc.isInvalid()) return zc::none;

  const zc::byte* ptr = loc.getOpaqueValue();
  const uint64_t numBuffers = buffers.size();

  // If the cache is out-of-date, update it now.
  if (numBuffers != locCache.numBuffersOriginal) {
    locCache.sortedBuffers.clear();
    for (const auto& buf : buffers) { locCache.sortedBuffers.add(buf->id); }
    locCache.numBuffersOriginal = numBuffers;

    // Sort the buffer IDs by source range.
    std::sort(locCache.sortedBuffers.begin(), locCache.sortedBuffers.end(),
              BufferIDRangeComparator{*this});

    // Remove lower-numbered buffers with the same source ranges as higher-
    // numbered buffers. We want later alias buffers to be found first.
    auto newEnd = std::unique(locCache.sortedBuffers.begin(), locCache.sortedBuffers.end(),
                              BufferIDSameRange{*this});
    locCache.sortedBuffers.resize(newEnd - locCache.sortedBuffers.begin());
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
    const uint64_t candidateId = *(it - 1);
    ZC_IF_SOME(candidate, idToBuffer.find(candidateId)) {
      if (ptr >= candidate.data.begin() && ptr < candidate.data.end()) {
        locCache.lastBufferId = candidateId;
        return candidateId;
      }
    }
  }

  return zc::none;
}

zc::Maybe<unsigned> SourceManager::Impl::resolveFromLineCol(uint64_t bufferId, unsigned line,
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

zc::StringPtr SourceManager::Impl::getIdentifierForBuffer(uint64_t bufferId) {
  return ZC_ASSERT_NONNULL(idToBuffer.find(bufferId)).identifier;
}

CharSourceRange SourceManager::Impl::getRangeForBuffer(uint64_t bufferId) const {
  const Buffer& buffer = ZC_ASSERT_NONNULL(idToBuffer.find(bufferId));
  const SourceLoc start{buffer.getBufferStart()};
  return CharSourceRange(start, buffer.getBufferSize());
}

zc::Maybe<uint64_t> SourceManager::Impl::getExternalSourceBufferID(const zc::StringPtr path) {
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
    const uint64_t bufferId = addNewSourceBuffer(zc::mv(data), sourcePath.toString());
    pathToBufferId.insert(sourcePath.toString(), bufferId);

    return bufferId;
  }

  // If the file is not found, return an invalid buffer ID
  constexpr uint64_t InvalidBufferId = 0;
  return InvalidBufferId;
}

SourceLoc SourceManager::Impl::getLocFromExternalSource(const zc::StringPtr path,
                                                        const unsigned line, const unsigned col) {
  ZC_IF_SOME(bufferId, getExternalSourceBufferID(path)) {
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

zc::Maybe<uint64_t> SourceManager::getExternalSourceBufferID(const zc::StringPtr path) {
  return impl->getExternalSourceBufferID(path);
}

SourceLoc SourceManager::getLocFromExternalSource(zc::StringPtr path, unsigned line, unsigned col) {
  return impl->getLocFromExternalSource(path, line, col);
}

zc::StringPtr SourceManager::getIdentifierForBuffer(uint64_t bufferId) {
  return impl->getIdentifierForBuffer(bufferId);
}

CharSourceRange SourceManager::getRangeForBuffer(uint64_t bufferId) const {
  return impl->getRangeForBuffer(bufferId);
}

uint64_t SourceManager::addNewSourceBuffer(zc::Array<zc::byte> inputData,
                                           const zc::StringPtr bufIdentifier) {
  return impl->addNewSourceBuffer(zc::mv(inputData), bufIdentifier);
}

uint64_t SourceManager::addMemBufferCopy(const zc::ArrayPtr<const zc::byte> inputData,
                                         const zc::StringPtr bufIdentifier) {
  return impl->addMemBufferCopy(inputData, bufIdentifier);
}

zc::Maybe<uint64_t> SourceManager::findBufferContainingLoc(const SourceLoc& loc) const {
  return impl->findBufferContainingLoc(loc);
}

void SourceManager::createVirtualFile(const SourceLoc& loc, const zc::StringPtr name,
                                      const int lineOffset, const unsigned length) {
  impl->createVirtualFile(loc, name, lineOffset, length);
}

unsigned SourceManager::getLocOffsetInBuffer(SourceLoc Loc, uint64_t bufferId) const {
  return impl->getLocOffsetInBuffer(Loc, bufferId);
}

SourceLoc SourceManager::getLocForBufferStart(uint64_t bufferId) const {
  return impl->getLocForBufferStart(bufferId);
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
