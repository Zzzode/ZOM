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

#include "zomlang/compiler/ownership/drop-elaborated-mir.h"

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

template <typename Result>
ir::IrOperationResult<Result> reject(const mir::VerifiedBuiltMir& builtMir,
                                     const checker::CheckerIdentityAuthority& identities,
                                     ir::IrFailureKind kind, uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Drop elaboration failure fallback must be legal");
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
    if (admitted.is<ir::AcceptedIrFailureDescriptor>()) {
      failures.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    } else {
      failures.add(zc::mv(admitted).get<ir::FallbackIrFailureDescriptor>().fact);
    }
    auto sorted = ir::SortedIrInvariantFailureFacts::from(zc::mv(failures));
    ZC_IF_SOME(values, sorted) {
      return ir::IrOperationResult<Result>::irInvariantRejected(zc::mv(values));
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

const mir::MirFunction* findFunction(zc::ArrayPtr<const mir::MirFunction> functions,
                                     identity::DefId owner) {
  for (const auto& function : functions) {
    if (function.owner == owner) return &function;
  }
  return nullptr;
}

zc::Maybe<zc::Vector<DropDischargeRecord>> elaborateLinear(
    const facts::VerifiedOwnershipResourceFacts& resources,
    zc::ArrayPtr<const mir::MirFunction> mirFunctions) {
  zc::Vector<DropDischargeRecord> discharges;
  for (const auto& resourceFunction : resources.functions()) {
    const auto* mirFunction = findFunction(mirFunctions, resourceFunction.owner);
    if (mirFunction == nullptr || mirFunction->blocks.size() == 0) return zc::none;
    const auto& facts = resourceFunction.facts;
    const auto& plans = resourceFunction.dropPlans;

    // Track which fact ordinals have been discharged. Every pending drop
    // obligation must be discharged by exactly one drop plan.
    zc::Vector<bool> discharged;
    for (size_t i = 0; i < facts.size(); ++i) discharged.add(false);

    for (const auto& plan : plans) {
      if (plan.components.size() == 0) return zc::none;
      for (size_t index = 0; index < plan.components.size(); ++index) {
        const auto factOrdinal = plan.components[index].factOrdinal;
        if (factOrdinal >= facts.size()) return zc::none;
        if (discharged[factOrdinal]) return zc::none;
        discharged[factOrdinal] = true;

        // Components must be in reverse declaration order so each child is
        // pre-consumed before its parent's action.
        if (index > 0) {
          const auto previousOrdinal = plan.components[index - 1].factOrdinal;
          if (facts[previousOrdinal].declarationOrdinal <=
              facts[factOrdinal].declarationOrdinal) {
            return zc::none;
          }
        }
      }

      zc::Vector<DropDischargeComponent> components;
      for (const auto& component : plan.components) {
        const auto& fact = facts[component.factOrdinal];
        components.add(DropDischargeComponent{
            facts::MovePathKey{fact.subject.origin.owner, fact.subject.origin.place.clone()},
            fact.subject.clone(), fact.subject.originType, component.action,
            fact.declarationOrdinal});
      }

      const auto& block = mirFunction->blocks[0];
      discharges.add(DropDischargeRecord{
          MirEventKey{
              MirLocation{resourceFunction.owner,
                          MirPoint::exit(block.id, MirExitKind::Return)},
              0},
          facts::MovePathKey{plan.subject.origin.owner, plan.subject.origin.place.clone()},
          DropDischargeKind::LogicalDrop, plan.mode, zc::mv(components)});
    }

    // Every pending drop obligation must have a complete discharge path.
    for (bool done : discharged) {
      if (!done) return zc::none;
    }
  }
  return discharges;
}

}  // namespace

struct DropElaboratedMir::Impl final {
  Impl(OwnershipCheckedMir&& checked, zc::Vector<DropDischargeRecord>&& discharges) noexcept
      : checked(zc::mv(checked)), discharges(zc::mv(discharges)) {}

  OwnershipCheckedMir checked;
  zc::Vector<DropDischargeRecord> discharges;
};

DropElaboratedMir::DropElaboratedMir(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
DropElaboratedMir::~DropElaboratedMir() noexcept(false) = default;
DropElaboratedMir::DropElaboratedMir(DropElaboratedMir&&) noexcept = default;
DropElaboratedMir& DropElaboratedMir::operator=(DropElaboratedMir&&) noexcept = default;

identity::SemanticContextBrand DropElaboratedMir::semanticContext() const noexcept {
  return impl->checked.semanticContext();
}
const identity::ContextFingerprint& DropElaboratedMir::contextFingerprint() const noexcept {
  return impl->checked.contextFingerprint();
}
identity::ModuleId DropElaboratedMir::module() const noexcept { return impl->checked.module(); }
const OwnershipCheckedMir& DropElaboratedMir::checkedMir() const noexcept {
  return impl->checked;
}
zc::ArrayPtr<const DropDischargeRecord> DropElaboratedMir::discharges() const noexcept {
  return impl->discharges.asPtr();
}
OwnershipCheckedMir DropElaboratedMir::takeCheckedMir() && noexcept {
  return zc::mv(impl->checked);
}

ir::IrOperationResult<DropElaboratedMir> DropElaborator::elaborateDrops(
    OwnershipCheckedMir&& checked,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository) {
  const auto& builtMir = checked.builtMir();
  const auto& overlay = checked.eventOverlay();
  const auto& facts = checked.facts();
  const auto identities = builtMir.retainIdentityAuthority();
  const auto& lease = builtMir.borrowEvidenceLease();
  const auto evidence = repository.lookup(lease);
  if (!matches(builtMir, overlay, facts, lease, repository, evidence) ||
      !builtMir.matchesBorrowEvidenceInput(lease, repository)) {
    return reject<DropElaboratedMir>(builtMir, identities,
                                     ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto discharges = elaborateLinear(facts.resources(), builtMir.functions());
  if (discharges == zc::none) {
    return reject<DropElaboratedMir>(builtMir, identities, ir::IrFailureKind::InvalidCleanup, 1);
  }
  ZC_IF_SOME(value, discharges) {
    return ir::IrOperationResult<DropElaboratedMir>::verified(DropElaboratedMir(
        zc::heap<DropElaboratedMir::Impl>(zc::mv(checked), zc::mv(value))));
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::ownership
