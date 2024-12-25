// Copyright (c) 2024 Zode.Z. All rights reserved
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

#ifndef ZOM_SOURCE_SOURCE_MANAGER_H_
#define ZOM_SOURCE_SOURCE_MANAGER_H_

#include "src/zc/core/common.h"
#include "src/zc/core/memory.h"
#include "src/zc/core/string.h"
#include "src/zom/diagnostics/diagnostic.h"
#include "src/zom/source/location.h"

namespace zom {
namespace source {

class SourceManager {
public:
  struct VirtualFile {
    CharSourceRange range;
    zc::StringPtr name;
    int lineOffset{0};
  };

  struct GeneratedSourceInfo {
    // Define the structure for generated source info
  };

  struct FixIt {
    SourceRange range;
    zc::String replacementText;
  };

  struct LineAndColumn {
    unsigned line;
    unsigned column;

    LineAndColumn(const unsigned l, const unsigned c) : line(l), column(c) {}
  };

  SourceManager();
  ~SourceManager() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(SourceManager);

  // Buffer management
  unsigned addNewSourceBuffer(zc::Own<zc::InputStream> input);
  unsigned addNewSourceBuffer(zc::StringPtr filename);
  unsigned addMemBufferCopy(zc::ArrayPtr<const zc::byte> inputData,
                            zc::StringPtr bufIdentifier = "");

  // Virtual file management
  void createVirtualFile(SourceLoc loc, zc::StringPtr name, int lineOffset, unsigned length);
  const VirtualFile* getVirtualFile(SourceLoc loc) const;

  // Generated source info
  void setGeneratedSourceInfo(unsigned bufferId, GeneratedSourceInfo info);
  const GeneratedSourceInfo* getGeneratedSourceInfo(unsigned bufferId) const;

  // Location and range operations
  SourceLoc getLocForOffset(unsigned bufferId, unsigned offset) const;
  LineAndColumn getLineAndColumn(SourceLoc loc) const;
  unsigned getLineNumber(SourceLoc loc) const;
  bool isBefore(SourceLoc first, SourceLoc second) const;
  bool isAtOrBefore(SourceLoc first, SourceLoc second) const;
  bool containsTokenLoc(SourceRange range, SourceLoc loc) const;
  bool encloses(SourceRange enclosing, SourceRange inner) const;

  // Content retrieval
  zc::ArrayPtr<const zc::byte> getEntireTextForBuffer(unsigned bufferId) const;
  zc::ArrayPtr<const zc::byte> extractText(SourceRange range) const;

  // Buffer identification
  unsigned findBufferContainingLoc(SourceLoc loc) const;
  zc::StringPtr getFilename(unsigned bufferId) const;

  // Line and column operations
  zc::Maybe<unsigned> resolveFromLineCol(unsigned bufferId, unsigned line, unsigned col) const;
  zc::Maybe<unsigned> resolveOffsetForEndOfLine(unsigned bufferId, unsigned line) const;
  zc::Maybe<unsigned> getLineLength(unsigned bufferId, unsigned line) const;
  SourceLoc getLocForLineCol(unsigned bufferId, unsigned line, unsigned col) const;

  // External source support
  unsigned getExternalSourceBufferID(zc::StringPtr path);
  SourceLoc getLocFromExternalSource(zc::StringPtr path, unsigned line, unsigned col);

  // Diagnostics
  void getMessage(SourceLoc loc, zom::diagnostics::DiagnosticKind kind, const zc::String& msg,
                  zc::ArrayPtr<SourceRange> ranges, zc::ArrayPtr<FixIt> fixIts,
                  zc::OutputStream& os) const;

  // Verification
  void verifyAllBuffers() const;

  // Regex literal support
  void recordRegexLiteralStartLoc(SourceLoc loc);
  bool isRegexLiteralStart(SourceLoc loc) const;

  // New methods based on the provided information
  CharSourceRange getCharSourceRange(SourceRange range) const;
  char extractCharAfter(SourceLoc loc) const;
  SourceLoc getLocForEndOfToken(SourceLoc loc) const;

private:
  struct BufferInfo {
    zc::Own<zc::InputStream> input;
    zc::String identifier;
    zc::Vector<zc::byte> content;
    GeneratedSourceInfo genInfo;
  };

  zc::Vector<BufferInfo> buffers;
  zc::Vector<VirtualFile> virtualFiles;
  zc::Vector<SourceLoc> regexLiteralStartLocs;

  struct BufferLocCache {
    zc::Vector<unsigned> sortedBuffers;
    unsigned numBuffersOriginal = 0;
    zc::Maybe<unsigned> lastBufferId;
  };

  mutable BufferLocCache locCache;

  void updateLocCache() const;
  zc::Maybe<unsigned> findBufferContainingLocInternal(SourceLoc loc) const;
};

}  // namespace source
}  // namespace zom

#endif  // ZOM_SOURCE_SOURCE_MANAGER_H_
