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
#include "zomlang/compiler/ownership/facts/flow-subset.h"

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

bool sameMovePath(const facts::MovePathKey& left, const facts::MovePathKey& right) {
  return left.owner == right.owner && samePlace(left.place, right.place);
}

/// \brief Returns the current place of a linear obligation after applying its
/// verified transfers in event order.
facts::MovePathKey obligationCurrentPlace(const facts::LinearObligationFact& obligation) {
  if (obligation.transfers.size() == 0) {
    return facts::MovePathKey{obligation.key.place.owner, obligation.key.place.place.clone()};
  }
  const auto& last = obligation.transfers[obligation.transfers.size() - 1];
  return facts::MovePathKey{last.to.owner, last.to.place.clone()};
}

/// \brief Finds the linear obligation whose key matches the given subject.
zc::Maybe<uint32_t> findLinearObligation(
    zc::ArrayPtr<const facts::LinearObligationFact> obligations,
    const facts::DropResourceSubject& subject) {
  for (uint32_t index = 0; index < obligations.size(); ++index) {
    if (obligations[index].key.introduction == subject.introduction &&
        sameMovePath(obligations[index].key.place, subject.origin)) {
      return index;
    }
  }
  return zc::none;
}

/// \brief Maps a verified linear-consumption kind to its discharge kind.
DropDischargeKind dischargeKind(facts::LinearConsumptionKind kind) {
  switch (kind) {
    case facts::LinearConsumptionKind::Return:
      return DropDischargeKind::ReturnTransfer;
    case facts::LinearConsumptionKind::ConsumingCall:
      return DropDischargeKind::ConsumingCallTransfer;
    case facts::LinearConsumptionKind::LogicalDrop:
      return DropDischargeKind::LogicalDrop;
  }
  ZC_UNREACHABLE
}

/// \brief Collects the block ids of every normal-return exit of a function.
///
/// A normal-return exit is a block whose terminator is Return. Unreachable is a
/// divergent exit that owns no drop discharge, and Call/Goto/SwitchInt are
/// internal edges. Unwind exits do not occur: the admitted flow subset rejects
/// any Call with an unwind target, so the multi-block walk only ever observes
/// normal edges. The returned order follows the function's block order.
zc::Vector<mir::MirBlockId> normalReturnExitBlocks(const mir::MirFunction& function) {
  zc::Vector<mir::MirBlockId> exits;
  for (const auto& block : function.blocks) {
    if (block.terminator.kind() == mir::MirTerminatorKind::Return) exits.add(block.id);
  }
  return exits;
}

/// \brief Finds the initialization inventory for one function owner.
const facts::InitializationFunction* findInitialization(
    zc::ArrayPtr<const facts::InitializationFunction> initialization, identity::DefId owner) {
  for (const auto& function : initialization) {
    if (function.owner == owner) return &function;
  }
  return nullptr;
}

/// \brief Returns true when the move path is initialized at the given normal
/// return exit in the verified initialization facts.
///
/// A logical resource is dropped at a normal-return exit only where its subject
/// is live-and-initialized. The initialization builder records one
/// `MirPoint::exit(block, Return)` fact per return block; the subject is
/// discharged at that exit when the fact's state is initialized.
bool initializedAtReturnExit(const facts::InitializationFunction& initialization,
                             const facts::MovePathKey& place, mir::MirBlockId exitBlock) {
  for (const auto& fact : initialization.facts) {
    if (fact.point.kind() != MirPointKind::Exit) continue;
    const auto& exitPoint = fact.point.exitValue();
    if (exitPoint.kind != MirExitKind::Return || exitPoint.block != exitBlock) continue;
    if (!sameMovePath(fact.key, place)) continue;
    return fact.state == facts::InitializationState::initialized();
  }
  return false;
}

