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

#pragma once

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace source {

class SourceLoc;
class SourceRange;
class CharSourceRange;

struct LineAndColumn {
  uint32_t line;
  uint32_t column;
  LineAndColumn(const uint32_t l, const uint32_t c) : line(l), column(c) {}
};

class BufferId {
public:
  explicit BufferId(uint64_t val) noexcept;
  ~BufferId() noexcept(false);

  BufferId(const BufferId& other);
  BufferId& operator=(const BufferId& other);

  BufferId(BufferId&& other) noexcept;
  BufferId& operator=(BufferId&& other) noexcept;

  operator uint64_t() const;

  bool operator==(const BufferId& other) const;
  bool operator!=(const BufferId& other) const;
  bool operator<(const BufferId& other) const;
  bool operator>(const BufferId& other) const;
  bool operator<=(const BufferId& other) const;
  bool operator>=(const BufferId& other) const;

  bool isValid() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

struct VirtualFile {
  CharSourceRange range;
  zc::StringPtr name;
  int lineOffset;
};

class SourceManager {
public:
  SourceManager() noexcept;
  ~SourceManager() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(SourceManager);

  /// Buffer management
  BufferId addNewSourceBuffer(zc::Array<zc::byte> inputData, zc::StringPtr bufIdentifier);
  BufferId addMemBufferCopy(zc::ArrayPtr<const zc::byte> inputData, zc::StringPtr bufIdentifier);

  /// Virtual file management
  void createVirtualFile(const SourceLoc& loc, zc::StringPtr name, int lineOffset, unsigned length);
  const zc::Maybe<const VirtualFile&> getVirtualFile(const SourceLoc& loc) const;

  /// Generated source info
  void setGeneratedSourceInfo(BufferId bufferId, const struct GeneratedSourceInfo& info);
  const GeneratedSourceInfo* getGeneratedSourceInfo(BufferId bufferId) const;

  /// Returns the SourceLoc for the beginning of the specified buffer
  /// (at offset zero).
  ///
  /// Note that the resulting location might not point at the first token: it
  /// might point at whitespace or a comment.
  SourceLoc getLocForBufferStart(BufferId bufferId) const;

  /// Returns the offset in bytes for the given valid source location.
  unsigned getLocOffsetInBuffer(SourceLoc Loc, BufferId bufferId) const;

  /// Location and range operations
  SourceLoc getLocForOffset(BufferId bufferId, unsigned offset) const;
  LineAndColumn getLineAndColumn(const SourceLoc& loc) const;
  LineAndColumn getPresumedLineAndColumnForLoc(SourceLoc Loc, BufferId bufferId) const;
  unsigned getLineNumber(const SourceLoc& loc) const;
  bool isBefore(const SourceLoc& first, const SourceLoc& second) const;
  bool isAtOrBefore(const SourceLoc& first, const SourceLoc& second) const;
  bool containsTokenLoc(const SourceRange& range, const SourceLoc& loc) const;
  bool encloses(const SourceRange& enclosing, const SourceRange& inner) const;

  /// Returns a buffer identifier for the given location.
  zc::StringPtr getDisplayNameForLoc(const SourceLoc& loc) const;

  /// Content retrieval
  zc::ArrayPtr<const zc::byte> getEntireTextForBuffer(BufferId bufferId) const;
  zc::ArrayPtr<const zc::byte> extractText(const SourceRange& range,
                                           zc::Maybe<BufferId> bufferId) const;

  /// Buffer identification
  zc::Maybe<BufferId> findBufferContainingLoc(const SourceLoc& loc) const;
  zc::StringPtr getFilename(BufferId bufferId) const;

  /// Line and column operations
  zc::Maybe<unsigned> resolveFromLineCol(BufferId bufferId, unsigned line, unsigned col) const;
  zc::Maybe<unsigned> resolveOffsetForEndOfLine(BufferId bufferId, unsigned line) const;
  zc::Maybe<unsigned> getLineLength(BufferId bufferId, unsigned line) const;
  SourceLoc getLocForLineCol(BufferId bufferId, unsigned line, unsigned col) const;

  /// External source support
  zc::Maybe<BufferId> getFileSystemSourceBufferID(zc::StringPtr path);
  SourceLoc getLocFromExternalSource(zc::StringPtr path, unsigned line, unsigned col);

  zc::StringPtr getIdentifierForBuffer(BufferId bufferId) const;

  CharSourceRange getRangeForBuffer(BufferId bufferId) const;

  /// Verification
  void verifyAllBuffers() const;

  /// Regex literal support
  void recordRegexLiteralStartLoc(const SourceLoc& loc);
  bool isRegexLiteralStart(const SourceLoc& loc) const;

  const zc::Vector<BufferId> getManagedBufferIds() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
