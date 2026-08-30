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

#include "compiler/ir/executable-publication.h"

#include "compiler/identity/identity-invariant.h"
#include "compiler/identity/semantic/context-fingerprint.h"
#include "compiler/ir/executable-inspector.h"
#include "compiler/ir/executable-publication-internal.h"
#include "compiler/ir/link-publication-internal.h"
#include "zc/core/debug.h"
#include "zc/core/io.h"
#include "zc/core/vector.h"

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#define ZOM_LINK_PUBLICATION_SUPPORTED 1
#else
#define ZOM_LINK_PUBLICATION_SUPPORTED 0
#endif

namespace zomlang::compiler::ir {
namespace {

constexpr zc::StringPtr kManifestSuffix = ".zom-artifact"_zc;
constexpr zc::StringPtr kJournalDomain = "zom.link-publication-journal"_zc;
constexpr uint8_t kJournalSchema = 0x01;
// The two fixed recovery claim slot names live in the shared D1 internal header
// so both this transaction and the lower-layer snapshot cleanup in
// invoke-linker.cc agree on the exact names that a generic content sweep must
// never remove.
constexpr zc::StringPtr kExecutableQuarantine = detail::kExecutableQuarantineSlot;
constexpr zc::StringPtr kManifestQuarantine = detail::kManifestQuarantineSlot;

bool startsWithMagic(zc::ArrayPtr<const uint8_t> bytes, zc::ArrayPtr<const uint8_t> magic) {
  if (bytes.size() < magic.size()) { return false; }
  for (size_t i = 0; i < magic.size(); ++i) {
    if (bytes[i] != magic[i]) { return false; }
  }
  return true;
}

identity::Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  ZC_IF_SOME(value, identity::sha256(bytes)) { return value; }
  ZC_UNREACHABLE
}

identity::ContextFingerprint publicationSessionContext() {
  return identity::ContextFingerprint::fromCanonicalDigest(
      requireDigest("zom.executable-publication"_zc.asBytes()));
}

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

PublicationRejection rejectPublication(IrFailureKind kind, uint32_t ordinal) {
  UnusedIdentityResolver resolver;
  auto fallback =
      IrFailureFallbackContext::from(IrFailurePhase::ExecutablePublication,
                                     IrFailureOwner::session(publicationSessionContext().clone()));
  ZC_IREQUIRE(fallback != zc::none, "Executable publication fallback must be legal");
  const bool capability = kind == IrFailureKind::OutputCreationFailed;
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = IrFailureDescriptor::decoded(
      capability ? IrRejectedBranch::CapabilityRejected : IrRejectedBranch::IrInvariantRejected,
      IrFailurePhase::ExecutablePublication, kind,
      IrFailureOwner::session(publicationSessionContext().clone()), zc::mv(noSite),
      IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath), ordinal);
  auto admitted =
      IrFailureFactory::admit(zc::mv(descriptor), ZC_REQUIRE_NONNULL(fallback), resolver);
  ZC_IREQUIRE(admitted.is<AcceptedIrFailureDescriptor>(),
              "Session-owned publication rejection must admit");
  IrFailureFact fact = zc::mv(admitted).get<AcceptedIrFailureDescriptor>().fact;
  if (capability) {
    zc::Vector<IrFailureFact> facts;
    facts.add(zc::mv(fact));
    return PublicationRejection::capabilityRejected(
        ZC_REQUIRE_NONNULL(SortedCapabilityFailureFacts::from(zc::mv(facts))));
  }
  zc::Vector<IrFailureFact> facts;
  facts.add(zc::mv(fact));
  return PublicationRejection::irInvariantRejected(
      ZC_REQUIRE_NONNULL(SortedIrInvariantFailureFacts::from(zc::mv(facts))));
}

template <typename Value>
PublicationRejection remapPublicationRejection(IrOperationResult<Value>&& result) {
  ZC_IREQUIRE(!result.isVerified(), "publication rejection remap requires a rejection");
  if (result.isCapabilityRejected()) {
    return PublicationRejection::capabilityRejected(zc::mv(result).takeCapabilityFailures());
  }
  if (result.isIdentityInvariantRejected()) {
    return PublicationRejection::identityInvariantRejected(zc::mv(result).takeIdentityFailures());
  }
  return PublicationRejection::irInvariantRejected(zc::mv(result).takeInvariantFailures());
}

zc::Array<uint8_t> copyBytes(zc::ArrayPtr<const uint8_t> source) {
  auto result = zc::heapArray<uint8_t>(source.size());
  for (size_t index = 0; index < source.size(); ++index) result[index] = source[index];
  return result;
}

zc::Maybe<zc::String> pathParent(zc::StringPtr path) {
  if (path.size() < 2 || path[0] != '/') { return zc::none; }
  size_t slash = path.size();
  while (slash > 0 && path[slash - 1] != '/') { --slash; }
  if (slash == 0 || slash == path.size()) { return zc::none; }
  if (slash == 1) { return zc::str("/"); }
  return zc::heapString(path.slice(0, slash - 1));
}

zc::Maybe<zc::String> pathLeaf(zc::StringPtr path) {
  if (path.size() < 2 || path[0] != '/') { return zc::none; }
  size_t slash = path.size();
  while (slash > 0 && path[slash - 1] != '/') { --slash; }
  if (slash == 0 || slash == path.size()) { return zc::none; }
  return zc::heapString(path.slice(slash));
}

void appendUint64(zc::Vector<uint8_t>& out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendFrame(zc::Vector<uint8_t>& out, zc::ArrayPtr<const uint8_t> value) {
  appendUint64(out, value.size());
  out.addAll(value);
}

void appendFrame(zc::Vector<uint8_t>& out, zc::StringPtr value) {
  appendUint64(out, value.size());
  for (char byte : value) { out.add(static_cast<uint8_t>(byte)); }
}

void appendInputRecord(zc::Vector<uint8_t>& out, const LinkInputRecord& record) {
  appendFrame(out, record.path());
  out.add(static_cast<uint8_t>(record.role()));
  appendFrame(out, record.digest().bytes());
  appendUint64(out, record.byteCount());
}

identity::Sha256Digest buildToolchainIdentity(const VerifiedLinkPlan& plan) {
  const ToolchainClosureRecord& closure = plan.toolchainClosure();
  zc::Vector<uint8_t> bytes;
  for (char byte : "zom.toolchain-closure"_zc) { bytes.add(static_cast<uint8_t>(byte)); }
  bytes.add(0);
  appendFrame(bytes, closure.targetSpecificationIdentity());
  appendFrame(bytes, closure.sysroot());
  bytes.add(static_cast<uint8_t>(closure.linkerKind()));
  appendFrame(bytes, closure.linkerPath());
  appendFrame(bytes, closure.linkerDigest().bytes());
  appendUint64(bytes, closure.linkerByteCount());
  appendUint64(bytes, closure.crtObjects().size());
  for (const LinkInputRecord& record : closure.crtObjects()) { appendInputRecord(bytes, record); }
  appendUint64(bytes, closure.defaultLibraries().size());
  for (const LinkInputRecord& record : closure.defaultLibraries()) {
    appendInputRecord(bytes, record);
  }
  return requireDigest(bytes.asPtr());
}

zc::Array<identity::Sha256Digest> buildInputDigests(const VerifiedLinkPlan& plan) {
  zc::Vector<identity::Sha256Digest> values;
  auto add = [&](zc::ArrayPtr<const LinkInputRecord> records) {
    for (const LinkInputRecord& record : records) { values.add(record.digest()); }
  };
  add(plan.toolchainClosure().crtObjects());
  add(plan.objectRecords());
  add(plan.runtimeRecords());
  add(plan.toolchainClosure().defaultLibraries());
  return values.releaseAsArray();
}

bool manifestMatches(const VerifiedExecutableManifest& manifest,
                     const LinkedOutputCandidate& candidate,
                     const detail::PublicationFileSnapshot& snapshot) {
  const VerifiedLinkPlan& plan = candidate.plan();
  if (manifest.finalDestination() != plan.outputPath() ||
      manifest.executableDigest() != snapshot.digest ||
      manifest.executableByteCount() != snapshot.byteCount || manifest.linkPlanId() != plan.id() ||
      manifest.targetSpecificationIdentity() != plan.targetSpecificationIdentity()) {
    return false;
  }
  const auto expectedInputs = ExecutablePublicationManifestBinding::inputArtifactDigests(plan);
  if (manifest.inputArtifactDigests() != expectedInputs.asPtr()) { return false; }
  return manifest.toolchainIdentity() ==
         ExecutablePublicationManifestBinding::toolchainIdentity(plan).bytes();
}

zc::String stageName(JournalStage stage) {
  switch (stage) {
    case JournalStage::None:
      // None names no durable journal record, so it never keys a journal file.
      break;
    case JournalStage::Started:
      return zc::str("started");
    case JournalStage::ManifestStaged:
      return zc::str("manifest-staged");
    case JournalStage::ExecCommitted:
      return zc::str("exec-committed");
    case JournalStage::ManifestCommitted:
      return zc::str("manifest-committed");
  }
  ZC_UNREACHABLE
}

zc::String journalLeaf(const SnapshotTransactionId& token, JournalStage stage) {
  return zc::str("journal.", token.toHex(), ".", stageName(stage));
}

zc::String journalTempLeaf(const SnapshotTransactionId& token, JournalStage stage) {
  return zc::str(".journal.", token.toHex(), ".", stageName(stage), ".tmp");
}

zc::String manifestTempLeaf(const SnapshotTransactionId& token) {
  return zc::str(".zom-manifest.", token.toHex());
}

zc::String absoluteChild(zc::StringPtr parent, zc::StringPtr leaf) {
  return parent == "/"_zc ? zc::str("/", leaf) : zc::str(parent, "/", leaf);
}

struct JournalRecord final {
  JournalStage stage;
  identity::Sha256Digest previousId;
  bool hasPrevious;
  SnapshotTransactionId token;
  zc::String executablePath;
  zc::String manifestPath;
  zc::String manifestTempPath;
  LinkPlanId planId;
  StableDirectoryIdentity rootIdentity;
  detail::PublicationFileSnapshot output;
  identity::Sha256Digest expectedManifestDigest;
  zc::Maybe<StableFileIdentity> manifestTempIdentity;
  identity::Sha256Digest manifestTempDigest;
};

zc::Array<uint8_t> encodeJournalPayload(const JournalRecord& record) {
  zc::Vector<uint8_t> out;
  for (char byte : kJournalDomain) { out.add(static_cast<uint8_t>(byte)); }
  out.add(0);
  out.add(kJournalSchema);
  out.add(static_cast<uint8_t>(record.stage));
  appendFrame(out, record.token.bytes());
  out.add(record.hasPrevious ? 1 : 0);
  if (record.hasPrevious) { appendFrame(out, record.previousId.bytes()); }
  appendFrame(out, record.executablePath);
  appendFrame(out, record.manifestPath);
  appendFrame(out, record.manifestTempPath);
  appendFrame(out, record.planId.digest().bytes());
  appendUint64(out, record.rootIdentity.device());
  appendUint64(out, record.rootIdentity.inode());
  appendUint64(out, record.output.identity.device());
  appendUint64(out, record.output.identity.inode());
  appendUint64(out, record.output.identity.linkCount());
  appendUint64(out, record.output.byteCount);
  appendFrame(out, record.output.digest.bytes());
  appendFrame(out, record.expectedManifestDigest.bytes());
  out.add(record.manifestTempIdentity != zc::none ? 1 : 0);
  ZC_IF_SOME(identity, record.manifestTempIdentity) {
    appendUint64(out, identity.device());
    appendUint64(out, identity.inode());
    appendUint64(out, identity.linkCount());
    appendFrame(out, record.manifestTempDigest.bytes());
  }
  return out.releaseAsArray();
}

struct JournalCommit final {
  identity::Sha256Digest id;
  zc::String leaf;
};

#if ZOM_LINK_PUBLICATION_SUPPORTED

bool writeAllFd(int fd, zc::ArrayPtr<const uint8_t> bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t written = ::write(fd, bytes.begin() + offset, bytes.size() - offset);
    if (written < 0) {
      if (errno == EINTR) { continue; }
      return false;
    }
    if (written == 0) { return false; }
    offset += static_cast<size_t>(written);
  }
  return true;
}

