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

#include "compiler/ir/executable-manifest-codec.h"
#include "compiler/ir/invoke-linker.h"
#include "compiler/ir/target-registry.h"
#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/one-of.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir {

namespace detail {
class PublicationTransactionMinter;
}

/// \brief Checks that produced executable bytes carry the expected object-format
///        magic.
ZC_NODISCARD bool inspectExecutableFormat(zc::ArrayPtr<const uint8_t> executableBytes,
                                          ObjectFormat expectedFormat);

/// \brief The durable stages of one manifest-last publication journal chain.
enum class JournalStage : uint8_t {
  Started = 0x01,
  ManifestStaged = 0x02,
  ExecCommitted = 0x03,
  ManifestCommitted = 0x04,
};

/// \brief Canonical manifest fields derived from a verified link plan.
///
/// D1 uses the same derivation to reject a manifest assembled from a different
/// plan; D5 uses it when constructing the manifest in the first place.
class ExecutablePublicationManifestBinding final {
public:
  ZC_NODISCARD static zc::Array<identity::Sha256Digest> inputArtifactDigests(
      const VerifiedLinkPlan& plan);
  ZC_NODISCARD static identity::Sha256Digest toolchainIdentity(const VerifiedLinkPlan& plan);
};

/// \brief A manifest-committed executable accepted for downstream use.
class PublishedExecutableArtifact final {
public:
  PublishedExecutableArtifact(PublishedExecutableArtifact&&) noexcept = default;
  PublishedExecutableArtifact& operator=(PublishedExecutableArtifact&&) noexcept = default;
  ZC_DISALLOW_COPY(PublishedExecutableArtifact);
  ~PublishedExecutableArtifact() noexcept = default;

  ZC_NODISCARD zc::StringPtr finalDestination() const noexcept {
    return manifestValue.finalDestination();
  }
  ZC_NODISCARD zc::ArrayPtr<const uint8_t> targetSpecificationIdentity() const noexcept {
    return manifestValue.targetSpecificationIdentity();
  }
  ZC_NODISCARD const identity::Sha256Digest& executableDigest() const noexcept {
    return manifestValue.executableDigest();
  }
  ZC_NODISCARD uint64_t executableByteCount() const noexcept {
    return manifestValue.executableByteCount();
  }
  ZC_NODISCARD const LinkPlanId& linkPlanId() const noexcept { return manifestValue.linkPlanId(); }
  ZC_NODISCARD const VerifiedExecutableManifest& manifest() const noexcept { return manifestValue; }

private:
  friend class detail::PublicationTransactionMinter;
  explicit PublishedExecutableArtifact(VerifiedExecutableManifest&& manifest) noexcept
      : manifestValue(zc::mv(manifest)) {}

  VerifiedExecutableManifest manifestValue;
};

using PublicationRejection = IrOperationResult<PublishedExecutableArtifact>;

/// \brief Durable owner proof for a publication pair that a recovery pass must
///        adjudicate without deleting by path alone.
class PublicationRecoveryObligation final {
public:
  PublicationRecoveryObligation(PublicationRecoveryObligation&&) noexcept = default;
  PublicationRecoveryObligation& operator=(PublicationRecoveryObligation&&) noexcept = default;
  ZC_DISALLOW_COPY(PublicationRecoveryObligation);
  ~PublicationRecoveryObligation() noexcept = default;

