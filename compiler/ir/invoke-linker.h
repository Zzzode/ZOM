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

namespace detail {
/// \brief The sole factory permitted to construct the restricted snapshot record
///        types below (the attorney/factory idiom). It is defined only in the
///        test-only internal header `invoke-linker-internal.h`, so no ordinary
///        production translation unit can mint a transaction id, a directory
///        identity, or a cleanup obligation. The public surface names this
///        neutral factory rather than the snapshot capability itself.
class LinkSnapshotMinter;
/// \brief The sole factory permitted to construct a `LinkedOutputCandidate` from
///        its (internal) transaction impl. Defined only in the implementation
///        file, where the snapshot machinery is complete, so the public header
///        names a neutral attorney rather than the transaction internals.
class LinkOutputCandidateFactory;
/// \brief The restricted D1 publication attorney. Its surface is internal and
///        implemented beside the candidate pimpl.
class LinkPublicationTransaction;
}  // namespace detail

/// \brief A fixed 128-bit opaque identity for one snapshot transaction.
///
/// Generated from the OS CSPRNG so a transaction-private snapshot directory name
/// is unpredictable (no other process can pre-create or guess it) and unique.
/// It is a typed value, never a free-form string, so an obligation record cannot
/// be confused with an arbitrary path. A meaningful id is minted only by the
/// snapshot machinery (through `detail::LinkSnapshotMinter`), so external code
/// cannot forge one from chosen bytes.
class SnapshotTransactionId final {
public:
  /// \brief A zero id. Inert on its own: a real, unpredictable id is always
  ///        produced by the snapshot machinery from the OS CSPRNG. Public so the
  ///        machinery can hold one by value before assigning the minted id.
  SnapshotTransactionId() = default;
  SnapshotTransactionId(SnapshotTransactionId&&) noexcept = default;
  SnapshotTransactionId& operator=(SnapshotTransactionId&&) noexcept = default;
  SnapshotTransactionId(const SnapshotTransactionId&) noexcept = default;
  SnapshotTransactionId& operator=(const SnapshotTransactionId&) noexcept = default;
  ~SnapshotTransactionId() noexcept = default;

  /// \brief The 16 identity bytes.
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> bytes() const noexcept { return zc::arrayPtr(value); }

  /// \brief The lowercase-hex spelling used as the private directory name.
  ZC_NODISCARD zc::String toHex() const;

  bool operator==(const SnapshotTransactionId& other) const noexcept {
    return bytes() == other.bytes();
  }
  bool operator!=(const SnapshotTransactionId& other) const noexcept { return !(*this == other); }

private:
  /// \brief Builds an id from exactly 16 bytes; none unless exactly 16. Minted
  ///        only by the snapshot machinery, so an id cannot be forged from
  ///        chosen bytes.
  ZC_NODISCARD static zc::Maybe<SnapshotTransactionId> fromBytes(zc::ArrayPtr<const uint8_t> bytes);

  friend class detail::LinkSnapshotMinter;

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
  StableDirectoryIdentity(StableDirectoryIdentity&&) noexcept = default;
  StableDirectoryIdentity& operator=(StableDirectoryIdentity&&) noexcept = default;
  StableDirectoryIdentity(const StableDirectoryIdentity&) noexcept = default;
  StableDirectoryIdentity& operator=(const StableDirectoryIdentity&) noexcept = default;
  ~StableDirectoryIdentity() noexcept = default;

  ZC_NODISCARD uint64_t device() const noexcept { return deviceValue; }
  ZC_NODISCARD uint64_t inode() const noexcept { return inodeValue; }

  /// \brief True when this identity is exactly `(device, inode)`. Comparison is
  ///        safe to expose; only construction is restricted.
  ZC_NODISCARD bool matches(uint64_t device, uint64_t inode) const noexcept {
    return deviceValue == device && inodeValue == inode;
  }

  bool operator==(const StableDirectoryIdentity& other) const noexcept {
    return deviceValue == other.deviceValue && inodeValue == other.inodeValue;
  }
  bool operator!=(const StableDirectoryIdentity& other) const noexcept { return !(*this == other); }

private:
  StableDirectoryIdentity(uint64_t device, uint64_t inode) noexcept
      : deviceValue(device), inodeValue(inode) {}

