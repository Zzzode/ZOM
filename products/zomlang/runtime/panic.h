// Copyright (c) 2025 Zode.Z. All rights reserved.
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

#include "zc/core/string.h"

namespace zomlang {
namespace runtime {

/// \brief Runtime panic category passed through the target-independent panic ABI.
enum class ZomPanicKind : uint8_t {
  ForcedUnwrap,
  ExplicitPanic,
  Unreachable,
  Todo,
  Assertion,
  Bounds,
  Overflow,
  Runtime,
};

/// \brief Source position carried by panic metadata.
struct ZomSourceSpan {
  const char* file = nullptr;
  uint32_t line = 0;
  uint32_t column = 0;
  uint32_t byteStart = 0;
  uint32_t byteEnd = 0;
};

/// \brief Target-independent panic metadata consumed by runtime entry points.
struct ZomPanicInfo {
  ZomPanicKind kind = ZomPanicKind::Runtime;
  ZomSourceSpan span;
  const char* message = nullptr;
  const void* payload = nullptr;
  const void* backtrace = nullptr;
  uint64_t taskId = 0;
};

using ZomPanicThunk = void (*)(void*);

/// \brief Stable text name for a panic kind.
zc::StringPtr panicKindName(ZomPanicKind kind);

}  // namespace runtime
}  // namespace zomlang

extern "C" {

[[noreturn]] void __zom_panic(const zomlang::runtime::ZomPanicInfo* info);

[[noreturn]] void __zom_begin_panic_unwind(const zomlang::runtime::ZomPanicInfo* info);

[[noreturn]] void __zom_abort_panic(const zomlang::runtime::ZomPanicInfo* info);

bool __zom_catch_unwind(zomlang::runtime::ZomPanicThunk thunk, void* context,
                        zomlang::runtime::ZomPanicInfo* outPanicInfo);
}
