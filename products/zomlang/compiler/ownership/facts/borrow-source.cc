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

#include "zomlang/compiler/ownership/facts/borrow-source.h"

#include "zomlang/compiler/ir/ir-diagnostic-adapter.h"
#include "zomlang/compiler/ownership/facts/flow-subset.h"
#include "zomlang/compiler/ownership/source-suppression.h"

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
ir::IrOperationResult<Result> rejectInvariant(const mir::VerifiedBuiltMir& builtMir,
                                              const checker::CheckerIdentityAuthority& identities,
                                              ir::IrFailureKind kind, uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Borrow source failure fallback must be legal");
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
    zc::Vector<ir::IrFailureFact> failureFacts;
    failureFacts.add(zc::mv(admitted).get<ir::AcceptedIrFailureDescriptor>().fact);
    auto facts = ir::SortedIrInvariantFailureFacts::from(zc::mv(failureFacts));
    ZC_IF_SOME(values, facts) {
      return ir::IrOperationResult<Result>::irInvariantRejected(zc::mv(values));
    }
  }
  ZC_UNREACHABLE
}

zc::Maybe<const OwnershipFunctionEventOverlay&> overlayFor(
    const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner) {
  for (const auto& function : overlay.functions()) {
    if (function.owner == owner) return function;
  }
  return zc::none;
}

bool allFunctionsAdmitted(const mir::VerifiedBuiltMir& builtMir) {
  for (const auto& function : builtMir.functions()) {
    if (!isAdmittedFlowSubset(function)) return false;
  }
  return true;
}