  friend class detail::LinkSnapshotMinter;

  uint64_t deviceValue;
  uint64_t inodeValue;
};

/// \brief An opaque, exact stable identity of a regular file: the full
///        `(st_dev, st_ino)` tuple plus the hard-link count `st_nlink`, captured
///        by `fstat` on a held descriptor.
///
/// Distinct from `StableDirectoryIdentity` so a file identity is never confused
/// with a directory identity. The link count is part of the identity because the
/// D4 output invariant requires `st_nlink == 1` (the transaction is the sole link
/// to the inode; a multiply-linked inode could be rewritten in place through an
/// external path). A recovery or publication step (RFC 0043 D1/INV-8) compares
/// this against a freshly `fstat`-ed tuple before trusting the output, so it is
/// minted only by the snapshot machinery and can never be forged from chosen
/// values.
class StableFileIdentity final {
public:
  StableFileIdentity(StableFileIdentity&&) noexcept = default;
  StableFileIdentity& operator=(StableFileIdentity&&) noexcept = default;
  StableFileIdentity(const StableFileIdentity&) noexcept = default;
  StableFileIdentity& operator=(const StableFileIdentity&) noexcept = default;
  ~StableFileIdentity() noexcept = default;

  ZC_NODISCARD uint64_t device() const noexcept { return deviceValue; }
  ZC_NODISCARD uint64_t inode() const noexcept { return inodeValue; }
  ZC_NODISCARD uint64_t linkCount() const noexcept { return linkCountValue; }

  /// \brief True when this identity is exactly `(device, inode)`. The link count
  ///        is not part of equality: it proves sole-ownership at capture time but
  ///        a later re-check compares the object identity.
  ZC_NODISCARD bool matches(uint64_t device, uint64_t inode) const noexcept {
    return deviceValue == device && inodeValue == inode;
  }

  bool operator==(const StableFileIdentity& other) const noexcept {
    return deviceValue == other.deviceValue && inodeValue == other.inodeValue &&
           linkCountValue == other.linkCountValue;
  }
  bool operator!=(const StableFileIdentity& other) const noexcept { return !(*this == other); }

private:
  StableFileIdentity(uint64_t device, uint64_t inode, uint64_t linkCount) noexcept
      : deviceValue(device), inodeValue(inode), linkCountValue(linkCount) {}

  friend class detail::LinkSnapshotMinter;

  uint64_t deviceValue;
  uint64_t inodeValue;
  uint64_t linkCountValue;
};

/// \brief Why a snapshot-tree cleanup could not complete. A closed set; the
///        recovery journal switches on it.
enum class CleanupFailureKind : uint8_t {
  /// No exact stable identity could be captured (getFd/fstat unavailable), so the
  /// top-level removal was never attempted; recovery must adjudicate explicitly
  /// and must not auto-delete. The tree's contents are still freed first.
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
  /// Recovering a durable D1 publication journal after process restart.
  PublicationRecovery = 0x03,
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
  // Constructed only by the snapshot machinery, and by move only for the owned
  // `outputParent` string, so the `RecoveryRequired` branch can be produced
  // without any allocation on the noexcept finish path.
  SnapshotCleanupObligation(const SnapshotTransactionId& transactionId, zc::String&& outputParent,
                            zc::Maybe<StableDirectoryIdentity> directoryIdentity,
                            const LinkPlanId& planId, CleanupFailureKind kind,
                            CleanupStage stage) noexcept
      : transactionIdValue(transactionId),
        outputParentValue(zc::mv(outputParent)),
        directoryIdentityValue(zc::mv(directoryIdentity)),
        planIdValue(planId),
        kindValue(kind),
        stageValue(stage) {}

  friend class detail::LinkSnapshotMinter;

  SnapshotTransactionId transactionIdValue;
  zc::String outputParentValue;
  zc::Maybe<StableDirectoryIdentity> directoryIdentityValue;
  LinkPlanId planIdValue;
  CleanupFailureKind kindValue;
  CleanupStage stageValue;
};

/// \brief The payload of the `RecoveryRequired` branch: the primary IR result
///        (verified value or failure facts, preserved losslessly) plus the one
///        cleanup obligation a recovery step must adjudicate.
template <typename VerifiedValue>
struct RecoveryRequiredOutcome {
  IrOperationResult<VerifiedValue> primary;
  SnapshotCleanupObligation obligation;
};

