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

#include "compiler/ownership/facts/escape.h"

#include "compiler/ir/ir-failure.h"

namespace zomlang::compiler::ownership::facts {
namespace {

identity::IdentityInvariant invalidIdentity(identity::IdentityAllocationPhase phase,
                                            uint32_t ordinal) {
  zc::Maybe<zc::Array<uint8_t>> noKey;
  zc::Maybe<identity::UnbrandedSourceRange> noRange;
  auto invariant = identity::IdentityInvariant::from(
      identity::IdentityInvariantKind::InvalidHandle, phase, zc::mv(noKey), zc::mv(noRange),
      identity::IdentityApiSite::HandleLookup, ordinal);
  ZC_IF_SOME(value, invariant) { return zc::mv(value); }
  ZC_UNREACHABLE
}

/// \brief Identity resolver that rejects all expansion requests.
///
/// Escape verification does not need identity expansion; this resolver
/// satisfies the IrFailureFactory contract without retaining authority.
class AuthorityIdentityResolver final : public ir::IrFailureIdentityResolver {
public:
  explicit AuthorityIdentityResolver(const checker::CheckerIdentityAuthority& identities) noexcept
      : identities(identities) {}

  ir::ExpandedIrIdentityResult expand(identity::ModuleId module) const override {
    auto key = identities.module(module);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Module, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(identity::DefId definition) const override {
    auto key = identities.definition(definition);
    if (key == zc::none) {
      return ir::RejectedIrIdentityValue{
          invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
    }
    ZC_IF_SOME(value, key) {
      auto expanded = ir::ExpandedIrIdentity::from(value.key().encode());
      ZC_IF_SOME(bytes, expanded) { return ir::ExpandedIrIdentityValue{zc::mv(bytes)}; }
    }
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Encoding, 0)};
  }

