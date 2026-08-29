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

// RFC 0043 O5/KR5.3 slice: prove host toolchain discovery reads only the files
// an explicit spec names, THROUGH a VerifiedSysroot capability that binds the
// read directory to its canonical identity, digests each one, and assembles a
// validated ToolchainClosureRecord - failing closed on every missing, empty, or
// mis-declared input with no ambient PATH/environment fallback and no divergent
// read-root vs recorded-path. verifyClosureMatchesHostFormat is the second,
// independent host-format gate.

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

// One search input record: a single sysroot-relative path that drives both the
// read/digest and the derived recorded path.
ToolchainSearchInput input(LinkInputRole role, zc::StringPtr relativePath) {
  return ToolchainSearchInput{role, zc::str(relativePath)};
}

// A well-formed ELF search spec naming a linker driver, one CRT object, and one
// default library, all as paths relative to the sysroot.
ToolchainSearchSpec elfSpec() {
  zc::Vector<ToolchainSearchInput> inputs(2);
  inputs.add(input(LinkInputRole::CrtObject, "lib/crt1.o"_zc));
  inputs.add(input(LinkInputRole::DefaultLibrary, "lib/libc.a"_zc));
  return ToolchainSearchSpec{targetIdentity(), LinkerDriverKind::ElfDriver, zc::str("bin/ld"),
                             inputs.releaseAsArray()};
}

// The canonical sysroot absolute path every fixture uses.
constexpr zc::StringPtr kSysrootPath = "/opt/zom/sysroot"_zc;

// Writes `contents` at `relativePath` under `dir`, creating parents.
void writeFile(zc::Directory& dir, zc::StringPtr relativePath, zc::StringPtr contents) {
  dir.openFile(zc::Path::parse(relativePath), zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(contents);
}

// A filesystem root whose /opt/zom/sysroot subtree holds a complete ELF toolchain.
zc::Own<zc::Directory> completeRoot() {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "opt/zom/sysroot/bin/ld"_zc, "ELF-LINKER-DRIVER"_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/libc.a"_zc, "LIBC-ARCHIVE"_zc);
  return dir;
}

// A minimal in-memory Filesystem wrapping one in-memory root directory, so a
// VerifiedSysroot can be opened against a real filesystem-root capability
// (never an arbitrary subdirectory posing as root).
class InMemoryFilesystem final : public zc::Filesystem {
public:
  explicit InMemoryFilesystem(zc::Own<zc::Directory>&& root) : rootDir(zc::mv(root)) {}
  const zc::Directory& getRoot() const override { return *rootDir; }
  const zc::Directory& getCurrent() const override { return *rootDir; }
  zc::PathPtr getCurrentPath() const override { return zc::PathPtr(nullptr); }

private:
  zc::Own<zc::Directory> rootDir;
};

// Opens the VerifiedSysroot at kSysrootPath under a filesystem wrapping `root`,
// requiring success. Keeps the filesystem alive via the returned pair's second.
struct SysrootFixture {
  zc::Own<InMemoryFilesystem> filesystem;
  VerifiedSysroot sysroot;
};

SysrootFixture sysrootOf(zc::Own<zc::Directory>&& root) {
  auto filesystem = zc::heap<InMemoryFilesystem>(zc::mv(root));
  auto sysroot = VerifiedSysroot::open(*filesystem, kSysrootPath);
  ZC_REQUIRE(sysroot != zc::none);
  return SysrootFixture{zc::mv(filesystem), ZC_REQUIRE_NONNULL(zc::mv(sysroot))};
}

ZC_TEST("VerifiedSysroot binds the read directory to its canonical identity") {
  InMemoryFilesystem fs(completeRoot());
  auto sysroot = VerifiedSysroot::open(fs, kSysrootPath);
  ZC_ASSERT(sysroot != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(sysroot).identity() == kSysrootPath);
}

ZC_TEST("VerifiedSysroot rejects a non-absolute canonical path") {
  InMemoryFilesystem fs(completeRoot());
  ZC_EXPECT(VerifiedSysroot::open(fs, "opt/zom/sysroot"_zc) == zc::none);
}

ZC_TEST("VerifiedSysroot rejects the bare root path") {
  InMemoryFilesystem fs(completeRoot());
  ZC_EXPECT(VerifiedSysroot::open(fs, "/"_zc) == zc::none);
}

ZC_TEST("VerifiedSysroot rejects a missing directory") {
  InMemoryFilesystem fs(completeRoot());
  ZC_EXPECT(VerifiedSysroot::open(fs, "/no/such/sysroot"_zc) == zc::none);
}

ZC_TEST("VerifiedSysroot rejects an existing non-directory path") {
  // /opt/zom/sysroot/bin/ld is a file, not a directory: open must fail (none),
  // never throw.
  InMemoryFilesystem fs(completeRoot());
  ZC_EXPECT(VerifiedSysroot::open(fs, "/opt/zom/sysroot/bin/ld"_zc) == zc::none);
}

ZC_TEST("Toolchain discovery resolves a complete spec into a validated closure") {
  auto fixture = sysrootOf(completeRoot());
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, elfSpec());
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

ZC_TEST("Toolchain discovery records the derived sysroot-relative path") {
  // Every recorded path is derived from the bound sysroot identity plus the same
  // relative path that was read, so the digested file and the recorded path name
  // the same object. There is no caller-supplied independent recorded path.
  auto fixture = sysrootOf(completeRoot());
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, elfSpec());
  ZC_ASSERT(result.ok());
  const ToolchainClosureRecord& closure = result.closure();
  ZC_EXPECT(closure.linkerPath() == "/opt/zom/sysroot/bin/ld"_zc);
  ZC_ASSERT(closure.crtObjects().size() == 1u);
  ZC_EXPECT(closure.crtObjects()[0].path() == "/opt/zom/sysroot/lib/crt1.o"_zc);
  ZC_ASSERT(closure.defaultLibraries().size() == 1u);
  ZC_EXPECT(closure.defaultLibraries()[0].path() == "/opt/zom/sysroot/lib/libc.a"_zc);
}

