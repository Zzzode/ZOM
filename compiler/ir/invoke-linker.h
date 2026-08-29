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

#pragma once

#include <cstdint>

#include "compiler/ir/ir-failure.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

/// Forward declaration: `PreparedLinkInputs::finishAndCleanup` pairs a cleanup
/// outcome with a linked-executable result, which is defined further below.
class VerifiedLinkedExecutable;

/// \brief A fixed 128-bit opaque identity for one snapshot transaction.
///
/// Generated from the OS CSPRNG so a transaction-private snapshot directory name
/// is unpredictable (no other process can pre-create or guess it) and unique.
/// It is a typed value, never a free-form string, so an orphan record cannot be
/// confused with an arbitrary path.
class SnapshotTransactionId final {
public:
  /// \brief A zero id. Only a placeholder to be overwritten by `fromBytes`; a
  ///        real transaction always assigns a CSPRNG value before use.
  SnapshotTransactionId() = default;

  /// \brief Builds an id from exactly 16 bytes.
  /// \return none unless `bytes` is exactly 16 bytes long.
  ZC_NODISCARD static zc::Maybe<SnapshotTransactionId> fromBytes(zc::ArrayPtr<const uint8_t> bytes);

  /// \brief The 16 identity bytes.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept { return zc::arrayPtr(value); }

  /// \brief The lowercase-hex spelling used as the private directory name.
  ZC_NODISCARD zc::String toHex() const;

  bool operator==(const SnapshotTransactionId& other) const noexcept {
    return bytes() == other.bytes();
  }
  bool operator!=(const SnapshotTransactionId& other) const noexcept { return !(*this == other); }

private:
  uint8_t value[16] = {};
};

/// \brief An opaque, exact stable identity of a directory: the full
///        `(st_dev, st_ino)` tuple captured by `fstat`, never a lossy hash.
///
/// A recovery step compares this against a freshly `fstatat`-ed tuple before
/// removing a tree, so it never deletes a different object that took the same
/// path. Two identities are equal only when both components match. This is a
/// Linux-first value; a filesystem that cannot supply an exact tuple yields no
/// identity at all (see `Maybe<StableDirectoryIdentity>` on the obligation) and
/// recovery must not auto-delete.
class StableDirectoryIdentity final {
public:
  StableDirectoryIdentity(uint64_t device, uint64_t inode) noexcept
      : deviceValue(device), inodeValue(inode) {}

  ZC_NODISCARD uint64_t device() const noexcept { return deviceValue; }
  ZC_NODISCARD uint64_t inode() const noexcept { return inodeValue; }

  bool operator==(const StableDirectoryIdentity& other) const noexcept {
    return deviceValue == other.deviceValue && inodeValue == other.inodeValue;
  }
  bool operator!=(const StableDirectoryIdentity& other) const noexcept { return !(*this == other); }

private:
  uint64_t deviceValue;
  uint64_t inodeValue;
};

/// \brief Why a snapshot-tree cleanup could not complete. A closed set; the
///        recovery journal switches on it.
enum class CleanupFailureKind : uint8_t {
  /// No exact stable identity could be captured (getFd/fstat unavailable), so
  /// removal was never attempted; recovery must adjudicate explicitly and must
  /// not auto-delete.
  IdentityUnavailable = 0x01,
  /// The tree's exact identity no longer matches on the re-check just before
  /// removal, so removal was refused rather than deleting a swapped object.
  IdentityMismatch = 0x02,
  /// Removing the tree's contents (through the held directory capability)
  /// failed.
  ContentRemovalFailed = 0x03,
  /// Removing the now-empty top-level directory failed.
  TopLevelRemovalFailed = 0x04,
};

/// \brief The stage at which a cleanup failure occurred.
enum class CleanupStage : uint8_t {
  /// Rolling back after a prepare-time rejection (no process was spawned).
  PrepareRollback = 0x01,
  /// Removing the tree after the linker process was awaited.
  PostSpawnCleanup = 0x02,
};

