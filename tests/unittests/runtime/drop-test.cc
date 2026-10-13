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

#include "zc/core/memory.h"
#include "zc/ztest/test.h"

namespace {

struct DropProbe {
  int integer;
  double real;
  char character;
};

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

ZC_TEST("Runtime.DropGlueSymbolIsLinkable") {
  // The ownership rail lowers builtin drops to this raw symbol; it must exist.
  ZC_EXPECT(__zom_drop != nullptr);
}

ZC_TEST("Runtime.DropGlueAcceptsHeapAddress") {
  zc::Own<int> value = zc::heap<int>(42);
  // Dropping a heap address neither frees nor mutates the storage; the owner
  // remains responsible for deallocation.
  __zom_drop(value.get());
  ZC_EXPECT(*value == 42);
}

ZC_TEST("Runtime.DropGlueLeavesMemoryPatternUntouched") {
  alignas(16) unsigned char storage[64];
  for (size_t i = 0; i < sizeof(storage); ++i) { storage[i] = static_cast<unsigned char>(i); }
  __zom_drop(storage);
  for (size_t i = 0; i < sizeof(storage); ++i) {
    ZC_EXPECT(storage[i] == static_cast<unsigned char>(i));
  }
}

ZC_TEST("Runtime.DropGlueLeavesArrayElementsUntouched") {
  int values[5] = {10, 20, 30, 40, 50};
  for (int& element : values) { __zom_drop(&element); }
  for (int i = 0; i < 5; ++i) { ZC_EXPECT(values[i] == (i + 1) * 10); }
}

ZC_TEST("Runtime.DropGlueLeavesStructFieldsUntouched") {
  DropProbe probe{1, 2.5, 'x'};
  __zom_drop(&probe);
  ZC_EXPECT(probe.integer == 1);
  ZC_EXPECT(probe.real == 2.5);
  ZC_EXPECT(probe.character == 'x');
}

ZC_TEST("Runtime.DropGlueAcceptsConstAddress") {
  const int value = 11;
  // The C ABI drops qualifiers; a builtin drop never writes, so const storage
  // is safe to pass after an explicit cast.
  __zom_drop(const_cast<int*>(&value));
  ZC_EXPECT(value == 11);
}

ZC_TEST("Runtime.DropGlueAcceptsOverAlignedAddress") {
  alignas(128) int value = 5;
  __zom_drop(&value);
  ZC_EXPECT(value == 5);
}

ZC_TEST("Runtime.DropGlueAcceptsInteriorAddress") {
  int values[4] = {1, 2, 3, 4};
  // Dropping an interior element leaves every element, including its
  // neighbours, untouched.
  __zom_drop(&values[2]);
  ZC_EXPECT(values[0] == 1);
  ZC_EXPECT(values[1] == 2);
  ZC_EXPECT(values[2] == 3);
  ZC_EXPECT(values[3] == 4);
}

ZC_TEST("Runtime.DropGlueAcceptsArrayBaseAddress") {
  int values[3] = {9, 8, 7};
  // Array-to-pointer decay yields the address of the first element.
  __zom_drop(values);
  ZC_EXPECT(values[0] == 9);
  ZC_EXPECT(values[1] == 8);
  ZC_EXPECT(values[2] == 7);
}

ZC_TEST("Runtime.DropGlueAcceptsStaticStorage") {
  static int value = 9;
  __zom_drop(&value);
  ZC_EXPECT(value == 9);
}

ZC_TEST("Runtime.DropGlueIsSafeUnderRepetition") {
  int value = 3;
  // Stress the no-op contract: the glue must stay side-effect free.
  for (int i = 0; i < 10000; ++i) { __zom_drop(&value); }
  ZC_EXPECT(value == 3);
}

ZC_TEST("Runtime.DropGlueInNestedScopes") {
  int outer = 1;
  {
    int inner = 2;
    {
      int innermost = 3;
      __zom_drop(&innermost);
      ZC_EXPECT(innermost == 3);
    }
    __zom_drop(&inner);
    ZC_EXPECT(inner == 2);
  }
  __zom_drop(&outer);
  ZC_EXPECT(outer == 1);
}

}  // namespace
