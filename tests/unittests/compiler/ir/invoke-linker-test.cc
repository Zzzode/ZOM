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
// snapshot paths, and (RFC 0043 D4) writes the linker output into the unified
// transaction root at `<root>/output-candidate`, transferring the still-live root
// into a move-only `LinkedOutputCandidate` on success. This defeats the input
// side of the link TOCTOU: an inode swapped after verification but before exec is
// never linked. The driver is a real compiled ELF (ZOM_FAKE_LINKER_SUCCESS/
// PARTIAL/NO_OUTPUT/SYMLINK/EMPTY/DIRECTORY/HARDLINK, one variant per compile-time
// behavior, the last four exercising the D4 output structural invariants); a `#!`
// script cannot be exec'd by descriptor. On non-Linux hosts the descriptor-exec
// path does not exist and the integration cases are skipped, but the in-memory
// fail-closed case still runs.
//
// Fault injection is applied through a transparent, delegating `Filesystem`
// wrapper (below): it forwards every filesystem call to a real disk filesystem
// but can be armed to make one real `File::write`, `ReadableFile::read`, or
// `Directory::tryRemove` CALL throw, distinguishing content-stage from top-
// level-stage removes by capability. The fault fires at the start of the real
// virtual call (a whole-call fault, not a torn/partial write or a short read);
// it exercises the production exception seam without simulating partial I/O.
// There is no fault parameter on any production signature; the wrapper is the
// ordinary `filesystem` argument. The transaction-token collision retry is
// exercised through a scriptable `SnapshotTokenSource` reachable only through
// the internal `LinkerInvocationTestAccess` (compiled into this test target and
// the implementation file, never a production translation unit).

#include "compiler/ir/invoke-linker.h"
//
#include <unistd.h>
#if defined(ZOM_FAKE_LINKER_SUCCESS)
#include <sys/stat.h>  // fstat/chmod, used only by the Linux fixture-driven cases
#endif

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/invoke-linker-internal.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/debug.h"
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

// =======================================================================================
// Fault-injecting filesystem wrapper
//
// A shared, mutable fault script. It is armed with exactly one fault kind and a
// zero-based "skip count": the fault fires on the (skip+1)-th matching CALL and
// then disarms, so a test can, for example, fail the write-call of the 2nd
// snapshot file. Every fault is a whole-call fault: it throws at the start of
// the real virtual call, before any bytes are written or read - it exercises the
// production exception seam, not a torn write or a short read.
//
// Tree membership is tracked by capability, not by inspecting a bare relative
// path: the root wrapper is "outside" the tree; a subdir opened under a name
// starting with ".zomlink-" is "inside", and every file or subdir opened from an
// inside directory is inside too. This matters because the production code opens
// snapshot files and removes snapshot contents THROUGH the held snapshot-dir
// capability with bare leaf paths ("driver"), which carry no ".zomlink-"
// component of their own. With the flag propagated at open time:
//   - Write faults hit only write-calls into the snapshot tree.
//   - Read faults hit only read-calls of snapshot files (the pass-2 re-verify
//     reads), never the phase-1 source reads.
//   - ContentRemove faults hit only remove-calls issued through an inside
//     directory (content stage); TopLevelRemove faults hit only the parent
//     removing a ".zomlink-" entry (top-level stage).

enum class FaultKind {
  None,
  Write,           // Fail a File::write-call into the snapshot tree.
  Read,            // Fail a ReadableFile::read-call of a snapshot file (pass-2 re-verify).
  ContentRemove,   // Fail a tryRemove-call issued through an inside (snapshot) directory.
  TopLevelRemove,  // Fail a parent's tryRemove-call of a ".zomlink-" top-level directory.
};

struct FaultScript {
  FaultKind kind = FaultKind::None;
  size_t skip = 0;     // Number of matching calls to let pass before firing.
  bool fired = false;  // Set once the fault has fired (single-shot).
  // When true, an inside-tree (snapshot) directory reports no descriptor, so the
  // production exact-identity capture yields none. This models a filesystem that
  // cannot supply a stable (dev,ino) for the snapshot directory, exercising the
  // IdentityUnavailable path without faulting any I/O call.
  bool suppressInsideTreeFd = false;

  // Returns true (and disarms) when a call matching `candidate` should fault.
  bool shouldFire(FaultKind candidate) {
    if (kind != candidate || fired) { return false; }
    if (skip > 0) {
      --skip;
      return false;
    }
    fired = true;
    return true;
  }
};

// True when any component of `path` names a ".zomlink-" transaction directory.
bool pathHasSnapshotComponent(zc::PathPtr path) {
  for (size_t i = 0; i < path.size(); ++i) {
    if (zc::StringPtr(path[i]).startsWith(".zomlink-"_zc)) { return true; }
  }
  return false;
}

// True when `path`'s last component is a ".zomlink-" directory (the top-level
// transaction entry a parent directory removes).
bool isSnapshotTopLevel(zc::PathPtr path) {
  return path.size() >= 1 && zc::StringPtr(path[path.size() - 1]).startsWith(".zomlink-"_zc);
}

class FaultFile final : public zc::File {
public:
  FaultFile(zc::Own<const zc::File>&& inner, FaultScript& script, bool insideTree)
      : inner(zc::mv(inner)), script(script), insideTree(insideTree) {}

  // A file always reports its real descriptor: the driver snapshot's fd is the
  // exec target, and identity suppression applies only to the snapshot directory.
  zc::Maybe<int> getFd() const override { return inner->getFd(); }
  Metadata stat() const override { return inner->stat(); }
  void sync() const override { inner->sync(); }
  void datasync() const override { inner->datasync(); }
  size_t read(uint64_t offset, zc::ArrayPtr<zc::byte> buffer) const override {
    if (insideTree && script.shouldFire(FaultKind::Read)) {
      ZC_FAIL_REQUIRE("injected pass-2 read-call fault");
    }
    return inner->read(offset, buffer);
  }
  zc::Array<const zc::byte> mmap(uint64_t offset, uint64_t size) const override {
    return inner->mmap(offset, size);
  }
  zc::Array<zc::byte> mmapPrivate(uint64_t offset, uint64_t size) const override {
    return inner->mmapPrivate(offset, size);
  }
  void write(uint64_t offset, zc::ArrayPtr<const zc::byte> data) const override {
    if (insideTree && script.shouldFire(FaultKind::Write)) {
      ZC_FAIL_REQUIRE("injected snapshot write-call fault");
    }
    inner->write(offset, data);
  }
  void zero(uint64_t offset, uint64_t size) const override { inner->zero(offset, size); }
  void truncate(uint64_t size) const override { inner->truncate(size); }
  zc::Own<const zc::WritableFileMapping> mmapWritable(uint64_t offset,
                                                      uint64_t size) const override {
    return inner->mmapWritable(offset, size);
  }

protected:
  zc::Own<const FsNode> cloneFsNode() const override {
    return zc::heap<FaultFile>(inner->clone(), script, insideTree);
  }

private:
  zc::Own<const zc::File> inner;
  FaultScript& script;
  bool insideTree;
};

class FaultReadableFile final : public zc::ReadableFile {
public:
  FaultReadableFile(zc::Own<const zc::ReadableFile>&& inner, FaultScript& script, bool insideTree)
      : inner(zc::mv(inner)), script(script), insideTree(insideTree) {}

  zc::Maybe<int> getFd() const override { return inner->getFd(); }
  Metadata stat() const override { return inner->stat(); }
  void sync() const override { inner->sync(); }
  void datasync() const override { inner->datasync(); }
  size_t read(uint64_t offset, zc::ArrayPtr<zc::byte> buffer) const override {
    if (insideTree && script.shouldFire(FaultKind::Read)) {
      ZC_FAIL_REQUIRE("injected pass-2 read-call fault");
    }
    return inner->read(offset, buffer);
  }
  zc::Array<const zc::byte> mmap(uint64_t offset, uint64_t size) const override {
    return inner->mmap(offset, size);
  }
  zc::Array<zc::byte> mmapPrivate(uint64_t offset, uint64_t size) const override {
    return inner->mmapPrivate(offset, size);
  }

protected:
  zc::Own<const FsNode> cloneFsNode() const override {
    return zc::heap<FaultReadableFile>(inner->clone(), script, insideTree);
  }

private:
  zc::Own<const zc::ReadableFile> inner;
  FaultScript& script;
  bool insideTree;
};

