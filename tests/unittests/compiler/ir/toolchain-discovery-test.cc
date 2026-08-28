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

// RFC 0043 O5/KR5.3 slice (Tier 1.2): prove host toolchain discovery reads only
// the files an explicit spec names, digests each one, and assembles a validated
// ToolchainClosureRecord - failing closed on every missing, empty, or
// mis-declared input with no ambient PATH/environment fallback. The second gate,
// verifyClosureMatchesHostFormat, independently rejects a cleanly-resolved
// closure whose driver family does not match the running host's object format.

#include "compiler/ir/toolchain-discovery.h"

#include "compiler/ir/link-plan-codec.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/filesystem.h"
#include "zc/core/time.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

// A canonical non-empty target identity used across the cases.
zc::Array<uint8_t> targetIdentity() {
  zc::Vector<uint8_t> bytes(4);
  bytes.add(0x01);
  bytes.add(0x02);
  bytes.add(0x03);
  bytes.add(0x04);
  return bytes.releaseAsArray();
}

// One search input record: a relative path to read plus the absolute path
// recorded into the closure.
ToolchainSearchInput input(LinkInputRole role, zc::StringPtr relativePath,
                           zc::StringPtr recordedPath) {
  return ToolchainSearchInput{role, zc::str(relativePath), zc::str(recordedPath)};
}

// A well-formed ELF search spec naming a linker driver, one CRT object, and one
// default library, all as paths relative to the search root.
ToolchainSearchSpec elfSpec() {
  zc::Vector<ToolchainSearchInput> inputs(2);
  inputs.add(input(LinkInputRole::CrtObject, "lib/crt1.o"_zc, "/opt/zom/sysroot/lib/crt1.o"_zc));
  inputs.add(
      input(LinkInputRole::DefaultLibrary, "lib/libc.a"_zc, "/opt/zom/sysroot/lib/libc.a"_zc));
  return ToolchainSearchSpec{
      targetIdentity(),  zc::str("/opt/zom/sysroot"),        LinkerDriverKind::ElfDriver,
      zc::str("bin/ld"), zc::str("/opt/zom/sysroot/bin/ld"), inputs.releaseAsArray()};
}

// Writes `contents` at `relativePath` under `dir`, creating parents.
void writeFile(zc::Directory& dir, zc::StringPtr relativePath, zc::StringPtr contents) {
  dir.openFile(zc::Path::parse(relativePath), zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(contents);
}

// A search root populated with a complete, non-empty ELF toolchain.
zc::Own<zc::Directory> completeRoot() {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "bin/ld"_zc, "ELF-LINKER-DRIVER"_zc);
  writeFile(*dir, "lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "lib/libc.a"_zc, "LIBC-ARCHIVE"_zc);
  return dir;
}

ZC_TEST("Toolchain discovery resolves a complete spec into a validated closure") {
  auto root = completeRoot();
  ToolchainDiscoveryResult result = discoverToolchain(*root, elfSpec());
  ZC_ASSERT(result.ok());

  const ToolchainClosureRecord& closure = result.closure();
  ZC_EXPECT(closure.linkerKind() == LinkerDriverKind::ElfDriver);
  ZC_EXPECT(closure.linkerPath() == "/opt/zom/sysroot/bin/ld"_zc);
  ZC_EXPECT(closure.sysroot() == "/opt/zom/sysroot"_zc);
  ZC_EXPECT(closure.linkerByteCount() == zc::StringPtr("ELF-LINKER-DRIVER"_zc).size());
  ZC_ASSERT(closure.crtObjects().size() == 1u);
  ZC_ASSERT(closure.defaultLibraries().size() == 1u);
  ZC_EXPECT(closure.crtObjects()[0].role() == LinkInputRole::CrtObject);
  ZC_EXPECT(closure.defaultLibraries()[0].role() == LinkInputRole::DefaultLibrary);
}

ZC_TEST("Toolchain discovery rejects an empty target identity as MalformedSpec") {
  auto root = completeRoot();
  ToolchainSearchSpec spec = elfSpec();
  spec.targetSpecificationIdentity = zc::Array<uint8_t>();
  ToolchainDiscoveryResult result = discoverToolchain(*root, spec);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::MalformedSpec);
}

ZC_TEST("Toolchain discovery rejects an empty sysroot as MalformedSpec") {
  auto root = completeRoot();
  ToolchainSearchSpec spec = elfSpec();
  spec.sysroot = zc::str("");
  ToolchainDiscoveryResult result = discoverToolchain(*root, spec);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::MalformedSpec);
}

ZC_TEST("Toolchain discovery rejects a missing linker as LinkerNotFound") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "lib/libc.a"_zc, "LIBC-ARCHIVE"_zc);
  ToolchainDiscoveryResult result = discoverToolchain(*dir, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::LinkerNotFound);
}

ZC_TEST("Toolchain discovery rejects a missing input as InputNotFound") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "bin/ld"_zc, "ELF-LINKER-DRIVER"_zc);
  writeFile(*dir, "lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  // libc.a is absent.
  ToolchainDiscoveryResult result = discoverToolchain(*dir, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::InputNotFound);
}

ZC_TEST("Toolchain discovery rejects an empty linker as EmptyInput") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "bin/ld"_zc, ""_zc);
  writeFile(*dir, "lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "lib/libc.a"_zc, "LIBC-ARCHIVE"_zc);
  ToolchainDiscoveryResult result = discoverToolchain(*dir, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::EmptyInput);
}

ZC_TEST("Toolchain discovery rejects an empty input file as EmptyInput") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "bin/ld"_zc, "ELF-LINKER-DRIVER"_zc);
  writeFile(*dir, "lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "lib/libc.a"_zc, ""_zc);
  ToolchainDiscoveryResult result = discoverToolchain(*dir, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::EmptyInput);
}

ZC_TEST("Toolchain discovery rejects a wrong input role as InvalidInputRole") {
  auto root = completeRoot();
  ToolchainSearchSpec spec = elfSpec();
  zc::Vector<ToolchainSearchInput> inputs(1);
  // ObjectArtifact is not a valid closure input role (only CrtObject /
  // DefaultLibrary are); discovery must reject it.
  inputs.add(
      input(LinkInputRole::ObjectArtifact, "lib/crt1.o"_zc, "/opt/zom/sysroot/lib/crt1.o"_zc));
  spec.inputs = inputs.releaseAsArray();
  ToolchainDiscoveryResult result = discoverToolchain(*root, spec);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::InvalidInputRole);
}

ZC_TEST("Toolchain discovery rejects a non-absolute linker path as ClosureRejected") {
  auto root = completeRoot();
  ToolchainSearchSpec spec = elfSpec();
  // A relative recorded linker path passes the presence check but fails the
  // final ToolchainClosureRecord::make absolute-path invariant.
  spec.linkerAbsolutePath = zc::str("relative/ld");
  ToolchainDiscoveryResult result = discoverToolchain(*root, spec);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::ClosureRejected);
}

ZC_TEST("Toolchain closure format check matches ELF host and rejects others") {
  auto root = completeRoot();
  ToolchainDiscoveryResult result = discoverToolchain(*root, elfSpec());
  ZC_ASSERT(result.ok());
  ZC_EXPECT(verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::Elf));
  // The same ELF closure must be rejected against a non-ELF host format.
  ZC_EXPECT(!verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::MachO));
  ZC_EXPECT(!verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::Coff));
  ZC_EXPECT(!verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::Wasm));
}

}  // namespace
}  // namespace zomlang::compiler::ir