bool syncFd(int fd) {
  for (;;) {
    if (::fsync(fd) == 0) { return true; }
    if (errno != EINTR) { return false; }
  }
}

bool shouldFail(PublicationCheckpointObserver* observer, PublicationFaultPoint point) {
  return observer != nullptr && observer->fail(point);
}

bool trustedOutputDirectory(int fd) {
  struct stat st;
  if (::fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode)) { return false; }
  return st.st_uid == ::geteuid() && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

detail::PublicationRenameResult renameNoReplace(int fromDirFd, zc::StringPtr fromLeaf, int toDirFd,
                                                zc::StringPtr toLeaf) {
#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
  zc::String from = zc::heapString(fromLeaf);
  zc::String to = zc::heapString(toLeaf);
  for (;;) {
    if (::syscall(SYS_renameat2, fromDirFd, from.cStr(), toDirFd, to.cStr(), RENAME_NOREPLACE) ==
        0) {
      return detail::PublicationRenameResult::Renamed;
    }
    if (errno == EINTR) { continue; }
    if (errno == EEXIST) { return detail::PublicationRenameResult::DestinationExists; }
    if (errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP) {
      return detail::PublicationRenameResult::Unsupported;
    }
    return detail::PublicationRenameResult::Failed;
  }
#else
  (void)fromDirFd;
  (void)fromLeaf;
  (void)toDirFd;
  (void)toLeaf;
  return detail::PublicationRenameResult::Unsupported;
#endif
}

zc::Maybe<detail::PublicationFileSnapshot> snapshotFd(int fd) {
  struct stat st;
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_nlink != 1) {
    return zc::none;
  }
  auto bytes = zc::heapArray<uint8_t>(static_cast<size_t>(st.st_size));
  size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t count =
        ::pread(fd, bytes.begin() + offset, bytes.size() - offset, static_cast<off_t>(offset));
    if (count < 0) {
      if (errno == EINTR) { continue; }
      return zc::none;
    }
    if (count == 0) { return zc::none; }
    offset += static_cast<size_t>(count);
  }
  return detail::PublicationFileSnapshot{
      detail::LinkPublicationTransaction::fileIdentity(static_cast<uint64_t>(st.st_dev),
                                                       static_cast<uint64_t>(st.st_ino),
                                                       static_cast<uint64_t>(st.st_nlink)),
      static_cast<uint64_t>(st.st_size), requireDigest(bytes.asPtr())};
}

bool entryMatches(int dirFd, zc::StringPtr leaf, const StableFileIdentity& expected) {
  zc::String name = zc::heapString(leaf);
  struct stat st;
  if (::fstatat(dirFd, name.cStr(), &st, AT_SYMLINK_NOFOLLOW) != 0) { return false; }
  return S_ISREG(st.st_mode) &&
         expected.matches(static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino));
}

bool entryAbsent(int dirFd, zc::StringPtr leaf) {
  zc::String name = zc::heapString(leaf);
  struct stat st;
  if (::fstatat(dirFd, name.cStr(), &st, AT_SYMLINK_NOFOLLOW) == 0) { return false; }
  return errno == ENOENT;
}

zc::Maybe<JournalCommit> commitJournal(int directoryFd, const JournalRecord& record,
                                       PublicationCheckpointObserver* observer) {
  zc::Array<uint8_t> payload = encodeJournalPayload(record);
  identity::Sha256Digest checksum = requireDigest(payload.asPtr());
  zc::Vector<uint8_t> encoded;
  encoded.addAll(payload.asPtr());
  appendFrame(encoded, checksum.bytes());
  zc::Array<uint8_t> bytes = encoded.releaseAsArray();
  identity::Sha256Digest id = requireDigest(bytes.asPtr());
  zc::String temp = journalTempLeaf(record.token, record.stage);
  zc::String final = journalLeaf(record.token, record.stage);
  int raw = ::openat(directoryFd, temp.cStr(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                     0600);
  if (raw < 0) { return zc::none; }
  zc::OwnFd fd(raw);
  bool written = !shouldFail(observer, PublicationFaultPoint::JournalWrite) &&
                 writeAllFd(fd.get(), bytes.asPtr()) &&
                 !shouldFail(observer, PublicationFaultPoint::JournalFileSync) && syncFd(fd.get());
  if (!written) {
    if (!shouldFail(observer, PublicationFaultPoint::JournalTemporaryCleanup)) {
      (void)::unlinkat(directoryFd, temp.cStr(), 0);
    }
    return zc::none;
  }
  detail::PublicationRenameResult renamed =
      shouldFail(observer, PublicationFaultPoint::JournalInstall)
          ? detail::PublicationRenameResult::Failed
          : renameNoReplace(directoryFd, temp, directoryFd, final);
  if (renamed != detail::PublicationRenameResult::Renamed) {
    if (!shouldFail(observer, PublicationFaultPoint::JournalTemporaryCleanup)) {
      (void)::unlinkat(directoryFd, temp.cStr(), 0);
    }
    return zc::none;
  }
  if (shouldFail(observer, PublicationFaultPoint::JournalDirectorySync) || !syncFd(directoryFd)) {
    return zc::none;
  }
  return JournalCommit{id, zc::mv(final)};
}

bool removeJournalChain(int directoryFd, zc::ArrayPtr<const zc::String> leaves,
                        PublicationCheckpointObserver* observer = nullptr) {
  for (size_t index = leaves.size(); index > 0; --index) {
    zc::String leaf = zc::heapString(leaves[index - 1]);
    if (shouldFail(observer, PublicationFaultPoint::JournalChainRemove)) { return false; }
    if (::unlinkat(directoryFd, leaf.cStr(), 0) != 0 && errno != ENOENT) { return false; }
  }
  if (shouldFail(observer, PublicationFaultPoint::JournalChainSync)) { return false; }
  return syncFd(directoryFd);
}

bool journalResidueExists(int directoryFd, const SnapshotTransactionId& token, JournalStage stage) {
  return !entryAbsent(directoryFd, journalLeaf(token, stage)) ||
         !entryAbsent(directoryFd, journalTempLeaf(token, stage));
}

zc::Maybe<zc::Array<uint8_t>> readFileAt(int directoryFd, zc::StringPtr leaf) {
  zc::String name = zc::heapString(leaf);
  int raw = ::openat(directoryFd, name.cStr(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (raw < 0) { return zc::none; }
  zc::OwnFd fd(raw);
  struct stat st;
  if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
      st.st_size > 1024 * 1024) {
    return zc::none;
  }
  auto bytes = zc::heapArray<uint8_t>(static_cast<size_t>(st.st_size));
  size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t count = ::pread(fd.get(), bytes.begin() + offset, bytes.size() - offset,
                            static_cast<off_t>(offset));
    if (count < 0) {
      if (errno == EINTR) { continue; }
      return zc::none;
    }
    if (count == 0) { return zc::none; }
    offset += static_cast<size_t>(count);
  }
  return bytes;
}

class JournalCursor final {
public:
  explicit JournalCursor(zc::ArrayPtr<const uint8_t> bytes) : bytes(bytes) {}

  zc::Maybe<uint8_t> byte() {
    if (position >= bytes.size()) { return zc::none; }
    return bytes[position++];
  }

  zc::Maybe<uint64_t> uint64() {
    if (position + 8 > bytes.size()) { return zc::none; }
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) { value = (value << 8) | bytes[position++]; }
    return value;
  }

  zc::Maybe<zc::ArrayPtr<const uint8_t>> frame() {
    ZC_IF_SOME(length, uint64()) {
      if (length > bytes.size() - position) { return zc::none; }
      auto result = bytes.slice(position, position + static_cast<size_t>(length));
      position += static_cast<size_t>(length);
      return result;
    }
    return zc::none;
  }

  zc::Maybe<zc::String> stringFrame() {
    ZC_IF_SOME(value, frame()) {
      for (uint8_t byte : value) {
        if (byte == 0) { return zc::none; }
      }
      return zc::heapString(reinterpret_cast<const char*>(value.begin()), value.size());
    }
    return zc::none;
  }

  bool consume(zc::StringPtr expected) {
    if (position + expected.size() > bytes.size()) { return false; }
    for (char byte : expected) {
      if (bytes[position++] != static_cast<uint8_t>(byte)) { return false; }
    }
    return true;
  }

  bool atEnd() const noexcept { return position == bytes.size(); }

private:
  zc::ArrayPtr<const uint8_t> bytes;
  size_t position = 0;
};

