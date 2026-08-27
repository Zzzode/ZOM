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
#include <signal.h>
#if !_WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "zc/core/exception.h"
#include "zc/core/function.h"
#include "zc/core/io.h"
#include "zc/core/miniposix.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace runtime {

namespace {

void setBool(void* context) {
  bool& value = *static_cast<bool*>(context);
  value = true;
}

#if !_WIN32
using PanicEntry = void (*)(const ZomPanicInfo*);

bool panicEntryWritesToStderrAndSignals(PanicEntry entry, const ZomPanicInfo& info,
                                        zc::StringPtr expected) {
  int pipeFds[2]{};
  ZC_SYSCALL(zc::miniposix::pipe(pipeFds));

  pid_t child;
  ZC_SYSCALL(child = fork());
  if (child == 0) {
    zc::resetCrashHandlers();
    zc::miniposix::close(pipeFds[0]);
    if (dup2(pipeFds[1], STDERR_FILENO) < 0) { _exit(127); }
    zc::miniposix::close(pipeFds[1]);
    entry(&info);
  }

  zc::miniposix::close(pipeFds[1]);
  zc::VectorOutputStream stderrOutput;
  zc::byte buffer[256];
  for (;;) {
    zc::miniposix::ssize_t bytesRead = zc::miniposix::read(pipeFds[0], buffer, sizeof(buffer));
    if (bytesRead < 0 && errno == EINTR) continue;
    if (bytesRead <= 0) break;
    stderrOutput.write(zc::arrayPtr(buffer).first(static_cast<size_t>(bytesRead)));
  }
  zc::miniposix::close(pipeFds[0]);

  int status;
  ZC_SYSCALL(waitpid(child, &status, 0));
  auto actual = zc::str(stderrOutput.getArray().asChars());
  bool signaled = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
  bool matches = actual == expected;
  ZC_EXPECT(signaled);
  ZC_EXPECT(matches);
  return signaled && matches;
}
#endif

}  // namespace

ZC_TEST("Runtime.PanicKindNamesAreStable") {
  ZC_EXPECT(panicKindName(ZomPanicKind::ForcedUnwrap) == "forced_unwrap"_zc);
  ZC_EXPECT(panicKindName(ZomPanicKind::ExplicitPanic) == "explicit_panic"_zc);
  ZC_EXPECT(panicKindName(ZomPanicKind::Unreachable) == "unreachable"_zc);
  ZC_EXPECT(panicKindName(ZomPanicKind::Todo) == "todo"_zc);
  ZC_EXPECT(panicKindName(ZomPanicKind::Assertion) == "assertion"_zc);
  ZC_EXPECT(panicKindName(ZomPanicKind::Bounds) == "bounds"_zc);
  ZC_EXPECT(panicKindName(ZomPanicKind::Overflow) == "overflow"_zc);
  ZC_EXPECT(panicKindName(ZomPanicKind::Runtime) == "runtime"_zc);
}

ZC_TEST("Runtime.PanicStrategyNamesAreStable") {
  ZC_EXPECT(panicStrategyName(ZomPanicStrategy::Abort) == "abort"_zc);
  ZC_EXPECT(panicStrategyName(ZomPanicStrategy::Unwind) == "unwind"_zc);
}

ZC_TEST("Runtime.PanicStrategySupportReflectsCurrentRuntime") {
  ZC_EXPECT(isPanicStrategySupported(ZomPanicStrategy::Abort));
  ZC_EXPECT(!isPanicStrategySupported(ZomPanicStrategy::Unwind));
}

ZC_TEST("Runtime.FormatPanicInfoIncludesStableMetadata") {
  ZomPanicInfo info;
  info.kind = ZomPanicKind::ForcedUnwrap;
  info.span.file = "main.zom";
  info.span.line = 4;
  info.span.column = 12;
  info.span.byteStart = 20;
  info.span.byteEnd = 24;
  info.message = "forced unwrap failed";
  info.taskId = 9;

  ZC_EXPECT(formatPanicInfo(info) ==
            "panic(kind=forced_unwrap, file=main.zom, line=4, column=12, bytes=20..24, "
            "message=forced unwrap failed, task=9)"_zc);
}

