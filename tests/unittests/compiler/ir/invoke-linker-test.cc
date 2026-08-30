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
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>  // fstat/chmod, used only by the Linux fixture-driven cases
#include <sys/wait.h>
#endif

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/executable-inspector.h"
#include "compiler/ir/executable-publication-internal.h"
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
ExecutableInspectionProfile elfInspectionProfile(
    ExecutableMachine machine = ExecutableMachine::X86_64,
    zc::StringPtr requiredRuntimeSymbol = zc::StringPtr()) {
  zc::Vector<zc::String> requiredRuntimeSymbols;
  if (requiredRuntimeSymbol.size() != 0) {
    requiredRuntimeSymbols.add(zc::heapString(requiredRuntimeSymbol));
  }
  auto profile = ExecutableInspectionProfile::make(
      ObjectFormat::Elf, machine, 64, requiredRuntimeSymbols.releaseAsArray(), zc::str("__zom_"));
  ZC_REQUIRE(profile != zc::none);
  return ZC_REQUIRE_NONNULL(zc::mv(profile));
}

VerifiedLinkPlan buildScenario(const zc::Directory& dir, zc::StringPtr base,
                               zc::StringPtr driverFixturePath, Scenario& out,
                               ExecutableMachine machine = ExecutableMachine::X86_64,
                               zc::StringPtr requiredRuntimeSymbol = zc::StringPtr()) {
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
                                elfInspectionProfile(machine, requiredRuntimeSymbol),
                                zc::heapArray<uint8_t>({0x7a, 0x6f, 0x6d}),  // "zom"
                                twoObjects.finish(),
                                twoRuntimes.finish(),
                                zc::str(base),
                                zc::str(base, "/app")};
  auto result = LinkPlanVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

zc::Array<uint8_t> copyBytes(zc::ArrayPtr<const uint8_t> source) {
  auto result = zc::heapArray<uint8_t>(source.size());
  for (size_t index = 0; index < source.size(); ++index) { result[index] = source[index]; }
  return result;
}

VerifiedExecutableManifest buildManifest(const LinkedOutputCandidate& candidate,
                                         zc::StringPtr outputRoot) {
  const VerifiedLinkPlan& plan = candidate.plan();
  identity::Sha256Digest toolchain = ExecutablePublicationManifestBinding::toolchainIdentity(plan);
  ExecutableManifestRequest request{
      zc::heapString(plan.outputPath()),
      zc::heapString(outputRoot),
      copyBytes(plan.targetSpecificationIdentity()),
      candidate.outputDigest(),
      candidate.outputSize(),
      plan.id(),
      ExecutablePublicationManifestBinding::inputArtifactDigests(plan),
      copyBytes(toolchain.bytes())};
  IrOperationResult<VerifiedExecutableManifest> result =
      ExecutableManifestVerifier::verify(zc::mv(request));
  ZC_REQUIRE(result.isVerified());
  return zc::mv(result).takeVerified();
}

class ManifestCompetitorObserver final : public PublicationCheckpointObserver {
public:
  explicit ManifestCompetitorObserver(const zc::Directory& outputDir) : outputDir(outputDir) {}

  void reached(PublicationCheckpoint checkpoint) override {
    if (checkpoint != PublicationCheckpoint::ExecCommittedDurable || created) { return; }
    outputDir.openFile(zc::Path("app.zom-artifact"_zc), zc::WriteMode::CREATE)
        ->writeAll("competitor-manifest"_zc);
    created = true;
  }

  bool didCreate() const noexcept { return created; }

private:
  const zc::Directory& outputDir;
  bool created = false;
};

class ExecutableReplacementObserver final : public PublicationCheckpointObserver {
public:
  explicit ExecutableReplacementObserver(const zc::Directory& outputDir) : outputDir(outputDir) {}

  void reached(PublicationCheckpoint checkpoint) override {
    if (checkpoint != PublicationCheckpoint::ExecCommittedDurable || replaced) { return; }
    ZC_REQUIRE(outputDir.tryRemove(zc::Path("app"_zc)));
    outputDir.openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("competitor-app"_zc);
    replaced = true;
  }

  bool didReplace() const noexcept { return replaced; }

private:
  const zc::Directory& outputDir;
  bool replaced = false;
};

class ExitAtPublicationCheckpoint final : public PublicationCheckpointObserver {
public:
  explicit ExitAtPublicationCheckpoint(PublicationCheckpoint target) : target(target) {}

  void reached(PublicationCheckpoint checkpoint) override {
    if (checkpoint == target) { _exit(64 + static_cast<int>(checkpoint)); }
  }

private:
  PublicationCheckpoint target;
};

