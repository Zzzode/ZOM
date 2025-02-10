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

#include <unordered_map>

#include "zc/core/debug.h"
#include "zomlang/compiler/source/module.h"

namespace zomlang {
namespace compiler {
namespace source {

// ========== SourceManager::Impl

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
    zc::Array<FixIt> fixIts;
  };

  explicit Impl(const zc::Filesystem& disk, zc::Own<const zc::ReadableFile> file,
                const zc::ReadableDirectory& sourceDir, zc::Path path) noexcept;
  ~Impl() noexcept(false);

  // Buffer management
  uint64_t addNewSourceBuffer(zc::Own<zc::InputStream> input, zc::Own<Module> module);
  uint64_t addMemBufferCopy(const zc::ArrayPtr<const zc::byte> inputData,
                            const zc::StringPtr& bufIdentifier, Module* module);

  // Virtual file management
  void createVirtualFile(const SourceLoc& loc, const zc::StringPtr name, int lineOffset,
                         unsigned length);
  const VirtualFile* getVirtualFile(const SourceLoc& loc) const;

  // Generated source info
  void setGeneratedSourceInfo(uint64_t bufferId, const GeneratedSourceInfo& info);
  const GeneratedSourceInfo* getGeneratedSourceInfo(uint64_t bufferId) const;

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
  uint64_t findBufferContainingLoc(const SourceLoc& loc) const;
  zc::StringPtr getFilename(uint64_t bufferId) const;

  // Line and column operations
  zc::Maybe<unsigned> resolveFromLineCol(uint64_t bufferId, unsigned line, unsigned col) const;
  zc::Maybe<unsigned> resolveOffsetForEndOfLine(uint64_t bufferId, unsigned line) const;
  zc::Maybe<unsigned> getLineLength(uint64_t bufferId, unsigned line) const;
  SourceLoc getLocForLineCol(uint64_t bufferId, unsigned line, unsigned col) const;

  // External source support
  uint64_t getExternalSourceBufferID(const zc::StringPtr& path);
  SourceLoc getLocFromExternalSource(const zc::StringPtr& path, unsigned line, unsigned col);

  // Diagnostics
  void getMessage(const SourceLoc& loc, DiagnosticKind kind, const zc::String& msg,
                  zc::ArrayPtr<SourceRange> ranges, zc::ArrayPtr<FixIt> fixIts,
                  zc::OutputStream& os) const;

  // Verification
  void verifyAllBuffers() const;

  // Regex literal support
  void recordRegexLiteralStartLoc(const SourceLoc& loc);
  bool isRegexLiteralStart(const SourceLoc& loc) const;

  // Module management
  void setModuleForBuffer(uint64_t bufferId, zc::Own<Module> module);
  zc::Maybe<const Module&> getModuleForBuffer(uint64_t bufferId) const;

private:
  /// The filesystem to use for reading files.
  const zc::Filesystem& disk;

  /// The source file being compiled.
  zc::Own<const zc::ReadableFile> file;
  /// The source directory being compiled.
  ZC_UNUSED const zc::ReadableDirectory& sourceDir;
  /// Path to the source file being compiled.
  const zc::Path path;

  zc::Vector<VirtualFile> virtualFiles;
  zc::Vector<SourceLoc> regexLiteralStartLocs;

  mutable struct BufferLocCache_ {
    zc::Vector<uint64_t> sortedBuffers;
    uint64_t numBuffersOriginal = 0;
    zc::Maybe<uint64_t> lastBufferId;
  } locCache;

  void updateLocCache() const;
};

// SourceManager::Impl

SourceManager::Impl::Impl(const zc::Filesystem& disk, zc::Own<const zc::ReadableFile> file,
                          const zc::ReadableDirectory& sourceDir, zc::Path path) noexcept
    : disk(disk), file(zc::mv(file)), sourceDir(sourceDir), path(zc::mv(path)) {}

SourceManager::Impl::~Impl() noexcept(false) = default;

void SourceManager::Impl::createVirtualFile(const SourceLoc& loc, zc::StringPtr name,
                                            int lineOffset, unsigned length) {
  VirtualFile vf;
  vf.range = CharSourceRange{loc, length};
  vf.name = name;
  vf.lineOffset = lineOffset;
  virtualFiles.add(zc::mv(vf));
}

void SourceManager::Impl::getMessage(const SourceLoc& loc, DiagnosticKind kind,
                                     const zc::String& msg, zc::ArrayPtr<SourceRange> ranges,
                                     zc::ArrayPtr<FixIt> fixIts, zc::OutputStream& os) const {}

// ================================================================================
// SourceManager

SourceManager::SourceManager(const zc::Filesystem& disk, zc::Own<const zc::ReadableFile> file,
                             const zc::ReadableDirectory& sourceDir, zc::Path path) noexcept
    : impl(zc::heap<Impl>(disk, zc::mv(file), sourceDir, zc::mv(path))) {}

SourceManager::~SourceManager() noexcept(false) = default;

void SourceManager::createVirtualFile(const SourceLoc& loc, const zc::StringPtr name,
                                      const int lineOffset, const unsigned length) {
  impl->createVirtualFile(loc, name, lineOffset, length);
}

void SourceManager::getMessage(const SourceLoc& loc, DiagnosticKind kind, const zc::String& msg,
                               zc::ArrayPtr<SourceRange> ranges, zc::ArrayPtr<FixIt> fixIts,
                               zc::OutputStream& os) const {
  impl->getMessage(loc, kind, msg, ranges, fixIts, os);
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