  ZC_NODISCARD const SnapshotTransactionId& transactionId() const noexcept {
    return transactionIdValue;
  }
  ZC_NODISCARD zc::StringPtr executablePath() const noexcept { return executablePathValue; }
  ZC_NODISCARD zc::StringPtr manifestPath() const noexcept { return manifestPathValue; }
  ZC_NODISCARD zc::StringPtr journalDirectory() const noexcept { return journalDirectoryValue; }
  ZC_NODISCARD const StableFileIdentity& commitPointIdentity() const noexcept {
    return commitPointIdentityValue;
  }
  ZC_NODISCARD JournalStage lastDurableStage() const noexcept { return stageValue; }
  ZC_NODISCARD bool hasSnapshotCleanupObligation() const noexcept {
    return snapshotObligationValue != zc::none;
  }
  ZC_NODISCARD SnapshotCleanupObligation takeSnapshotCleanupObligation() && {
    ZC_IREQUIRE(snapshotObligationValue != zc::none,
                "publication recovery has no nested snapshot cleanup obligation");
    return ZC_REQUIRE_NONNULL(zc::mv(snapshotObligationValue));
  }

private:
  friend class detail::PublicationTransactionMinter;
  PublicationRecoveryObligation(const SnapshotTransactionId& transactionId,
                                zc::String&& executablePath, zc::String&& manifestPath,
                                zc::String&& journalDirectory,
                                const StableFileIdentity& commitPointIdentity, JournalStage stage,
                                zc::Maybe<SnapshotCleanupObligation>&& snapshotObligation) noexcept
      : transactionIdValue(transactionId),
        executablePathValue(zc::mv(executablePath)),
        manifestPathValue(zc::mv(manifestPath)),
        journalDirectoryValue(zc::mv(journalDirectory)),
        commitPointIdentityValue(commitPointIdentity),
        stageValue(stage),
        snapshotObligationValue(zc::mv(snapshotObligation)) {}

  SnapshotTransactionId transactionIdValue;
  zc::String executablePathValue;
  zc::String manifestPathValue;
  zc::String journalDirectoryValue;
  StableFileIdentity commitPointIdentityValue;
  JournalStage stageValue;
  zc::Maybe<SnapshotCleanupObligation> snapshotObligationValue;
};

struct SnapshotRecoveryRequired final {
  PublicationRejection primary;
  SnapshotCleanupObligation obligation;
};

struct PublicationRecoveryRequired final {
  zc::Maybe<PublicationRejection> primary;
  PublicationRecoveryObligation obligation;
};

/// \brief The two disjoint recovery debts accepted by RFC 0043 D1.
class LinkRecoveryRequired final {
public:
  LinkRecoveryRequired(LinkRecoveryRequired&&) noexcept = default;
  LinkRecoveryRequired& operator=(LinkRecoveryRequired&&) noexcept = default;
  ZC_DISALLOW_COPY(LinkRecoveryRequired);
  ~LinkRecoveryRequired() noexcept = default;

  ZC_NODISCARD static LinkRecoveryRequired snapshot(PublicationRejection&& primary,
                                                    SnapshotCleanupObligation&& obligation);
  ZC_NODISCARD static LinkRecoveryRequired publication(zc::Maybe<PublicationRejection>&& primary,
                                                       PublicationRecoveryObligation&& obligation);

  ZC_NODISCARD bool isSnapshotRecoveryRequired() const noexcept;
  ZC_NODISCARD bool isPublicationRecoveryRequired() const noexcept;
  ZC_NODISCARD SnapshotRecoveryRequired takeSnapshot() &&;
  ZC_NODISCARD PublicationRecoveryRequired takePublication() &&;

private:
  explicit LinkRecoveryRequired(
      zc::OneOf<SnapshotRecoveryRequired, PublicationRecoveryRequired>&& value) noexcept;
  zc::OneOf<SnapshotRecoveryRequired, PublicationRecoveryRequired> value;
};

/// \brief The explicit three-way result of consuming a linked output candidate.
class ZC_NODISCARD PublicationOutcome final {
public:
  PublicationOutcome(PublicationOutcome&&) noexcept = default;
  PublicationOutcome& operator=(PublicationOutcome&&) noexcept = default;
  ZC_DISALLOW_COPY(PublicationOutcome);
  ~PublicationOutcome() noexcept = default;

  ZC_NODISCARD static PublicationOutcome published(PublishedExecutableArtifact&& artifact);
  ZC_NODISCARD static PublicationOutcome recoveryRequired(LinkRecoveryRequired&& recovery);
  ZC_NODISCARD static PublicationOutcome rejected(PublicationRejection&& rejection);