ZC_TEST("Runtime.FormatPanicInfoHandlesMissingOptionalFields") {
  ZomPanicInfo info;

  ZC_EXPECT(formatPanicInfo(info) ==
            "panic(kind=runtime, file=<unknown>, line=0, column=0, bytes=0..0, "
            "message=<none>, task=0)"_zc);
}

ZC_TEST("Runtime.CatchUnwindAbortStrategyCallsThunkAndReturnsFalse") {
  bool called = false;
  ZomPanicInfo info;
  info.kind = ZomPanicKind::ExplicitPanic;
  info.message = "old";

  bool caught = __zom_catch_unwind(setBool, &called, &info);

  ZC_EXPECT(called);
  ZC_EXPECT(!caught);
  ZC_EXPECT(info.kind == ZomPanicKind::Runtime);
  ZC_EXPECT(info.message == nullptr);
}

ZC_TEST("Runtime.CatchUnwindHandlesNullThunk") {
  ZomPanicInfo info;

  bool caught = __zom_catch_unwind(nullptr, nullptr, &info);

  ZC_EXPECT(!caught);
  ZC_EXPECT(info.kind == ZomPanicKind::Runtime);
}

ZC_TEST("Runtime.AbortPanicRaisesSigabrt") {
#if _WIN32
  ZC_EXPECT_SIGNAL(SIGABRT, __zom_abort_panic(nullptr));
#else
  ZomPanicInfo info;
  info.kind = ZomPanicKind::ExplicitPanic;
  ZC_EXPECT(panicEntryWritesToStderrAndSignals(
      __zom_abort_panic, info,
      "panic(kind=explicit_panic, file=<unknown>, line=0, column=0, bytes=0..0, "
      "message=<none>, task=0)\n"_zc));
#endif
}

ZC_TEST("Runtime.AbortPanicWritesMetadataBeforeSignal") {
#if _WIN32
  ZC_EXPECT(true);
#else
  ZomPanicInfo info;
  info.kind = ZomPanicKind::Assertion;
  info.span.file = "assert.zom";
  info.span.line = 8;
  info.span.column = 3;
  info.span.byteStart = 40;
  info.span.byteEnd = 47;
  info.message = "assertion failed";
  info.taskId = 77;

  ZC_EXPECT(panicEntryWritesToStderrAndSignals(
      __zom_abort_panic, info,
      "panic(kind=assertion, file=assert.zom, line=8, column=3, bytes=40..47, "
      "message=assertion failed, task=77)\n"_zc));
#endif
}

ZC_TEST("Runtime.PanicEntrypointsUseAbortStrategy") {
#if _WIN32
  ZC_EXPECT_SIGNAL(SIGABRT, __zom_panic(nullptr));
#else
  ZomPanicInfo info;
  info.kind = ZomPanicKind::Bounds;
  info.span.file = "bounds.zom";
  info.span.line = 12;
  info.span.column = 9;
  info.span.byteStart = 50;
  info.span.byteEnd = 55;
  info.message = "index out of bounds";
  info.taskId = 4;

  ZC_EXPECT(panicEntryWritesToStderrAndSignals(
      __zom_panic, info,
      "panic(kind=bounds, file=bounds.zom, line=12, column=9, bytes=50..55, "
      "message=index out of bounds, task=4)\n"_zc));
  ZC_EXPECT(panicEntryWritesToStderrAndSignals(
      __zom_begin_panic_unwind, info,
      "panic(kind=bounds, file=bounds.zom, line=12, column=9, bytes=50..55, "
      "message=index out of bounds, task=4)\n"_zc));
#endif
}

ZC_TEST("Runtime.PanicAbiSymbolsAreLinkable") {
  ZC_EXPECT(__zom_panic != nullptr);
  ZC_EXPECT(__zom_abort_panic != nullptr);
  ZC_EXPECT(__zom_begin_panic_unwind != nullptr);
  ZC_EXPECT(__zom_catch_unwind != nullptr);
}

}  // namespace runtime
}  // namespace zomlang