zc::Maybe<JournalRecord> decodeJournal(zc::ArrayPtr<const uint8_t> encoded,
                                       JournalStage expectedStage, zc::StringPtr expectedTokenHex) {
  if (encoded.size() < 40) { return zc::none; }
  zc::ArrayPtr<const uint8_t> payload = encoded.slice(0, encoded.size() - 40);
  JournalCursor checksumCursor(encoded.slice(encoded.size() - 40));
  zc::Maybe<zc::ArrayPtr<const uint8_t>> checksumFrame = checksumCursor.frame();
  if (checksumFrame == zc::none || !checksumCursor.atEnd() ||
      ZC_REQUIRE_NONNULL(checksumFrame).size() != 32) {
    return zc::none;
  }
  identity::Sha256Digest checksum = requireDigest(payload);
  if (checksum.bytes() != ZC_REQUIRE_NONNULL(checksumFrame)) { return zc::none; }

  JournalCursor cursor(payload);
  if (!cursor.consume(kJournalDomain)) { return zc::none; }
  zc::Maybe<uint8_t> separator = cursor.byte();
  zc::Maybe<uint8_t> schema = cursor.byte();
  zc::Maybe<uint8_t> stageByte = cursor.byte();
  if (separator == zc::none || schema == zc::none || stageByte == zc::none ||
      ZC_REQUIRE_NONNULL(separator) != 0 || ZC_REQUIRE_NONNULL(schema) != kJournalSchema ||
      ZC_REQUIRE_NONNULL(stageByte) != static_cast<uint8_t>(expectedStage)) {
    return zc::none;
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> tokenBytes = cursor.frame();
  if (tokenBytes == zc::none) { return zc::none; }
  zc::Maybe<SnapshotTransactionId> token =
      detail::LinkPublicationTransaction::transactionIdFromBytes(ZC_REQUIRE_NONNULL(tokenBytes));
  if (token == zc::none || ZC_REQUIRE_NONNULL(token).toHex() != expectedTokenHex) {
    return zc::none;
  }
  zc::Maybe<uint8_t> previousFlag = cursor.byte();
  if (previousFlag == zc::none || ZC_REQUIRE_NONNULL(previousFlag) > 1) { return zc::none; }
  bool hasPrevious = ZC_REQUIRE_NONNULL(previousFlag) == 1;
  identity::Sha256Digest previous;
  if (hasPrevious) {
    ZC_IF_SOME(value, cursor.frame()) {
      ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(value)) {
        previous = digest;
      } else {
        return zc::none;
      }
    } else {
      return zc::none;
    }
  }
  zc::Maybe<zc::String> executablePath = cursor.stringFrame();
  zc::Maybe<zc::String> manifestPath = cursor.stringFrame();
  zc::Maybe<zc::String> manifestTempPath = cursor.stringFrame();
  if (executablePath == zc::none || manifestPath == zc::none || manifestTempPath == zc::none) {
    return zc::none;
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> planBytes = cursor.frame();
  if (planBytes == zc::none) { return zc::none; }
  zc::Maybe<identity::Sha256Digest> planDigest =
      identity::Sha256Digest::fromBytes(ZC_REQUIRE_NONNULL(planBytes));
  if (planDigest == zc::none) { return zc::none; }
  zc::Maybe<uint64_t> rootDev = cursor.uint64();
  zc::Maybe<uint64_t> rootIno = cursor.uint64();
  zc::Maybe<uint64_t> outputDev = cursor.uint64();
  zc::Maybe<uint64_t> outputIno = cursor.uint64();
  zc::Maybe<uint64_t> outputLinks = cursor.uint64();
  zc::Maybe<uint64_t> outputSize = cursor.uint64();
  if (rootDev == zc::none || rootIno == zc::none || outputDev == zc::none ||
      outputIno == zc::none || outputLinks == zc::none || outputSize == zc::none) {
    return zc::none;
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> outputDigestBytes = cursor.frame();
  zc::Maybe<zc::ArrayPtr<const uint8_t>> manifestDigestBytes = cursor.frame();
  if (outputDigestBytes == zc::none || manifestDigestBytes == zc::none) { return zc::none; }
  zc::Maybe<identity::Sha256Digest> outputDigest =
      identity::Sha256Digest::fromBytes(ZC_REQUIRE_NONNULL(outputDigestBytes));
  zc::Maybe<identity::Sha256Digest> manifestDigest =
      identity::Sha256Digest::fromBytes(ZC_REQUIRE_NONNULL(manifestDigestBytes));
  if (outputDigest == zc::none || manifestDigest == zc::none) { return zc::none; }
  zc::Maybe<uint8_t> manifestIdentityFlag = cursor.byte();
  if (manifestIdentityFlag == zc::none || ZC_REQUIRE_NONNULL(manifestIdentityFlag) > 1) {
    return zc::none;
  }
  zc::Maybe<StableFileIdentity> manifestIdentity;
  identity::Sha256Digest stagedManifestDigest;
  if (ZC_REQUIRE_NONNULL(manifestIdentityFlag) == 1) {
    zc::Maybe<uint64_t> dev = cursor.uint64();
    zc::Maybe<uint64_t> ino = cursor.uint64();
    zc::Maybe<uint64_t> links = cursor.uint64();
    zc::Maybe<zc::ArrayPtr<const uint8_t>> digestBytes = cursor.frame();
    if (dev == zc::none || ino == zc::none || links == zc::none || digestBytes == zc::none) {
      return zc::none;
    }
    zc::Maybe<identity::Sha256Digest> digest =
        identity::Sha256Digest::fromBytes(ZC_REQUIRE_NONNULL(digestBytes));
    if (digest == zc::none) { return zc::none; }
    manifestIdentity = detail::LinkPublicationTransaction::fileIdentity(
        ZC_REQUIRE_NONNULL(dev), ZC_REQUIRE_NONNULL(ino), ZC_REQUIRE_NONNULL(links));
    stagedManifestDigest = ZC_REQUIRE_NONNULL(digest);
  }
  if (!cursor.atEnd()) { return zc::none; }
  if (expectedStage == JournalStage::Started && (hasPrevious || manifestIdentity != zc::none)) {
    return zc::none;
  }
  if (expectedStage != JournalStage::Started && (!hasPrevious || manifestIdentity == zc::none)) {
    return zc::none;
  }
  return JournalRecord{expectedStage,
                       previous,
                       hasPrevious,
                       ZC_REQUIRE_NONNULL(zc::mv(token)),
                       ZC_REQUIRE_NONNULL(zc::mv(executablePath)),
                       ZC_REQUIRE_NONNULL(zc::mv(manifestPath)),
                       ZC_REQUIRE_NONNULL(zc::mv(manifestTempPath)),
                       LinkPlanId::fromDigest(ZC_REQUIRE_NONNULL(planDigest)),
                       detail::LinkPublicationTransaction::directoryIdentity(
                           ZC_REQUIRE_NONNULL(rootDev), ZC_REQUIRE_NONNULL(rootIno)),
                       detail::PublicationFileSnapshot{
                           detail::LinkPublicationTransaction::fileIdentity(
                               ZC_REQUIRE_NONNULL(outputDev), ZC_REQUIRE_NONNULL(outputIno),
                               ZC_REQUIRE_NONNULL(outputLinks)),
                           ZC_REQUIRE_NONNULL(outputSize), ZC_REQUIRE_NONNULL(outputDigest)},
                       ZC_REQUIRE_NONNULL(manifestDigest),
                       zc::mv(manifestIdentity),
                       stagedManifestDigest};
}

bool sameJournalTransaction(const JournalRecord& first, const JournalRecord& next) {
  return first.token == next.token && first.executablePath == next.executablePath &&
         first.manifestPath == next.manifestPath &&
         first.manifestTempPath == next.manifestTempPath && first.planId == next.planId &&
         first.rootIdentity == next.rootIdentity &&
         first.expectedManifestDigest == next.expectedManifestDigest;
}

struct RecoveryTreeCleanup final {
  bool clean;
  CleanupFailureKind kind;
};

// Defined below; forward-declared so the recovery cleanup path can notify the
// optional test observer at its claimed-before-verification seam.
void notify(PublicationCheckpointObserver* observer, PublicationCheckpoint checkpoint);

RecoveryTreeCleanup cleanupRecordedRoot(const zc::Directory& outputDir, int outputDirFd,
                                        const JournalRecord& record, bool claimExecutable,
                                        zc::StringPtr executableLeaf, bool claimManifestTemp,
                                        zc::StringPtr manifestTemp,
                                        const zc::Maybe<StableFileIdentity>& manifestTempIdentity,
                                        PublicationCheckpointObserver* observer) {
  zc::String rootLeaf = zc::str(".zomlink-", record.token.toHex());
  zc::String quarantine = zc::str(rootLeaf, ".cleanup");
  RecoveryTreeCleanup result{false, CleanupFailureKind::ContentRemovalFailed};
  bool topLevelRemovalAttempted = false;
  auto exception = zc::runCatchingExceptions([&]() {
    bool alreadyClaimed = false;
    zc::Maybe<zc::Own<const zc::Directory>> rootMaybe =
        outputDir.tryOpenSubdir(zc::Path(rootLeaf), zc::WriteMode::MODIFY);
    if (rootMaybe == zc::none) {
      rootMaybe = outputDir.tryOpenSubdir(zc::Path(quarantine), zc::WriteMode::MODIFY);
      alreadyClaimed = rootMaybe != zc::none;
    }
    if (rootMaybe == zc::none) {
      // The transaction root is the only owner proof that authorizes quarantining
      // and removing a claimed public entry. If it is gone while we still hold a
      // claimed executable or manifest temp, ownership can no longer be proven:
      // fail closed (retain the entry, report for explicit repair) rather than
      // reporting clean and letting the caller delete owner-proved evidence.
      if (claimExecutable || claimManifestTemp) {
        result.kind = CleanupFailureKind::IdentityMismatch;
        return;
      }
      result.clean = true;
      return;
    }
    zc::Own<const zc::Directory> root = ZC_REQUIRE_NONNULL(zc::mv(rootMaybe));
    zc::Maybe<int> rootFd = root->getFd();
    if (rootFd == zc::none) {
      result.kind = CleanupFailureKind::IdentityUnavailable;
      return;
    }
    struct stat rootStat;
    if (::fstat(ZC_REQUIRE_NONNULL(rootFd), &rootStat) != 0 || !S_ISDIR(rootStat.st_mode) ||
        !record.rootIdentity.matches(static_cast<uint64_t>(rootStat.st_dev),
                                     static_cast<uint64_t>(rootStat.st_ino))) {
      result.kind = CleanupFailureKind::IdentityMismatch;
      return;
    }

    if (!alreadyClaimed) {
      detail::PublicationRenameResult claim =
          renameNoReplace(outputDirFd, rootLeaf, outputDirFd, quarantine);
      if (claim != detail::PublicationRenameResult::Renamed) {
        result.kind = CleanupFailureKind::TopLevelRemovalFailed;
        return;
      }
      struct stat claimed;
      if (::fstatat(outputDirFd, quarantine.cStr(), &claimed, AT_SYMLINK_NOFOLLOW) != 0 ||
          !S_ISDIR(claimed.st_mode) ||
          !record.rootIdentity.matches(static_cast<uint64_t>(claimed.st_dev),
                                       static_cast<uint64_t>(claimed.st_ino))) {
        (void)renameNoReplace(outputDirFd, quarantine, outputDirFd, rootLeaf);
        result.kind = CleanupFailureKind::IdentityMismatch;
        return;
      }
    }

    auto claimFile = [&](zc::StringPtr sourceLeaf, zc::StringPtr quarantineLeaf,
                         const StableFileIdentity& expectedIdentity,
                         const identity::Sha256Digest& expectedDigest,
                         const zc::Maybe<uint64_t>& expectedSize) -> bool {
      // Verifies the quarantined slot's owner proof (exact identity, digest, and
      // optional size) through a no-follow read-only handle.
      auto verifyQuarantinedSlot = [&]() -> bool {
        zc::String quarantineName = zc::heapString(quarantineLeaf);
        int raw = ::openat(ZC_REQUIRE_NONNULL(rootFd), quarantineName.cStr(),
                           O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        zc::Maybe<detail::PublicationFileSnapshot> claimed;
        if (raw >= 0) {
          zc::OwnFd fd(raw);
          claimed = snapshotFd(fd.get());
        }
        return claimed != zc::none && ZC_REQUIRE_NONNULL(claimed).identity == expectedIdentity &&
               ZC_REQUIRE_NONNULL(claimed).digest == expectedDigest &&
               (expectedSize == zc::none ||
                ZC_REQUIRE_NONNULL(claimed).byteCount == ZC_REQUIRE_NONNULL(expectedSize));
      };

      // Seam fired before the rename so a test can pin this worker's claim
      // decision (the source is still present) before a competing worker moves
      // it. Production passes no observer, so it is a no-op there.
      notify(observer, PublicationCheckpoint::ExecutableClaimAboutToRename);
      detail::PublicationRenameResult claim =
          renameNoReplace(outputDirFd, sourceLeaf, ZC_REQUIRE_NONNULL(rootFd), quarantineLeaf);
      if (claim == detail::PublicationRenameResult::Failed &&
          entryAbsent(outputDirFd, sourceLeaf)) {
        // The public source is gone, but a concurrent recovery of the same token
        // may have already renamed it into this quarantine slot and not yet
        // proved ownership. Source-absent alone therefore does not prove the slot
        // is ours to remove: if the slot is empty the transaction is already
        // clean, but a present slot must verify against the expected owner proof;
        // a mismatch (a competitor's file) or an unreadable slot is retained for
        // explicit repair rather than swept.
        if (entryAbsent(ZC_REQUIRE_NONNULL(rootFd), quarantineLeaf)) { return true; }
        return verifyQuarantinedSlot();
      }
      if (claim != detail::PublicationRenameResult::Renamed) { return false; }

      // The public entry is now inside the transaction root's quarantine slot but
      // its owner proof has not been verified yet. This seam lets a test drive a
      // concurrent recovery into exactly this window; production passes no
      // observer, so it is a no-op there.
      notify(observer, PublicationCheckpoint::ExecutableClaimedBeforeVerification);

      if (verifyQuarantinedSlot()) { return true; }
      (void)renameNoReplace(ZC_REQUIRE_NONNULL(rootFd), quarantineLeaf, outputDirFd, sourceLeaf);
      return false;
    };

    if (claimExecutable) {
      zc::Maybe<uint64_t> expectedSize(record.output.byteCount);
      if (!claimFile(executableLeaf, kExecutableQuarantine, record.output.identity,
                     record.output.digest, expectedSize)) {
        result.kind = CleanupFailureKind::IdentityMismatch;
        return;
      }
    }
    if (claimManifestTemp) {
      zc::Maybe<uint64_t> noSize;
      if (manifestTempIdentity == zc::none ||
          !claimFile(manifestTemp, kManifestQuarantine, ZC_REQUIRE_NONNULL(manifestTempIdentity),
                     record.manifestTempDigest, noSize)) {
        result.kind = CleanupFailureKind::IdentityMismatch;
        return;
      }
    }
    if ((claimExecutable || claimManifestTemp) && !syncFd(outputDirFd)) {
      result.kind = CleanupFailureKind::TopLevelRemovalFailed;
      return;
    }

    // Before sweeping the transaction root, refuse to delete a claim slot this
    // call did not itself place. A concurrent recovery of the same token may have
    // renamed a public entry into the quarantine root and be mid-way through
    // verifying its owner proof (paused at ExecutableClaimedBeforeVerification).
    // Observing the public name absent leaves this worker with claimExecutable ==
    // false, so it must not infer the slot is removable: an unverified in-flight
    // claim is retained and reported for explicit repair rather than swept.
    if (!entryAbsent(ZC_REQUIRE_NONNULL(rootFd), kExecutableQuarantine) && !claimExecutable) {
      result.kind = CleanupFailureKind::IdentityMismatch;
      return;
    }
    if (!entryAbsent(ZC_REQUIRE_NONNULL(rootFd), kManifestQuarantine) && !claimManifestTemp) {
      result.kind = CleanupFailureKind::IdentityMismatch;
      return;
    }

    // Explicitly remove only the fixed claim slots this call itself proved by
    // exact owner identity + digest (+ size) above, and confirm each is gone.
    // tryRemove returning false means the slot was already absent (the benign
    // claimFile "already clean" outcome); a real IO error throws and is caught
    // below as ContentRemovalFailed. The post-removal re-check reports a slot
    // that somehow survived rather than sweeping past it and mis-reporting clean.
    // A slot this call did NOT prove is never removed here.
    auto removeProvenSlot = [&](zc::StringPtr slot) -> bool {
      (void)root->tryRemove(zc::Path(slot));
      return entryAbsent(ZC_REQUIRE_NONNULL(rootFd), slot);
    };
    if (claimExecutable && !removeProvenSlot(kExecutableQuarantine)) {
      result.kind = CleanupFailureKind::ContentRemovalFailed;
      return;
    }
    if (claimManifestTemp && !removeProvenSlot(kManifestQuarantine)) {
      result.kind = CleanupFailureKind::ContentRemovalFailed;
      return;
    }

    // Cleanup-time seam: a test drives a concurrent worker holding a pre-existing
    // root descriptor into this exact window to late-claim a fixed slot; the
    // sweep below must never remove it. Production passes no observer, so this is
    // a no-op there.
    notify(observer, PublicationCheckpoint::TransactionRootContentsAboutToSweep);

    // Sweep the remaining transaction contents, but never a fixed claim slot: a
    // slot still present here is a concurrent recovery's in-flight or late claim
    // whose ownership this call has not proved. It is retained, and the final
    // non-recursive root removal below then fails on the non-empty directory, so
    // the transaction is reported as a retriable cleanup debt rather than
    // deleting a slot that recovery is mid-way through verifying.
    for (const zc::String& name : root->listNames()) {
      zc::StringPtr text(name);
      if (text == kExecutableQuarantine || text == kManifestQuarantine) { continue; }
      if (!root->tryRemove(zc::Path(name))) {
        result.kind = CleanupFailureKind::ContentRemovalFailed;
        return;
      }
    }
    topLevelRemovalAttempted = true;
    // Non-recursive removal: if a retained fixed claim slot (or any late entry)
    // remains, this fails on the non-empty directory instead of recursively
    // deleting the retained slot.
    if (::unlinkat(outputDirFd, quarantine.cStr(), AT_REMOVEDIR) != 0) {
      result.kind = CleanupFailureKind::TopLevelRemovalFailed;
      return;
    }
    result.clean = true;
  });
  if (exception != zc::none) {
    result.clean = false;
    result.kind = topLevelRemovalAttempted ? CleanupFailureKind::TopLevelRemovalFailed
                                           : CleanupFailureKind::ContentRemovalFailed;
  }
  return result;
}

bool verifyingFinalPair(int outputDirFd, zc::StringPtr executableLeaf, zc::StringPtr manifestLeaf,
                        const JournalRecord& record) {
  zc::String executable = zc::heapString(executableLeaf);
  zc::String manifest = zc::heapString(manifestLeaf);
  int executableRaw = ::openat(outputDirFd, executable.cStr(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (executableRaw < 0) { return false; }
  zc::OwnFd executableFd(executableRaw);
  zc::Maybe<detail::PublicationFileSnapshot> output = snapshotFd(executableFd.get());
  if (output == zc::none || ZC_REQUIRE_NONNULL(output).identity != record.output.identity ||
      ZC_REQUIRE_NONNULL(output).byteCount != record.output.byteCount ||
      ZC_REQUIRE_NONNULL(output).digest != record.output.digest) {
    return false;
  }
  zc::Maybe<zc::Array<uint8_t>> manifestBytes = readFileAt(outputDirFd, manifest);
  return manifestBytes != zc::none &&
         requireDigest(ZC_REQUIRE_NONNULL(manifestBytes).asPtr()) == record.expectedManifestDigest;
}

#endif  // ZOM_LINK_PUBLICATION_SUPPORTED

void notify(PublicationCheckpointObserver* observer, PublicationCheckpoint checkpoint) {
  if (observer != nullptr) { observer->reached(checkpoint); }
}

}  // namespace

namespace detail {

class PublicationTransactionMinter final {
public:
  static PublishedExecutableArtifact artifact(VerifiedExecutableManifest&& manifest) noexcept {
    return PublishedExecutableArtifact(zc::mv(manifest));
  }

  static PublicationRecoveryObligation obligation(
      const SnapshotTransactionId& token, zc::String&& executablePath, zc::String&& manifestPath,
      zc::String&& journalDirectory, const StableFileIdentity& identity, JournalStage stage,
      zc::Maybe<SnapshotCleanupObligation>&& snapshot) noexcept {
    return PublicationRecoveryObligation(token, zc::mv(executablePath), zc::mv(manifestPath),
                                         zc::mv(journalDirectory), identity, stage,
                                         zc::mv(snapshot));
  }
};

}  // namespace detail

bool inspectExecutableFormat(zc::ArrayPtr<const uint8_t> executableBytes,
                             ObjectFormat expectedFormat) {
  switch (expectedFormat) {
    case ObjectFormat::Elf: {
      const uint8_t elf[] = {0x7f, 0x45, 0x4c, 0x46};
      return startsWithMagic(executableBytes, zc::arrayPtr(elf, 4));
    }
    case ObjectFormat::MachO: {
      const uint8_t machO32Le[] = {0xce, 0xfa, 0xed, 0xfe};
      const uint8_t machO64Le[] = {0xcf, 0xfa, 0xed, 0xfe};
      const uint8_t machO32Be[] = {0xfe, 0xed, 0xfa, 0xce};
      const uint8_t machO64Be[] = {0xfe, 0xed, 0xfa, 0xcf};
      return startsWithMagic(executableBytes, zc::arrayPtr(machO32Le, 4)) ||
             startsWithMagic(executableBytes, zc::arrayPtr(machO64Le, 4)) ||
             startsWithMagic(executableBytes, zc::arrayPtr(machO32Be, 4)) ||
             startsWithMagic(executableBytes, zc::arrayPtr(machO64Be, 4));
    }
    case ObjectFormat::Coff:
    case ObjectFormat::Wasm:
      return false;
  }
  return false;
}

zc::Array<identity::Sha256Digest> ExecutablePublicationManifestBinding::inputArtifactDigests(
    const VerifiedLinkPlan& plan) {
  return buildInputDigests(plan);
}

identity::Sha256Digest ExecutablePublicationManifestBinding::toolchainIdentity(
    const VerifiedLinkPlan& plan) {
  return buildToolchainIdentity(plan);
}

LinkRecoveryRequired LinkRecoveryRequired::snapshot(PublicationRejection&& primary,
                                                    SnapshotCleanupObligation&& obligation) {
  ZC_IREQUIRE(!primary.isVerified(), "snapshot recovery primary must be a rejection");
  return LinkRecoveryRequired(zc::OneOf<SnapshotRecoveryRequired, PublicationRecoveryRequired>(
      SnapshotRecoveryRequired{zc::mv(primary), zc::mv(obligation)}));
}

LinkRecoveryRequired LinkRecoveryRequired::publication(zc::Maybe<PublicationRejection>&& primary,
                                                       PublicationRecoveryObligation&& obligation) {
  ZC_IF_SOME(value, primary) {
    ZC_IREQUIRE(!value.isVerified(), "publication recovery primary must be a rejection");
  }
  return LinkRecoveryRequired(zc::OneOf<SnapshotRecoveryRequired, PublicationRecoveryRequired>(
      PublicationRecoveryRequired{zc::mv(primary), zc::mv(obligation)}));
}

LinkRecoveryRequired::LinkRecoveryRequired(
    zc::OneOf<SnapshotRecoveryRequired, PublicationRecoveryRequired>&& valueParam) noexcept
    : value(zc::mv(valueParam)) {}

bool LinkRecoveryRequired::isSnapshotRecoveryRequired() const noexcept {
  return value.is<SnapshotRecoveryRequired>();
}
bool LinkRecoveryRequired::isPublicationRecoveryRequired() const noexcept {
  return value.is<PublicationRecoveryRequired>();
}
SnapshotRecoveryRequired LinkRecoveryRequired::takeSnapshot() && {
  ZC_IREQUIRE(isSnapshotRecoveryRequired(), "link recovery is not snapshot-only");
  return zc::mv(value).get<SnapshotRecoveryRequired>();
}
PublicationRecoveryRequired LinkRecoveryRequired::takePublication() && {
  ZC_IREQUIRE(isPublicationRecoveryRequired(), "link recovery is not publication recovery");
  return zc::mv(value).get<PublicationRecoveryRequired>();
}

PublicationOutcome PublicationOutcome::published(PublishedExecutableArtifact&& artifact) {
  return PublicationOutcome(
      zc::OneOf<PublishedExecutableArtifact, LinkRecoveryRequired, PublicationRejection>(
          zc::mv(artifact)));
}
PublicationOutcome PublicationOutcome::recoveryRequired(LinkRecoveryRequired&& recovery) {
  return PublicationOutcome(
      zc::OneOf<PublishedExecutableArtifact, LinkRecoveryRequired, PublicationRejection>(
          zc::mv(recovery)));
}
PublicationOutcome PublicationOutcome::rejected(PublicationRejection&& rejection) {
  ZC_IREQUIRE(!rejection.isVerified(), "a publication rejection cannot be verified");
  return PublicationOutcome(
      zc::OneOf<PublishedExecutableArtifact, LinkRecoveryRequired, PublicationRejection>(
          zc::mv(rejection)));
}
PublicationOutcome::PublicationOutcome(zc::OneOf<PublishedExecutableArtifact, LinkRecoveryRequired,
                                                 PublicationRejection>&& valueParam) noexcept
    : value(zc::mv(valueParam)) {}
bool PublicationOutcome::isPublished() const noexcept {
  return value.is<PublishedExecutableArtifact>();
}
bool PublicationOutcome::isRecoveryRequired() const noexcept {
  return value.is<LinkRecoveryRequired>();
}
bool PublicationOutcome::isRejected() const noexcept { return value.is<PublicationRejection>(); }
PublishedExecutableArtifact PublicationOutcome::takePublished() && {
  ZC_IREQUIRE(isPublished(), "publication outcome is not Published");
  return zc::mv(value).get<PublishedExecutableArtifact>();
}
LinkRecoveryRequired PublicationOutcome::takeRecoveryRequired() && {
  ZC_IREQUIRE(isRecoveryRequired(), "publication outcome is not RecoveryRequired");
  return zc::mv(value).get<LinkRecoveryRequired>();
}
PublicationRejection PublicationOutcome::takeRejected() && {
  ZC_IREQUIRE(isRejected(), "publication outcome is not Rejected");
  return zc::mv(value).get<PublicationRejection>();
}

PublicationRecoveryResult PublicationRecoveryResult::clean() {
  return PublicationRecoveryResult(PublicationRecoveryStatus::Clean, zc::none);
}
PublicationRecoveryResult PublicationRecoveryResult::published() {
  return PublicationRecoveryResult(PublicationRecoveryStatus::Published, zc::none);
}
PublicationRecoveryResult PublicationRecoveryResult::recoveryRequired(
    PublicationRecoveryObligation&& obligation) {
  return PublicationRecoveryResult(PublicationRecoveryStatus::RecoveryRequired, zc::mv(obligation));
}
PublicationRecoveryResult PublicationRecoveryResult::explicitRepairRequired() {
  return PublicationRecoveryResult(PublicationRecoveryStatus::ExplicitRepairRequired, zc::none);
}
PublicationRecoveryObligation PublicationRecoveryResult::takeObligation() && {
  ZC_IREQUIRE(obligationValue != zc::none, "publication recovery result has no obligation");
  return ZC_REQUIRE_NONNULL(zc::mv(obligationValue));
}

namespace {

PublicationOutcome publishLinkedOutputObserved(LinkedOutputCandidate candidate,
                                               VerifiedExecutableManifest manifest,
                                               PublicationCheckpointObserver* observer) {
#if !ZOM_LINK_PUBLICATION_SUPPORTED
  CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
  PublicationRejection primary = rejectPublication(IrFailureKind::OutputCreationFailed, 0);
  if (disposition.isClean()) { return PublicationOutcome::rejected(zc::mv(primary)); }
  return PublicationOutcome::recoveryRequired(
      LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(disposition).takeObligation()));
#else
  SnapshotTransactionId token = detail::LinkPublicationTransaction::transactionId(candidate);
  StableDirectoryIdentity rootIdentity =
      detail::LinkPublicationTransaction::rootIdentity(candidate);
  const zc::Directory& outputDir =
      detail::LinkPublicationTransaction::outputParentDirectory(candidate);
  zc::Maybe<int> outputDirFdMaybe = outputDir.getFd();
  if (outputDirFdMaybe == zc::none) {
    CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
    PublicationRejection primary = rejectPublication(IrFailureKind::OutputCreationFailed, 1);
    if (disposition.isClean()) { return PublicationOutcome::rejected(zc::mv(primary)); }
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(disposition).takeObligation()));
  }
  int duplicatedOutputDirFd = ::dup(ZC_REQUIRE_NONNULL(outputDirFdMaybe));
  if (duplicatedOutputDirFd < 0) {
    CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
    PublicationRejection primary = rejectPublication(IrFailureKind::OutputCreationFailed, 1);
    if (disposition.isClean()) { return PublicationOutcome::rejected(zc::mv(primary)); }
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(disposition).takeObligation()));
  }
  zc::OwnFd outputDirFdOwner(duplicatedOutputDirFd);
  int outputDirFd = outputDirFdOwner.get();
  if (!trustedOutputDirectory(outputDirFd)) {
    CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
    PublicationRejection primary = rejectPublication(IrFailureKind::OutputCreationFailed, 18);
    if (disposition.isClean()) { return PublicationOutcome::rejected(zc::mv(primary)); }
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(disposition).takeObligation()));
  }
  zc::String outputParent =
      zc::heapString(detail::LinkPublicationTransaction::outputParent(candidate));
  zc::Maybe<zc::String> executableLeafMaybe = pathLeaf(candidate.plan().outputPath());
  zc::Maybe<zc::String> recordedParent = pathParent(candidate.plan().outputPath());
  if (executableLeafMaybe == zc::none || recordedParent == zc::none ||
      ZC_REQUIRE_NONNULL(recordedParent) != outputParent) {
    CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
    PublicationRejection primary = rejectPublication(IrFailureKind::InvalidFact, 2);
    if (disposition.isClean()) { return PublicationOutcome::rejected(zc::mv(primary)); }
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(disposition).takeObligation()));
  }
  zc::String executableLeaf = ZC_REQUIRE_NONNULL(zc::mv(executableLeafMaybe));
  zc::String manifestLeaf = zc::str(executableLeaf, kManifestSuffix);
  zc::String executablePath = zc::heapString(candidate.plan().outputPath());
  zc::String manifestPath = zc::str(executablePath, kManifestSuffix);
  zc::String manifestTemp = manifestTempLeaf(token);
  zc::String manifestTempPath = absoluteChild(outputParent, manifestTemp);
  zc::Array<uint8_t> manifestBytes = ExecutableManifestCodec::encode(manifest);
  identity::Sha256Digest manifestDigest = requireDigest(manifestBytes.asPtr());

  auto rejectBeforeJournal = [&](IrFailureKind kind, uint32_t ordinal) -> PublicationOutcome {
    PublicationRejection primary = rejectPublication(kind, ordinal);
    CleanupDisposition disposition = zc::mv(candidate).discardAndCleanup();
    if (disposition.isClean()) { return PublicationOutcome::rejected(zc::mv(primary)); }
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(disposition).takeObligation()));
  };

  if (!entryAbsent(outputDirFd, executableLeaf) || !entryAbsent(outputDirFd, manifestLeaf)) {
    return rejectBeforeJournal(IrFailureKind::OutputCreationFailed, 3);
  }
  zc::Maybe<detail::PublicationFileSnapshot> initial =
      detail::LinkPublicationTransaction::recheckOutput(candidate);
  if (initial == zc::none || !manifestMatches(manifest, candidate, ZC_REQUIRE_NONNULL(initial))) {
    return rejectBeforeJournal(IrFailureKind::InvalidFact, 4);
  }

  auto recoveryAfterJournalInstallFailure =
      [&](PublicationRejection&& primary, JournalStage lastDurableStage,
          const StableFileIdentity& outputIdentity) -> PublicationOutcome {
    CleanupDisposition rootCleanup = zc::mv(candidate).discardAndCleanup();
    zc::Maybe<SnapshotCleanupObligation> snapshot;
    if (rootCleanup.isObligated()) { snapshot = zc::mv(rootCleanup).takeObligation(); }
    PublicationRecoveryObligation obligation = detail::PublicationTransactionMinter::obligation(
        token, zc::heapString(executablePath), zc::heapString(manifestPath),
        zc::heapString(outputParent), outputIdentity, lastDurableStage, zc::mv(snapshot));
    zc::Maybe<PublicationRejection> cause(zc::mv(primary));
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::publication(zc::mv(cause), zc::mv(obligation)));
  };

  zc::Vector<zc::String> journalLeaves;
  JournalRecord record{JournalStage::Started,
                       identity::Sha256Digest(),
                       false,
                       token,
                       zc::heapString(executablePath),
                       zc::heapString(manifestPath),
                       zc::heapString(manifestTempPath),
                       candidate.plan().id(),
                       rootIdentity,
                       ZC_REQUIRE_NONNULL(initial),
                       manifestDigest,
                       zc::none,
                       identity::Sha256Digest()};
  zc::Maybe<JournalCommit> started = commitJournal(outputDirFd, record, observer);
  if (started == zc::none) {
    if (journalResidueExists(outputDirFd, token, JournalStage::Started)) {
      // The initial Started record is durable only after its write, no-replace
      // install, and directory fsync all succeed. A commit fault that leaves
      // residue means the record's durability is unproven, so the obligation
      // reports no durable stage (None) rather than falsely asserting Started.
      return recoveryAfterJournalInstallFailure(
          rejectPublication(IrFailureKind::OutputCreationFailed, 5), JournalStage::None,
          ZC_REQUIRE_NONNULL(initial).identity);
    }
    return rejectBeforeJournal(IrFailureKind::OutputCreationFailed, 5);
  }
  journalLeaves.add(zc::heapString(ZC_REQUIRE_NONNULL(started).leaf));
  notify(observer, PublicationCheckpoint::StartedDurable);

  zc::OwnFd manifestFd;
  zc::Maybe<StableFileIdentity> manifestIdentity;
  bool manifestTempCreated = false;
  detail::PublicationFileSnapshot commitPoint = record.output;
  JournalStage durableStage = JournalStage::Started;
  bool manifestRenamed = false;

  auto makePublicationObligation = [&](zc::Maybe<SnapshotCleanupObligation>&& snapshot) {
    return detail::PublicationTransactionMinter::obligation(
        token, zc::heapString(executablePath), zc::heapString(manifestPath),
        zc::heapString(outputParent), commitPoint.identity, durableStage, zc::mv(snapshot));
  };

  auto claimParentEntry = [&](zc::StringPtr leaf, zc::StringPtr quarantine,
                              const detail::PublicationFileSnapshot& expected,
                              bool& rootContainsUnownedEntry) -> bool {
    detail::PublicationClaimResult result =
        detail::LinkPublicationTransaction::claimParentEntryNoReplace(candidate, leaf, quarantine,
                                                                      expected);
    switch (result) {
      case detail::PublicationClaimResult::ClaimedOwned:
      case detail::PublicationClaimResult::SourceAbsent:
        return true;
      case detail::PublicationClaimResult::CompetitorRetained:
      case detail::PublicationClaimResult::QuarantineExists:
        rootContainsUnownedEntry = true;
        return false;
      case detail::PublicationClaimResult::CompetitorRestored:
      case detail::PublicationClaimResult::Unsupported:
      case detail::PublicationClaimResult::Failed:
        return false;
    }
    ZC_UNREACHABLE
  };

  auto cleanupBeforeExecutable = [&](PublicationRejection&& primary) -> PublicationOutcome {
    bool manifestClean = !manifestTempCreated;
    bool rootContainsUnownedEntry = false;
    ZC_IF_SOME(identity, manifestIdentity) {
      detail::PublicationFileSnapshot expected{identity, manifestBytes.size(), manifestDigest};
      manifestClean =
          claimParentEntry(manifestTemp, kManifestQuarantine, expected, rootContainsUnownedEntry);
    }
    if (rootContainsUnownedEntry) {
      SnapshotCleanupObligation retained =
          detail::LinkPublicationTransaction::abandonRootForRecovery(
              candidate, CleanupFailureKind::IdentityMismatch);
      zc::Maybe<SnapshotCleanupObligation> snapshot(zc::mv(retained));
      zc::Maybe<PublicationRejection> cause(zc::mv(primary));
      return PublicationOutcome::recoveryRequired(LinkRecoveryRequired::publication(
          zc::mv(cause), makePublicationObligation(zc::mv(snapshot))));
    }
    CleanupDisposition rootCleanup = zc::mv(candidate).discardAndCleanup();
    bool journalClean = false;
    if (manifestClean && rootCleanup.isClean()) {
      journalClean = removeJournalChain(outputDirFd, journalLeaves.asPtr(), observer);
    }
    if (manifestClean && rootCleanup.isClean() && journalClean) {
      return PublicationOutcome::rejected(zc::mv(primary));
    }
    if (!manifestClean || !journalClean) {
      zc::Maybe<SnapshotCleanupObligation> snapshot;
      if (rootCleanup.isObligated()) { snapshot = zc::mv(rootCleanup).takeObligation(); }
      zc::Maybe<PublicationRejection> cause(zc::mv(primary));
      return PublicationOutcome::recoveryRequired(LinkRecoveryRequired::publication(
          zc::mv(cause), makePublicationObligation(zc::mv(snapshot))));
    }
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(rootCleanup).takeObligation()));
  };

  auto cleanupAfterExecutable = [&](PublicationRejection&& primary,
                                    bool allowRollback) -> PublicationOutcome {
    bool executableClean = false;
    bool rootContainsUnownedEntry = false;
    if (allowRollback) {
      executableClean = claimParentEntry(executableLeaf, kExecutableQuarantine, commitPoint,
                                         rootContainsUnownedEntry);
      if (executableClean) { (void)syncFd(outputDirFd); }
    }
    bool manifestClean = manifestRenamed || !manifestTempCreated;
    ZC_IF_SOME(identity, manifestIdentity) {
      if (!manifestRenamed) {
        detail::PublicationFileSnapshot expected{identity, manifestBytes.size(), manifestDigest};
        manifestClean =
            claimParentEntry(manifestTemp, kManifestQuarantine, expected, rootContainsUnownedEntry);
      }
    }
    if (rootContainsUnownedEntry) {
      SnapshotCleanupObligation retained =
          detail::LinkPublicationTransaction::abandonRootForRecovery(
              candidate, CleanupFailureKind::IdentityMismatch);
      zc::Maybe<SnapshotCleanupObligation> snapshot(zc::mv(retained));
      zc::Maybe<PublicationRejection> cause(zc::mv(primary));
      return PublicationOutcome::recoveryRequired(LinkRecoveryRequired::publication(
          zc::mv(cause), makePublicationObligation(zc::mv(snapshot))));
    }
    CleanupDisposition rootCleanup = zc::mv(candidate).discardAndCleanup();
    bool journalClean = false;
    if (executableClean && manifestClean && rootCleanup.isClean()) {
      journalClean = removeJournalChain(outputDirFd, journalLeaves.asPtr(), observer);
    }
    if (executableClean && manifestClean && rootCleanup.isClean() && journalClean) {
      return PublicationOutcome::rejected(zc::mv(primary));
    }
    zc::Maybe<SnapshotCleanupObligation> snapshot;
    if (rootCleanup.isObligated()) { snapshot = zc::mv(rootCleanup).takeObligation(); }
    zc::Maybe<PublicationRejection> cause(zc::mv(primary));
    return PublicationOutcome::recoveryRequired(LinkRecoveryRequired::publication(
        zc::mv(cause), makePublicationObligation(zc::mv(snapshot))));
  };

  auto recoveryAfterManifestRename =
      [&](zc::Maybe<PublicationRejection>&& primary) -> PublicationOutcome {
    CleanupDisposition rootCleanup = zc::mv(candidate).discardAndCleanup();
    zc::Maybe<SnapshotCleanupObligation> snapshot;
    if (rootCleanup.isObligated()) { snapshot = zc::mv(rootCleanup).takeObligation(); }
    return PublicationOutcome::recoveryRequired(LinkRecoveryRequired::publication(
        zc::mv(primary), makePublicationObligation(zc::mv(snapshot))));
  };

  int rawManifest = ::openat(outputDirFd, manifestTemp.cStr(),
                             O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (rawManifest < 0) {
    return cleanupBeforeExecutable(rejectPublication(IrFailureKind::OutputCreationFailed, 6));
  }
  manifestFd = zc::OwnFd(rawManifest);
  manifestTempCreated = true;
  if (shouldFail(observer, PublicationFaultPoint::ManifestWrite) ||
      !writeAllFd(manifestFd.get(), manifestBytes.asPtr()) ||
      shouldFail(observer, PublicationFaultPoint::ManifestFileSync) || !syncFd(manifestFd.get()) ||
      shouldFail(observer, PublicationFaultPoint::ManifestDirectorySync) || !syncFd(outputDirFd)) {
    ZC_IF_SOME(snapshot, snapshotFd(manifestFd.get())) { manifestIdentity = snapshot.identity; }
    return cleanupBeforeExecutable(rejectPublication(IrFailureKind::OutputCreationFailed, 7));
  }
  zc::Maybe<detail::PublicationFileSnapshot> stagedManifest = snapshotFd(manifestFd.get());
  if (stagedManifest == zc::none || ZC_REQUIRE_NONNULL(stagedManifest).digest != manifestDigest) {
    return cleanupBeforeExecutable(rejectPublication(IrFailureKind::CanonicalCodecMismatch, 8));
  }
  manifestIdentity = ZC_REQUIRE_NONNULL(stagedManifest).identity;
  notify(observer, PublicationCheckpoint::ManifestTemporaryDurable);

  zc::Maybe<detail::PublicationFileSnapshot> fresh =
      detail::LinkPublicationTransaction::recheckOutput(candidate);
  if (fresh == zc::none || !manifestMatches(manifest, candidate, ZC_REQUIRE_NONNULL(fresh))) {
    return cleanupBeforeExecutable(rejectPublication(IrFailureKind::InvalidFact, 9));
  }
  commitPoint = ZC_REQUIRE_NONNULL(fresh);
  record.stage = JournalStage::ManifestStaged;
  record.previousId = ZC_REQUIRE_NONNULL(started).id;
  record.hasPrevious = true;
  record.output = commitPoint;
  record.manifestTempIdentity = manifestIdentity;
  record.manifestTempDigest = manifestDigest;
  zc::Maybe<JournalCommit> manifestStaged = commitJournal(outputDirFd, record, observer);
  if (manifestStaged == zc::none) {
    if (journalResidueExists(outputDirFd, token, JournalStage::ManifestStaged)) {
      return recoveryAfterJournalInstallFailure(
          rejectPublication(IrFailureKind::OutputCreationFailed, 10), durableStage,
          commitPoint.identity);
    }
    return cleanupBeforeExecutable(rejectPublication(IrFailureKind::OutputCreationFailed, 10));
  }
  journalLeaves.add(zc::heapString(ZC_REQUIRE_NONNULL(manifestStaged).leaf));
  durableStage = JournalStage::ManifestStaged;
  notify(observer, PublicationCheckpoint::ManifestStagedDurable);

  fresh = detail::LinkPublicationTransaction::recheckOutput(candidate);
  stagedManifest = snapshotFd(manifestFd.get());
  if (fresh == zc::none || stagedManifest == zc::none ||
      ZC_REQUIRE_NONNULL(fresh).identity != commitPoint.identity ||
      ZC_REQUIRE_NONNULL(fresh).byteCount != commitPoint.byteCount ||
      ZC_REQUIRE_NONNULL(fresh).digest != commitPoint.digest ||
      ZC_REQUIRE_NONNULL(stagedManifest).identity != ZC_REQUIRE_NONNULL(manifestIdentity) ||
      ZC_REQUIRE_NONNULL(stagedManifest).digest != manifestDigest) {
    return cleanupBeforeExecutable(rejectPublication(IrFailureKind::InvalidFact, 11));
  }
  detail::PublicationRenameResult execRename =
      detail::LinkPublicationTransaction::renameOutputNoReplace(candidate, executableLeaf);
  if (execRename != detail::PublicationRenameResult::Renamed) {
    return cleanupBeforeExecutable(rejectPublication(IrFailureKind::OutputCreationFailed, 12));
  }
  notify(observer, PublicationCheckpoint::ExecutableRenamed);
  if (shouldFail(observer, PublicationFaultPoint::ExecutableDirectorySync) ||
      !syncFd(outputDirFd)) {
    return cleanupAfterExecutable(rejectPublication(IrFailureKind::OutputCreationFailed, 13), true);
  }
  notify(observer, PublicationCheckpoint::ExecutableDirectoryDurable);

  record.stage = JournalStage::ExecCommitted;
  record.previousId = ZC_REQUIRE_NONNULL(manifestStaged).id;
  zc::Maybe<JournalCommit> execCommitted = commitJournal(outputDirFd, record, observer);
  if (execCommitted == zc::none) {
    if (journalResidueExists(outputDirFd, token, JournalStage::ExecCommitted)) {
      return recoveryAfterJournalInstallFailure(
          rejectPublication(IrFailureKind::OutputCreationFailed, 14), durableStage,
          commitPoint.identity);
    }
    return cleanupAfterExecutable(rejectPublication(IrFailureKind::OutputCreationFailed, 14), true);
  }
  journalLeaves.add(zc::heapString(ZC_REQUIRE_NONNULL(execCommitted).leaf));
  durableStage = JournalStage::ExecCommitted;
  notify(observer, PublicationCheckpoint::ExecCommittedDurable);

  fresh = detail::LinkPublicationTransaction::recheckOutput(candidate);
  stagedManifest = snapshotFd(manifestFd.get());
  const bool finalEntryMatches = entryMatches(outputDirFd, executableLeaf, commitPoint.identity);
  if (fresh == zc::none || stagedManifest == zc::none || !finalEntryMatches ||
      ZC_REQUIRE_NONNULL(fresh).identity != commitPoint.identity ||
      ZC_REQUIRE_NONNULL(fresh).byteCount != commitPoint.byteCount ||
      ZC_REQUIRE_NONNULL(fresh).digest != commitPoint.digest ||
      ZC_REQUIRE_NONNULL(stagedManifest).identity != ZC_REQUIRE_NONNULL(manifestIdentity) ||
      ZC_REQUIRE_NONNULL(stagedManifest).digest != manifestDigest) {
    return cleanupAfterExecutable(rejectPublication(IrFailureKind::InvalidFact, 15), false);
  }
  detail::PublicationRenameResult manifestRename =
      renameNoReplace(outputDirFd, manifestTemp, outputDirFd, manifestLeaf);
  if (manifestRename != detail::PublicationRenameResult::Renamed) {
    notify(observer, PublicationCheckpoint::ManifestRenameRejected);
    return cleanupAfterExecutable(rejectPublication(IrFailureKind::OutputCreationFailed, 16), true);
  }
  manifestRenamed = true;
  notify(observer, PublicationCheckpoint::ManifestRenamed);
  if (shouldFail(observer, PublicationFaultPoint::FinalManifestDirectorySync) ||
      !syncFd(outputDirFd)) {
    zc::Maybe<PublicationRejection> noPrimary;
    return recoveryAfterManifestRename(zc::mv(noPrimary));
  }
  notify(observer, PublicationCheckpoint::ManifestDirectoryDurable);

  record.stage = JournalStage::ManifestCommitted;
  record.previousId = ZC_REQUIRE_NONNULL(execCommitted).id;
  zc::Maybe<JournalCommit> manifestCommitted = commitJournal(outputDirFd, record, observer);
  if (manifestCommitted == zc::none) {
    zc::Maybe<PublicationRejection> noPrimary;
    return recoveryAfterManifestRename(zc::mv(noPrimary));
  }
  journalLeaves.add(zc::heapString(ZC_REQUIRE_NONNULL(manifestCommitted).leaf));
  durableStage = JournalStage::ManifestCommitted;
  notify(observer, PublicationCheckpoint::ManifestCommittedDurable);

  if (!verifyingFinalPair(outputDirFd, executableLeaf, manifestLeaf, record)) {
    zc::Maybe<PublicationRejection> primary(rejectPublication(IrFailureKind::InvalidFact, 17));
    return recoveryAfterManifestRename(zc::mv(primary));
  }

  CleanupDisposition rootCleanup = zc::mv(candidate).discardAndCleanup();
  if (rootCleanup.isObligated()) {
    zc::Maybe<PublicationRejection> noPrimary;
    zc::Maybe<SnapshotCleanupObligation> snapshot(zc::mv(rootCleanup).takeObligation());
    return PublicationOutcome::recoveryRequired(LinkRecoveryRequired::publication(
        zc::mv(noPrimary), makePublicationObligation(zc::mv(snapshot))));
  }
  if (!removeJournalChain(outputDirFd, journalLeaves.asPtr(), observer)) {
    zc::Maybe<PublicationRejection> noPrimary;
    zc::Maybe<SnapshotCleanupObligation> noSnapshot;
    return PublicationOutcome::recoveryRequired(LinkRecoveryRequired::publication(
        zc::mv(noPrimary), makePublicationObligation(zc::mv(noSnapshot))));
  }
  return PublicationOutcome::published(
      detail::PublicationTransactionMinter::artifact(zc::mv(manifest)));
#endif
}

}  // namespace