zc::Maybe<identity::SourceSpan> sourceSpanFor(const mir::MirFunction& function,
                                              const OwnershipFunctionEventOverlay& overlay,
                                              const MirEventKey& event) {
  for (const auto& source : overlay.sourceMap) {
    if (source.key == event) return source.span.clone();
  }
  if (event.location.owner != function.owner) return zc::none;
  const auto& point = event.location.point;
  if (point.kind() == MirPointKind::Entry) {
    if (event.operandOrdinal >= function.locals.size()) return zc::none;
    return function.locals[event.operandOrdinal].sourceSpan.clone();
  }
  mir::MirBlockId block;
  switch (point.kind()) {
    case MirPointKind::BeforeStatement:
      block = point.beforeStatementValue().block;
      break;
    case MirPointKind::AfterStatement:
      block = point.afterStatementValue().block;
      break;
    case MirPointKind::BeforeTerminator:
      block = point.beforeTerminatorValue().block;
      break;
    case MirPointKind::Edge:
      block = point.edgeValue().from;
      break;
    case MirPointKind::Exit:
      block = point.exitValue().block;
      break;
    case MirPointKind::Entry:
      ZC_UNREACHABLE
  }
  for (const auto& candidate : function.blocks) {
    if (candidate.id != block) continue;
    if (point.kind() == MirPointKind::BeforeStatement) {
      const auto ordinal = point.beforeStatementValue().ordinal;
      if (ordinal >= candidate.statements.size()) return zc::none;
      return candidate.statements[ordinal].sourceSpan().clone();
    }
    if (point.kind() == MirPointKind::AfterStatement) {
      const auto ordinal = point.afterStatementValue().ordinal;
      if (ordinal >= candidate.statements.size()) return zc::none;
      return candidate.statements[ordinal].sourceSpan().clone();
    }
    return candidate.terminator.sourceSpan().clone();
  }
  return zc::none;
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

zc::Maybe<MovePathKey> findMovePath(const VerifiedMovePaths& movePaths, identity::DefId owner,
                                    const mir::MirPlace& place) {
  zc::Maybe<MovePathKey> result;
  for (const auto& function : movePaths.functions()) {
    if (function.owner != owner) continue;
    for (const auto& fact : function.facts) {
      if (!samePlace(fact.key.place, place)) continue;
      if (result != zc::none) return zc::none;
      result = MovePathKey{fact.key.owner, fact.key.place.clone()};
    }
  }
  return result;
}

/// \brief Block position and intra-block index for one MIR point in program order.
struct EventPosition final {
  uint32_t blockIndex;
  uint64_t localIndex;

  bool operator<(const EventPosition& other) const noexcept {
    if (blockIndex != other.blockIndex) return blockIndex < other.blockIndex;
    return localIndex < other.localIndex;
  }
  bool operator<=(const EventPosition& other) const noexcept { return !(other < *this); }
};

zc::Maybe<uint32_t> findBlockIndex(const mir::MirFunction& function, mir::MirBlockId block) {
  for (uint32_t i = 0; i < function.blocks.size(); ++i) {
    if (function.blocks[i].id == block) return i;
  }
  return zc::none;
}

zc::Maybe<EventPosition> eventPosition(const mir::MirFunction& function, const MirPoint& point) {
  switch (point.kind()) {
    case MirPointKind::Entry: {
      if (function.blocks.size() == 0) return zc::none;
      return EventPosition{0, 0};
    }
    case MirPointKind::BeforeStatement: {
      auto idx = findBlockIndex(function, point.beforeStatementValue().block);
      ZC_IF_SOME(value, idx) {
        return EventPosition{value, 1 + 2 * point.beforeStatementValue().ordinal};
      }
      return zc::none;
    }
    case MirPointKind::AfterStatement: {
      auto idx = findBlockIndex(function, point.afterStatementValue().block);
      ZC_IF_SOME(value, idx) {
        return EventPosition{value, 2 + 2 * point.afterStatementValue().ordinal};
      }
      return zc::none;
    }
    case MirPointKind::BeforeTerminator: {
      auto idx = findBlockIndex(function, point.beforeTerminatorValue().block);
      ZC_IF_SOME(value, idx) {
        return EventPosition{value, 1 + 2 * function.blocks[value].statements.size()};
      }
      return zc::none;
    }
    case MirPointKind::Edge: {
      auto idx = findBlockIndex(function, point.edgeValue().from);
      ZC_IF_SOME(value, idx) {
        return EventPosition{value, 2 + 2 * function.blocks[value].statements.size()};
      }
      return zc::none;
    }
    case MirPointKind::Exit: {
      auto idx = findBlockIndex(function, point.exitValue().block);
      ZC_IF_SOME(value, idx) {
        return EventPosition{value, 3 + 2 * function.blocks[value].statements.size()};
      }
      return zc::none;
    }
  }
  return zc::none;
}

/// \brief Returns true when left comes strictly before right in one function's program order.
bool beforeInFunction(const mir::MirFunction& function, const MirEventKey& left,
                      const MirEventKey& right) {
  auto leftPos = eventPosition(function, left.location.point);
  auto rightPos = eventPosition(function, right.location.point);
  ZC_IF_SOME(leftValue, leftPos) {
    ZC_IF_SOME(rightValue, rightPos) {
      if (leftValue < rightValue) return true;
      if (rightValue < leftValue) return false;
      return left.operandOrdinal < right.operandOrdinal;
    }
  }
  return false;
}

/// \brief Finds the last read of one destination local in program order.
zc::Maybe<MirEventKey> lastUseOfDestination(const mir::MirFunction& function,
                                            const mir::MirPlace& destination) {
  zc::Maybe<MirEventKey> lastUse;
  auto consider = [&](const MirEventKey& event) {
    ZC_IF_SOME(current, lastUse) {
      if (beforeInFunction(function, current, event)) { lastUse = event; }
      return;
    }
    lastUse = event;
  };
  for (const auto& block : function.blocks) {
    for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
      const auto& statement = block.statements[ordinal];
      if (statement.kind() == mir::MirStatementKind::Assign) {
        const auto& assignment = statement.assignmentValue();
        if (assignment.value.kind() == mir::MirRvalueKind::Use) {
          const auto& operand = assignment.value.useValue().operand;
          if ((operand.kind() == mir::MirOperandKind::Copy ||
               operand.kind() == mir::MirOperandKind::Move) &&
              operand.place().local() == destination.local()) {
            consider(MirEventKey{
                MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 0});
          }
        }
      }
      if (statement.kind() == mir::MirStatementKind::BorrowCreation) {
        const auto& borrow = statement.borrowCreationValue();
        if (borrow.source.local() == destination.local()) {
          consider(MirEventKey{
              MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 1});
        }
      }
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
      const auto& ret = block.terminator.returnValue();
      if (ret.value != zc::none) {
        ZC_IF_SOME(value, ret.value) {
          if ((value.kind() == mir::MirOperandKind::Copy ||
               value.kind() == mir::MirOperandKind::Move) &&
              value.place().local() == destination.local()) {
            consider(
                MirEventKey{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0});
          }
        }
      }
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
      const auto& call = block.terminator.callValue();
      for (uint32_t argIndex = 0; argIndex < call.arguments.size(); ++argIndex) {
        const auto& arg = call.arguments[argIndex];
        if ((arg.kind() == mir::MirOperandKind::Copy || arg.kind() == mir::MirOperandKind::Move) &&
            arg.place().local() == destination.local()) {
          consider(MirEventKey{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                               argIndex});
        }
      }
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
      const auto& discriminant = block.terminator.switchIntValue().discriminant;
      if ((discriminant.kind() == mir::MirOperandKind::Copy ||
           discriminant.kind() == mir::MirOperandKind::Move) &&
          discriminant.place().local() == destination.local()) {
        consider(MirEventKey{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0});
      }
    }
  }
  return lastUse;
}

