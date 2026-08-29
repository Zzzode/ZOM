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

// RFC 0043 O5/KR5.3 slice (Tier 1.5, D3b): the InvokeLinker step snapshots and
// re-verifies EVERY link input (driver, closure CRT objects and default
// libraries, object and runtime records) into a transaction-private directory,
// then execs the driver by the snapshot descriptor with a rewritten argv naming
// snapshot paths. This defeats the input side of the link TOCTOU: an inode
// swapped after verification but before exec is never linked. The driver is a
// real compiled ELF (ZOM_FAKE_LINKER_SUCCESS/PARTIAL/NO_OUTPUT, one variant per
// compile-time behavior); a `#!` script cannot be exec'd by descriptor. On
// non-Linux hosts the descriptor-exec path does not exist and the integration
// cases are skipped, but the in-memory fail-closed case still runs.

#include "compiler/ir/invoke-linker.h"

#include <unistd.h>

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::ir {
namespace {

identity::Sha256Digest digestOf(zc::ArrayPtr<const zc::byte> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_REQUIRE(digest != zc::none);
  return ZC_REQUIRE_NONNULL(digest);
}

// A unique absolute temp directory for this process's run.
zc::String tempDirPath() { return zc::str("/tmp/zom-link-executable-", getpid()); }

zc::Own<const zc::Directory> openDir(zc::Filesystem& fs, zc::StringPtr absoluteDir) {
  ZC_REQUIRE(absoluteDir.size() > 1 && absoluteDir[0] == '/');
  return fs.getRoot().openSubdir(
      zc::Path::parse(absoluteDir.slice(1)),
      zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);
}

// Writes `bytes` to `<dir>/<name>`, optionally executable, and returns them.
zc::Array<zc::byte> writeFile(const zc::Directory& dir, zc::StringPtr name,
                              zc::ArrayPtr<const zc::byte> bytes, bool executable) {
  zc::WriteMode mode = zc::WriteMode::CREATE | zc::WriteMode::MODIFY;
  if (executable) { mode = mode | zc::WriteMode::EXECUTABLE; }
  dir.openFile(zc::Path::parse(name), mode)->writeAll(bytes);
  auto owned = zc::heapArray<zc::byte>(bytes.size());
  for (size_t i = 0; i < bytes.size(); ++i) { owned[i] = bytes[i]; }
  return owned;
}

zc::Array<zc::byte> bytesOf(zc::StringPtr text) {
  auto owned = zc::heapArray<zc::byte>(text.size());
  for (size_t i = 0; i < text.size(); ++i) { owned[i] = static_cast<zc::byte>(text.begin()[i]); }
  return owned;
}

LinkInputRecord recordFor(zc::StringPtr path, LinkInputRole role,
                          zc::ArrayPtr<const zc::byte> bytes) {
  auto record = LinkInputRecord::make(path, role, digestOf(bytes), bytes.size());
  ZC_REQUIRE(record != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(record));
}

zc::Array<LinkInputRecord> oneInput(LinkInputRecord&& record) {
  auto builder = zc::heapArrayBuilder<LinkInputRecord>(1);
  builder.add(zc::mv(record));
  return builder.finish();
}

#if defined(ZOM_FAKE_LINKER_SUCCESS)

// Reads a compiled fake-linker ELF fixture variant's bytes from the host.
zc::Array<zc::byte> fixtureBytes(zc::StringPtr absolutePath) {
  auto fs = zc::newDiskFilesystem();
  ZC_REQUIRE(absolutePath.size() > 1 && absolutePath[0] == '/');
  return fs->getRoot().openFile(zc::Path::parse(absolutePath.slice(1)))->readAllBytes();
}

// The materialized inputs plus the verified plan naming them, all rooted at
// `base`. Which fake-linker ELF variant is copied to `<base>/ld` (success /
// partial / no-output) is chosen by the caller's `driverFixturePath`, because
// the plan carries no argument surface to carry a runtime mode token.
struct Scenario {
  zc::Array<zc::byte> driverBytes;
  zc::Array<zc::byte> crtBytes;
  zc::Array<zc::byte> objectBytes;
  zc::Array<zc::byte> libBytes;
};

// Materializes driver + inputs under `dir` and builds the verified plan for them.
VerifiedLinkPlan buildScenario(const zc::Directory& dir, zc::StringPtr base,
                               zc::StringPtr driverFixturePath, Scenario& out) {
  out.driverBytes = writeFile(dir, "ld"_zc, fixtureBytes(driverFixturePath).asPtr(),
                              /*executable=*/true);
  out.crtBytes = writeFile(dir, "crt1.o"_zc, bytesOf("CRT-OBJECT-BYTES"_zc).asPtr(), false);
  out.objectBytes = writeFile(dir, "app.o"_zc, bytesOf("USER-OBJECT-BYTES"_zc).asPtr(), false);
  out.libBytes = writeFile(dir, "libc.a"_zc, bytesOf("LIBC-ARCHIVE-BYTES"_zc).asPtr(), false);

  const uint8_t targetIdentity[] = {0x74, 0x67, 0x74};  // "tgt"
  auto driverPath = zc::str(base, "/ld");
  auto crtPath = zc::str(base, "/crt1.o");
  auto objectPath = zc::str(base, "/app.o");
  auto libPath = zc::str(base, "/libc.a");

  auto closure = ToolchainClosureRecord::make(
      zc::arrayPtr(targetIdentity, 3), base, LinkerDriverKind::ElfDriver, driverPath,
      digestOf(out.driverBytes.asPtr()), out.driverBytes.size(),
      oneInput(recordFor(crtPath, LinkInputRole::CrtObject, out.crtBytes.asPtr())),
      oneInput(recordFor(libPath, LinkInputRole::DefaultLibrary, out.libBytes.asPtr())));
  ZC_REQUIRE(closure != zc::none);

  ExecutableLinkRequest request{
      ZC_REQUIRE_NONNULL(zc::mv(closure)),
      zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}),  // "zom"
      oneInput(recordFor(objectPath, LinkInputRole::ObjectArtifact, out.objectBytes.asPtr())),
      zc::Array<LinkInputRecord>(),
      zc::str(base),
      zc::str(base, "/app")};
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

ZC_TEST("linkExecutable snapshots inputs, execs the driver by fd, and reads back the executable") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_ASSERT(result.isVerified());
  auto executable = zc::mv(result).takeVerified();
  zc::ArrayPtr<const uint8_t> bytes = executable.bytes();
  ZC_ASSERT(bytes.size() == 4u);
  ZC_EXPECT(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');

  // The fake driver recorded the invocation it saw at "<output>.args". Every
  // input token must name a snapshot path (inside a private ".zomlink-" dir),
  // NOT the original source path, and the recorded size must match the input.
  auto argsText = dir->openFile(zc::Path("app.args"_zc))->readAllText();
  ZC_EXPECT(argsText.find(".zomlink-"_zc) != zc::none);
  // No original source path survives into the argv (all rewritten to snapshots).
  ZC_EXPECT(argsText.find(zc::str(base, "/app.o")) == zc::none);
  ZC_EXPECT(argsText.find(zc::str(base, "/crt1.o")) == zc::none);
  ZC_EXPECT(argsText.find(zc::str(base, "/libc.a")) == zc::none);
  // The snapshot the driver read for the user object held its expected bytes.
  ZC_EXPECT(argsText.find(zc::str("size=", scenario.objectBytes.size())) != zc::none);

  // The private snapshot tree was removed once linkExecutable returned (the
  // PreparedLinkInputs was dropped after the child was awaited).
  bool foundSnapshot = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { foundSnapshot = true; }
  }
  ZC_EXPECT(!foundSnapshot);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects an input whose inode is swapped after verification") {
  // The plan records the digest of the original app.o. After the plan is built,
  // the app.o pathname is repointed at a DIFFERENT inode with different bytes
  // (remove + create). The snapshot re-verification must reject it as an
  // input-revision mismatch and never spawn the driver.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  dir->remove(zc::Path("app.o"_zc));
  writeFile(*dir, "app.o"_zc, bytesOf("SWAPPED-OBJECT-BYTES-DIFFERENT"_zc).asPtr(), false);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isIrInvariantRejected());
  // No output was produced and no snapshot tree was left behind.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  bool foundSnapshot = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { foundSnapshot = true; }
  }
  ZC_EXPECT(!foundSnapshot);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a driver whose inode is swapped after verification") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Same length swap on the driver so the digest check (not a length check) is
  // what rejects it.
  auto swapped = zc::heapArray<zc::byte>(scenario.driverBytes.size());
  for (size_t i = 0; i < swapped.size(); ++i) { swapped[i] = scenario.driverBytes[i]; }
  swapped[0] = swapped[0] == zc::byte{0} ? zc::byte{1} : zc::byte{0};
  dir->remove(zc::Path("ld"_zc));
  writeFile(*dir, "ld"_zc, swapped.asPtr(), true);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isIrInvariantRejected());
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a pre-existing stale output before snapshotting") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  dir->openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("stale"_zc);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());
  // The stale file is left untouched (we reject, we do not clobber it).
  ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllText() == "stale"_zc);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable cleans partial output when the driver exits nonzero") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_PARTIAL ""_zc, scenario);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());
  // A nonzero exit is CapabilityRejected: OutputCreationFailed per RFC 0043.
  ZC_EXPECT(result.isCapabilityRejected());
  // The partial output the failing driver wrote is removed.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  // The private snapshot tree is also removed.
  bool foundSnapshot = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { foundSnapshot = true; }
  }
  ZC_EXPECT(!foundSnapshot);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable reports a missing output on a clean exit") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_NO_OUTPUT ""_zc, scenario);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());
  // A missing output after a clean exit is CapabilityRejected: OutputCreationFailed.
  ZC_EXPECT(result.isCapabilityRejected());

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

