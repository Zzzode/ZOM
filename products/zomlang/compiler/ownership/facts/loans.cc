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

#include "zomlang/compiler/ownership/facts/loans.h"

#include "zomlang/compiler/driver/borrow-evidence.h"
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
  ZC_IREQUIRE(fallback != zc::none, "Loan fact failure fallback must be legal");
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

MovePathKey cloneMovePathKey(const MovePathKey& key) {
  return MovePathKey{key.owner, key.place.clone()};
}

bool sameMovePathKey(const MovePathKey& left, const MovePathKey& right) {
  return left.owner == right.owner && samePlace(left.place, right.place);
}

bool hasBorrowIssue(const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner,
                    const MirPoint& point) {
  for (const auto& function : overlay.functions()) {
    if (function.owner != owner) continue;
    size_t matches = 0;
    for (const auto& slot : function.slots) {
      if (slot.key.location.point != point || slot.key.operandOrdinal != 1 ||
          slot.stage != OwnershipEventStage::Effect || slot.roles.size() != 2 ||
          slot.roles[0] != OwnershipEventRole::Operation ||
          slot.roles[1] != OwnershipEventRole::BorrowIssue) {
        continue;
      }
      ++matches;
    }
    return matches == 1;
  }
  return false;
}

bool hasBorrowCommit(const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner,
                     const MirPoint& point) {
  for (const auto& function : overlay.functions()) {
    if (function.owner != owner) continue;
    size_t matches = 0;
    for (const auto& slot : function.slots) {
      if (slot.key.location.point != point || slot.key.operandOrdinal != 2 ||
          slot.stage != OwnershipEventStage::Commit || slot.roles.size() != 1 ||
          slot.roles[0] != OwnershipEventRole::DestinationWrite) {
        continue;
      }
      ++matches;
    }
    return matches == 1;
  }
  return false;
}

size_t borrowActivationCount(const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner,
                             const MirPoint& point) {
  for (const auto& function : overlay.functions()) {
    if (function.owner != owner) continue;
    size_t matches = 0;
    for (const auto& slot : function.slots) {
      if (slot.key.location.point != point || slot.key.operandOrdinal != 1 ||
          slot.stage != OwnershipEventStage::Commit || slot.roles.size() != 1 ||
          slot.roles[0] != OwnershipEventRole::BorrowActivation) {
        continue;
      }
      ++matches;
    }
    return matches;
  }
  return 0;
}

bool hasValidDeferredActivations(const mir::VerifiedBuiltMir& builtMir,
                                 const VerifiedOwnershipEventOverlay& overlay) {
  size_t expectedFacts = 0;
  for (const auto& function : builtMir.functions()) {
    zc::Maybe<const OwnershipFunctionEventOverlay&> functionOverlay;
    for (const auto& candidate : overlay.functions()) {
      if (candidate.owner != function.owner) continue;
      if (functionOverlay != zc::none) return false;
      functionOverlay = candidate;
    }
    if (functionOverlay == zc::none) return false;
    for (const auto& block : function.blocks) {
      if (block.terminator.kind() != mir::MirTerminatorKind::Call) continue;
      const auto& call = block.terminator.callValue();
      const auto activationPoint = MirPoint::edge(block.id, 0, call.normalTarget);
      if (call.effect.kind() == mir::MirCallEffectKind::NoActivation) {
        if (borrowActivationCount(overlay, function.owner, activationPoint) != 0) return false;
        continue;
      }
      auto activated = call.effect.activatedMutableReceiver();
      if (activated == zc::none ||
          borrowActivationCount(overlay, function.owner, activationPoint) != 1) {
        return false;
      }
      zc::Maybe<uint32_t> issueOrdinal;
      for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        const auto& statement = block.statements[ordinal];
        if (statement.kind() != mir::MirStatementKind::BorrowCreation ||
            statement.borrowCreationValue().kind != mir::MirBorrowKind::Mutable ||
            statement.borrowCreationValue().destination.local() != ZC_ASSERT_NONNULL(activated)) {
          continue;
        }
        if (issueOrdinal != zc::none) return false;
        issueOrdinal = ordinal;
      }
      if (issueOrdinal == zc::none) return false;
      const auto& borrow = block.statements[ZC_ASSERT_NONNULL(issueOrdinal)].borrowCreationValue();
      const MirEventKey issue{
          MirLocation{function.owner,
                      MirPoint::beforeStatement(block.id, ZC_ASSERT_NONNULL(issueOrdinal))},
          1};
      const MirEventKey receiverSource{
          MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0};
      const MirEventKey activation{MirLocation{function.owner, activationPoint}, 1};
      size_t matches = 0;
      ZC_IF_SOME(value, functionOverlay) {
        for (const auto& fact : value.deferredActivations) {
          if (fact.loan.issue != issue) continue;
          if (fact.receiverSource != receiverSource || fact.activation != activation ||
              fact.receiverMode != checker::checked::ReceiverMode::Mutable ||
              fact.adjustmentSource != borrow.source.resultType() ||
              fact.adjustmentDestination != borrow.destination.resultType() ||
              fact.adjustmentSteps.size() != 1 ||
              fact.adjustmentSteps[0] != checker::checked::ReceiverAdjustmentStep::BorrowMutable) {
            return false;
          }
          ++matches;
        }
      }
      if (matches != 1) return false;
      ++expectedFacts;
    }
  }
  size_t actualFacts = 0;
  for (const auto& function : overlay.functions())
    actualFacts += function.deferredActivations.size();
  return actualFacts == expectedFacts;
}

