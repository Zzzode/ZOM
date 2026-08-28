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

  // Input artifact digests must be strictly ascending (sorted, no duplicates).
  for (size_t index = 1; index < request.inputArtifactDigests.size(); ++index) {
    const auto& previous = request.inputArtifactDigests[index - 1];
    const auto& current = request.inputArtifactDigests[index];
    if (!(previous < current)) { return rejectManifest(IrFailureKind::CanonicalCodecMismatch, 5); }
  }

  auto manifest = VerifiedExecutableManifest(
      zc::mv(request.finalDestination), zc::mv(request.targetSpecificationIdentity),
      request.executableDigest, request.executableByteCount, request.linkPlanId,
      zc::mv(request.inputArtifactDigests), zc::mv(request.toolchainIdentity),
      ExecutableManifestId());
  manifest.idValue = ExecutableManifestCodec::computeId(manifest);
  return IrOperationResult<VerifiedExecutableManifest>::verified(zc::mv(manifest));
}

}  // namespace zomlang::compiler::ir