void crashPublicationAt(PublicationCheckpoint checkpoint, zc::StringPtr suffix,
                        zc::String& baseOut) {
  baseOut = zc::str(tempDirPath(), "-", suffix);
  auto setupFs = zc::newDiskFilesystem();
  auto setupDir = openDir(*setupFs, baseOut);
  (void)setupDir;

  pid_t child = ::fork();
  ZC_REQUIRE(child >= 0);
  if (child == 0) {
    auto fs = zc::newDiskFilesystem();
    auto dir = openDir(*fs, baseOut);
    Scenario scenario;
    VerifiedLinkPlan plan = buildScenario(*dir, baseOut, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
    LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
    VerifiedExecutableManifest manifest = buildManifest(candidate, baseOut);
    ExitAtPublicationCheckpoint observer(checkpoint);
    (void)PublicationTransactionTestAccess::publishObserved(zc::mv(candidate), zc::mv(manifest),
                                                            observer);
    _exit(127);
  }
  int status = 0;
  ZC_REQUIRE(::waitpid(child, &status, 0) == child);
  ZC_REQUIRE(WIFEXITED(status));
  ZC_REQUIRE(WEXITSTATUS(status) == 64 + static_cast<int>(checkpoint));
}

PublicationRecoveryResult crashAndRecoverPublication(PublicationCheckpoint checkpoint,
                                                     zc::StringPtr suffix, zc::String& baseOut) {
  crashPublicationAt(checkpoint, suffix, baseOut);
  auto recoveryFs = zc::newDiskFilesystem();
  return recoverLinkedOutputPublication(*recoveryFs, zc::str(baseOut, "/app"));
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

class FinalManifestCorruptionObserver final : public PublicationCheckpointObserver {
public:
  explicit FinalManifestCorruptionObserver(const zc::Directory& outputDir) : outputDir(outputDir) {}

  void reached(PublicationCheckpoint checkpoint) override {
    if (checkpoint != PublicationCheckpoint::ManifestRenamed || corrupted) { return; }
    outputDir.openFile(zc::Path("app.zom-artifact"_zc), zc::WriteMode::MODIFY)
        ->writeAll("corrupt"_zc);
    corrupted = true;
  }

  bool didCorrupt() const noexcept { return corrupted; }

private:
  const zc::Directory& outputDir;
  bool corrupted = false;
};

class ManifestStagedJournalCompetitorObserver final : public PublicationCheckpointObserver {
public:
  explicit ManifestStagedJournalCompetitorObserver(const zc::Directory& outputDir)
      : outputDir(outputDir) {}

  void reached(PublicationCheckpoint checkpoint) override {
    if (checkpoint != PublicationCheckpoint::StartedDurable || created) { return; }
    zc::String tree = soleSnapshotTreeName(outputDir);
    zc::StringPtr treeView(tree);
    ZC_REQUIRE(treeView.startsWith(".zomlink-"_zc));
    zc::String token = zc::heapString(treeView.slice(9));
    zc::String leaf = zc::str("journal.", token, ".manifest-staged");
    outputDir.openFile(zc::Path(leaf), zc::WriteMode::CREATE)
        ->writeAll("competitor-manifest-staged"_zc);
    created = true;
  }

  bool didCreate() const noexcept { return created; }

private:
  const zc::Directory& outputDir;
  bool created = false;
};

class ManifestAndExecutableCompetitorObserver final : public PublicationCheckpointObserver {
public:
  explicit ManifestAndExecutableCompetitorObserver(const zc::Directory& outputDir)
      : outputDir(outputDir) {}

  void reached(PublicationCheckpoint checkpoint) override {
    if (checkpoint == PublicationCheckpoint::ExecCommittedDurable && !manifestCreated) {
      outputDir.openFile(zc::Path("app.zom-artifact"_zc), zc::WriteMode::CREATE)
          ->writeAll("competitor-manifest"_zc);
      manifestCreated = true;
    }
    if (checkpoint == PublicationCheckpoint::ManifestRenameRejected && !executableReplaced) {
      ZC_REQUIRE(outputDir.tryRemove(zc::Path("app"_zc)));
      outputDir.openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("competitor-app"_zc);
      executableReplaced = true;
    }
  }

  bool completed() const noexcept { return manifestCreated && executableReplaced; }

private:
  const zc::Directory& outputDir;
  bool manifestCreated = false;
  bool executableReplaced = false;
};

class PublicationFaultObserver final : public PublicationCheckpointObserver {
public:
  explicit PublicationFaultObserver(
      PublicationFaultPoint first,
      PublicationFaultPoint second = PublicationFaultPoint::JournalWrite, bool hasSecond = false,
      size_t firstSkip = 0)
      : first(first), second(second), hasSecond(hasSecond), firstSkip(firstSkip) {}

  void reached(PublicationCheckpoint) override {}

  bool fail(PublicationFaultPoint point) override {
    if (point == first && !firstFired) {
      if (firstSkip > 0) {
        --firstSkip;
        return false;
      }
      firstFired = true;
      return true;
    }
    if (hasSecond && point == second && !secondFired) {
      secondFired = true;
      return true;
    }
    return false;
  }

  bool firedAll() const noexcept { return firstFired && (!hasSecond || secondFired); }

private:
  PublicationFaultPoint first;
  PublicationFaultPoint second;
  bool hasSecond;
  size_t firstSkip;
  bool firstFired = false;
  bool secondFired = false;
};

class BlockAtPublicationCheckpoint final : public PublicationCheckpointObserver {
public:
  BlockAtPublicationCheckpoint(PublicationCheckpoint target, int readyFd, int releaseFd)
      : target(target), readyFd(readyFd), releaseFd(releaseFd) {}

  void reached(PublicationCheckpoint checkpoint) override {
    if (checkpoint != target || reachedTarget) { return; }
    reachedTarget = true;
    const char marker = 'r';
    if (::write(readyFd, &marker, 1) != 1) { _exit(120); }
    char release = 0;
    if (::read(releaseFd, &release, 1) != 1 || release != 'g') { _exit(121); }
  }

private:
  PublicationCheckpoint target;
  int readyFd;
  int releaseFd;
  bool reachedTarget = false;
};

void crashPublicationInBaseAt(PublicationCheckpoint checkpoint, zc::StringPtr base) {
  pid_t child = ::fork();
  ZC_REQUIRE(child >= 0);
  if (child == 0) {
    auto fs = zc::newDiskFilesystem();
    auto dir = openDir(*fs, base);
    Scenario scenario;
    VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
    LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
    VerifiedExecutableManifest manifest = buildManifest(candidate, base);
    ExitAtPublicationCheckpoint observer(checkpoint);
    (void)PublicationTransactionTestAccess::publishObserved(zc::mv(candidate), zc::mv(manifest),
                                                            observer);
    _exit(127);
  }
  int status = 0;
  ZC_REQUIRE(::waitpid(child, &status, 0) == child);
  ZC_REQUIRE(WIFEXITED(status));
  ZC_REQUIRE(WEXITSTATUS(status) == 64 + static_cast<int>(checkpoint));
}

ZC_TEST("publication recovery never infers ownership from a pre-Started token name") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-pre-started-recovery");
  auto dir = openDir(*fs, base);
  dir->openSubdir(zc::Path(".zomlink-00112233445566778899aabbccddeeff"_zc),
                  zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  ZC_EXPECT(dir->tryLstat(zc::Path(".zomlink-00112233445566778899aabbccddeeff"_zc)) != zc::none);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publication recovery rejects a checksum-corrupted journal chain without deleting") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ExecCommittedDurable, "corrupt-journal-chain"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  zc::String execJournal;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc) &&
        zc::StringPtr(name).endsWith(".exec-committed"_zc)) {
      execJournal = zc::heapString(name);
    }
  }
  ZC_REQUIRE(execJournal.size() > 0);
  auto journalFile = dir->openFile(zc::Path(execJournal), zc::WriteMode::MODIFY);
  zc::Array<zc::byte> bytes = journalFile->readAllBytes();
  ZC_REQUIRE(bytes.size() > 0);
  bytes[bytes.size() - 1] ^= 0x01;
  journalFile->writeAll(bytes.asPtr());
  journalFile->sync();
  dir->sync();

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  ZC_EXPECT(countSnapshotTrees(*dir) == 1u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publication recovery is a total function across every durable crash window") {
  const PublicationCheckpoint checkpoints[] = {
      PublicationCheckpoint::StartedDurable,
      PublicationCheckpoint::ManifestTemporaryDurable,
      PublicationCheckpoint::ManifestStagedDurable,
      PublicationCheckpoint::ExecutableRenamed,
      PublicationCheckpoint::ExecutableDirectoryDurable,
      PublicationCheckpoint::ExecCommittedDurable,
      PublicationCheckpoint::ManifestRenamed,
      PublicationCheckpoint::ManifestDirectoryDurable,
      PublicationCheckpoint::ManifestCommittedDurable,
  };
  const PublicationRecoveryStatus expected[] = {
      PublicationRecoveryStatus::Clean,     PublicationRecoveryStatus::ExplicitRepairRequired,
      PublicationRecoveryStatus::Clean,     PublicationRecoveryStatus::Clean,
      PublicationRecoveryStatus::Clean,     PublicationRecoveryStatus::Clean,
      PublicationRecoveryStatus::Published, PublicationRecoveryStatus::Published,
      PublicationRecoveryStatus::Published,
  };
  const zc::StringPtr suffixes[] = {
      "crash-started"_zc,          "crash-manifest-temp"_zc,   "crash-manifest-staged"_zc,
      "crash-exec-renamed"_zc,     "crash-exec-synced"_zc,     "crash-exec-committed"_zc,
      "crash-manifest-renamed"_zc, "crash-manifest-synced"_zc, "crash-manifest-committed"_zc,
  };

  for (size_t index = 0; index < 9; ++index) {
    zc::String base;
    PublicationRecoveryResult result =
        crashAndRecoverPublication(checkpoints[index], suffixes[index], base);
    ZC_EXPECT(result.status() == expected[index]);

    auto fs = zc::newDiskFilesystem();
    auto dir = openDir(*fs, base);
    const bool published = expected[index] == PublicationRecoveryStatus::Published;
    ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none == published);
    ZC_EXPECT(dir->tryLstat(zc::Path("app.zom-artifact"_zc)) != zc::none == published);
    if (expected[index] == PublicationRecoveryStatus::ExplicitRepairRequired) {
      bool foundRetainedTemp = false;
      for (const zc::String& name : dir->listNames()) {
        if (zc::StringPtr(name).startsWith(".zom-manifest."_zc)) { foundRetainedTemp = true; }
      }
      ZC_EXPECT(foundRetainedTemp);
    }
    fs->getRoot().remove(zc::Path::parse(base.slice(1)));
  }
}