class FaultDirectory final : public zc::Directory {
public:
  FaultDirectory(zc::Own<const zc::Directory>&& inner, FaultScript& script, bool insideTree)
      : inner(zc::mv(inner)), script(script), insideTree(insideTree) {}

  zc::Maybe<int> getFd() const override {
    // Model a filesystem that cannot supply a stable descriptor for the snapshot
    // directory, so the production identity capture yields none.
    if (insideTree && script.suppressInsideTreeFd) { return zc::none; }
    return inner->getFd();
  }
  Metadata stat() const override { return inner->stat(); }
  void sync() const override { inner->sync(); }
  void datasync() const override { inner->datasync(); }
  zc::Array<zc::String> listNames() const override { return inner->listNames(); }
  zc::Array<Entry> listEntries() const override { return inner->listEntries(); }
  bool exists(zc::PathPtr path) const override { return inner->exists(path); }
  zc::Maybe<FsNode::Metadata> tryLstat(zc::PathPtr path) const override {
    return inner->tryLstat(path);
  }

  zc::Maybe<zc::Own<const zc::ReadableFile>> tryOpenFile(zc::PathPtr path) const override {
    ZC_IF_SOME(file, inner->tryOpenFile(path)) {
      return zc::Own<const zc::ReadableFile>(
          zc::heap<FaultReadableFile>(zc::mv(file), script, childInside(path)));
    }
    return zc::none;
  }
  zc::Maybe<zc::Own<const zc::ReadableDirectory>> tryOpenSubdir(zc::PathPtr path) const override {
    ZC_IF_SOME(dir, inner->tryOpenSubdir(path)) {
      return zc::Own<const zc::ReadableDirectory>(
          zc::heap<FaultDirectory>(dir.downcast<const zc::Directory>(), script, childInside(path)));
    }
    return zc::none;
  }
  zc::Maybe<zc::String> tryReadlink(zc::PathPtr path) const override {
    return inner->tryReadlink(path);
  }

  zc::Maybe<zc::Own<const zc::File>> tryOpenFile(zc::PathPtr path,
                                                 zc::WriteMode mode) const override {
    ZC_IF_SOME(file, inner->tryOpenFile(path, mode)) {
      return zc::Own<const zc::File>(zc::heap<FaultFile>(zc::mv(file), script, childInside(path)));
    }
    return zc::none;
  }
  zc::Own<Replacer<zc::File>> replaceFile(zc::PathPtr path, zc::WriteMode mode) const override {
    return inner->replaceFile(path, mode);
  }
  zc::Own<const zc::File> createTemporary() const override { return inner->createTemporary(); }
  zc::Maybe<zc::Own<zc::AppendableFile>> tryAppendFile(zc::PathPtr path,
                                                       zc::WriteMode mode) const override {
    return inner->tryAppendFile(path, mode);
  }
  zc::Maybe<zc::Own<const zc::Directory>> tryOpenSubdir(zc::PathPtr path,
                                                        zc::WriteMode mode) const override {
    ZC_IF_SOME(dir, inner->tryOpenSubdir(path, mode)) {
      return zc::Own<const zc::Directory>(
          zc::heap<FaultDirectory>(zc::mv(dir), script, childInside(path)));
    }
    return zc::none;
  }
  zc::Own<Replacer<zc::Directory>> replaceSubdir(zc::PathPtr path,
                                                 zc::WriteMode mode) const override {
    return inner->replaceSubdir(path, mode);
  }
  bool trySymlink(zc::PathPtr linkpath, zc::StringPtr content, zc::WriteMode mode) const override {
    return inner->trySymlink(linkpath, content, mode);
  }
  bool tryTransfer(zc::PathPtr toPath, zc::WriteMode toMode, const zc::Directory& fromDirectory,
                   zc::PathPtr fromPath, zc::TransferMode mode) const override {
    return inner->tryTransfer(toPath, toMode, fromDirectory, fromPath, mode);
  }
  bool tryRemove(zc::PathPtr path) const override {
    // A remove issued through an inside (snapshot) directory is content-stage.
    if (insideTree && script.shouldFire(FaultKind::ContentRemove)) {
      ZC_FAIL_REQUIRE("injected content remove-call fault");
    }
    // A parent removing a ".zomlink-" entry is the top-level stage.
    if (!insideTree && isSnapshotTopLevel(path) && script.shouldFire(FaultKind::TopLevelRemove)) {
      ZC_FAIL_REQUIRE("injected top-level remove-call fault");
    }
    return inner->tryRemove(path);
  }

protected:
  zc::Own<const FsNode> cloneFsNode() const override {
    return zc::heap<FaultDirectory>(inner->clone(), script, insideTree);
  }

private:
  // A child opened at `path` is inside the tree if this directory already is, or
  // if the open path itself descends through a ".zomlink-" component.
  bool childInside(zc::PathPtr path) const { return insideTree || pathHasSnapshotComponent(path); }

  zc::Own<const zc::Directory> inner;
  FaultScript& script;
  bool insideTree;
};

// A Filesystem whose root is a FaultDirectory over a real disk root. The fault
// script is shared by reference with every node the wrapper mints (files, opened
// subdirs, and their clones), so a fault armed before the operation fires inside
// the real call the production code makes. The root is outside any tree. It owns
// its own disk filesystem handle, so a test can keep a separate handle to the
// same real disk for setup and cleanup without a use-after-move.
class FaultFilesystem final : public zc::Filesystem {
public:
  explicit FaultFilesystem(FaultScript& script)
      : disk(zc::newDiskFilesystem()),
        rootWrapper(zc::heap<FaultDirectory>(disk->getRoot().clone(), script,
                                             /*insideTree=*/false)) {}
  const zc::Directory& getRoot() const override { return *rootWrapper; }
  const zc::Directory& getCurrent() const override { return *rootWrapper; }
  zc::PathPtr getCurrentPath() const override { return zc::PathPtr(nullptr); }

private:
  zc::Own<zc::Filesystem> disk;
  zc::Own<const FaultDirectory> rootWrapper;
};

// A scriptable transaction-token source: returns a fixed sequence of 16-byte
// tokens, then falls back to a distinct deterministic counter so it never
// repeats or runs out. Used to force an exclusive-create collision (return the
// same token twice, then a fresh one) and prove the retry.
class ScriptedTokenSource final : public SnapshotTokenSource {
public:
  explicit ScriptedTokenSource(zc::Array<zc::Array<uint8_t>>&& scripted)
      : scripted(zc::mv(scripted)) {}
  ~ScriptedTokenSource() noexcept override = default;

  bool nextToken(uint8_t (&out)[16]) override {
    if (cursor < scripted.size()) {
      ZC_REQUIRE(scripted[cursor].size() == 16);
      for (size_t i = 0; i < 16; ++i) { out[i] = scripted[cursor][i]; }
      ++cursor;
      return true;
    }
    // Deterministic non-repeating fallback beyond the scripted prefix.
    for (size_t i = 0; i < 16; ++i) { out[i] = static_cast<uint8_t>(0xA0 + i); }
    out[0] = static_cast<uint8_t>(0xA0 + fallback);
    ++fallback;
    return true;
  }

private:
  zc::Array<zc::Array<uint8_t>> scripted;
  size_t cursor = 0;
  uint8_t fallback = 0;
};

zc::Array<uint8_t> token16(uint8_t fill) {
  auto bytes = zc::heapArray<uint8_t>(16);
  for (size_t i = 0; i < 16; ++i) { bytes[i] = fill; }
  return bytes;
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

// Counts the ".zomlink-" transaction directories directly under `dir`.
size_t countSnapshotTrees(const zc::Directory& dir) {
  size_t count = 0;
  for (const zc::String& name : dir.listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { ++count; }
  }
  return count;
}

// The sole ".zomlink-" transaction directory name directly under `dir`. Requires
// exactly one to exist (the live transaction root of a successful link).
zc::String soleSnapshotTreeName(const zc::Directory& dir) {
  zc::String found;
  size_t count = 0;
  for (const zc::String& name : dir.listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) {
      found = zc::heapString(name);
      ++count;
    }
  }
  ZC_REQUIRE(count == 1);
  return found;
}

