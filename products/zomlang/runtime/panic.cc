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

#include <errno.h>

#include <cstdlib>

#include "zc/core/miniposix.h"

namespace zomlang {
namespace runtime {

namespace {

void writeAllToStderr(zc::ArrayPtr<const zc::byte> data) {
  auto pos = data.begin();
  auto remaining = data.size();
  while (remaining > 0) {
    zc::miniposix::ssize_t written = zc::miniposix::write(STDERR_FILENO, pos, remaining);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return;
    pos += written;
    remaining -= static_cast<size_t>(written);
  }
}

void writeStringToStderr(zc::StringPtr text) { writeAllToStderr(text.asBytes()); }

void writeUIntToStderr(uint64_t value) {
  char buffer[20];
  char* end = buffer + sizeof(buffer);
  char* pos = end;
  do {
    *--pos = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value > 0);
  writeAllToStderr(zc::arrayPtr(pos, end).asBytes());
}

void reportPanicBeforeAbort(const ZomPanicInfo* info) {
  ZomPanicInfo fallback;
  const auto& resolvedInfo = info == nullptr ? fallback : *info;
  zc::StringPtr file =
      resolvedInfo.span.file == nullptr ? "<unknown>"_zc : zc::StringPtr(resolvedInfo.span.file);
  zc::StringPtr message =
      resolvedInfo.message == nullptr ? "<none>"_zc : zc::StringPtr(resolvedInfo.message);

  writeStringToStderr("panic(kind="_zc);
  writeStringToStderr(panicKindName(resolvedInfo.kind));
  writeStringToStderr(", file="_zc);
  writeStringToStderr(file);
  writeStringToStderr(", line="_zc);
  writeUIntToStderr(resolvedInfo.span.line);
  writeStringToStderr(", column="_zc);
  writeUIntToStderr(resolvedInfo.span.column);
  writeStringToStderr(", bytes="_zc);
  writeUIntToStderr(resolvedInfo.span.byteStart);
  writeStringToStderr(".."_zc);
  writeUIntToStderr(resolvedInfo.span.byteEnd);
  writeStringToStderr(", message="_zc);
  writeStringToStderr(message);
  writeStringToStderr(", task="_zc);
  writeUIntToStderr(resolvedInfo.taskId);
  writeStringToStderr(")\n"_zc);
}

}  // namespace

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

zc::StringPtr panicStrategyName(ZomPanicStrategy strategy) {
  switch (strategy) {
    case ZomPanicStrategy::Abort:
      return "abort"_zc;
    case ZomPanicStrategy::Unwind:
      return "unwind"_zc;
  }
  ZC_UNREACHABLE;
}

bool isPanicStrategySupported(ZomPanicStrategy strategy) {
  switch (strategy) {
    case ZomPanicStrategy::Abort:
      return true;
    case ZomPanicStrategy::Unwind:
      return false;
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
  zomlang::runtime::reportPanicBeforeAbort(info);
  std::abort();
}

bool __zom_catch_unwind(zomlang::runtime::ZomPanicThunk thunk, void* context,
                        zomlang::runtime::ZomPanicInfo* outPanicInfo) {
  if (outPanicInfo != nullptr) { *outPanicInfo = zomlang::runtime::ZomPanicInfo(); }
  if (thunk != nullptr) { thunk(context); }
  return false;
}
}
