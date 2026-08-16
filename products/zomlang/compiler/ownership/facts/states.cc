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

#include "zomlang/compiler/ownership/facts/states.h"

#include "zomlang/compiler/ir/ir-diagnostic-adapter.h"

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
  ZC_IREQUIRE(fallback != zc::none, "Reference-state failure fallback must be legal");
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

bool sameProjection(const mir::MirProjection& left, const mir::MirProjection& right) {
  if (left.kind() != right.kind() || left.inputType() != right.inputType() ||
      left.resultType() != right.resultType()) {
    return false;
  }
  if (left.kind() == mir::MirProjectionKind::Dereference) return true;
  if (left.kind() == mir::MirProjectionKind::Field) {
    return left.fieldValue().field == right.fieldValue().field;
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

bool inputsMatch(const VerifiedReferenceDefinitions& references,
                 const VerifiedReborrowRegions& regions, const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return references.semanticContext() == builtMir.semanticContext() &&
         regions.semanticContext() == builtMir.semanticContext() &&
         references.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         regions.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         references.module() == builtMir.module() && regions.module() == builtMir.module() &&
         references.builtRevision().digest() == builtMir.revision().digest() &&
         regions.builtRevision().digest() == builtMir.revision().digest() &&
         references.overlayRevision().digest() == overlay.revision().digest() &&
         regions.overlayRevision().digest() == overlay.revision().digest() &&
         references.borrowEvidenceRevision().digest() ==
             builtMir.borrowEvidenceRevision().digest() &&
         regions.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest();
}

zc::Maybe<const ReborrowRegion&> regionFor(const VerifiedReborrowRegions& regions,
                                           const ReferenceDefinition& reference) {
  zc::Maybe<const ReborrowRegion&> result;
  for (const auto& region : regions.regions()) {
    if (region.owner != reference.owner || region.loan != reference.loan ||
        region.inputParameter != reference.origin.rootParameter) {
      continue;
    }
    if (result != zc::none) return zc::none;
    result = region;
  }
  return result;
}

zc::Maybe<zc::Vector<ReborrowState>> derive(const VerifiedReferenceDefinitions& references,
                                            const VerifiedReborrowRegions& regions) {
  zc::Vector<ReborrowState> states;
  for (const auto& reference : references.definitions()) {
    auto region = regionFor(regions, reference);
    if (region == zc::none) return zc::none;
    ZC_IF_SOME(value, region) {
      if (value.entry != reference.origin.entry || value.members.size() != 6 ||
          value.members[1] != reference.livePoints.afterCommit ||
          value.members[2] != reference.livePoints.afterCommitCfg ||
          value.members[3] != reference.livePoints.beforeReturnCfg ||
          value.members[4] != reference.livePoints.beforeReturn ||
          value.members[5] != reference.livePoints.afterReturn) {
        return zc::none;
      }
      for (size_t index = 1; index < value.members.size(); ++index) {
        states.add(ReborrowState{
            reference.owner, value.members[index], reference.loan, reference.origin.rootParameter,
            MovePathKey{reference.destination.owner, reference.destination.place.clone()}});
      }
    }
  }
  return states;
}

bool sameStates(zc::ArrayPtr<const ReborrowState> left, zc::ArrayPtr<const ReborrowState> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].owner != right[index].owner || left[index].point != right[index].point ||
        left[index].loan != right[index].loan ||
        left[index].inputParameter != right[index].inputParameter ||
        left[index].destination.owner != right[index].destination.owner ||
        !samePlace(left[index].destination.place, right[index].destination.place)) {
      return false;
    }
  }
  return true;
}

}  // namespace

ReborrowStateCandidate::ReborrowStateCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<ReborrowState>&& states) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      states(zc::mv(states)) {}

struct VerifiedReborrowStates::Impl final {
  explicit Impl(ReborrowStateCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  ReborrowStateCandidate candidate;
};

VerifiedReborrowStates::VerifiedReborrowStates(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedReborrowStates::~VerifiedReborrowStates() noexcept(false) = default;
VerifiedReborrowStates::VerifiedReborrowStates(VerifiedReborrowStates&&) noexcept = default;
VerifiedReborrowStates& VerifiedReborrowStates::operator=(VerifiedReborrowStates&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedReborrowStates::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedReborrowStates::contextFingerprint()
    const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedReborrowStates::module() const noexcept {
  return impl->candidate.module;
}
const mir::MirRevisionId& VerifiedReborrowStates::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedReborrowStates::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedReborrowStates::borrowEvidenceRevision() const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const ReborrowState> VerifiedReborrowStates::states() const noexcept {
  return impl->candidate.states.asPtr();
}

ir::IrOperationResult<ReborrowStateCandidate> ReborrowStateBuilder::build(
    const VerifiedReferenceDefinitions& references, const VerifiedReborrowRegions& regions,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(references, regions, builtMir, overlay)) {
    return reject<ReborrowStateCandidate>(builtMir, identities,
                                          ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto states = derive(references, regions);
  if (states == zc::none) {
    return reject<ReborrowStateCandidate>(builtMir, identities,
                                          ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  ZC_IF_SOME(value, states) {
    return ir::IrOperationResult<ReborrowStateCandidate>::verified(ReborrowStateCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), builtMir.borrowEvidenceRevision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedReborrowStates> ReborrowStateVerifier::verify(
    ReborrowStateCandidate&& candidate, const VerifiedReferenceDefinitions& references,
    const VerifiedReborrowRegions& regions, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(references, regions, builtMir, overlay)) {
    return reject<VerifiedReborrowStates>(builtMir, identities,
                                          ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = derive(references, regions);
  if (expected == zc::none || !sameStates(candidate.states, ZC_ASSERT_NONNULL(expected))) {
    return reject<VerifiedReborrowStates>(builtMir, identities,
                                          ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedReborrowStates>::verified(
      VerifiedReborrowStates(zc::heap<VerifiedReborrowStates::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