#endif  // defined(ZOM_FAKE_LINKER_SUCCESS)

ZC_TEST("linkExecutable fails closed on a filesystem exposing no real descriptors") {
  // An in-memory filesystem's File::getFd() returns none, so the driver snapshot
  // cannot be exec'd by descriptor. The prepare step must fail closed rather than
  // ever falling back to a pathname exec.
  auto dir = zc::newInMemoryDirectory(zc::nullClock());
  auto driverBytes = bytesOf("ELF-LINKER-DRIVER-BYTES"_zc);
  auto objectBytes = bytesOf("USER-OBJECT-BYTES"_zc);
  dir->openFile(zc::Path::parse("mem/ld"_zc),
                zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT | zc::WriteMode::EXECUTABLE)
      ->writeAll(driverBytes.asPtr());
  dir->openFile(zc::Path::parse("mem/app.o"_zc),
                zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(objectBytes.asPtr());

  const uint8_t targetIdentity[] = {0x74, 0x67, 0x74};
  auto closure = ToolchainClosureRecord::make(
      zc::arrayPtr(targetIdentity, 3), "/mem"_zc, LinkerDriverKind::ElfDriver, "/mem/ld"_zc,
      digestOf(driverBytes.asPtr()), driverBytes.size(), zc::Array<LinkInputRecord>(),
      zc::Array<LinkInputRecord>());
  ZC_REQUIRE(closure != zc::none);
  ExecutableLinkRequest request{
      ZC_REQUIRE_NONNULL(zc::mv(closure)),
      zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}),
      oneInput(recordFor("/mem/app.o"_zc, LinkInputRole::ObjectArtifact, objectBytes.asPtr())),
      zc::Array<LinkInputRecord>(),
      zc::str("/mem"),
      zc::str("/mem/app")};
  auto planResult = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(planResult.isVerified());
  auto plan = zc::mv(planResult).takeVerified();

  auto result = linkExecutable(plan, *dir);
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isCapabilityRejected());
  // No snapshot tree survives the fail-closed path.
  bool foundSnapshot = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { foundSnapshot = true; }
  }
  ZC_EXPECT(!foundSnapshot);
}

}  // namespace
}  // namespace zomlang::compiler::ir
