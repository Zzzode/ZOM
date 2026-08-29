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

#include "compiler/ir/invoke-linker.h"
//
#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/identity-invariant.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/ir/invoke-linker-internal.h"
#include "zc/core/debug.h"
#include "zc/core/exception.h"
#include "zc/core/io.h"
#include "zc/core/subprocess.h"
#include "zc/core/vector.h"

// RFC 0043 D3b is Linux-first: the exact-identity owner proof (fstat/fstatat on
// a real descriptor), the CSPRNG token (getrandom), and exec-by-descriptor all
// require Linux syscalls. All Linux-only includes, types, and syscalls live
// behind this compile boundary; on any other platform the snapshot machinery
// compiles to a closed rejection and never references a Linux header.
#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#define ZOM_LINK_SNAPSHOT_SUPPORTED 1
#else
#define ZOM_LINK_SNAPSHOT_SUPPORTED 0
#endif

namespace zomlang::compiler::ir {

namespace {

// The fixed name prefix of a transaction-private snapshot directory. The
// unpredictable part is the hex transaction id that follows; the prefix only
// aids human inspection. The obligation record derives the same path from this
// prefix plus the typed token, so there is a single path formula.
constexpr zc::StringPtr kSnapshotDirPrefix = ".zomlink-"_zc;

// The fixed leaf name of the linker output inside the transaction root. RFC 0043
// D4: the driver's `-o` names `<root>/output-candidate`, never a public final
// path; the D1 publication step renames this into the plan's final output.
constexpr zc::StringPtr kOutputCandidateName = "output-candidate"_zc;

// The session context bound to a linker-invocation rejection. Like the other
// RFC 0043 link phases, LinkerInvocation is session-owned, so the fact carries
// the session fingerprint and never needs module/definition identity expansion.
identity::ContextFingerprint linkerInvocationSessionContext() {
  auto digest = identity::sha256("zom.linker-invocation"_zc.asBytes());
  ZC_IREQUIRE(digest != zc::none, "linker-invocation session digest must compute");
  return identity::ContextFingerprint::fromCanonicalDigest(ZC_REQUIRE_NONNULL(digest));
}

// A resolver that expands nothing: a session-owned fact is admitted without any
// module/definition/instance expansion, so this is never invoked.
class UnusedIdentityResolver final : public IrFailureIdentityResolver {
public:
  ExpandedIrIdentityResult expand(identity::ModuleId) const override {
    return rejected(identity::IdentityAllocationPhase::Module);
  }
  ExpandedIrIdentityResult expand(identity::DefId) const override {
    return rejected(identity::IdentityAllocationPhase::Definition);
  }
  ExpandedIrIdentityResult expand(InstanceId) const override {
    return rejected(identity::IdentityAllocationPhase::Definition);
  }

private:
  static ExpandedIrIdentityResult rejected(identity::IdentityAllocationPhase phase) {
    zc::Maybe<zc::Array<uint8_t>> noKey;
    zc::Maybe<identity::UnbrandedSourceRange> noRange;
    auto invariant = identity::IdentityInvariant::from(
        identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
        identity::IdentityApiSite::HandleLookup, 0);
    ZC_IF_SOME(value, invariant) { return RejectedIrIdentityValue{zc::mv(value)}; }
    ZC_UNREACHABLE
  }
};

// Builds a single session-owned LinkerInvocation rejection with `kind`, carrying
// a `Backend { operation: InvokeLinker }` site as RFC 0043 requires for every
// linker-process failure. Per the RFC failure table, `OutputCreationFailed` is a
// CapabilityRejected row (a spawn failure, nonzero exit, or missing/empty
// output - the requested link output could not be produced) and the invariant
// kinds (InputRevisionMismatch / InvalidFact / CanonicalCodecMismatch) are
// IrInvariantRejected rows. The failure fact is independent of the verified
// value type, so the rejection is produced for any `IrOperationResult<T>`.
template <typename T>
IrOperationResult<T> rejectLinkerInvocation(IrFailureKind kind, uint32_t ordinal) {
  UnusedIdentityResolver resolver;
  auto fallback = IrFailureFallbackContext::from(
      IrFailurePhase::LinkerInvocation,
      IrFailureOwner::session(linkerInvocationSessionContext().clone()));
  ZC_IREQUIRE(fallback != zc::none, "Linker invocation failure fallback must be legal");
  const bool capability = kind == IrFailureKind::OutputCreationFailed;
  const auto branch =
      capability ? IrRejectedBranch::CapabilityRejected : IrRejectedBranch::IrInvariantRejected;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  // Every linker-process failure carries a Backend { InvokeLinker } site.
  zc::Maybe<IrFailureSite> site = IrFailureSite::backend(zc::none, BackendOperation::InvokeLinker);
  auto descriptor = IrFailureDescriptor::decoded(
      branch, IrFailurePhase::LinkerInvocation, kind,
      IrFailureOwner::session(linkerInvocationSessionContext().clone()), zc::mv(site),
      IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(context, fallback) {
    auto admitted = IrFailureFactory::admit(zc::mv(descriptor), context, resolver);
    ZC_IREQUIRE(admitted.is<AcceptedIrFailureDescriptor>(),
                "Session-owned linker invocation rejection must admit without expansion");
    if (capability) {
      zc::Vector<IrFailureFact> facts;
      facts.add(zc::mv(admitted).get<AcceptedIrFailureDescriptor>().fact);
      auto sorted = SortedCapabilityFailureFacts::from(zc::mv(facts));
      ZC_IF_SOME(values, sorted) {
        return IrOperationResult<T>::capabilityRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<IrFailureFact> facts;
    facts.add(zc::mv(admitted).get<AcceptedIrFailureDescriptor>().fact);
    auto sorted = SortedIrInvariantFailureFacts::from(zc::mv(facts));
    ZC_IF_SOME(values, sorted) { return IrOperationResult<T>::irInvariantRejected(zc::mv(values)); }
  }
  ZC_UNREACHABLE
}

// Re-maps a PreparedLinkInputs rejection into a LinkedOutputCandidate-typed
// result, preserving the branch (the fact family is identical for both value
// types). Consumes `preparedResult`, which must be a rejection.
IrOperationResult<LinkedOutputCandidate> remapPreparedRejection(
    IrOperationResult<PreparedLinkInputs>&& preparedResult) {
  if (preparedResult.isCapabilityRejected()) {
    return IrOperationResult<LinkedOutputCandidate>::capabilityRejected(
        zc::mv(preparedResult).takeCapabilityFailures());
  }
  return IrOperationResult<LinkedOutputCandidate>::irInvariantRejected(
      zc::mv(preparedResult).takeInvariantFailures());
}

#if ZOM_LINK_SNAPSHOT_SUPPORTED

// Opens an absolute path under `root`, returning its bytes, or none if absent.
// A read fault (for example from a fault-injecting wrapper) propagates as an
// exception, which the caller's catcher turns into a rejection.
zc::Maybe<zc::Array<zc::byte>> tryReadAbsolute(const zc::ReadableDirectory& root,
                                               zc::StringPtr absolutePath) {
  if (absolutePath.size() < 2 || absolutePath[0] != '/') { return zc::none; }
  ZC_IF_SOME(file, root.tryOpenFile(zc::Path::parse(absolutePath.slice(1)))) {
    return file->readAllBytes();
  }
  return zc::none;
}

// Interprets a byte sequence as one argv token, or none when empty or carrying
// an interior NUL (which cannot survive a C-string argv token).
zc::Maybe<zc::String> bytesToArgument(zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() == 0) { return zc::none; }
  for (uint8_t b : bytes) {
    if (b == 0) { return zc::none; }
  }
  return zc::heapString(reinterpret_cast<const char*>(bytes.begin()), bytes.size());
}

// Returns the parent directory of a normalized absolute file path, or none when
// the path is not a normalized absolute path. "/name" yields "/". The path is
// treated as an opaque POSIX string, matching the link-plan path contract.
zc::Maybe<zc::String> parentDirectory(zc::StringPtr path) {
  if (path.size() == 0 || path[0] != '/') { return zc::none; }
  size_t lastSlash = 0;
  bool found = false;
  for (size_t index = 1; index < path.size(); ++index) {
    if (path[index] == '/') {
      lastSlash = index;
      found = true;
    }
  }
  if (!found) { return zc::str("/"); }
  return zc::heapString(path.cStr(), lastSlash);
}

// Joins an absolute directory path with a child name, collapsing the root case
// so "/" + "x" is "/x" rather than "//x".
zc::String joinPath(zc::StringPtr directory, zc::StringPtr child) {
  if (directory == "/"_zc) { return zc::str("/", child); }
  return zc::str(directory, "/", child);
}

// The transaction directory's leaf name: the shared single formula for the
// private directory that `prepare` creates and the obligation record derives.
zc::String snapshotDirName(const SnapshotTransactionId& token) {
  return zc::str(kSnapshotDirPrefix, token.toHex());
}

// The absolute snapshot-directory path for a transaction.
zc::String snapshotTreePath(zc::StringPtr outputParent, const SnapshotTransactionId& token) {
  return joinPath(outputParent, snapshotDirName(token));
}

// Verifies `bytes` against a recorded digest and byte count.
bool bytesMatch(zc::ArrayPtr<const zc::byte> bytes, const identity::Sha256Digest& digest,
                uint64_t byteCount) {
  if (bytes.size() != byteCount) { return false; }
  zc::Maybe<identity::Sha256Digest> computed = identity::sha256(bytes.asBytes());
  ZC_IF_SOME(value, computed) { return value == digest; }
  return false;
}

// One input to snapshot: its source absolute path, the plan's recorded digest
// and byte count, the snapshot file name, and whether it must be executable.
struct SnapshotPlan {
  zc::String sourcePath;
  identity::Sha256Digest digest;
  uint64_t byteCount;
  zc::String snapshotName;
  bool executable;
};

// The outcome of an attempt to remove a transaction-private snapshot tree. On a
// non-Removed result the caller records a cleanup obligation with the mapped
// kind.
struct TreeCleanupOutcome {
  bool removed;
  CleanupFailureKind failureKind;  // Valid only when !removed.
};

TreeCleanupOutcome treeRemoved() {
  return TreeCleanupOutcome{true, CleanupFailureKind::ContentRemovalFailed};
}
TreeCleanupOutcome cleanupFailed(CleanupFailureKind kind) {
  return TreeCleanupOutcome{false, kind};
}

// The production CSPRNG token source: 16 bytes from getrandom. Fails closed when
// the source is unavailable or returns short.
class CsprngTokenSource final : public SnapshotTokenSource {
public:
  bool nextToken(uint8_t (&out)[16]) override {
    size_t filled = 0;
    while (filled < sizeof(out)) {
      ssize_t n = ::getrandom(out + filled, sizeof(out) - filled, 0);
      if (n <= 0) {
        if (n < 0 && errno == EINTR) { continue; }
        return false;
      }
      filled += static_cast<size_t>(n);
    }
    return true;
  }
};

// Removes the transaction-private snapshot tree, race-tightened per RFC 0043
// D3b. It always removes the tree's *contents* first, through the held snapshot
// directory capability (`unlinkat` relative to the retained directory fd), so a
// competitor that swaps the top-level path cannot redirect content deletion into
// a different directory, and so an identity-unavailable transaction still frees
// its snapshot files instead of leaking the whole tree. It then removes the
// now-empty top-level entry through the held parent capability only after
// confirming the entry's exact stable identity still matches the value captured
// at creation.
//
// This is a fail-closed staleness check, not an atomic unlink-if-identity: a
// same-UID active attacker could still replace the entry in the window between
// the fstatat comparison and the unlinkat. Defending that window would require
// an atomic claim/quarantine or a private mount namespace, which is out of
// scope for this slice. `capturedIdentity` == none means no exact identity was
// available, so the (now-empty) top-level entry is never removed and the
// obligation is IdentityUnavailable. Never throws: a fault is attributed to its
// stage.
TreeCleanupOutcome removeSnapshotTree(
    const zc::Directory& parentDir, const zc::Directory& snapshotDir, zc::StringPtr leafName,
    const zc::Maybe<StableDirectoryIdentity>& capturedIdentity) noexcept {
  TreeCleanupOutcome outcome = cleanupFailed(CleanupFailureKind::ContentRemovalFailed);
  bool reachedTopLevel = false;
  auto exception = zc::runCatchingExceptions([&]() {
    // Remove the tree's contents through the held snapshot directory capability,
    // unconditionally (before any identity decision), so even an identity-
    // unavailable transaction frees its snapshot files. The snapshot tree in this
    // slice is a flat set of files (no subdirectories).
    for (const zc::String& name : snapshotDir.listNames()) {
      if (!snapshotDir.tryRemove(zc::Path(name))) {
        outcome = cleanupFailed(CleanupFailureKind::ContentRemovalFailed);
        return;
      }
    }

    // No exact identity: the contents are freed, but never auto-remove the
    // top-level entry without an owner proof. Leave the empty directory and
    // report the obligation for explicit recovery.
    if (capturedIdentity == zc::none) {
      outcome = cleanupFailed(CleanupFailureKind::IdentityUnavailable);
      return;
    }
    const StableDirectoryIdentity& expected = ZC_REQUIRE_NONNULL(capturedIdentity);

    reachedTopLevel = true;
    // Re-check the top-level entry's exact identity through the held parent fd
    // just before removing it, so a swapped object at the same name is refused.
    ZC_IF_SOME(parentFd, parentDir.getFd()) {
      struct stat st;
      auto leaf = zc::heapString(leafName);
      if (::fstatat(parentFd, leaf.cStr(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
        // Only ENOENT means the entry is genuinely gone (contents removed and the
        // directory no longer resolves), which discharges the obligation. Any
        // other errno (EACCES, EIO, ENOTDIR, EBADF, ...) means we could not
        // confirm the entry is gone: since an exact identity was captured, that
        // is a top-level removal fault, not "removed".
        outcome = errno == ENOENT ? treeRemoved()
                                  : cleanupFailed(CleanupFailureKind::TopLevelRemovalFailed);
        return;
      }
      if (!expected.matches(static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino))) {
        outcome = cleanupFailed(CleanupFailureKind::IdentityMismatch);
        return;
      }
      if (!parentDir.tryRemove(zc::Path(leaf))) {
        outcome = cleanupFailed(CleanupFailureKind::TopLevelRemovalFailed);
        return;
      }
      outcome = treeRemoved();
    } else {
      // The parent exposes no descriptor: cannot make an exact pre-removal
      // check, so fail closed.
      outcome = cleanupFailed(CleanupFailureKind::IdentityUnavailable);
    }
  });
  if (exception != zc::none) {
    // A thrown filesystem fault is attributed to the stage it occurred in.
    return reachedTopLevel ? cleanupFailed(CleanupFailureKind::TopLevelRemovalFailed)
                           : cleanupFailed(CleanupFailureKind::ContentRemovalFailed);
  }
  return outcome;
}

}  // namespace

#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED

// =======================================================================================
// SnapshotTransactionId

zc::Maybe<SnapshotTransactionId> SnapshotTransactionId::fromBytes(
    zc::ArrayPtr<const uint8_t> bytes) {
  if (bytes.size() != 16) { return zc::none; }
  SnapshotTransactionId id;
  for (size_t index = 0; index < 16; ++index) { id.value[index] = bytes[index]; }
  return id;
}

zc::String SnapshotTransactionId::toHex() const {
  static constexpr char kHex[] = "0123456789abcdef";
  auto chars = zc::heapArray<char>(32);
  for (size_t index = 0; index < 16; ++index) {
    chars[index * 2] = kHex[(value[index] >> 4) & 0x0f];
    chars[index * 2 + 1] = kHex[value[index] & 0x0f];
  }
  return zc::heapString(chars.begin(), chars.size());
}

// =======================================================================================
// SnapshotCleanupObligation

zc::String SnapshotCleanupObligation::treePath() const {
  if (outputParentValue == "/"_zc) {
    return zc::str("/", kSnapshotDirPrefix, transactionIdValue.toHex());
  }
  return zc::str(outputParentValue, "/", kSnapshotDirPrefix, transactionIdValue.toHex());
}

// =======================================================================================
// PreparedLinkInputs directory-identity capture

#if ZOM_LINK_SNAPSHOT_SUPPORTED

zc::Maybe<StableDirectoryIdentity> PreparedLinkInputs::captureDirectoryIdentity(
    const zc::Directory& dir) noexcept {
  zc::Maybe<StableDirectoryIdentity> result;
  auto exception = zc::runCatchingExceptions([&]() {
    ZC_IF_SOME(fd, dir.getFd()) {
      struct stat st;
      if (::fstat(fd, &st) == 0) {
        result = detail::LinkSnapshotMinter::identity(static_cast<uint64_t>(st.st_dev),
                                                      static_cast<uint64_t>(st.st_ino));
      }
    }
  });
  if (exception != zc::none) { return zc::none; }
  return result;
}

#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED

// =======================================================================================
// Shared transaction-root ownership

#if ZOM_LINK_SNAPSHOT_SUPPORTED

namespace {

// The tree-owning state of one link transaction: the held parent and snapshot
// directory capabilities, the typed token and canonical parent the tree path is
// derived from, the exact captured directory identity, and the owning plan id.
// Both PreparedLinkInputs (before the driver runs) and LinkedOutputCandidate
// (after a successful link, holding the still-live root) hold exactly this state,
// so the removal and obligation logic lives in one place and is moved wholesale
// from the prepared capability into the output candidate on the success path.
struct SnapshotTreeOwnership {
  zc::Own<const zc::Directory> parentDir;
  zc::Own<const zc::Directory> snapshotDir;
  SnapshotTransactionId transactionId;
  zc::String outputParent;
  zc::String leafName;
  zc::Maybe<StableDirectoryIdentity> directoryIdentity;
  LinkPlanId planId;

  // Removes this transaction's tree. Never throws.
  TreeCleanupOutcome cleanup() noexcept {
    return removeSnapshotTree(*parentDir, *snapshotDir, leafName, directoryIdentity);
  }

  // Builds a cleanup obligation for this transaction at `stage` from a non-
  // removed cleanup outcome. Moves the owned parent string so no allocation
  // happens on the noexcept cleanup path. Consumes `outputParent`.
  SnapshotCleanupObligation obligation(const TreeCleanupOutcome& result,
                                       CleanupStage stage) noexcept {
    zc::Maybe<StableDirectoryIdentity> identityCopy;
    ZC_IF_SOME(value, directoryIdentity) {
      identityCopy = detail::LinkSnapshotMinter::identity(value.device(), value.inode());
    }
    return detail::LinkSnapshotMinter::obligation(transactionId, zc::mv(outputParent),
                                                  zc::mv(identityCopy), planId, result.failureKind,
                                                  stage);
  }

  // Removes the tree and reports the resource-cleanup disposition, at `stage`.
  CleanupDisposition discard(CleanupStage stage) noexcept {
    TreeCleanupOutcome outcome = cleanup();
    if (outcome.removed) { return CleanupDisposition::clean(); }
    return CleanupDisposition::obligated(obligation(outcome, stage));
  }
};

}  // namespace

#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED

// =======================================================================================
// PreparedLinkInputs

struct PreparedLinkInputs::Impl {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  // The transaction-root ownership (held capabilities, token, identity, plan id).
  // Moved wholesale into the LinkedOutputCandidate on the success path.
  SnapshotTreeOwnership tree;
  // The driver snapshot: the very ReadableFile whose bytes pass-2 re-verified,
  // retained and exec'd directly - there is no second pathname reopen between
  // hash and exec. There is deliberately no writable alias.
  zc::Own<const zc::ReadableFile> driverSnapshot;
  zc::String program;
  zc::Array<zc::String> argvValues;
  zc::String workingDirectory;
  zc::Array<zc::String> environmentValues;
  // The absolute path of the `<root>/output-candidate` the driver's `-o` names.
  // The file is opened relative to `tree.snapshotDir`, not resolved from this
  // string; it is recorded so the candidate can report it.
  zc::String outputCandidatePath;

  // Removes this transaction's tree. Never throws.
  TreeCleanupOutcome cleanup() noexcept { return tree.cleanup(); }
#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED
};

PreparedLinkInputs::PreparedLinkInputs(zc::Own<Impl> implParam) noexcept
    : impl(zc::mv(implParam)) {}

// Move operations are defined out of line because `Impl` is only complete here,
// so a consumer translation unit can move the capability without seeing `Impl`.
PreparedLinkInputs::PreparedLinkInputs(PreparedLinkInputs&&) noexcept = default;
PreparedLinkInputs& PreparedLinkInputs::operator=(PreparedLinkInputs&&) noexcept = default;

PreparedLinkInputs::~PreparedLinkInputs() noexcept {
  // A moved-from object (including one consumed by discardAndCleanup or
  // intoCandidate) owns no tree. Otherwise this is a last-resort leak guard:
  // neither explicit consumer was called, so best-effort remove the tree and
  // swallow any fault. It produces no structured record - the explicit
  // discardAndCleanup path is the one that does.
  if (impl.get() == nullptr) { return; }
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  (void)impl->cleanup();
#endif
}

CleanupDisposition PreparedLinkInputs::discardAndCleanup() && noexcept {
  // Consume the capability: move the impl out so the destructor becomes a no-op
  // and cannot re-remove the tree after this reports an obligation.
  zc::Own<Impl> owned = zc::mv(impl);
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  // tree.discard moves already-owned fields on the obligation branch; no
  // allocation, so this noexcept function cannot terminate on a bad_alloc.
  return owned->tree.discard(CleanupStage::PostSpawnCleanup);
#else
  (void)owned;
  return CleanupDisposition::clean();
#endif
}

const zc::Directory& PreparedLinkInputs::snapshotDirectory() const noexcept {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  return *impl->tree.snapshotDir;
#else
  ZC_IREQUIRE(false, "PreparedLinkInputs snapshot directory requires the snapshot machinery");
  ZC_UNREACHABLE
#endif
}

zc::StringPtr PreparedLinkInputs::outputCandidatePath() const noexcept {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  return impl->outputCandidatePath;
#else
  return zc::StringPtr();
#endif
}

// PreparedLinkInputs::intoCandidate is defined after the LinkOutputCandidateFactory
// (below), since it needs that attorney and the complete LinkedOutputCandidate::Impl.

int PreparedLinkInputs::driverDescriptor() const {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  ZC_IF_SOME(fd, impl->driverSnapshot->getFd()) { return fd; }
#endif
  ZC_IREQUIRE(false, "PreparedLinkInputs driver snapshot must expose a descriptor");
  ZC_UNREACHABLE
}

zc::StringPtr PreparedLinkInputs::program() const noexcept { return impl->program; }
zc::ArrayPtr<const zc::String> PreparedLinkInputs::argv() const noexcept {
  return impl->argvValues.asPtr();
}
zc::StringPtr PreparedLinkInputs::workingDirectory() const noexcept {
  return impl->workingDirectory;
}
zc::ArrayPtr<const zc::String> PreparedLinkInputs::environment() const noexcept {
  return impl->environmentValues.asPtr();
}

#if ZOM_LINK_SNAPSHOT_SUPPORTED

namespace {

// A rejection produced by the snapshot worker: an RFC failure kind and ordinal.
struct PrepareRejection {
  IrFailureKind kind;
  uint32_t ordinal;
};

}  // namespace

// The shared prepare body, parameterized by the transaction-token source. It is
// the member entry point (so it can mint transaction ids, capture directory
// identities, and construct the capability); `prepare` delegates to it with the
// production CSPRNG source. The filesystem is whatever the caller supplies; a
// test may pass a fault-injecting wrapper whose File::write / ReadableFile::read
// / Directory::tryRemove fail, and those faults propagate as ordinary rejections
// or cleanup obligations.
CleanupAwareOutcome<PreparedLinkInputs> PreparedLinkInputs::prepareWithTokenSource(
    const VerifiedLinkPlan& plan, const zc::Filesystem& filesystem,
    SnapshotTokenSource& tokenSource) {
  const zc::Directory& filesystemRoot = filesystem.getRoot();
  const ToolchainClosureRecord& closure = plan.toolchainClosure();

  using Outcome = CleanupAwareOutcome<PreparedLinkInputs>;
  auto rejectComplete = [](IrFailureKind kind, uint32_t ordinal) -> Outcome {
    return Outcome::complete(rejectLinkerInvocation<PreparedLinkInputs>(kind, ordinal));
  };

  // The root must expose real descriptors: the exact-identity owner proof and
  // exec-by-descriptor both require them, so a descriptor-less (in-memory) root
  // fails closed before any tree is created.
  if (filesystemRoot.getFd() == zc::none) {
    return rejectComplete(IrFailureKind::OutputCreationFailed, 0);
  }

  // The entry symbol and output parent are required to build the invocation.
  zc::Maybe<zc::String> entryArgument = bytesToArgument(plan.entrySymbol());
  if (entryArgument == zc::none) { return rejectComplete(IrFailureKind::InvalidFact, 1); }
  zc::StringPtr outputPath = plan.outputPath();
  zc::Maybe<zc::String> outputParentMaybe = parentDirectory(outputPath);
  if (outputParentMaybe == zc::none) { return rejectComplete(IrFailureKind::InvalidFact, 2); }
  zc::String outputParent = ZC_REQUIRE_NONNULL(zc::mv(outputParentMaybe));

  // Enumerate every input in RFC 0043 canonical order with a snapshot name
  // derived from its role and canonical index (never a source basename, which
  // could collide across distinct source paths). The driver is snapshotted too;
  // it alone is executable and is the exec target.
  auto entry = [&](zc::StringPtr sourcePath, const identity::Sha256Digest& digest,
                   uint64_t byteCount, zc::String&& name, bool executable) {
    return SnapshotPlan{zc::str(sourcePath), digest, byteCount, zc::mv(name), executable};
  };
  zc::Vector<SnapshotPlan> driverPlan;
  driverPlan.add(entry(closure.linkerPath(), closure.linkerDigest(), closure.linkerByteCount(),
                       zc::str("driver"), true));
  zc::Vector<SnapshotPlan> orderedInputs;
  {
    size_t index = 0;
    for (const LinkInputRecord& record : closure.crtObjects()) {
      orderedInputs.add(entry(record.path(), record.digest(), record.byteCount(),
                              zc::str("crt-", index++), false));
    }
    index = 0;
    for (const LinkInputRecord& record : plan.objectRecords()) {
      orderedInputs.add(entry(record.path(), record.digest(), record.byteCount(),
                              zc::str("obj-", index++), false));
    }
    index = 0;
    for (const LinkInputRecord& record : plan.runtimeRecords()) {
      orderedInputs.add(entry(record.path(), record.digest(), record.byteCount(),
                              zc::str("rt-", index++), false));
    }
    index = 0;
    for (const LinkInputRecord& record : closure.defaultLibraries()) {
      orderedInputs.add(entry(record.path(), record.digest(), record.byteCount(),
                              zc::str("lib-", index++), false));
    }
  }

  // Open the parent and create the transaction-private tree. Every filesystem
  // call here can throw (a fault-injecting wrapper, a real ENOSPC or EACCES,
  // ...); none may escape the CleanupAwareOutcome. These all run before this
  // transaction owns any committed tree - the loop stops the instant a directory
  // is exclusively created, and nothing after that point throws - so a fault here
  // maps to a Complete rejection with nothing to clean up.
  zc::Own<const zc::Directory> parentDir;
  SnapshotTransactionId token;
  zc::String leafName;
  zc::Own<const zc::Directory> snapshotDir;
  bool parentOpened = false;
  bool tokenSourceFailed = false;
  bool created = false;
  constexpr int kMaxTokenAttempts = 8;
  auto acquireException = zc::runCatchingExceptions([&]() {
    zc::String parentRelPath =
        outputParent == "/"_zc ? zc::str(".") : zc::heapString(outputParent.slice(1));
    zc::Maybe<zc::Own<const zc::Directory>> parentMaybe = filesystemRoot.tryOpenSubdir(
        zc::Path::parse(parentRelPath),
        zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);
    ZC_IF_SOME(dir, parentMaybe) {
      parentDir = zc::mv(dir);
      parentOpened = true;
    } else {
      return;
    }

    // Create a fresh, transaction-private snapshot directory under an unpredictable,
    // exclusively-created name. Never pre-delete a path: on a name collision,
    // generate a fresh token and retry, and never unlink an existing tree that
    // another transaction may own.
    for (int attempt = 0; attempt < kMaxTokenAttempts && !created; ++attempt) {
      uint8_t raw[16];
      if (!tokenSource.nextToken(raw)) {
        // The token source is unavailable: fail closed, no tree was created.
        tokenSourceFailed = true;
        return;
      }
      token = ZC_REQUIRE_NONNULL(detail::LinkSnapshotMinter::transactionId(zc::arrayPtr(raw, 16)));
      leafName = snapshotDirName(token);
      // Exclusive create (no MODIFY) so an existing directory is never adopted;
      // PRIVATE so it is owner-only. Opened relative to the held parent capability.
      // On EEXIST tryOpenSubdir returns none and we retry with a fresh token;
      // the existing tree is never touched.
      zc::Maybe<zc::Own<const zc::Directory>> opened = parentDir->tryOpenSubdir(
          zc::Path(leafName), zc::WriteMode::CREATE | zc::WriteMode::PRIVATE);
      ZC_IF_SOME(dir, opened) {
        snapshotDir = zc::mv(dir);
        created = true;
      }
    }
  });
  // A pre-ownership filesystem fault is a Complete rejection: no committed tree
  // exists (the loop never leaves a created directory behind on a throwing path).
  if (acquireException != zc::none) {
    return rejectComplete(IrFailureKind::OutputCreationFailed, 3);
  }
  if (!parentOpened) { return rejectComplete(IrFailureKind::OutputCreationFailed, 3); }
  if (tokenSourceFailed) { return rejectComplete(IrFailureKind::OutputCreationFailed, 4); }
  if (!created) { return rejectComplete(IrFailureKind::OutputCreationFailed, 5); }

  // Capture the exact stable identity right after creation; it is re-checked
  // before any removal.
  zc::Maybe<StableDirectoryIdentity> directoryIdentity =
      PreparedLinkInputs::captureDirectoryIdentity(*snapshotDir);

  // A helper that rolls back the freshly created tree and reports the outcome for
  // a prepare-time rejection.
  auto rejectWithRollback = [&](IrFailureKind kind, uint32_t ordinal) -> Outcome {
    TreeCleanupOutcome cleanup =
        removeSnapshotTree(*parentDir, *snapshotDir, leafName, directoryIdentity);
    auto primary = rejectLinkerInvocation<PreparedLinkInputs>(kind, ordinal);
    if (cleanup.removed) { return Outcome::complete(zc::mv(primary)); }
    zc::Maybe<StableDirectoryIdentity> identityCopy;
    ZC_IF_SOME(value, directoryIdentity) {
      identityCopy = detail::LinkSnapshotMinter::identity(value.device(), value.inode());
    }
    SnapshotCleanupObligation obligation = detail::LinkSnapshotMinter::obligation(
        token, zc::heapString(outputParent), zc::mv(identityCopy), plan.id(), cleanup.failureKind,
        CleanupStage::PrepareRollback);
    return Outcome::recoveryRequired(zc::mv(primary), zc::mv(obligation));
  };

  // Fail closed if no exact identity was captured: never proceed to snapshot,
  // build a Prepared capability, or spawn without an owner proof. Roll back the
  // content through the held capability, never auto-remove the top-level entry,
  // and report an IdentityUnavailable obligation.
  if (directoryIdentity == zc::none) {
    return rejectWithRollback(IrFailureKind::OutputCreationFailed, 6);
  }

  // The snapshot work: read + verify each source, write it into the private tree,
  // re-verify each snapshot copy from its own read handle, retain the driver
  // snapshot handle for exec, and build the rewritten argv. Any filesystem fault
  // (including one injected by a wrapper's File::write / ReadableFile::read) is
  // caught and turned into a rejection rather than escaping.
  zc::Maybe<PrepareRejection> rejection;
  zc::Own<PreparedLinkInputs::Impl> preparedImpl;
  auto workException = zc::runCatchingExceptions([&]() {
    auto snapshotOne = [&](const SnapshotPlan& item) -> bool {
      zc::Maybe<zc::Array<zc::byte>> sourceBytes = tryReadAbsolute(filesystemRoot, item.sourcePath);
      if (sourceBytes == zc::none) { return false; }
      zc::Array<zc::byte> bytes = ZC_REQUIRE_NONNULL(zc::mv(sourceBytes));
      if (!bytesMatch(bytes.asPtr(), item.digest, item.byteCount)) { return false; }
      // Exclusive create (no MODIFY) so an existing file is never reused or
      // overwritten; PRIVATE so the snapshot is owner-only. Relative to the held
      // snapshot directory capability.
      zc::WriteMode mode = zc::WriteMode::CREATE | zc::WriteMode::PRIVATE;
      if (item.executable) { mode = mode | zc::WriteMode::EXECUTABLE; }
      zc::Maybe<zc::Own<const zc::File>> file =
          snapshotDir->tryOpenFile(zc::Path(item.snapshotName), mode);
      if (file == zc::none) { return false; }
      // The write itself may fault (a fault-injecting wrapper models a mid-write
      // failure here); the exception propagates to the outer catcher.
      ZC_REQUIRE_NONNULL(zc::mv(file))->writeAll(bytes.asPtr());
      return true;
    };
    for (const SnapshotPlan& item : driverPlan) {
      if (!snapshotOne(item)) {
        rejection = PrepareRejection{IrFailureKind::InputRevisionMismatch, 7};
        return;
      }
    }
    for (const SnapshotPlan& item : orderedInputs) {
      if (!snapshotOne(item)) {
        rejection = PrepareRejection{IrFailureKind::InputRevisionMismatch, 8};
        return;
      }
    }

    // Re-verify every snapshot copy from its own read handle. The write handles
    // above have been dropped, so these read-only opens hold no writable alias.
    // The driver's re-opened handle is RETAINED and becomes the exec target -
    // there is no later second pathname reopen of the driver.
    zc::Own<const zc::ReadableFile> driverSnapshot;
    bool driverVerified = false;
    auto reverifyOne = [&](const SnapshotPlan& item, bool isDriver) -> bool {
      zc::Maybe<zc::Own<const zc::ReadableFile>> file =
          snapshotDir->tryOpenFile(zc::Path(item.snapshotName));
      if (file == zc::none) { return false; }
      zc::Own<const zc::ReadableFile> handle = ZC_REQUIRE_NONNULL(zc::mv(file));
      // The read itself may fault (a fault-injecting wrapper models a pass-2 read
      // failure here); the exception propagates to the outer catcher.
      zc::Array<zc::byte> bytes = handle->readAllBytes();
      if (!bytesMatch(bytes.asPtr(), item.digest, item.byteCount)) { return false; }
      if (isDriver) {
        driverSnapshot = zc::mv(handle);
        driverVerified = true;
      }
      return true;
    };
    for (const SnapshotPlan& item : driverPlan) {
      if (!reverifyOne(item, /*isDriver=*/true)) {
        rejection = PrepareRejection{IrFailureKind::InputRevisionMismatch, 9};
        return;
      }
    }
    for (const SnapshotPlan& item : orderedInputs) {
      if (!reverifyOne(item, /*isDriver=*/false)) {
        rejection = PrepareRejection{IrFailureKind::InputRevisionMismatch, 10};
        return;
      }
    }

    // The exec target is exactly the driver handle whose bytes were re-verified.
    if (!driverVerified || driverSnapshot->getFd() == zc::none) {
      rejection = PrepareRejection{IrFailureKind::OutputCreationFailed, 11};
      return;
    }

    // Build the rewritten argument vector once, now that every input verified.
    zc::String treePath = snapshotTreePath(outputParent, token);
    zc::String driverSnapshotPath = joinPath(treePath, driverPlan[0].snapshotName);
    // RFC 0043 D4: the driver writes into the transaction root, never the final
    // path. `-o` names `<root>/output-candidate`; the final output request stays
    // in the plan and is honored only by the later D1 publication step.
    zc::String outputCandidatePath = joinPath(treePath, kOutputCandidateName);
    zc::Vector<zc::String> argv;
    argv.add(zc::str(driverSnapshotPath));
    argv.add(zc::str("-o"));
    argv.add(zc::str(outputCandidatePath));
    argv.add(zc::str("-e"));
    argv.add(zc::str(ZC_REQUIRE_NONNULL(entryArgument)));
    for (const SnapshotPlan& item : orderedInputs) {
      argv.add(joinPath(treePath, item.snapshotName));
    }

    auto builtImpl = zc::heap<PreparedLinkInputs::Impl>();
    builtImpl->tree.parentDir = parentDir->clone();
    builtImpl->tree.snapshotDir = snapshotDir->clone();
    builtImpl->tree.transactionId = token;
    builtImpl->tree.outputParent = zc::heapString(outputParent);
    builtImpl->tree.leafName = zc::heapString(leafName);
    builtImpl->tree.directoryIdentity = zc::mv(directoryIdentity);
    builtImpl->tree.planId = plan.id();
    builtImpl->driverSnapshot = zc::mv(driverSnapshot);
    builtImpl->program = zc::mv(driverSnapshotPath);
    builtImpl->argvValues = argv.releaseAsArray();
    builtImpl->workingDirectory = zc::heapString(outputParent);
    builtImpl->environmentValues = zc::Array<zc::String>();
    builtImpl->outputCandidatePath = zc::mv(outputCandidatePath);
    preparedImpl = zc::mv(builtImpl);
  });

  // A filesystem fault during the work is an OutputCreationFailed rejection.
  if (workException != zc::none && rejection == zc::none) {
    rejection = PrepareRejection{IrFailureKind::OutputCreationFailed, 12};
  }

  if (rejection == zc::none && preparedImpl.get() != nullptr) {
    // Success: the tree is live and owned by the returned capability.
    return Outcome::complete(
        IrOperationResult<PreparedLinkInputs>::verified(PreparedLinkInputs(zc::mv(preparedImpl))));
  }

  PrepareRejection reject = ZC_REQUIRE_NONNULL(zc::mv(rejection));
  return rejectWithRollback(reject.kind, reject.ordinal);
}

CleanupAwareOutcome<PreparedLinkInputs> PreparedLinkInputs::prepare(
    const VerifiedLinkPlan& plan, const zc::Filesystem& filesystem) {
  CsprngTokenSource csprng;
  return prepareWithTokenSource(plan, filesystem, csprng);
}

#else  // !ZOM_LINK_SNAPSHOT_SUPPORTED

CleanupAwareOutcome<PreparedLinkInputs> PreparedLinkInputs::prepare(const VerifiedLinkPlan&,
                                                                    const zc::Filesystem&) {
  // The snapshot machinery requires Linux syscalls; on other platforms it fails
  // closed with a capability rejection and never touches the filesystem.
  return CleanupAwareOutcome<PreparedLinkInputs>::complete(
      rejectLinkerInvocation<PreparedLinkInputs>(IrFailureKind::OutputCreationFailed, 0));
}

CleanupAwareOutcome<PreparedLinkInputs> PreparedLinkInputs::prepareWithTokenSource(
    const VerifiedLinkPlan&, const zc::Filesystem&, SnapshotTokenSource&) {
  return CleanupAwareOutcome<PreparedLinkInputs>::complete(
      rejectLinkerInvocation<PreparedLinkInputs>(IrFailureKind::OutputCreationFailed, 0));
}

#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED

// =======================================================================================
// LinkedOutputCandidate

struct LinkedOutputCandidate::Impl {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  // The still-live transaction root, transferred from the PreparedLinkInputs on
  // the success path. The candidate is its sole owner until discardAndCleanup.
  SnapshotTreeOwnership tree;
  // The moved-in verified plan: the single authority for the publication context.
  VerifiedLinkPlan plan;
  // The transaction-owned no-follow read-only handle to <root>/output-candidate,
  // and the exact identity + byte count captured from that same handle.
  zc::Own<const zc::ReadableFile> outputHandle;
  StableFileIdentity outputIdentity;
  uint64_t outputSize;
  zc::String outputCandidatePath;
#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED
};

namespace detail {
// The attorney that mints a LinkedOutputCandidate from the transferred
// transaction state. Defined here (not in the public header) so the public
// surface names a neutral factory while the snapshot internals stay private.
class LinkOutputCandidateFactory {
public:
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  static LinkedOutputCandidate create(SnapshotTreeOwnership&& tree, VerifiedLinkPlan&& plan,
                                      zc::Own<const zc::ReadableFile>&& outputHandle,
                                      StableFileIdentity outputIdentity, uint64_t outputSize,
                                      zc::String&& outputCandidatePath) noexcept {
    auto impl = zc::heap<LinkedOutputCandidate::Impl>(LinkedOutputCandidate::Impl{
        zc::mv(tree), zc::mv(plan), zc::mv(outputHandle), zc::mv(outputIdentity), outputSize,
        zc::mv(outputCandidatePath)});
    return LinkedOutputCandidate(zc::mv(impl));
  }
#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED
};
}  // namespace detail

LinkedOutputCandidate PreparedLinkInputs::intoCandidate(
    VerifiedLinkPlan&& plan, zc::Own<const zc::ReadableFile>&& outputHandle,
    StableFileIdentity outputIdentity, uint64_t outputSize) && noexcept {
  // Consume the capability: move the tree ownership out so the destructor becomes
  // a no-op and never removes the tree - the returned candidate owns it now.
  zc::Own<Impl> owned = zc::mv(impl);
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  return detail::LinkOutputCandidateFactory::create(zc::mv(owned->tree), zc::mv(plan),
                                                    zc::mv(outputHandle), zc::mv(outputIdentity),
                                                    outputSize, zc::mv(owned->outputCandidatePath));
#else
  (void)owned;
  (void)plan;
  (void)outputHandle;
  (void)outputIdentity;
  (void)outputSize;
  ZC_IREQUIRE(false, "intoCandidate requires the snapshot machinery");
  ZC_UNREACHABLE
#endif
}

LinkedOutputCandidate::LinkedOutputCandidate(zc::Own<Impl> implParam) noexcept
    : impl(zc::mv(implParam)) {}

LinkedOutputCandidate::LinkedOutputCandidate(LinkedOutputCandidate&&) noexcept = default;
LinkedOutputCandidate& LinkedOutputCandidate::operator=(LinkedOutputCandidate&&) noexcept = default;

LinkedOutputCandidate::~LinkedOutputCandidate() noexcept {
  // A moved-from candidate (including one consumed by discardAndCleanup) owns no
  // tree. Otherwise this is a last-resort leak guard: discardAndCleanup was never
  // called, so best-effort remove the tree and swallow any fault, producing no
  // structured record.
  if (impl.get() == nullptr) { return; }
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  (void)impl->tree.cleanup();
#endif
}

const VerifiedLinkPlan& LinkedOutputCandidate::plan() const noexcept {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  return impl->plan;
#else
  ZC_IREQUIRE(false, "LinkedOutputCandidate requires the snapshot machinery");
  ZC_UNREACHABLE
#endif
}

const StableFileIdentity& LinkedOutputCandidate::outputIdentity() const noexcept {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  return impl->outputIdentity;
#else
  ZC_IREQUIRE(false, "LinkedOutputCandidate requires the snapshot machinery");
  ZC_UNREACHABLE
#endif
}

uint64_t LinkedOutputCandidate::outputSize() const noexcept {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  return impl->outputSize;
#else
  return 0;
#endif
}

zc::Array<uint8_t> LinkedOutputCandidate::readOutput() const {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  // Read through the transaction-owned handle - the single physical authority for
  // the linker output - not by re-opening a pathname.
  zc::Array<zc::byte> bytes = impl->outputHandle->readAllBytes();
  auto owned = zc::heapArray<uint8_t>(bytes.size());
  for (size_t index = 0; index < bytes.size(); ++index) {
    owned[index] = static_cast<uint8_t>(bytes[index]);
  }
  return owned;
#else
  ZC_IREQUIRE(false, "LinkedOutputCandidate requires the snapshot machinery");
  ZC_UNREACHABLE
#endif
}

zc::StringPtr LinkedOutputCandidate::outputCandidatePath() const noexcept {
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  return impl->outputCandidatePath;
#else
  return zc::StringPtr();
#endif
}

CleanupDisposition LinkedOutputCandidate::discardAndCleanup() && noexcept {
  // Consume the candidate: move the impl out so the destructor becomes a no-op
  // and cannot re-remove the tree after this reports an obligation.
  zc::Own<Impl> owned = zc::mv(impl);
#if ZOM_LINK_SNAPSHOT_SUPPORTED
  // Drop the output handle before removing the tree so no descriptor keeps the
  // snapshot inode pinned during removal.
  owned->outputHandle = nullptr;
  return owned->tree.discard(CleanupStage::PostSpawnCleanup);
#else
  (void)owned;
  return CleanupDisposition::clean();
#endif
}

// =======================================================================================
// linkExecutable

#if ZOM_LINK_SNAPSHOT_SUPPORTED

CleanupAwareOutcome<LinkedOutputCandidate> linkExecutable(VerifiedLinkPlan&& plan,
                                                          const zc::Filesystem& filesystem) {
  const zc::Directory& filesystemRoot = filesystem.getRoot();
  using Outcome = CleanupAwareOutcome<LinkedOutputCandidate>;

  // Reject a pre-existing file at the plan's FINAL output path up front (INV-1
  // no-clobber): a link never overwrites an existing final artifact. This does
  // not create, write, or remove the final path - the driver writes only into the
  // transaction root, and the D1 publication step is the sole writer of the final
  // path. The existence probe can throw (a faulting filesystem); since no tree
  // exists yet, a throw is a Complete rejection with nothing to clean up.
  zc::StringPtr outputPath = plan.outputPath();
  if (outputPath.size() < 2 || outputPath[0] != '/') {
    return Outcome::complete(
        rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::InvalidFact, 20));
  }
  zc::Path outputRelative = zc::Path::parse(outputPath.slice(1));
  bool outputExists = false;
  auto existsException =
      zc::runCatchingExceptions([&]() { outputExists = filesystemRoot.exists(outputRelative); });
  if (existsException != zc::none) {
    return Outcome::complete(
        rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 26));
  }
  if (outputExists) {
    return Outcome::complete(
        rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::InvalidFact, 21));
  }