// Links a plan expecting a Complete outcome carrying a verified candidate,
// returning the still-live candidate (RFC 0043 D4: success transfers the
// transaction root into the candidate rather than cleaning it, so the caller
// must observe through the candidate and then `discardAndCleanup()`).
LinkedOutputCandidate linkExpectingCandidate(VerifiedLinkPlan&& plan, const zc::Filesystem& fs) {
  CleanupAwareOutcome<LinkedOutputCandidate> outcome = linkExecutable(zc::mv(plan), fs);
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<LinkedOutputCandidate> result = zc::mv(outcome).takeComplete();
  ZC_ASSERT(result.isVerified());
  return zc::mv(result).takeVerified();
}

// Counts spawn attempts. RFC 0043 D4: the fixture's ".started"/".args" markers now
// live INSIDE the transaction root and are removed with it on a reject, so their
// absence at the final path no longer proves "never spawned" (they never exist
// there, and even in-tree markers are cleaned). The per-call internal
// SpawnAttemptObserver is the drift-proof spawn evidence: it is notified once, at
// the very last point before the child is launched, so count()==0 proves a
// rejection fired before any spawn attempt and count()==1 proves a spawn was
// attempted. It observes only "a spawn was attempted", not that an OS child ran.
class CountingSpawnObserver final : public SpawnAttemptObserver {
public:
  void onSpawnAttempt() noexcept override { ++countValue; }
  size_t count() const { return countValue; }

private:
  size_t countValue = 0;
};

// Links a plan through the internal observed entry point, expecting a Complete
// rejection AND that NO spawn was attempted (the rejection fired at or before a
// D4 pre-spawn gate). Returns the rejection result.
IrOperationResult<LinkedOutputCandidate> linkExpectingRejectNoSpawn(VerifiedLinkPlan&& plan,
                                                                    const zc::Filesystem& fs) {
  CountingSpawnObserver observer;
  CleanupAwareOutcome<LinkedOutputCandidate> outcome =
      LinkerInvocationTestAccess::linkExecutableObserved(zc::mv(plan), fs, observer);
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<LinkedOutputCandidate> result = zc::mv(outcome).takeComplete();
  ZC_EXPECT(!result.isVerified());
  // The core assertion: no spawn was ever attempted.
  ZC_EXPECT(observer.count() == 0u);
  return result;
}

// Links a plan through the internal observed entry point, expecting a Complete
// rejection AFTER a spawn was attempted (the driver ran and its output failed a
// D4 post-spawn gate). Returns the rejection result.
IrOperationResult<LinkedOutputCandidate> linkExpectingRejectAfterSpawn(VerifiedLinkPlan&& plan,
                                                                       const zc::Filesystem& fs) {
  CountingSpawnObserver observer;
  CleanupAwareOutcome<LinkedOutputCandidate> outcome =
      LinkerInvocationTestAccess::linkExecutableObserved(zc::mv(plan), fs, observer);
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<LinkedOutputCandidate> result = zc::mv(outcome).takeComplete();
  ZC_EXPECT(!result.isVerified());
  // A spawn was attempted exactly once before the post-spawn failure.
  ZC_EXPECT(observer.count() == 1u);
  return result;
}

// The exact (dev, ino) of the subdirectory `name` under `parent`, read through
// its real descriptor. Mirrors the production owner-identity capture, so a test
// can prove a tree's identity is unchanged (not merely its byte content). Only
// used by the Linux fixture-driven cases below, and defined there so a non-Linux
// build (where the fixture block is disabled) neither pulls in <sys/stat.h> nor
// leaves an unused fstat helper.

#if defined(ZOM_FAKE_LINKER_SUCCESS)

struct DirIdentity {
  uint64_t dev;
  uint64_t ino;
  bool operator==(const DirIdentity& other) const { return dev == other.dev && ino == other.ino; }
};
DirIdentity subdirIdentity(const zc::Directory& parent, zc::StringPtr name) {
  auto sub = parent.openSubdir(zc::Path(zc::heapString(name)));
  int fd = ZC_REQUIRE_NONNULL(sub->getFd());
  struct stat st;
  ZC_REQUIRE(::fstat(fd, &st) == 0);
  return DirIdentity{static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino)};
}

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
//
// The input set covers every role with more than one member where the role
// admits several (two objects, two runtime objects), so a test can lock both the
// cross-role order and the within-role canonical index order. Each field also
// records the snapshot name the production code derives for it (role prefix +
// canonical index), so a test can rebuild the exact expected argv.
struct Scenario {
  zc::Array<zc::byte> driverBytes;
  zc::Array<zc::byte> crtBytes;
  zc::Array<zc::byte> object0Bytes;
  zc::Array<zc::byte> object1Bytes;
  zc::Array<zc::byte> runtime0Bytes;
  zc::Array<zc::byte> runtime1Bytes;
  zc::Array<zc::byte> libBytes;
};

// Materializes driver + inputs under `dir` and builds the verified plan for them.
// Roles laid down so the RFC 0043 canonical argv order is non-trivial:
//   driver, -o out, -e entry, crt-0, obj-0, obj-1, rt-0, rt-1, lib-0.
VerifiedLinkPlan buildScenario(const zc::Directory& dir, zc::StringPtr base,
                               zc::StringPtr driverFixturePath, Scenario& out) {
  out.driverBytes = writeFile(dir, "ld"_zc, fixtureBytes(driverFixturePath).asPtr(),
                              /*executable=*/true);
  out.crtBytes = writeFile(dir, "crt1.o"_zc, bytesOf("CRT-OBJECT-BYTES"_zc).asPtr(), false);
  out.object0Bytes = writeFile(dir, "app0.o"_zc, bytesOf("USER-OBJECT-ZERO"_zc).asPtr(), false);
  out.object1Bytes = writeFile(dir, "app1.o"_zc, bytesOf("USER-OBJECT-ONE-"_zc).asPtr(), false);
  out.runtime0Bytes = writeFile(dir, "rt0.o"_zc, bytesOf("RUNTIME-OBJ-ZERO"_zc).asPtr(), false);
  out.runtime1Bytes = writeFile(dir, "rt1.o"_zc, bytesOf("RUNTIME-OBJ-ONE-"_zc).asPtr(), false);
  out.libBytes = writeFile(dir, "libc.a"_zc, bytesOf("LIBC-ARCHIVE-BYTES"_zc).asPtr(), false);

  const uint8_t targetIdentity[] = {0x74, 0x67, 0x74};  // "tgt"
  auto driverPath = zc::str(base, "/ld");
  auto crtPath = zc::str(base, "/crt1.o");
  auto object0Path = zc::str(base, "/app0.o");
  auto object1Path = zc::str(base, "/app1.o");
  auto runtime0Path = zc::str(base, "/rt0.o");
  auto runtime1Path = zc::str(base, "/rt1.o");
  auto libPath = zc::str(base, "/libc.a");

  auto closure = ToolchainClosureRecord::make(
      zc::arrayPtr(targetIdentity, 3), base, LinkerDriverKind::ElfDriver, driverPath,
      digestOf(out.driverBytes.asPtr()), out.driverBytes.size(),
      oneInput(recordFor(crtPath, LinkInputRole::CrtObject, out.crtBytes.asPtr())),
      oneInput(recordFor(libPath, LinkInputRole::DefaultLibrary, out.libBytes.asPtr())));
  ZC_REQUIRE(closure != zc::none);

  // Feed the object and runtime records in REVERSE canonical order (app1 before
  // app0, rt1 before rt0). A passing canonical-order assertion on the verified
  // plan then proves the verifier sorted them, not that the test preserved input
  // order.
  auto twoObjects = zc::heapArrayBuilder<LinkInputRecord>(2);
  twoObjects.add(recordFor(object1Path, LinkInputRole::ObjectArtifact, out.object1Bytes.asPtr()));
  twoObjects.add(recordFor(object0Path, LinkInputRole::ObjectArtifact, out.object0Bytes.asPtr()));
  auto twoRuntimes = zc::heapArrayBuilder<LinkInputRecord>(2);
  twoRuntimes.add(recordFor(runtime1Path, LinkInputRole::RuntimeObject, out.runtime1Bytes.asPtr()));
  twoRuntimes.add(recordFor(runtime0Path, LinkInputRole::RuntimeObject, out.runtime0Bytes.asPtr()));

  ExecutableLinkRequest request{ZC_REQUIRE_NONNULL(zc::mv(closure)),
                                zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}),  // "zom"
                                twoObjects.finish(),
                                twoRuntimes.finish(),
                                zc::str(base),
                                zc::str(base, "/app")};
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

