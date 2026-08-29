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
// See the License for the specific language governing permissions and limitations under
// the License.

// RFC 0043 D3b internal surface. This header is NOT part of the public linker
// API: it is compiled into `invoke-linker.cc` and into the `invoke-linker-test`
// target only, never installed and never included by an ordinary production
// translation unit. It exposes the transaction-private snapshot capability
// `PreparedLinkInputs`, the transaction-token seam `SnapshotTokenSource`, and the
// test peer `LinkerInvocationTestAccess`. The public header
// (`invoke-linker.h`) names none of these; production code reaches the linker
// only through `linkExecutable`.
//
// Keeping this out of the public surface is a security boundary, not a style
// choice: if `PreparedLinkInputs::prepare` / `discardAndCleanup` and a public
// `LinkerInvocationTestAccess` were reachable from production, a caller could
// mint a snapshot transaction with a scripted token or drop a prepared
// capability bare (bypassing the structured cleanup obligation), defeating the
// exact-identity ownership guarantees the capability exists to enforce.

#pragma once

#include <cstdint>

#include "compiler/ir/invoke-linker.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

namespace detail {
/// \brief The attorney/factory that mints the restricted snapshot record types
///        declared in the public header (`SnapshotTransactionId`,
///        `StableDirectoryIdentity`, `SnapshotCleanupObligation`). Those types
///        friend exactly this factory, so only the snapshot machinery - which is
///        confined to this internal surface and the implementation file - can
///        construct them. Production code sees the records but can never forge
///        one.
class LinkSnapshotMinter {
public:
  ZC_NODISCARD static zc::Maybe<SnapshotTransactionId> transactionId(
      zc::ArrayPtr<const uint8_t> bytes) {
    return SnapshotTransactionId::fromBytes(bytes);
  }
  ZC_NODISCARD static zc::Maybe<StableDirectoryIdentity> identity(uint64_t device, uint64_t inode) {
    return StableDirectoryIdentity(device, inode);
  }
  ZC_NODISCARD static StableFileIdentity fileIdentity(uint64_t device, uint64_t inode,
                                                      uint64_t linkCount) {
    return StableFileIdentity(device, inode, linkCount);
  }
  ZC_NODISCARD static SnapshotCleanupObligation obligation(
      const SnapshotTransactionId& transactionId, zc::String&& outputParent,
      zc::Maybe<StableDirectoryIdentity> directoryIdentity, const LinkPlanId& planId,
      CleanupFailureKind kind, CleanupStage stage) noexcept {
    return SnapshotCleanupObligation(transactionId, zc::mv(outputParent), zc::mv(directoryIdentity),
                                     planId, kind, stage);
  }
};
}  // namespace detail

/// \brief A source of snapshot transaction tokens. Production uses the OS CSPRNG;
///        a test can supply a scripted sequence (for example a colliding token
///        followed by a fresh one) to exercise the exclusive-create retry.
///
/// This type exists only in the internal surface, so a production translation
/// unit cannot subclass it to inject a predictable token into a real link.
class SnapshotTokenSource {
public:
  virtual ~SnapshotTokenSource() noexcept = default;
  /// \brief Fills `out` with 16 token bytes. Returns false to model a CSPRNG
  ///        failure (fail closed).
  virtual bool nextToken(uint8_t (&out)[16]) = 0;
};

/// \brief A transaction-private, re-verified snapshot of every link input plus
///        the rewritten driver invocation that consumes it.
///
/// RFC 0043 D3b defeats the input side of the link TOCTOU. `linkExecutable`
/// re-reads the driver by pathname just before spawning, but the driver then
/// re-opens each input (and, without this, itself) by pathname at exec time, so
/// an inode swapped between verification and exec would be linked. This
/// capability closes that window:
///
///   1. Every input the plan names - the driver, the closure CRT objects and
///      default libraries, and the plan's object and runtime records - is read
///      from the source filesystem, verified against the plan's recorded digest
///      and byte count, and copied into a fresh, transaction-private snapshot
///      directory whose per-file names are derived from the input's canonical
///      role and index (never a source basename, which could collide across
///      distinct source paths).
///   2. After every input is snapshotted, each snapshot copy is re-opened and
///      re-verified against the plan from the snapshot handle, proving the bytes
///      that were written are the bytes the plan proved.
///   3. Only then is the rewritten argument vector built once, pointing every
///      input token at its snapshot path, and the driver snapshot re-opened
///      read-only for execution by descriptor. There is never a half-built
///      invocation: the factory returns this capability only after all inputs
///      verify, or a rejection with the private tree already removed.
///
/// The driver snapshot's descriptor is borrowed from a `ReadableFile` this
/// object owns, so the owner must outlive the `SubprocessCommand::run` that
/// execs it. Cleanup is explicit: `discardAndCleanup()` consumes the capability,
/// removes the private tree, and reports a `CleanupDisposition` (`Clean` or
/// `Obligated`). On a clean link the capability is instead consumed by
/// `intoCandidate`, which transfers the still-live transaction root into a
/// `LinkedOutputCandidate` rather than removing it. The destructor is a
/// `noexcept` last-resort leak guard that only acts when neither
/// `discardAndCleanup` nor `intoCandidate` was called, and never produces a
/// structured record. The type is constructed only by
/// `prepare`/`prepareWithTokenSource` and consumed only by `linkExecutable` (and
/// the test peer), so an external caller cannot drop it and silently lose an
/// obligation.
class PreparedLinkInputs final {
public:
  PreparedLinkInputs(PreparedLinkInputs&&) noexcept;
  PreparedLinkInputs& operator=(PreparedLinkInputs&&) noexcept;
  ZC_DISALLOW_COPY(PreparedLinkInputs);
  ~PreparedLinkInputs() noexcept;

