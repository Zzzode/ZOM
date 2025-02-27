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

#pragma once

#include "zc/core/filesystem.h"
#include "zc/core/string.h"

namespace zomlang {
namespace compiler {

namespace diagnostics {
class DiagnosticEngine;
}

namespace source {

class SourceManager;
class ModuleFile;
class CharSourceRange;

class Module {
public:
  /// Module types
  enum class Kind {
    MainModule,   // 主模块
    Library,      // 库模块
    ClangModule,  // Clang 兼容模块
    Synthesized,  // 编译器生成
    Plugin        // 插件模块
  };

  /// Module compilation phase
  enum class Phase {
    Parsed,       // 已解析
    TypeChecked,  // 类型检查完成
    ZISGen,       // 生成 ZIS
    IRGen,        // 生成 IR
    Optimized,    // 优化完成
    Emitted       // 代码生成完成
  };

  Module(Kind kind, zc::String name) noexcept;
  ~Module() noexcept(false);

  void addSourceFile(zc::Own<ModuleFile> file);
  const zc::Vector<zc::Own<ModuleFile>>& sourceFiles() const;

  void addDependency(Module& dep, bool isPublic);
  const zc::Vector<Module*>& getDependencies() const;

  Phase currentPhase() const;
  void advanceToPhase(Phase newPhase);

  Kind getModuleKind() const;
  zc::StringPtr getModuleName() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ModuleFile {
public:
  ModuleFile(zc::StringPtr filename, uint64_t bufferId);

  zc::String filename() const;
  zc::ArrayPtr<const zc::byte> getContent(const SourceManager& sm) const;
  CharSourceRange entireRange() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

class ModuleLoader {
public:
  struct SearchPath {
    zc::Path path;
    bool isSystem;  // System directories have higher priority
  };

  explicit ModuleLoader(SourceManager& sm) noexcept;
  ~ModuleLoader() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ModuleLoader);

  /// Add a module search path
  void addSearchPath(zc::Path path, bool isSystem = false);

  const zc::Path resolveModulePath(zc::StringPtr modulePath);

  /// Loading dependencies recursively
  zc::Maybe<const Module&> loadModule(zc::StringPtr moduleName, uint64_t bufferId,
                                      Module::Kind kind, diagnostics::DiagnosticEngine& diag);

private:
  class Impl;
  zc::Own<Impl> impl;
};

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