/// \brief A structured, driver-consumable record of a snapshot tree whose
///        cleanup could not be completed, so a later recovery/journal step can
///        adjudicate it.
///
/// It is a cleanup *obligation*, not an assertion that an orphaned tree still
/// exists on disk: a competitor that replaced the top-level path may already
/// have detached the tree this transaction created, so recovery must re-check
/// before acting. It carries the typed transaction id, the canonical parent
/// directory the tree was created under, the exact stable directory identity if
/// one was captured (a recovery step compares it before removing, so it never
/// deletes a different object that later took the same path; `none` means no
/// exact identity was available and recovery must not auto-delete), the owning
/// `LinkPlanId`, and the closed cleanup-failure kind and stage. The tree's path
/// is not stored as an independent, mutable field: `treePath()` derives it from
/// the parent and the typed token, so the token and the path can never drift
/// apart. It deliberately does not copy the primary IR failure kind: the primary
/// `IrOperationResult` is the sole authority for why the link failed, and
/// duplicating it here would drift.
class SnapshotCleanupObligation final {
public:
  SnapshotCleanupObligation(SnapshotCleanupObligation&&) noexcept = default;
  SnapshotCleanupObligation& operator=(SnapshotCleanupObligation&&) noexcept = default;
  ZC_DISALLOW_COPY(SnapshotCleanupObligation);
  ~SnapshotCleanupObligation() noexcept = default;

  SnapshotCleanupObligation(const SnapshotTransactionId& transactionId, zc::StringPtr outputParent,
                            zc::Maybe<StableDirectoryIdentity> directoryIdentity,
                            const LinkPlanId& planId, CleanupFailureKind kind, CleanupStage stage)
      : transactionIdValue(transactionId),
        outputParentValue(zc::heapString(outputParent)),
        directoryIdentityValue(zc::mv(directoryIdentity)),
        planIdValue(planId),
        kindValue(kind),
        stageValue(stage) {}

  ZC_NODISCARD const SnapshotTransactionId& transactionId() const noexcept {
    return transactionIdValue;
  }

  /// \brief The absolute path of the tree, derived from the parent and the typed
  ///        token (single source; no independent path field).
  ZC_NODISCARD zc::String treePath() const;

  ZC_NODISCARD zc::StringPtr outputParent() const noexcept { return outputParentValue; }
  ZC_NODISCARD const zc::Maybe<StableDirectoryIdentity>& directoryIdentity() const noexcept {
    return directoryIdentityValue;
  }
  ZC_NODISCARD const LinkPlanId& planId() const noexcept { return planIdValue; }
  ZC_NODISCARD CleanupFailureKind cleanupFailureKind() const noexcept { return kindValue; }
  ZC_NODISCARD CleanupStage cleanupStage() const noexcept { return stageValue; }

private:
  SnapshotTransactionId transactionIdValue;
  zc::String outputParentValue;
  zc::Maybe<StableDirectoryIdentity> directoryIdentityValue;
  LinkPlanId planIdValue;
  CleanupFailureKind kindValue;
  CleanupStage stageValue;
};

/// \brief A closed two-state result that makes an un-cleaned snapshot tree
///        impossible to ignore.
///
/// `Complete` means the operation finished and its private snapshot tree was
/// removed, so there is nothing to recover. `RecoveryRequired` means the
/// operation finished (with any primary result, verified or rejected) but its
/// snapshot tree could not be removed, so exactly one `SnapshotCleanupObligation` is
/// carried for a later recovery step. A caller cannot read the primary result
/// without also seeing whether recovery is required: a verified primary paired
/// with a cleanup failure is `RecoveryRequired`, never a clean success. The type
/// is `[[nodiscard]]`.
template <typename VerifiedValue>
class ZC_NODISCARD CleanupAwareOutcome final {
public:
  CleanupAwareOutcome(CleanupAwareOutcome&&) noexcept = default;
  CleanupAwareOutcome& operator=(CleanupAwareOutcome&&) noexcept = default;
  ZC_DISALLOW_COPY(CleanupAwareOutcome);
  ~CleanupAwareOutcome() noexcept = default;

