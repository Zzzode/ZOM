#include "src/zom/source/manager.h"

#include <algorithm>

namespace zom {
namespace source {

SourceManager::SourceManager() = default;

SourceManager::~SourceManager() noexcept(false) = default;

unsigned SourceManager::addNewSourceBuffer(zc::Own<zc::InputStream> input) {
  BufferInfo info;
  info.input = zc::mv(input);
  // TODO: Read content from input stream into info.content
  buffers.add(zc::mv(info));
  return buffers.size() - 1;
}

unsigned SourceManager::addNewSourceBuffer(zc::StringPtr filename) {
  // TODO: Open file, create InputStream, and call
  // AddNewSourceBuffer(InputStream)
  return 0;  // Placeholder
}

unsigned SourceManager::addMemBufferCopy(zc::ArrayPtr<const zc::byte> inputData,
                                         zc::StringPtr bufIdentifier) {
  BufferInfo info;
  info.identifier = zc::heapString(bufIdentifier);
  info.content = zc::heapArray<zc::byte>(inputData);
  buffers.add(zc::mv(info));
  return buffers.size() - 1;
}

void SourceManager::createVirtualFile(SourceLoc loc, zc::StringPtr name, int lineOffset,
                                      unsigned length) {
  VirtualFile vf;
  vf.range = CharSourceRange{loc, length};
  vf.name = name;
  vf.lineOffset = lineOffset;
  virtualFiles.add(zc::mv(vf));
}

const SourceManager::VirtualFile* SourceManager::getVirtualFile(SourceLoc loc) const {
  for (const auto& vf : virtualFiles) {
    if (vf.range.contains(loc)) { return &vf; }
  }
  return nullptr;
}

SourceLoc SourceManager::getLocForOffset(unsigned bufferId, unsigned offset) const {
  if (bufferId >= buffers.size()) return SourceLoc();
  return SourceLoc::getFromOpaqueValue((bufferId << 24) | offset);
}

SourceManager::LineAndColumn SourceManager::getLineAndColumn(SourceLoc loc) const {
  unsigned bufferId = findBufferContainingLoc(loc);
  if (bufferId == -1U) return {0, 0};

  ZC_UNUSED const auto& buffer = buffers[bufferId];
  ZC_UNUSED unsigned offset = loc.getOpaqueValue() & 0xFFFFFF;

  // TODO: Implement line and column calculation based on buffer content
  return {1, 1};  // Placeholder
}

bool SourceManager::isBefore(SourceLoc first, SourceLoc second) const {
  unsigned bufferId1 = findBufferContainingLoc(first);
  unsigned bufferId2 = findBufferContainingLoc(second);

  if (bufferId1 != bufferId2) return bufferId1 < bufferId2;

  return (first.getOpaqueValue() & 0xFFFFFF) < (second.getOpaqueValue() & 0xFFFFFF);
}

zc::ArrayPtr<const zc::byte> SourceManager::getEntireTextForBuffer(unsigned bufferId) const {
  if (bufferId >= buffers.size()) return nullptr;
  return buffers[bufferId].content;
}

unsigned SourceManager::findBufferContainingLoc(SourceLoc loc) const {
  if (loc.isInvalid()) return -1U;

  updateLocCache();

  auto it = std::lower_bound(locCache.sortedBuffers.begin(), locCache.sortedBuffers.end(),
                             loc.getOpaqueValue() >> 24);
  if (it == locCache.sortedBuffers.begin()) return -1U;
  return *(it - 1);
}

void SourceManager::updateLocCache() const {
  if (locCache.numBuffersOriginal == buffers.size()) return;

  locCache.sortedBuffers.clear();
  for (unsigned i = 0; i < buffers.size(); ++i) { locCache.sortedBuffers.add(i); }
  std::sort(locCache.sortedBuffers.begin(), locCache.sortedBuffers.end());
  locCache.numBuffersOriginal = buffers.size();
}

CharSourceRange SourceManager::getCharSourceRange(SourceRange range) const {
  return CharSourceRange(range.getStart(), range.getEnd());
}

char SourceManager::extractCharAfter(SourceLoc loc) const {
  unsigned bufferId = findBufferContainingLoc(loc);
  if (bufferId == -1U) return '\0';

  const auto& buffer = buffers[bufferId];
  unsigned offset = loc.getOpaqueValue() & 0xFFFFFF;

  if (offset >= buffer.content.size()) return '\0';
  return buffer.content[offset];
}

SourceLoc SourceManager::getLocForEndOfToken(SourceLoc loc) const {
  unsigned bufferId = findBufferContainingLoc(loc);
  if (bufferId == -1U) return loc;

  ZC_UNUSED const auto& buffer = buffers[bufferId];
  unsigned offset = loc.getOpaqueValue() & 0xFFFFFF;

  // TODO: Implement logic to find the end of the token
  // This might involve skipping whitespace, finding the next non-alphanumeric
  // character, etc.

  return getLocForOffset(bufferId, offset + 1);  // Placeholder implementation
}

// ... Implement other methods ...

}  // namespace source
}  // namespace zom
