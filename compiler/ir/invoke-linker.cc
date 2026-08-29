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

#include <unistd.h>

#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/identity-invariant.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "zc/core/debug.h"
#include "zc/core/subprocess.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

namespace {

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

// Opens an absolute path under `root`, returning its bytes, or none if absent.
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

// Lowercase hex of the first `count` bytes of a digest, for a collision-resistant
// private-directory name derived from the plan identity.
zc::String hexPrefix(zc::ArrayPtr<const uint8_t> bytes, size_t count) {
  static constexpr char kHex[] = "0123456789abcdef";
  size_t take = zc::min(count, bytes.size());
  auto chars = zc::heapArray<char>(take * 2);
  for (size_t index = 0; index < take; ++index) {
    chars[index * 2] = kHex[(bytes[index] >> 4) & 0x0f];
    chars[index * 2 + 1] = kHex[bytes[index] & 0x0f];
  }
  return zc::heapString(chars.begin(), chars.size());
}

// One input to snapshot: its source absolute path, the plan's recorded digest
// and byte count, the snapshot file name, and whether it must be executable.
struct SnapshotPlan {
  zc::String sourcePath;
  identity::Sha256Digest digest;
  uint64_t byteCount;
  zc::String snapshotName;
  zc::String snapshotPath;  // absolute path of the snapshot copy
  bool executable;
};

// Verifies `bytes` against a recorded digest and byte count.
bool bytesMatch(zc::ArrayPtr<const zc::byte> bytes, const identity::Sha256Digest& digest,
                uint64_t byteCount) {
  if (bytes.size() != byteCount) { return false; }
  zc::Maybe<identity::Sha256Digest> computed = identity::sha256(bytes.asBytes());
  ZC_IF_SOME(value, computed) { return value == digest; }
  return false;
}

}  // namespace

// =======================================================================================
// PreparedLinkInputs

struct PreparedLinkInputs::Impl {
  // A clone of the filesystem root, kept so the destructor can remove the
  // private snapshot tree independently of the caller's root handle.
  zc::Own<const zc::Directory> rootHandle;
  // The private snapshot directory path relative to the root (for removal).
  zc::String snapshotRelPath;
  // The driver snapshot re-opened read-only: its descriptor is the exec target.
  // Kept alive so the borrowed descriptor stays valid across the spawn. There is
  // deliberately no writable alias to the same object.
  zc::Own<const zc::ReadableFile> driverSnapshot;
  zc::String program;
  zc::Array<zc::String> argvValues;
  zc::String workingDirectory;
  zc::Array<zc::String> environmentValues;
};

PreparedLinkInputs::PreparedLinkInputs(zc::Own<Impl> implParam) noexcept
    : impl(zc::mv(implParam)) {}

PreparedLinkInputs::~PreparedLinkInputs() noexcept(false) {
  // A moved-from object owns no tree.
  if (impl.get() == nullptr) { return; }
  // Remove the entire private snapshot tree. On the success path the caller keeps
  // this object alive across the spawn, so removal happens only after the linker
  // process has been awaited; on every failure path the tree is removed here too.
  impl->rootHandle->tryRemove(zc::Path::parse(impl->snapshotRelPath));
}