PublicationOutcome publishLinkedOutput(LinkedOutputCandidate candidate,
                                       VerifiedExecutableManifest manifest) {
  return publishLinkedOutputObserved(zc::mv(candidate), zc::mv(manifest), nullptr);
}

PublicationOutcome PublicationTransactionTestAccess::publishObserved(
    LinkedOutputCandidate candidate, VerifiedExecutableManifest manifest,
    PublicationCheckpointObserver& observer) {
  return publishLinkedOutputObserved(zc::mv(candidate), zc::mv(manifest), &observer);
}

PublicationOutcome linkAndPublish(VerifiedLinkPlan plan, const zc::Filesystem& filesystem) {
  CleanupAwareOutcome<LinkedOutputCandidate> linked = linkExecutable(zc::mv(plan), filesystem);
  if (linked.isRecoveryRequired()) {
    RecoveryRequiredOutcome<LinkedOutputCandidate> recovery = zc::mv(linked).takeRecoveryRequired();
    PublicationRejection primary = remapPublicationRejection(zc::mv(recovery.primary));
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(recovery.obligation)));
  }

  IrOperationResult<LinkedOutputCandidate> linkedResult = zc::mv(linked).takeComplete();
  if (!linkedResult.isVerified()) {
    return PublicationOutcome::rejected(remapPublicationRejection(zc::mv(linkedResult)));
  }
  LinkedOutputCandidate candidate = zc::mv(linkedResult).takeVerified();

  auto rejectCandidate = [&](PublicationRejection&& primary) -> PublicationOutcome {
    CleanupDisposition cleanup = zc::mv(candidate).discardAndCleanup();
    if (cleanup.isClean()) { return PublicationOutcome::rejected(zc::mv(primary)); }
    return PublicationOutcome::recoveryRequired(
        LinkRecoveryRequired::snapshot(zc::mv(primary), zc::mv(cleanup).takeObligation()));
  };

  zc::Maybe<detail::PublicationFileSnapshot> snapshot =
      detail::LinkPublicationTransaction::recheckOutput(candidate);
  zc::Maybe<zc::Array<uint8_t>> outputBytes;
  auto readException = zc::runCatchingExceptions([&]() { outputBytes = candidate.readOutput(); });
  if (snapshot == zc::none || outputBytes == zc::none || readException != zc::none) {
    return rejectCandidate(rejectPublication(IrFailureKind::InvalidFact, 30));
  }
  zc::Array<uint8_t> bytes = ZC_REQUIRE_NONNULL(zc::mv(outputBytes));
  zc::Maybe<identity::Sha256Digest> digest = identity::sha256(bytes.asPtr());
  if (digest == zc::none || ZC_REQUIRE_NONNULL(snapshot).identity != candidate.outputIdentity() ||
      ZC_REQUIRE_NONNULL(snapshot).byteCount != candidate.outputSize() ||
      ZC_REQUIRE_NONNULL(snapshot).digest != candidate.outputDigest() ||
      bytes.size() != ZC_REQUIRE_NONNULL(snapshot).byteCount ||
      ZC_REQUIRE_NONNULL(digest) != ZC_REQUIRE_NONNULL(snapshot).digest) {
    return rejectCandidate(rejectPublication(IrFailureKind::InvalidFact, 31));
  }

  const VerifiedLinkPlan& candidatePlan = candidate.plan();
  zc::Maybe<ExecutableInspectionFailure> inspection = ExecutableImageInspector::inspect(
      bytes.asPtr(), candidatePlan.inspectionProfile(), candidatePlan.entrySymbol());
  if (inspection != zc::none) {
    const IrFailureKind kind =
        ZC_REQUIRE_NONNULL(inspection) == ExecutableInspectionFailure::AbiMismatch
            ? IrFailureKind::InvalidAbi
            : IrFailureKind::InvalidFact;
    return rejectCandidate(rejectPublication(kind, 32));
  }

  zc::Maybe<zc::String> outputRoot = pathParent(candidatePlan.outputPath());
  if (outputRoot == zc::none) {
    return rejectCandidate(rejectPublication(IrFailureKind::InvalidFact, 33));
  }
  identity::Sha256Digest toolchain =
      ExecutablePublicationManifestBinding::toolchainIdentity(candidatePlan);
  ExecutableManifestRequest manifestRequest{
      zc::heapString(candidatePlan.outputPath()),
      ZC_REQUIRE_NONNULL(zc::mv(outputRoot)),
      copyBytes(candidatePlan.targetSpecificationIdentity()),
      ZC_REQUIRE_NONNULL(snapshot).digest,
      ZC_REQUIRE_NONNULL(snapshot).byteCount,
      candidatePlan.id(),
      ExecutablePublicationManifestBinding::inputArtifactDigests(candidatePlan),
      copyBytes(toolchain.bytes())};
  IrOperationResult<VerifiedExecutableManifest> manifest =
      ExecutableManifestVerifier::verify(zc::mv(manifestRequest));
  if (!manifest.isVerified()) {
    return rejectCandidate(remapPublicationRejection(zc::mv(manifest)));
  }
  return publishLinkedOutput(zc::mv(candidate), zc::mv(manifest).takeVerified());
}

