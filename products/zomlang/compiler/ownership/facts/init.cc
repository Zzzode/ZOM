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

#include "zomlang/compiler/ownership/facts/init.h"

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
ir::IrOperationResult<Result> rejectInvariant(const mir::VerifiedBuiltMir& builtMir,
                                              const checker::CheckerIdentityAuthority& identities,
                                              ir::IrFailureKind kind, uint32_t ordinal) {
  identity::DefId definition;
  if (builtMir.functions().size() != 0) definition = builtMir.functions()[0].owner;
  AuthorityIdentityResolver resolver(identities);
  auto fallback = ir::IrFailureFallbackContext::from(ir::IrFailurePhase::OwnershipProofValidation,
                                                     ir::IrFailureOwner::definition(definition));
  ZC_IREQUIRE(fallback != zc::none, "Initialization failure fallback must be legal");
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

bool validState(InitializationState state) {
  return state.storageLive ? (!state.mustBeInitialized || state.mayBeInitialized)
                           : !state.mayBeInitialized && !state.mustBeInitialized;
}

bool samePlace(const mir::MirPlace& left, const mir::MirPlace& right);

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

struct InitializationPathState final {
  InitializationState state;
  zc::Vector<InitializationLossCause> lossCauses;
};

zc::Vector<InitializationLossCause> oneLossCause(InitializationLossKind kind, MirEventKey&& event,
                                                 mir::MirLocalId local) {
  zc::Vector<InitializationLossCause> causes;
  causes.add(InitializationLossCause{kind, zc::mv(event), local});
  return causes;
}

zc::Vector<InitializationLossCause> cloneLossCauses(
    zc::ArrayPtr<const InitializationLossCause> causes) {
  zc::Vector<InitializationLossCause> cloned;
  for (const auto& cause : causes) cloned.add(cause);
  return cloned;
}

MirEventKey eventAt(identity::DefId owner, MirPoint&& point, uint32_t ordinal) {
  return MirEventKey{MirLocation{owner, zc::mv(point)}, ordinal};
}

void setUnavailable(InitializationPathState& state, InitializationLossKind kind,
                    MirEventKey&& event, mir::MirLocalId local, InitializationState unavailable) {
  state.state = unavailable;
  state.lossCauses = oneLossCause(kind, zc::mv(event), local);
}

void setInitialized(InitializationPathState& state) {
  state.state = InitializationState::initialized();
  zc::Vector<InitializationLossCause> noCauses;
  state.lossCauses = zc::mv(noCauses);
}

zc::Maybe<size_t> localIndex(const mir::MirFunction& function, mir::MirLocalId local) {
  for (size_t index = 0; index < function.locals.size(); ++index) {
    if (function.locals[index].id == local) return index;
  }
  return zc::none;
}

zc::Maybe<size_t> pathIndex(const MovePathFunction& paths, const mir::MirPlace& place) {
  for (size_t index = 0; index < paths.facts.size(); ++index) {
    if (samePlace(paths.facts[index].key.place, place)) return index;
  }
  return zc::none;
}

MirEventKey cloneEvent(const MirEventKey& event) {
  return eventAt(event.location.owner, MirPoint(event.location.point), event.operandOrdinal);
}

bool isPathWithin(const mir::MirPlace& root, const mir::MirPlace& candidate) {
  if (root.local() != candidate.local() || root.rootType() != candidate.rootType() ||
      root.projections().size() > candidate.projections().size()) {
    return false;
  }
  for (size_t index = 0; index < root.projections().size(); ++index) {
    if (!sameProjection(root.projections()[index], candidate.projections()[index])) return false;
  }
  return true;
}

bool validLocalPlace(const mir::MirFunction& function, const mir::MirPlace& place) {
  if (!place.hasConsistentTypeChain()) return false;
  auto index = localIndex(function, place.local());
  if (index == zc::none) return false;
  ZC_IF_SOME(value, index) {
    const auto& local = function.locals[value];
    if (place.rootType() != local.type) return false;
    for (const auto& projection : place.projections()) {
      if (!projection.isStructurallyValid()) return false;
      if (projection.kind() != mir::MirProjectionKind::Index) continue;
      if (localIndex(function, projection.indexValue().index) == zc::none) return false;
    }
    return place.projections().size() != 0 || place.resultType() == local.type;
  }
  ZC_UNREACHABLE
}

bool sameLossCause(const InitializationLossCause& left, const InitializationLossCause& right) {
  return left.kind == right.kind && left.local == right.local && left.event == right.event;
}

bool validLossCause(const InitializationLossCause& cause, identity::DefId owner,
                    mir::MirLocalId local) {
  if (cause.local != local || cause.event.location.owner != owner || !cause.local.isValid()) {
    return false;
  }
  switch (cause.kind) {
    case InitializationLossKind::NeverInitialized:
    case InitializationLossKind::Moved:
    case InitializationLossKind::Deinitialized:
    case InitializationLossKind::StorageEnded:
      return true;
  }
  return false;
}

bool sameLossCauses(zc::ArrayPtr<const InitializationLossCause> left,
                    zc::ArrayPtr<const InitializationLossCause> right, identity::DefId owner,
                    mir::MirLocalId local, InitializationState state) {
  if (left.size() != right.size() ||
      (state == InitializationState::initialized() ? left.size() != 0 : left.size() == 0)) {
    return false;
  }
  for (size_t index = 0; index < left.size(); ++index) {
    if (!validLossCause(left[index], owner, local) || !validLossCause(right[index], owner, local) ||
        !sameLossCause(left[index], right[index])) {
      return false;
    }
  }
  return true;
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

MovePathKey rootKey(identity::DefId owner, const mir::MirLocalDeclaration& local) {
  zc::Vector<mir::MirProjection> projections;
  return MovePathKey{owner, mir::MirPlace(local.id, local.type, zc::mv(projections), local.type)};
}

MovePathKey cloneKey(const MovePathKey& key) { return MovePathKey{key.owner, key.place.clone()}; }

zc::Maybe<const OwnershipFunctionEventOverlay&> overlayFor(
    const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner) {
  for (const auto& function : overlay.functions()) {
    if (function.owner == owner) return function;
  }
  return zc::none;
}

bool hasInitializationPlan(const OwnershipFunctionEventOverlay& overlay,
                           const MirEventKey& initialization, const mir::MirPlace& root) {
  bool found = false;
  for (const auto& plan : overlay.logicalDropPlans) {
    if (plan.initialization != initialization) continue;
    if (found || !samePlace(plan.root, root)) return false;
    found = true;
  }
  return found;
}

bool hasCompleteInitializationPlans(const mir::MirFunction& function,
                                    const OwnershipFunctionEventOverlay& overlay) {
  size_t expectedPlans = 0;
  for (const auto& block : function.blocks) {
    for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
      const auto& statement = block.statements[ordinal];
      if (statement.kind() != mir::MirStatementKind::Assign) continue;
      const auto rvalueKind = statement.assignmentValue().value.kind();
      if (rvalueKind != mir::MirRvalueKind::Use &&
          rvalueKind != mir::MirRvalueKind::NominalAggregate) {
        continue;
      }
      ++expectedPlans;
      const MirEventKey initialization{
          MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 2};
      if (!hasInitializationPlan(overlay, initialization,
                                 statement.assignmentValue().destination)) {
        return false;
      }
    }
    if (block.terminator.kind() != mir::MirTerminatorKind::Call) continue;
    ++expectedPlans;
    const auto& call = block.terminator.callValue();
    const MirEventKey initialization{
        MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)}, 0};
    if (!hasInitializationPlan(overlay, initialization, call.destination)) return false;
  }
  return overlay.logicalDropPlans.size() == expectedPlans;
}

