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

#include <signal.h>

#include "zc/core/function.h"
#include "zc/ztest/test.h"

namespace zomlang {
namespace runtime {

namespace {

void setBool(void* context) {
  bool& value = *static_cast<bool*>(context);
  value = true;
}

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
  ZomPanicInfo info;
  info.kind = ZomPanicKind::ExplicitPanic;
  ZC_EXPECT_SIGNAL(SIGABRT, __zom_abort_panic(&info));
}

ZC_TEST("Runtime.PanicAbiSymbolsAreLinkable") {
  ZC_EXPECT(__zom_panic != nullptr);
  ZC_EXPECT(__zom_abort_panic != nullptr);
  ZC_EXPECT(__zom_begin_panic_unwind != nullptr);
  ZC_EXPECT(__zom_catch_unwind != nullptr);
}

}  // namespace runtime
}  // namespace zomlang
