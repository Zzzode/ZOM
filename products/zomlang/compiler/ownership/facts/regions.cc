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

#include "zomlang/compiler/ownership/facts/regions.h"

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
  ZC_IREQUIRE(fallback != zc::none, "Region failure fallback must be legal");
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

bool sameMembers(zc::ArrayPtr<const OwnershipPoint> left,
                 zc::ArrayPtr<const OwnershipPoint> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

bool inputsMatch(const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
                 const VerifiedReferenceDefinitions& references,
                 const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return flow.semanticContext() == builtMir.semanticContext() &&
         loans.semanticContext() == builtMir.semanticContext() &&
         references.semanticContext() == builtMir.semanticContext() &&
         flow.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         loans.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         references.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         flow.module() == builtMir.module() && loans.module() == builtMir.module() &&
         references.module() == builtMir.module() &&
         flow.builtRevision().digest() == builtMir.revision().digest() &&
         loans.builtRevision().digest() == builtMir.revision().digest() &&
         references.builtRevision().digest() == builtMir.revision().digest() &&
         flow.overlayRevision().digest() == overlay.revision().digest() &&
         loans.overlayRevision().digest() == overlay.revision().digest() &&
         references.overlayRevision().digest() == overlay.revision().digest() &&
         loans.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest() &&
         references.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest();
}

zc::Maybe<const FlowFunction&> flowFor(const VerifiedFlow& flow, identity::DefId owner) {
  zc::Maybe<const FlowFunction&> result;
  for (const auto& function : flow.functions()) {
    if (function.owner != owner) continue;
    if (result != zc::none) return zc::none;
    result = function;
  }
  return result;
}

bool hasPoint(const FlowFunction& flow, const OwnershipPoint& point) {
  for (const auto& value : flow.points) {
    if (value == point) return true;
  }
  return false;
}

bool reaches(const FlowFunction& flow, const OwnershipPoint& from, const OwnershipPoint& to,
             zc::Vector<OwnershipPoint>& visited) {
  if (from == to) return true;
  for (const auto& value : visited) {
    if (value == from) return false;
  }
  visited.add(from);
  for (const auto& edge : flow.edges) {
    if (edge.from != from) continue;
    if (reaches(flow, edge.to, to, visited)) return true;
  }
  return false;
}

bool flowContainsMembers(const VerifiedFlow& flow, identity::DefId owner,
                         zc::ArrayPtr<const OwnershipPoint> members) {
  auto function = flowFor(flow, owner);
  if (function == zc::none || members.size() == 0) return false;
  ZC_IF_SOME(value, function) {
    for (const auto& member : members) {
      if (!hasPoint(value, member)) return false;
    }
    for (size_t index = 1; index < members.size(); ++index) {
      zc::Vector<OwnershipPoint> visited;
      if (!reaches(value, members[index - 1], members[index], visited)) return false;
    }
    return true;
  }
  ZC_UNREACHABLE
}

zc::Maybe<const LoanFact&> loanFor(const VerifiedLoanFacts& loans,
                                   const ReferenceDefinition& reference) {
  zc::Maybe<const LoanFact&> result;
  for (const auto& loan : loans.loans()) {
    if (loan.owner != reference.owner || loan.issue != reference.loan ||
        loan.commit != reference.introduction) {
      continue;
    }
    if (result != zc::none) return zc::none;
    result = loan;
  }
  return result;
}

zc::Maybe<zc::Vector<ReborrowRegion>> derive(const VerifiedFlow& flow,
                                             const VerifiedLoanFacts& loans,
                                             const VerifiedReferenceDefinitions& references) {
  zc::Vector<ReborrowRegion> regions;
  for (const auto& reference : references.definitions()) {
    auto loan = loanFor(loans, reference);
    if (loan == zc::none) return zc::none;
    ZC_IF_SOME(value, loan) {
      if (value.activeFrom != reference.origin.activation ||
          reference.origin.entry.location.owner != reference.owner ||
          reference.origin.entry.location.point.kind() != MirPointKind::Entry ||
          reference.origin.entry.operandOrdinal != 0) {
        return zc::none;
      }
      zc::Vector<OwnershipPoint> members;
      members.add(value.activeFrom);
      members.add(reference.livePoints.afterCommit);
      members.add(reference.livePoints.afterCommitCfg);
      members.add(reference.livePoints.beforeReturnCfg);
      members.add(reference.livePoints.beforeReturn);
      members.add(reference.livePoints.afterReturn);
      if (!flowContainsMembers(flow, reference.owner, members.asPtr())) return zc::none;
      regions.add(ReborrowRegion{reference.owner, reference.origin.entry, reference.loan,
                                 reference.origin.rootParameter, zc::mv(members)});
    }
  }
  return regions;
}

bool sameRegions(zc::ArrayPtr<const ReborrowRegion> left,
                 zc::ArrayPtr<const ReborrowRegion> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].owner != right[index].owner || left[index].entry != right[index].entry ||
        left[index].loan != right[index].loan ||
        left[index].inputParameter != right[index].inputParameter ||
        !sameMembers(left[index].members.asPtr(), right[index].members.asPtr())) {
      return false;
    }
  }
  return true;
}

}  // namespace

ReborrowRegionCandidate::ReborrowRegionCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::SemanticContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<ReborrowRegion>&& regions) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      regions(zc::mv(regions)) {}

struct VerifiedReborrowRegions::Impl final {
  explicit Impl(ReborrowRegionCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  ReborrowRegionCandidate candidate;
};

VerifiedReborrowRegions::VerifiedReborrowRegions(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedReborrowRegions::~VerifiedReborrowRegions() noexcept(false) = default;
VerifiedReborrowRegions::VerifiedReborrowRegions(VerifiedReborrowRegions&&) noexcept = default;
VerifiedReborrowRegions& VerifiedReborrowRegions::operator=(VerifiedReborrowRegions&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedReborrowRegions::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::SemanticContextFingerprint& VerifiedReborrowRegions::contextFingerprint()
    const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedReborrowRegions::module() const noexcept {
  return impl->candidate.module;
}
const mir::MirRevisionId& VerifiedReborrowRegions::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedReborrowRegions::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedReborrowRegions::borrowEvidenceRevision() const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const ReborrowRegion> VerifiedReborrowRegions::regions() const noexcept {
  return impl->candidate.regions.asPtr();
}

ir::IrOperationResult<ReborrowRegionCandidate> ReborrowRegionBuilder::build(
    const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(flow, loans, references, builtMir, overlay)) {
    return reject<ReborrowRegionCandidate>(builtMir, identities,
                                           ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto regions = derive(flow, loans, references);
  if (regions == zc::none) {
    return reject<ReborrowRegionCandidate>(builtMir, identities,
                                           ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  ZC_IF_SOME(value, regions) {
    return ir::IrOperationResult<ReborrowRegionCandidate>::verified(ReborrowRegionCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), builtMir.borrowEvidenceRevision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedReborrowRegions> ReborrowRegionVerifier::verify(
    ReborrowRegionCandidate&& candidate, const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(flow, loans, references, builtMir, overlay)) {
    return reject<VerifiedReborrowRegions>(builtMir, identities,
                                           ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = derive(flow, loans, references);
  if (expected == zc::none || !sameRegions(candidate.regions, ZC_ASSERT_NONNULL(expected))) {
    return reject<VerifiedReborrowRegions>(builtMir, identities,
                                           ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedReborrowRegions>::verified(
      VerifiedReborrowRegions(zc::heap<VerifiedReborrowRegions::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
