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
#include "zomlang/compiler/ownership/facts/capture.h"
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/points.h"
#include "zomlang/compiler/ownership/facts/raw-provenance.h"
#include "zomlang/compiler/ownership/facts/region-key.h"
#include "zomlang/compiler/ownership/facts/region-membership.h"
#include "zomlang/compiler/ownership/facts/resources.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership::facts {

// ---------------------------------------------------------------------------
// EscapeKind
// ---------------------------------------------------------------------------

/// \brief Return escape: the reference escapes through the function return.
struct EscapeReturn final {};

/// \brief Store escape: the reference escapes into a destination at a region.
struct EscapeStore final {
  MovePathKey destination;
  RegionKey destinationRegion;
};

/// \brief Closure capture escape: the reference is captured by a closure.
struct EscapeClosureCapture final {
  MovePathKey closure;
  RegionKey closureRegion;
};

/// \brief Closed kind algebra for one escape.
///
/// Tags are `0x01` through `0x03` in declaration order (Return, Store,
/// ClosureCapture).
class EscapeKind final {
public:
  ZC_NODISCARD static EscapeKind returnEscape() noexcept { return EscapeKind(EscapeReturn{}); }
  ZC_NODISCARD static EscapeKind storeEscape(MovePathKey destination, RegionKey destinationRegion) {
    return EscapeKind(EscapeStore{zc::mv(destination), zc::mv(destinationRegion)});
  }
  ZC_NODISCARD static EscapeKind closureCaptureEscape(MovePathKey closure,
                                                      RegionKey closureRegion) {
    return EscapeKind(EscapeClosureCapture{zc::mv(closure), zc::mv(closureRegion)});
  }

  ZC_NODISCARD bool isReturn() const noexcept { return value.is<EscapeReturn>(); }
  ZC_NODISCARD bool isStore() const noexcept { return value.is<EscapeStore>(); }
  ZC_NODISCARD bool isClosureCapture() const noexcept { return value.is<EscapeClosureCapture>(); }

  ZC_NODISCARD const EscapeStore& storeValue() const { return value.get<EscapeStore>(); }
  ZC_NODISCARD const EscapeClosureCapture& closureCaptureValue() const {
    return value.get<EscapeClosureCapture>();
  }

  /// \brief Returns the canonical variant tag (0x01-0x03).
  ZC_NODISCARD uint8_t tag() const noexcept {
    if (isReturn()) return 0x01;
    if (isStore()) return 0x02;
    return 0x03;
  }

  /// \brief Returns a deep copy of this escape kind.
  ZC_NODISCARD EscapeKind clone() const {
    if (isReturn()) return EscapeKind(EscapeReturn{});
    if (isStore()) {
      const auto& store = storeValue();
      return EscapeKind(
          EscapeStore{MovePathKey{store.destination.owner, store.destination.place.clone()},
                      store.destinationRegion.clone()});
    }
    const auto& closure = closureCaptureValue();
    return EscapeKind(
        EscapeClosureCapture{MovePathKey{closure.closure.owner, closure.closure.place.clone()},
                             closure.closureRegion.clone()});
  }

  bool operator==(const EscapeKind& other) const noexcept {
    if (tag() != other.tag()) return false;
    if (isReturn()) return true;
    if (isStore()) {
      const auto& left = storeValue();
      const auto& right = other.storeValue();
      return !lessMovePathKey(left.destination, right.destination) &&
             !lessMovePathKey(right.destination, left.destination) &&
             left.destinationRegion == right.destinationRegion;
    }
    const auto& left = closureCaptureValue();
    const auto& right = other.closureCaptureValue();
    return !lessMovePathKey(left.closure, right.closure) &&
           !lessMovePathKey(right.closure, left.closure) &&
           left.closureRegion == right.closureRegion;
  }
  bool operator!=(const EscapeKind& other) const noexcept { return !(*this == other); }

private:
  explicit EscapeKind(zc::OneOf<EscapeReturn, EscapeStore, EscapeClosureCapture> v) noexcept
      : value(zc::mv(v)) {}

  zc::OneOf<EscapeReturn, EscapeStore, EscapeClosureCapture> value;
};