bool setUnavailable(const MovePathFunction& paths, zc::Vector<InitializationPathState>& states,
                    const mir::MirPlace& place, InitializationLossKind kind, MirEventKey&& event,
                    InitializationState unavailable) {
  if (states.size() != paths.facts.size()) return false;
  bool found = false;
  for (size_t index = 0; index < paths.facts.size(); ++index) {
    if (!isPathWithin(place, paths.facts[index].key.place)) continue;
    setUnavailable(states[index], kind, cloneEvent(event), place.local(), unavailable);
    found = true;
  }
  return found;
}

bool setInitialized(const MovePathFunction& paths, zc::Vector<InitializationPathState>& states,
                    const mir::MirPlace& place) {
  if (states.size() != paths.facts.size()) return false;
  bool found = false;
  for (size_t index = 0; index < paths.facts.size(); ++index) {
    if (!isPathWithin(place, paths.facts[index].key.place)) continue;
    setInitialized(states[index]);
    found = true;
  }
  return found;
}

bool applyOperand(const mir::MirFunction& function, const MovePathFunction& paths,
                  const mir::MirOperand& operand, MirEventKey&& event,
                  zc::Vector<InitializationPathState>& states) {
  if (operand.kind() == mir::MirOperandKind::Constant) return true;
  if (!validLocalPlace(function, operand.place())) return false;
  if (operand.kind() == mir::MirOperandKind::Move) {
    return setUnavailable(paths, states, operand.place(), InitializationLossKind::Moved,
                          zc::mv(event), InitializationState::uninitialized());
  }
  return true;
}