/// \brief Converts a loan's AfterEvent activation point to a comparable event key.
zc::Maybe<MirEventKey> activationEvent(const mir::MirFunction& function, const LoanFact& loan) {
  if (loan.activeFrom.kind() != OwnershipPointKind::AfterEvent) return zc::none;
  const auto& issue = loan.activeFrom.afterEventValue().event;
  const auto& point = issue.location.point;
  if (point.kind() == MirPointKind::BeforeStatement) {
    return MirEventKey{
        MirLocation{function.owner, MirPoint::afterStatement(point.beforeStatementValue().block,
                                                             point.beforeStatementValue().ordinal)},
        0};
  }
  return zc::none;
}

/// \brief Returns true when the loan is live at the given event.
///
/// The loan is live from its activation point (AfterEvent of the issue, or
/// AfterEvent of a deferred receiver activation) until the last read of its
/// destination temporary. This is the event-granular NLL liveness region:
/// every event at or after activation and at or before the last destination
/// read carries the loan.
bool loanActiveAt(const mir::MirFunction& function, const LoanFact& loan,
                  const MirEventKey& event) {
  auto activation = activationEvent(function, loan);
  ZC_IF_SOME(activationValue, activation) {
    if (beforeInFunction(function, event, activationValue)) return false;
    auto lastUse = lastUseOfDestination(function, loan.destination.place);
    ZC_IF_SOME(lastUseValue, lastUse) { return !beforeInFunction(function, lastUseValue, event); }
  }
  return false;
}

/// \brief Returns true when the borrow source is a reborrow of the loan destination.
bool isReborrowOf(const mir::MirPlace& borrowSource, const mir::MirPlace& loanDestination) {
  if (borrowSource.local() != loanDestination.local()) return false;
  if (loanDestination.projections().size() != 0) return false;
  if (borrowSource.projections().size() != 1) return false;
  return borrowSource.projections()[0].kind() == mir::MirProjectionKind::Dereference;
}

/// \brief Emits BorrowDoesNotLiveLongEnoughFailure for each returned local borrow.
///
/// A returned reference may originate only from a parameter or receiver. A
/// local borrow that escapes through the return has a referent whose storage
/// dies before the caller can use the reference, so the source is rejected.
void escapeFailures(const mir::VerifiedBuiltMir& builtMir,
                    const VerifiedOwnershipEventOverlay& overlay,
                    const VerifiedReferenceDefinitions& references,
                    zc::Vector<OwnershipSourceFailure>& failures, uint32_t& traversalOrdinal) {
  for (const auto& definition : references.definitions()) {
    if (!definition.origin.detail.is<LocalReferenceOrigin>()) continue;
    zc::Maybe<const mir::MirFunction&> mirFunction;
    for (const auto& candidate : builtMir.functions()) {
      if (candidate.owner == definition.owner) {
        mirFunction = candidate;
        break;
      }
    }
    if (mirFunction == zc::none) continue;
    auto functionOverlay = overlayFor(overlay, definition.owner);
    if (functionOverlay == zc::none) continue;
    ZC_IF_SOME(function, mirFunction) {
      ZC_IF_SOME(functionOverlayValue, functionOverlay) {
        auto returnSpan = sourceSpanFor(function, functionOverlayValue, definition.returned);
        auto borrowSpan = sourceSpanFor(function, functionOverlayValue, definition.loan);
        if (returnSpan == zc::none || borrowSpan == zc::none) continue;
        ZC_IF_SOME(returnSpanValue, returnSpan) {
          ZC_IF_SOME(borrowSpanValue, borrowSpan) {
            zc::Vector<EscapeFailureCause> causes;
            causes.add(EscapeFailureCause{definition.loan, zc::mv(borrowSpanValue)});
            failures.add(BorrowDoesNotLiveLongEnoughFailure{
                definition.owner, definition.returned, zc::mv(returnSpanValue),
                MovePathKey{definition.origin.referent.owner,
                            definition.origin.referent.place.clone()},
                traversalOrdinal++, zc::mv(causes)});
          }
        }
      }
    }
  }
}

