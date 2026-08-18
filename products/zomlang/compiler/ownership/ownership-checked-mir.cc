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

#include "zomlang/compiler/ownership/ownership-checked-mir.h"

#include "zomlang/compiler/ir/ir-diagnostic-adapter.h"

namespace zomlang::compiler::ownership {
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

ir::IrOperationResult<OwnershipCheckedMir> reject(
    const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities,
    uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Ownership finalize failure fallback must be legal");
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
        return ir::IrOperationResult<OwnershipCheckedMir>::identityInvariantRejected(
            zc::mv(values));
      }
      ZC_UNREACHABLE
    }
    zc::Vector<ir::IrFailureFact> failures;
    if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
      failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    } else {
      failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
    }
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<OwnershipCheckedMir>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

bool matches(const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
             const facts::VerifiedOwnershipInputs& facts,
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
         facts.semanticContext() == builtMir.semanticContext() &&
         facts.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         facts.module() == builtMir.module() &&
         facts.builtRevision().digest() == builtMir.revision().digest() &&
         facts.overlayRevision().digest() == overlay.revision().digest() &&
         facts.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest();
}

}  // namespace

struct OwnershipCheckedMir::Impl final {
  Impl(mir::VerifiedBuiltMir&& builtMir, VerifiedOwnershipEventOverlay&& eventOverlay,
       facts::VerifiedOwnershipInputs&& facts) noexcept
      : builtMir(zc::mv(builtMir)), eventOverlay(zc::mv(eventOverlay)), facts(zc::mv(facts)) {}

  mir::VerifiedBuiltMir builtMir;
  VerifiedOwnershipEventOverlay eventOverlay;
  facts::VerifiedOwnershipInputs facts;
};

OwnershipCheckedMir::OwnershipCheckedMir(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
OwnershipCheckedMir::~OwnershipCheckedMir() noexcept(false) = default;
OwnershipCheckedMir::OwnershipCheckedMir(OwnershipCheckedMir&&) noexcept = default;
OwnershipCheckedMir& OwnershipCheckedMir::operator=(OwnershipCheckedMir&&) noexcept = default;

identity::SemanticContextBrand OwnershipCheckedMir::semanticContext() const noexcept {
  return impl->builtMir.semanticContext();
}
const identity::ContextFingerprint& OwnershipCheckedMir::contextFingerprint() const noexcept {
  return impl->builtMir.contextFingerprint();
}
identity::ModuleId OwnershipCheckedMir::module() const noexcept { return impl->builtMir.module(); }
const mir::VerifiedBuiltMir& OwnershipCheckedMir::builtMir() const noexcept {
  return impl->builtMir;
}
const VerifiedOwnershipEventOverlay& OwnershipCheckedMir::eventOverlay() const noexcept {
  return impl->eventOverlay;
}
const facts::VerifiedOwnershipInputs& OwnershipCheckedMir::facts() const noexcept {
  return impl->facts;
}
const mir::MirRevisionId& OwnershipCheckedMir::builtRevision() const noexcept {
  return impl->builtMir.revision();
}
const OwnershipEventOverlayRevision& OwnershipCheckedMir::eventOverlayRevision() const noexcept {
  return impl->eventOverlay.revision();
}
const driver::borrow_evidence::BorrowEvidenceRevision& OwnershipCheckedMir::borrowEvidenceRevision()
    const noexcept {
  return impl->builtMir.borrowEvidenceRevision();
}

ir::IrOperationResult<OwnershipCheckedMir> OwnershipFinalizer::finalizeOwnership(
    mir::VerifiedBuiltMir&& builtMir, VerifiedOwnershipEventOverlay&& eventOverlay,
    facts::VerifiedOwnershipInputs&& facts,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository) {
  const auto identities = builtMir.retainIdentityAuthority();
  const auto& lease = builtMir.borrowEvidenceLease();
  const auto evidence = repository.lookup(lease);
  if (!matches(builtMir, eventOverlay, facts, lease, repository, evidence) ||
      !builtMir.matchesBorrowEvidenceInput(lease, repository)) {
    return reject(builtMir, identities, 0);
  }
  return ir::IrOperationResult<OwnershipCheckedMir>::verified(OwnershipCheckedMir(
      zc::heap<OwnershipCheckedMir::Impl>(zc::mv(builtMir), zc::mv(eventOverlay), zc::mv(facts))));
}

}  // namespace zomlang::compiler::ownership
