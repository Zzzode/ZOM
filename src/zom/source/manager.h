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

#include "src/zc/base/common.h"
#include "src/zc/memory/memory.h"
#include "src/zc/strings/string.h"
#include "src/zom/diagnostics/diagnostic.h"
#include "src/zom/source/location.h"

namespace zom {
namespace source {

class SourceManager {
public:
  struct VirtualFile {
    CharSourceRange range;
    zc::StringPtr name;
    int line_offset{0};
  };

  struct GeneratedSourceInfo {
    // Define the structure for generated source info
  };

  struct FixIt {
    SourceRange range;
    zc::String replacement_text;
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
  unsigned AddNewSourceBuffer(zc::Own<zc::InputStream> input);
  unsigned AddNewSourceBuffer(zc::StringPtr filename);
  unsigned AddMemBufferCopy(zc::ArrayPtr<const zc::byte> input_data,
                            zc::StringPtr buf_identifier = "");

  // Virtual file management
  void CreateVirtualFile(SourceLoc loc, zc::StringPtr name, int line_offset, unsigned length);
  const VirtualFile* GetVirtualFile(SourceLoc loc) const;

  // Generated source info
  void SetGeneratedSourceInfo(unsigned buffer_id, GeneratedSourceInfo info);
  const GeneratedSourceInfo* GetGeneratedSourceInfo(unsigned buffer_id) const;

  // Location and range operations
  SourceLoc GetLocForOffset(unsigned buffer_id, unsigned offset) const;
  LineAndColumn GetLineAndColumn(SourceLoc loc) const;
  unsigned GetLineNumber(SourceLoc loc) const;
  bool IsBefore(SourceLoc first, SourceLoc second) const;
  bool IsAtOrBefore(SourceLoc first, SourceLoc second) const;
  bool ContainsTokenLoc(SourceRange range, SourceLoc loc) const;
  bool Encloses(SourceRange enclosing, SourceRange inner) const;

  // Content retrieval
  zc::ArrayPtr<const zc::byte> GetEntireTextForBuffer(unsigned buffer_id) const;
  zc::ArrayPtr<const zc::byte> ExtractText(SourceRange range) const;

  // Buffer identification
  unsigned FindBufferContainingLoc(SourceLoc loc) const;
  zc::StringPtr GetFilename(unsigned buffer_id) const;

  // Line and column operations
  zc::Maybe<unsigned> ResolveFromLineCol(unsigned buffer_id, unsigned line, unsigned col) const;
  zc::Maybe<unsigned> ResolveOffsetForEndOfLine(unsigned buffer_id, unsigned line) const;
  zc::Maybe<unsigned> GetLineLength(unsigned buffer_id, unsigned line) const;
  SourceLoc GetLocForLineCol(unsigned buffer_id, unsigned line, unsigned col) const;

  // External source support
  unsigned GetExternalSourceBufferID(zc::StringPtr path);
  SourceLoc GetLocFromExternalSource(zc::StringPtr path, unsigned line, unsigned col);

  // Diagnostics
  void GetMessage(SourceLoc loc, zom::diagnostics::DiagnosticKind kind, const zc::String& msg,
                  zc::ArrayPtr<SourceRange> ranges, zc::ArrayPtr<FixIt> fix_its,
                  zc::OutputStream& os) const;

  // Verification
  void VerifyAllBuffers() const;

  // Regex literal support
  void RecordRegexLiteralStartLoc(SourceLoc loc);
  bool IsRegexLiteralStart(SourceLoc loc) const;

  // New methods based on the provided information
  CharSourceRange GetCharSourceRange(SourceRange range) const;
  char ExtractCharAfter(SourceLoc loc) const;
  SourceLoc GetLocForEndOfToken(SourceLoc loc) const;

private:
  struct BufferInfo {
    zc::Own<zc::InputStream> input;
    zc::String identifier;
    zc::Vector<zc::byte> content;
    GeneratedSourceInfo gen_info;
  };

  zc::Vector<BufferInfo> buffers_;
  zc::Vector<VirtualFile> virtual_files_;
  zc::Vector<SourceLoc> regex_literal_start_locs_;

  struct BufferLocCache {
    zc::Vector<unsigned> sorted_buffers;
    unsigned num_buffers_original = 0;
    zc::Maybe<unsigned> last_buffer_id;
  };

  mutable BufferLocCache loc_cache_;

  void UpdateLocCache() const;
  zc::Maybe<unsigned> FindBufferContainingLocInternal(SourceLoc loc) const;
};

}  // namespace source
}  // namespace zom

#endif  // ZOM_SOURCE_SOURCE_MANAGER_H_