bool applyRvalue(const mir::MirFunction& function, const MovePathFunction& paths,
                 const mir::MirRvalue& rvalue, MirEventKey&& event,
                 zc::Vector<InitializationPathState>& states) {
  if (rvalue.kind() == mir::MirRvalueKind::Use) {
    return applyOperand(function, paths, rvalue.useValue().operand, zc::mv(event), states);
  }
  const auto& aggregate = rvalue.nominalAggregateValue();
  if (!aggregate.definition.isValid() || !aggregate.type.isValid()) return false;
  for (const auto& element : aggregate.elements) {
    if (!element.field.isValid() || element.operand.kind() != mir::MirOperandKind::Constant) {
      return false;
    }
  }
  return true;
}

bool initialize(const mir::MirFunction& function, const MovePathFunction& paths,
                const mir::MirPlace& place, zc::Vector<InitializationPathState>& states,
                bool overwrite) {
  if (!validLocalPlace(function, place)) return false;
  auto index = pathIndex(paths, place);
  if (index == zc::none) return false;
  ZC_IF_SOME(value, index) {
    const auto state = states[value].state;
    if (!state.storageLive || (overwrite ? !state.mustBeInitialized : state.mayBeInitialized)) {
      return false;
    }
    return setInitialized(paths, states, place);
  }
  ZC_UNREACHABLE
}

bool appendFacts(zc::Vector<InitializationFact>& facts, MirPoint point,
                 const mir::MirFunction& function, const MovePathFunction& paths,
                 zc::ArrayPtr<const InitializationPathState> states) {
  for (const auto& path : paths.facts) {
    if (path.key.owner != function.owner || !validLocalPlace(function, path.key.place)) {
      return false;
    }
    auto index = pathIndex(paths, path.key.place);
    if (index == zc::none) return false;
    ZC_IF_SOME(value, index) {
      facts.add(InitializationFact{point, cloneKey(path.key), states[value].state,
                                   cloneLossCauses(states[value].lossCauses.asPtr())});
    }
  }
  return true;
}

bool applyStatement(const mir::MirFunction& function, const MovePathFunction& paths,
                    const mir::MirStatement& statement, mir::MirBlockId block, uint32_t ordinal,
                    zc::Vector<InitializationPathState>& states) {
  auto statementEvent = [&]() {
    return eventAt(function.owner, MirPoint::beforeStatement(block, ordinal), 0);
  };
  switch (statement.kind()) {
    case mir::MirStatementKind::Assign: {
      const auto& assignment = statement.assignmentValue();
      return applyRvalue(function, paths, assignment.value, statementEvent(), states) &&
             initialize(function, paths, assignment.destination, states,
                        assignment.initialization == mir::MirInitializationKind::Overwrite);
    }
    case mir::MirStatementKind::StorageLive: {
      auto root = rootKey(
          function.owner,
          function.locals[ZC_ASSERT_NONNULL(localIndex(function, statement.storageLocal()))]);
      for (size_t index = 0; index < paths.facts.size(); ++index) {
        if (isPathWithin(root.place, paths.facts[index].key.place) &&
            states[index].state != InitializationState::dead()) {
          return false;
        }
      }
      return setUnavailable(paths, states, root.place, InitializationLossKind::NeverInitialized,
                            statementEvent(), InitializationState::uninitialized());
    }
    case mir::MirStatementKind::StorageDead: {
      auto root = rootKey(
          function.owner,
          function.locals[ZC_ASSERT_NONNULL(localIndex(function, statement.storageLocal()))]);
      for (size_t index = 0; index < paths.facts.size(); ++index) {
        if (isPathWithin(root.place, paths.facts[index].key.place) &&
            states[index].state != InitializationState::uninitialized()) {
          return false;
        }
      }
      return setUnavailable(paths, states, root.place, InitializationLossKind::StorageEnded,
                            statementEvent(), InitializationState::dead());
    }
    case mir::MirStatementKind::BorrowCreation: {
      const auto& borrow = statement.borrowCreationValue();
      return validLocalPlace(function, borrow.source) &&
             initialize(function, paths, borrow.destination, states, false);
    }
    case mir::MirStatementKind::SetDiscriminant:
      return validLocalPlace(function, statement.setDiscriminantValue().destination);
    case mir::MirStatementKind::Deinitialize: {
      const auto& deinitialization = statement.deinitializeValue();
      if (!validLocalPlace(function, deinitialization.destination)) return false;
      return setUnavailable(paths, states, deinitialization.destination,
                            InitializationLossKind::Deinitialized, statementEvent(),
                            InitializationState::uninitialized());
    }
  }
  return false;
}

