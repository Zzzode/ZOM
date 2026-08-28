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

// RFC 0043 O5/KR5.3 slice (Tier 1.5, reworked): the InvokeLinker step consumes a
// VerifiedLinkPlan plus the filesystem root - not a forkable bare invocation. It
// re-verifies the driver digest before spawning (rejecting a file replaced after
// discovery), refuses a stale pre-existing output, spawns a real driver, cleans
// partial output on failure, reads back the produced executable on success, and
// maps every rejection to an RFC 0010 LinkerInvocation-phase IrOperationResult.
// It drives a real on-disk fake-linker script, not a mock.

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

identity::Sha256Digest digestOfText(zc::StringPtr text) { return digestOf(text.asBytes()); }

LinkInputRecord input(zc::StringPtr path, LinkInputRole role, zc::StringPtr digestSeed,
                      uint64_t byteCount) {
  auto record = LinkInputRecord::make(path, role, digestOfText(digestSeed), byteCount);
  ZC_REQUIRE(record != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(record));
}

zc::Array<LinkInputRecord> oneInput(LinkInputRecord&& record) {
  auto builder = zc::heapArrayBuilder<LinkInputRecord>(1);
  builder.add(zc::mv(record));
  return builder.finish();
}

zc::Array<LinkerArgumentRecord> noArguments() { return zc::Array<LinkerArgumentRecord>(); }

// A unique absolute temp directory for this process's run.
zc::String tempDirPath() { return zc::str("/tmp/zom-link-executable-", getpid()); }

zc::Own<const zc::Directory> openDir(zc::Filesystem& fs, zc::StringPtr absoluteDir) {
  ZC_REQUIRE(absoluteDir.size() > 1 && absoluteDir[0] == '/');
  return fs.getRoot().openSubdir(
      zc::Path::parse(absoluteDir.slice(1)),
      zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);
}

void writeScript(const zc::Directory& dir, zc::StringPtr name, zc::StringPtr body) {
  dir.openFile(zc::Path::parse(name),
               zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::EXECUTABLE)
      ->writeAll(body);
}

// Builds a verified plan whose driver absolute path and recorded digest/byte
// count match `driverBody` located at `<base>/ld`, output at `<base>/app`.
VerifiedLinkPlan planForDriver(zc::StringPtr base, zc::StringPtr driverBody) {
  const uint8_t targetIdentity[] = {0x74, 0x67, 0x74};  // "tgt"
  auto driverPath = zc::str(base, "/ld");
  auto outputRoot = zc::str(base);
  auto outputPath = zc::str(base, "/app");
  auto objectPath = zc::str(base, "/app.o");

  auto closure = ToolchainClosureRecord::make(
      zc::arrayPtr(targetIdentity, 3), base, LinkerDriverKind::ElfDriver, driverPath,
      digestOfText(driverBody), zc::StringPtr(driverBody).size(), zc::Array<LinkInputRecord>(),
      zc::Array<LinkInputRecord>());
  ZC_REQUIRE(closure != zc::none);

  ExecutableLinkRequest request{
      ZC_REQUIRE_NONNULL(zc::mv(closure)),
      zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}),  // "zom"
      oneInput(input(objectPath, LinkInputRole::ObjectArtifact, "obj", 512)),
      zc::Array<LinkInputRecord>(),
      noArguments(),
      zc::mv(outputRoot),
      zc::mv(outputPath)};
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

constexpr zc::StringPtr kDriverBody = "#!/bin/sh\nprintf '\\177ELF' > \"$2\"\n"_zc;

ZC_TEST("linkExecutable spawns the verified driver and reads back the executable") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  writeScript(*dir, "ld"_zc, kDriverBody);
  auto plan = planForDriver(base, kDriverBody);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_ASSERT(result.isVerified());
  auto executable = zc::mv(result).takeVerified();
  zc::ArrayPtr<const uint8_t> bytes = executable.bytes();
  ZC_ASSERT(bytes.size() == 4u);
  ZC_EXPECT(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a driver replaced after discovery") {
  // The plan's recorded digest is for kDriverBody, but the on-disk driver was
  // swapped for different bytes after discovery. The pre-spawn re-verification
  // must reject it as an input-revision mismatch and never spawn.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  // Same length as kDriverBody so the byte-count check passes and the digest
  // check is what rejects it (a stronger assertion than a length mismatch).
  zc::String swapped = zc::heapString(kDriverBody);
  swapped[swapped.size() - 2] = swapped[swapped.size() - 2] == 'n' ? 'N' : 'n';
  writeScript(*dir, "ld"_zc, swapped);
  auto plan = planForDriver(base, kDriverBody);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isIrInvariantRejected());
  // The output must not have been produced.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a pre-existing stale output") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  writeScript(*dir, "ld"_zc, kDriverBody);
  // A stale file already occupies the plan's output path.
  dir->openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("stale"_zc);
  auto plan = planForDriver(base, kDriverBody);

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
  // Writes a partial output THEN fails: the cleanup must remove it.
  zc::String failingBody = zc::heapString("#!/bin/sh\nprintf 'partial' > \"$2\"\nexit 3\n"_zc);
  writeScript(*dir, "ld"_zc, failingBody);
  auto plan = planForDriver(base, failingBody);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());
  // The partial output the failing driver wrote is removed.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable reports a missing output on a clean exit") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  zc::String noopBody = zc::heapString("#!/bin/sh\nexit 0\n"_zc);
  writeScript(*dir, "ld"_zc, noopBody);
  auto plan = planForDriver(base, noopBody);

  auto result = linkExecutable(plan, fs->getRoot());
  ZC_EXPECT(!result.isVerified());

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

}  // namespace
}  // namespace zomlang::compiler::ir
