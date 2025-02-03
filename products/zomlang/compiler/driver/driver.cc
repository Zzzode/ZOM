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

#include "zomlang/compiler/driver/driver.h"

#include "zc/core/filesystem.h"
#include "zomlang/compiler/basic/compiler.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {

// CompilerDriver::Impl

class CompilerDriver::Impl {
public:
  Impl() noexcept;
  ~Impl() noexcept(false);

  struct OutputDirective {
    zc::ArrayPtr<zc::byte> name;
    zc::Maybe<zc::Path> dir;

    ZC_DISALLOW_COPY(OutputDirective);
    OutputDirective(OutputDirective&&) = default;
    OutputDirective(zc::ArrayPtr<zc::byte> name, zc::Maybe<zc::Path> dir)
        : name(name), dir(zc::mv(dir)) {}
  };

  zc::Maybe<Module&> addSourceFileImpl(zc::StringPtr file);

private:
  zc::Own<Compiler> compiler;
  zc::Own<ModuleLoader> loader;
  zc::Own<SourceManager> sourceManager;

  zc::Vector<OutputDirective> outputs;
};

CompilerDriver::Impl::Impl() noexcept
    : compiler(zc::heap<Compiler>()),
      loader(zc::heap<ModuleLoader>()),
      sourceManager(zc::heap<SourceManager>()) {}

CompilerDriver::Impl::~Impl() noexcept(false) {}

zc::Maybe<Module&> CompilerDriver::Impl::addSourceFileImpl(const zc::StringPtr file) {
  const auto [dir, path] = sourceManager->getDirWithPath(file);
  return loader->loadModule(dir, path);
}

// CompilerDriver

CompilerDriver::CompilerDriver() noexcept : impl(zc::heap<Impl>()) {}
CompilerDriver::~CompilerDriver() noexcept(false) {}

zc::Maybe<Module&> CompilerDriver::addSourceFile(const zc::StringPtr file) {
  return impl->addSourceFileImpl(file);
}

}  // namespace compiler
}  // namespace zomlang