zc::Maybe<size_t> blockIndex(const mir::MirFunction& function, mir::MirBlockId id) {
  for (size_t index = 0; index < function.blocks.size(); ++index) {
    if (function.blocks[index].id == id) return index;
  }
  return zc::none;
}

zc::Maybe<InitializationFunction> deriveFunction(const mir::MirFunction& function,
                                                 const MovePathFunction& paths) {
  if (function.blocks.size() == 0) return zc::none;
  zc::Vector<InitializationPathState> states;
  for (const auto& path : paths.facts) {
    if (path.key.owner != function.owner || !validLocalPlace(function, path.key.place)) {
      return zc::none;
    }
    auto localIndexValue = localIndex(function, path.key.place.local());
    if (localIndexValue == zc::none) return zc::none;
    const auto& local = function.locals[ZC_ASSERT_NONNULL(localIndexValue)];
    if (local.kind == mir::MirLocalKind::Parameter) {
      states.add(InitializationPathState{InitializationState::initialized(),
                                         zc::Vector<InitializationLossCause>()});
    } else {
      states.add(InitializationPathState{
          InitializationState::dead(),
          oneLossCause(InitializationLossKind::NeverInitialized,
                       eventAt(function.owner, MirPoint::entry(),
                               static_cast<uint32_t>(ZC_ASSERT_NONNULL(localIndexValue))),
                       local.id)});
    }
  }
  zc::Vector<InitializationFact> facts;
  if (!appendFacts(facts, MirPoint::entry(), function, paths, states.asPtr())) return zc::none;
  zc::Vector<mir::MirBlockId> visited;
  size_t current = 0;
  for (size_t steps = 0; steps < function.blocks.size(); ++steps) {
    const auto& block = function.blocks[current];
    for (const auto previous : visited) {
      if (previous == block.id) return zc::none;
    }
    visited.add(block.id);
    for (size_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
      if (!appendFacts(facts, MirPoint::beforeStatement(block.id, static_cast<uint32_t>(ordinal)),
                       function, paths, states.asPtr())) {
        return zc::none;
      }
      if (!applyStatement(function, paths, block.statements[ordinal], block.id,
                          static_cast<uint32_t>(ordinal), states)) {
        return zc::none;
      }
      if (!appendFacts(facts, MirPoint::afterStatement(block.id, static_cast<uint32_t>(ordinal)),
                       function, paths, states.asPtr())) {
        return zc::none;
      }
    }
    if (!appendFacts(facts, MirPoint::beforeTerminator(block.id), function, paths,
                     states.asPtr())) {
      return zc::none;
    }
    if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
      ZC_IF_SOME(value, block.terminator.returnValue().value) {
        if (!applyOperand(function, paths, value,
                          eventAt(function.owner, MirPoint::beforeTerminator(block.id), 0),
                          states)) {
          if (value.kind() == mir::MirOperandKind::Constant ||
              !validLocalPlace(function, value.place())) {
            return zc::none;
          }
        }
      }
      if (!appendFacts(facts, MirPoint::exit(block.id, MirExitKind::Return), function, paths,
                       states.asPtr())) {
        return zc::none;
      }
      return InitializationFunction{function.owner, zc::mv(facts)};
    }
    if (block.terminator.kind() != mir::MirTerminatorKind::Call) return zc::none;
    const auto& call = block.terminator.callValue();
    if (call.unwindTarget != zc::none) return zc::none;
    for (uint32_t ordinal = 0; ordinal < call.arguments.size(); ++ordinal) {
      if (!applyOperand(function, paths, call.arguments[ordinal],
                        eventAt(function.owner, MirPoint::beforeTerminator(block.id), ordinal),
                        states)) {
        return zc::none;
      }
    }
    if (!initialize(function, paths, call.destination, states, false)) return zc::none;
    if (!appendFacts(facts, MirPoint::edge(block.id, 0, call.normalTarget), function, paths,
                     states.asPtr())) {
      return zc::none;
    }
    auto next = blockIndex(function, call.normalTarget);
    if (next == zc::none) return zc::none;
    ZC_IF_SOME(value, next) { current = value; }
  }
  return zc::none;
}

bool lessEvent(const MirEventKey& left, const MirEventKey& right) {
  if (left.location.point < right.location.point) return true;
  if (right.location.point < left.location.point) return false;
  return left.operandOrdinal < right.operandOrdinal;
}