// ---------------------------------------------------------------------------
// EscapeOriginRoute
// ---------------------------------------------------------------------------

/// \brief Direct escape route: the reference escapes without a raw carrier.
struct EscapeDirectRoute final {};

/// \brief Raw-carrier escape route: the reference escapes through a raw carrier.
struct EscapeRawCarrierRoute final {
  RawProvenanceCarrierKey carrier;
};

/// \brief Route through which one reference origin reaches an escape.
///
/// Tags are `Direct = 0x01` and `RawCarrier = 0x02`.
class EscapeOriginRoute final {
public:
  ZC_NODISCARD static EscapeOriginRoute direct() noexcept {
    return EscapeOriginRoute(EscapeDirectRoute{});
  }
  ZC_NODISCARD static EscapeOriginRoute rawCarrier(RawProvenanceCarrierKey carrier) {
    return EscapeOriginRoute(EscapeRawCarrierRoute{zc::mv(carrier)});
  }

  ZC_NODISCARD bool isDirect() const noexcept { return value.is<EscapeDirectRoute>(); }
  ZC_NODISCARD bool isRawCarrier() const noexcept { return value.is<EscapeRawCarrierRoute>(); }
  ZC_NODISCARD const EscapeRawCarrierRoute& rawCarrierValue() const {
    return value.get<EscapeRawCarrierRoute>();
  }

  /// \brief Returns the canonical variant tag (0x01-0x02).
  ZC_NODISCARD uint8_t tag() const noexcept { return isDirect() ? 0x01 : 0x02; }

  /// \brief Returns a deep copy of this escape origin route.
  ZC_NODISCARD EscapeOriginRoute clone() const {
    if (isDirect()) return EscapeOriginRoute(EscapeDirectRoute{});
    const auto& carrier = rawCarrierValue();
    return EscapeOriginRoute(EscapeRawCarrierRoute{RawProvenanceCarrierKey{
        carrier.carrier.introduction, MovePathKey{carrier.carrier.destination.owner,
                                                  carrier.carrier.destination.place.clone()}}});
  }

  bool operator==(const EscapeOriginRoute& other) const noexcept {
    if (tag() != other.tag()) return false;
    if (isDirect()) return true;
    const auto& left = rawCarrierValue();
    const auto& right = other.rawCarrierValue();
    return left.carrier.introduction == right.carrier.introduction &&
           !lessMovePathKey(left.carrier.destination, right.carrier.destination) &&
           !lessMovePathKey(right.carrier.destination, left.carrier.destination);
  }
  bool operator!=(const EscapeOriginRoute& other) const noexcept { return !(*this == other); }

private:
  explicit EscapeOriginRoute(zc::OneOf<EscapeDirectRoute, EscapeRawCarrierRoute> v) noexcept
      : value(zc::mv(v)) {}

  zc::OneOf<EscapeDirectRoute, EscapeRawCarrierRoute> value;
};

// ---------------------------------------------------------------------------
// EscapeOriginCause
// ---------------------------------------------------------------------------

/// \brief One reference origin and the route through which it reaches an escape.
///
/// `origins` is the complete multi-origin relation. It contains one `Direct`
/// row for every direct reference origin and one `RawCarrier` row for every
/// `Reference` origin in every reaching raw carrier's transitive provenance.
struct EscapeOriginCause final {
  ReferenceOrigin origin;
  EscapeOriginRoute route;

  ZC_NODISCARD EscapeOriginCause clone() const {
    return EscapeOriginCause{origin.clone(), route.clone()};
  }

  bool operator==(const EscapeOriginCause& other) const noexcept {
    return origin == other.origin && route == other.route;
  }
  bool operator!=(const EscapeOriginCause& other) const noexcept { return !(*this == other); }
};

// ---------------------------------------------------------------------------
// EscapeProof
// ---------------------------------------------------------------------------

/// \brief Owned escape proof: the escaping value is owned.
struct EscapeOwnedProof final {};

/// \brief Static escape proof: the escaping value is a static reference.
struct EscapeStaticProof final {};

/// \brief Direct-input escape proof: the escaping value is a direct borrow input.
struct EscapeDirectInputProof final {
  BorrowInputKey input;
};