/// \brief A closed two-state result that makes an un-cleaned snapshot tree
///        impossible to ignore.
///
/// `Complete` means the operation finished with no unaccounted cleanup
/// obligation: either its private snapshot tree was removed, OR (on a successful
/// link, RFC 0043 D4) the tree's ownership was transferred into the verified
/// return capability, which is then responsible for the eventual
/// `discardAndCleanup`. Either way there is nothing an outer caller must
/// separately recover. `RecoveryRequired` means the operation finished (with any
/// primary result, verified or rejected) but its snapshot tree could not be
/// removed, so exactly one `SnapshotCleanupObligation` is carried for a later
/// recovery step. The two branches are extracted by distinct consuming accessors
/// - `takeComplete()` requires `Complete`, `takeRecoveryRequired()` returns both
/// the primary and the obligation - so a caller cannot read a verified primary
/// while silently dropping a cleanup obligation: a verified primary paired with a
/// cleanup failure is `RecoveryRequired`, never a clean success. The type is
/// `[[nodiscard]]`.
template <typename VerifiedValue>
class ZC_NODISCARD CleanupAwareOutcome final {
public:
  CleanupAwareOutcome(CleanupAwareOutcome&&) noexcept = default;
  CleanupAwareOutcome& operator=(CleanupAwareOutcome&&) noexcept = default;
  ZC_DISALLOW_COPY(CleanupAwareOutcome);
  ~CleanupAwareOutcome() noexcept = default;

  /// \brief The operation finished with no unaccounted cleanup obligation: the
  ///        snapshot tree was removed, or its ownership was transferred into the
  ///        verified return capability.
  ZC_NODISCARD static CleanupAwareOutcome complete(IrOperationResult<VerifiedValue>&& result) {
    return CleanupAwareOutcome(zc::mv(result), zc::none);
  }

  /// \brief The operation finished but its snapshot tree could not be removed.
  ZC_NODISCARD static CleanupAwareOutcome recoveryRequired(
      IrOperationResult<VerifiedValue>&& primary, SnapshotCleanupObligation&& obligation) {
    return CleanupAwareOutcome(zc::mv(primary), zc::mv(obligation));
  }

  ZC_NODISCARD bool isComplete() const noexcept { return obligationValue == zc::none; }
  ZC_NODISCARD bool isRecoveryRequired() const noexcept { return obligationValue != zc::none; }

  /// \brief The primary IR result of a `Complete` outcome. Requires isComplete();
  ///        an outcome that requires recovery cannot be consumed as a clean one.
  ZC_NODISCARD IrOperationResult<VerifiedValue>&& takeComplete() && {
    ZC_IREQUIRE(obligationValue == zc::none,
                "takeComplete() on a RecoveryRequired outcome would drop the cleanup obligation");
    return zc::mv(resultValue);
  }

  /// \brief The primary result and the cleanup obligation of a `RecoveryRequired`
  ///        outcome, taken together so the obligation cannot be dropped.
  ///        Requires isRecoveryRequired().
  ZC_NODISCARD RecoveryRequiredOutcome<VerifiedValue> takeRecoveryRequired() && {
    ZC_IREQUIRE(obligationValue != zc::none, "takeRecoveryRequired() on a Complete outcome");
    return RecoveryRequiredOutcome<VerifiedValue>{zc::mv(resultValue),
                                                  ZC_REQUIRE_NONNULL(zc::mv(obligationValue))};
  }

private:
  CleanupAwareOutcome(IrOperationResult<VerifiedValue>&& result,
                      zc::Maybe<SnapshotCleanupObligation>&& obligation) noexcept
      : resultValue(zc::mv(result)), obligationValue(zc::mv(obligation)) {}

  IrOperationResult<VerifiedValue> resultValue;
  zc::Maybe<SnapshotCleanupObligation> obligationValue;
};