bool sameFunctions(zc::ArrayPtr<const InitializationFunction> left,
                   zc::ArrayPtr<const InitializationFunction> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].owner != right[index].owner ||
        left[index].facts.size() != right[index].facts.size()) {
      return false;
    }
    for (size_t fact = 0; fact < left[index].facts.size(); ++fact) {
      const auto& first = left[index].facts[fact];
      const auto& second = right[index].facts[fact];
      if (first.point != second.point || first.key.owner != left[index].owner ||
          second.key.owner != right[index].owner || !samePlace(first.key.place, second.key.place) ||
          first.state != second.state || !validState(first.state) ||
          !sameLossCauses(first.lossCauses.asPtr(), second.lossCauses.asPtr(), left[index].owner,
                          first.key.place.local(), first.state)) {
        return false;
      }
    }
  }
  return true;
}

bool inputsMatch(const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay, const VerifiedFlow& flow,
                 const VerifiedMovePaths& movePaths) {
  if (overlay.semanticContext() != builtMir.semanticContext() ||
      overlay.module() != builtMir.module() ||
      overlay.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      overlay.builtRevision().digest() != builtMir.revision().digest() ||
      flow.semanticContext() != builtMir.semanticContext() || flow.module() != builtMir.module() ||
      flow.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      flow.builtRevision().digest() != builtMir.revision().digest() ||
      flow.overlayRevision().digest() != overlay.revision().digest() ||
      flow.functions().size() != builtMir.functions().size() ||
      movePaths.semanticContext() != builtMir.semanticContext() ||
      movePaths.module() != builtMir.module() ||
      movePaths.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      movePaths.builtRevision().digest() != builtMir.revision().digest() ||
      movePaths.overlayRevision().digest() != overlay.revision().digest() ||
      movePaths.functions().size() != builtMir.functions().size()) {
    return false;
  }
  return true;
}

bool hasFlowPoint(const FlowFunction& flow, const MirPoint& point) {
  for (const auto& candidate : flow.points) {
    if (candidate.kind() != OwnershipPointKind::Cfg || candidate.cfgValue().point != point) {
      continue;
    }
    return true;
  }
  return false;
}

bool factsUseFlow(const InitializationFunction& facts, const FlowFunction& flow) {
  if (facts.owner != flow.owner) return false;
  for (const auto& fact : facts.facts) {
    if (!hasFlowPoint(flow, fact.point)) return false;
  }
  return true;
}

