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

// RFC 0043 D1 internal surface. This header is shared only by the linker
// candidate implementation and the publication transaction. It exposes the
// minimum capability operations publication needs without making the transaction
// root or its descriptors part of the public linker API.

#pragma once

#include <cstdint>

#include "compiler/identity/crypto/sha256.h"
#include "compiler/ir/invoke-linker.h"
#include "zc/core/common.h"
#include "zc/core/filesystem.h"
#include "zc/core/string.h"

namespace zomlang::compiler::ir::detail {

/// \brief The two fixed publication-recovery claim slot names inside a
///        transaction root.
///
/// A recovery worker renames a public final entry into one of these slots under
/// the held root capability and removes it only after proving the slot's exact
/// owner identity, digest, and (for the executable) byte count. Because the slot
/// name is fixed rather than derived from the transaction contents, any generic
/// content sweep of a transaction root MUST skip both names: a concurrent
/// protocol-following recovery may have just claimed a competitor into the slot
/// and not yet proved ownership, and deleting it would destroy the evidence that
/// recovery is mid-way through verifying. Only the call that itself proved a
/// slot's owner removes that slot; a slot left present forces the final
/// directory removal to fail on a non-empty root, which is reported as a
/// retriable cleanup debt. This follows the existing unified-root trust
/// boundary (a private root plus a trusted same-UID principal); it is not a
/// claim of protection against a hostile same-UID owner.
inline constexpr zc::StringPtr kExecutableQuarantineSlot = "publication-executable-cleanup"_zc;
inline constexpr zc::StringPtr kManifestQuarantineSlot = "publication-manifest-cleanup"_zc;

struct PublicationFileSnapshot final {
  StableFileIdentity identity;
  uint64_t byteCount;
  identity::Sha256Digest digest;
};

enum class PublicationRenameResult : uint8_t {
  Renamed = 0x01,
  DestinationExists = 0x02,
  Unsupported = 0x03,
  Failed = 0x04,
};

enum class PublicationClaimResult : uint8_t {
  ClaimedOwned = 0x01,
  SourceAbsent = 0x02,
  CompetitorRestored = 0x03,
  CompetitorRetained = 0x04,
  QuarantineExists = 0x05,
  Unsupported = 0x06,
  Failed = 0x07,
};

/// \brief Restricted attorney for the D1 publication transaction.
///
/// Definitions live with `LinkedOutputCandidate::Impl`, so publication can use
/// the candidate's held output and directory capabilities without exposing them
/// to ordinary callers or reopening the output by path.
class LinkPublicationTransaction final {
public:
  ZC_NODISCARD static StableFileIdentity fileIdentity(uint64_t device, uint64_t inode,
                                                      uint64_t linkCount) noexcept;
  ZC_NODISCARD static zc::Maybe<SnapshotTransactionId> transactionIdFromBytes(
      zc::ArrayPtr<const uint8_t> bytes);
  ZC_NODISCARD static StableDirectoryIdentity directoryIdentity(uint64_t device,
                                                                uint64_t inode) noexcept;
  ZC_NODISCARD static SnapshotCleanupObligation snapshotObligation(
      const SnapshotTransactionId& transactionId, zc::String&& outputParent,
      const StableDirectoryIdentity& directoryIdentity, const LinkPlanId& planId,
      CleanupFailureKind kind) noexcept;
  ZC_NODISCARD static const SnapshotTransactionId& transactionId(
      const LinkedOutputCandidate& candidate) noexcept;
  ZC_NODISCARD static zc::StringPtr outputParent(const LinkedOutputCandidate& candidate) noexcept;
  ZC_NODISCARD static const StableDirectoryIdentity& rootIdentity(
      const LinkedOutputCandidate& candidate) noexcept;
  ZC_NODISCARD static const zc::Directory& outputParentDirectory(
      const LinkedOutputCandidate& candidate) noexcept;
  /// \brief Re-derives regular-file shape, sole-link identity, byte count, and
  ///        digest from the candidate's same held read-only output handle.
  ZC_NODISCARD static zc::Maybe<PublicationFileSnapshot> recheckOutput(
      const LinkedOutputCandidate& candidate) noexcept;

  /// \brief Moves `<root>/output-candidate` to the final leaf with Linux
  ///        `renameat2(RENAME_NOREPLACE)`. No fallback is permitted.
  ZC_NODISCARD static PublicationRenameResult renameOutputNoReplace(
      const LinkedOutputCandidate& candidate, zc::StringPtr finalLeaf) noexcept;

  /// \brief Atomically claims one output-parent entry into the transaction root,
  ///        verifies the claimed inode, and restores a competitor when possible.
  ZC_NODISCARD static PublicationClaimResult claimParentEntryNoReplace(
      const LinkedOutputCandidate& candidate, zc::StringPtr parentLeaf,
      zc::StringPtr quarantineLeaf, const PublicationFileSnapshot& expected) noexcept;

  /// \brief Consumes the candidate without deleting a root that may contain an
  ///        unowned quarantined entry, returning its explicit cleanup debt.
  ZC_NODISCARD static SnapshotCleanupObligation abandonRootForRecovery(
      LinkedOutputCandidate& candidate, CleanupFailureKind kind) noexcept;
};

}  // namespace zomlang::compiler::ir::detail
