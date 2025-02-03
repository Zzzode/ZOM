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

namespace {

struct FileKey {
  const zc::ReadableDirectory& baseDir;
  zc::PathPtr path;
  zc::Maybe<const zc::ReadableFile&> file;
  uint64_t hashCode;
  uint64_t size;
  zc::Date lastModified;

  FileKey(const zc::ReadableDirectory& baseDir, const zc::PathPtr path)
      : baseDir(baseDir),
        path(path),
        file(zc::none),
        hashCode(0),
        size(0),
        lastModified(zc::UNIX_EPOCH) {}

  FileKey(const zc::ReadableDirectory& baseDir, const zc::PathPtr path,
          const zc::ReadableFile& file)
      : FileKey(baseDir, path, file, file.stat()) {}

  FileKey(const zc::ReadableDirectory& baseDir, const zc::PathPtr path,
          const zc::ReadableFile& file, const zc::FsNode::Metadata& meta)
      : baseDir(baseDir),
        path(path),
        file(file),
        hashCode(meta.hashCode),
        size(meta.size),
        lastModified(meta.lastModified) {}

  bool operator==(const FileKey& other) const {
    if (&baseDir == &other.baseDir && path == other.path) return true;

    if (hashCode != other.hashCode || size != other.size || lastModified != other.lastModified)
      return false;

    if (path.size() > 0 && other.path.size() > 0 &&
        path[path.size() - 1] != other.path[other.path.size() - 1])
      return false;

    const auto mapping1 = ZC_ASSERT_NONNULL(file).mmap(0, size);
    const auto mapping2 = ZC_ASSERT_NONNULL(other.file).mmap(0, size);
    return mapping1 == mapping2;
  }
};

struct FileKeyHash {
  size_t operator()(const FileKey& key) const {
    constexpr size_t prime = 0x9e3779b97f4a7c15;
    size_t seed = key.hashCode;

    for (auto& part : key.path) {
      seed ^= zc::hashCode<zc::StringPtr>(part) + prime + (seed << 6) + (seed >> 2);
    }

    seed = (seed ^ (key.size * prime)) * prime;
    seed = (seed ^ ((key.lastModified - zc::UNIX_EPOCH) / zc::MILLISECONDS * prime)) * prime;

    if constexpr (sizeof(size_t) < sizeof(decltype(key.hashCode))) { return (seed >> 32) ^ seed; }
    return seed;
  }
};

}  // namespace

// ========== ModuleLoader

ModuleLoader::ModuleLoader() : impl(zc::heap<Impl>()) {}
ModuleLoader::~ModuleLoader() noexcept(false) {}

// ========== ModuleLoader::Impl

class ModuleLoader::Impl {
public:
  Impl() noexcept = default;
  ~Impl() noexcept(false) = default;

  zc::Maybe<Module&> loadModule(const zc::ReadableDirectory& dir, zc::PathPtr path);

private:
  std::unordered_map<FileKey, zc::Own<Module>, FileKeyHash> modules;
  uint64_t nextModuleId;
};

zc::Maybe<Module&> ModuleLoader::loadModule(const zc::ReadableDirectory& dir,
                                            const zc::PathPtr path) {
  return impl->loadModule(dir, path);
}

// ModuleLoader::ModuleImpl
class ModuleLoader::ModuleImpl final : public Module {
public:
  ModuleImpl(Impl& loader, zc::Own<const zc::ReadableFile> file,
             const zc::ReadableDirectory& sourceDir, zc::Path pathParam, const uint64_t id)
      : loader(loader),
        file(zc::mv(file)),
        sourceDir(sourceDir),
        path(zc::mv(pathParam)),
        sourceNameStr(path.toString()),
        moduleId(id),
        compiled(false) {
    ZC_REQUIRE(path.size() > 0);
  }

  ~ModuleImpl() noexcept(false) override {};

  /// @brief Returns the source name of this module.
  zc::StringPtr getSourceName() override;
  /// @brief Returns true if this module is compiled.
  bool isCompiled() override;
  /// @brief Retrieves the unique ID of the module
  uint64_t getModuleId() const override { return moduleId; }

private:
  ZC_UNUSED Impl& loader;
  zc::Own<const zc::ReadableFile> file;
  ZC_UNUSED const zc::ReadableDirectory& sourceDir;
  zc::Path path;
  zc::String sourceNameStr;
  uint64_t moduleId;
  bool compiled;
};

zc::StringPtr ModuleLoader::ModuleImpl::getSourceName() { return sourceNameStr; }
bool ModuleLoader::ModuleImpl::isCompiled() { return compiled; }

zc::Maybe<Module&> ModuleLoader::Impl::loadModule(const zc::ReadableDirectory& dir,
                                                  zc::PathPtr path) {
  ZC_IF_SOME(file, dir.tryOpenFile(path)) {
    zc::Path pathCopy = path.clone();
    auto key = FileKey(dir, pathCopy, *file);
    if (const auto it = modules.find(key); it != modules.end()) { return *it->second; }

    uint64_t id = nextModuleId++;
    zc::Own<ModuleImpl> module =
        zc::heap<ModuleImpl>(*this, zc::mv(file), dir, zc::mv(pathCopy), id);

    auto& result = *module;
    const auto [fst, snd] = modules.insert(std::make_pair(zc::mv(key), zc::mv(module)));
    if (snd) { return result; }
    // Now that we have the file open, we noticed a collision. Return the old file.
    return *fst->second;
  }

  return zc::none;
}

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
    zc::Array<FixIt> fixIts;
  };

  Impl() noexcept;
  ~Impl() noexcept(false);

  // Buffer management
  DirWithPath getDirWithPath(zc::StringPtr file);
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
  zc::Own<zc::Filesystem> disk;

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

SourceManager::Impl::Impl() noexcept : disk(zc::newDiskFilesystem()) {}

SourceManager::Impl::~Impl() noexcept(false) = default;

SourceManager::DirWithPath SourceManager::Impl::getDirWithPath(const zc::StringPtr filePath) {
  const zc::PathPtr cwd = disk->getCurrentPath();
  zc::Path path = cwd.evalNative(filePath);

  ZC_REQUIRE(path.size() > 0);
  zc::Path sourcePath =
      path.startsWith(cwd) ? path.slice(cwd.size(), path.size()).clone() : zc::mv(path);
  const zc::ReadableDirectory& dir = path.startsWith(cwd) ? disk->getCurrent() : disk->getRoot();

  return DirWithPath{dir, zc::mv(sourcePath)};
}

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

// SourceManager

SourceManager::SourceManager() noexcept : impl(zc::heap<Impl>()) {}

SourceManager::~SourceManager() noexcept(false) = default;

SourceManager::DirWithPath SourceManager::getDirWithPath(const zc::StringPtr file) {
  return impl->getDirWithPath(file);
}

void SourceManager::createVirtualFile(const SourceLoc& loc, const zc::StringPtr name,
                                      int lineOffset, unsigned length) {
  impl->createVirtualFile(loc, name, lineOffset, length);
}

void SourceManager::getMessage(const SourceLoc& loc, DiagnosticKind kind, const zc::String& msg,
                               zc::ArrayPtr<SourceRange> ranges, zc::ArrayPtr<FixIt> fixIts,
                               zc::OutputStream& os) const {
  impl->getMessage(loc, kind, msg, ranges, fixIts, os);
}

}  // namespace compiler
}  // namespace zomlang