bool validInputs(const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay, const VerifiedFlow& flow,
                 const VerifiedMovePaths& movePaths) {
  if (!inputsMatch(builtMir, overlay, flow, movePaths)) return false;
  if (overlay.functions().size() != builtMir.functions().size()) return false;
  for (size_t index = 0; index < builtMir.functions().size(); ++index) {
    const auto& function = builtMir.functions()[index];
    const auto& paths = movePaths.functions()[index];
    const auto& flowFunction = flow.functions()[index];
    if (paths.owner != function.owner || flowFunction.owner != function.owner) return false;
    for (size_t local = 0; local < function.locals.size(); ++local) {
      const auto root = rootKey(function.owner, function.locals[local]);
      bool found = false;
      for (const auto& fact : paths.facts) {
        if (fact.parent != zc::none || fact.key.owner != function.owner ||
            !samePlace(fact.key.place, root.place)) {
          continue;
        }
        if (found || !validLocalPlace(function, fact.key.place)) return false;
        found = true;
      }
      if (!found) return false;
    }
    for (const auto& fact : paths.facts) {
      if (fact.key.owner != function.owner || !validLocalPlace(function, fact.key.place)) {
        return false;
      }
    }
    auto functionOverlay = overlayFor(overlay, function.owner);
    if (functionOverlay == zc::none) return false;
    ZC_IF_SOME(value, functionOverlay) {
      if (!hasCompleteInitializationPlans(function, value)) return false;
    }
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

zc::Maybe<const InitializationFunction&> initializationFor(
    const VerifiedInitializationFacts& initialization, identity::DefId owner) {
  for (const auto& function : initialization.functions()) {
    if (function.owner == owner) return function;
  }
  return zc::none;
}

zc::Maybe<const InitializationFact&> initializationAt(const InitializationFunction& function,
                                                      const MirPoint& point,
                                                      const mir::MirPlace& place) {
  for (const auto& fact : function.facts) {
    if (fact.point == point && samePlace(fact.key.place, place)) return fact;
  }
  return zc::none;
}

bool hasRole(const MirEventSlot& slot, OwnershipEventRole role) {
  for (const auto candidate : slot.roles) {
    if (candidate == role) return true;
  }
  return false;
}

bool hasOperandRead(const OwnershipFunctionEventOverlay& overlay, const MirEventKey& event) {
  for (const auto& slot : overlay.slots) {
    if (slot.key == event) {
      return slot.stage == OwnershipEventStage::Source &&
             hasRole(slot, OwnershipEventRole::OperandRead);
    }
  }
  return false;
}

bool appendSourceFailure(const mir::MirFunction& function,
                         const OwnershipFunctionEventOverlay& overlay,
                         const InitializationFunction& initialization, const MirPoint& point,
                         const mir::MirOperand& operand, const identity::SourceSpan& useSpan,
                         uint32_t operandOrdinal, uint32_t& traversalOrdinal,
                         zc::Vector<InitializationSourceFailure>& failures) {
  if (operand.kind() == mir::MirOperandKind::Constant) return true;
  const auto primary = eventAt(function.owner, MirPoint(point), operandOrdinal);
  if (!hasOperandRead(overlay, primary)) return false;
  zc::Maybe<const InitializationFact&> fact;
  fact = initializationAt(initialization, point, operand.place());
  if (fact == zc::none) return false;
  ZC_IF_SOME(state, fact) {
    if (state.state.mustBeInitialized) return true;
    if (state.lossCauses.size() == 0) return false;
    bool allMoves = true;
    for (const auto& cause : state.lossCauses) {
      if (cause.kind != InitializationLossKind::Moved) {
        allMoves = false;
        break;
      }
    }
    zc::Vector<InitializationSourceFailureCause> unavailableCauses;
    unavailableCauses.reserve(state.lossCauses.size());
    for (const auto& cause : state.lossCauses) {
      auto span = sourceSpanFor(function, overlay, cause.event);
      if (span == zc::none) return false;
      unavailableCauses.add(InitializationSourceFailureCause{cause.kind, cause.event,
                                                             zc::mv(ZC_ASSERT_NONNULL(span))});
    }
    failures.add(InitializationSourceFailure{
        allMoves ? InitializationSourceFailureKind::UseAfterMove
                 : InitializationSourceFailureKind::UninitializedPlaceUse,
        function.owner, primary, useSpan.clone(),
        MovePathKey{function.owner, operand.place().clone()}, zc::mv(unavailableCauses),
        traversalOrdinal++});
    return true;
  }
  ZC_UNREACHABLE
}

bool appendSourceFailure(const mir::MirFunction& function,
                         const OwnershipFunctionEventOverlay& overlay,
                         const InitializationFunction& initialization, const MirPoint& point,
                         const mir::MirPlace& place, const identity::SourceSpan& useSpan,
                         uint32_t operandOrdinal, uint32_t& traversalOrdinal,
                         zc::Vector<InitializationSourceFailure>& failures) {
  auto operand = mir::MirOperand::copy(place.clone());
  return appendSourceFailure(function, overlay, initialization, point, operand, useSpan,
                             operandOrdinal, traversalOrdinal, failures);
}

zc::Maybe<zc::Vector<InitializationSourceFailure>> sourceFailures(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedInitializationFacts& initialization) {
  zc::Vector<InitializationSourceFailure> failures;
  uint32_t traversalOrdinal = 0;
  for (const auto& function : builtMir.functions()) {
    auto facts = initializationFor(initialization, function.owner);
    auto functionOverlay = overlayFor(overlay, function.owner);
    if (facts == zc::none || functionOverlay == zc::none) return zc::none;
    for (const auto& block : function.blocks) {
      for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        const auto& statement = block.statements[ordinal];
        const auto point = MirPoint::beforeStatement(block.id, ordinal);
        switch (statement.kind()) {
          case mir::MirStatementKind::Assign:
            if (statement.assignmentValue().value.kind() != mir::MirRvalueKind::Use) break;
            if (!appendSourceFailure(function, ZC_ASSERT_NONNULL(functionOverlay),
                                     ZC_ASSERT_NONNULL(facts), point,
                                     statement.assignmentValue().value.useValue().operand,
                                     statement.sourceSpan(), 0, traversalOrdinal, failures)) {
              return zc::none;
            }
            break;
          case mir::MirStatementKind::BorrowCreation:
            if (!appendSourceFailure(function, ZC_ASSERT_NONNULL(functionOverlay),
                                     ZC_ASSERT_NONNULL(facts), point,
                                     statement.borrowCreationValue().source, statement.sourceSpan(),
                                     0, traversalOrdinal, failures)) {
              return zc::none;
            }
            break;
          case mir::MirStatementKind::StorageLive:
          case mir::MirStatementKind::StorageDead:
          case mir::MirStatementKind::SetDiscriminant:
          case mir::MirStatementKind::Deinitialize:
            break;
        }
      }
      const auto point = MirPoint::beforeTerminator(block.id);
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        ZC_IF_SOME(value, block.terminator.returnValue().value) {
          if (!appendSourceFailure(function, ZC_ASSERT_NONNULL(functionOverlay),
                                   ZC_ASSERT_NONNULL(facts), point, value,
                                   block.terminator.sourceSpan(), 0, traversalOrdinal, failures)) {
            return zc::none;
          }
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
        uint32_t argumentOrdinal = 0;
        for (const auto& argument : block.terminator.callValue().arguments) {
          if (!appendSourceFailure(function, ZC_ASSERT_NONNULL(functionOverlay),
                                   ZC_ASSERT_NONNULL(facts), point, argument,
                                   block.terminator.sourceSpan(), argumentOrdinal++,
                                   traversalOrdinal, failures)) {
            return zc::none;
          }
        }
      }
    }
  }
  return failures;
}

zc::Maybe<zc::Vector<InitializationFunction>> derive(const mir::VerifiedBuiltMir& builtMir,
                                                     const VerifiedOwnershipEventOverlay& overlay,
                                                     const VerifiedFlow& flow,
                                                     const VerifiedMovePaths& movePaths) {
  if (!validInputs(builtMir, overlay, flow, movePaths)) return zc::none;
  zc::Vector<InitializationFunction> functions;
  for (size_t index = 0; index < builtMir.functions().size(); ++index) {
    const auto& function = builtMir.functions()[index];
    auto facts = deriveFunction(function, movePaths.functions()[index]);
    if (facts == zc::none) return zc::none;
    ZC_IF_SOME(value, facts) {
      if (!factsUseFlow(value, flow.functions()[index])) return zc::none;
      functions.add(zc::mv(value));
    }
  }
  return functions;
}

}  // namespace

