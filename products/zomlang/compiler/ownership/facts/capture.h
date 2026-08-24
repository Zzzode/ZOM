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
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/facts/region-key.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership::facts {

// ---------------------------------------------------------------------------
// CaptureFact
// ---------------------------------------------------------------------------

/// \brief One capture boundary: one value captured by one closure construction.
///
/// `construction` is the closure construction event. `closure` is the move path
/// of the constructed closure value and `captured` is the move path of the
/// captured value. `closureRegion` is the ClosureValueRegion introduced by the
/// construction event and `capturedRegion` is the region of the captured value
/// at the capture point. The boundary records that the captured value's region
/// must outlive the closure value's region.
struct CaptureFact final {
  MirEventKey construction;
  MovePathKey closure;
  MovePathKey captured;
  RegionKey closureRegion;
  RegionKey capturedRegion;
};

/// \brief Untrusted capture inventory awaiting independent reconstruction.
class CaptureCandidate final {
public:
  CaptureCandidate(identity::SemanticContextBrand semanticContext,
                   identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
                   mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
                   driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                   zc::Vector<CaptureFact>&& captures) noexcept;
  CaptureCandidate(CaptureCandidate&&) noexcept = default;
  CaptureCandidate& operator=(CaptureCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(CaptureCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<CaptureFact> captures;
};

/// \brief Immutable capture inventory bound to one verified ownership input snapshot.
class VerifiedCaptureFacts final {
public:
  ~VerifiedCaptureFacts() noexcept(false);
  VerifiedCaptureFacts(VerifiedCaptureFacts&&) noexcept;
  VerifiedCaptureFacts& operator=(VerifiedCaptureFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedCaptureFacts);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const CaptureFact> captures() const noexcept;

private:
  struct Impl;
  explicit VerifiedCaptureFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class CaptureVerifier;
};

/// \brief Derives capture facts for the admitted closure subset.
///
/// Closures are not yet admitted by surface admission, so the builder derives
/// an empty inventory for every input snapshot. The derivation activates here
/// once closure construction reaches MIR.
class CaptureBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<CaptureCandidate> build(
      const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs the capture inventory.
///
/// The verifier independently confirms that the admitted subset admits no
/// captures and rejects any non-empty candidate as an invalid ownership proof.
class CaptureVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedCaptureFacts> verify(
      CaptureCandidate&& candidate, const VerifiedMovePaths& movePaths,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
