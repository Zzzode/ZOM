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

namespace zomlang::compiler::ir::detail {

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
  ZC_NODISCARD static zc::StringPtr outputParent(
      const LinkedOutputCandidate& candidate) noexcept;
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
};

}  // namespace zomlang::compiler::ir::detail