bool InitializationSourceFailureOrdering::less(const InitializationSourceFailure& left,
                                               const InitializationSourceFailure& right) noexcept {
  if (left.useSpan.byteStart() != right.useSpan.byteStart()) {
    return left.useSpan.byteStart() < right.useSpan.byteStart();
  }
  if (left.useSpan.byteEnd() != right.useSpan.byteEnd()) {
    return left.useSpan.byteEnd() < right.useSpan.byteEnd();
  }
  if (left.kind != right.kind) {
    return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
  }
  if (left.traversalOrdinal != right.traversalOrdinal) {
    return left.traversalOrdinal < right.traversalOrdinal;
  }
  if (lessEvent(left.primary, right.primary)) return true;
  if (lessEvent(right.primary, left.primary)) return false;
  if (left.unavailableCauses.size() != right.unavailableCauses.size()) {
    return left.unavailableCauses.size() < right.unavailableCauses.size();
  }
  for (size_t index = 0; index < left.unavailableCauses.size(); ++index) {
    const auto& leftCause = left.unavailableCauses[index];
    const auto& rightCause = right.unavailableCauses[index];
    if (leftCause.kind != rightCause.kind) {
      return static_cast<uint8_t>(leftCause.kind) < static_cast<uint8_t>(rightCause.kind);
    }
    if (lessEvent(leftCause.event, rightCause.event)) return true;
    if (lessEvent(rightCause.event, leftCause.event)) return false;
    if (leftCause.span.byteStart() != rightCause.span.byteStart()) {
      return leftCause.span.byteStart() < rightCause.span.byteStart();
    }
    if (leftCause.span.byteEnd() != rightCause.span.byteEnd()) {
      return leftCause.span.byteEnd() < rightCause.span.byteEnd();
    }
  }
  return false;
}

InitializationCandidate::InitializationCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    zc::Vector<InitializationFunction>&& functions) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      functions(zc::mv(functions)) {}

struct VerifiedInitializationFacts::Impl final {
  explicit Impl(InitializationCandidate&& candidate) noexcept
      : semanticContext(candidate.semanticContext),
        contextFingerprint(zc::mv(candidate.contextFingerprint)),
        module(candidate.module),
        builtRevision(candidate.builtRevision),
        overlayRevision(candidate.overlayRevision),
        functions(zc::mv(candidate.functions)) {}

  identity::SemanticContextBrand semanticContext;
  identity::ContextFingerprint contextFingerprint;
  identity::ModuleId module;
  mir::MirRevisionId builtRevision;
  OwnershipEventOverlayRevision overlayRevision;
  zc::Vector<InitializationFunction> functions;
};