  /// \brief Snapshots and re-verifies every input into a transaction-private
  ///        tree, returning the prepared capability or a rejection.
  ///
  /// On success the outcome's primary is a verified `PreparedLinkInputs` whose
  /// tree is still live (the caller consumes it and later calls
  /// `discardAndCleanup` or `intoCandidate`). On a prepare-time rejection the
  /// tree is removed here; if that removal fails the outcome is
  /// `RecoveryRequired` carrying the obligation. Production uses the OS CSPRNG
  /// token source.
  ///
  /// \param plan The verified link plan naming every input and the driver.
  /// \param filesystem The filesystem whose root the plan's absolute paths
  ///        resolve against; its root must expose real descriptors, so an
  ///        in-memory filesystem fails closed.
  ZC_NODISCARD static CleanupAwareOutcome<PreparedLinkInputs> prepare(
      const VerifiedLinkPlan& plan, const zc::Filesystem& filesystem);

  /// \brief Like `prepare`, but with a caller-supplied transaction-token source
  ///        so a test can force an exclusive-create collision and prove the
  ///        retry. Write/read/remove faults are injected through a fault-
  ///        injecting `Filesystem` wrapper passed as the ordinary `filesystem`
  ///        argument, so no production signature carries a fault parameter.
  ZC_NODISCARD static CleanupAwareOutcome<PreparedLinkInputs> prepareWithTokenSource(
      const VerifiedLinkPlan& plan, const zc::Filesystem& filesystem,
      SnapshotTokenSource& tokenSource);

  /// \brief Removes the private snapshot tree and reports the cleanup disposition,
  ///        consuming this capability. After it returns, this object is moved-from
  ///        and its destructor does nothing. Used on the link-failure path (and by
  ///        the prepare-level tests) to discard a prepared tree without
  ///        transferring it. The cleanup runs at the `PostSpawnCleanup` stage.
  ZC_NODISCARD CleanupDisposition discardAndCleanup() && noexcept;

  /// \brief The snapshot directory capability, so the caller can open the
  ///        `<root>/output-candidate` the driver wrote (relative to this held
  ///        directory) with a no-follow open. Valid while this object is alive.
  ZC_NODISCARD const zc::Directory& snapshotDirectory() const noexcept;

  /// \brief The absolute path of `<root>/output-candidate` the driver's `-o`
  ///        argument named. Used only to record the candidate's path; the file is
  ///        opened relative to `snapshotDirectory()`, not resolved from this path.
  ZC_NODISCARD zc::StringPtr outputCandidatePath() const noexcept;

  /// \brief Transfers the still-live transaction root into a
  ///        `LinkedOutputCandidate`, consuming this capability (RFC 0043 D4
  ///        success path). The candidate owns the tree, the moved-in verified
  ///        plan, and the transaction-owned output handle plus its captured exact
  ///        identity, byte count, and digest; this object is moved-from afterward,
  ///        so its destructor does nothing (the tree is NOT removed - the candidate
  ///        owns it now).
  ///
  /// This is NOT `noexcept`: it allocates the candidate storage first, and that
  /// heap allocation is the sole throwing step. Because the allocation runs BEFORE
  /// this capability's tree ownership is consumed, a `bad_alloc` leaves `*this`
  /// fully intact, so the caller can still `discardAndCleanup()` the transaction
  /// root - no tree is ever leaked without an obligation. Call it inside the
  /// caller's closed catcher.
  ///
  /// \param plan The verified link plan, moved into the candidate.
  /// \param outputHandle The no-follow read-only handle to `<root>/output-candidate`.
  /// \param outputIdentity The output's exact `(dev, ino)` + link count captured
  ///        from `outputHandle`.
  /// \param outputSize The output's byte count captured from `outputHandle`.
  /// \param outputDigest The output's SHA-256 digest computed from the same handle.
  ZC_NODISCARD LinkedOutputCandidate intoCandidate(VerifiedLinkPlan&& plan,
                                                   zc::Own<const zc::ReadableFile>&& outputHandle,
                                                   StableFileIdentity outputIdentity,
                                                   uint64_t outputSize,
                                                   identity::Sha256Digest outputDigest) &&;

