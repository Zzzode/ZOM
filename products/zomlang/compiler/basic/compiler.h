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

#pragma once

#include "zc/core/memory.h"
#include "zc/core/mutex.h"
#include "zomlang/compiler/source/module.h"

namespace zomlang {
namespace compiler {

class Module;
class Diagnostic;

class Compiler {
public:
  Compiler() noexcept;
  ~Compiler() noexcept(false);

  void parseModules() const noexcept;
  zc::Array<Diagnostic> getDiagnostics() const noexcept;

private:
  class Impl;
  zc::MutexGuarded<zc::Own<Impl>> impl;

  class CompiledModule;
};

}  // namespace compiler
}  // namespace zomlang
