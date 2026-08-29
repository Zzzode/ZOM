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

// A minimal in-memory Filesystem wrapping one in-memory root, so linkExecutable
// can be driven against a filesystem whose root exposes no real descriptor (the
// fail-closed case).
class InMemoryFilesystem final : public zc::Filesystem {
public:
  explicit InMemoryFilesystem(zc::Own<zc::Directory>&& root) : rootDir(zc::mv(root)) {}
  const zc::Directory& getRoot() const override { return *rootDir; }
  const zc::Directory& getCurrent() const override { return *rootDir; }
  zc::PathPtr getCurrentPath() const override { return zc::PathPtr(nullptr); }

private:
  zc::Own<zc::Directory> rootDir;
};

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

// Runs a link and asserts the tree was cleaned up (Complete), returning the
// primary IR result. The common case: no cleanup obligation was produced.
IrOperationResult<VerifiedLinkedExecutable> linkAndExpectComplete(const VerifiedLinkPlan& plan,
                                                                  const zc::Filesystem& fs) {
  CleanupAwareOutcome<VerifiedLinkedExecutable> outcome = linkExecutable(plan, fs);
  ZC_ASSERT(outcome.isComplete());
  return zc::mv(outcome).takePrimary();
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

  auto result = linkAndExpectComplete(plan, *fs);
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

  auto result = linkAndExpectComplete(plan, *fs);
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

  auto result = linkAndExpectComplete(plan, *fs);
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

  auto result = linkAndExpectComplete(plan, *fs);
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

  auto result = linkAndExpectComplete(plan, *fs);
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

  auto result = linkAndExpectComplete(plan, *fs);
  ZC_EXPECT(!result.isVerified());
  // A missing output after a clean exit is CapabilityRejected: OutputCreationFailed.
  ZC_EXPECT(result.isCapabilityRejected());

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

// Counts the ".zomlink-" transaction directories directly under `dir`.
size_t countSnapshotTrees(const zc::Directory& dir) {
  size_t count = 0;
  for (const zc::String& name : dir.listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { ++count; }
  }
  return count;
}

ZC_TEST("linkExecutable maps a mid-snapshot write fault to a rejection and cleans up") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // A filesystem fault thrown mid-write must not escape as an exception; it is
  // caught and mapped to an OutputCreationFailed rejection, and the private tree
  // is rolled back (Complete: cleanup itself succeeded).
  CleanupAwareOutcome<VerifiedLinkedExecutable> outcome =
      linkExecutable(plan, *fs, PrepareInjectedFault::WriteMidSnapshot);
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<VerifiedLinkedExecutable> result = zc::mv(outcome).takePrimary();
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isCapabilityRejected());
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable maps a pass-2 read fault to a rejection and cleans up") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  CleanupAwareOutcome<VerifiedLinkedExecutable> outcome =
      linkExecutable(plan, *fs, PrepareInjectedFault::Pass2Read);
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<VerifiedLinkedExecutable> result = zc::mv(outcome).takePrimary();
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isCapabilityRejected());
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST(
    "linkExecutable reports RecoveryRequired when cleanup fails, preserving a verified primary") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // The link itself succeeds (verified primary), but post-spawn cleanup is forced
  // to fail. The outcome must be RecoveryRequired - not a clean success - and the
  // verified primary must be preserved losslessly, with a structured obligation
  // carrying the transaction id, the exact captured identity, and the plan id.
  CleanupAwareOutcome<VerifiedLinkedExecutable> outcome =
      linkExecutable(plan, *fs, PrepareInjectedFault::TreeRemoval);
  ZC_ASSERT(outcome.isRecoveryRequired());
  const SnapshotCleanupObligation& obligation = outcome.orphan();
  ZC_EXPECT(obligation.cleanupStage() == CleanupStage::PostSpawnCleanup);
  ZC_EXPECT(obligation.cleanupFailureKind() == CleanupFailureKind::ContentRemovalFailed);
  ZC_EXPECT(obligation.planId() == plan.id());
  ZC_EXPECT(obligation.directoryIdentity() != zc::none);
  // The obligation's tree path is derived from its parent and token and names a
  // ".zomlink-" directory.
  ZC_EXPECT(zc::StringPtr(obligation.treePath()).find(".zomlink-"_zc) != zc::none);
  // The verified primary survives intact.
  IrOperationResult<VerifiedLinkedExecutable> primary = zc::mv(outcome).takePrimary();
  ZC_ASSERT(primary.isVerified());
  zc::ArrayPtr<const uint8_t> bytes = primary.verifiedValue().bytes();
  ZC_ASSERT(bytes.size() == 4u);
  ZC_EXPECT(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');

  // The forced-failure left the tree behind; remove the whole base for hygiene.
  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("PreparedLinkInputs::prepare reports RecoveryRequired when rollback cleanup fails") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Swap the object inode so prepare rejects, AND force the rollback cleanup to
  // fail: the prepare outcome must be RecoveryRequired at the PrepareRollback
  // stage with the rejection primary preserved.
  dir->remove(zc::Path("app.o"_zc));
  writeFile(*dir, "app.o"_zc, bytesOf("SWAPPED-OBJECT-BYTES-DIFFERENT"_zc).asPtr(), false);

  CleanupAwareOutcome<PreparedLinkInputs> outcome =
      PreparedLinkInputs::prepare(plan, *fs, PrepareInjectedFault::TreeRemoval);
  ZC_ASSERT(outcome.isRecoveryRequired());
  ZC_EXPECT(outcome.orphan().cleanupStage() == CleanupStage::PrepareRollback);
  ZC_EXPECT(!outcome.primary().isVerified());

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable cleanup does not delete a competitor tree that replaced the path") {
  // Model a competitor that, before cleanup's identity check, replaces the
  // top-level path with a DIFFERENT directory (a new inode). The exact-identity
  // re-check must refuse to remove it, reporting IdentityMismatch, and the
  // competitor's directory must survive. This is the stale/replaced-path
  // fail-closed guarantee, not a proof against an active same-instant race.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  CleanupAwareOutcome<PreparedLinkInputs> preparedOutcome = PreparedLinkInputs::prepare(plan, *fs);
  ZC_ASSERT(preparedOutcome.isComplete());
  IrOperationResult<PreparedLinkInputs> preparedResult = zc::mv(preparedOutcome).takePrimary();
  ZC_ASSERT(preparedResult.isVerified());
  PreparedLinkInputs prepared = zc::mv(preparedResult).takeVerified();

  // Find the created transaction directory, then replace it with a competitor's
  // directory of a distinct inode (remove the original tree contents + dir, then
  // create a fresh dir with a sentinel file at the same name).
  zc::String treeName;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { treeName = zc::heapString(name); }
  }
  ZC_ASSERT(treeName.size() > 0);
  dir->remove(zc::Path(treeName));  // remove the original inode
  auto competitor =
      dir->openSubdir(zc::Path(treeName), zc::WriteMode::CREATE | zc::WriteMode::MODIFY);
  competitor->openFile(zc::Path("competitor-owned"_zc), zc::WriteMode::CREATE)->writeAll("x"_zc);

  // finishAndCleanup must refuse to delete the competitor (identity mismatch).
  auto primary = IrOperationResult<VerifiedLinkedExecutable>::verified(
      VerifiedLinkedExecutable(zc::heapArray<uint8_t>({0x7f})));
  CleanupAwareOutcome<VerifiedLinkedExecutable> outcome =
      zc::mv(prepared).finishAndCleanup(zc::mv(primary));
  ZC_ASSERT(outcome.isRecoveryRequired());
  ZC_EXPECT(outcome.orphan().cleanupFailureKind() == CleanupFailureKind::IdentityMismatch);
  // The competitor's directory and its sentinel file are untouched.
  ZC_EXPECT(dir->openSubdir(zc::Path(treeName))->exists(zc::Path("competitor-owned"_zc)));

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("two concurrent prepares of the same plan use distinct trees and do not interfere") {
  // Two overlapping transactions for the same plan must each get an unpredictable,
  // distinct private tree (no shared deterministic name, no pre-delete of the
  // other), and finishing one must not remove the other's tree.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  CleanupAwareOutcome<PreparedLinkInputs> firstOutcome = PreparedLinkInputs::prepare(plan, *fs);
  ZC_ASSERT(firstOutcome.isComplete());
  CleanupAwareOutcome<PreparedLinkInputs> secondOutcome = PreparedLinkInputs::prepare(plan, *fs);
  ZC_ASSERT(secondOutcome.isComplete());

  IrOperationResult<PreparedLinkInputs> firstResult = zc::mv(firstOutcome).takePrimary();
  IrOperationResult<PreparedLinkInputs> secondResult = zc::mv(secondOutcome).takePrimary();
  ZC_ASSERT(firstResult.isVerified());
  ZC_ASSERT(secondResult.isVerified());
  // Both trees coexist: two distinct private directories.
  ZC_EXPECT(countSnapshotTrees(*dir) == 2u);

  PreparedLinkInputs first = zc::mv(firstResult).takeVerified();
  PreparedLinkInputs second = zc::mv(secondResult).takeVerified();
  // The two programs (driver snapshot paths) are in different trees.
  ZC_EXPECT(first.program() != second.program());

  // Finishing the first removes only its own tree; the second's survives.
  auto primaryA = IrOperationResult<VerifiedLinkedExecutable>::verified(
      VerifiedLinkedExecutable(zc::heapArray<uint8_t>({0x7f})));
  CleanupAwareOutcome<VerifiedLinkedExecutable> firstFinish =
      zc::mv(first).finishAndCleanup(zc::mv(primaryA));
  ZC_ASSERT(firstFinish.isComplete());
  ZC_EXPECT(countSnapshotTrees(*dir) == 1u);

  // Finishing the second removes the last tree.
  auto primaryB = IrOperationResult<VerifiedLinkedExecutable>::verified(
      VerifiedLinkedExecutable(zc::heapArray<uint8_t>({0x7f})));
  CleanupAwareOutcome<VerifiedLinkedExecutable> secondFinish =
      zc::mv(second).finishAndCleanup(zc::mv(primaryB));
  ZC_ASSERT(secondFinish.isComplete());
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

#endif  // defined(ZOM_FAKE_LINKER_SUCCESS)

ZC_TEST("linkExecutable fails closed on a filesystem exposing no real descriptors") {
  // An in-memory filesystem's root exposes no real descriptor, so the exact
  // owner-identity capture and the exec-by-descriptor both fail closed. The
  // prepare step must reject before creating any tree, never falling back to a
  // pathname exec.
  auto memRoot = zc::newInMemoryDirectory(zc::nullClock());
  auto driverBytes = bytesOf("ELF-LINKER-DRIVER-BYTES"_zc);
  auto objectBytes = bytesOf("USER-OBJECT-BYTES"_zc);
  memRoot
      ->openFile(zc::Path::parse("mem/ld"_zc),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT | zc::WriteMode::EXECUTABLE)
      ->writeAll(driverBytes.asPtr());
  memRoot
      ->openFile(zc::Path::parse("mem/app.o"_zc),
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

  InMemoryFilesystem memFs(zc::mv(memRoot));
  CleanupAwareOutcome<VerifiedLinkedExecutable> outcome = linkExecutable(plan, memFs);
  // No tree was created (fail-closed before creation), so the outcome is Complete
  // with a capability rejection, not RecoveryRequired.
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<VerifiedLinkedExecutable> result = zc::mv(outcome).takePrimary();
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isCapabilityRejected());
  // No snapshot tree survives the fail-closed path.
  bool foundSnapshot = false;
  for (const zc::String& name : memFs.getRoot().listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { foundSnapshot = true; }
  }
  ZC_EXPECT(!foundSnapshot);
}

}  // namespace
}  // namespace zomlang::compiler::ir