  ir::ExpandedIrIdentityResult expand(ir::InstanceId) const override {
    return ir::RejectedIrIdentityValue{
        invalidIdentity(identity::IdentityAllocationPhase::Definition, 0)};
  }

private:
  const checker::CheckerIdentityAuthority& identities;
};

template <typename Result>
ir::IrOperationResult<Result> reject(const mir::VerifiedBuiltMir& builtMir,
                                     const checker::CheckerIdentityAuthority& identities,
                                     ir::IrFailureKind kind, uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Escape failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::OwnershipProofValidation, kind,
      ir::IrFailureOwner::definition(definition), zc::mv(noSite), ir::IrFailureDetail::none(),
      zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<Result>::identityInvariantRejected(zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<Result>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

/// \brief Checks that all verified inputs share the same lineage.
bool inputsMatch(const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
                 const VerifiedReferenceDefinitions& references,
                 const VerifiedOwnershipResourceFacts& resources,
                 const VerifiedCaptureFacts& captures, const VerifiedRegionMemberships& memberships,
                 const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return flow.semanticContext() == builtMir.semanticContext() &&
         flow.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         flow.module() == builtMir.module() &&
         flow.builtRevision().digest() == builtMir.revision().digest() &&
         flow.overlayRevision().digest() == overlay.revision().digest() &&
         loans.semanticContext() == builtMir.semanticContext() &&
         loans.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         loans.module() == builtMir.module() &&
         loans.builtRevision().digest() == builtMir.revision().digest() &&
         loans.overlayRevision().digest() == overlay.revision().digest() &&
         references.semanticContext() == builtMir.semanticContext() &&
         references.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         references.module() == builtMir.module() &&
         references.builtRevision().digest() == builtMir.revision().digest() &&
         references.overlayRevision().digest() == overlay.revision().digest() &&
         resources.semanticContext() == builtMir.semanticContext() &&
         resources.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         resources.module() == builtMir.module() &&
         resources.builtRevision().digest() == builtMir.revision().digest() &&
         resources.overlayRevision().digest() == overlay.revision().digest() &&
         captures.semanticContext() == builtMir.semanticContext() &&
         captures.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         captures.module() == builtMir.module() &&
         captures.builtRevision().digest() == builtMir.revision().digest() &&
         captures.overlayRevision().digest() == overlay.revision().digest() &&
         memberships.semanticContext() == builtMir.semanticContext() &&
         memberships.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         memberships.module() == builtMir.module() &&
         memberships.builtRevision().digest() == builtMir.revision().digest() &&
         memberships.overlayRevision().digest() == overlay.revision().digest();
}

bool sameProjection(const mir::MirProjection& left, const mir::MirProjection& right) {
  if (left.kind() != right.kind() || left.inputType() != right.inputType() ||
      left.resultType() != right.resultType()) {
    return false;
  }
  switch (left.kind()) {
    case mir::MirProjectionKind::Field:
      return left.fieldValue().field == right.fieldValue().field;
    case mir::MirProjectionKind::Index:
      return left.indexValue().index == right.indexValue().index;
    case mir::MirProjectionKind::Dereference:
      return true;
    case mir::MirProjectionKind::Downcast:
      return left.downcastValue().variant == right.downcastValue().variant;
    case mir::MirProjectionKind::Subslice:
      return left.subsliceValue().first == right.subsliceValue().first &&
             left.subsliceValue().pastLast == right.subsliceValue().pastLast;
  }
  return false;
}

bool samePlace(const mir::MirPlace& left, const mir::MirPlace& right) {
  if (left.local() != right.local() || left.rootType() != right.rootType() ||
      left.resultType() != right.resultType() ||
      left.projections().size() != right.projections().size()) {
    return false;
  }
  for (size_t index = 0; index < left.projections().size(); ++index) {
    if (!sameProjection(left.projections()[index], right.projections()[index])) return false;
  }
  return true;
}

/// \brief Orders two move paths by owner then place for equality checks.
bool sameMovePath(const MovePathKey& left, const MovePathKey& right) {
  return !lessMovePathKey(left, right) && !lessMovePathKey(right, left);
}

/// \brief Returns whether one event slot carries the Escape role.
bool hasEscapeRole(const MirEventSlot& slot) noexcept {
  for (const auto role : slot.roles) {
    if (role == OwnershipEventRole::Escape) return true;
  }
  return false;
}

/// \brief Builds the RFC 0007 reference origin for one returned reference.
///
/// A parameter reborrow roots at the function's input region for that
/// parameter; a local borrow roots at the loan region. Both origins are active
/// in their root region. The activation event is the loan activation recorded
/// by the verified reference definition.
zc::Maybe<ReferenceOrigin> referenceOrigin(const ReferenceDefinition& definition) {
  zc::Maybe<RegionKey> rootRegion;
  if (definition.origin.detail.is<ParameterReferenceOrigin>()) {
    const auto index = definition.origin.detail.get<ParameterReferenceOrigin>().rootParameter;
    rootRegion = RegionKey::inputRegion(definition.owner, BorrowInputKey::parameter(index));
  } else if (definition.origin.detail.is<LocalReferenceOrigin>()) {
    rootRegion = RegionKey::loanRegion(LoanKey{definition.loan});
  } else {
    return zc::none;
  }
  // The admitted subset activates every reference at an AfterEvent point.
  if (definition.origin.activation.kind() != OwnershipPointKind::AfterEvent) return zc::none;
  const auto& activation = definition.origin.activation.afterEventValue().event;
  ZC_IF_SOME(region, rootRegion) {
    return ReferenceOrigin{ReferenceRoot{region.clone(),
                                         MovePathKey{definition.origin.referent.owner,
                                                     definition.origin.referent.place.clone()},
                                         definition.origin.entry},
                           region.clone(), activation};
  }
  ZC_UNREACHABLE
}

/// \brief Builds the escape proof for one escaping reference.
///
/// A reference rooted at a Static region escapes as a static reference, gated
/// by the Static proof requirements. A parameter reborrow escapes through a
/// direct borrow input. A local borrow escapes only while its loan is live, so
/// the proof names the reference's complete live point set; the loan region
/// contains every required point. The admitted subset admits no static
/// references, so the Static branch is infrastructure for when static
/// variables reach MIR.
zc::Maybe<EscapeProof> escapeProof(const ReferenceDefinition& definition,
                                   const ReferenceOrigin& origin,
                                   zc::ArrayPtr<const RegionMembership> memberships) {
  if (origin.root.region.isStatic()) {
    zc::Vector<EscapeOriginCause> origins;
    origins.add(EscapeOriginCause{origin.clone(), EscapeOriginRoute::direct()});
    if (!staticEscapeProofAdmissible(origins.asPtr(), memberships, definition.returned)) {
      return zc::none;
    }
    return EscapeProof::staticProof();
  }
  if (definition.origin.detail.is<ParameterReferenceOrigin>()) {
    const auto index = definition.origin.detail.get<ParameterReferenceOrigin>().rootParameter;
    return EscapeProof::directInput(BorrowInputKey::parameter(index));
  }
  if (!definition.origin.detail.is<LocalReferenceOrigin>()) return zc::none;
  zc::Vector<OwnershipPoint> requiredPoints;
  requiredPoints.add(definition.livePoints.afterCommit);
  requiredPoints.add(definition.livePoints.afterCommitCfg);
  requiredPoints.add(definition.livePoints.beforeReturnCfg);
  requiredPoints.add(definition.livePoints.beforeReturn);
  requiredPoints.add(definition.livePoints.afterReturn);
  return EscapeProof::contained(zc::mv(requiredPoints));
}

/// \brief Builds one escape fact for one reference with a Direct origin route.
///
/// The admitted subset admits no raw carriers, so every origin route is
/// Direct and the raw-carrier sequence is empty.
zc::Maybe<EscapeFact> directEscapeFact(const ReferenceDefinition& definition, EscapeKind kind,
                                       zc::ArrayPtr<const RegionMembership> memberships) {
  auto origin = referenceOrigin(definition);
  if (origin == zc::none) return zc::none;
  auto proof = escapeProof(definition, ZC_ASSERT_NONNULL(origin), memberships);
  if (proof == zc::none) return zc::none;
  zc::Vector<EscapeOriginCause> origins;
  ZC_IF_SOME(originValue, origin) {
    origins.add(EscapeOriginCause{zc::mv(originValue), EscapeOriginRoute::direct()});
  }
  zc::Vector<RawProvenanceCarrierKey> rawCarriers;
  return EscapeFact{definition.returned,
                    MovePathKey{definition.destination.owner, definition.destination.place.clone()},
                    zc::mv(kind),
                    zc::mv(origins),
                    zc::mv(rawCarriers),
                    zc::mv(ZC_ASSERT_NONNULL(proof))};
}

/// \brief Derives one Return escape per returned reference.
///
/// Every verified reference definition is a reference whose destination is
/// returned, so each is one admitted Return escape operand.
zc::Maybe<zc::Vector<EscapeFact>> deriveReturnEscapes(
    const VerifiedReferenceDefinitions& references,
    zc::ArrayPtr<const RegionMembership> memberships) {
  zc::Vector<EscapeFact> escapes;
  for (const auto& definition : references.definitions()) {
    auto fact = directEscapeFact(definition, EscapeKind::returnEscape(), memberships);
    if (fact == zc::none) return zc::none;
    ZC_IF_SOME(value, fact) { escapes.add(zc::mv(value)); }
  }
  return escapes;
}

/// \brief Derives Store escapes for the admitted subset.
///
/// A Store escape records one reference written into a destination that is not
/// the function return. The admitted surface subset admits no reference
/// stores, so the overlay carries no Escape-role slot off the return path and
/// this derives an empty sequence. When reference stores reach MIR, each
/// Escape-role slot at a non-return point becomes one Store escape: the slot
/// key is the escape event, the stored reference's origin routes Direct, and
/// the kind names the store destination and its storage region.
zc::Maybe<zc::Vector<EscapeFact>> deriveStoreEscapes(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedReferenceDefinitions& references,
    zc::ArrayPtr<const RegionMembership> memberships) {
  zc::Vector<EscapeFact> escapes;
  for (const auto& function : overlay.functions()) {
    for (const auto& slot : function.slots) {
      if (!hasEscapeRole(slot)) continue;
      // Escape-role slots at the return terminator are Return escapes, derived
      // by deriveReturnEscapes. A Store escape sits at a statement point.
      if (slot.key.location.point.kind() == MirPointKind::BeforeTerminator) continue;
      // The admitted subset admits no reference stores, so a non-return
      // Escape-role slot cannot occur in a verified overlay. Fail closed:
      // surface admission must grow the Store derivation before emitting one.
      return zc::none;
    }
  }
  return escapes;
}

/// \brief Derives one ClosureCapture escape per captured reference.
///
/// Each capture of a reference value escapes through the constructed closure:
/// the kind names the closure move path and closure region, the origin routes
/// Direct from the captured reference's root, and the proof requires the
/// origin to be contained at the construction point. The admitted subset
/// admits no closures, so the capture inventory is empty and this derives no
/// rows.
zc::Maybe<zc::Vector<EscapeFact>> deriveCaptureEscapes(
    const VerifiedCaptureFacts& captures, const VerifiedReferenceDefinitions& references,
    zc::ArrayPtr<const RegionMembership> memberships) {
  zc::Vector<EscapeFact> escapes;
  for (const auto& capture : captures.captures()) {
    const ReferenceDefinition* captured = nullptr;
    for (const auto& definition : references.definitions()) {
      if (sameMovePath(definition.destination, capture.captured)) {
        captured = &definition;
        break;
      }
    }
    // A captured value that is not a verified reference cannot route a
    // reference origin; the admitted subset admits no captures at all, so
    // reaching here means the capture inventory drifted from the reference
    // inventory and the derivation fails closed.
    if (captured == nullptr) return zc::none;
    auto fact =
        directEscapeFact(*captured,
                         EscapeKind::closureCaptureEscape(
                             MovePathKey{capture.closure.owner, capture.closure.place.clone()},
                             capture.closureRegion.clone()),
                         memberships);
    if (fact == zc::none) return zc::none;
    ZC_IF_SOME(value, fact) {
      value.key = capture.construction;
      value.source = MovePathKey{capture.captured.owner, capture.captured.place.clone()};
      zc::Vector<OwnershipPoint> requiredPoints;
      requiredPoints.add(OwnershipPoint::beforeEvent(capture.construction));
      value.proof = EscapeProof::contained(zc::mv(requiredPoints));
      escapes.add(zc::mv(value));
    }
  }
  return escapes;
}

/// \brief Derives the complete escape inventory for the admitted subset.
///
/// Return escapes come from verified reference definitions, Store escapes from
/// reference stores in the event overlay (not yet admitted), and
/// ClosureCapture escapes from verified capture facts (empty until closures
/// reach MIR). The admitted subset produces only Return escapes.
zc::Maybe<zc::Vector<EscapeFact>> derive(const VerifiedReferenceDefinitions& references,
                                         const VerifiedCaptureFacts& captures,
                                         zc::ArrayPtr<const RegionMembership> memberships,
                                         const mir::VerifiedBuiltMir& builtMir,
                                         const VerifiedOwnershipEventOverlay& overlay) {
  auto returns = deriveReturnEscapes(references, memberships);
  if (returns == zc::none) return zc::none;
  auto stores = deriveStoreEscapes(builtMir, overlay, references, memberships);
  if (stores == zc::none) return zc::none;
  auto captured = deriveCaptureEscapes(captures, references, memberships);
  if (captured == zc::none) return zc::none;
  zc::Vector<EscapeFact> escapes;
  ZC_IF_SOME(values, returns) {
    for (auto& fact : values) { escapes.add(zc::mv(fact)); }
  }
  ZC_IF_SOME(values, stores) {
    for (auto& fact : values) { escapes.add(zc::mv(fact)); }
  }
  ZC_IF_SOME(values, captured) {
    for (auto& fact : values) { escapes.add(zc::mv(fact)); }
  }
  return escapes;
}

bool sameOrigins(zc::ArrayPtr<const EscapeOriginCause> left,
                 zc::ArrayPtr<const EscapeOriginCause> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].origin != right[index].origin || left[index].route != right[index].route) {
      return false;
    }
  }
  return true;
}

bool sameRawCarriers(zc::ArrayPtr<const RawProvenanceCarrierKey> left,
                     zc::ArrayPtr<const RawProvenanceCarrierKey> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].introduction != right[index].introduction ||
        left[index].destination.owner != right[index].destination.owner ||
        !samePlace(left[index].destination.place, right[index].destination.place)) {
      return false;
    }
  }
  return true;
}

