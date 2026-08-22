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

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zomlang/compiler/checker/checker-identity-authority.h"
#include "zomlang/compiler/identity/crypto/sha256.h"
#include "zomlang/compiler/ownership/facts/inputs.h"
#include "zomlang/compiler/ownership/facts/ownership-facts-revision.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"
#include "zomlang/compiler/type/semantic-type-store.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Exact canonical ownership facts framing codec.
///
/// The codec serializes one complete verified ownership facts snapshot into a
/// domain-separated byte sequence covering thirteen groups: the eight facts
/// inventories (move paths, flow, initialization, loans, reference definitions,
/// reborrow regions, reborrow states, ownership resources), the four overlay-derived
/// inventories (logical drop plans, unsafe occurrences, cast resource plans, marker
/// decisions), and the metadata group (semantic context brand and lineage revisions).
/// The ownership resources group (group 8) encodes nine subsequences: facts,
/// transfers, cast routes, drop plans, linear obligations, linear carriers,
/// linear SCCs, raw-origin universe, and raw provenance.
/// The module identity is bound through the expanded module key in the frame header.
class OwnershipFactsCodec final {
public:
  /// \brief Encodes the framed `zom.ownership-facts` stream from pre-encoded groups.
  /// \param contextFingerprint Context fingerprint digest shared with the Built MIR.
  /// \param expandedModuleKey Canonical expanded module identity bytes.
  /// \param canonicalGroups Exactly thirteen non-empty canonical group records.
  /// \return The framed bytes, or none when the module key is empty or a group is empty.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encodeFramed(
      const identity::Sha256Digest& contextFingerprint,
      zc::ArrayPtr<const uint8_t> expandedModuleKey,
      zc::ArrayPtr<const zc::Array<uint8_t>> canonicalGroups);

  /// \brief Encodes one complete verified ownership facts snapshot into canonical bytes.
  /// \param inputs The eight verified facts inventories and their lineage.
  /// \param overlay The verified event overlay supplying the drop, unsafe, cast, and marker groups.
  /// \param identities Retained checker identity authority for definition and module key expansion.
  /// \param semanticTypes Live semantic type store for canonical type key resolution.
  /// \return The canonical bytes, or none when any identity or type lookup fails.
  ZC_NODISCARD static zc::Maybe<zc::Array<uint8_t>> encode(
      const VerifiedOwnershipInputs& inputs, const VerifiedOwnershipEventOverlay& overlay,
      const checker::CheckerIdentityAuthority& identities,
      const type::SemanticTypeStore& semanticTypes);

  /// \brief Computes the domain-separated facts revision for one verified snapshot.
  /// \return The revision, or none when encoding fails.
  ZC_NODISCARD static zc::Maybe<OwnershipFactsRevision> compute(
      const VerifiedOwnershipInputs& inputs, const VerifiedOwnershipEventOverlay& overlay,
      const checker::CheckerIdentityAuthority& identities,
      const type::SemanticTypeStore& semanticTypes);
};

}  // namespace zomlang::compiler::ownership::facts