zc::Maybe<MirEventKey> deferredActivationFor(const VerifiedOwnershipEventOverlay& overlay,
                                             identity::DefId owner, const MirEventKey& issue) {
  zc::Maybe<MirEventKey> activation;
  for (const auto& function : overlay.functions()) {
    if (function.owner != owner) continue;
    for (const auto& fact : function.deferredActivations) {
      if (fact.loan.issue != issue) continue;
      if (activation != zc::none) return zc::none;
      activation = fact.activation;
    }
  }
  return activation;
}

zc::Maybe<MovePathKey> findMovePath(const VerifiedMovePaths& movePaths, identity::DefId owner,
                                    const mir::MirPlace& place) {
  zc::Maybe<MovePathKey> result;
  for (const auto& function : movePaths.functions()) {
    if (function.owner != owner) continue;
    for (const auto& fact : function.facts) {
      if (!samePlace(fact.key.place, place)) continue;
      if (result != zc::none) return zc::none;
      result = cloneMovePathKey(fact.key);
    }
  }
  return result;
}

zc::Maybe<zc::Vector<LoanFact>> derive(const VerifiedMovePaths& movePaths,
                                       const mir::VerifiedBuiltMir& builtMir,
                                       const VerifiedOwnershipEventOverlay& overlay) {
  if (!hasValidDeferredActivations(builtMir, overlay)) return zc::none;
  zc::Vector<LoanFact> loans;
  for (const auto& function : builtMir.functions()) {
    for (const auto& block : function.blocks) {
      for (size_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        const auto& statement = block.statements[ordinal];
        if (statement.kind() != mir::MirStatementKind::BorrowCreation) continue;
        const auto& borrow = statement.borrowCreationValue();
        if (!borrow.source.hasConsistentTypeChain() ||
            !borrow.destination.hasConsistentTypeChain()) {
          return zc::none;
        }
        auto point = MirPoint::beforeStatement(block.id, static_cast<uint32_t>(ordinal));
        if (!hasBorrowIssue(overlay, function.owner, point) ||
            !hasBorrowCommit(overlay, function.owner, point)) {
          return zc::none;
        }
        auto source = findMovePath(movePaths, function.owner, borrow.source);
        auto destination = findMovePath(movePaths, function.owner, borrow.destination);
        if (source == zc::none || destination == zc::none) return zc::none;
        const MirEventKey issue{MirLocation{function.owner, point}, 1};
        auto activation = deferredActivationFor(overlay, function.owner, issue);
        OwnershipPoint activeFrom = OwnershipPoint::afterEvent(issue);
        ZC_IF_SOME(event, activation) { activeFrom = OwnershipPoint::afterEvent(zc::mv(event)); }
        ZC_IF_SOME(sourcePath, source) {
          ZC_IF_SOME(destinationPath, destination) {
            loans.add(LoanFact{function.owner, MirEventKey{MirLocation{function.owner, point}, 1},
                               MirEventKey{MirLocation{function.owner, point}, 2}, borrow.kind,
                               zc::mv(activeFrom), zc::mv(sourcePath), zc::mv(destinationPath)});
          }
        }
      }
    }
  }
  return loans;
}

