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

#include "zc/core/memory.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ownership/facts/paths.h"

namespace zomlang::compiler::ownership::facts {

/// \brief Canonical identity of one current-subset ownership-resource generation.
struct DropResourceSubject final {
  MirEventKey introduction;
  MovePathKey origin;
  identity::SemanticTypeId originType;

  ZC_NODISCARD DropResourceSubject clone() const {
    return DropResourceSubject{introduction, MovePathKey{origin.owner, origin.place.clone()},
                               originType};
  }
};

/// \brief One checker-authorized logical resource component rooted at an initialization event.
enum class DropRequirement : uint8_t {
  Logical = 0x01,
  Linear = 0x02,
  LinearLogical = 0x03,
};

struct OwnershipResourceFact final {
  DropResourceSubject subject;
  DropRequirement requirement;
  zc::Maybe<LogicalDropAction> dropAction;
  uint32_t declarationOrdinal;
};

/// \brief One ownership-preserving move of a current-subset resource component.
struct DropTransfer final {
  MovePathKey from;
  MovePathKey to;
  MirEventKey event;
};

/// \brief One type-changing cast that preserves a resource subject across the cast.
///
/// A checked cast (transmute) reinterprets a resource from one type to another
/// without changing its ownership. The subject's introduction event, origin
/// move path, and origin type are preserved unchanged; the route records the
/// exact source and destination move paths and the cast event so the verifier
/// can independently reconstruct the subject-preservation relation.
struct CastResourceRoute final {
  DropResourceSubject subject;
  MovePathKey from;
  MovePathKey to;
  MirEventKey event;
};

/// \brief Canonical identity of one linear obligation.
///
/// A Positive Linear marker decision creates one obligation at each
/// initialization. The key pairs the obligation's introduction event with its
/// origin move path.
struct LinearObligationKey final {
  MirEventKey introduction;
  MovePathKey place;

  ZC_NODISCARD LinearObligationKey clone() const {
    return LinearObligationKey{introduction, MovePathKey{place.owner, place.place.clone()}};
  }
};

/// \brief Canonical identity of one linear carrier.
///
/// A carrier names one static generation of an obligation's value. The root
/// carrier has creation == obligation.introduction and place ==
/// obligation.place. A transferred carrier is keyed by the transfer event and
/// destination place.
struct LinearCarrierKey final {
  LinearObligationKey obligation;
  MirEventKey creation;
  MovePathKey place;

  ZC_NODISCARD LinearCarrierKey clone() const {
    return LinearCarrierKey{obligation.clone(), creation,
                            MovePathKey{place.owner, place.place.clone()}};
  }
};

/// \brief One ownership-preserving transfer of a linear obligation.
struct LinearTransfer final {
  MovePathKey from;
  MovePathKey to;
  MirEventKey event;

  ZC_NODISCARD LinearTransfer clone() const {
    return LinearTransfer{MovePathKey{from.owner, from.place.clone()},
                          MovePathKey{to.owner, to.place.clone()}, event};
  }
};

/// \brief Closed consumption kind algebra for one linear obligation.
enum class LinearConsumptionKind : uint8_t {
  Return = 0x01,
  ConsumingCall = 0x02,
  LogicalDrop = 0x03,
};

/// \brief One consumption of a linear obligation.
struct LinearConsumption final {
  MovePathKey place;
  MirEventKey event;
  LinearConsumptionKind kind;

  ZC_NODISCARD LinearConsumption clone() const {
    return LinearConsumption{MovePathKey{place.owner, place.place.clone()}, event, kind};
  }
};

/// \brief One verified linear obligation in the resource inventory.
///
/// The transfers sequence contains exactly the static transfers reached by the
/// obligation; the consumptions sequence contains exactly the consuming events
/// reached by the obligation.
struct LinearObligationFact final {
  LinearObligationKey key;
  identity::SemanticTypeId subject;
  zc::Vector<LinearTransfer> transfers;
  zc::Vector<LinearConsumption> consumptions;

  ZC_NODISCARD LinearObligationFact clone() const {
    zc::Vector<LinearTransfer> clonedTransfers;
    for (const auto& transfer : transfers) clonedTransfers.add(transfer.clone());
    zc::Vector<LinearConsumption> clonedConsumptions;
    for (const auto& consumption : consumptions) clonedConsumptions.add(consumption.clone());
    return LinearObligationFact{key.clone(), subject, zc::mv(clonedTransfers),
                                zc::mv(clonedConsumptions)};
  }
};

/// \brief One transition between two linear carriers.
///
/// The predecessor names the carrier that reached the transfer event; the
/// transfer records the exact source, destination, and event.
struct LinearCarrierTransition final {
  LinearCarrierKey predecessor;
  LinearTransfer transfer;

  ZC_NODISCARD LinearCarrierTransition clone() const {
    return LinearCarrierTransition{predecessor.clone(), transfer.clone()};
  }
};

/// \brief One verified linear carrier in the resource inventory.
///
/// A root carrier has an empty incoming sequence. A transferred carrier has
/// one incoming transition for every reaching predecessor alternative at the
/// transfer event; distinct predecessors that converge at the same destination
/// produce one carrier with multiple incoming transitions.
struct LinearCarrierFact final {
  LinearCarrierKey key;
  zc::Vector<LinearCarrierTransition> incoming;