// Asserts a single failure fact is the complete RFC 0010 / RFC 0043 row for a
// LinkerInvocation rejection: not just the branch, but phase, kind, the
// Backend { InvokeLinker, instance none } site, a Session owner, and a None
// detail. `fact` is the one fact carried by the rejection branch.
void expectLinkerInvocationRow(const IrFailureFact& fact, IrRejectedBranch branch,
                               IrFailureKind kind) {
  ZC_EXPECT(fact.branch() == branch);
  ZC_EXPECT(fact.phase() == IrFailurePhase::LinkerInvocation);
  ZC_EXPECT(fact.kind() == kind);
  // Owner is the session (LinkerInvocation is session-owned, no module/def).
  ZC_EXPECT(fact.owner().kind() == IrFailureOwnerKind::Session);
  ZC_EXPECT(fact.owner().sessionContext() != zc::none);
  // Site is Backend { InvokeLinker }, with no instance under the current contract.
  ZC_IF_SOME(site, fact.site()) {
    ZC_EXPECT(site.kind() == IrFailureSiteKind::Backend);
    ZC_EXPECT(site.backendValue().operation == BackendOperation::InvokeLinker);
    ZC_EXPECT(site.backendValue().instance == zc::none);
  } else {
    ZC_FAIL_EXPECT("linker invocation failure must carry a Backend site");
  }
  // Detail is None (no instantiation cycle/budget payload for a link failure).
  ZC_EXPECT(fact.detail().kind() == IrFailureDetailKind::None);
}

// Extracts the single fact from a rejection, asserting exactly one is present.
// Works for both the IrInvariantRejected and CapabilityRejected branches.
const IrFailureFact& soleFailureFact(const IrOperationResult<LinkedOutputCandidate>& result) {
  if (result.isCapabilityRejected()) {
    zc::ArrayPtr<const IrFailureFact> facts = result.capabilityFailures().facts();
    ZC_REQUIRE(facts.size() == 1);
    return facts[0];
  }
  ZC_REQUIRE(result.isIrInvariantRejected());
  zc::ArrayPtr<const IrFailureFact> facts = result.invariantFailures().facts();
  ZC_REQUIRE(facts.size() == 1);
  return facts[0];
}