zc::Maybe<zc::Vector<DropDischargeRecord>> elaborateLinear(
    zc::ArrayPtr<const facts::OwnershipResourceFunction> resources,
    zc::ArrayPtr<const mir::MirFunction> mirFunctions,
    zc::ArrayPtr<const facts::InitializationFunction> initialization) {
  zc::Vector<DropDischargeRecord> discharges;
  for (const auto& resourceFunction : resources) {
    const auto* mirFunction = findFunction(mirFunctions, resourceFunction.owner);
    if (mirFunction == nullptr || mirFunction->blocks.size() == 0) return zc::none;
    // Only walk topologies the flow subset admits (reducible CFG of Call, Goto,
    // and SwitchInt edges ending in Return or Unreachable, no unwind edges).
    if (!facts::isAdmittedFlowSubset(*mirFunction)) return zc::none;
    const auto* initFunction = findInitialization(initialization, resourceFunction.owner);
    const auto exits = normalReturnExitBlocks(*mirFunction);
    const auto& facts = resourceFunction.facts;
    const auto& plans = resourceFunction.dropPlans;
    const auto& obligations = resourceFunction.linearObligations;

    // Track which fact ordinals have at least one discharge path. A resource
    // moved between places legitimately appears in multiple closed drop plans,
    // one per move site, so a fact may be covered by more than one plan; only a
    // fact with no plan at all is a missing discharge.
    zc::Vector<bool> discharged;
    for (size_t i = 0; i < facts.size(); ++i) discharged.add(false);

    // Track which linear obligations are consumed by the emitted cleanup. Every
    // Positive Linear obligation must be consumed by the emitted discharges.
    zc::Vector<bool> consumedObligations;
    for (size_t i = 0; i < obligations.size(); ++i) consumedObligations.add(false);

    for (const auto& plan : plans) {
      if (plan.components.size() == 0) return zc::none;
      for (size_t index = 0; index < plan.components.size(); ++index) {
        const auto factOrdinal = plan.components[index].factOrdinal;
        if (factOrdinal >= facts.size()) return zc::none;
        discharged[factOrdinal] = true;

        // Components must be in reverse declaration order so each child is
        // pre-consumed before its parent's action.
        if (index > 0) {
          const auto previousOrdinal = plan.components[index - 1].factOrdinal;
          if (facts[previousOrdinal].declarationOrdinal <= facts[factOrdinal].declarationOrdinal) {
            return zc::none;
          }
        }
      }

      // Build the ordered component prototype once; it is cloned per emitted
      // discharge because a linear value consumed on more than one branch fans
      // out to one record per consumption.
      auto buildComponents = [&]() {
        zc::Vector<DropDischargeComponent> components;
        for (const auto& component : plan.components) {
          const auto& fact = facts[component.factOrdinal];
          components.add(DropDischargeComponent{
              facts::MovePathKey{fact.subject.origin.owner, fact.subject.origin.place.clone()},
              fact.subject.clone(), fact.subject.originType, component.action,
              fact.declarationOrdinal});
        }
        return components;
      };

      ZC_IF_SOME(obligationIndex, findLinearObligation(obligations, plan.subject)) {
        consumedObligations[obligationIndex] = true;
        const auto& obligation = obligations[obligationIndex];
        if (obligation.consumptions.size() > 0) {
          // Emit one discharge per consumption. A linear value returned on both
          // arms of a SwitchInt diamond yields two ReturnTransfer records; a
          // value returned on one arm and consumed by a call on another yields
          // one ReturnTransfer and one ConsumingCallTransfer. Each discharge
          // event is the consumption's own block-correct cutpoint.
          for (const auto& consumption : obligation.consumptions) {
            discharges.add(DropDischargeRecord{
                consumption.event,
                facts::MovePathKey{plan.subject.origin.owner, plan.subject.origin.place.clone()},
                dischargeKind(consumption.kind), plan.mode, buildComponents(),
                consumption.clone()});
          }
        } else {
          // The obligation reaches a normal exit unconsumed by a return or call:
          // it is dropped at each normal-return exit with LogicalDrop. Synthesize
          // the linked LogicalDrop consume at the obligation's current place and
          // the exit's cutpoint.
          const auto currentPlace = obligationCurrentPlace(obligation);
          if (exits.size() == 0) return zc::none;
          for (const auto exitBlock : exits) {
            MirEventKey event{
                MirLocation{resourceFunction.owner, MirPoint::exit(exitBlock, MirExitKind::Return)},
                0};
            discharges.add(DropDischargeRecord{
                event,
                facts::MovePathKey{plan.subject.origin.owner, plan.subject.origin.place.clone()},
                DropDischargeKind::LogicalDrop, plan.mode, buildComponents(),
                facts::LinearConsumption{
                    facts::MovePathKey{currentPlace.owner, currentPlace.place.clone()}, event,
                    facts::LinearConsumptionKind::LogicalDrop}});
          }
        }
      } else {
        // A purely Logical resource (no linear obligation) is dropped with
        // LogicalDrop at each normal-return exit where the subject is
        // live-and-initialized in the verified initialization facts. A subject
        // that is moved out before every normal-return exit (its ownership
        // transferred into the return value or a callee) is initialized at no
        // exit and correctly emits zero drops: the drop obligation left with the
        // move, so there is nothing to discharge here. The plan's fact is still
        // marked discharged above, so the completeness check below holds.
        const auto subjectPlace =
            facts::MovePathKey{plan.subject.origin.owner, plan.subject.origin.place.clone()};
        for (const auto exitBlock : exits) {
          if (initFunction != nullptr &&
              !initializedAtReturnExit(*initFunction, subjectPlace, exitBlock)) {
            continue;
          }
          MirEventKey event{
              MirLocation{resourceFunction.owner, MirPoint::exit(exitBlock, MirExitKind::Return)},
              0};
          discharges.add(DropDischargeRecord{
              event, facts::MovePathKey{subjectPlace.owner, subjectPlace.place.clone()},
              DropDischargeKind::LogicalDrop, plan.mode, buildComponents(), zc::none});
        }
      }
    }

    // Every pending drop obligation must have a complete discharge path.
    for (bool done : discharged) {
      if (!done) return zc::none;
    }
    // Every Positive Linear obligation must be consumed by the emitted cleanup.
    for (bool consumed : consumedObligations) {
      if (!consumed) return zc::none;
    }
  }
  return discharges;
}