  ZC_NODISCARD bool isPublished() const noexcept;
  ZC_NODISCARD bool isRecoveryRequired() const noexcept;
  ZC_NODISCARD bool isRejected() const noexcept;
  ZC_NODISCARD PublishedExecutableArtifact takePublished() &&;
  ZC_NODISCARD LinkRecoveryRequired takeRecoveryRequired() &&;
  ZC_NODISCARD PublicationRejection takeRejected() &&;

private:
  explicit PublicationOutcome(zc::OneOf<PublishedExecutableArtifact, LinkRecoveryRequired,
                                        PublicationRejection>&& value) noexcept;
  zc::OneOf<PublishedExecutableArtifact, LinkRecoveryRequired, PublicationRejection> value;
};

enum class PublicationRecoveryStatus : uint8_t {
  Clean = 0x01,
  Published = 0x02,
  RecoveryRequired = 0x03,
  ExplicitRepairRequired = 0x04,
};

/// \brief Result of the explicit RFC 0043 D1 recovery entry point.
class ZC_NODISCARD PublicationRecoveryResult final {
public:
  PublicationRecoveryResult(PublicationRecoveryResult&&) noexcept = default;
  PublicationRecoveryResult& operator=(PublicationRecoveryResult&&) noexcept = default;
  ZC_DISALLOW_COPY(PublicationRecoveryResult);
  ~PublicationRecoveryResult() noexcept = default;

  ZC_NODISCARD static PublicationRecoveryResult clean();
  ZC_NODISCARD static PublicationRecoveryResult published();
  ZC_NODISCARD static PublicationRecoveryResult recoveryRequired(
      PublicationRecoveryObligation&& obligation);
  ZC_NODISCARD static PublicationRecoveryResult explicitRepairRequired();

  ZC_NODISCARD PublicationRecoveryStatus status() const noexcept { return statusValue; }
  ZC_NODISCARD bool hasObligation() const noexcept { return obligationValue != zc::none; }
  ZC_NODISCARD PublicationRecoveryObligation takeObligation() &&;

private:
  PublicationRecoveryResult(PublicationRecoveryStatus status,
                            zc::Maybe<PublicationRecoveryObligation>&& obligation) noexcept
      : statusValue(status), obligationValue(zc::mv(obligation)) {}

  PublicationRecoveryStatus statusValue;
  zc::Maybe<PublicationRecoveryObligation> obligationValue;
};

/// \brief Consumes a D4 candidate and publishes it with a durable manifest-last
///        journal transaction. Existing final entries are never replaced.
///
/// The operation re-derives the candidate's exact identity, byte count, digest,
/// regular-file shape, and sole-link proof from its same held output handle;
/// verifies the manifest is live-bound to the moved-in plan; durably commits the
/// immutable Started -> ManifestStaged -> ExecCommitted -> ManifestCommitted
/// hash chain; commits the executable and manifest with exclusive no-replace
/// renames; and releases the journal only after the residual manifest temporary
/// and transaction root are clean. No filesystem exception escapes.
ZC_NODISCARD PublicationOutcome publishLinkedOutput(LinkedOutputCandidate candidate,
                                                    VerifiedExecutableManifest manifest);

/// \brief Explicitly scans and recovers the journal chain for one final target.
///
/// A complete, verifying manifest is treated as published. An owner-proved
/// orphan is removed only after exact identity checks. A malformed/forked chain,
/// identity mismatch, or unrecognised state is retained and reported for
/// explicit repair. A pre-Started `.zomlink-<token>` root is never inferred from
/// its name and is therefore outside automatic recovery.
ZC_NODISCARD PublicationRecoveryResult
recoverLinkedOutputPublication(const zc::Filesystem& filesystem, zc::StringPtr finalDestination);

}  // namespace zomlang::compiler::ir
