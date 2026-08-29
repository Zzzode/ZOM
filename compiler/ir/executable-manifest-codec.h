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

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/ir-failure.h"
#include "compiler/ir/link-plan-codec.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"

namespace zomlang::compiler::ir {

/// \brief The domain-separated immutable identity of one verified executable
/// artifact manifest.
///
/// Computed by `ExecutableManifestCodec` as SHA-256 over the manifest's
/// canonical preimage and compared by digest.
class ExecutableManifestId final {
public:
  constexpr ExecutableManifestId() noexcept = default;

  ZC_NODISCARD static ExecutableManifestId fromDigest(
      const identity::Sha256Digest& digest) noexcept {
    return ExecutableManifestId(digest);
  }
  ZC_NODISCARD const identity::Sha256Digest& digest() const noexcept { return value; }

  bool operator==(const ExecutableManifestId& other) const noexcept { return value == other.value; }
  bool operator!=(const ExecutableManifestId& other) const noexcept { return !(*this == other); }

private:
  explicit ExecutableManifestId(const identity::Sha256Digest& digest) noexcept : value(digest) {}

  identity::Sha256Digest value;
};

/// \brief An executable artifact manifest whose canonical invariants an
/// independent verifier proved.
///
/// RFC 0043 "Executable Verification And Publication": `PublishedExecutableArtifact`
/// owns the normalized final destination, target identity, executable digest,
/// byte count, `LinkPlanId`, and the immutable `ExecutableArtifactManifest`. The
/// manifest is a canonical, domain-separated encoding of that data plus the
/// ordered input artifact digests and toolchain identity. This value type models
/// exactly that manifest and computes its deterministic identity; it has no
/// public aggregate initializer, so only `ExecutableManifestVerifier::verify`
/// constructs one.
///
/// This slice models and verifies the manifest as pure data. It does not link an
/// executable, read or write the filesystem, or bind a live target-registry
/// capability; the `linkExecutable` session API, the ELF/Mach-O executable
/// verifier, and atomic two-file publication are later RFC 0043 slices.
class VerifiedExecutableManifest final {
public:
  VerifiedExecutableManifest(VerifiedExecutableManifest&&) noexcept = default;
  VerifiedExecutableManifest& operator=(VerifiedExecutableManifest&&) noexcept = default;
  ZC_DISALLOW_COPY(VerifiedExecutableManifest);
  ~VerifiedExecutableManifest() noexcept = default;

  ZC_NODISCARD zc::StringPtr finalDestination() const noexcept { return finalDestinationValue; }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> targetSpecificationIdentity() const noexcept {
    return targetIdentityValue.asPtr();
  }
  ZC_NODISCARD const identity::Sha256Digest& executableDigest() const noexcept {
    return executableDigestValue;
  }
  ZC_NODISCARD uint64_t executableByteCount() const noexcept { return executableByteCountValue; }
  ZC_NODISCARD const LinkPlanId& linkPlanId() const noexcept { return linkPlanIdValue; }
  ZC_NODISCARD zc::ArrayPtr<const identity::Sha256Digest> inputArtifactDigests() const noexcept {
    return inputDigestValues.asPtr();
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> toolchainIdentity() const noexcept {
    return toolchainIdentityValue.asPtr();
  }
  ZC_NODISCARD const ExecutableManifestId& id() const noexcept { return idValue; }

private:
  friend class ExecutableManifestVerifier;

  VerifiedExecutableManifest(zc::String&& finalDestination, zc::Array<uint8_t>&& targetIdentity,
                             const identity::Sha256Digest& executableDigest,
                             uint64_t executableByteCount, const LinkPlanId& linkPlanId,
                             zc::Array<identity::Sha256Digest>&& inputDigests,
                             zc::Array<uint8_t>&& toolchainIdentity,
                             const ExecutableManifestId& id) noexcept
      : finalDestinationValue(zc::mv(finalDestination)),
        targetIdentityValue(zc::mv(targetIdentity)),
        executableDigestValue(executableDigest),
        executableByteCountValue(executableByteCount),
        linkPlanIdValue(linkPlanId),
        inputDigestValues(zc::mv(inputDigests)),
        toolchainIdentityValue(zc::mv(toolchainIdentity)),
        idValue(id) {}

  zc::String finalDestinationValue;
  zc::Array<uint8_t> targetIdentityValue;
  identity::Sha256Digest executableDigestValue;
  uint64_t executableByteCountValue;
  LinkPlanId linkPlanIdValue;
  zc::Array<identity::Sha256Digest> inputDigestValues;
  zc::Array<uint8_t> toolchainIdentityValue;
  ExecutableManifestId idValue;
};

/// \brief The unverified request an independent verifier turns into a manifest.
///
/// RFC 0043 "Executable Verification And Publication": the manifest is produced
/// from a verified link result, so this slice models exactly the fields the
/// verifier proves. The live request bound to a `PublishedExecutableArtifact` and
/// a `TargetRegistryCapability` is a later slice.
struct ExecutableManifestRequest final {
  zc::String finalDestination;
  zc::String outputRoot;
  zc::Array<uint8_t> targetSpecificationIdentity;
  identity::Sha256Digest executableDigest;
  uint64_t executableByteCount = 0;
  LinkPlanId linkPlanId;
  zc::Array<identity::Sha256Digest> inputArtifactDigests;
  zc::Array<uint8_t> toolchainIdentity;
};

/// \brief Canonical codec for the verified executable manifest.
///
/// The preimage is a domain-separated, length-framed encoding:
///   ASCII("zom.executable-manifest") 0x00
///   Frame(finalDestination)
///   Frame(targetSpecificationIdentity)
///   Frame(executableDigest) uint64(executableByteCount)
///   Frame(linkPlanId digest)
///   uint64(inputDigestCount) [Frame(digest)...]
///   Frame(toolchainIdentity)
/// `Frame` is a big-endian uint64 byte length followed by the exact bytes.
class ExecutableManifestCodec final {
public:
  /// \brief Encodes a verified manifest to its canonical preimage bytes.
  ZC_NODISCARD static zc::Array<uint8_t> encode(const VerifiedExecutableManifest& manifest);
  /// \brief Computes the manifest's `ExecutableManifestId` (SHA-256 of the preimage).
  ZC_NODISCARD static ExecutableManifestId computeId(const VerifiedExecutableManifest& manifest);
};

/// \brief The independent verifier that constructs a `VerifiedExecutableManifest`.
///
/// RFC 0043 "Executable Verification And Publication": a manifest is admitted
/// only after its fields are proven well formed - a normalized final destination
/// inside the output root, a non-zero executable byte count, non-empty target and
/// toolchain identities, and ordered, duplicate-free input artifact digests.
/// Rejection consumes the request and publishes no partial manifest. Each
/// rejection maps to an RFC 0043 failure row under `ExecutablePublication`.
class ExecutableManifestVerifier final {
public:
  /// \brief Verifies the request and, on success, constructs the manifest.
  /// \param request The manifest request, consumed on every branch.
  /// \return A verified manifest, or an RFC 0010 `ExecutablePublication` rejection.
  ZC_NODISCARD static IrOperationResult<VerifiedExecutableManifest> verify(
      ExecutableManifestRequest&& request);
};

}  // namespace zomlang::compiler::ir