/// \brief Certifies that the emitted cleanup discharges every Positive Linear
/// obligation exactly once. The terminal verifier re-validates this after the
/// full successor chain has rechecked the lineage.
bool cleanupConsumed(const DropElaboratedMir& elaborated) {
  const auto& resources = elaborated.checkedMir().facts().resources();
  const auto discharges = elaborated.discharges();
  for (const auto& function : resources.functions()) {
    for (const auto& obligation : function.linearObligations) {
      bool consumed = false;
      for (const auto& discharge : discharges) {
        if (discharge.linearConsume == zc::none) continue;
        ZC_IF_SOME(consume, discharge.linearConsume) {
          for (const auto& consumption : obligation.consumptions) {
            if (consume.event == consumption.event && consume.kind == consumption.kind) {
              consumed = true;
              break;
            }
          }
          if (!consumed && consume.kind == facts::LinearConsumptionKind::LogicalDrop &&
              sameMovePath(consume.place, obligationCurrentPlace(obligation))) {
            consumed = true;
          }
        }
        if (consumed) break;
      }
      if (!consumed) return false;
    }
  }
  return true;
}

}  // namespace

struct DropElaboratedMir::Impl final {
  Impl(OwnershipCheckedMir&& checked, zc::Vector<DropDischargeRecord>&& discharges) noexcept
      : semanticContext(checked.semanticContext()),
        contextFingerprint(checked.contextFingerprint().clone()),
        module(checked.module()),
        checked(zc::mv(checked)),
        discharges(zc::mv(discharges)) {}

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  OwnershipCheckedMir checked;
  zc::Vector<DropDischargeRecord> discharges;
};