/// \brief The resource-cleanup disposition of consuming a link transaction root,
///        independent of any caller's primary result.
///
/// `discardAndCleanup` on a `LinkedOutputCandidate` returns this: `Clean` when
/// the transaction root was removed, or `Obligated` carrying the one
/// `SnapshotCleanupObligation` a recovery step must adjudicate. A caller folds it
/// into its own primary rejection (a `Clean` disposition yields `Rejected`, an
/// `Obligated` disposition yields a recovery-required result). Keeping the
/// primary out of the disposition avoids threading an arbitrary
/// `IrOperationResult<T>` through the cleanup seam.
class CleanupDisposition final {
public:
  CleanupDisposition(CleanupDisposition&&) noexcept = default;
  CleanupDisposition& operator=(CleanupDisposition&&) noexcept = default;
  ZC_DISALLOW_COPY(CleanupDisposition);
  ~CleanupDisposition() noexcept = default;

  /// \brief The transaction root was removed; there is nothing to recover.
  ZC_NODISCARD static CleanupDisposition clean() { return CleanupDisposition(zc::none); }

  /// \brief The transaction root could not be removed; one obligation is carried.
  ZC_NODISCARD static CleanupDisposition obligated(SnapshotCleanupObligation&& obligation) {
    return CleanupDisposition(zc::mv(obligation));
  }

  ZC_NODISCARD bool isClean() const noexcept { return obligationValue == zc::none; }
  ZC_NODISCARD bool isObligated() const noexcept { return obligationValue != zc::none; }

  /// \brief The carried obligation of an `Obligated` disposition, taken by move.
  ///        Requires isObligated().
  ZC_NODISCARD SnapshotCleanupObligation takeObligation() && {
    ZC_IREQUIRE(obligationValue != zc::none, "takeObligation() on a Clean disposition");
    return ZC_REQUIRE_NONNULL(zc::mv(obligationValue));
  }

private:
  explicit CleanupDisposition(zc::Maybe<SnapshotCleanupObligation>&& obligation) noexcept
      : obligationValue(zc::mv(obligation)) {}

  zc::Maybe<SnapshotCleanupObligation> obligationValue;
};

/// \brief The move-only capability that owns a still-live link transaction root
///        after a successful linker invocation (RFC 0043 D4).
///
/// `linkExecutable` does not publish on success: it moves the transaction-root
/// ownership into this candidate and returns it. The candidate is the sole
/// legitimate owner of the still-live `.zomlink-<token>/` root; it holds
///
///   * the moved-in `VerifiedLinkPlan` (its `LinkPlanId`, target/toolchain
///     identity, input digests, and the final output request the D1 publication
///     step will publish to). The candidate is the single authority for the
///     publication context, so no external re-assembly is needed;
///   * a transaction-owned read-only handle to `<root>/output-candidate`, opened
///     with a no-follow open so a symlink at that entry is refused, plus the
///     output's exact `StableFileIdentity` (`(dev, ino)` and link count) captured
///     from that same handle. A later step reads the bytes through the handle and
///     re-derives the digest/size/identity from the same object, so the candidate
///     never becomes a second, drift-able byte authority;
///   * its consume state, so it cannot be consumed twice.
///
/// It is deliberately not called "verified": a candidate means only that the
/// driver ran, exited zero, and produced a regular, non-empty, singly-linked
/// output in the transaction root. It does NOT assert the output's format,
/// architecture, entry symbol, or runtime symbols were checked - those are the
/// later D5 `ExecutablePublication` checks.
///
/// Cleanup is explicit: `discardAndCleanup()` consumes the candidate, removes the
/// transaction root, and reports a `CleanupDisposition`. The destructor is a
/// `noexcept` last-resort leak guard that only acts when `discardAndCleanup` was
/// never called and produces no structured record.
class LinkedOutputCandidate final {
public:
  LinkedOutputCandidate(LinkedOutputCandidate&&) noexcept;
  LinkedOutputCandidate& operator=(LinkedOutputCandidate&&) noexcept;
  ZC_DISALLOW_COPY(LinkedOutputCandidate);
  ~LinkedOutputCandidate() noexcept;

  /// \brief The moved-in verified link plan; the single authority for the
  ///        publication context.
  ZC_NODISCARD const VerifiedLinkPlan& plan() const noexcept;

  /// \brief The output candidate's exact stable file identity (dev/ino + link
  ///        count), captured from the held read-only handle at link time.
  ZC_NODISCARD const StableFileIdentity& outputIdentity() const noexcept;

  /// \brief The output candidate's byte count, computed from the held handle.
  ZC_NODISCARD uint64_t outputSize() const noexcept;

