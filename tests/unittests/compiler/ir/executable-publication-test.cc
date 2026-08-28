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

// RFC 0043 O5/KR5.3 slice (Tier 1.4): prove the executable-format verifier
// matches leading object-format magic (never inferring safety from a linker exit
// status) and that publication is an atomic two-file transaction - executable
// plus .zom-artifact manifest written to temporaries then renamed, never
// replacing an existing final path, leaving nothing behind on failure.

#include "compiler/ir/executable-publication.h"

#include "compiler/ir/target-registry.h"
#include "zc/core/array.h"
#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

// A minimal ELF-magic executable payload.
zc::Array<uint8_t> elfBytes() {
  return zc::heapArray<uint8_t>({0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00});
}

// A payload with no recognized magic.
zc::Array<uint8_t> junkBytes() { return zc::heapArray<uint8_t>({0x00, 0x01, 0x02, 0x03}); }

zc::Array<uint8_t> manifestBytes() {
  return zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d, 0x2d, 0x61, 0x72});
}

ZC_TEST("Executable format check accepts ELF magic and rejects mismatches") {
  auto elf = elfBytes();
  ZC_EXPECT(inspectExecutableFormat(elf.asPtr(), ObjectFormat::Elf));
  // The same ELF payload is not a Mach-O executable.
  ZC_EXPECT(!inspectExecutableFormat(elf.asPtr(), ObjectFormat::MachO));

  auto junk = junkBytes();
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

ZC_TEST("Publication writes both the executable and the manifest") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  auto executable = elfBytes();
  auto manifest = manifestBytes();

  ExecutablePublicationResult result = publishExecutable(*dir, "app"_zc, "app.zom-artifact"_zc,
                                                         executable.asPtr(), manifest.asPtr());
  ZC_ASSERT(result.ok());

  ZC_EXPECT(dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(dir->exists(zc::Path("app.zom-artifact"_zc)));
  ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllBytes().asPtr() == executable.asPtr());
  ZC_EXPECT(dir->openFile(zc::Path("app.zom-artifact"_zc))->readAllBytes().asPtr() ==
            manifest.asPtr());
}

ZC_TEST("Publication refuses to replace an existing executable destination") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  // Pre-existing final executable path.
  dir->openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("old"_zc);

  auto executable = elfBytes();
  auto manifest = manifestBytes();
  ExecutablePublicationResult result = publishExecutable(*dir, "app"_zc, "app.zom-artifact"_zc,
                                                         executable.asPtr(), manifest.asPtr());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ExecutablePublicationFailure::DestinationExists);
  // The manifest must not have been written when the executable path is taken.
  ZC_EXPECT(!dir->exists(zc::Path("app.zom-artifact"_zc)));
}

ZC_TEST("Publication refuses to replace an existing manifest destination") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  // Pre-existing final manifest path.
  dir->openFile(zc::Path("app.zom-artifact"_zc), zc::WriteMode::CREATE)->writeAll("old"_zc);

  auto executable = elfBytes();
  auto manifest = manifestBytes();
  ExecutablePublicationResult result = publishExecutable(*dir, "app"_zc, "app.zom-artifact"_zc,
                                                         executable.asPtr(), manifest.asPtr());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ExecutablePublicationFailure::DestinationExists);
  // The executable must not have been written when the manifest path is taken.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
}

}  // namespace
}  // namespace zomlang::compiler::ir