DropElaboratedMir::DropElaboratedMir(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
DropElaboratedMir::~DropElaboratedMir() noexcept(false) = default;
DropElaboratedMir::DropElaboratedMir(DropElaboratedMir&&) noexcept = default;
DropElaboratedMir& DropElaboratedMir::operator=(DropElaboratedMir&&) noexcept = default;

identity::SemanticContextBrand DropElaboratedMir::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::ContextFingerprint& DropElaboratedMir::contextFingerprint() const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId DropElaboratedMir::module() const noexcept { return impl->module; }
const OwnershipCheckedMir& DropElaboratedMir::checkedMir() const noexcept { return impl->checked; }
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
  const auto identities = retainIdentityAuthority(builtMir);
  if (!recheckLineage(checked, repository)) {
    return reject<DropElaboratedMir>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch,
                                     0);
  }
  auto discharges = DropElaborator::computeDischarges(checked.facts().resources().functions(),
                                                      builtMir.functions(),
                                                      checked.facts().initialization().functions());
  if (discharges == zc::none) {
    return reject<DropElaboratedMir>(builtMir, identities, ir::IrFailureKind::InvalidCleanup, 1);
  }
  ZC_IF_SOME(value, discharges) {
    return ir::IrOperationResult<DropElaboratedMir>::verified(
        DropElaboratedMir(zc::heap<DropElaboratedMir::Impl>(zc::mv(checked), zc::mv(value))));
  }
  ZC_UNREACHABLE
}

zc::Maybe<zc::Vector<DropDischargeRecord>> DropElaborator::computeDischarges(
    zc::ArrayPtr<const facts::OwnershipResourceFunction> resources,
    zc::ArrayPtr<const mir::MirFunction> mirFunctions,
    zc::ArrayPtr<const facts::InitializationFunction> initialization) {
  return elaborateLinear(resources, mirFunctions, initialization);
}

bool DropElaborator::recheckLineage(
    const OwnershipCheckedMir& checked,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& capability) {
  const auto& builtMir = checked.builtMir();
  const auto& overlay = checked.eventOverlay();
  const auto& facts = checked.facts();
  const auto& lease = builtMir.borrowEvidenceLease();
  const auto evidence = capability.lookup(lease);
  return matches(builtMir, overlay, facts, lease, capability, evidence) &&
         builtMir.matchesBorrowEvidenceInput(lease, capability);
}

checker::CheckerIdentityAuthority DropElaborator::retainIdentityAuthority(
    const mir::VerifiedBuiltMir& builtMir) {
  return builtMir.retainIdentityAuthority();
}

struct CoroutineElaboratedMir::Impl final {
  explicit Impl(DropElaboratedMir&& elaborated) noexcept : elaborated(zc::mv(elaborated)) {}

  DropElaboratedMir elaborated;
};

