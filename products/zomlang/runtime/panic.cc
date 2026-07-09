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

#include "zomlang/runtime/panic.h"

#include <cstdlib>

namespace zomlang {
namespace runtime {

zc::StringPtr panicKindName(ZomPanicKind kind) {
  switch (kind) {
    case ZomPanicKind::ForcedUnwrap:
      return "forced_unwrap"_zc;
    case ZomPanicKind::ExplicitPanic:
      return "explicit_panic"_zc;
    case ZomPanicKind::Unreachable:
      return "unreachable"_zc;
    case ZomPanicKind::Todo:
      return "todo"_zc;
    case ZomPanicKind::Assertion:
      return "assertion"_zc;
    case ZomPanicKind::Bounds:
      return "bounds"_zc;
    case ZomPanicKind::Overflow:
      return "overflow"_zc;
    case ZomPanicKind::Runtime:
      return "runtime"_zc;
  }
  ZC_UNREACHABLE;
}

zc::String formatPanicInfo(const ZomPanicInfo& info) {
  zc::StringPtr file = info.span.file == nullptr ? "<unknown>"_zc : zc::StringPtr(info.span.file);
  zc::StringPtr message = info.message == nullptr ? "<none>"_zc : zc::StringPtr(info.message);
  return zc::str("panic(kind=", panicKindName(info.kind), ", file=", file,
                 ", line=", static_cast<uint64_t>(info.span.line),
                 ", column=", static_cast<uint64_t>(info.span.column),
                 ", bytes=", static_cast<uint64_t>(info.span.byteStart), "..",
                 static_cast<uint64_t>(info.span.byteEnd), ", message=", message,
                 ", task=", info.taskId, ")");
}

}  // namespace runtime
}  // namespace zomlang

extern "C" {

[[noreturn]] void __zom_panic(const zomlang::runtime::ZomPanicInfo* info) {
  __zom_abort_panic(info);
}

[[noreturn]] void __zom_begin_panic_unwind(const zomlang::runtime::ZomPanicInfo* info) {
  __zom_abort_panic(info);
}

[[noreturn]] void __zom_abort_panic(const zomlang::runtime::ZomPanicInfo* info) {
  (void)info;
  std::abort();
}

bool __zom_catch_unwind(zomlang::runtime::ZomPanicThunk thunk, void* context,
                        zomlang::runtime::ZomPanicInfo* outPanicInfo) {
  if (outPanicInfo != nullptr) { *outPanicInfo = zomlang::runtime::ZomPanicInfo(); }
  if (thunk != nullptr) { thunk(context); }
  return false;
}
}