bool sameLoans(zc::ArrayPtr<const LoanFact> left, zc::ArrayPtr<const LoanFact> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].owner != right[index].owner || left[index].kind != right[index].kind ||
        left[index].issue != right[index].issue || left[index].commit != right[index].commit ||
        left[index].activeFrom != right[index].activeFrom ||
        !sameMovePathKey(left[index].source, right[index].source) ||
        !sameMovePathKey(left[index].destination, right[index].destination)) {
      return false;
    }
  }
  return true;
}

bool inputsMatch(const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return movePaths.semanticContext() == builtMir.semanticContext() &&
         movePaths.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         movePaths.module() == builtMir.module() &&
         movePaths.builtRevision().digest() == builtMir.revision().digest() &&
         movePaths.overlayRevision().digest() == overlay.revision().digest() &&
         builtMir.semanticContext() == overlay.semanticContext() &&
         builtMir.contextFingerprint().digest() == overlay.contextFingerprint().digest() &&
         builtMir.module() == overlay.module() &&
         builtMir.checkedFactsRevision().digest() == overlay.checkedFactsRevision().digest() &&
         builtMir.revision().digest() == overlay.builtRevision().digest();
}

}  // namespace

LoanCandidate::LoanCandidate(identity::SemanticContextBrand semanticContext,
                             identity::ContextFingerprint&& contextFingerprint,
                             identity::ModuleId module, mir::MirRevisionId builtRevision,
                             OwnershipEventOverlayRevision overlayRevision,
                             driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
                             zc::Vector<LoanFact>&& loans) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      loans(zc::mv(loans)) {}

struct VerifiedLoanFacts::Impl final {
  explicit Impl(LoanCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  LoanCandidate candidate;
};

VerifiedLoanFacts::VerifiedLoanFacts(zc::Own<Impl>&& impl) noexcept : impl(zc::mv(impl)) {}
VerifiedLoanFacts::~VerifiedLoanFacts() noexcept(false) = default;
VerifiedLoanFacts::VerifiedLoanFacts(VerifiedLoanFacts&&) noexcept = default;
VerifiedLoanFacts& VerifiedLoanFacts::operator=(VerifiedLoanFacts&&) noexcept = default;
identity::SemanticContextBrand VerifiedLoanFacts::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedLoanFacts::contextFingerprint() const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedLoanFacts::module() const noexcept { return impl->candidate.module; }
const mir::MirRevisionId& VerifiedLoanFacts::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedLoanFacts::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision& VerifiedLoanFacts::borrowEvidenceRevision()
    const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const LoanFact> VerifiedLoanFacts::loans() const noexcept {
  return impl->candidate.loans.asPtr();
}

ir::IrOperationResult<LoanCandidate> LoanBuilder::build(
    const VerifiedMovePaths& movePaths, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(movePaths, builtMir, overlay)) {
    return reject<LoanCandidate>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto loans = derive(movePaths, builtMir, overlay);
  if (loans == zc::none) {
    return reject<LoanCandidate>(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  ZC_IF_SOME(value, loans) {
    return ir::IrOperationResult<LoanCandidate>::verified(LoanCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), builtMir.borrowEvidenceRevision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedLoanFacts> LoanVerifier::verify(
    LoanCandidate&& candidate, const VerifiedMovePaths& movePaths,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(movePaths, builtMir, overlay)) {
    return reject<VerifiedLoanFacts>(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch,
                                     0);
  }
  auto expected = derive(movePaths, builtMir, overlay);
  if (expected == zc::none || !sameLoans(candidate.loans, ZC_ASSERT_NONNULL(expected))) {
    return reject<VerifiedLoanFacts>(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof,
                                     1);
  }
  return ir::IrOperationResult<VerifiedLoanFacts>::verified(
      VerifiedLoanFacts(zc::heap<VerifiedLoanFacts::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