CoroutineElaboratedMir::CoroutineElaboratedMir(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
CoroutineElaboratedMir::~CoroutineElaboratedMir() noexcept(false) = default;
CoroutineElaboratedMir::CoroutineElaboratedMir(CoroutineElaboratedMir&&) noexcept = default;
CoroutineElaboratedMir& CoroutineElaboratedMir::operator=(CoroutineElaboratedMir&&) noexcept =
    default;

identity::SemanticContextBrand CoroutineElaboratedMir::semanticContext() const noexcept {
  return impl->elaborated.semanticContext();
}
const identity::ContextFingerprint& CoroutineElaboratedMir::contextFingerprint() const noexcept {
  return impl->elaborated.contextFingerprint();
}
identity::ModuleId CoroutineElaboratedMir::module() const noexcept {
  return impl->elaborated.module();
}
zc::ArrayPtr<const DropDischargeRecord> CoroutineElaboratedMir::discharges() const noexcept {
  return impl->elaborated.discharges();
}
DropElaboratedMir CoroutineElaboratedMir::takeDropElaboratedMir() && noexcept {
  return zc::mv(impl->elaborated);
}
OwnershipCheckedMir CoroutineElaboratedMir::takeCheckedMir() && noexcept {
  return zc::mv(impl->elaborated).takeCheckedMir();
}

ir::IrOperationResult<CoroutineElaboratedMir> CoroutineElaborator::elaborateCoroutines(
    DropElaboratedMir&& elaborated,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository) {
  const auto& builtMir = elaborated.checkedMir().builtMir();
  const auto identities = DropElaborator::retainIdentityAuthority(builtMir);
  if (!DropElaborator::recheckLineage(elaborated.checkedMir(), repository)) {
    return reject<CoroutineElaboratedMir>(builtMir, identities,
                                          ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  // The current MIR subset admits no coroutine suspension points: the closed
  // terminator algebra (Return, Unreachable, Call) cannot represent one. A
  // future coroutine subset must extend the algebra and reject any suspension
  // point here before publication.
  return ir::IrOperationResult<CoroutineElaboratedMir>::verified(
      CoroutineElaboratedMir(zc::heap<CoroutineElaboratedMir::Impl>(zc::mv(elaborated))));
}

struct VerifiedExecutableMir::Impl final {
  explicit Impl(CoroutineElaboratedMir&& elaborated) noexcept : elaborated(zc::mv(elaborated)) {}

  CoroutineElaboratedMir elaborated;
};

VerifiedExecutableMir::VerifiedExecutableMir(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedExecutableMir::~VerifiedExecutableMir() noexcept(false) = default;
VerifiedExecutableMir::VerifiedExecutableMir(VerifiedExecutableMir&&) noexcept = default;
VerifiedExecutableMir& VerifiedExecutableMir::operator=(VerifiedExecutableMir&&) noexcept = default;

identity::SemanticContextBrand VerifiedExecutableMir::semanticContext() const noexcept {
  return impl->elaborated.semanticContext();
}
const identity::ContextFingerprint& VerifiedExecutableMir::contextFingerprint() const noexcept {
  return impl->elaborated.contextFingerprint();
}
identity::ModuleId VerifiedExecutableMir::module() const noexcept {
  return impl->elaborated.module();
}
zc::ArrayPtr<const DropDischargeRecord> VerifiedExecutableMir::discharges() const noexcept {
  return impl->elaborated.discharges();
}
OwnershipCheckedMir VerifiedExecutableMir::takeCheckedMir() && noexcept {
  return zc::mv(impl->elaborated).takeCheckedMir();
}

ir::IrOperationResult<VerifiedExecutableMir> ExecutableMirVerifier::verifyExecutableMir(
    CoroutineElaboratedMir&& elaborated,
    const driver::borrow_evidence::BorrowEvidenceRepositoryCapability& repository) {
  const auto& dropElaborated = elaborated.impl->elaborated;
  const auto& builtMir = dropElaborated.checkedMir().builtMir();
  const auto identities = DropElaborator::retainIdentityAuthority(builtMir);
  if (!DropElaborator::recheckLineage(dropElaborated.checkedMir(), repository)) {
    return reject<VerifiedExecutableMir>(builtMir, identities,
                                         ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  if (!cleanupConsumed(dropElaborated)) {
    return reject<VerifiedExecutableMir>(builtMir, identities, ir::IrFailureKind::InvalidCleanup,
                                         1);
  }
  return ir::IrOperationResult<VerifiedExecutableMir>::verified(
      VerifiedExecutableMir(zc::heap<VerifiedExecutableMir::Impl>(zc::mv(elaborated))));
}

}  // namespace zomlang::compiler::ownership