  ZC_NODISCARD LinearCarrierFact clone() const {
    zc::Vector<LinearCarrierTransition> clonedIncoming;
    for (const auto& transition : incoming) clonedIncoming.add(transition.clone());
    return LinearCarrierFact{key.clone(), zc::mv(clonedIncoming)};
  }
};

/// \brief One strongly connected component of mutually-reachable linear carriers.
///
/// The carrier relation may contain SCCs when a value is transferred around a
/// loop. Every carrier is reachable from the obligation's unique root, and no
/// transition may target the root. With the current straight-line MIR (no loop
/// terminators), every SCC is a singleton, but the computation is exact for
/// future loop support.
struct LinearCarrierScc final {
  zc::Vector<LinearCarrierKey> carriers;

  ZC_NODISCARD LinearCarrierScc clone() const {
    zc::Vector<LinearCarrierKey> clonedCarriers;
    for (const auto& carrier : carriers) clonedCarriers.add(carrier.clone());
    return LinearCarrierScc{zc::mv(clonedCarriers)};
  }
};

/// \brief Closed/open acceptance mode for one component drop plan.
///
/// A Closed drop accepts exactly Initialized and executes every component. An
/// Open drop additionally accepts Uninitialized and MaybeInitialized, executing
/// only each initialized alternative's components. Both modes produce
/// Uninitialized on every alternative.
enum class DropPlanMode : uint8_t {
  Closed = 0x01,
  Open = 0x02,
};

/// \brief One ordered component step in an open/closed drop plan.
///
/// Pre-consumption removes the component's pending drop obligation and linked
/// linear obligation immediately before the optional action. A present action
/// is abort-only: normal return continues to the next component, while panic
/// enters the terminal RFC 0006 abort path with no ownership, cleanup, or
/// unwind successor. Remaining components do not run after abort.
struct DropPlanComponent final {
  uint32_t factOrdinal;
  zc::Maybe<LogicalDropAction> action;
};

/// \brief Complete open/closed component drop plan for one resource subject.
///
/// Components are ordered in reverse declaration order (children before parent)
/// so each child is pre-consumed before its parent's action. The plan exists
/// only after every component returns normally; no partial discharge is
/// published for an aborting execution.
struct DropPlan final {
  DropResourceSubject subject;
  DropPlanMode mode;
  zc::Vector<DropPlanComponent> components;
};

/// \brief Complete logical resource inventory for one current-subset MIR function.
struct OwnershipResourceFunction final {
  identity::DefId owner;
  zc::Vector<OwnershipResourceFact> facts;
  zc::Vector<DropTransfer> transfers;
  zc::Vector<CastResourceRoute> castRoutes;
  zc::Vector<DropPlan> dropPlans;
  zc::Vector<LinearObligationFact> linearObligations;
  zc::Vector<LinearCarrierFact> linearCarriers;
  zc::Vector<LinearCarrierScc> linearSccs;
};

/// \brief Untrusted logical resource inventory awaiting independent reconstruction.
class OwnershipResourceCandidate final {
public:
  OwnershipResourceCandidate(identity::SemanticContextBrand semanticContext,
                             identity::ContextFingerprint&& contextFingerprint,
                             identity::ModuleId module, mir::MirRevisionId builtRevision,
                             OwnershipEventOverlayRevision overlayRevision,
                             zc::Vector<OwnershipResourceFunction>&& functions) noexcept;
  OwnershipResourceCandidate(OwnershipResourceCandidate&&) noexcept = default;
  OwnershipResourceCandidate& operator=(OwnershipResourceCandidate&&) noexcept = delete;
  ZC_DISALLOW_COPY(OwnershipResourceCandidate);

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  zc::Vector<OwnershipResourceFunction> functions;
};

/// \brief Immutable logical resource facts bound to one Built MIR and event overlay.
class VerifiedOwnershipResourceFacts final {
public:
  ~VerifiedOwnershipResourceFacts() noexcept(false);
  VerifiedOwnershipResourceFacts(VerifiedOwnershipResourceFacts&&) noexcept;
  VerifiedOwnershipResourceFacts& operator=(VerifiedOwnershipResourceFacts&&) noexcept;
  ZC_DISALLOW_COPY(VerifiedOwnershipResourceFacts);

  ZC_NODISCARD identity::SemanticContextBrand semanticContext() const noexcept;
  ZC_NODISCARD const identity::ContextFingerprint& contextFingerprint() const noexcept;
  ZC_NODISCARD identity::ModuleId module() const noexcept;
  ZC_NODISCARD const mir::MirRevisionId& builtRevision() const noexcept;
  ZC_NODISCARD const OwnershipEventOverlayRevision& overlayRevision() const noexcept;
  ZC_NODISCARD zc::ArrayPtr<const OwnershipResourceFunction> functions() const noexcept;

private:
  struct Impl;
  explicit VerifiedOwnershipResourceFacts(zc::Own<Impl>&& impl) noexcept;
  zc::Own<Impl> impl;

  friend class OwnershipResourceVerifier;
};

/// \brief Projects checker-authorized logical resources into immutable ownership inputs.
class OwnershipResourceBuilder final {
public:
  ZC_NODISCARD static ir::IrOperationResult<OwnershipResourceCandidate> build(
      const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
      const VerifiedOwnershipEventOverlay& overlay);
};

/// \brief Independently reconstructs logical resources from verified overlay and MIR inputs.
class OwnershipResourceVerifier final {
public:
  ZC_NODISCARD static ir::IrOperationResult<VerifiedOwnershipResourceFacts> verify(
      OwnershipResourceCandidate&& candidate, const VerifiedMovePaths& movePaths,
      const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay);
};

}  // namespace zomlang::compiler::ownership::facts