/// \brief Contained escape proof: the escaping value is contained at required points.
struct EscapeContainedProof final {
  zc::Vector<OwnershipPoint> requiredPoints;
};

/// \brief Address-only escape proof: the escaping value is only an address.
struct EscapeAddressOnlyProof final {};

/// \brief Proof that one escape is admissible.
///
/// Tags are `0x01` through `0x05` in declaration order (Owned, Static,
/// DirectInput, Contained, AddressOnly).
///
/// Proof requirements:
/// - `Owned` requires both origin and raw-carrier sequences empty.
/// - `Static` requires a non-empty origin sequence, every origin root region to
///   be `Static`, and every active region to contain `BeforeEvent(key)`.
/// - `DirectInput` is legal only for `Return`, requires a non-empty origin
///   sequence, every origin to name the receiver or parameter, and every active
///   region to contain `BeforeEvent(key)`.
/// - `Contained` requires a non-empty origin sequence, `requiredPoints` to equal
///   the destination or closure point set, and every origin active region to
///   contain that complete set.
/// - `AddressOnly` requires an empty origin sequence and non-empty raw carriers.
class EscapeProof final {
public:
  ZC_NODISCARD static EscapeProof owned() noexcept { return EscapeProof(EscapeOwnedProof{}); }
  ZC_NODISCARD static EscapeProof staticProof() noexcept {
    return EscapeProof(EscapeStaticProof{});
  }
  ZC_NODISCARD static EscapeProof directInput(BorrowInputKey input) {
    return EscapeProof(EscapeDirectInputProof{zc::mv(input)});
  }
  ZC_NODISCARD static EscapeProof contained(zc::Vector<OwnershipPoint> requiredPoints) {
    return EscapeProof(EscapeContainedProof{zc::mv(requiredPoints)});
  }
  ZC_NODISCARD static EscapeProof addressOnly() noexcept {
    return EscapeProof(EscapeAddressOnlyProof{});
  }

  ZC_NODISCARD bool isOwned() const noexcept { return value.is<EscapeOwnedProof>(); }
  ZC_NODISCARD bool isStatic() const noexcept { return value.is<EscapeStaticProof>(); }
  ZC_NODISCARD bool isDirectInput() const noexcept { return value.is<EscapeDirectInputProof>(); }
  ZC_NODISCARD bool isContained() const noexcept { return value.is<EscapeContainedProof>(); }
  ZC_NODISCARD bool isAddressOnly() const noexcept { return value.is<EscapeAddressOnlyProof>(); }

  ZC_NODISCARD const EscapeDirectInputProof& directInputValue() const {
    return value.get<EscapeDirectInputProof>();
  }
  ZC_NODISCARD const EscapeContainedProof& containedValue() const {
    return value.get<EscapeContainedProof>();
  }

  /// \brief Returns the canonical variant tag (0x01-0x05).
  ZC_NODISCARD uint8_t tag() const noexcept {
    if (isOwned()) return 0x01;
    if (isStatic()) return 0x02;
    if (isDirectInput()) return 0x03;
    if (isContained()) return 0x04;
    return 0x05;
  }

  bool operator==(const EscapeProof& other) const noexcept {
    if (tag() != other.tag()) return false;
    if (isOwned() || isStatic() || isAddressOnly()) return true;
    if (isDirectInput()) { return directInputValue().input == other.directInputValue().input; }
    // Contained: compare required points by size and element-wise equality.
    const auto& left = containedValue().requiredPoints;
    const auto& right = other.containedValue().requiredPoints;
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
      if (!(left[i] == right[i])) return false;
    }
    return true;
  }
  bool operator!=(const EscapeProof& other) const noexcept { return !(*this == other); }

private:
  explicit EscapeProof(zc::OneOf<EscapeOwnedProof, EscapeStaticProof, EscapeDirectInputProof,
                                 EscapeContainedProof, EscapeAddressOnlyProof>
                           v) noexcept
      : value(zc::mv(v)) {}

  zc::OneOf<EscapeOwnedProof, EscapeStaticProof, EscapeDirectInputProof, EscapeContainedProof,
            EscapeAddressOnlyProof>
      value;
};