bool sameEscapes(zc::ArrayPtr<const EscapeFact> left, zc::ArrayPtr<const EscapeFact> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].key != right[index].key ||
        left[index].source.owner != right[index].source.owner ||
        !samePlace(left[index].source.place, right[index].source.place) ||
        left[index].kind != right[index].kind ||
        !sameOrigins(left[index].origins.asPtr(), right[index].origins.asPtr()) ||
        !sameRawCarriers(left[index].rawCarriers.asPtr(), right[index].rawCarriers.asPtr()) ||
        left[index].proof != right[index].proof) {
      return false;
    }
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Proof validation predicates
// ---------------------------------------------------------------------------

bool staticEscapeProofAdmissible(zc::ArrayPtr<const EscapeOriginCause> origins,
                                 zc::ArrayPtr<const RegionMembership> memberships,
                                 const MirEventKey& key) noexcept {
  if (origins.size() == 0) return false;
  for (const auto& cause : origins) {
    if (!cause.origin.root.region.isStatic()) return false;
  }
  for (const auto& cause : origins) {
    bool contained = false;
    for (const auto& membership : memberships) {
      if (membership.region == cause.origin.active &&
          membership.point.kind() == OwnershipPointKind::BeforeEvent &&
          membership.point.beforeEventValue().event == key) {
        contained = true;
        break;
      }
    }
    if (!contained) return false;
  }
  return true;
}

