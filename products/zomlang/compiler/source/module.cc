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

#include "zomlang/compiler/source/module.h"

#include <unordered_map>

#include "zc/core/debug.h"
#include "zc/core/filesystem.h"
#include "zc/core/map.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace source {

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

    seed = (seed ^ key.size * prime) * prime;
    seed = (seed ^ (key.lastModified - zc::UNIX_EPOCH) / zc::MILLISECONDS * prime) * prime;

    if constexpr (sizeof(size_t) < sizeof(decltype(key.hashCode))) { return (seed >> 32) ^ seed; }
    return seed;
  }
};

}  // namespace

// ================================================================================
// Module::Impl

struct Module::Impl {
  Kind kind;
  zc::String name;
  zc::Vector<zc::Own<ModuleFile>> files;
  zc::Vector<const Module*> deps;

  Impl(Kind kind, zc::String name) : kind(kind), name(zc::mv(name)) {}
};

// ================================================================================
// ModuleFile::Impl

struct ModuleFile::Impl {
  zc::String filename;
  uint64_t bufferId;

  Impl(zc::String name, const uint64_t bid) : filename(zc::mv(name)), bufferId(bid) {}
};

zc::ArrayPtr<const zc::byte> ModuleFile::getContent(const SourceManager& sm) const {
  return sm.getEntireTextForBuffer(impl->bufferId);
}

// ================================================================================
// Module

Module::Module(Kind kind, zc::String name) noexcept : impl(zc::heap<Impl>(kind, zc::mv(name))) {};
Module::~Module() noexcept(false) = default;

// ================================================================================
// ModuleLoader

ModuleLoader::ModuleLoader(SourceManager& sm) noexcept : impl(zc::heap<Impl>(sm)) {}
ModuleLoader::~ModuleLoader() noexcept(false) = default;

// ================================================================================
// ModuleLoader::Impl

class ModuleLoader::Impl {
public:
  explicit Impl(SourceManager& sm) noexcept : sm(sm) {}
  ~Impl() noexcept(false) = default;

  zc::Maybe<const Module&> loadModule(zc::StringPtr moduleName, uint64_t bufferId,
                                      Module::Kind kind, diagnostics::DiagnosticEngine& diag);

private:
  SourceManager& sm;
  zc::Vector<SearchPath> searchPaths;
  /// Module caching
  zc::HashMap<zc::StringPtr, Module&> loadedModules;
};

zc::Maybe<const Module&> ModuleLoader::Impl::loadModule(zc::StringPtr moduleName,
                                                        const uint64_t bufferId, Module::Kind kind,
                                                        diagnostics::DiagnosticEngine& diag) {
  ZC_IF_SOME(module, loadedModules.find(moduleName)) { return module; }

  if (diag.hasErrors()) { return zc::none; }

  auto module = zc::heap<ModuleFile>(moduleName, bufferId);
  loadedModules.insert(moduleName, *module);
  return *module;
}

zc::Maybe<const Module&> ModuleLoader::loadModule(const zc::StringPtr moduleName,
                                                  const uint64_t bufferId, const Module::Kind kind,
                                                  diagnostics::DiagnosticEngine& diag) {
  return impl->loadModule(moduleName, bufferId, kind, diag);
}

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
