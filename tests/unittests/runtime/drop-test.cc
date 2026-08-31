// Copyright (c) 2026 Zode.Z. All rights reserved.
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

#include "runtime/drop.h"

#include "zc/ztest/test.h"

namespace {

ZC_TEST("Runtime.DropGlueAcceptsNullReceiver") {
  // A builtin drop of a null receiver is a no-op that returns normally.
  __zom_drop(nullptr);
}

ZC_TEST("Runtime.DropGlueAcceptsStackAddress") {
  int value = 7;
  // A builtin drop leaves the value untouched and returns normally.
  __zom_drop(&value);
  ZC_EXPECT(value == 7);
}

ZC_TEST("Runtime.DropGlueIsRepeatable") {
  int value = 0;
  // The glue carries no per-value state, so repeated drops stay no-ops.
  __zom_drop(&value);
  __zom_drop(&value);
  ZC_EXPECT(value == 0);
}

}  // namespace