int PreparedLinkInputs::driverDescriptor() const {
  ZC_IF_SOME(fd, impl->driverSnapshot->getFd()) { return fd; }
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

IrOperationResult<PreparedLinkInputs> PreparedLinkInputs::prepare(
    const VerifiedLinkPlan& plan, const zc::Directory& filesystemRoot) {
  const ToolchainClosureRecord& closure = plan.toolchainClosure();

  // The entry symbol and output parent are required to build the invocation.
  zc::Maybe<zc::String> entryArgument = bytesToArgument(plan.entrySymbol());
  if (entryArgument == zc::none) {
    return rejectLinkerInvocation<PreparedLinkInputs>(IrFailureKind::InvalidFact, 0);
  }
  zc::StringPtr outputPath = plan.outputPath();
  zc::Maybe<zc::String> outputParent = parentDirectory(outputPath);
  if (outputParent == zc::none) {
    return rejectLinkerInvocation<PreparedLinkInputs>(IrFailureKind::InvalidFact, 1);
  }
  zc::String parent = ZC_REQUIRE_NONNULL(zc::mv(outputParent));

  // Enumerate every input in RFC 0043 canonical order with a snapshot name
  // derived from its role and canonical index (never a source basename, which
  // could collide across distinct source paths). The driver is snapshotted too;
  // it alone is executable and is the exec target.
  zc::String snapshotDirName = zc::str(".zomlink-", static_cast<uint64_t>(::getpid()), "-",
                                       hexPrefix(plan.id().digest().bytes(), 8));
  zc::String snapshotDirAbs = joinPath(parent, snapshotDirName);

  auto entry = [&](zc::StringPtr sourcePath, const identity::Sha256Digest& digest,
                   uint64_t byteCount, zc::String&& name, bool executable) {
    zc::String snapshotPath = joinPath(snapshotDirAbs, name);
    return SnapshotPlan{zc::str(sourcePath),  digest,    byteCount, zc::mv(name),
                        zc::mv(snapshotPath), executable};
  };

  zc::Vector<SnapshotPlan> driverPlan;
  driverPlan.add(entry(closure.linkerPath(), closure.linkerDigest(), closure.linkerByteCount(),
                       zc::str("driver"), true));

  // Ordered non-driver inputs: closure CRT objects, user objects, runtime
  // objects, then closure default libraries.
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

  // Create a fresh private snapshot directory. Remove any crash leftover of the
  // exact same name first so the directory is guaranteed to start empty.
  zc::String snapshotRelPath = zc::heapString(snapshotDirAbs.slice(1));
  filesystemRoot.tryRemove(zc::Path::parse(snapshotRelPath));
  zc::Own<const zc::Directory> snapshotDir = filesystemRoot.openSubdir(
      zc::Path::parse(snapshotRelPath),
      zc::WriteMode::CREATE | zc::WriteMode::MODIFY | zc::WriteMode::CREATE_PARENT);

  // A cleanup helper that removes the private tree before returning a rejection.
  auto fail = [&](IrFailureKind kind, uint32_t ordinal) -> IrOperationResult<PreparedLinkInputs> {
    filesystemRoot.tryRemove(zc::Path::parse(snapshotRelPath));
    return rejectLinkerInvocation<PreparedLinkInputs>(kind, ordinal);
  };

  // Pass 1: read each source, verify it against the plan, and write the snapshot.
  // A missing, resized, or re-digested source is an input-revision mismatch.
  auto snapshotOne = [&](const SnapshotPlan& item) -> bool {
    zc::Maybe<zc::Array<zc::byte>> sourceBytes = tryReadAbsolute(filesystemRoot, item.sourcePath);
    if (sourceBytes == zc::none) { return false; }
    zc::Array<zc::byte> bytes = ZC_REQUIRE_NONNULL(zc::mv(sourceBytes));
    if (!bytesMatch(bytes.asPtr(), item.digest, item.byteCount)) { return false; }
    zc::WriteMode mode = zc::WriteMode::CREATE | zc::WriteMode::MODIFY;
    if (item.executable) { mode = mode | zc::WriteMode::EXECUTABLE; }
    snapshotDir->openFile(zc::Path::parse(item.snapshotName), mode)->writeAll(bytes.asPtr());
    return true;
  };
  for (const SnapshotPlan& item : driverPlan) {
    if (!snapshotOne(item)) { return fail(IrFailureKind::InputRevisionMismatch, 2); }
  }
  for (const SnapshotPlan& item : orderedInputs) {
    if (!snapshotOne(item)) { return fail(IrFailureKind::InputRevisionMismatch, 3); }
  }

  // Pass 2: re-verify every snapshot copy from its own handle, proving the bytes
  // written are the bytes the plan proved. The write handles from pass 1 have
  // already been dropped, so these read-only opens hold no writable alias.
  auto reverifyOne = [&](const SnapshotPlan& item) -> bool {
    zc::Maybe<zc::Own<const zc::ReadableFile>> file =
        snapshotDir->tryOpenFile(zc::Path::parse(item.snapshotName));
    if (file == zc::none) { return false; }
    zc::Array<zc::byte> bytes = ZC_REQUIRE_NONNULL(zc::mv(file))->readAllBytes();
    return bytesMatch(bytes.asPtr(), item.digest, item.byteCount);
  };
  for (const SnapshotPlan& item : driverPlan) {
    if (!reverifyOne(item)) { return fail(IrFailureKind::InputRevisionMismatch, 4); }
  }
  for (const SnapshotPlan& item : orderedInputs) {
    if (!reverifyOne(item)) { return fail(IrFailureKind::InputRevisionMismatch, 5); }
  }

  // Open the driver snapshot read-only for execution by descriptor. A root that
  // exposes no real descriptor (an in-memory filesystem) fails closed here; the
  // path is never re-resolved for exec.
  zc::Own<const zc::ReadableFile> driverSnapshot =
      snapshotDir->openFile(zc::Path::parse(driverPlan[0].snapshotName));
  if (driverSnapshot->getFd() == zc::none) { return fail(IrFailureKind::OutputCreationFailed, 6); }

  // Build the rewritten argument vector once, now that every input verified.
  // Every input token names its snapshot path; the output is the real output
  // path the driver produces (it is not an input and is not snapshotted). The
  // plan carries no generic argument surface, so the vector is derived entirely
  // from the closed structural fields.
  zc::Vector<zc::String> argv;
  argv.add(zc::str(driverPlan[0].snapshotPath));
  argv.add(zc::str("-o"));
  argv.add(zc::str(outputPath));
  argv.add(zc::str("-e"));
  argv.add(ZC_REQUIRE_NONNULL(zc::mv(entryArgument)));
  for (const SnapshotPlan& item : orderedInputs) { argv.add(zc::str(item.snapshotPath)); }

  auto impl = zc::heap<Impl>();
  impl->rootHandle = filesystemRoot.clone();
  impl->snapshotRelPath = zc::mv(snapshotRelPath);
  impl->driverSnapshot = zc::mv(driverSnapshot);
  impl->program = zc::str(driverPlan[0].snapshotPath);
  impl->argvValues = argv.releaseAsArray();
  impl->workingDirectory = zc::mv(parent);
  impl->environmentValues = zc::Array<zc::String>();
  return IrOperationResult<PreparedLinkInputs>::verified(PreparedLinkInputs(zc::mv(impl)));
}

// =======================================================================================
// linkExecutable

IrOperationResult<VerifiedLinkedExecutable> linkExecutable(const VerifiedLinkPlan& plan,
                                                           const zc::Directory& filesystemRoot) {
  // Reject a pre-existing file at the plan's output path so a stale artifact can
  // never be mistaken for this link's result, before any snapshot work.
  zc::StringPtr outputPath = plan.outputPath();
  if (outputPath.size() < 2 || outputPath[0] != '/') {
    return rejectLinkerInvocation<VerifiedLinkedExecutable>(IrFailureKind::InvalidFact, 10);
  }
  zc::Path outputRelative = zc::Path::parse(outputPath.slice(1));
  if (filesystemRoot.exists(outputRelative)) {
    return rejectLinkerInvocation<VerifiedLinkedExecutable>(IrFailureKind::InvalidFact, 11);
  }

  // Snapshot and re-verify every input into a transaction-private tree, then hold
  // the prepared capability alive across the spawn so the driver descriptor stays
  // valid; its destructor removes the tree after the linker process is awaited.
  IrOperationResult<PreparedLinkInputs> preparedResult =
      PreparedLinkInputs::prepare(plan, filesystemRoot);
  if (preparedResult.isCapabilityRejected()) {
    return IrOperationResult<VerifiedLinkedExecutable>::capabilityRejected(
        zc::mv(preparedResult).takeCapabilityFailures());
  }
  if (preparedResult.isIrInvariantRejected()) {
    return IrOperationResult<VerifiedLinkedExecutable>::irInvariantRejected(
        zc::mv(preparedResult).takeInvariantFailures());
  }
  ZC_IREQUIRE(preparedResult.isVerified(), "prepared link inputs must be verified or rejected");
  PreparedLinkInputs prepared = zc::mv(preparedResult).takeVerified();

  // Spawn the driver by its snapshot descriptor with an explicit empty
  // environment. The rewritten argv points every input token at a snapshot path.
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

  // Classify the outcome; on any failure, remove partial output.
  auto rejectWithCleanup = [&](IrFailureKind kind,
                               uint32_t ordinal) -> IrOperationResult<VerifiedLinkedExecutable> {
    filesystemRoot.tryRemove(outputRelative);
    return rejectLinkerInvocation<VerifiedLinkedExecutable>(kind, ordinal);
  };

  if (!spawnResult.spawned()) { return rejectWithCleanup(IrFailureKind::OutputCreationFailed, 12); }
  const zc::SubprocessOutput& output = spawnResult.output();
  if (output.terminationKind == zc::SubprocessTerminationKind::Signaled) {
    return rejectWithCleanup(IrFailureKind::OutputCreationFailed, 13);
  }
  if (output.code != 0) { return rejectWithCleanup(IrFailureKind::OutputCreationFailed, 14); }

  // The driver exited cleanly; the planned output must now exist. Read it back as
  // the verified linked executable.
  zc::Maybe<zc::Array<zc::byte>> producedBytes = tryReadAbsolute(filesystemRoot, outputPath);
  if (producedBytes == zc::none) {
    return rejectWithCleanup(IrFailureKind::OutputCreationFailed, 15);
  }
  zc::Array<zc::byte> produced = ZC_REQUIRE_NONNULL(zc::mv(producedBytes));
  auto owned = zc::heapArray<uint8_t>(produced.size());
  for (size_t index = 0; index < produced.size(); ++index) { owned[index] = produced[index]; }
  return IrOperationResult<VerifiedLinkedExecutable>::verified(
      VerifiedLinkedExecutable(zc::mv(owned)));
  // `prepared` is dropped here, removing the private snapshot tree after the
  // linker process has been fully awaited.
}

}  // namespace zomlang::compiler::ir