ZC_TEST(
    "linkExecutable snapshots inputs, execs the driver by fd, and captures the output candidate") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);

  // RFC 0043 D4: on success the transaction root is NOT cleaned - its ownership is
  // transferred into the candidate, so exactly one ".zomlink-" tree is still live.
  ZC_ASSERT(countSnapshotTrees(*dir) == 1u);
  zc::String treeName = soleSnapshotTreeName(*dir);
  auto tree = dir->openSubdir(zc::Path(zc::heapString(treeName)));

  // The candidate reads back the produced executable bytes through its own held
  // handle (not a pathname re-open); the fake success driver wrote ELF magic.
  zc::Array<uint8_t> bytes = candidate.readOutput();
  ZC_ASSERT(bytes.size() == 4u);
  ZC_EXPECT(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');
  // The candidate's captured size matches the bytes it holds, and its exact
  // identity records a sole hard link (st_nlink == 1).
  ZC_EXPECT(candidate.outputSize() == 4u);
  ZC_EXPECT(candidate.outputIdentity().linkCount() == 1u);
  // The candidate's digest was computed from the same held handle: it equals the
  // SHA-256 of exactly the bytes readOutput() returns, and of the known ELF magic.
  ZC_EXPECT(candidate.outputDigest() == digestOf(bytes.asPtr()));
  ZC_EXPECT(candidate.outputDigest() == digestOf(candidate.readOutput().asPtr()));
  // The output candidate path names <tree>/output-candidate, never the final path.
  ZC_EXPECT(zc::StringPtr(candidate.outputCandidatePath()).endsWith("/output-candidate"_zc));
  ZC_EXPECT(zc::StringPtr(candidate.outputCandidatePath()).find(".zomlink-"_zc) != zc::none);
  // The plan the candidate now owns still names the final output path.
  ZC_EXPECT(zc::StringPtr(candidate.plan().outputPath()).endsWith("/app"_zc));

  // The driver wrote its output and the ".args"/".started" markers as siblings of
  // <tree>/output-candidate INSIDE the transaction root, never at the final path.
  ZC_EXPECT(tree->exists(zc::Path("output-candidate"_zc)));
  ZC_EXPECT(tree->exists(zc::Path("output-candidate.started"_zc)));
  auto argsText = tree->openFile(zc::Path("output-candidate.args"_zc))->readAllText();
  // Every input token names a snapshot path (inside the private tree), NOT the
  // original source path, and the recorded size matches the input.
  ZC_EXPECT(argsText.find(".zomlink-"_zc) != zc::none);
  ZC_EXPECT(argsText.find(zc::str(base, "/app0.o")) == zc::none);
  ZC_EXPECT(argsText.find(zc::str(base, "/crt1.o")) == zc::none);
  ZC_EXPECT(argsText.find(zc::str(base, "/libc.a")) == zc::none);
  ZC_EXPECT(argsText.find(zc::str("size=", scenario.object0Bytes.size())) != zc::none);

  // The final path was never written - the driver only wrote into the tree.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));

  // Observation done: discard the transaction root through the explicit seam
  // (never a bare directory delete). It reports Clean and removes the tree.
  CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
  ZC_EXPECT(disposition.isClean());
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

// Returns the substring of every line in `text` that begins with `prefix`, in
// order, with the prefix stripped. Used to parse the fixture's "<out>.args" dump.
zc::Array<zc::String> linesWithPrefix(zc::StringPtr text, zc::StringPtr prefix) {
  zc::Vector<zc::String> out;
  const char* data = text.begin();
  size_t n = text.size();
  size_t pos = 0;
  auto matchesAt = [&](size_t at, zc::StringPtr needle) -> bool {
    if (at + needle.size() > n) { return false; }
    for (size_t i = 0; i < needle.size(); ++i) {
      if (data[at + i] != needle.begin()[i]) { return false; }
    }
    return true;
  };
  while (pos < n) {
    size_t lineEnd = pos;
    while (lineEnd < n && data[lineEnd] != '\n') { ++lineEnd; }
    if (matchesAt(pos, prefix)) {
      size_t start = pos + prefix.size();
      out.add(zc::heapString(data + start, lineEnd - start));
    }
    pos = lineEnd + 1;
  }
  return out.releaseAsArray();
}

// The full raw argv the fixture recorded, one per "arg[<i>]=<token>" line, in
// index order. The fixture writes them in ascending index order, so the returned
// order is argv order.
zc::Array<zc::String> recordedArgv(zc::StringPtr argsText) {
  return linesWithPrefix(argsText, "arg["_zc);  // yields "<i>]=<token>"
}

// The single integer value of the fixture's "argc=<n>" line.
size_t recordedArgc(zc::StringPtr argsText) {
  zc::Array<zc::String> lines = linesWithPrefix(argsText, "argc="_zc);
  ZC_REQUIRE(lines.size() == 1);
  size_t value = 0;
  for (char c : lines[0]) {
    ZC_REQUIRE(c >= '0' && c <= '9');
    value = value * 10 + static_cast<size_t>(c - '0');
  }
  return value;
}

// The recorded token for a raw argv index, extracted from its "arg[<i>]=<token>"
// line. `recordedArgv` returns lines shaped "<i>]=<token>"; this strips the
// "<i>]=" head and returns the token for the requested index.
zc::String argvToken(zc::ArrayPtr<const zc::String> argvLines, size_t index) {
  for (const zc::String& line : argvLines) {
    // line is "<i>]=<token>"; parse the leading integer up to ']'.
    size_t bracket = 0;
    zc::StringPtr view = line;
    size_t parsed = 0;
    bool ok = true;
    while (bracket < view.size() && view[bracket] != ']') {
      char c = view[bracket];
      if (c < '0' || c > '9') {
        ok = false;
        break;
      }
      parsed = parsed * 10 + static_cast<size_t>(c - '0');
      ++bracket;
    }
    if (!ok || bracket + 1 >= view.size() || view[bracket] != ']' || view[bracket + 1] != '=') {
      continue;
    }
    if (parsed == index) {
      return zc::heapString(view.begin() + bracket + 2, view.size() - bracket - 2);
    }
  }
  ZC_FAIL_REQUIRE("no recorded argv token for index", index);
}

ZC_TEST("linkExecutable rewrites argv to the exact canonical snapshot order for every role") {
  // Two authorities are locked separately:
  //   1. The verified plan canonicalizes its input records: even when the request
  //      is fed in reverse order, plan.objectRecords()/runtimeRecords() come out
  //      sorted by canonical key (role, then path). This proves the ordering is
  //      the verifier's, not the test's input order.
  //   2. linkExecutable expands that plan into the exact 11-token argv
  //        <tree>/driver -o <tree>/output-candidate -e zom <tree>/crt-0 obj-0 obj-1 rt-0 rt-1 lib-0
  //      which the driver records token by index; the test rebuilds the full
  //      vector and compares every token (argc == 11, full equality). argv[2] is
  //      the transaction-root output candidate, never the final path.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  // buildScenario feeds the object/runtime records to the request in REVERSE
  // canonical order, so a passing canonical-order assertion below proves the
  // verifier sorted them.
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Authority 1: the verified plan's records are in canonical order (by path
  // within a role), regardless of the reversed request order.
  ZC_ASSERT(plan.objectRecords().size() == 2u);
  ZC_EXPECT(zc::StringPtr(plan.objectRecords()[0].path()).endsWith("/app0.o"_zc));
  ZC_EXPECT(zc::StringPtr(plan.objectRecords()[1].path()).endsWith("/app1.o"_zc));
  ZC_ASSERT(plan.runtimeRecords().size() == 2u);
  ZC_EXPECT(zc::StringPtr(plan.runtimeRecords()[0].path()).endsWith("/rt0.o"_zc));
  ZC_EXPECT(zc::StringPtr(plan.runtimeRecords()[1].path()).endsWith("/rt1.o"_zc));

  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  zc::String treeName = soleSnapshotTreeName(*dir);
  auto tree = dir->openSubdir(zc::Path(zc::heapString(treeName)));

  auto argsText = tree->openFile(zc::Path("output-candidate.args"_zc))->readAllText();
  // Authority 2: the recorded argv is exactly 11 tokens.
  ZC_ASSERT(recordedArgc(argsText) == 11u);
  zc::Array<zc::String> argvLines = recordedArgv(argsText);
  ZC_ASSERT(argvLines.size() == 11u);

  // Derive the transaction tree prefix from argv[0] (the driver snapshot path):
  // everything up to and including the last '/'.
  zc::String driverToken = argvToken(argvLines, 0);
  size_t lastSlash = 0;
  for (size_t i = 0; i < driverToken.size(); ++i) {
    if (driverToken[i] == '/') { lastSlash = i; }
  }
  zc::String treePrefix = zc::heapString(driverToken.begin(), lastSlash + 1);
  ZC_EXPECT(zc::StringPtr(treePrefix).find(".zomlink-"_zc) != zc::none);
  ZC_EXPECT(zc::StringPtr(treePrefix).startsWith(zc::str(base, "/")));

  // Build the full expected 11-token vector and compare index by index.
  auto expected = zc::heapArrayBuilder<zc::String>(11);
  expected.add(zc::str(treePrefix, "driver"));            // argv[0]: driver snapshot
  expected.add(zc::str("-o"));                            // argv[1]
  expected.add(zc::str(treePrefix, "output-candidate"));  // argv[2]: tree output candidate
  expected.add(zc::str("-e"));                            // argv[3]
  expected.add(zc::str("zom"));                           // argv[4]: entry symbol
  expected.add(zc::str(treePrefix, "crt-0"));             // argv[5]
  expected.add(zc::str(treePrefix, "obj-0"));             // argv[6]
  expected.add(zc::str(treePrefix, "obj-1"));             // argv[7]
  expected.add(zc::str(treePrefix, "rt-0"));              // argv[8]
  expected.add(zc::str(treePrefix, "rt-1"));              // argv[9]
  expected.add(zc::str(treePrefix, "lib-0"));             // argv[10]
  zc::Array<zc::String> expectedArgv = expected.finish();

  for (size_t i = 0; i < expectedArgv.size(); ++i) {
    ZC_EXPECT(argvToken(argvLines, i) == expectedArgv[i]);
  }
  // The candidate's recorded output path equals argv[2].
  ZC_EXPECT(candidate.outputCandidatePath() == expectedArgv[2]);

  ZC_EXPECT(zc::mv(candidate).discardAndCleanup().isClean());
  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects an input whose inode is swapped after verification") {
  // The plan records the digest of the original app0.o. After the plan is built,
  // the app0.o pathname is repointed at a DIFFERENT inode with different bytes
  // (remove + create). The snapshot re-verification must reject it as an
  // input-revision mismatch and never spawn the driver.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  dir->remove(zc::Path("app0.o"_zc));
  writeFile(*dir, "app0.o"_zc, bytesOf("SWAPPED-OBJECT-BYTES-DIFFERENT"_zc).asPtr(), false);

  auto result = linkExpectingRejectNoSpawn(zc::mv(plan), *fs);
  ZC_EXPECT(result.isIrInvariantRejected());
  // No output was produced (never spawned) and no snapshot tree was left behind.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

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

  auto result = linkExpectingRejectNoSpawn(zc::mv(plan), *fs);
  ZC_EXPECT(result.isIrInvariantRejected());
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a same-length byte change in every input role before spawning") {
  // For each snapshot role - driver, crt object, user object, runtime object, and
  // default library - repoint the source at a DIFFERENT inode with the SAME byte
  // count but different bytes after the plan is built. Each case proves:
  //   * role attribution: on disk, ONLY that role's source differs from its plan
  //     bytes; the other SIX inputs still match, so the gate that fires is that
  //     role's;
  //   * it is a digest gate, not a size gate (same byte count);
  //   * it fires before the driver is spawned (no ".started"/".args"/output);
  //   * the private snapshot tree is cleaned (Complete, not RecoveryRequired);
  //   * the failure is the complete RFC 0043 InputRevisionMismatch row (one
  //     shared exact-row helper, so every field is checked identically).
  struct RoleFile {
    zc::StringPtr fileName;
    bool executable;
  };
  const RoleFile roleFiles[] = {
      {"ld"_zc, true},     {"crt1.o"_zc, false}, {"app0.o"_zc, false},
      {"rt0.o"_zc, false}, {"libc.a"_zc, false},
  };

  for (const RoleFile& mutatedRole : roleFiles) {
    auto fs = zc::newDiskFilesystem();
    zc::String base = tempDirPath();
    auto dir = openDir(*fs, base);
    Scenario scenario;
    auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

    // The plan-recorded pristine bytes for each of the SEVEN input files, so
    // on-disk isolation can be checked against what the plan digested for every
    // input (not just the five mutation representatives).
    auto recordedBytesFor = [&](zc::StringPtr name) -> zc::ArrayPtr<const zc::byte> {
      if (name == "ld"_zc) { return scenario.driverBytes.asPtr(); }
      if (name == "crt1.o"_zc) { return scenario.crtBytes.asPtr(); }
      if (name == "app0.o"_zc) { return scenario.object0Bytes.asPtr(); }
      if (name == "app1.o"_zc) { return scenario.object1Bytes.asPtr(); }
      if (name == "rt0.o"_zc) { return scenario.runtime0Bytes.asPtr(); }
      if (name == "rt1.o"_zc) { return scenario.runtime1Bytes.asPtr(); }
      if (name == "libc.a"_zc) { return scenario.libBytes.asPtr(); }
      ZC_UNREACHABLE;
    };
    const zc::StringPtr allInputFiles[] = {
        "ld"_zc, "crt1.o"_zc, "app0.o"_zc, "app1.o"_zc, "rt0.o"_zc, "rt1.o"_zc, "libc.a"_zc,
    };
    auto bytesEqual = [](zc::ArrayPtr<const zc::byte> a, zc::ArrayPtr<const zc::byte> b) {
      if (a.size() != b.size()) { return false; }
      for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) { return false; }
      }
      return true;
    };

    // Mutate exactly one role: same length, one byte flipped, fresh inode.
    auto original = dir->openFile(zc::Path(zc::heapString(mutatedRole.fileName)))->readAllBytes();
    auto mutated = zc::heapArray<zc::byte>(original.size());
    for (size_t i = 0; i < original.size(); ++i) { mutated[i] = original[i]; }
    ZC_REQUIRE(mutated.size() > 0);
    mutated[0] = mutated[0] == zc::byte{0} ? zc::byte{1} : zc::byte{0};
    dir->remove(zc::Path(zc::heapString(mutatedRole.fileName)));
    writeFile(*dir, mutatedRole.fileName, mutated.asPtr(), mutatedRole.executable);

    // Role attribution over ALL seven inputs: on disk, ONLY the mutated role
    // differs from the bytes the plan recorded (same length), so this role's
    // snapshot gate is the sole possible trigger; the other six still match.
    for (zc::StringPtr fileName : allInputFiles) {
      auto onDisk = dir->openFile(zc::Path(zc::heapString(fileName)))->readAllBytes();
      bool matchesRecorded = bytesEqual(onDisk.asPtr(), recordedBytesFor(fileName));
      if (fileName == mutatedRole.fileName) {
        ZC_EXPECT(onDisk.size() == recordedBytesFor(fileName).size());  // same length
        ZC_EXPECT(!matchesRecorded);                                    // but different bytes
      } else {
        ZC_EXPECT(matchesRecorded);  // untouched
      }
    }

    // Use the observed entry point: the driver must NOT be spawned on an input-
    // revision mismatch (it is caught by the snapshot re-verify, a pre-spawn gate).
    auto result = linkExpectingRejectNoSpawn(zc::mv(plan), *fs);
    // Same-length change caught by the digest: InputRevisionMismatch (an
    // IrInvariantRejected row). Full RFC row via the shared helper.
    expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::IrInvariantRejected,
                              IrFailureKind::InputRevisionMismatch);

    // No final path was ever touched. (The spawn-observer count==0 inside
    // linkExpectingRejectNoSpawn is the drift-proof "never spawned" proof; the
    // fixture's in-tree markers are cleaned with the transaction root, so they
    // could not serve that role.)
    ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
    // The transaction root was discarded on the reject path (Complete, asserted
    // by linkExpectingRejectNoSpawn, not RecoveryRequired).
    ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

    fs->getRoot().remove(zc::Path::parse(base.slice(1)));
  }
}

