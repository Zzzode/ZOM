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

#include "compiler/ir/executable-manifest-codec.h"

#include "compiler/identity/identity-invariant.h"
#include "compiler/identity/semantic/context-fingerprint.h"

namespace zomlang::compiler::ir {
namespace {

void appendUint64(zc::Vector<uint8_t>& output, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    output.add(static_cast<uint8_t>(value >> static_cast<uint32_t>(shift)));
  }
}

void appendFramed(zc::Vector<uint8_t>& output, zc::ArrayPtr<const uint8_t> value) {
  appendUint64(output, value.size());
  output.addAll(value);
}

void appendFramed(zc::Vector<uint8_t>& output, zc::StringPtr value) {
  appendUint64(output, value.size());
  for (const auto byte : value) { output.add(static_cast<uint8_t>(byte)); }
}

class DecodeCursor final {
public:
  explicit DecodeCursor(zc::ArrayPtr<const uint8_t> bytes) : bytes(bytes) {}

  bool consume(zc::StringPtr expected) {
    if (position + expected.size() > bytes.size()) { return false; }
    for (char byte : expected) {
      if (bytes[position++] != static_cast<uint8_t>(byte)) { return false; }
    }
    return true;
  }

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

  bool atEnd() const noexcept { return position == bytes.size(); }

private:
  zc::ArrayPtr<const uint8_t> bytes;
  size_t position = 0;
};

identity::Sha256Digest requireDigest(zc::ArrayPtr<const uint8_t> bytes) {
  auto digest = identity::sha256(bytes);
  ZC_IF_SOME(value, digest) { return value; }
  ZC_UNREACHABLE
}

// A normalized path in this slice is a non-empty absolute POSIX path with no
// unnormalized "." or ".." segment and no empty interior segment. This matches
// the pure-value normalization the link-plan codec uses; RFC 0043's live path
// service performs the filesystem-bound normalization in a later slice.
bool isNormalizedAbsolutePath(zc::StringPtr path) {
  if (path.size() == 0 || path[0] != '/') { return false; }
  size_t segmentStart = 1;
  for (size_t index = 1; index <= path.size(); ++index) {
    const bool atEnd = index == path.size();
    if (!atEnd && path[index] != '/') { continue; }
    const size_t length = index - segmentStart;
    if (length == 0) { return false; }
    if (length == 1 && path[segmentStart] == '.') { return false; }
    if (length == 2 && path[segmentStart] == '.' && path[segmentStart + 1] == '.') { return false; }
    segmentStart = index + 1;
  }
  return true;
}

// True when `path` equals `root` or is a descendant of `root` (a normalized
// same-tree containment check on already-normalized absolute paths).
bool isInsideRoot(zc::StringPtr root, zc::StringPtr path) {
  if (root.size() == 0 || path.size() < root.size()) { return false; }
  for (size_t index = 0; index < root.size(); ++index) {
    if (path[index] != root[index]) { return false; }
  }
  if (path.size() == root.size()) { return true; }
  const bool rootEndsWithSlash = root[root.size() - 1] == '/';
  return rootEndsWithSlash || path[root.size()] == '/';
}

// The session context bound to an executable-publication rejection. The link and
// publication phases are session-owned (RFC 0043 failure table), so the fact
// carries the session fingerprint and never needs module/definition identity.
identity::ContextFingerprint manifestSessionContext() {
  auto digest = requireDigest("zom.executable-manifest"_zc.asBytes());
  return identity::ContextFingerprint::fromCanonicalDigest(digest);
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

// Builds a single session-owned ExecutablePublication rejection with `kind`.
IrOperationResult<VerifiedExecutableManifest> rejectManifest(IrFailureKind kind, uint32_t ordinal) {
  UnusedIdentityResolver resolver;
  auto fallback =
      IrFailureFallbackContext::from(IrFailurePhase::ExecutablePublication,
                                     IrFailureOwner::session(manifestSessionContext().clone()));
  ZC_IREQUIRE(fallback != zc::none, "Executable publication failure fallback must be legal");
  zc::Maybe<IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = IrFailureDescriptor::decoded(
      IrRejectedBranch::IrInvariantRejected, IrFailurePhase::ExecutablePublication, kind,
      IrFailureOwner::session(manifestSessionContext().clone()), zc::mv(noSite),
      IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(context, fallback) {
    auto admitted = IrFailureFactory::admit(zc::mv(descriptor), context, resolver);
    ZC_IREQUIRE(admitted.is<AcceptedIrFailureDescriptor>(),
                "Session-owned manifest rejection must admit without identity expansion");
    zc::Vector<IrFailureFact> facts;
    facts.add(zc::mv(admitted).get<AcceptedIrFailureDescriptor>().fact);
    auto sorted = SortedIrInvariantFailureFacts::from(zc::mv(facts));
    ZC_IF_SOME(values, sorted) {
      return IrOperationResult<VerifiedExecutableManifest>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

}  // namespace

// ---------------------------------------------------------------------------
// ExecutableManifestCodec
// ---------------------------------------------------------------------------

zc::Array<uint8_t> ExecutableManifestCodec::encode(const VerifiedExecutableManifest& manifest) {
  zc::Vector<uint8_t> preimage;
  for (const auto byte : "zom.executable-manifest"_zc) { preimage.add(static_cast<uint8_t>(byte)); }
  preimage.add(0);
  appendFramed(preimage, manifest.finalDestination());
  appendFramed(preimage, manifest.targetSpecificationIdentity());
  appendFramed(preimage, manifest.executableDigest().bytes());
  appendUint64(preimage, manifest.executableByteCount());
  appendFramed(preimage, manifest.linkPlanId().digest().bytes());
  const auto inputs = manifest.inputArtifactDigests();
  appendUint64(preimage, inputs.size());
  for (const auto& digest : inputs) { appendFramed(preimage, digest.bytes()); }
  appendFramed(preimage, manifest.toolchainIdentity());
  return preimage.releaseAsArray();
}

IrOperationResult<VerifiedExecutableManifest> ExecutableManifestCodec::decode(
    zc::ArrayPtr<const uint8_t> bytes, zc::StringPtr outputRoot) {
  DecodeCursor cursor(bytes);
  if (!cursor.consume("zom.executable-manifest"_zc)) {
    return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 6);
  }
  zc::Maybe<uint8_t> separator = cursor.byte();
  zc::Maybe<zc::String> finalDestination = cursor.stringFrame();
  zc::Maybe<zc::ArrayPtr<const uint8_t>> targetIdentity = cursor.frame();
  zc::Maybe<zc::ArrayPtr<const uint8_t>> executableDigestBytes = cursor.frame();
  zc::Maybe<uint64_t> byteCount = cursor.uint64();
  zc::Maybe<zc::ArrayPtr<const uint8_t>> linkPlanDigestBytes = cursor.frame();
  zc::Maybe<uint64_t> inputCount = cursor.uint64();
  if (separator == zc::none || ZC_REQUIRE_NONNULL(separator) != 0 || finalDestination == zc::none ||
      targetIdentity == zc::none || executableDigestBytes == zc::none || byteCount == zc::none ||
      linkPlanDigestBytes == zc::none || inputCount == zc::none ||
      ZC_REQUIRE_NONNULL(inputCount) > bytes.size() / 40) {
    return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 6);
  }
  zc::Maybe<identity::Sha256Digest> executableDigest =
      identity::Sha256Digest::fromBytes(ZC_REQUIRE_NONNULL(executableDigestBytes));
  zc::Maybe<identity::Sha256Digest> linkPlanDigest =
      identity::Sha256Digest::fromBytes(ZC_REQUIRE_NONNULL(linkPlanDigestBytes));
  if (executableDigest == zc::none || linkPlanDigest == zc::none) {
    return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 6);
  }
  zc::Vector<identity::Sha256Digest> inputDigests;
  for (uint64_t index = 0; index < ZC_REQUIRE_NONNULL(inputCount); ++index) {
    zc::Maybe<zc::ArrayPtr<const uint8_t>> digestBytes = cursor.frame();
    if (digestBytes == zc::none) {
      return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 6);
    }
    zc::Maybe<identity::Sha256Digest> digest =
        identity::Sha256Digest::fromBytes(ZC_REQUIRE_NONNULL(digestBytes));
    if (digest == zc::none) { return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 6); }
    inputDigests.add(ZC_REQUIRE_NONNULL(digest));
  }
  zc::Maybe<zc::ArrayPtr<const uint8_t>> toolchainIdentity = cursor.frame();
  if (toolchainIdentity == zc::none || !cursor.atEnd()) {
    return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 6);
  }
  auto targetCopy = zc::heapArray<uint8_t>(ZC_REQUIRE_NONNULL(targetIdentity).size());
  for (size_t index = 0; index < targetCopy.size(); ++index) {
    targetCopy[index] = ZC_REQUIRE_NONNULL(targetIdentity)[index];
  }
  auto toolchainCopy = zc::heapArray<uint8_t>(ZC_REQUIRE_NONNULL(toolchainIdentity).size());
  for (size_t index = 0; index < toolchainCopy.size(); ++index) {
    toolchainCopy[index] = ZC_REQUIRE_NONNULL(toolchainIdentity)[index];
  }
  ExecutableManifestRequest request{ZC_REQUIRE_NONNULL(zc::mv(finalDestination)),
                                    zc::heapString(outputRoot),
                                    zc::mv(targetCopy),
                                    ZC_REQUIRE_NONNULL(executableDigest),
                                    ZC_REQUIRE_NONNULL(byteCount),
                                    LinkPlanId::fromDigest(ZC_REQUIRE_NONNULL(linkPlanDigest)),
                                    inputDigests.releaseAsArray(),
                                    zc::mv(toolchainCopy)};
  IrOperationResult<VerifiedExecutableManifest> result =
      ExecutableManifestVerifier::verify(zc::mv(request));
  if (!result.isVerified()) { return result; }
  VerifiedExecutableManifest manifest = zc::mv(result).takeVerified();
  if (encode(manifest).asPtr() != bytes) {
    return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 6);
  }
  return IrOperationResult<VerifiedExecutableManifest>::verified(zc::mv(manifest));
}