bool addressOnlyEscapeProofAdmissible(
    zc::ArrayPtr<const EscapeOriginCause> origins,
    zc::ArrayPtr<const RawProvenanceCarrierKey> rawCarriers) noexcept {
  return origins.size() == 0 && rawCarriers.size() != 0;
}

// ---------------------------------------------------------------------------
// EscapeCandidate
// ---------------------------------------------------------------------------

EscapeCandidate::EscapeCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<EscapeFact>&& escapes) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      escapes(zc::mv(escapes)) {}

// ---------------------------------------------------------------------------
// VerifiedEscapeFacts
// ---------------------------------------------------------------------------

struct VerifiedEscapeFacts::Impl final {
  explicit Impl(EscapeCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}

  EscapeCandidate candidate;
};

VerifiedEscapeFacts::VerifiedEscapeFacts(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedEscapeFacts::VerifiedEscapeFacts(VerifiedEscapeFacts&&) noexcept = default;
VerifiedEscapeFacts& VerifiedEscapeFacts::operator=(VerifiedEscapeFacts&&) noexcept = default;
VerifiedEscapeFacts::~VerifiedEscapeFacts() noexcept(false) = default;

identity::SemanticContextBrand VerifiedEscapeFacts::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedEscapeFacts::contextFingerprint() const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedEscapeFacts::module() const noexcept { return impl->candidate.module; }
const mir::MirRevisionId& VerifiedEscapeFacts::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedEscapeFacts::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision& VerifiedEscapeFacts::borrowEvidenceRevision()
    const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const EscapeFact> VerifiedEscapeFacts::escapes() const noexcept {
  return impl->candidate.escapes.asPtr();
}

// ---------------------------------------------------------------------------
// EscapeBuilder
// ---------------------------------------------------------------------------

ir::IrOperationResult<EscapeCandidate> EscapeBuilder::build(
    const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references, const VerifiedOwnershipResourceFacts& resources,
    const VerifiedCaptureFacts& captures, const VerifiedRegionMemberships& memberships,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(flow, loans, references, resources, captures, memberships, builtMir, overlay)) {
    return reject<EscapeCandidate>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch,
                                   0);
  }
  auto escapes = derive(references, captures, memberships.memberships(), builtMir, overlay);
  if (escapes == zc::none) {
    return reject<EscapeCandidate>(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof,
                                   1);
  }
  ZC_IF_SOME(value, escapes) {
    return ir::IrOperationResult<EscapeCandidate>::verified(EscapeCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), builtMir.borrowEvidenceRevision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

// ---------------------------------------------------------------------------
// EscapeVerifier
// ---------------------------------------------------------------------------

ir::IrOperationResult<VerifiedEscapeFacts> EscapeVerifier::verify(
    EscapeCandidate&& candidate, const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references, const VerifiedOwnershipResourceFacts& resources,
    const VerifiedCaptureFacts& captures, const VerifiedRegionMemberships& memberships,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(flow, loans, references, resources, captures, memberships, builtMir, overlay)) {
    return reject<VerifiedEscapeFacts>(builtMir, identities,
                                       ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  // Validate the RFC 0013 proof requirements for every candidate row. The
  // admitted subset produces only DirectInput and Contained proofs, so the
  // Static and AddressOnly gates below are infrastructure that rejects any
  // candidate carrying an inadmissible proof before structural comparison.
  for (const auto& fact : candidate.escapes) {
    if (fact.proof.isStatic() &&
        !staticEscapeProofAdmissible(fact.origins.asPtr(), memberships.memberships(), fact.key)) {
      return reject<VerifiedEscapeFacts>(builtMir, identities,
                                         ir::IrFailureKind::InvalidOwnershipProof, 2);
    }
    if (fact.proof.isAddressOnly() &&
        !addressOnlyEscapeProofAdmissible(fact.origins.asPtr(), fact.rawCarriers.asPtr())) {
      return reject<VerifiedEscapeFacts>(builtMir, identities,
                                         ir::IrFailureKind::InvalidOwnershipProof, 2);
    }
  }
  // Independently reconstruct the expected inventory and reject any missing,
  // additional, wrong-kind, wrong-ordinal, missing-origin-route,
  // incomplete-point-set, foreign-carrier, or proof-incompatible row.
  auto expected = derive(references, captures, memberships.memberships(), builtMir, overlay);
  if (expected == zc::none ||
      !sameEscapes(candidate.escapes.asPtr(), ZC_ASSERT_NONNULL(expected).asPtr())) {
    return reject<VerifiedEscapeFacts>(builtMir, identities,
                                       ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedEscapeFacts>::verified(
      VerifiedEscapeFacts(zc::heap<VerifiedEscapeFacts::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
