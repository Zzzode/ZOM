#include "src/zom/source/manager.h"

#include <algorithm>

namespace zom {
namespace source {

SourceManager::SourceManager() = default;

SourceManager::~SourceManager() noexcept(false) = default;

unsigned SourceManager::AddNewSourceBuffer(zc::Own<zc::InputStream> input) {
  BufferInfo info;
  info.input = zc::mv(input);
  // TODO: Read content from input stream into info.content
  buffers_.add(zc::mv(info));
  return buffers_.size() - 1;
}

unsigned SourceManager::AddNewSourceBuffer(zc::StringPtr filename) {
  // TODO: Open file, create InputStream, and call
  // AddNewSourceBuffer(InputStream)
  return 0;  // Placeholder
}

unsigned SourceManager::AddMemBufferCopy(zc::ArrayPtr<const zc::byte> input_data,
                                         zc::StringPtr buf_identifier) {
  BufferInfo info;
  info.identifier = zc::heapString(buf_identifier);
  info.content = zc::heapArray<zc::byte>(input_data);
  buffers_.add(zc::mv(info));
  return buffers_.size() - 1;
}

void SourceManager::CreateVirtualFile(SourceLoc loc, zc::StringPtr name, int line_offset,
                                      unsigned length) {
  VirtualFile vf;
  vf.range = CharSourceRange{loc, length};
  vf.name = name;
  vf.line_offset = line_offset;
  virtual_files_.add(zc::mv(vf));
}

const SourceManager::VirtualFile* SourceManager::GetVirtualFile(SourceLoc loc) const {
  for (const auto& vf : virtual_files_) {
    if (vf.range.Contains(loc)) { return &vf; }
  }
  return nullptr;
}

SourceLoc SourceManager::GetLocForOffset(unsigned buffer_id, unsigned offset) const {
  if (buffer_id >= buffers_.size()) return SourceLoc();
  return SourceLoc::GetFromOpaqueValue((buffer_id << 24) | offset);
}

SourceManager::LineAndColumn SourceManager::GetLineAndColumn(SourceLoc loc) const {
  unsigned buffer_id = FindBufferContainingLoc(loc);
  if (buffer_id == -1U) return {0, 0};

  ZC_UNUSED const auto& buffer = buffers_[buffer_id];
  ZC_UNUSED unsigned offset = loc.GetOpaqueValue() & 0xFFFFFF;

  // TODO: Implement line and column calculation based on buffer content
  return {1, 1};  // Placeholder
}

bool SourceManager::IsBefore(SourceLoc first, SourceLoc second) const {
  unsigned buffer_id1 = FindBufferContainingLoc(first);
  unsigned buffer_id2 = FindBufferContainingLoc(second);

  if (buffer_id1 != buffer_id2) return buffer_id1 < buffer_id2;

  return (first.GetOpaqueValue() & 0xFFFFFF) < (second.GetOpaqueValue() & 0xFFFFFF);
}

zc::ArrayPtr<const zc::byte> SourceManager::GetEntireTextForBuffer(unsigned buffer_id) const {
  if (buffer_id >= buffers_.size()) return nullptr;
  return buffers_[buffer_id].content;
}

unsigned SourceManager::FindBufferContainingLoc(SourceLoc loc) const {
  if (loc.IsInvalid()) return -1U;

  UpdateLocCache();

  auto it = std::lower_bound(loc_cache_.sorted_buffers.begin(), loc_cache_.sorted_buffers.end(),
                             loc.GetOpaqueValue() >> 24);
  if (it == loc_cache_.sorted_buffers.begin()) return -1U;
  return *(it - 1);
}

void SourceManager::UpdateLocCache() const {
  if (loc_cache_.num_buffers_original == buffers_.size()) return;

  loc_cache_.sorted_buffers.clear();
  for (unsigned i = 0; i < buffers_.size(); ++i) { loc_cache_.sorted_buffers.add(i); }
  std::sort(loc_cache_.sorted_buffers.begin(), loc_cache_.sorted_buffers.end());
  loc_cache_.num_buffers_original = buffers_.size();
}

CharSourceRange SourceManager::GetCharSourceRange(SourceRange range) const {
  return CharSourceRange(range.start(), range.end());
}

char SourceManager::ExtractCharAfter(SourceLoc loc) const {
  unsigned buffer_id = FindBufferContainingLoc(loc);
  if (buffer_id == -1U) return '\0';

  const auto& buffer = buffers_[buffer_id];
  unsigned offset = loc.GetOpaqueValue() & 0xFFFFFF;

  if (offset >= buffer.content.size()) return '\0';
  return buffer.content[offset];
}

SourceLoc SourceManager::GetLocForEndOfToken(SourceLoc loc) const {
  unsigned buffer_id = FindBufferContainingLoc(loc);
  if (buffer_id == -1U) return loc;

  ZC_UNUSED const auto& buffer = buffers_[buffer_id];
  unsigned offset = loc.GetOpaqueValue() & 0xFFFFFF;

  // TODO: Implement logic to find the end of the token
  // This might involve skipping whitespace, finding the next non-alphanumeric
  // character, etc.

  return GetLocForOffset(buffer_id, offset + 1);  // Placeholder implementation
}

// ... Implement other methods ...

}  // namespace source
}  // namespace zom
