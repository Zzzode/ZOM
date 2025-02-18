// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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

#include "zc/core/string.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"

namespace zomlang {
namespace compiler {

namespace source {
class BufferId;
}

namespace driver {

class CompilerDriver {
public:
  CompilerDriver() noexcept;
  ~CompilerDriver() noexcept(false);

  zc::Maybe<source::BufferId> addSourceFile(zc::StringPtr file);

  const diagnostics::DiagnosticEngine& getDiagnosticEngine() const;

private:
  class Impl;
  zc::Own<Impl> impl;
};

}  // namespace driver
}  // namespace compiler
}  // namespace zomlang