/// \brief Emits borrow-conflict failures for BorrowCreation statements with active overlapping
/// loans.
///
/// A mutable borrow conflicts with every active overlapping loan. A shared
/// borrow conflicts only with an active mutable loan. Reborrows of a loan
/// destination suspend the parent rather than conflicting with it.
void borrowConflictFailures(const mir::VerifiedBuiltMir& builtMir,
                            const VerifiedOwnershipEventOverlay& overlay,
                            const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
                            zc::Vector<OwnershipSourceFailure>& failures,
                            uint32_t& traversalOrdinal) {
  for (const auto& function : builtMir.functions()) {
    auto functionOverlay = overlayFor(overlay, function.owner);
    if (functionOverlay == zc::none) continue;
    ZC_IF_SOME(functionOverlayValue, functionOverlay) {
      for (const auto& block : function.blocks) {
        for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
          const auto& statement = block.statements[ordinal];
          if (statement.kind() != mir::MirStatementKind::BorrowCreation) continue;
          const auto& borrow = statement.borrowCreationValue();
          const MirEventKey borrowEvent{
              MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 1};
          zc::Vector<LoanFailureCause> causes;
          for (const auto& loan : loans.loans()) {
            if (loan.owner != function.owner) continue;
            if (isReborrowOf(borrow.source, loan.destination.place)) continue;
            if (!loanActiveAt(function, loan, borrowEvent)) continue;
            if (!placesConflict(borrow.source, loan.source.place)) continue;
            if (borrow.kind == mir::MirBorrowKind::Shared &&
                loan.kind != mir::MirBorrowKind::Mutable) {
              continue;
            }
            auto loanSpan = sourceSpanFor(function, functionOverlayValue, loan.issue);
            if (loanSpan == zc::none) continue;
            ZC_IF_SOME(spanValue, loanSpan) {
              causes.add(LoanFailureCause{LoanKey{loan.issue},
                                          MovePathKey{loan.source.owner, loan.source.place.clone()},
                                          loan.issue, zc::mv(spanValue)});
            }
          }
          if (causes.size() == 0) continue;
          auto sourcePath = findMovePath(movePaths, function.owner, borrow.source);
          if (sourcePath == zc::none) continue;
          auto borrowSpan = sourceSpanFor(function, functionOverlayValue, borrowEvent);
          if (borrowSpan == zc::none) continue;
          ZC_IF_SOME(pathValue, sourcePath) {
            ZC_IF_SOME(spanValue, borrowSpan) {
              if (borrow.kind == mir::MirBorrowKind::Mutable) {
                failures.add(MutableBorrowConflictFailure{
                    function.owner, borrowEvent, zc::mv(spanValue),
                    MovePathKey{pathValue.owner, pathValue.place.clone()}, traversalOrdinal++,
                    zc::mv(causes)});
              } else {
                failures.add(SharedBorrowConflictFailure{
                    function.owner, borrowEvent, zc::mv(spanValue),
                    MovePathKey{pathValue.owner, pathValue.place.clone()}, traversalOrdinal++,
                    zc::mv(causes)});
              }
            }
          }
        }
      }
    }
  }
}

