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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compiler/ir/executable-publication.h"

#include "compiler/ir/target-registry.h"
#include "zc/core/array.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

zc::Array<uint8_t> elfBytes() {
  return zc::heapArray<uint8_t>({0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00});
}

ZC_TEST("Executable format check accepts ELF magic and rejects mismatches") {
  auto elf = elfBytes();
  ZC_EXPECT(inspectExecutableFormat(elf.asPtr(), ObjectFormat::Elf));
  ZC_EXPECT(!inspectExecutableFormat(elf.asPtr(), ObjectFormat::MachO));

  auto junk = zc::heapArray<uint8_t>({0x00, 0x01, 0x02, 0x03});
  ZC_EXPECT(!inspectExecutableFormat(junk.asPtr(), ObjectFormat::Elf));
}

ZC_TEST("Executable format check rejects a payload shorter than the magic") {
  auto tooShort = zc::heapArray<uint8_t>({0x7f, 0x45});
  ZC_EXPECT(!inspectExecutableFormat(tooShort.asPtr(), ObjectFormat::Elf));
}

ZC_TEST("Executable format check fails closed for formats without a rule") {
  auto elf = elfBytes();
  ZC_EXPECT(!inspectExecutableFormat(elf.asPtr(), ObjectFormat::Coff));
  ZC_EXPECT(!inspectExecutableFormat(elf.asPtr(), ObjectFormat::Wasm));
}

}  // namespace
}  // namespace zomlang::compiler::ir