ZC_TEST("Toolchain discovery rejects an empty target identity as MalformedSpec") {
  auto fixture = sysrootOf(completeRoot());
  ToolchainSearchSpec spec = elfSpec();
  spec.targetSpecificationIdentity = zc::Array<uint8_t>();
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, spec);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::MalformedSpec);
}

ZC_TEST("Toolchain discovery rejects a traversal relative path as MalformedSpec") {
  // A '..' segment must be rejected before any read, so the traversal never
  // reaches the filesystem.
  auto fixture = sysrootOf(completeRoot());
  ToolchainSearchSpec spec = elfSpec();
  spec.linkerRelativePath = zc::str("../outside/ld");
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, spec);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::MalformedSpec);
}

ZC_TEST("Toolchain discovery rejects an interior-dotdot and double-slash path") {
  auto fixture = sysrootOf(completeRoot());
  ToolchainSearchSpec spec = elfSpec();
  spec.linkerRelativePath = zc::str("bin/../bin/ld");
  ToolchainDiscoveryResult a = discoverToolchain(fixture.sysroot, spec);
  ZC_ASSERT(!a.ok());
  ZC_EXPECT(a.failure() == ToolchainDiscoveryFailure::MalformedSpec);

  auto fixture2 = sysrootOf(completeRoot());
  ToolchainSearchSpec spec2 = elfSpec();
  spec2.linkerRelativePath = zc::str("bin//ld");
  ToolchainDiscoveryResult b = discoverToolchain(fixture2.sysroot, spec2);
  ZC_ASSERT(!b.ok());
  ZC_EXPECT(b.failure() == ToolchainDiscoveryFailure::MalformedSpec);
}

ZC_TEST("Toolchain discovery rejects a missing linker as LinkerNotFound") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "opt/zom/sysroot/lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/libc.a"_zc, "LIBC-ARCHIVE"_zc);
  auto fixture = sysrootOf(zc::mv(dir));
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::LinkerNotFound);
}

ZC_TEST("Toolchain discovery rejects a missing input as InputNotFound") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "opt/zom/sysroot/bin/ld"_zc, "ELF-LINKER-DRIVER"_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  // libc.a is absent.
  auto fixture = sysrootOf(zc::mv(dir));
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::InputNotFound);
}

ZC_TEST("Toolchain discovery rejects an empty linker as EmptyInput") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "opt/zom/sysroot/bin/ld"_zc, ""_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/libc.a"_zc, "LIBC-ARCHIVE"_zc);
  auto fixture = sysrootOf(zc::mv(dir));
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::EmptyInput);
}

ZC_TEST("Toolchain discovery rejects an empty input file as EmptyInput") {
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  writeFile(*dir, "opt/zom/sysroot/bin/ld"_zc, "ELF-LINKER-DRIVER"_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/crt1.o"_zc, "CRT1-OBJECT"_zc);
  writeFile(*dir, "opt/zom/sysroot/lib/libc.a"_zc, ""_zc);
  auto fixture = sysrootOf(zc::mv(dir));
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, elfSpec());
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::EmptyInput);
}

ZC_TEST("Toolchain discovery rejects a wrong input role as InvalidInputRole") {
  auto fixture = sysrootOf(completeRoot());
  ToolchainSearchSpec spec = elfSpec();
  zc::Vector<ToolchainSearchInput> inputs(1);
  // ObjectArtifact is not a valid closure input role (only CrtObject /
  // DefaultLibrary are); discovery must reject it.
  inputs.add(input(LinkInputRole::ObjectArtifact, "lib/crt1.o"_zc));
  spec.inputs = inputs.releaseAsArray();
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, spec);
  ZC_ASSERT(!result.ok());
  ZC_EXPECT(result.failure() == ToolchainDiscoveryFailure::InvalidInputRole);
}

ZC_TEST("Toolchain closure format check matches ELF host and rejects others") {
  auto fixture = sysrootOf(completeRoot());
  ToolchainDiscoveryResult result = discoverToolchain(fixture.sysroot, elfSpec());
  ZC_ASSERT(result.ok());
  ZC_EXPECT(verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::Elf));
  // The same ELF closure must be rejected against a non-ELF host format.
  ZC_EXPECT(!verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::MachO));
  ZC_EXPECT(!verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::Coff));
  ZC_EXPECT(!verifyClosureMatchesHostFormat(result.closure(), ObjectFormat::Wasm));
}

}  // namespace
}  // namespace zomlang::compiler::ir