  // Snapshot and re-verify every input into the unified transaction root.
  CleanupAwareOutcome<PreparedLinkInputs> preparedOutcome =
      PreparedLinkInputs::prepare(plan, filesystem);
  if (preparedOutcome.isRecoveryRequired()) {
    // Preparation itself could not clean up (it never returns RecoveryRequired
    // with a verified PreparedLinkInputs, so its primary is always a rejection).
    RecoveryRequiredOutcome<PreparedLinkInputs> rr = zc::mv(preparedOutcome).takeRecoveryRequired();
    return Outcome::recoveryRequired(remapPreparedRejection(zc::mv(rr.primary)),
                                     zc::mv(rr.obligation));
  }
  IrOperationResult<PreparedLinkInputs> preparedResult = zc::mv(preparedOutcome).takeComplete();
  if (!preparedResult.isVerified()) {
    return Outcome::complete(remapPreparedRejection(zc::mv(preparedResult)));
  }
  PreparedLinkInputs prepared = zc::mv(preparedResult).takeVerified();

  // From here a transaction root is committed and owned by `prepared`. Every
  // remaining step - the spawn (pipe/fork/poll can raise) and the output capture -
  // runs inside a closed catcher that produces a rejection instead of unwinding.
  // On a rejection the tree is discarded through `discardAndCleanup` (which never
  // touches the final path); on success the tree ownership is transferred into the
  // returned candidate and NOT removed.