// ---------------------------------------------------------------------------
// EscapeFact
// ---------------------------------------------------------------------------

/// \brief One complete verified escape fact for one escape operand.
///
/// Every successful escape operand has one complete proof record. The verifier
/// derives one row for every admissible escape operand and rejects a missing,
/// additional, wrong-kind, wrong-ordinal, missing-origin-route,
/// incomplete-point-set, foreign-carrier, or proof-incompatible row as
/// `InvalidOwnershipProof`.
struct EscapeFact final {
  MirEventKey key;
  MovePathKey source;
  EscapeKind kind;
  zc::Vector<EscapeOriginCause> origins;
  zc::Vector<RawProvenanceCarrierKey> rawCarriers;
  EscapeProof proof;
};

/// \brief Validates the Static proof requirements for one escape.
///
/// A Static proof requires a non-empty origin sequence, every origin root
/// region to be Static, and every origin active region to be live at
/// BeforeEvent(key) in the verified region-membership inventory. The admitted
/// subset admits no static references, so no produced fact carries a Static
/// proof; this predicate is the RFC 0013 validation gate for when static
/// references reach MIR.
ZC_NODISCARD bool staticEscapeProofAdmissible(zc::ArrayPtr<const EscapeOriginCause> origins,
                                              zc::ArrayPtr<const RegionMembership> memberships,
                                              const MirEventKey& key) noexcept;

/// \brief Validates the AddressOnly proof requirements for one escape.
///
/// An AddressOnly proof requires an empty origin sequence and a non-empty
/// raw-carrier sequence: the escaping value is an address carried through raw
/// provenance rather than a full reference. The admitted subset admits no raw
/// carriers, so no produced fact carries an AddressOnly proof; this predicate
/// is the RFC 0013 validation gate for when raw carriers reach MIR.
ZC_NODISCARD bool addressOnlyEscapeProofAdmissible(
    zc::ArrayPtr<const EscapeOriginCause> origins,
    zc::ArrayPtr<const RawProvenanceCarrierKey> rawCarriers) noexcept;

/// \brief Untrusted escape inventory awaiting independent reconstruction.
class EscapeCandidate final {
public:
  EscapeCandidate(identity::SemanticContextBrand semanticContext,
                  identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
                  mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
                  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                  zc::Vector<EscapeFact>&& escapes) noexcept;
  EscapeCandidate(EscapeCandidate&&) noexcept = default;
  EscapeCandidate& operator=(EscapeCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(EscapeCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision;
  zc::Vector<EscapeFact> escapes;
};

/// \brief Immutable escape inventory bound to one verified ownership input snapshot.
class VerifiedEscapeFacts final {
public:
  ~VerifiedEscapeFacts() noexcept(false);
  VerifiedEscapeFacts(VerifiedEscapeFacts&&) noexcept;
  VerifiedEscapeFacts& operator=(VerifiedEscapeFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedEscapeFacts);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD const driver::borrow_evidence::BorrowEvidenceRevision& borrowEvidenceRevision()
      const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const EscapeFact> escapes() const noexcept;

private:
  struct Impl;
  explicit VerifiedEscapeFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class EscapeVerifier;
};

/// \brief Derives escape facts for the admitted escape-operand subset.
///
/// Return escapes derive from verified reference definitions, store escapes
/// from reference stores in the event overlay (not yet admitted), and
/// closure-capture escapes from verified capture facts (empty until closures
/// reach MIR). The proof classifier additionally recognizes Static and
/// AddressOnly proofs as infrastructure for those admitted subsets.
class EscapeBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<EscapeCandidate> build(
      const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
      const VerifiedReferenceDefinitions& references,
      const VerifiedOwnershipResourceFacts& resources, const VerifiedCaptureFacts& captures,
      const VerifiedRegionMemberships& memberships, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs the escape inventory.
class EscapeVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedEscapeFacts> verify(
      EscapeCandidate&& candidate, const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
      const VerifiedReferenceDefinitions& references,
      const VerifiedOwnershipResourceFacts& resources, const VerifiedCaptureFacts& captures,
      const VerifiedRegionMemberships& memberships, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