  /// \brief The output candidate's SHA-256 digest, computed from the same held
  ///        read-only handle at link time (over exactly `outputSize()` bytes). A
  ///        later D5 step re-reads and re-hashes the same object and checks it
  ///        against this value before publishing; the candidate is not a second,
  ///        drift-able byte authority.
  ZC_NODISCARD const identity::Sha256Digest& outputDigest() const noexcept;

  /// \brief The output candidate bytes, read back through the transaction-owned
  ///        read-only handle. A later D5 step re-derives digest/size from the same
  ///        object; this is an inspection read, not a second byte authority.
  ZC_NODISCARD zc::Array<uint8_t> readOutput() const;

  /// \brief The absolute path of the still-live `<root>/output-candidate`.
  ZC_NODISCARD zc::StringPtr outputCandidatePath() const noexcept;

  /// \brief Removes the transaction root and reports the cleanup disposition,
  ///        consuming this candidate. After it returns, this object is moved-from
  ///        and its destructor does nothing. A failed caller (and the D4 tests)
  ///        use this seam rather than dropping the candidate bare.
  ZC_NODISCARD CleanupDisposition discardAndCleanup() && noexcept;

  // The pimpl is completed in the implementation file, where the snapshot
  // machinery is defined; it is an incomplete type here.
  struct Impl;

private:
  friend class detail::LinkOutputCandidateFactory;
  friend class detail::LinkPublicationTransaction;

  zc::Own<Impl> impl;

  explicit LinkedOutputCandidate(zc::Own<Impl> impl) noexcept;
};

/// \brief Invokes the linker driver for a verified plan into a transaction-owned
///        output candidate (RFC 0043 D4).
///
/// The InvokeLinker step. It consumes the `VerifiedLinkPlan` by move (so the same
/// plan cannot be replayed into a second concurrent link) plus the filesystem the
/// plan's absolute paths resolve against - there is no forkable independent argv,
/// working directory, or output path. It:
///
///   1. snapshots and re-verifies every plan input (driver, closure CRT objects
///      and default libraries, object and runtime records) into the unified
///      transaction root, defeating the input TOCTOU: the driver runs by the
///      snapshot descriptor and every input token names a snapshot path;
///   2. spawns the driver by descriptor through the shell-free child-process
///      primitive with an explicit empty environment, writing `-o` to
///      `<root>/output-candidate` inside the transaction root - never a public
///      final path;
///   3. on a spawn failure, a signal, a nonzero exit, or a missing output, cleans
///      *only the transaction root* and rejects - it never touches a public final
///      path; and
///   4. on success, opens the output candidate with a no-follow open, confirms it
///      is a regular, non-empty, singly-linked (`st_nlink == 1`) file whose exact
///      identity it captures, and transfers the still-live transaction root into a
///      `LinkedOutputCandidate`.
///
/// Every rejection is an RFC 0010 `IrOperationResult` failure under
/// `IrFailurePhase::LinkerInvocation`; an input-revision change maps to
/// `InputRevisionMismatch`, and every other linker failure maps to
/// `OutputCreationFailed` (capability) or `InvalidFact`. On success the transaction
/// root is NOT removed - its ownership is transferred into the returned candidate,
/// so `CleanupAwareOutcome::Complete` here means "no unaccounted cleanup
/// obligation: the tree was removed, or its ownership was transferred into the
/// returned capability". A cleanup that was attempted and could not complete is
/// `RecoveryRequired`. No filesystem exception escapes.
///
/// \param plan The verified link plan, consumed by value so the caller must move
///        a move-only plan in and cannot reuse it after the call on ANY branch
///        (success or rejection); the sole source of inputs, driver, and the
///        final output request.
/// \param filesystem The filesystem whose root the plan's normalized absolute
///        paths resolve against; its root must expose real descriptors, so an
///        in-memory filesystem fails closed.
/// \return A cleanup-aware outcome wrapping the transaction-owned output candidate
///         or a LinkerInvocation-phase rejection.
ZC_NODISCARD CleanupAwareOutcome<LinkedOutputCandidate> linkExecutable(
    VerifiedLinkPlan plan, const zc::Filesystem& filesystem);

}  // namespace zomlang::compiler::ir