  /// \brief The operation finished and its snapshot tree was removed.
  ZC_NODISCARD static CleanupAwareOutcome complete(IrOperationResult<VerifiedValue>&& result) {
    return CleanupAwareOutcome(zc::mv(result), zc::none);
  }

  /// \brief The operation finished but its snapshot tree could not be removed.
  ZC_NODISCARD static CleanupAwareOutcome recoveryRequired(
      IrOperationResult<VerifiedValue>&& primary, SnapshotCleanupObligation&& orphan) {
    return CleanupAwareOutcome(zc::mv(primary), zc::mv(orphan));
  }

  ZC_NODISCARD bool isComplete() const noexcept { return orphanValue == zc::none; }
  ZC_NODISCARD bool isRecoveryRequired() const noexcept { return orphanValue != zc::none; }

  /// \brief The primary IR result, valid in both states.
  ZC_NODISCARD const IrOperationResult<VerifiedValue>& primary() const noexcept {
    return resultValue;
  }
  ZC_NODISCARD IrOperationResult<VerifiedValue>&& takePrimary() && { return zc::mv(resultValue); }

  /// \brief The orphaned snapshot. Requires isRecoveryRequired().
  ZC_NODISCARD const SnapshotCleanupObligation& orphan() const {
    return ZC_REQUIRE_NONNULL(orphanValue);
  }

private:
  CleanupAwareOutcome(IrOperationResult<VerifiedValue>&& result,
                      zc::Maybe<SnapshotCleanupObligation>&& orphan) noexcept
      : resultValue(zc::mv(result)), orphanValue(zc::mv(orphan)) {}

  IrOperationResult<VerifiedValue> resultValue;
  zc::Maybe<SnapshotCleanupObligation> orphanValue;
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
///      and byte count, and copied into a fresh, process-private snapshot
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
/// execs it. Cleanup is explicit: `finishAndCleanup()` consumes the capability,
/// removes the private tree, and reports either `Complete` or (if the tree could
/// not be removed) `RecoveryRequired` with a structured `SnapshotCleanupObligation`. The
/// destructor is a `noexcept` last-resort leak guard that only acts when
/// `finishAndCleanup` was never called, and never produces a structured record.
/// The type is constructed only by `PreparedLinkInputs::prepare` and consumed
/// only by `linkExecutable`, so an external caller cannot drop it and silently
/// lose an orphan.

/// \brief A test-only forced fault used to exercise the fail-closed and
///        recovery-obligation paths of the snapshot transaction. Production
///        always passes `None`.
enum class PrepareInjectedFault : uint8_t {
  /// No injected fault (production).
  None = 0x00,
  /// Throw while writing a snapshot copy (exercises the caught-fault rejection
  /// with rollback during pass 1).
  WriteMidSnapshot = 0x01,
  /// Throw while re-verifying a snapshot copy (exercises pass-2 fault handling).
  Pass2Read = 0x02,
  /// Force every snapshot-tree removal to fail (exercises the RecoveryRequired
  /// obligation on both the prepare-rollback and post-spawn cleanup paths).
  TreeRemoval = 0x03,
};

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
  /// `finishAndCleanup`). On a prepare-time rejection the tree is removed here;
  /// if that removal fails the outcome is `RecoveryRequired` carrying the orphan.
  ///
  /// \param plan The verified link plan naming every input and the driver.
  /// \param filesystem The filesystem whose root the plan's absolute paths
  ///        resolve against; its root must expose real descriptors, so an
  ///        in-memory filesystem fails closed.
  /// \param injectedFault A test-only forced fault; production passes None.
  ZC_NODISCARD static CleanupAwareOutcome<PreparedLinkInputs> prepare(
      const VerifiedLinkPlan& plan, const zc::Filesystem& filesystem,
      PrepareInjectedFault injectedFault = PrepareInjectedFault::None);