  /// \brief The borrowed descriptor of the driver snapshot, valid while this
  ///        object is alive. The exec targets exactly this open object.
  ZC_NODISCARD int driverDescriptor() const;

  /// \brief The driver snapshot's absolute path (the default `argv[0]`; not used
  ///        to resolve the executable, which runs by descriptor).
  ZC_NODISCARD zc::StringPtr program() const noexcept;

  /// \brief The rewritten argument vector; every input token names a snapshot
  ///        path. `argv[0]` is the driver snapshot path.
  ZC_NODISCARD zc::ArrayPtr<const zc::String> argv() const noexcept;

  /// \brief The working directory for the driver (the output file's parent).
  ZC_NODISCARD zc::StringPtr workingDirectory() const noexcept;

  /// \brief The explicit environment as flattened (name, value) pairs; empty in
  ///        the current closure shape.
  ZC_NODISCARD zc::ArrayPtr<const zc::String> environment() const noexcept;

  /// \brief Captures a directory's exact stable identity (dev/ino) through its
  ///        descriptor, or none when unavailable. A `PreparedLinkInputs` member
  ///        so only the snapshot machinery mints identities (via the minter).
  ZC_NODISCARD static zc::Maybe<StableDirectoryIdentity> captureDirectoryIdentity(
      const zc::Directory& dir) noexcept;

  // The pimpl is exposed within this internal header so the snapshot body in the
  // implementation file can populate it; it is an incomplete type here.
  struct Impl;

private:
  zc::Own<Impl> impl;

  explicit PreparedLinkInputs(zc::Own<Impl> impl) noexcept;
};

/// \brief A per-call test observer notified exactly once, immediately before the
///        driver subprocess is spawned (after every prepare and D4 pre-spawn
///        gate). Production passes none; a test passes an instance to prove a
///        rejection fired WITHOUT attempting a spawn.
///
/// It deliberately proves only "no spawn was attempted" (the notify point is just
/// before `SubprocessCommand::run`); it does not claim an OS child actually
/// started. It is a per-call argument (never global/static/thread-local), so
/// there is no shared spawn state between concurrent links. It lives only in this
/// internal header, so no production translation unit can pass one.
class SpawnAttemptObserver {
public:
  virtual ~SpawnAttemptObserver() noexcept = default;
  /// \brief Called once, just before the driver `run()` is invoked.
  virtual void onSpawnAttempt() noexcept = 0;
};

/// \brief `linkExecutable` with a per-call spawn-attempt observer, so a test can
///        prove a rejection fired without attempting a spawn. Production reaches
///        the linker only through the public `linkExecutable`, which forwards
///        here with a null observer.
ZC_NODISCARD CleanupAwareOutcome<LinkedOutputCandidate> linkExecutableWithObserver(
    VerifiedLinkPlan plan, const zc::Filesystem& filesystem, SpawnAttemptObserver* observer);

/// \brief Test access to the internal snapshot seam. It lives in this internal
///        header (compiled only into `invoke-linker.cc` and the test target), so
///        there is no callable `LinkerInvocationTestAccess` in a production
///        build. It re-exposes `prepareWithTokenSource` and the spawn-observer
///        entry point for readability at call sites; the token source, the fault-
///        injecting filesystem wrapper, and the spawn observer are the only test
///        seams, and none touches a production signature.
struct LinkerInvocationTestAccess {
  ZC_NODISCARD static CleanupAwareOutcome<PreparedLinkInputs> prepareWithTokenSource(
      const VerifiedLinkPlan& plan, const zc::Filesystem& filesystem,
      SnapshotTokenSource& tokenSource) {
    return PreparedLinkInputs::prepareWithTokenSource(plan, filesystem, tokenSource);
  }

  ZC_NODISCARD static CleanupAwareOutcome<LinkedOutputCandidate> linkExecutableObserved(
      VerifiedLinkPlan plan, const zc::Filesystem& filesystem, SpawnAttemptObserver& observer) {
    return linkExecutableWithObserver(zc::mv(plan), filesystem, &observer);
  }
};

}  // namespace zomlang::compiler::ir
