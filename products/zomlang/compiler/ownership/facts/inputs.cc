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

#include "zomlang/compiler/ownership/facts/inputs.h"

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

ir::IrOperationResult<VerifiedOwnershipInputs> reject(
    const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities,
    uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Ownership input failure fallback must be legal");
  zc::Maybe<ir::IrFailureSite> noSite;
  zc::Maybe<identity::SourceSpan> noSpan;
  zc::Vector<uint32_t> noPath;
  auto descriptor = ir::IrFailureDescriptor::decoded(
      ir::IrRejectedBranch::IrInvariantRejected, ir::IrFailurePhase::OwnershipProofValidation,
      ir::IrFailureKind::InputRevisionMismatch, ir::IrFailureOwner::definition(definition),
      zc::mv(noSite), ir::IrFailureDetail::none(), zc::mv(noSpan), zc::mv(noPath), ordinal);
  ZC_IF_SOME(fallbackValue, fallback) {
    auto admitted = ir::IrFailureFactory::admit(zc::mv(descriptor), fallbackValue, resolver);
    if (admitted.is<ir::IdentityRejectedIrFailureDescriptor>()) {
      zc::Vector<identity::IdentityInvariant> failures;
      failures.add(zc::mv(admitted).get<ir::IdentityRejectedIrFailureDescriptor>().failure);
      auto sorted = ir::SortedIdentityInvariantFacts::from(zc::mv(failures));
      ZC_IF_SOME(values, sorted) {
        return ir::IrOperationResult<VerifiedOwnershipInputs>::identityInvariantRejected(
            zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<VerifiedOwnershipInputs>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

bool matches(const VerifiedMovePaths& movePaths, const VerifiedFlow& flow,
             const VerifiedInitializationFacts& initialization, const VerifiedLoanFacts& loans,
             const VerifiedReferenceDefinitions& references, const VerifiedReborrowRegions& regions,
             const VerifiedReborrowStates& states, const VerifiedOwnershipResourceFacts& resources,
             const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
             const driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease,
             const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability,
             const driver::borrow_evidence::BorrowEvidenceLookupResult& evidence) {
  if (!evidence.isResolved() ||
      evidence.evidence().semanticContext() != builtMir.semanticContext() ||
      evidence.evidence().contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      evidence.evidence().module() != builtMir.module() ||
      evidence.evidence().revision().digest() != builtMir.borrowEvidenceRevision().digest() ||
      lease.semanticContext() != builtMir.semanticContext() ||
      lease.key().module != builtMir.module() ||
      lease.key().revision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      capability.semanticContext() != builtMir.semanticContext()) {
    return false;
  }
  return overlay.semanticContext() == builtMir.semanticContext() &&
         overlay.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         overlay.module() == builtMir.module() &&
         overlay.checkedFactsRevision().digest() == builtMir.checkedFactsRevision().digest() &&
         overlay.builtRevision().digest() == builtMir.revision().digest() &&
         movePaths.semanticContext() == builtMir.semanticContext() &&
         flow.semanticContext() == builtMir.semanticContext() &&
         initialization.semanticContext() == builtMir.semanticContext() &&
         loans.semanticContext() == builtMir.semanticContext() &&
         references.semanticContext() == builtMir.semanticContext() &&
         regions.semanticContext() == builtMir.semanticContext() &&
         states.semanticContext() == builtMir.semanticContext() &&
         resources.semanticContext() == builtMir.semanticContext() &&
         movePaths.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         flow.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         initialization.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         loans.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         references.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         regions.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         states.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         resources.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         movePaths.module() == builtMir.module() && flow.module() == builtMir.module() &&
         initialization.module() == builtMir.module() && loans.module() == builtMir.module() &&
         references.module() == builtMir.module() && regions.module() == builtMir.module() &&
         states.module() == builtMir.module() && resources.module() == builtMir.module() &&
         movePaths.builtRevision().digest() == builtMir.revision().digest() &&
         flow.builtRevision().digest() == builtMir.revision().digest() &&
         initialization.builtRevision().digest() == builtMir.revision().digest() &&
         loans.builtRevision().digest() == builtMir.revision().digest() &&
         references.builtRevision().digest() == builtMir.revision().digest() &&
         regions.builtRevision().digest() == builtMir.revision().digest() &&
         states.builtRevision().digest() == builtMir.revision().digest() &&
         resources.builtRevision().digest() == builtMir.revision().digest() &&
         movePaths.overlayRevision().digest() == overlay.revision().digest() &&
         flow.overlayRevision().digest() == overlay.revision().digest() &&
         initialization.overlayRevision().digest() == overlay.revision().digest() &&
         loans.overlayRevision().digest() == overlay.revision().digest() &&
         references.overlayRevision().digest() == overlay.revision().digest() &&
         regions.overlayRevision().digest() == overlay.revision().digest() &&
         states.overlayRevision().digest() == overlay.revision().digest() &&
         resources.overlayRevision().digest() == overlay.revision().digest() &&
         loans.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest() &&
         references.borrowEvidenceRevision().digest() ==
             builtMir.borrowEvidenceRevision().digest() &&
         regions.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest() &&
         states.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest();
}

}  // namespace

struct VerifiedOwnershipInputs::Impl final {
  Impl(VerifiedMovePaths&& movePaths, VerifiedFlow&& flow,
       VerifiedInitializationFacts&& initialization, VerifiedLoanFacts&& loans,
       VerifiedReferenceDefinitions&& references, VerifiedReborrowRegions&& regions,
       VerifiedReborrowStates&& states, VerifiedOwnershipResourceFacts&& resources,
       driver::borrow_evidence::VerifiedBorrowEvidenceLease&& borrowEvidenceLease,
       driver::borrow_evidence::BorrowEvidenceRepositoryCapability&&
           borrowEvidenceCapability) noexcept
      : movePaths(zc::mv(movePaths)),
        flow(zc::mv(flow)),
        initialization(zc::mv(initialization)),
        loans(zc::mv(loans)),
        references(zc::mv(references)),
        regions(zc::mv(regions)),
        states(zc::mv(states)),
        resources(zc::mv(resources)),
        borrowEvidenceLease(zc::mv(borrowEvidenceLease)),
        borrowEvidenceCapability(zc::mv(borrowEvidenceCapability)) {}

  VerifiedMovePaths movePaths;
  VerifiedFlow flow;
  VerifiedInitializationFacts initialization;
  VerifiedLoanFacts loans;
  VerifiedReferenceDefinitions references;
  VerifiedReborrowRegions regions;
  VerifiedReborrowStates states;
  VerifiedOwnershipResourceFacts resources;
  driver::borrow_evidence::VerifiedBorrowEvidenceLease borrowEvidenceLease;
  driver::borrow_evidence::BorrowEvidenceRepositoryCapability borrowEvidenceCapability;
};

VerifiedOwnershipInputs::VerifiedOwnershipInputs(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedOwnershipInputs::~VerifiedOwnershipInputs() noexcept(false) = default;
VerifiedOwnershipInputs::VerifiedOwnershipInputs(VerifiedOwnershipInputs&&) noexcept = default;
VerifiedOwnershipInputs& VerifiedOwnershipInputs::operator=(VerifiedOwnershipInputs&&) noexcept =
    default;
identity::SemanticContextBrand VerifiedOwnershipInputs::semanticContext() const noexcept {
  return impl->movePaths.semanticContext();
}
const identity::ContextFingerprint& VerifiedOwnershipInputs::contextFingerprint()
    const noexcept {
  return impl->movePaths.contextFingerprint();
}
identity::ModuleId VerifiedOwnershipInputs::module() const noexcept {
  return impl->movePaths.module();
}
const mir::MirRevisionId& VerifiedOwnershipInputs::builtRevision() const noexcept {
  return impl->movePaths.builtRevision();
}
const OwnershipEventOverlayRevision& VerifiedOwnershipInputs::overlayRevision() const noexcept {
  return impl->movePaths.overlayRevision();
}
const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedOwnershipInputs::borrowEvidenceRevision() const noexcept {
  return impl->loans.borrowEvidenceRevision();
}
bool VerifiedOwnershipInputs::hasLiveBorrowEvidence() const noexcept {
  const auto evidence = impl->borrowEvidenceCapability.lookup(impl->borrowEvidenceLease);
  return evidence.isResolved() && evidence.evidence().semanticContext() == semanticContext() &&
         evidence.evidence().contextFingerprint().digest() == contextFingerprint().digest() &&
         evidence.evidence().module() == module() &&
         evidence.evidence().revision().digest() == borrowEvidenceRevision().digest();
}
const VerifiedMovePaths& VerifiedOwnershipInputs::movePaths() const noexcept {
  return impl->movePaths;
}
const VerifiedFlow& VerifiedOwnershipInputs::flow() const noexcept { return impl->flow; }
const VerifiedInitializationFacts& VerifiedOwnershipInputs::initialization() const noexcept {
  return impl->initialization;
}
const VerifiedLoanFacts& VerifiedOwnershipInputs::loans() const noexcept { return impl->loans; }
const VerifiedReferenceDefinitions& VerifiedOwnershipInputs::references() const noexcept {
  return impl->references;
}
const VerifiedReborrowRegions& VerifiedOwnershipInputs::regions() const noexcept {
  return impl->regions;
}
const VerifiedReborrowStates& VerifiedOwnershipInputs::states() const noexcept {
  return impl->states;
}
const VerifiedOwnershipResourceFacts& VerifiedOwnershipInputs::resources() const noexcept {
  return impl->resources;
}

ir::IrOperationResult<VerifiedOwnershipInputs> OwnershipInputVerifier::verify(
    VerifiedMovePaths&& movePaths, VerifiedFlow&& flow,
    VerifiedInitializationFacts&& initialization, VerifiedLoanFacts&& loans,
    VerifiedReferenceDefinitions&& references, VerifiedReborrowRegions&& regions,
    VerifiedReborrowStates&& states, VerifiedOwnershipResourceFacts&& resources,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const driver::borrow_evidence::VerifiedBorrowEvidenceLease& lease,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability) {
  const auto identities = builtMir.retainIdentityAuthority();
  const auto evidence = capability.lookup(lease);
  if (!matches(movePaths, flow, initialization, loans, references, regions, states, resources,
               builtMir, overlay, lease, capability, evidence) ||
      !builtMir.matchesBorrowEvidenceInput(lease, capability)) {
    return reject(builtMir, identities, 0);
  }
  auto retainedLease = builtMir.retainBorrowEvidenceLease();
  auto retainedCapability = builtMir.retainBorrowEvidenceCapability();
  return ir::IrOperationResult<VerifiedOwnershipInputs>::verified(
      VerifiedOwnershipInputs(zc::heap<VerifiedOwnershipInputs::Impl>(
          zc::mv(movePaths), zc::mv(flow), zc::mv(initialization), zc::mv(loans),
          zc::mv(references), zc::mv(regions), zc::mv(states), zc::mv(resources),
          zc::mv(retainedLease), zc::mv(retainedCapability))));
}

}  // namespace zomlang::compiler::ownership::facts
