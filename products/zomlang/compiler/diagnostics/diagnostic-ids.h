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

#include <cstdint>

namespace zomlang {
namespace compiler {
namespace diagnostics {

enum class DiagID : uint32_t {
#define DIAG(Name, ...) Name,
#include "zomlang/compiler/diagnostics/diagnostics.def"

#undef DIAG
  NumDiags
};

enum class DiagSeverity : uint8_t { Note, Remark, Warning, Error, Fatal };

}  // namespace diagnostics
}  // namespace compiler
}  // namespace zomlang