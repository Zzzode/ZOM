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

#include "compiler/ir/executable-publication.h"

namespace zomlang::compiler::ir {

enum class PublicationCheckpoint : uint8_t {
  StartedDurable = 0x01,
  ManifestTemporaryDurable = 0x02,
  ManifestStagedDurable = 0x03,
  ExecutableRenamed = 0x04,
  ExecutableDirectoryDurable = 0x05,
  ExecCommittedDurable = 0x06,
  ManifestRenamed = 0x07,
  ManifestDirectoryDurable = 0x08,
  ManifestCommittedDurable = 0x09,
  ManifestRenameRejected = 0x0a,
  // Recovery-time seam: a public final entry has been renamed into the
  // transaction root's quarantine slot but its owner proof (snapshot identity +
  // digest) has not yet been verified. A concurrent recovery must not observe a
  // claimed-but-unverified competitor as removable.
  ExecutableClaimedBeforeVerification = 0x0b,
  // Recovery-time seam fired just before the public entry is renamed into the
  // quarantine slot. Lets a test pin one worker's claim decision (source still
  // present, so claimExecutable is true) before a competing worker moves it.
  ExecutableClaimAboutToRename = 0x0c,
  // Cleanup-time seam fired after a transaction root has been top-level-claimed
  // and its identity re-verified, and after this call has removed the fixed
  // claim slots it itself proved, but before the remaining contents are
  // enumerated and swept. Lets a test drive a concurrent worker holding a
  // pre-existing root descriptor into this window to late-claim a fixed slot and
  // prove the generic sweep never removes it.
  TransactionRootContentsAboutToSweep = 0x0d,
};

enum class PublicationFaultPoint : uint8_t {
  JournalWrite = 0x01,
  JournalFileSync = 0x02,
  JournalInstall = 0x03,
  JournalDirectorySync = 0x04,
  JournalTemporaryCleanup = 0x05,
  ManifestWrite = 0x06,
  ManifestFileSync = 0x07,
  ManifestDirectorySync = 0x08,
  ExecutableDirectorySync = 0x09,
  FinalManifestDirectorySync = 0x0a,
  JournalChainRemove = 0x0b,
  JournalChainSync = 0x0c,
};

class PublicationCheckpointObserver {
public:
  virtual ~PublicationCheckpointObserver() noexcept = default;
  virtual void reached(PublicationCheckpoint checkpoint) = 0;
  virtual bool fail(PublicationFaultPoint) { return false; }
};

struct PublicationTransactionTestAccess final {
  ZC_NODISCARD static PublicationOutcome publishObserved(LinkedOutputCandidate candidate,
                                                         VerifiedExecutableManifest manifest,
                                                         PublicationCheckpointObserver& observer);
  ZC_NODISCARD static PublicationRecoveryResult recoverObserved(
      const zc::Filesystem& filesystem, zc::StringPtr finalDestination,
      PublicationCheckpointObserver& observer);
};

/// \brief Internal D1 transaction consumed only by the D5 publication boundary.
///
/// The operation re-derives the candidate's exact identity, byte count, digest,
/// regular-file shape, and sole-link proof from its same held output handle;
/// verifies the manifest is live-bound to the moved-in plan; durably commits the
/// immutable Started -> ManifestStaged -> ExecCommitted -> ManifestCommitted
/// hash chain; commits the executable and manifest with exclusive no-replace
/// renames; and releases the journal only after the residual manifest temporary
/// and transaction root are clean. It is deliberately absent from the public IR
/// API so no caller can bypass D5 executable inspection and mint a published
/// artifact directly.
ZC_NODISCARD PublicationOutcome publishLinkedOutput(LinkedOutputCandidate candidate,
                                                    VerifiedExecutableManifest manifest);

}  // namespace zomlang::compiler::ir