/// \brief Emits MoveOutOfBorrowFailure for Move operands whose place carries an active loan.
void moveOutOfBorrowFailures(const mir::VerifiedBuiltMir& builtMir,
                             const VerifiedOwnershipEventOverlay& overlay,
                             const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
                             zc::Vector<OwnershipSourceFailure>& failures,
                             uint32_t& traversalOrdinal) {
  for (const auto& function : builtMir.functions()) {
    auto functionOverlay = overlayFor(overlay, function.owner);
    if (functionOverlay == zc::none) continue;
    ZC_IF_SOME(functionOverlayValue, functionOverlay) {
      for (const auto& block : function.blocks) {
        for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
          const auto& statement = block.statements[ordinal];
          if (statement.kind() != mir::MirStatementKind::Assign) continue;
          const auto& assignment = statement.assignmentValue();
          if (assignment.value.kind() != mir::MirRvalueKind::Use) continue;
          const auto& operand = assignment.value.useValue().operand;
          if (operand.kind() != mir::MirOperandKind::Move) continue;
          const MirEventKey moveEvent{
              MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 0};
          zc::Vector<LoanFailureCause> causes;
          for (const auto& loan : loans.loans()) {
            if (loan.owner != function.owner) continue;
            if (!loanActiveAt(function, loan, moveEvent)) continue;
            if (!placesConflict(operand.place(), loan.source.place)) continue;
            auto loanSpan = sourceSpanFor(function, functionOverlayValue, loan.issue);
            if (loanSpan == zc::none) continue;
            ZC_IF_SOME(spanValue, loanSpan) {
              causes.add(LoanFailureCause{LoanKey{loan.issue},
                                          MovePathKey{loan.source.owner, loan.source.place.clone()},
                                          loan.issue, zc::mv(spanValue)});
            }
          }
          if (causes.size() == 0) continue;
          auto movePath = findMovePath(movePaths, function.owner, operand.place());
          if (movePath == zc::none) continue;
          auto moveSpan = sourceSpanFor(function, functionOverlayValue, moveEvent);
          if (moveSpan == zc::none) continue;
          ZC_IF_SOME(pathValue, movePath) {
            ZC_IF_SOME(spanValue, moveSpan) {
              failures.add(
                  MoveOutOfBorrowFailure{function.owner, moveEvent, zc::mv(spanValue),
                                         MovePathKey{pathValue.owner, pathValue.place.clone()},
                                         traversalOrdinal++, zc::mv(causes)});
            }
          }
        }
        if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
          const auto& ret = block.terminator.returnValue();
          if (ret.value == zc::none) continue;
          ZC_IF_SOME(value, ret.value) {
            if (value.kind() != mir::MirOperandKind::Move) continue;
            const MirEventKey moveEvent{
                MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0};
            zc::Vector<LoanFailureCause> causes;
            for (const auto& loan : loans.loans()) {
              if (loan.owner != function.owner) continue;
              if (!loanActiveAt(function, loan, moveEvent)) continue;
              if (!placesConflict(value.place(), loan.source.place)) continue;
              auto loanSpan = sourceSpanFor(function, functionOverlayValue, loan.issue);
              if (loanSpan == zc::none) continue;
              ZC_IF_SOME(spanValue, loanSpan) {
                causes.add(LoanFailureCause{
                    LoanKey{loan.issue}, MovePathKey{loan.source.owner, loan.source.place.clone()},
                    loan.issue, zc::mv(spanValue)});
              }
            }
            if (causes.size() == 0) continue;
            auto movePath = findMovePath(movePaths, function.owner, value.place());
            if (movePath == zc::none) continue;
            auto moveSpan = sourceSpanFor(function, functionOverlayValue, moveEvent);
            if (moveSpan == zc::none) continue;
            ZC_IF_SOME(pathValue, movePath) {
              ZC_IF_SOME(spanValue, moveSpan) {
                failures.add(
                    MoveOutOfBorrowFailure{function.owner, moveEvent, zc::mv(spanValue),
                                           MovePathKey{pathValue.owner, pathValue.place.clone()},
                                           traversalOrdinal++, zc::mv(causes)});
              }
            }
          }
        }
        if (block.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
          const auto& discriminant = block.terminator.switchIntValue().discriminant;
          if (discriminant.kind() == mir::MirOperandKind::Move) {
            const MirEventKey moveEvent{
                MirLocation{function.owner, MirPoint::beforeTerminator(block.id)}, 0};
            zc::Vector<LoanFailureCause> causes;
            for (const auto& loan : loans.loans()) {
              if (loan.owner != function.owner) continue;
              if (!loanActiveAt(function, loan, moveEvent)) continue;
              if (!placesConflict(discriminant.place(), loan.source.place)) continue;
              auto loanSpan = sourceSpanFor(function, functionOverlayValue, loan.issue);
              if (loanSpan == zc::none) continue;
              ZC_IF_SOME(spanValue, loanSpan) {
                causes.add(LoanFailureCause{
                    LoanKey{loan.issue}, MovePathKey{loan.source.owner, loan.source.place.clone()},
                    loan.issue, zc::mv(spanValue)});
              }
            }
            if (causes.size() != 0) {
              auto movePath = findMovePath(movePaths, function.owner, discriminant.place());
              if (movePath != zc::none) {
                auto moveSpan = sourceSpanFor(function, functionOverlayValue, moveEvent);
                if (moveSpan != zc::none) {
                  ZC_IF_SOME(pathValue, movePath) {
                    ZC_IF_SOME(spanValue, moveSpan) {
                      failures.add(MoveOutOfBorrowFailure{
                          function.owner, moveEvent, zc::mv(spanValue),
                          MovePathKey{pathValue.owner, pathValue.place.clone()}, traversalOrdinal++,
                          zc::mv(causes)});
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

zc::Maybe<zc::Vector<OwnershipSourceFailure>> borrowSourceFailures(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references) {
  // eventPosition and beforeInFunction compute program order as (blockIndex,
  // localIndex), a linear block-order assumption that is not branch-aware. The
  // admitted subset admits branches, so this ordering is only sound for the
  // current single-path production MIR; branch-aware ordering is tracked
  // separately.
  if (!allFunctionsAdmitted(builtMir)) return zc::none;
  zc::Vector<OwnershipSourceFailure> failures;
  uint32_t traversalOrdinal = 0;
  escapeFailures(builtMir, overlay, references, failures, traversalOrdinal);
  borrowConflictFailures(builtMir, overlay, movePaths, loans, failures, traversalOrdinal);
  moveOutOfBorrowFailures(builtMir, overlay, movePaths, loans, failures, traversalOrdinal);
  return failures;
}

}  // namespace

BorrowSourceVerificationResult BorrowSourceVerifier::verify(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedMovePaths& movePaths, const VerifiedLoanFacts& loans,
    const VerifiedReferenceDefinitions& references) {
  if (movePaths.semanticContext() != builtMir.semanticContext() ||
      movePaths.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      movePaths.module() != builtMir.module() ||
      movePaths.builtRevision().digest() != builtMir.revision().digest() ||
      movePaths.overlayRevision().digest() != overlay.revision().digest() ||
      loans.semanticContext() != builtMir.semanticContext() ||
      loans.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      loans.module() != builtMir.module() ||
      loans.builtRevision().digest() != builtMir.revision().digest() ||
      loans.overlayRevision().digest() != overlay.revision().digest() ||
      references.semanticContext() != builtMir.semanticContext() ||
      references.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      references.module() != builtMir.module() ||
      references.builtRevision().digest() != builtMir.revision().digest() ||
      references.overlayRevision().digest() != overlay.revision().digest() ||
      overlay.semanticContext() != builtMir.semanticContext() ||
      overlay.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      overlay.module() != builtMir.module() ||
      overlay.builtRevision().digest() != builtMir.revision().digest()) {
    const auto identities = builtMir.retainIdentityAuthority();
    return reject(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  if (!allFunctionsAdmitted(builtMir)) {
    const auto identities = builtMir.retainIdentityAuthority();
    return reject(builtMir, identities, ir::IrFailureKind::InvalidControlFlow, 2);
  }
  auto failures = borrowSourceFailures(builtMir, overlay, movePaths, loans, references);
  if (failures == zc::none) {
    const auto identities = builtMir.retainIdentityAuthority();
    return reject(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof, 0);
  }
  ZC_IF_SOME(values, failures) {
    auto suppressed = SourceSuppression::suppress(zc::mv(values));
    auto deduplicated = OwnershipSourceFailureOrdering::deduplicate(zc::mv(suppressed));
    auto sorted =
        ir::SortedSourceFailureFacts<OwnershipSourceFailure, OwnershipSourceFailureOrdering>::from(
            zc::mv(deduplicated));
    ZC_IF_SOME(value, sorted) {
      return BorrowSourceVerificationResult::sourceRejected(zc::mv(value));
    }
  }
  return BorrowSourceVerificationResult::verified(BorrowSourceAccepted{});
}

BorrowSourceVerificationResult BorrowSourceVerifier::reject(
    const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities,
    ir::IrFailureKind kind, uint32_t ordinal) {
  auto rejected = rejectInvariant<BorrowSourceAccepted>(builtMir, identities, kind, ordinal);
  if (rejected.isIdentityInvariantRejected()) {
    return BorrowSourceVerificationResult::identityInvariantRejected(
        zc::mv(rejected).takeIdentityFailures());
  }
  if (rejected.isIrInvariantRejected()) {
    return BorrowSourceVerificationResult::irInvariantRejected(
        zc::mv(rejected).takeInvariantFailures());
  }
  ZC_UNREACHABLE
}

}  // namespace zomlang::compiler::ownership::facts
