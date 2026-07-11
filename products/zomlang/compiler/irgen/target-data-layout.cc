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

#include "zomlang/compiler/irgen/target-data-layout.h"

#include "zc/core/common.h"

namespace zomlang {
namespace compiler {
namespace irgen {

TargetDataLayout::TargetDataLayout(uint64_t pointerSize, uint64_t pointerAlignment)
    : pointerSize(pointerSize), pointerAlignment(pointerAlignment) {}

TargetDataLayout TargetDataLayout::ilp32() { return TargetDataLayout(4, 4); }

TargetDataLayout TargetDataLayout::lp64() { return TargetDataLayout(8, 8); }

uint64_t TargetDataLayout::getPointerSize() const { return pointerSize; }

uint64_t TargetDataLayout::getPointerAlignment() const { return pointerAlignment; }

ScalarLayout TargetDataLayout::getIntegerLayout(IntegerScalarWidth width) const {
  switch (width) {
    case IntegerScalarWidth::W8:
      return ScalarLayout{1, 1};
    case IntegerScalarWidth::W16:
      return ScalarLayout{2, 2};
    case IntegerScalarWidth::W32:
      return ScalarLayout{4, 4};
    case IntegerScalarWidth::W64:
      return ScalarLayout{8, 8};
  }
  ZC_UNREACHABLE;
}

ScalarLayout TargetDataLayout::getFloatLayout(FloatScalarWidth width) const {
  switch (width) {
    case FloatScalarWidth::W32:
      return ScalarLayout{4, 4};
    case FloatScalarWidth::W64:
      return ScalarLayout{8, 8};
  }
  ZC_UNREACHABLE;
}

ScalarLayout TargetDataLayout::getBoolLayout() const {
  return getIntegerLayout(IntegerScalarWidth::W8);
}

ScalarLayout TargetDataLayout::getCharLayout() const {
  return getIntegerLayout(IntegerScalarWidth::W32);
}

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