VerifiedInitializationFacts::VerifiedInitializationFacts(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedInitializationFacts::~VerifiedInitializationFacts() noexcept(false) = default;
VerifiedInitializationFacts::VerifiedInitializationFacts(VerifiedInitializationFacts&&) noexcept =
    default;
VerifiedInitializationFacts& VerifiedInitializationFacts::operator=(
    VerifiedInitializationFacts&&) noexcept = default;
identity::SemanticContextBrand VerifiedInitializationFacts::semanticContext() const noexcept {
  return impl->semanticContext;
}
const identity::ContextFingerprint& VerifiedInitializationFacts::contextFingerprint()
    const noexcept {
  return impl->contextFingerprint;
}
identity::ModuleId VerifiedInitializationFacts::module() const noexcept { return impl->module; }
const mir::MirRevisionId& VerifiedInitializationFacts::builtRevision() const noexcept {
  return impl->builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedInitializationFacts::overlayRevision() const noexcept {
  return impl->overlayRevision;
}
zc::ArrayPtr<const InitializationFunction> VerifiedInitializationFacts::functions() const noexcept {
  return impl->functions;
}

InitializationSourceVerificationResult InitializationSourceVerifier::verify(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedInitializationFacts& initialization) {
  if (initialization.semanticContext() != builtMir.semanticContext() ||
      initialization.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      initialization.module() != builtMir.module() ||
      initialization.builtRevision().digest() != builtMir.revision().digest() ||
      initialization.overlayRevision().digest() != overlay.revision().digest() ||
      overlay.semanticContext() != builtMir.semanticContext() ||
      overlay.contextFingerprint().digest() != builtMir.contextFingerprint().digest() ||
      overlay.module() != builtMir.module() ||
      overlay.builtRevision().digest() != builtMir.revision().digest()) {
    const auto identities = builtMir.retainIdentityAuthority();
    return reject(builtMir, identities, ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto failures = sourceFailures(builtMir, overlay, initialization);
  if (failures == zc::none) {
    const auto identities = builtMir.retainIdentityAuthority();
    return reject(builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof, 0);
  }
  ZC_IF_SOME(values, failures) {
    auto sorted =
        ir::SortedSourceFailureFacts<InitializationSourceFailure,
                                     InitializationSourceFailureOrdering>::from(zc::mv(values));
    ZC_IF_SOME(value, sorted) {
      return InitializationSourceVerificationResult::sourceRejected(zc::mv(value));
    }
  }
  return InitializationSourceVerificationResult::verified(InitializationSourceAccepted{});
}

InitializationSourceVerificationResult InitializationSourceVerifier::reject(
    const mir::VerifiedBuiltMir& builtMir, const checker::CheckerIdentityAuthority& identities,
    ir::IrFailureKind kind, uint32_t ordinal) {
  auto rejected =
      rejectInvariant<InitializationSourceAccepted>(builtMir, identities, kind, ordinal);
  if (rejected.isIdentityInvariantRejected()) {
    return InitializationSourceVerificationResult::identityInvariantRejected(
        zc::mv(rejected).takeIdentityFailures());
  }
  if (rejected.isIrInvariantRejected()) {
    return InitializationSourceVerificationResult::irInvariantRejected(
        zc::mv(rejected).takeInvariantFailures());
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<InitializationCandidate> InitializationBuilder::build(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedFlow& flow, const VerifiedMovePaths& movePaths) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(builtMir, overlay, flow, movePaths)) {
    return rejectInvariant<InitializationCandidate>(builtMir, identities,
                                                    ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto functions = derive(builtMir, overlay, flow, movePaths);
  if (functions == zc::none) {
    return rejectInvariant<InitializationCandidate>(builtMir, identities,
                                                    ir::IrFailureKind::InvalidOwnershipProof, 0);
  }
  ZC_IF_SOME(value, functions) {
    return ir::IrOperationResult<InitializationCandidate>::verified(InitializationCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedInitializationFacts> InitializationVerifier::verify(
    InitializationCandidate&& candidate, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay, const VerifiedFlow& flow,
    const VerifiedMovePaths& movePaths) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.module != builtMir.module() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      !inputsMatch(builtMir, overlay, flow, movePaths)) {
    return rejectInvariant<VerifiedInitializationFacts>(
        builtMir, identities, ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = derive(builtMir, overlay, flow, movePaths);
  if (expected == zc::none || !sameFunctions(candidate.functions, ZC_ASSERT_NONNULL(expected))) {
    return rejectInvariant<VerifiedInitializationFacts>(
        builtMir, identities, ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedInitializationFacts>::verified(
      VerifiedInitializationFacts(zc::heap<VerifiedInitializationFacts::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
