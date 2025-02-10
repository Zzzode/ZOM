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
namespace source {

class SourceManager;

class Module {
public:
  Module(zc::Own<SourceManager>, zc::StringPtr moduleName, uint64_t id) noexcept;
  ~Module() noexcept(false);

  /// Creates a new module from the given file.
  static zc::Own<Module> create(zc::Own<SourceManager> sm, zc::StringPtr moduleName, uint64_t id);

  /// Returns the source name of this module.
  zc::StringPtr getModuleName();
  /// Returns the source content of this module.
  ZC_NODISCARD bool isCompiled() const;
  /// Retrieves the unique ID of the module
  ZC_NODISCARD uint64_t getModuleId() const;
  /// Returns the source manager.
  SourceManager& getSourceManager();

  bool operator==(const Module& rhs) const { return getModuleId() == rhs.getModuleId(); }
  bool operator!=(const Module& rhs) const { return getModuleId() != rhs.getModuleId(); }

private:
  class Impl;
  zc::Own<Impl> impl;
};

class ModuleLoader {
public:
  ModuleLoader();
  ~ModuleLoader() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(ModuleLoader);

  /// Loads a module from the given path.
  zc::Maybe<const Module&> loadModule(const zc::ReadableDirectory& dir, zc::PathPtr path);
  zc::Maybe<const Module&> loadModule(zc::StringPtr path);

private:
  class Impl;
  zc::Own<Impl> impl;
};

}  // namespace source
}  // namespace compiler
}  // namespace zomlang