ZC_TEST("publishLinkedOutput commits the executable first and the manifest as visibility marker") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-success");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  zc::Array<uint8_t> expectedManifest = ExecutableManifestCodec::encode(manifest);

  PublicationOutcome outcome = publishLinkedOutput(zc::mv(candidate), zc::mv(manifest));
  ZC_ASSERT(outcome.isPublished());
  PublishedExecutableArtifact artifact = zc::mv(outcome).takePublished();
  ZC_EXPECT(artifact.finalDestination() == zc::str(base, "/app"));
  // The success fixture now links a real ELF carrying the `zom` entry symbol (so
  // D5 executable inspection has a genuine image to inspect), not a 4-byte magic
  // stub: assert the committed executable is a non-trivial ELF by size and magic.
  const zc::Array<zc::byte> committedExecutable = dir->openFile(zc::Path("app"_zc))->readAllBytes();
  ZC_REQUIRE(committedExecutable.size() > 64);
  ZC_EXPECT(committedExecutable[0] == 0x7f && committedExecutable[1] == 'E' &&
            committedExecutable[2] == 'L' && committedExecutable[3] == 'F');
  // Bind the manifest read to a named local: comparing `readAllBytes().asPtr()`
  // against another `.asPtr()` would leave both views dangling once the full
  // expression's temporaries are destroyed.
  const zc::Array<zc::byte> committedManifest =
      dir->openFile(zc::Path("app.zom-artifact"_zc))->readAllBytes();
  ZC_EXPECT(committedManifest.asPtr() == expectedManifest.asPtr());
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);
  for (const zc::String& name : dir->listNames()) {
    ZC_EXPECT(!zc::StringPtr(name).startsWith("journal."_zc));
    ZC_EXPECT(!zc::StringPtr(name).startsWith(".zom-manifest."_zc));
  }

  PublicationRecoveryResult reopened = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(reopened.status() == PublicationRecoveryStatus::Published);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publishLinkedOutput rejects a manifest not live-bound to the candidate") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-mismatch");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  const VerifiedLinkPlan& heldPlan = candidate.plan();
  identity::Sha256Digest wrongDigest = digestOf(bytesOf("wrong-output"_zc).asPtr());
  identity::Sha256Digest toolchain =
      ExecutablePublicationManifestBinding::toolchainIdentity(heldPlan);
  ExecutableManifestRequest request{
      zc::heapString(heldPlan.outputPath()),
      zc::heapString(base),
      copyBytes(heldPlan.targetSpecificationIdentity()),
      wrongDigest,
      candidate.outputSize(),
      heldPlan.id(),
      ExecutablePublicationManifestBinding::inputArtifactDigests(heldPlan),
      copyBytes(toolchain.bytes())};
  IrOperationResult<VerifiedExecutableManifest> manifestResult =
      ExecutableManifestVerifier::verify(zc::mv(request));
  ZC_REQUIRE(manifestResult.isVerified());

  PublicationOutcome outcome =
      publishLinkedOutput(zc::mv(candidate), zc::mv(manifestResult).takeVerified());
  ZC_ASSERT(outcome.isRejected());
  PublicationRejection rejection = zc::mv(outcome).takeRejected();
  ZC_EXPECT(rejection.isIrInvariantRejected());
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(!dir->exists(zc::Path("app.zom-artifact"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publishLinkedOutput loses the manifest rename race without clobbering the competitor") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-manifest-race");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  ManifestCompetitorObserver observer(*dir);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.didCreate());
  ZC_ASSERT(outcome.isRejected());
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(dir->openFile(zc::Path("app.zom-artifact"_zc))->readAllText() ==
            "competitor-manifest"_zc);
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publishLinkedOutput never deletes an app replacement after the executable rename") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-app-replacement");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  ExecutableReplacementObserver observer(*dir);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.didReplace());
  ZC_ASSERT(outcome.isRecoveryRequired());
  LinkRecoveryRequired recovery = zc::mv(outcome).takeRecoveryRequired();
  ZC_ASSERT(recovery.isPublicationRecoveryRequired());
  PublicationRecoveryRequired publication = zc::mv(recovery).takePublication();
  ZC_EXPECT(publication.primary != zc::none);
  ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllText() == "competitor-app"_zc);
  ZC_EXPECT(!dir->exists(zc::Path("app.zom-artifact"_zc)));

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
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
  ZC_ASSERT(bytes.size() > 64u);
  ZC_EXPECT(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');
  // The candidate's captured size matches the bytes it holds, and its exact
  // identity records a sole hard link (st_nlink == 1).
  ZC_EXPECT(candidate.outputSize() == bytes.size());
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

ZC_TEST("linkAndPublish consumes a verified plan through ELF inspection and D1 publication") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-link-and-publish-success");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);

  PublicationOutcome outcome = linkAndPublish(zc::mv(plan), *fs);
  ZC_ASSERT(outcome.isPublished());
  PublishedExecutableArtifact artifact = zc::mv(outcome).takePublished();
  ZC_EXPECT(artifact.finalDestination() == zc::str(base, "/app"));
  ZC_EXPECT(dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(dir->exists(zc::Path("app.zom-artifact"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  PublicationRecoveryResult reopened = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(reopened.status() == PublicationRecoveryStatus::Published);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkAndPublish rejects a structurally linked but malformed executable image") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-link-and-publish-invalid-image");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_INVALID_IMAGE ""_zc, scenario);

  PublicationOutcome outcome = linkAndPublish(zc::mv(plan), *fs);
  ZC_ASSERT(outcome.isRejected());
  PublicationRejection rejection = zc::mv(outcome).takeRejected();
  ZC_REQUIRE(rejection.isIrInvariantRejected());
  ZC_REQUIRE(rejection.invariantFailures().facts().size() == 1);
  ZC_EXPECT(rejection.invariantFailures().facts()[0].phase() ==
            IrFailurePhase::ExecutablePublication);
  ZC_EXPECT(rejection.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidFact);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(!dir->exists(zc::Path("app.zom-artifact"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkAndPublish rejects an executable whose machine differs from the verified plan") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-link-and-publish-machine-mismatch");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario,
                                        ExecutableMachine::AArch64);

  PublicationOutcome outcome = linkAndPublish(zc::mv(plan), *fs);
  ZC_ASSERT(outcome.isRejected());
  PublicationRejection rejection = zc::mv(outcome).takeRejected();
  ZC_REQUIRE(rejection.isIrInvariantRejected());
  ZC_EXPECT(rejection.invariantFailures().facts()[0].phase() ==
            IrFailurePhase::ExecutablePublication);
  ZC_EXPECT(rejection.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidAbi);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("linkAndPublish rejects a missing required runtime symbol") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-link-and-publish-runtime-symbol");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario,
                                        ExecutableMachine::X86_64, "zom.missing"_zc);

  PublicationOutcome outcome = linkAndPublish(zc::mv(plan), *fs);
  ZC_ASSERT(outcome.isRejected());
  PublicationRejection rejection = zc::mv(outcome).takeRejected();
  ZC_REQUIRE(rejection.isIrInvariantRejected());
  ZC_EXPECT(rejection.invariantFailures().facts()[0].phase() ==
            IrFailurePhase::ExecutablePublication);
  ZC_EXPECT(rejection.invariantFailures().facts()[0].kind() == IrFailureKind::InvalidFact);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
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
  ZC_ASSERT(bytes.size() > 64u);
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
      elfInspectionProfile(),
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

ZC_TEST("an unrelated pre-Started root cannot hide a complete published pair") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-published-pair-with-unrelated-root");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  PublicationOutcome outcome = linkAndPublish(zc::mv(plan), *fs);
  ZC_REQUIRE(outcome.isPublished());

  constexpr zc::StringPtr unrelatedRoot = ".zomlink-ffeeddccbbaa99887766554433221100"_zc;
  dir->openSubdir(zc::Path(unrelatedRoot), zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::Published);
  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  ZC_EXPECT(dir->tryLstat(zc::Path("app.zom-artifact"_zc)) != zc::none);
  ZC_EXPECT(dir->tryLstat(zc::Path(unrelatedRoot)) != zc::none);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("malformed journal residue cannot hide a complete published pair") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-published-pair-with-malformed-journal");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  PublicationOutcome outcome = linkAndPublish(zc::mv(plan), *fs);
  ZC_REQUIRE(outcome.isPublished());

  constexpr zc::StringPtr malformedJournal = "journal.0123456789abcdef0123456789abcdef.started"_zc;
  dir->openFile(zc::Path(malformedJournal), zc::WriteMode::CREATE)
      ->writeAll("malformed-unrelated-journal"_zc);

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::Published);
  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  ZC_EXPECT(dir->tryLstat(zc::Path("app.zom-artifact"_zc)) != zc::none);
  ZC_EXPECT(dir->tryLstat(zc::Path(malformedJournal)) != zc::none);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publication recovery rejects a later journal stage whose Started owner proof is missing") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ExecCommittedDurable, "missing-started-chain"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);

  for (const zc::String& name : dir->listNames()) {
    zc::StringPtr text(name);
    if (text.startsWith("journal."_zc) && text.endsWith(".exec-committed"_zc)) { continue; }
    dir->remove(zc::Path(name));
  }

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  bool retainedExecStage = false;
  for (const zc::String& name : dir->listNames()) {
    zc::StringPtr text(name);
    if (text.startsWith("journal."_zc) && text.endsWith(".exec-committed"_zc)) {
      retainedExecStage = true;
    }
  }
  ZC_EXPECT(retainedExecStage);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publication recovery never deletes an executable from a ManifestCommitted pair") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ManifestCommittedDurable,
                     "manifest-committed-missing-manifest"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  ZC_REQUIRE(dir->tryRemove(zc::Path("app.zom-artifact"_zc)));

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("concurrent recovery cannot sweep a claimed but unverified competitor executable") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ExecutableRenamed,
                     "concurrent-recovery-claimed-competitor"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  zc::String rootLeaf = soleSnapshotTreeName(*dir);
  zc::String quarantineRoot = zc::str(rootLeaf, ".cleanup");

  ZC_REQUIRE(dir->tryRemove(zc::Path("app"_zc)));
  dir->openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("competitor-app"_zc);
  dir->sync();

  int readyPipe[2];
  int releasePipe[2];
  ZC_REQUIRE(::pipe(readyPipe) == 0);
  ZC_REQUIRE(::pipe(releasePipe) == 0);
  pid_t recoveryA = ::fork();
  ZC_REQUIRE(recoveryA >= 0);
  if (recoveryA == 0) {
    (void)::close(readyPipe[0]);
    (void)::close(releasePipe[1]);
    auto childFs = zc::newDiskFilesystem();
    BlockAtPublicationCheckpoint observer(
        PublicationCheckpoint::ExecutableClaimedBeforeVerification, readyPipe[1], releasePipe[0]);
    PublicationRecoveryResult result = PublicationTransactionTestAccess::recoverObserved(
        *childFs, zc::str(base, "/app"), observer);
    _exit(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired ? 0 : 122);
  }

  (void)::close(readyPipe[1]);
  (void)::close(releasePipe[0]);
  char ready = 0;
  ZC_REQUIRE(::read(readyPipe[0], &ready, 1) == 1);
  ZC_REQUIRE(ready == 'r');
  (void)::close(readyPipe[0]);

  // Recovery A owns the rename but has not proved that the quarantined app is
  // this transaction's output. Recovery B must therefore retain the closed
  // quarantine slot and all owner evidence rather than treating ENOENT at the
  // public name as proof that cleanup already completed.
  PublicationRecoveryResult recoveryB = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(recoveryB.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  auto quarantineMaybe = dir->tryOpenSubdir(zc::Path(quarantineRoot), zc::WriteMode::MODIFY);
  ZC_EXPECT(quarantineMaybe != zc::none);
  if (quarantineMaybe != zc::none) {
    auto quarantine = ZC_REQUIRE_NONNULL(zc::mv(quarantineMaybe));
    auto claimedMaybe = quarantine->tryOpenFile(zc::Path("publication-executable-cleanup"_zc));
    ZC_EXPECT(claimedMaybe != zc::none);
    if (claimedMaybe != zc::none) {
      auto claimed = ZC_REQUIRE_NONNULL(zc::mv(claimedMaybe));
      ZC_EXPECT(claimed->readAllText() == "competitor-app"_zc);
    }
  }
  bool journalRetainedWhileClaimed = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedWhileClaimed = true; }
  }
  ZC_EXPECT(journalRetainedWhileClaimed);

  const char release = 'g';
  ZC_REQUIRE(::write(releasePipe[1], &release, 1) == 1);
  (void)::close(releasePipe[1]);
  int status = 0;
  ZC_REQUIRE(::waitpid(recoveryA, &status, 0) == recoveryA);
  ZC_REQUIRE(WIFEXITED(status));
  ZC_EXPECT(WEXITSTATUS(status) == 0);

  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  if (dir->tryLstat(zc::Path("app"_zc)) != zc::none) {
    ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllText() == "competitor-app"_zc);
  }
  ZC_EXPECT(dir->tryLstat(zc::Path(quarantineRoot)) != zc::none);
  bool journalRetainedAfterRestore = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedAfterRestore = true; }
  }
  ZC_EXPECT(journalRetainedAfterRestore);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("source absence cannot authorize sweeping another recovery's unverified claim") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ExecutableRenamed,
                     "concurrent-recovery-source-absence"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  zc::String rootLeaf = soleSnapshotTreeName(*dir);
  zc::String quarantineRoot = zc::str(rootLeaf, ".cleanup");

  ZC_REQUIRE(dir->tryRemove(zc::Path("app"_zc)));
  dir->openFile(zc::Path("app"_zc), zc::WriteMode::CREATE)->writeAll("competitor-app"_zc);
  dir->sync();

  // Pin recovery B after it has observed the public app and decided that it
  // must claim it, but before its rename. Recovery A can then win that rename
  // and pause before owner verification, forcing B through rename-ENOENT while
  // B's stale claimExecutable decision remains true.
  int beforeReadyPipe[2];
  int beforeReleasePipe[2];
  ZC_REQUIRE(::pipe(beforeReadyPipe) == 0);
  ZC_REQUIRE(::pipe(beforeReleasePipe) == 0);
  pid_t recoveryB = ::fork();
  ZC_REQUIRE(recoveryB >= 0);
  if (recoveryB == 0) {
    (void)::close(beforeReadyPipe[0]);
    (void)::close(beforeReleasePipe[1]);
    auto childFs = zc::newDiskFilesystem();
    BlockAtPublicationCheckpoint observer(PublicationCheckpoint::ExecutableClaimAboutToRename,
                                          beforeReadyPipe[1], beforeReleasePipe[0]);
    PublicationRecoveryResult result = PublicationTransactionTestAccess::recoverObserved(
        *childFs, zc::str(base, "/app"), observer);
    _exit(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired ? 0 : 123);
  }
  (void)::close(beforeReadyPipe[1]);
  (void)::close(beforeReleasePipe[0]);
  char beforeReady = 0;
  ZC_REQUIRE(::read(beforeReadyPipe[0], &beforeReady, 1) == 1);
  ZC_REQUIRE(beforeReady == 'r');
  (void)::close(beforeReadyPipe[0]);

  int claimedReadyPipe[2];
  int claimedReleasePipe[2];
  ZC_REQUIRE(::pipe(claimedReadyPipe) == 0);
  ZC_REQUIRE(::pipe(claimedReleasePipe) == 0);
  pid_t recoveryA = ::fork();
  ZC_REQUIRE(recoveryA >= 0);
  if (recoveryA == 0) {
    (void)::close(beforeReleasePipe[1]);
    (void)::close(claimedReadyPipe[0]);
    (void)::close(claimedReleasePipe[1]);
    auto childFs = zc::newDiskFilesystem();
    BlockAtPublicationCheckpoint observer(
        PublicationCheckpoint::ExecutableClaimedBeforeVerification, claimedReadyPipe[1],
        claimedReleasePipe[0]);
    PublicationRecoveryResult result = PublicationTransactionTestAccess::recoverObserved(
        *childFs, zc::str(base, "/app"), observer);
    _exit(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired ? 0 : 124);
  }
  (void)::close(claimedReadyPipe[1]);
  (void)::close(claimedReleasePipe[0]);
  char claimedReady = 0;
  ZC_REQUIRE(::read(claimedReadyPipe[0], &claimedReady, 1) == 1);
  ZC_REQUIRE(claimedReady == 'r');
  (void)::close(claimedReadyPipe[0]);

  const char release = 'g';
  ZC_REQUIRE(::write(beforeReleasePipe[1], &release, 1) == 1);
  (void)::close(beforeReleasePipe[1]);
  int recoveryBStatus = 0;
  ZC_REQUIRE(::waitpid(recoveryB, &recoveryBStatus, 0) == recoveryB);
  ZC_EXPECT(WIFEXITED(recoveryBStatus));
  if (WIFEXITED(recoveryBStatus)) { ZC_EXPECT(WEXITSTATUS(recoveryBStatus) == 0); }

  // B must not interpret its rename ENOENT as an already-clean entry. A still
  // owns an in-flight, unverified claim in the closed slot, so B retains the
  // competitor and the journal/root owner evidence.
  auto quarantineMaybe = dir->tryOpenSubdir(zc::Path(quarantineRoot), zc::WriteMode::MODIFY);
  ZC_EXPECT(quarantineMaybe != zc::none);
  if (quarantineMaybe != zc::none) {
    auto quarantine = ZC_REQUIRE_NONNULL(zc::mv(quarantineMaybe));
    ZC_EXPECT(quarantine->openFile(zc::Path("publication-executable-cleanup"_zc))->readAllText() ==
              "competitor-app"_zc);
  }
  bool journalRetainedWhileClaimed = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedWhileClaimed = true; }
  }
  ZC_EXPECT(journalRetainedWhileClaimed);

  ZC_REQUIRE(::write(claimedReleasePipe[1], &release, 1) == 1);
  (void)::close(claimedReleasePipe[1]);
  int recoveryAStatus = 0;
  ZC_REQUIRE(::waitpid(recoveryA, &recoveryAStatus, 0) == recoveryA);
  ZC_REQUIRE(WIFEXITED(recoveryAStatus));
  ZC_EXPECT(WEXITSTATUS(recoveryAStatus) == 0);

  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  if (dir->tryLstat(zc::Path("app"_zc)) != zc::none) {
    ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllText() == "competitor-app"_zc);
  }
  ZC_EXPECT(dir->tryLstat(zc::Path(quarantineRoot)) != zc::none);
  bool journalRetainedAfterRestore = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedAfterRestore = true; }
  }
  ZC_EXPECT(journalRetainedAfterRestore);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publication root cleanup cannot sweep a recovery's unverified claim") {
  zc::String base = zc::str(tempDirPath(), "-publication-cleanup-vs-recovery-claim");
  auto setupFs = zc::newDiskFilesystem();
  auto setupDir = openDir(*setupFs, base);
  (void)setupDir;

  // Pause the in-flight publisher after app has been renamed but before the
  // ExecCommitted record. This leaves a durable ManifestStaged chain that a
  // protocol-following recovery worker can inspect.
  int publicationReadyPipe[2];
  int publicationReleasePipe[2];
  ZC_REQUIRE(::pipe(publicationReadyPipe) == 0);
  ZC_REQUIRE(::pipe(publicationReleasePipe) == 0);
  pid_t publisher = ::fork();
  ZC_REQUIRE(publisher >= 0);
  if (publisher == 0) {
    (void)::close(publicationReadyPipe[0]);
    (void)::close(publicationReleasePipe[1]);
    auto childFs = zc::newDiskFilesystem();
    auto childDir = openDir(*childFs, base);
    Scenario scenario;
    VerifiedLinkPlan plan = buildScenario(*childDir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
    LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *childFs);
    VerifiedExecutableManifest manifest = buildManifest(candidate, base);
    BlockAtPublicationCheckpoint observer(PublicationCheckpoint::ExecutableRenamed,
                                          publicationReadyPipe[1], publicationReleasePipe[0]);
    PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
        zc::mv(candidate), zc::mv(manifest), observer);
    _exit(outcome.isRecoveryRequired() ? 0 : 125);
  }
  (void)::close(publicationReadyPipe[1]);
  (void)::close(publicationReleasePipe[0]);
  char publicationReady = 0;
  ZC_REQUIRE(::read(publicationReadyPipe[0], &publicationReady, 1) == 1);
  ZC_REQUIRE(publicationReady == 'r');
  (void)::close(publicationReadyPipe[0]);

  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  zc::String rootLeaf = soleSnapshotTreeName(*dir);
  zc::String quarantineRoot = zc::str(rootLeaf, ".cleanup");
  zc::Maybe<int> outputDirFd = dir->getFd();
  ZC_REQUIRE(outputDirFd != zc::none);
  int appFd = ::openat(ZC_REQUIRE_NONNULL(outputDirFd), "app", O_WRONLY | O_TRUNC | O_CLOEXEC);
  ZC_REQUIRE(appFd >= 0);
  constexpr char competitor[] = "competitor-app";
  ZC_REQUIRE(::write(appFd, competitor, sizeof(competitor) - 1) ==
             static_cast<ssize_t>(sizeof(competitor) - 1));
  ZC_REQUIRE(::fsync(appFd) == 0);
  ZC_REQUIRE(::close(appFd) == 0);
  dir->sync();

  // Recovery atomically claims the competitor into the same transaction root
  // and pauses before proving ownership. The publisher still holds that root's
  // directory capability, so its later cleanup must not interpret the missing
  // original root name as authority to remove contents through the capability.
  int recoveryReadyPipe[2];
  int recoveryReleasePipe[2];
  ZC_REQUIRE(::pipe(recoveryReadyPipe) == 0);
  ZC_REQUIRE(::pipe(recoveryReleasePipe) == 0);
  pid_t recovery = ::fork();
  ZC_REQUIRE(recovery >= 0);
  if (recovery == 0) {
    (void)::close(publicationReleasePipe[1]);
    (void)::close(recoveryReadyPipe[0]);
    (void)::close(recoveryReleasePipe[1]);
    auto childFs = zc::newDiskFilesystem();
    BlockAtPublicationCheckpoint observer(
        PublicationCheckpoint::ExecutableClaimedBeforeVerification, recoveryReadyPipe[1],
        recoveryReleasePipe[0]);
    PublicationRecoveryResult result = PublicationTransactionTestAccess::recoverObserved(
        *childFs, zc::str(base, "/app"), observer);
    _exit(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired ? 0 : 126);
  }
  (void)::close(recoveryReadyPipe[1]);
  (void)::close(recoveryReleasePipe[0]);
  char recoveryReady = 0;
  ZC_REQUIRE(::read(recoveryReadyPipe[0], &recoveryReady, 1) == 1);
  ZC_REQUIRE(recoveryReady == 'r');
  (void)::close(recoveryReadyPipe[0]);

  const char release = 'g';
  ZC_REQUIRE(::write(publicationReleasePipe[1], &release, 1) == 1);
  (void)::close(publicationReleasePipe[1]);
  int publisherStatus = 0;
  ZC_REQUIRE(::waitpid(publisher, &publisherStatus, 0) == publisher);
  ZC_EXPECT(WIFEXITED(publisherStatus));
  if (WIFEXITED(publisherStatus)) { ZC_EXPECT(WEXITSTATUS(publisherStatus) == 0); }

  auto quarantineMaybe = dir->tryOpenSubdir(zc::Path(quarantineRoot), zc::WriteMode::MODIFY);
  ZC_EXPECT(quarantineMaybe != zc::none);
  if (quarantineMaybe != zc::none) {
    auto quarantine = ZC_REQUIRE_NONNULL(zc::mv(quarantineMaybe));
    auto claimedMaybe = quarantine->tryOpenFile(zc::Path("publication-executable-cleanup"_zc));
    ZC_EXPECT(claimedMaybe != zc::none);
    if (claimedMaybe != zc::none) {
      auto claimed = ZC_REQUIRE_NONNULL(zc::mv(claimedMaybe));
      ZC_EXPECT(claimed->readAllText() == "competitor-app"_zc);
    }
  }
  bool journalRetainedWhileClaimed = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedWhileClaimed = true; }
  }
  ZC_EXPECT(journalRetainedWhileClaimed);

  ZC_REQUIRE(::write(recoveryReleasePipe[1], &release, 1) == 1);
  (void)::close(recoveryReleasePipe[1]);
  int recoveryStatus = 0;
  ZC_REQUIRE(::waitpid(recovery, &recoveryStatus, 0) == recovery);
  ZC_REQUIRE(WIFEXITED(recoveryStatus));
  ZC_EXPECT(WEXITSTATUS(recoveryStatus) == 0);

  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  if (dir->tryLstat(zc::Path("app"_zc)) != zc::none) {
    ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllText() == "competitor-app"_zc);
  }
  ZC_EXPECT(dir->tryLstat(zc::Path(quarantineRoot)) != zc::none);
  bool journalRetainedAfterRestore = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedAfterRestore = true; }
  }
  ZC_EXPECT(journalRetainedAfterRestore);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("a top-level claimed root retains a recovery's late fixed-slot claim") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ExecutableRenamed,
                     "claimed-root-late-recovery-claim"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  zc::String rootLeaf = soleSnapshotTreeName(*dir);
  zc::String quarantineRoot = zc::str(rootLeaf, ".cleanup");
  zc::Maybe<int> outputDirFd = dir->getFd();
  ZC_REQUIRE(outputDirFd != zc::none);

  // Keep the journal-recorded inode but drift its bytes, then hide the public
  // name. Recovery A therefore decides there is no executable to claim and can
  // atomically claim the transaction root before recovery B makes this same
  // inode visible again.
  int appFd = ::openat(ZC_REQUIRE_NONNULL(outputDirFd), "app", O_WRONLY | O_TRUNC | O_CLOEXEC);
  ZC_REQUIRE(appFd >= 0);
  constexpr char competitor[] = "competitor-app";
  ZC_REQUIRE(::write(appFd, competitor, sizeof(competitor) - 1) ==
             static_cast<ssize_t>(sizeof(competitor) - 1));
  ZC_REQUIRE(::fsync(appFd) == 0);
  ZC_REQUIRE(::close(appFd) == 0);
  ZC_REQUIRE(::renameat(ZC_REQUIRE_NONNULL(outputDirFd), "app", ZC_REQUIRE_NONNULL(outputDirFd),
                        "late-app") == 0);
  dir->sync();

  int sweepReadyPipe[2];
  int sweepReleasePipe[2];
  ZC_REQUIRE(::pipe(sweepReadyPipe) == 0);
  ZC_REQUIRE(::pipe(sweepReleasePipe) == 0);
  pid_t recoveryA = ::fork();
  ZC_REQUIRE(recoveryA >= 0);
  if (recoveryA == 0) {
    (void)::close(sweepReadyPipe[0]);
    (void)::close(sweepReleasePipe[1]);
    auto childFs = zc::newDiskFilesystem();
    BlockAtPublicationCheckpoint observer(
        PublicationCheckpoint::TransactionRootContentsAboutToSweep, sweepReadyPipe[1],
        sweepReleasePipe[0]);
    PublicationRecoveryResult result = PublicationTransactionTestAccess::recoverObserved(
        *childFs, zc::str(base, "/app"), observer);
    _exit(result.status() == PublicationRecoveryStatus::RecoveryRequired ? 0 : 127);
  }
  (void)::close(sweepReadyPipe[1]);
  (void)::close(sweepReleasePipe[0]);
  char sweepReady = 0;
  ZC_REQUIRE(::read(sweepReadyPipe[0], &sweepReady, 1) == 1);
  ZC_REQUIRE(sweepReady == 'r');
  (void)::close(sweepReadyPipe[0]);

  // A has successfully renamed the exact root to its top-level cleanup name and
  // re-verified its identity. Re-expose the drifted, journal-recorded inode only
  // now, so B's claim lands in the fixed slot after A's pre-sweep decisions.
  ZC_EXPECT(dir->tryLstat(zc::Path(rootLeaf)) == zc::none);
  ZC_REQUIRE(dir->tryLstat(zc::Path(quarantineRoot)) != zc::none);
  ZC_REQUIRE(::renameat(ZC_REQUIRE_NONNULL(outputDirFd), "late-app",
                        ZC_REQUIRE_NONNULL(outputDirFd), "app") == 0);
  dir->sync();

  int claimedReadyPipe[2];
  int claimedReleasePipe[2];
  ZC_REQUIRE(::pipe(claimedReadyPipe) == 0);
  ZC_REQUIRE(::pipe(claimedReleasePipe) == 0);
  pid_t recoveryB = ::fork();
  ZC_REQUIRE(recoveryB >= 0);
  if (recoveryB == 0) {
    (void)::close(sweepReleasePipe[1]);
    (void)::close(claimedReadyPipe[0]);
    (void)::close(claimedReleasePipe[1]);
    auto childFs = zc::newDiskFilesystem();
    BlockAtPublicationCheckpoint observer(
        PublicationCheckpoint::ExecutableClaimedBeforeVerification, claimedReadyPipe[1],
        claimedReleasePipe[0]);
    PublicationRecoveryResult result = PublicationTransactionTestAccess::recoverObserved(
        *childFs, zc::str(base, "/app"), observer);
    _exit(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired ? 0 : 128);
  }
  (void)::close(claimedReadyPipe[1]);
  (void)::close(claimedReleasePipe[0]);
  char claimedReady = 0;
  ZC_REQUIRE(::read(claimedReadyPipe[0], &claimedReady, 1) == 1);
  ZC_REQUIRE(claimedReady == 'r');
  (void)::close(claimedReadyPipe[0]);

  // B now holds the already top-level-claimed root and has moved the competitor
  // into the fixed executable slot, but has not verified its digest. A's generic
  // sweep must skip that slot; its non-recursive rmdir must fail on the retained
  // entry and return an explicit cleanup debt rather than deleting evidence.
  const char release = 'g';
  ZC_REQUIRE(::write(sweepReleasePipe[1], &release, 1) == 1);
  (void)::close(sweepReleasePipe[1]);
  int recoveryAStatus = 0;
  ZC_REQUIRE(::waitpid(recoveryA, &recoveryAStatus, 0) == recoveryA);
  ZC_REQUIRE(WIFEXITED(recoveryAStatus));
  ZC_EXPECT(WEXITSTATUS(recoveryAStatus) == 0);

  auto quarantineMaybe = dir->tryOpenSubdir(zc::Path(quarantineRoot), zc::WriteMode::MODIFY);
  ZC_EXPECT(quarantineMaybe != zc::none);
  if (quarantineMaybe != zc::none) {
    auto quarantine = ZC_REQUIRE_NONNULL(zc::mv(quarantineMaybe));
    auto claimedMaybe = quarantine->tryOpenFile(zc::Path("publication-executable-cleanup"_zc));
    ZC_EXPECT(claimedMaybe != zc::none);
    if (claimedMaybe != zc::none) {
      ZC_EXPECT(ZC_REQUIRE_NONNULL(zc::mv(claimedMaybe))->readAllText() == "competitor-app"_zc);
    }
  }
  bool journalRetainedWhileClaimed = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedWhileClaimed = true; }
  }
  ZC_EXPECT(journalRetainedWhileClaimed);

  ZC_REQUIRE(::write(claimedReleasePipe[1], &release, 1) == 1);
  (void)::close(claimedReleasePipe[1]);
  int recoveryBStatus = 0;
  ZC_REQUIRE(::waitpid(recoveryB, &recoveryBStatus, 0) == recoveryB);
  ZC_REQUIRE(WIFEXITED(recoveryBStatus));
  ZC_EXPECT(WEXITSTATUS(recoveryBStatus) == 0);

  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  if (dir->tryLstat(zc::Path("app"_zc)) != zc::none) {
    ZC_EXPECT(dir->openFile(zc::Path("app"_zc))->readAllText() == "competitor-app"_zc);
  }
  ZC_EXPECT(dir->tryLstat(zc::Path(quarantineRoot)) != zc::none);
  bool journalRetainedAfterRestore = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { journalRetainedAfterRestore = true; }
  }
  ZC_EXPECT(journalRetainedAfterRestore);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publication recovery retains owner evidence when an orphan app has no transaction root") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ExecCommittedDurable, "exec-committed-missing-root"_zc,
                     base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zomlink-"_zc)) { dir->remove(zc::Path(name)); }
  }
  dir->sync();
  ZC_REQUIRE(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  ZC_REQUIRE(countSnapshotTrees(*dir) == 0u);

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  ZC_EXPECT(dir->tryLstat(zc::Path("app"_zc)) != zc::none);
  bool retainedJournal = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { retainedJournal = true; }
  }
  ZC_EXPECT(retainedJournal);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("Started recovery quarantines its matching app but retains an unproved manifest temp") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ExecutableRenamed, "started-with-app"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  for (const zc::String& name : dir->listNames()) {
    zc::StringPtr text(name);
    if (text.startsWith("journal."_zc) && text.endsWith(".manifest-staged"_zc)) {
      ZC_REQUIRE(dir->tryRemove(zc::Path(name)));
    }
  }

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  bool retainedManifestTemp = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zom-manifest."_zc)) { retainedManifestTemp = true; }
  }
  ZC_EXPECT(retainedManifestTemp);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("ManifestStaged recovery never deletes a replacement manifest temp") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ManifestStagedDurable,
                     "manifest-staged-temp-replacement"_zc, base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  zc::String manifestTemp;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".zom-manifest."_zc)) {
      manifestTemp = zc::heapString(name);
    }
  }
  ZC_REQUIRE(manifestTemp.size() > 0);
  ZC_REQUIRE(dir->tryRemove(zc::Path(manifestTemp)));
  dir->openFile(zc::Path(manifestTemp), zc::WriteMode::CREATE)
      ->writeAll("competitor-manifest-temp"_zc);

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  ZC_EXPECT(dir->openFile(zc::Path(manifestTemp))->readAllText() == "competitor-manifest-temp"_zc);
  ZC_EXPECT(countSnapshotTrees(*dir) == 1u);
  bool retainedJournal = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { retainedJournal = true; }
  }
  ZC_EXPECT(retainedJournal);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("ExecCommitted recovery retains a non-verifying final manifest") {
  zc::String base;
  crashPublicationAt(PublicationCheckpoint::ManifestRenamed, "exec-committed-invalid-manifest"_zc,
                     base);
  auto fs = zc::newDiskFilesystem();
  auto dir = openDir(*fs, base);
  dir->openFile(zc::Path("app.zom-artifact"_zc), zc::WriteMode::MODIFY)
      ->writeAll("invalid-manifest"_zc);

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  ZC_EXPECT(dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(dir->openFile(zc::Path("app.zom-artifact"_zc))->readAllText() == "invalid-manifest"_zc);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publication recovery rejects two valid Started chains for the same final target") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-two-started-chains");
  auto dir = openDir(*fs, base);
  (void)dir;
  crashPublicationInBaseAt(PublicationCheckpoint::StartedDurable, base);
  crashPublicationInBaseAt(PublicationCheckpoint::StartedDurable, base);

  PublicationRecoveryResult result = recoverLinkedOutputPublication(*fs, zc::str(base, "/app"));
  ZC_EXPECT(result.status() == PublicationRecoveryStatus::ExplicitRepairRequired);
  auto reopened = openDir(*fs, base);
  size_t startedCount = 0;
  for (const zc::String& name : reopened->listNames()) {
    zc::StringPtr text(name);
    if (text.startsWith("journal."_zc) && text.endsWith(".started"_zc)) { ++startedCount; }
  }
  ZC_EXPECT(startedCount == 2u);
  ZC_EXPECT(countSnapshotTrees(*reopened) == 2u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publishLinkedOutput never mints Published after the final manifest drifts") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-final-manifest-drift");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  FinalManifestCorruptionObserver observer(*dir);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.didCorrupt());
  ZC_ASSERT(outcome.isRecoveryRequired());
  LinkRecoveryRequired recovery = zc::mv(outcome).takeRecoveryRequired();
  ZC_ASSERT(recovery.isPublicationRecoveryRequired());
  PublicationRecoveryRequired publication = zc::mv(recovery).takePublication();
  ZC_EXPECT(publication.primary != zc::none);
  ZC_EXPECT(dir->openFile(zc::Path("app.zom-artifact"_zc))->readAllText() == "corrupt"_zc);
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publishLinkedOutput never deletes a competitor Started journal") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-started-journal-race");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  zc::String tree = soleSnapshotTreeName(*dir);
  zc::StringPtr treeView(tree);
  ZC_REQUIRE(treeView.startsWith(".zomlink-"_zc));
  zc::String startedLeaf = zc::str("journal.", treeView.slice(9), ".started");
  dir->openFile(zc::Path(startedLeaf), zc::WriteMode::CREATE)->writeAll("competitor-started"_zc);

  PublicationOutcome outcome = publishLinkedOutput(zc::mv(candidate), zc::mv(manifest));
  ZC_ASSERT(outcome.isRecoveryRequired());
  LinkRecoveryRequired recovery = zc::mv(outcome).takeRecoveryRequired();
  ZC_ASSERT(recovery.isPublicationRecoveryRequired());
  PublicationRecoveryRequired publication = zc::mv(recovery).takePublication();
  ZC_EXPECT(publication.primary != zc::none);
  ZC_EXPECT(dir->openFile(zc::Path(startedLeaf))->readAllText() == "competitor-started"_zc);
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publishLinkedOutput never adopts a competitor ManifestStaged journal") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-manifest-staged-journal-race");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  ManifestStagedJournalCompetitorObserver observer(*dir);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.didCreate());
  ZC_ASSERT(outcome.isRecoveryRequired());
  LinkRecoveryRequired recovery = zc::mv(outcome).takeRecoveryRequired();
  ZC_ASSERT(recovery.isPublicationRecoveryRequired());
  PublicationRecoveryRequired publication = zc::mv(recovery).takePublication();
  ZC_EXPECT(publication.primary != zc::none);
  bool competitorSurvived = false;
  for (const zc::String& name : dir->listNames()) {
    zc::StringPtr text(name);
    if (text.startsWith("journal."_zc) && text.endsWith(".manifest-staged"_zc)) {
      competitorSurvived =
          dir->openFile(zc::Path(name))->readAllText() == "competitor-manifest-staged"_zc;
    }
  }
  ZC_EXPECT(competitorSurvived);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("publishLinkedOutput rejects an output directory writable by another Unix principal") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-untrusted-output-directory");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  ZC_REQUIRE(::chmod(base.cStr(), 0777) == 0);

  PublicationOutcome outcome = publishLinkedOutput(zc::mv(candidate), zc::mv(manifest));
  ZC_ASSERT(outcome.isRejected());
  PublicationRejection rejection = zc::mv(outcome).takeRejected();
  ZC_EXPECT(rejection.isCapabilityRejected());
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(!dir->exists(zc::Path("app.zom-artifact"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  ZC_REQUIRE(::chmod(base.cStr(), 0700) == 0);
  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("a Started journal directory-sync failure returns recovery with the installed record") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-started-sync-fault");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  PublicationFaultObserver observer(PublicationFaultPoint::JournalDirectorySync);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.firedAll());
  ZC_ASSERT(outcome.isRecoveryRequired());
  bool retainedStarted = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc) &&
        zc::StringPtr(name).endsWith(".started"_zc)) {
      retainedStarted = true;
    }
  }
  ZC_EXPECT(retainedStarted);
  ZC_EXPECT(!dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("a failed journal write and temporary cleanup never returns ordinary Rejected") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-journal-temp-cleanup-fault");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  PublicationFaultObserver observer(PublicationFaultPoint::JournalWrite,
                                    PublicationFaultPoint::JournalTemporaryCleanup, true);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.firedAll());
  ZC_ASSERT(outcome.isRecoveryRequired());
  LinkRecoveryRequired recovery = zc::mv(outcome).takeRecoveryRequired();
  ZC_ASSERT(recovery.isPublicationRecoveryRequired());
  PublicationRecoveryRequired publication = zc::mv(recovery).takePublication();
  ZC_EXPECT(static_cast<uint8_t>(publication.obligation.lastDurableStage()) == 0u);
  bool retainedTemporary = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith(".journal."_zc) && zc::StringPtr(name).endsWith(".tmp"_zc)) {
      retainedTemporary = true;
    }
  }
  ZC_EXPECT(retainedTemporary);
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("a final manifest directory-sync failure preserves the pair and returns outcome pending") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-final-sync-fault");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  PublicationFaultObserver observer(PublicationFaultPoint::FinalManifestDirectorySync);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.firedAll());
  ZC_ASSERT(outcome.isRecoveryRequired());
  LinkRecoveryRequired recovery = zc::mv(outcome).takeRecoveryRequired();
  ZC_ASSERT(recovery.isPublicationRecoveryRequired());
  PublicationRecoveryRequired publication = zc::mv(recovery).takePublication();
  ZC_EXPECT(publication.primary == zc::none);
  ZC_EXPECT(dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(dir->exists(zc::Path("app.zom-artifact"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

ZC_TEST("a committed-pair journal cleanup failure returns recovery and retains the journal") {
  auto fs = zc::newDiskFilesystem();
  zc::String base = zc::str(tempDirPath(), "-publish-journal-cleanup-fault");
  auto dir = openDir(*fs, base);
  Scenario scenario;
  VerifiedLinkPlan plan = buildScenario(*dir, base, ZOM_FAKE_LINKER_SUCCESS ""_zc, scenario);
  LinkedOutputCandidate candidate = linkExpectingCandidate(zc::mv(plan), *fs);
  VerifiedExecutableManifest manifest = buildManifest(candidate, base);
  PublicationFaultObserver observer(PublicationFaultPoint::JournalChainRemove);

  PublicationOutcome outcome = PublicationTransactionTestAccess::publishObserved(
      zc::mv(candidate), zc::mv(manifest), observer);
  ZC_ASSERT(observer.firedAll());
  ZC_ASSERT(outcome.isRecoveryRequired());
  bool retainedJournal = false;
  for (const zc::String& name : dir->listNames()) {
    if (zc::StringPtr(name).startsWith("journal."_zc)) { retainedJournal = true; }
  }
  ZC_EXPECT(retainedJournal);
  ZC_EXPECT(dir->exists(zc::Path("app"_zc)));
  ZC_EXPECT(dir->exists(zc::Path("app.zom-artifact"_zc)));
  ZC_EXPECT(countSnapshotTrees(*dir) == 0u);

  fs->getRoot().remove(zc::Path::parse(base.slice(1)));
}

}  // namespace
}  // namespace zomlang::compiler::ir
