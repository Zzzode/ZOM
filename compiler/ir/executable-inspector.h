// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#pragma once

#include <cstdint>

#include "compiler/ir/link-plan-codec.h"
#include "zc/core/array.h"
#include "zc/core/common.h"

namespace zomlang::compiler::ir {

/// \brief Closed failure classes produced by bounded executable-image inspection.
enum class ExecutableInspectionFailure : uint8_t {
  MalformedImage = 0x01,
  AbiMismatch = 0x02,
  MissingRequiredSymbol = 0x03,
  DuplicateRequiredSymbol = 0x04,
  UnresolvedRuntimeReference = 0x05,
};

/// \brief Independently verifies the bounded ELF64 or Mach-O64 image contract.
class ExecutableImageInspector final {
public:
  /// \return none when the image satisfies `profile`, `entrySymbol`, and every
  ///         required runtime-symbol definition; otherwise the first failure.
  ///
  /// The inspector owns the target-format projection of the entry point.
  /// `entrySymbol` must be the raw logical entry name (for example `zom` or
  /// `_start`); for a Mach-O target the inspector prepends the single leading
  /// underscore the Mach-O symbol table uses before matching, so callers must
  /// not pre-mangle it (a pre-mangled `_zom` would project to `__zom` and fail
  /// to match). Required runtime symbols, by contrast, are recorded in the
  /// profile in the target's raw symbol-table spelling and are matched as-is.
  /// Mach-O real execution is not exercised here; only the image contract is.
  ZC_NODISCARD static zc::Maybe<ExecutableInspectionFailure> inspect(
      zc::ArrayPtr<const uint8_t> bytes, const ExecutableInspectionProfile& profile,
      zc::ArrayPtr<const uint8_t> entrySymbol);
};

}  // namespace zomlang::compiler::ir