PublicationRecoveryResult recoverLinkedOutputPublicationObserved(
    const zc::Filesystem& filesystem, zc::StringPtr finalDestination,
    PublicationCheckpointObserver* observer) {
#if !ZOM_LINK_PUBLICATION_SUPPORTED
  (void)filesystem;
  (void)finalDestination;
  (void)observer;
  return PublicationRecoveryResult::explicitRepairRequired();
#else
  zc::Maybe<zc::String> parentMaybe = pathParent(finalDestination);
  zc::Maybe<zc::String> executableLeafMaybe = pathLeaf(finalDestination);
  if (parentMaybe == zc::none || executableLeafMaybe == zc::none) {
    return PublicationRecoveryResult::explicitRepairRequired();
  }
  zc::String outputParent = ZC_REQUIRE_NONNULL(zc::mv(parentMaybe));
  zc::String executableLeaf = ZC_REQUIRE_NONNULL(zc::mv(executableLeafMaybe));
  zc::String manifestLeaf = zc::str(executableLeaf, kManifestSuffix);
  zc::String manifestPath = zc::str(finalDestination, kManifestSuffix);
  const zc::Directory& root = filesystem.getRoot();
  zc::String parentRelative =
      outputParent == "/"_zc ? zc::str(".") : zc::heapString(outputParent.slice(1));
  zc::Maybe<zc::Own<const zc::Directory>> outputDirMaybe;
  auto openException = zc::runCatchingExceptions([&]() {
    outputDirMaybe = root.tryOpenSubdir(zc::Path::parse(parentRelative), zc::WriteMode::MODIFY);
  });
  if (openException != zc::none) { return PublicationRecoveryResult::explicitRepairRequired(); }
  if (outputDirMaybe == zc::none) { return PublicationRecoveryResult::clean(); }
  zc::Own<const zc::Directory> outputDir = ZC_REQUIRE_NONNULL(zc::mv(outputDirMaybe));
  zc::Maybe<int> rawDirectoryFd = outputDir->getFd();
  if (rawDirectoryFd == zc::none) { return PublicationRecoveryResult::explicitRepairRequired(); }
  int duplicated = ::dup(ZC_REQUIRE_NONNULL(rawDirectoryFd));
  if (duplicated < 0) { return PublicationRecoveryResult::explicitRepairRequired(); }
  zc::OwnFd directoryFdOwner(duplicated);
  int directoryFd = directoryFdOwner.get();
  if (!trustedOutputDirectory(directoryFd)) {
    return PublicationRecoveryResult::explicitRepairRequired();
  }

  zc::Vector<zc::String> matchingTokens;
  bool malformedStarted = false;
  auto listException = zc::runCatchingExceptions([&]() {
    for (const zc::String& name : outputDir->listNames()) {
      zc::StringPtr text(name);
      if (!text.startsWith("journal."_zc) || !text.endsWith(".started"_zc)) { continue; }
      if (text.size() != 48) {
        malformedStarted = true;
        continue;
      }
      zc::String tokenHex = zc::heapString(text.slice(8, 40));
      zc::Maybe<zc::Array<uint8_t>> bytes = readFileAt(directoryFd, text);
      if (bytes == zc::none) {
        malformedStarted = true;
        continue;
      }
      zc::Maybe<JournalRecord> record =
          decodeJournal(ZC_REQUIRE_NONNULL(bytes).asPtr(), JournalStage::Started, tokenHex);
      if (record == zc::none) {
        malformedStarted = true;
        continue;
      }
      if (ZC_REQUIRE_NONNULL(record).executablePath == finalDestination) {
        matchingTokens.add(zc::mv(tokenHex));
      }
    }
  });
  if (listException != zc::none) { return PublicationRecoveryResult::explicitRepairRequired(); }
  // A complete pair that verifies against a decodable manifest for THIS final
  // destination is published, and that decision must be independent of any
  // unrelated residue in the shared output directory: another target's
  // pre-Started root, orphan journal, or even a malformed `.started`. A malformed
  // record decodes to no target, so it never joins matchingTokens; when this
  // target has no matching Started chain, its published pair is authoritative and
  // the neighbour's residue is neither consulted nor deleted. This check runs
  // before the malformedStarted fail-fast for exactly that reason.
  auto verifiedPublishedPair = [&]() -> bool {
    if (entryAbsent(directoryFd, executableLeaf) || entryAbsent(directoryFd, manifestLeaf)) {
      return false;
    }
    zc::Maybe<zc::Array<uint8_t>> manifestBytes = readFileAt(directoryFd, manifestLeaf);
    if (manifestBytes == zc::none) { return false; }
    IrOperationResult<VerifiedExecutableManifest> decoded =
        ExecutableManifestCodec::decode(ZC_REQUIRE_NONNULL(manifestBytes).asPtr(), outputParent);
    if (!decoded.isVerified()) { return false; }
    VerifiedExecutableManifest manifest = zc::mv(decoded).takeVerified();
    zc::String executable = zc::heapString(executableLeaf);
    int executableRaw = ::openat(directoryFd, executable.cStr(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (executableRaw < 0) { return false; }
    zc::OwnFd executableFd(executableRaw);
    zc::Maybe<detail::PublicationFileSnapshot> output = snapshotFd(executableFd.get());
    return output != zc::none && manifest.finalDestination() == finalDestination &&
           ZC_REQUIRE_NONNULL(output).digest == manifest.executableDigest() &&
           ZC_REQUIRE_NONNULL(output).byteCount == manifest.executableByteCount();
  };
  if (matchingTokens.size() == 0 && verifiedPublishedPair()) {
    return PublicationRecoveryResult::published();
  }
  if (malformedStarted) { return PublicationRecoveryResult::explicitRepairRequired(); }
  if (matchingTokens.size() == 0) {
    bool executableExists = !entryAbsent(directoryFd, executableLeaf);
    bool manifestExists = !entryAbsent(directoryFd, manifestLeaf);
    bool unjournaledRoot = false;
    bool orphanJournalArtifact = false;
    for (const zc::String& name : outputDir->listNames()) {
      zc::StringPtr text(name);
      if (text.startsWith(".zomlink-"_zc)) { unjournaledRoot = true; }
      if (text.startsWith("journal."_zc) || text.startsWith(".journal."_zc)) {
        orphanJournalArtifact = true;
      }
    }
    if (unjournaledRoot || orphanJournalArtifact || executableExists || manifestExists) {
      return PublicationRecoveryResult::explicitRepairRequired();
    }
    return PublicationRecoveryResult::clean();
  }
  if (matchingTokens.size() != 1) { return PublicationRecoveryResult::explicitRepairRequired(); }
  zc::String tokenHex = zc::mv(matchingTokens[0]);

  auto load = [&](JournalStage stage) -> zc::Maybe<zc::Array<uint8_t>> {
    return readFileAt(directoryFd, zc::str("journal.", tokenHex, ".", stageName(stage)));
  };
  zc::Maybe<zc::Array<uint8_t>> startedBytes = load(JournalStage::Started);
  if (startedBytes == zc::none) { return PublicationRecoveryResult::explicitRepairRequired(); }
  zc::Maybe<JournalRecord> startedRecord =
      decodeJournal(ZC_REQUIRE_NONNULL(startedBytes).asPtr(), JournalStage::Started, tokenHex);
  if (startedRecord == zc::none) { return PublicationRecoveryResult::explicitRepairRequired(); }
  JournalRecord started = ZC_REQUIRE_NONNULL(zc::mv(startedRecord));
  if (started.executablePath != finalDestination || started.manifestPath != manifestPath) {
    return PublicationRecoveryResult::explicitRepairRequired();
  }

  JournalRecord* highest = &started;
  JournalRecord manifestStaged = JournalRecord{JournalStage::Started,
                                               identity::Sha256Digest(),
                                               false,
                                               started.token,
                                               zc::heapString(started.executablePath),
                                               zc::heapString(started.manifestPath),
                                               zc::heapString(started.manifestTempPath),
                                               started.planId,
                                               started.rootIdentity,
                                               started.output,
                                               started.expectedManifestDigest,
                                               zc::none,
                                               identity::Sha256Digest()};
  JournalRecord execCommitted = JournalRecord{JournalStage::Started,
                                              identity::Sha256Digest(),
                                              false,
                                              started.token,
                                              zc::heapString(started.executablePath),
                                              zc::heapString(started.manifestPath),
                                              zc::heapString(started.manifestTempPath),
                                              started.planId,
                                              started.rootIdentity,
                                              started.output,
                                              started.expectedManifestDigest,
                                              zc::none,
                                              identity::Sha256Digest()};
  JournalRecord manifestCommitted = JournalRecord{JournalStage::Started,
                                                  identity::Sha256Digest(),
                                                  false,
                                                  started.token,
                                                  zc::heapString(started.executablePath),
                                                  zc::heapString(started.manifestPath),
                                                  zc::heapString(started.manifestTempPath),
                                                  started.planId,
                                                  started.rootIdentity,
                                                  started.output,
                                                  started.expectedManifestDigest,
                                                  zc::none,
                                                  identity::Sha256Digest()};
  zc::Vector<zc::String> journalLeaves;
  journalLeaves.add(journalLeaf(started.token, JournalStage::Started));
  identity::Sha256Digest previousId = requireDigest(ZC_REQUIRE_NONNULL(startedBytes).asPtr());

  const JournalStage laterStages[] = {JournalStage::ManifestStaged, JournalStage::ExecCommitted,
                                      JournalStage::ManifestCommitted};
  for (JournalStage stage : laterStages) {
    zc::Maybe<zc::Array<uint8_t>> bytes = load(stage);
    if (bytes == zc::none) {
      for (JournalStage later : laterStages) {
        if (static_cast<uint8_t>(later) <= static_cast<uint8_t>(stage)) { continue; }
        if (load(later) != zc::none) { return PublicationRecoveryResult::explicitRepairRequired(); }
      }
      break;
    }
    zc::Maybe<JournalRecord> decoded =
        decodeJournal(ZC_REQUIRE_NONNULL(bytes).asPtr(), stage, tokenHex);
    if (decoded == zc::none) { return PublicationRecoveryResult::explicitRepairRequired(); }
    JournalRecord value = ZC_REQUIRE_NONNULL(zc::mv(decoded));
    if (!sameJournalTransaction(started, value) || !value.hasPrevious ||
        value.previousId != previousId) {
      return PublicationRecoveryResult::explicitRepairRequired();
    }
    if (stage == JournalStage::ManifestStaged) {
      manifestStaged = zc::mv(value);
      highest = &manifestStaged;
    } else if (stage == JournalStage::ExecCommitted) {
      if (manifestStaged.stage != JournalStage::ManifestStaged ||
          value.output.identity != manifestStaged.output.identity ||
          value.output.byteCount != manifestStaged.output.byteCount ||
          value.output.digest != manifestStaged.output.digest ||
          value.manifestTempIdentity != manifestStaged.manifestTempIdentity ||
          value.manifestTempDigest != manifestStaged.manifestTempDigest) {
        return PublicationRecoveryResult::explicitRepairRequired();
      }
      execCommitted = zc::mv(value);
      highest = &execCommitted;
    } else {
      if (execCommitted.stage != JournalStage::ExecCommitted ||
          value.output.identity != execCommitted.output.identity ||
          value.output.byteCount != execCommitted.output.byteCount ||
          value.output.digest != execCommitted.output.digest ||
          value.manifestTempIdentity != execCommitted.manifestTempIdentity ||
          value.manifestTempDigest != execCommitted.manifestTempDigest) {
        return PublicationRecoveryResult::explicitRepairRequired();
      }
      manifestCommitted = zc::mv(value);
      highest = &manifestCommitted;
    }
    journalLeaves.add(journalLeaf(started.token, stage));
    previousId = requireDigest(ZC_REQUIRE_NONNULL(bytes).asPtr());
  }

  zc::Maybe<zc::String> manifestTempLeafMaybe = pathLeaf(highest->manifestTempPath);
  zc::Maybe<zc::String> manifestTempParent = pathParent(highest->manifestTempPath);
  if (manifestTempLeafMaybe == zc::none || manifestTempParent == zc::none ||
      ZC_REQUIRE_NONNULL(manifestTempParent) != outputParent) {
    return PublicationRecoveryResult::explicitRepairRequired();
  }
  zc::String manifestTemp = ZC_REQUIRE_NONNULL(zc::mv(manifestTempLeafMaybe));

  auto obligation = [&](zc::Maybe<SnapshotCleanupObligation>&& snapshot) {
    return detail::PublicationTransactionMinter::obligation(
        highest->token, zc::heapString(highest->executablePath),
        zc::heapString(highest->manifestPath), zc::heapString(outputParent),
        highest->output.identity, highest->stage, zc::mv(snapshot));
  };
  auto recoveryRequired = [&](zc::Maybe<SnapshotCleanupObligation>&& snapshot) {
    return PublicationRecoveryResult::recoveryRequired(obligation(zc::mv(snapshot)));
  };

  bool executableExists = !entryAbsent(directoryFd, executableLeaf);
  bool manifestExists = !entryAbsent(directoryFd, manifestLeaf);
  bool published = false;
  bool claimExecutable = false;
  if (highest->stage == JournalStage::ManifestCommitted) {
    if (!executableExists || !manifestExists ||
        !verifyingFinalPair(directoryFd, executableLeaf, manifestLeaf, *highest)) {
      return PublicationRecoveryResult::explicitRepairRequired();
    }
    published = true;
  } else if (manifestExists) {
    if (!executableExists || highest->stage != JournalStage::ExecCommitted ||
        !verifyingFinalPair(directoryFd, executableLeaf, manifestLeaf, *highest)) {
      return PublicationRecoveryResult::explicitRepairRequired();
    }
    published = true;
  } else if (executableExists) {
    if (!entryMatches(directoryFd, executableLeaf, highest->output.identity)) {
      return PublicationRecoveryResult::explicitRepairRequired();
    }
    claimExecutable = true;
  } else if (highest->stage == JournalStage::ExecCommitted) {
    // ExecCommitted durably records the executable rename, so a now-absent `app`
    // with no final manifest is undecidable external tampering, not a rollback
    // point. Fail closed per the crash-recovery matrix catch-all: retain the
    // snapshot root and journal chain, report for explicit repair, and never
    // infer a delete of owner-proved evidence.
    return PublicationRecoveryResult::explicitRepairRequired();
  }

  bool retainUnprovedTemp = false;
  bool retainUnprovedJournalTemp = false;
  bool claimManifestTemp = false;
  zc::String journalTempPrefix = zc::str(".journal.", tokenHex, ".");
  for (const zc::String& name : outputDir->listNames()) {
    zc::StringPtr text(name);
    if (text.startsWith(journalTempPrefix) && text.endsWith(".tmp"_zc)) {
      retainUnprovedJournalTemp = true;
    }
  }
  if (!published && highest->stage == JournalStage::Started &&
      !entryAbsent(directoryFd, manifestTemp)) {
    retainUnprovedTemp = true;
  } else if (highest->stage != JournalStage::Started && !entryAbsent(directoryFd, manifestTemp)) {
    claimManifestTemp = true;
  }

  RecoveryTreeCleanup rootCleanup =
      cleanupRecordedRoot(*outputDir, directoryFd, *highest, claimExecutable, executableLeaf,
                          claimManifestTemp, manifestTemp, highest->manifestTempIdentity, observer);
  if (!rootCleanup.clean) {
    // An identity mismatch or unavailable identity is a non-retriable tamper /
    // ownership-ambiguity: retrying the same cleanup would loop forever, so
    // retain all evidence and demand explicit repair. Only a transient IO cleanup
    // failure (content or top-level removal) is a recoverable debt with an
    // obligation to retry.
    if (rootCleanup.kind == CleanupFailureKind::IdentityMismatch ||
        rootCleanup.kind == CleanupFailureKind::IdentityUnavailable) {
      return PublicationRecoveryResult::explicitRepairRequired();
    }
    SnapshotCleanupObligation nested = detail::LinkPublicationTransaction::snapshotObligation(
        highest->token, zc::heapString(outputParent), highest->rootIdentity, highest->planId,
        rootCleanup.kind);
    zc::Maybe<SnapshotCleanupObligation> snapshot(zc::mv(nested));
    return recoveryRequired(zc::mv(snapshot));
  }
  if (!removeJournalChain(directoryFd, journalLeaves.asPtr())) {
    zc::Maybe<SnapshotCleanupObligation> noSnapshot;
    return recoveryRequired(zc::mv(noSnapshot));
  }
  if (retainUnprovedTemp || retainUnprovedJournalTemp) {
    return PublicationRecoveryResult::explicitRepairRequired();
  }
  return published ? PublicationRecoveryResult::published() : PublicationRecoveryResult::clean();
#endif
}

PublicationRecoveryResult recoverLinkedOutputPublication(const zc::Filesystem& filesystem,
                                                         zc::StringPtr finalDestination) {
  return recoverLinkedOutputPublicationObserved(filesystem, finalDestination, nullptr);
}

PublicationRecoveryResult PublicationTransactionTestAccess::recoverObserved(
    const zc::Filesystem& filesystem, zc::StringPtr finalDestination,
    PublicationCheckpointObserver& observer) {
  return recoverLinkedOutputPublicationObserved(filesystem, finalDestination, &observer);
}

}  // namespace zomlang::compiler::ir