  /// \brief Removes the private snapshot tree and reports the cleanup outcome,
  ///        consuming this capability. After it returns, this object is
  ///        moved-from and its destructor does nothing.
  ///
  /// \param primary The primary link result to pair with the cleanup outcome. A
  ///        verified primary with a failed cleanup still yields RecoveryRequired.
  /// \param injectedFault A test-only forced fault; production passes None.
  ZC_NODISCARD CleanupAwareOutcome<VerifiedLinkedExecutable> finishAndCleanup(
      IrOperationResult<VerifiedLinkedExecutable>&& primary,
      PrepareInjectedFault injectedFault = PrepareInjectedFault::None) && noexcept;

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

private:
  struct Impl;
  zc::Own<Impl> impl;

  explicit PreparedLinkInputs(zc::Own<Impl> impl) noexcept;
};

/// \brief A linked executable produced from a verified link plan.
///
/// Carries the bytes of the executable the linker driver wrote at the plan's
/// output path, read back after a successful, verified invocation. It is only
/// constructed by `linkExecutable` on the success path.
class VerifiedLinkedExecutable final {
public:
  VerifiedLinkedExecutable(VerifiedLinkedExecutable&&) noexcept = default;
  VerifiedLinkedExecutable& operator=(VerifiedLinkedExecutable&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedLinkedExecutable);
  ~VerifiedLinkedExecutable() noexcept = default;

  explicit VerifiedLinkedExecutable(zc::Array<uint8_t>&& bytes) noexcept
      : bytesValue(zc::mv(bytes)) {}

  /// \brief The produced executable bytes.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept { return bytesValue.asPtr(); }

private:
  zc::Array<uint8_t> bytesValue;
};

/// \brief Invokes the linker driver for a verified plan and reads back the output.
///
/// RFC 0043 "Linker Driver Invocation": the InvokeLinker step. Its entire input
/// is the `VerifiedLinkPlan` plus the filesystem root the plan's absolute paths
/// resolve against - there is no forkable independent argv, working directory, or
/// output path. It:
///
///   1. snapshots and re-verifies every plan input (driver, closure CRT objects
///      and default libraries, object and runtime records) into a transaction-
///      private directory via `PreparedLinkInputs::prepare`, defeating the input
///      TOCTOU: the driver runs by the snapshot descriptor and every input token
///      names a snapshot path, so no source inode swapped after verification can
///      be linked;
///   2. rejects a pre-existing file at the plan's output path (no stale output
///      is ever accepted as a link result);
///   3. spawns the driver by descriptor through the shell-free child-process
///      primitive with an explicit empty environment;
///   4. on a spawn failure, a signal, a nonzero exit, or a missing/unchanged
///      output, removes any partial output it created and rejects; and
///   5. on success, reads back the produced executable and returns it.
///
/// Every rejection is an RFC 0010 `IrOperationResult` failure under
/// `IrFailurePhase::LinkerInvocation`; an input-revision change (a driver or
/// input whose bytes no longer match the plan) maps to `InputRevisionMismatch`,
/// and every other linker failure maps to `OutputCreationFailed` (capability) or
/// `InvalidFact`. The private snapshot tree is removed on every path; a removal
/// that fails is reported as `RecoveryRequired` with a structured
/// `SnapshotCleanupObligation`, so an un-cleaned tree is never silently left behind.
///
/// \param plan The verified link plan; the sole source of inputs, driver, and output.
/// \param filesystem The filesystem whose root the plan's normalized absolute
///        paths resolve against; its root must expose real descriptors, so an
///        in-memory filesystem fails closed.
/// \param injectedFault A test-only forced fault; production passes None.
/// \return A cleanup-aware outcome wrapping the verified linked executable or a
///         LinkerInvocation-phase rejection.
ZC_NODISCARD CleanupAwareOutcome<VerifiedLinkedExecutable> linkExecutable(
    const VerifiedLinkPlan& plan, const zc::Filesystem& filesystem,
    PrepareInjectedFault injectedFault = PrepareInjectedFault::None);

}  // namespace zomlang::compiler::ir