ZC_TEST("linkExecutable rejects a pre-existing stale output before snapshotting") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  dir->openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("stale"_zc);

  auto result = linkExpectingRejectNoSpawn(zc::mv(plan), *fs);
  // A pre-existing FINAL output is InvalidFact, an IrInvariantRejected row (the
  // plan's output-path fact cannot be honored) and is rejected before any spawn.
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::IrInvariantRejected,
                            IrFailureKind::InvalidFact);
  // The stale file is left untouched (we reject, we do not clobber it).
  ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllText() == "stale"_zc);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST(
    "linkExecutable rejects a pre-existing BROKEN symlink at the final path (INV-1 no-follow)") {
  // A broken symlink already sits at the final output path. A following existence
  // probe (`exists`) would resolve the dangling target and report "absent", letting
  // the link proceed and later clobber whatever the symlink names. The INV-1 gate is
  // a NO-FOLLOW lstat, so it rejects any pre-existing final entry - including a
  // broken symlink - up front, before any spawn, and leaves the entry untouched.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  // Create a dangling symlink at <base>/app pointing at a non-existent target.
  ZC_REQUIRE(
      dir->trySymlink(zc::Path("app"_zc), "does-not-exist-target"_zc, zc::WriteMode::CREATE));
  // Sanity: a following existence check treats the broken link as absent...
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  // ...but a no-follow lstat sees the link entry itself.
  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);

  auto result = linkExpectingRejectNoSpawn(zc::mv(plan), *fs);
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::IrInvariantRejected,
                            IrFailureKind::InvalidFact);
  // The broken symlink is left exactly as it was (never followed, never removed).
  ZC_IF_SOME(target, dir->tryReadlink(zc::Path("app"_zc))) {
    ZC_EXPECT(target == "does-not-exist-target"_zc);
  } else {
    ZC_FAIL_EXPECT("the broken symlink at the final path must survive untouched");
  }
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable cleans the transaction root when the driver exits nonzero") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_PARTIAL ""_zc, scenario);

  auto result = linkExpectingRejectAfterSpawn(zc::mv(plan), *fs);
  // A nonzero exit is CapabilityRejected: OutputCreationFailed per RFC 0043, after
  // a spawn was attempted (observer count == 1).
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::CapabilityRejected,
                            IrFailureKind::OutputCreationFailed);
  // The final path was never written (the driver wrote a partial output-candidate
  // inside the tree, which is discarded), and the transaction root is removed.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable reports a missing output on a clean exit") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_NO_OUTPUT ""_zc, scenario);

  auto result = linkExpectingRejectAfterSpawn(zc::mv(plan), *fs);
  // A missing output after a clean exit is CapabilityRejected: OutputCreationFailed,
  // after a spawn was attempted (observer count == 1).
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::CapabilityRejected,
                            IrFailureKind::OutputCreationFailed);
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a symlink output candidate (D4 no-follow invariant)") {
  // The driver exits zero but writes a SYMLINK at <root>/output-candidate (to a
  // sibling regular file with ELF magic). D4 opens the output candidate with a
  // no-follow open, so the symlink entry is refused even though a following open
  // would have succeeded on the target. The link is rejected as OutputCreationFailed
  // and the transaction root is cleaned - no candidate is produced.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SYMLINK ""_zc, scenario);

  auto result = linkExpectingRejectAfterSpawn(zc::mv(plan), *fs);
  // A non-regular (symlink) output is CapabilityRejected: OutputCreationFailed,
  // after the driver ran (observer count == 1).
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::CapabilityRejected,
                            IrFailureKind::OutputCreationFailed);
  // The final path was never touched and the transaction root is cleaned.
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects an empty output candidate (D4 non-empty invariant)") {
  // The driver exits zero but leaves an EMPTY regular file at the output candidate
  // (distinct from the missing-output case: the entry exists but has zero bytes).
  // D4's non-empty invariant must reject it after the spawn.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_EMPTY ""_zc, scenario);

  auto result = linkExpectingRejectAfterSpawn(zc::mv(plan), *fs);
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::CapabilityRejected,
                            IrFailureKind::OutputCreationFailed);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a directory output candidate (D4 regular-file invariant)") {
  // The driver exits zero but creates a DIRECTORY at the output candidate. D4's
  // regular-file invariant must reject it after the spawn.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_DIRECTORY ""_zc, scenario);

  auto result = linkExpectingRejectAfterSpawn(zc::mv(plan), *fs);
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::CapabilityRejected,
                            IrFailureKind::OutputCreationFailed);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable rejects a multiply-linked output candidate (D4 single-link invariant)") {
  // The driver exits zero, writes a regular ELF output, then creates a second hard
  // link to the same inode (st_nlink == 2). D4's sole-link invariant must reject it
  // after the spawn: a multiply-linked inode could be rewritten in place through the
  // external link, so it is not an exclusively-owned transaction output.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_HARDLINK ""_zc, scenario);

  auto result = linkExpectingRejectAfterSpawn(zc::mv(plan), *fs);
  expectLinkerInvocationRow(soleFailureFact(result), IrRejectedBranch::CapabilityRejected,
                            IrFailureKind::OutputCreationFailed);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable maps a snapshot write-call fault to a rejection and cleans up") {
  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Arm the wrapper to fail the very first snapshot File::write. The fault is
  // thrown inside the real write, caught by prepare's exception guard, and mapped
  // to an OutputCreationFailed rejection; the private tree is rolled back (the
  // remove itself succeeds, so the outcome is Complete).
  FaultScript script;
  script.kind = FaultKind::Write;
  script.skip = 0;
  FaultFilesystem faultFs(script);

  CleanupAwareOutcome<LinkedOutputCandidate> outcome = linkExecutable(zc::mv(plan), faultFs);
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<LinkedOutputCandidate> result = zc::mv(outcome).takeComplete();
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isCapabilityRejected());
  ZC_EXPECT(script.fired);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkExecutable maps a pass-2 read-call fault to a rejection and cleans up") {
  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Fail the first read of a snapshot file. Phase-1 source reads are OUTSIDE the
  // tree and are never faulted by the wrapper; the first inside-tree read is the
  // driver's pass-2 re-verify read, which is what this exercises.
  FaultScript script;
  script.kind = FaultKind::Read;
  script.skip = 0;
  FaultFilesystem faultFs(script);

  CleanupAwareOutcome<LinkedOutputCandidate> outcome = linkExecutable(zc::mv(plan), faultFs);
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<LinkedOutputCandidate> result = zc::mv(outcome).takeComplete();
  ZC_EXPECT(!result.isVerified());
  ZC_EXPECT(result.isCapabilityRejected());
  ZC_EXPECT(script.fired);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST(
    "LinkedOutputCandidate discardAndCleanup reports Obligated when post-spawn content cleanup "
    "fails, and the candidate's output survives") {
  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // The link itself succeeds and transfers the transaction root into the
  // candidate. Only when the caller later discards the candidate is the content
  // removal forced to fail: the disposition must be Obligated (not Clean), with a
  // structured obligation carrying the transaction id, the exact captured
  // identity, and the plan id. Before discarding, the candidate's output is fully
  // readable through its held handle.
  FaultScript script;
  script.kind = FaultKind::ContentRemove;
  script.skip = 0;
  FaultFilesystem faultFs(script);

  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), faultFs);
  LinkPlanId candidatePlanId = candidate.plan().id();
  // The verified output is readable through the candidate before any cleanup.
  zc::Array<uint8_t> bytes = candidate.readOutput();
  ZC_ASSERT(bytes.size() == 4u);
  ZC_EXPECT(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');

  CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
  ZC_ASSERT(disposition.isObligated());
  SnapshotCleanupObligation obligation = zc::mv(disposition).takeObligation();
  ZC_EXPECT(obligation.cleanupStage() == CleanupStage::PostSpawnCleanup);
  ZC_EXPECT(obligation.cleanupFailureKind() == CleanupFailureKind::ContentRemovalFailed);
  ZC_EXPECT(obligation.planId() == candidatePlanId);
  ZC_EXPECT(obligation.directoryIdentity() != zc::none);
  // The obligation's tree path is derived from its parent and token and names a
  // ".zomlink-" directory.
  ZC_EXPECT(zc::StringPtr(obligation.treePath()).find(".zomlink-"_zc) != zc::none);

  // The forced-failure left the tree behind; remove the whole base for hygiene.
  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST(
    "LinkedOutputCandidate discardAndCleanup reports TopLevelRemovalFailed on a top-level fault") {
  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Let content removal succeed, then fail the top-level directory unlink. The
  // exact-identity re-check has already passed (the tree is genuinely ours), so
  // the failure is attributed to the top-level stage.
  FaultScript script;
  script.kind = FaultKind::TopLevelRemove;
  script.skip = 0;
  FaultFilesystem faultFs(script);

  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), faultFs);
  CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
  ZC_ASSERT(disposition.isObligated());
  SnapshotCleanupObligation obligation = zc::mv(disposition).takeObligation();
  ZC_EXPECT(obligation.cleanupStage() == CleanupStage::PostSpawnCleanup);
  ZC_EXPECT(obligation.cleanupFailureKind() == CleanupFailureKind::TopLevelRemovalFailed);

  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("cleanup classifies a non-ENOENT fstatat error on the top-level entry as removal failure") {
  // The exact-identity re-check calls fstatat on the held parent descriptor. If
  // that call fails with anything other than ENOENT (here EACCES, forced by
  // stripping the parent directory's search permission after the tree is built),
  // cleanup must NOT treat the entry as gone: it classifies as
  // TopLevelRemovalFailed. Root bypasses directory permission checks, so this
  // real-permission case is skipped when running as root.
  if (::geteuid() == 0) { return; }

  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Prepare a real capability (content will be cleaned through the held snapshot
  // capability, then the top-level fstatat runs against the held parent fd).
  CleanupAwareOutcome<PreparedLinkInputs> preparedOutcome =
      PreparedLinkInputs::prepare(plan, *disk);
  ZC_ASSERT(preparedOutcome.isComplete());
  IrOperationResult<PreparedLinkInputs> preparedResult = zc::mv(preparedOutcome).takeComplete();
  ZC_ASSERT(preparedResult.isVerified());
  PreparedLinkInputs prepared = zc::mv(preparedResult).takeVerified();

  // Strip search/exec permission from the parent so fstatat(parentFd, leaf, ...)
  // fails with EACCES (not ENOENT). The held snapshot-dir descriptor is already
  // open, so content removal still succeeds; only the top-level re-check fails.
  auto parentReal = zc::str("/", base.slice(1));
  ZC_REQUIRE(::chmod(parentReal.cStr(), 0) == 0);

  CleanupDisposition disposition = zc::mv(prepared).discardAndCleanup();

  // Restore permissions before asserting, so cleanup of the base always works.
  ZC_REQUIRE(::chmod(parentReal.cStr(), 0700) == 0);

  ZC_ASSERT(disposition.isObligated());
  SnapshotCleanupObligation obligation = zc::mv(disposition).takeObligation();
  ZC_EXPECT(obligation.cleanupStage() == CleanupStage::PostSpawnCleanup);
  // A non-ENOENT fstatat error is a top-level removal failure, never "removed".
  ZC_EXPECT(obligation.cleanupFailureKind() == CleanupFailureKind::TopLevelRemovalFailed);

  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("PreparedLinkInputs::prepare reports RecoveryRequired when rollback cleanup fails") {
  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // Swap the object inode so prepare rejects, AND force the rollback content
  // removal to fail: the prepare outcome must be RecoveryRequired at the
  // PrepareRollback stage with the rejection primary preserved.
  dir->remove(zc::Path("app0.o"_zc));
  writeFile(*dir, "app0.o"_zc, bytesOf("SWAPPED-OBJECT-BYTES-DIFFERENT"_zc).asPtr(), false);

  FaultScript script;
  script.kind = FaultKind::ContentRemove;
  script.skip = 0;
  FaultFilesystem faultFs(script);

  CleanupAwareOutcome<PreparedLinkInputs> outcome = PreparedLinkInputs::prepare(plan, faultFs);
  ZC_ASSERT(outcome.isRecoveryRequired());
  RecoveryRequiredOutcome<PreparedLinkInputs> rr = zc::mv(outcome).takeRecoveryRequired();
  ZC_EXPECT(rr.obligation.cleanupStage() == CleanupStage::PrepareRollback);
  ZC_EXPECT(rr.obligation.cleanupFailureKind() == CleanupFailureKind::ContentRemovalFailed);
  ZC_EXPECT(!rr.primary.isVerified());

  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("prepare fails closed with IdentityUnavailable when the tree exposes no stable identity") {
  // The snapshot directory cannot supply an exact (dev,ino) - modeled by the
  // wrapper suppressing the inside-tree descriptor. Prepare must fail closed
  // BEFORE building a capability or spawning: reject with OutputCreationFailed,
  // clean the tree's CONTENTS through the held capability, but never auto-remove
  // the top-level entry without an owner proof, and report an IdentityUnavailable
  // obligation at the PrepareRollback stage.
  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  FaultScript script;
  script.suppressInsideTreeFd = true;
  FaultFilesystem faultFs(script);

  CleanupAwareOutcome<PreparedLinkInputs> outcome = PreparedLinkInputs::prepare(plan, faultFs);
  ZC_ASSERT(outcome.isRecoveryRequired());
  RecoveryRequiredOutcome<PreparedLinkInputs> rr = zc::mv(outcome).takeRecoveryRequired();
  ZC_EXPECT(!rr.primary.isVerified());
  ZC_EXPECT(rr.primary.isCapabilityRejected());
  ZC_EXPECT(rr.obligation.cleanupStage() == CleanupStage::PrepareRollback);
  ZC_EXPECT(rr.obligation.cleanupFailureKind() == CleanupFailureKind::IdentityUnavailable);
  // No exact identity was captured, so the obligation carries none.
  ZC_EXPECT(rr.obligation.directoryIdentity() == zc::none);

  // The empty top-level tree survives (never auto-removed without an owner proof),
  // but its contents were cleaned through the held capability: the directory the
  // obligation names exists and is empty.
  zc::String treeName;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { treeName = zc::heapString(name); }
  }
  ZC_ASSERT(treeName.size() > 0);
  ZC_EXPECT(dir->openSubdir(zc::Path(treeName))->listNames().size() == 0u);

  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
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
  IrOperationResult<PreparedLinkInputs> preparedResult = zc::mv(preparedOutcome).takeComplete();
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

  // discardAndCleanup must refuse to delete the competitor (identity mismatch).
  CleanupDisposition disposition = zc::mv(prepared).discardAndCleanup();
  ZC_ASSERT(disposition.isObligated());
  SnapshotCleanupObligation obligation = zc::mv(disposition).takeObligation();
  ZC_EXPECT(obligation.cleanupFailureKind() == CleanupFailureKind::IdentityMismatch);
  // The competitor's directory and its sentinel file are untouched.
  ZC_EXPECT(dir->openSubdir(zc::Path(treeName))->exists(zc::Path("competitor-owned"_zc)));

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("prepare retries a fresh token on an exclusive-create collision without touching it") {
  // A scripted token source returns the SAME token twice, then a distinct one.
  // The first prepare claims the tree at token-A. The second prepare, driven by
  // a source that offers token-A first, must find token-A already exists, refuse
  // to adopt or delete it, and retry with the next (distinct) token - so the two
  // transactions end up in two different trees and the first tree's contents and
  // identity are completely unchanged.
  auto fs = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*fs, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  // First transaction takes token 0xA1.
  auto firstScript = zc::heapArrayBuilder<zc::Array<uint8_t>>(1);
  firstScript.add(token16(0xA1));
  ScriptedTokenSource firstSource(firstScript.finish());
  CleanupAwareOutcome<PreparedLinkInputs> firstOutcome =
      LinkerInvocationTestAccess::prepareWithTokenSource(plan, *fs, firstSource);
  ZC_ASSERT(firstOutcome.isComplete());
  IrOperationResult<PreparedLinkInputs> firstResult = zc::mv(firstOutcome).takeComplete();
  ZC_ASSERT(firstResult.isVerified());
  PreparedLinkInputs first = zc::mv(firstResult).takeVerified();

  // Record the first tree's name and a fingerprint of its content.
  zc::String firstTreeName;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { firstTreeName = zc::heapString(name); }
  }
  ZC_ASSERT(firstTreeName.size() > 0);
  auto firstDriverBefore =
      dir->openSubdir(zc::Path(firstTreeName))->openFile(zc::Path("driver"_zc))->readAllBytes();
  // The first tree's EXACT identity (dev,ino), so the collision is proven not to
  // have swapped the directory object, not merely to have preserved its bytes.
  DirIdentity firstIdentityBefore = subdirIdentity(*dir, firstTreeName);

  // Second transaction is offered token 0xA1 first (collision), then 0xB2.
  auto secondScript = zc::heapArrayBuilder<zc::Array<uint8_t>>(2);
  secondScript.add(token16(0xA1));  // collides with the live first tree
  secondScript.add(token16(0xB2));  // fresh, must be used after retry
  ScriptedTokenSource secondSource(secondScript.finish());
  CleanupAwareOutcome<PreparedLinkInputs> secondOutcome =
      LinkerInvocationTestAccess::prepareWithTokenSource(plan, *fs, secondSource);
  ZC_ASSERT(secondOutcome.isComplete());
  IrOperationResult<PreparedLinkInputs> secondResult = zc::mv(secondOutcome).takeComplete();
  ZC_ASSERT(secondResult.isVerified());
  PreparedLinkInputs second = zc::mv(secondResult).takeVerified();

  // Two distinct trees now exist; the collision did not delete or reuse the first.
  ZC_EXPECT(countSnapshotTrees(*dir) == 2u);
  ZC_EXPECT(first.program() != second.program());
  // The first tree's content is byte-for-byte unchanged by the collision.
  auto firstDriverAfter =
      dir->openSubdir(zc::Path(firstTreeName))->openFile(zc::Path("driver"_zc))->readAllBytes();
  ZC_ASSERT(firstDriverBefore.size() == firstDriverAfter.size());
  bool unchanged = true;
  for (size_t i = 0; i < firstDriverBefore.size(); ++i) {
    if (firstDriverBefore[i] != firstDriverAfter[i]) { unchanged = false; }
  }
  ZC_EXPECT(unchanged);
  // The first tree's EXACT identity is unchanged: same directory object, never
  // unlinked-and-recreated by the colliding transaction.
  ZC_EXPECT(subdirIdentity(*dir, firstTreeName) == firstIdentityBefore);

  // Clean both up.
  ZC_ASSERT(zc::mv(first).discardAndCleanup().isClean());
  ZC_ASSERT(zc::mv(second).discardAndCleanup().isClean());
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("two overlapping (sequential, not threaded) prepares of the same plan use distinct trees") {
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

  IrOperationResult<PreparedLinkInputs> firstResult = zc::mv(firstOutcome).takeComplete();
  IrOperationResult<PreparedLinkInputs> secondResult = zc::mv(secondOutcome).takeComplete();
  ZC_ASSERT(firstResult.isVerified());
  ZC_ASSERT(secondResult.isVerified());
  // Both trees coexist: two distinct private directories.
  ZC_EXPECT(countSnapshotTrees(*dir) == 2u);

  PreparedLinkInputs first = zc::mv(firstResult).takeVerified();
  PreparedLinkInputs second = zc::mv(secondResult).takeVerified();
  // The two programs (driver snapshot paths) are in different trees.
  ZC_EXPECT(first.program() != second.program());

  // Finishing the first removes only its own tree; the second's survives.
  CleanupDisposition firstFinish = zc::mv(first).discardAndCleanup();
  ZC_ASSERT(firstFinish.isClean());
  ZC_EXPECT(countSnapshotTrees(*dir) == 1u);

  // Finishing the second removes the last tree.
  CleanupDisposition secondFinish = zc::mv(second).discardAndCleanup();
  ZC_ASSERT(secondFinish.isClean());
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("a prepared capability dropped without discardAndCleanup swallows a cleanup fault") {
  // If a prepared capability is dropped without discardAndCleanup, its destructor
  // is a noexcept last-resort leak guard: it best-effort removes the tree and
  // swallows any fault. With content removal armed to fail, dropping the
  // capability must NOT throw (the test would abort under the noexcept violation)
  // and simply leaves the tree behind - no structured obligation is produced on
  // this fallback path, which is exactly why discardAndCleanup is the real path.
  auto disk = zc::newDiskFilesystem();
  zc::String base = tempDirPath();
  auto dir = openDir(*disk, base);
  Scenario scenario;
  auto plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  FaultScript script;
  script.kind = FaultKind::ContentRemove;
  script.skip = 0;
  FaultFilesystem faultFs(script);

  {
    CleanupAwareOutcome<PreparedLinkInputs> outcome = PreparedLinkInputs::prepare(plan, faultFs);
    ZC_ASSERT(outcome.isComplete());
    IrOperationResult<PreparedLinkInputs> result = zc::mv(outcome).takeComplete();
    ZC_ASSERT(result.isVerified());
    PreparedLinkInputs prepared = zc::mv(result).takeVerified();
    // `prepared` goes out of scope here; its noexcept destructor runs the armed
    // failing removal and must not propagate.
  }
  // The destructor swallowed the fault (no throw reached here) and left the tree.
  ZC_EXPECT(script.fired);
  ZC_EXPECT(countSnapshotTrees(*dir) == 1u);

  disk->getRoot().remove(zc::Path::parse(base.slice(1)));
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
  CleanupAwareOutcome<LinkedOutputCandidate> outcome = linkExecutable(zc::mv(plan), memFs);
  // No tree was created (fail-closed before creation), so the outcome is Complete
  // with a capability rejection, not RecoveryRequired.
  ZC_ASSERT(outcome.isComplete());
  IrOperationResult<LinkedOutputCandidate> result = zc::mv(outcome).takeComplete();
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