ExecutableManifestId ExecutableManifestCodec::computeId(
    const VerifiedExecutableManifest& manifest) {
  auto bytes = encode(manifest);
  return ExecutableManifestId::fromDigest(requireDigest(bytes.asPtr()));
}

// ---------------------------------------------------------------------------
// ExecutableManifestVerifier
// ---------------------------------------------------------------------------

IrOperationResult<VerifiedExecutableManifest> ExecutableManifestVerifier::verify(
    ExecutableManifestRequest&& request) {
  // The final destination must be a normalized absolute path inside the output root.
  if (!isNormalizedAbsolutePath(request.outputRoot) ||
      !isNormalizedAbsolutePath(request.finalDestination)) {
    return rejectManifest(IrFailureKind::InvalidFact, 0);
  }
  if (!isInsideRoot(request.outputRoot, request.finalDestination)) {
    return rejectManifest(IrFailureKind::InvalidFact, 1);
  }

  // The executable must have a non-zero byte count.
  if (request.executableByteCount == 0) { return rejectManifest(IrFailureKind::InvalidFact, 2); }

  // Target and toolchain identities must be present.
  if (request.targetSpecificationIdentity.size() == 0) {
    return rejectManifest(IrFailureKind::MissingRequiredFact, 3);
  }
  if (request.toolchainIdentity.size() == 0) {
    return rejectManifest(IrFailureKind::MissingRequiredFact, 4);
  }

  // The sequence order is semantically meaningful: D1 live-binds it to the
  // verified plan's canonical linker-input order (CRT, object, runtime, default
  // library). It is therefore carried verbatim rather than sorted by digest;
  // equal digests at distinct canonical input positions are legal.

  auto manifest = VerifiedExecutableManifest(
      zc::mv(request.finalDestination), zc::mv(request.targetSpecificationIdentity),
      request.executableDigest, request.executableByteCount, request.linkPlanId,
      zc::mv(request.inputArtifactDigests), zc::mv(request.toolchainIdentity),
      ExecutableManifestId());
  manifest.idValue = ExecutableManifestCodec::computeId(manifest);
  return IrOperationResult<VerifiedExecutableManifest>::verified(zc::mv(manifest));
}

}  // namespace zomlang::compiler::ir