  // A successfully captured output candidate, populated only on the clean path.
  struct CapturedOutput {
    zc::Own<const zc::ReadableFile> handle;
    zc::Maybe<StableFileIdentity> identity;
    uint64_t size = 0;
  };

  // Opens `<root>/output-candidate` with a no-follow open relative to the held
  // snapshot directory, confirms it is a regular, non-empty, singly-linked file
  // whose exact identity matches the same handle's, and captures identity + size.
  // The directory-entry no-follow open and the fstat of the returned handle are a
  // pair: a symlink at the entry is refused by O_NOFOLLOW, and the handle is the
  // only object inspected, so a swap between open and stat cannot smuggle a
  // different inode. Returns none (fail closed) on any structural violation.
  auto captureOutput = [&](CapturedOutput& out) -> bool {
    const zc::Directory& snapshotDir = prepared.snapshotDirectory();
    ZC_IF_SOME(rootFd, snapshotDir.getFd()) {
      int fd = ::openat(rootFd, kOutputCandidateName.cStr(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      if (fd < 0) { return false; }
      zc::OwnFd owned(fd);
      struct stat st;
      if (::fstat(owned.get(), &st) != 0) { return false; }
      // Regular file only: a symlink is already excluded by O_NOFOLLOW (which
      // fails with ELOOP), but a directory/fifo/device must also be rejected.
      if (!S_ISREG(st.st_mode)) { return false; }
      // Non-empty: an empty output is not a link result.
      if (st.st_size <= 0) { return false; }
      // Sole link: st_nlink == 1 proves the transaction is the only link to the
      // inode, so no external path can rewrite the bytes in place.
      if (st.st_nlink != 1) { return false; }
      out.identity = detail::LinkSnapshotMinter::fileIdentity(static_cast<uint64_t>(st.st_dev),
                                                              static_cast<uint64_t>(st.st_ino),
                                                              static_cast<uint64_t>(st.st_nlink));
      out.size = static_cast<uint64_t>(st.st_size);
      // Wrap the same descriptor in a ReadableFile the candidate owns; the exec
      // and this capture never re-open by pathname.
      out.handle = zc::newDiskReadableFile(zc::mv(owned));
      return true;
    }
    return false;
  };

  // Classify the spawn + output capture into either a verified CapturedOutput
  // (returned to the caller by transferring the tree) or a rejection.
  CapturedOutput captured;
  IrOperationResult<LinkedOutputCandidate> rejection =
      rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 27);
  bool linkVerified = false;

  auto spawnException = zc::runCatchingExceptions([&]() {
    // Spawn the driver by its snapshot descriptor with an explicit empty
    // environment. The rewritten argv points every input token at a snapshot path
    // and `-o` at `<root>/output-candidate`.
    zc::SubprocessCommand command(prepared.program());
    command.envPolicy(zc::SubprocessEnvPolicy::Empty);
    command.cwd(prepared.workingDirectory());
    command.executableDescriptor(prepared.driverDescriptor());
    zc::ArrayPtr<const zc::String> argv = prepared.argv();
    if (argv.size() >= 1) { command.argv0(argv[0]); }
    for (size_t index = 1; index < argv.size(); ++index) { command.arg(argv[index]); }
    zc::ArrayPtr<const zc::String> environment = prepared.environment();
    for (size_t index = 0; index + 1 < environment.size(); index += 2) {
      command.env(environment[index], environment[index + 1]);
    }

    zc::SubprocessResult spawnResult = command.run();
    if (!spawnResult.spawned()) {
      rejection =
          rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 22);
      return;
    }
    const zc::SubprocessOutput& output = spawnResult.output();
    if (output.terminationKind == zc::SubprocessTerminationKind::Signaled) {
      rejection =
          rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 23);
      return;
    }
    if (output.code != 0) {
      rejection =
          rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 24);
      return;
    }
    // Capture the output candidate through the held snapshot directory: no-follow
    // open + regular/non-empty/single-link/exact-identity, all from one handle.
    if (!captureOutput(captured) || captured.identity == zc::none) {
      rejection =
          rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 25);
      return;
    }
    linkVerified = true;
  });
  // A spawn/capture fault that escaped the classification is a capability
  // rejection; the tree is discarded below (never a final-path touch).
  if (spawnException != zc::none) {
    linkVerified = false;
    rejection =
        rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 28);
  }

  if (linkVerified) {
    // Success: transfer the still-live transaction root into the candidate. The
    // tree is NOT removed - Complete here means the obligation was transferred.
    StableFileIdentity identity = ZC_REQUIRE_NONNULL(zc::mv(captured.identity));
    LinkedOutputCandidate candidate = zc::mv(prepared).intoCandidate(
        zc::mv(plan), zc::mv(captured.handle), zc::mv(identity), captured.size);
    return Outcome::complete(IrOperationResult<LinkedOutputCandidate>::verified(zc::mv(candidate)));
  }

  // Rejection: discard the transaction root and pair the primary rejection with
  // the cleanup disposition. No public final path is ever touched.
  CleanupDisposition disposition = zc::mv(prepared).discardAndCleanup();
  if (disposition.isClean()) { return Outcome::complete(zc::mv(rejection)); }
  return Outcome::recoveryRequired(zc::mv(rejection), zc::mv(disposition).takeObligation());
}

#else  // !ZOM_LINK_SNAPSHOT_SUPPORTED

CleanupAwareOutcome<LinkedOutputCandidate> linkExecutable(VerifiedLinkPlan&&,
                                                          const zc::Filesystem&) {
  return CleanupAwareOutcome<LinkedOutputCandidate>::complete(
      rejectLinkerInvocation<LinkedOutputCandidate>(IrFailureKind::OutputCreationFailed, 20));
}

#endif  // ZOM_LINK_SNAPSHOT_SUPPORTED

}  // namespace zomlang::compiler::ir
