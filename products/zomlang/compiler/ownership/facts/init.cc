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

MirEventKey cloneEvent(const MirEventKey& event);

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
                                                 MovePathKey&& path) {
  zc::Vector<InitializationLossCause> causes;
  causes.add(InitializationLossCause{kind, zc::mv(event), zc::mv(path)});
  return causes;
}

zc::Vector<InitializationLossCause> cloneLossCauses(
    zc::ArrayPtr<const InitializationLossCause> causes) {
  zc::Vector<InitializationLossCause> cloned;
  for (const auto& cause : causes) {
    cloned.add(InitializationLossCause{cause.kind, cloneEvent(cause.event),
                                       MovePathKey{cause.path.owner, cause.path.place.clone()}});
  }
  return cloned;
}

zc::Vector<InitializationPathState> cloneStates(
    zc::ArrayPtr<const InitializationPathState> states) {
  zc::Vector<InitializationPathState> cloned;
  for (const auto& state : states) {
    cloned.add(InitializationPathState{state.state, cloneLossCauses(state.lossCauses.asPtr())});
  }
  return cloned;
}

bool joinPathStates(zc::ArrayPtr<const InitializationPathState> left,
                    zc::ArrayPtr<const InitializationPathState> right,
                    zc::Vector<InitializationPathState>& joined) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    joined.add(InitializationPathState{
        InitializationLattice::joinState(left[index].state, right[index].state),
        InitializationLattice::mergeLossCauses(left[index].lossCauses.asPtr(),
                                               right[index].lossCauses.asPtr())});
  }
  return true;
}

MirEventKey eventAt(identity::DefId owner, MirPoint&& point, uint32_t ordinal) {
  return MirEventKey{MirLocation{owner, zc::mv(point)}, ordinal};
}

void setUnavailable(InitializationPathState& state, InitializationLossKind kind,
                    MirEventKey&& event, MovePathKey&& path, InitializationState unavailable) {
  state.state = unavailable;
  state.lossCauses = oneLossCause(kind, zc::mv(event), zc::mv(path));
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
  return left.kind == right.kind && left.event == right.event &&
         left.path.owner == right.path.owner && samePlace(left.path.place, right.path.place);
}

bool validLossCause(const InitializationLossCause& cause, identity::DefId owner,
                    mir::MirLocalId local) {
  if (cause.path.owner != owner || cause.path.place.local() != local ||
      cause.event.location.owner != owner || !cause.path.place.local().isValid()) {
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
  // A loss at `place` reaches the place itself, every descendant (the moved projection carries
  // its children with it), and every ancestor (a partial move makes the aggregate unavailable).
  // The causal path is always the operation place, so an ancestor fact records exactly which
  // descendant moved. For a root place the ancestor test matches only the root itself, leaving
  // storage-live and storage-dead behavior unchanged.
  MovePathKey causePath{paths.owner, place.clone()};
  bool found = false;
  for (size_t index = 0; index < paths.facts.size(); ++index) {
    const auto& path = paths.facts[index].key.place;
    if (!isPathWithin(place, path) && !isPathWithin(path, place)) continue;
    setUnavailable(states[index], kind, cloneEvent(event),
                   MovePathKey{causePath.owner, causePath.place.clone()}, unavailable);
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
    case mir::MirStatementKind::UnsafeScopeBoundary:
      return true;
  }
  return false;
}

zc::Maybe<size_t> blockIndex(const mir::MirFunction& function, mir::MirBlockId id) {
  for (size_t index = 0; index < function.blocks.size(); ++index) {
    if (function.blocks[index].id == id) return index;
  }
  return zc::none;
}

zc::Vector<mir::MirBlockId> predecessorBlocks(const FlowFunction& flow, mir::MirBlockId target) {
  zc::Vector<mir::MirBlockId> predecessors;
  for (const auto& point : flow.points) {
    if (point.kind() != OwnershipPointKind::Cfg) continue;
    const auto& location = point.cfgValue().point;
    if (location.kind() != MirPointKind::Edge) continue;
    if (location.edgeValue().to != target) continue;
    bool duplicate = false;
    for (const auto predecessor : predecessors) {
      if (predecessor == location.edgeValue().from) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) predecessors.add(location.edgeValue().from);
  }
  // Deterministic fold order independent of flow point ordering; the canonical
  // loss-cause sort makes joins order-independent, so this is defense-in-depth.
  for (size_t index = 1; index < predecessors.size(); ++index) {
    auto current = predecessors[index];
    size_t insertion = index;
    while (insertion != 0 && current.ordinal() < predecessors[insertion - 1].ordinal()) {
      predecessors[insertion] = predecessors[insertion - 1];
      --insertion;
    }
    predecessors[insertion] = current;
  }
  return predecessors;
}

zc::Maybe<zc::Vector<InitializationPathState>> joinPredecessorStates(
    const mir::MirFunction& function, const FlowFunction& flow, mir::MirBlockId target,
    zc::ArrayPtr<const zc::Maybe<zc::Vector<InitializationPathState>>> blockExitStates) {
  const auto predecessors = predecessorBlocks(flow, target);
  if (predecessors.size() == 0) return zc::none;
  zc::Maybe<zc::Vector<InitializationPathState>> joined;
  for (const auto predecessor : predecessors) {
    auto index = blockIndex(function, predecessor);
    if (index == zc::none) return zc::none;
    const auto& exit = blockExitStates[ZC_ASSERT_NONNULL(index)];
    if (exit == zc::none) return zc::none;
    ZC_IF_SOME(states, exit) {
      if (joined == zc::none) {
        joined = cloneStates(states.asPtr());
      } else {
        zc::Vector<InitializationPathState> merged;
        if (!joinPathStates(ZC_ASSERT_NONNULL(joined).asPtr(), states.asPtr(), merged)) {
          return zc::none;
        }
        joined = zc::mv(merged);
      }
    }
  }
  return joined;
}

/// \brief Join of the predecessor exit states that have already been computed,
/// skipping predecessors whose exit state is not yet available.
///
/// This is the monotone step of the convergence fixpoint: a diamond join or a
/// loop header is reached before all of its predecessors are computed, so the
/// strict joinPredecessorStates (which fails closed on any missing predecessor)
/// cannot be used during iteration. The join lattice is idempotent and
/// commutative, so folding whatever predecessors are ready and re-enqueueing on
/// growth converges to the same fixpoint the strict join produces once every
/// predecessor is present. Returns none when no predecessor exit is computed
/// yet, or when a predecessor block cannot be resolved.
zc::Maybe<zc::Vector<InitializationPathState>> joinComputedPredecessors(
    const mir::MirFunction& function, const FlowFunction& flow, mir::MirBlockId target,
    zc::ArrayPtr<const zc::Maybe<zc::Vector<InitializationPathState>>> blockExitStates) {
  const auto predecessors = predecessorBlocks(flow, target);
  if (predecessors.size() == 0) return zc::none;
  zc::Maybe<zc::Vector<InitializationPathState>> joined;
  for (const auto predecessor : predecessors) {
    auto index = blockIndex(function, predecessor);
    if (index == zc::none) return zc::none;
    const auto& exit = blockExitStates[ZC_ASSERT_NONNULL(index)];
    if (exit == zc::none) continue;  // Predecessor not yet computed; fold later.
    ZC_IF_SOME(states, exit) {
      if (joined == zc::none) {
        joined = cloneStates(states.asPtr());
      } else {
        zc::Vector<InitializationPathState> merged;
        if (!joinPathStates(ZC_ASSERT_NONNULL(joined).asPtr(), states.asPtr(), merged)) {
          return zc::none;
        }
        joined = zc::mv(merged);
      }
    }
  }
  return joined;
}

/// \brief Successor block ids of one terminator on its normal edges.
///
/// Call yields its normal target, Goto its target, and SwitchInt every arm
/// target plus the default. Return and Unreachable have no successors.
zc::Vector<mir::MirBlockId> terminatorSuccessors(const mir::MirTerminator& terminator) {
  zc::Vector<mir::MirBlockId> successors;
  if (terminator.kind() == mir::MirTerminatorKind::Call) {
    successors.add(terminator.callValue().normalTarget);
    return successors;
  }
  if (terminator.kind() == mir::MirTerminatorKind::Goto) {
    successors.add(terminator.gotoValue().target);
    return successors;
  }
  if (terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
    const auto& switchInt = terminator.switchIntValue();
    for (const auto& arm : switchInt.arms) successors.add(arm.target);
    successors.add(switchInt.defaultTarget);
  }
  return successors;
}

/// \brief Block indices reachable from the entry block, following terminator
/// successors without rejecting cycles.
///
/// Unlike a topological order, this admits back edges: the initialization
/// analysis converges on reducible loops through a monotone worklist fixpoint
/// (see deriveFunction), so it needs the reachable set, not a linear order.
/// Indices are returned in ascending block-index order for a deterministic
/// emission pass.
zc::Maybe<zc::Vector<size_t>> reachableBlocks(const mir::MirFunction& function) {
  zc::Vector<bool> seen;
  seen.resize(function.blocks.size());
  for (size_t index = 0; index < function.blocks.size(); ++index) seen[index] = false;
  zc::Vector<size_t> stack;
  stack.add(0);
  seen[0] = true;
  while (stack.size() != 0) {
    const size_t index = stack[stack.size() - 1];
    stack.removeLast();
    const auto& block = function.blocks[index];
    for (const auto successor : terminatorSuccessors(block.terminator)) {
      auto next = blockIndex(function, successor);
      if (next == zc::none) return zc::none;
      const size_t nextIndex = ZC_ASSERT_NONNULL(next);
      if (!seen[nextIndex]) {
        seen[nextIndex] = true;
        stack.add(nextIndex);
      }
    }
  }
  zc::Vector<size_t> order;
  for (size_t index = 0; index < function.blocks.size(); ++index) {
    if (seen[index]) order.add(index);
  }
  return order;
}

/// \brief Equality over one block's incoming state vector for fixpoint
/// convergence: same lattice state and identical canonical loss-cause set at
/// every path. Loss causes are canonically sorted by mergeLossCauses, so an
/// elementwise comparison is order-stable.
bool statesEqual(zc::ArrayPtr<const InitializationPathState> left,
                 zc::ArrayPtr<const InitializationPathState> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (!(left[index].state == right[index].state)) return false;
    if (left[index].lossCauses.size() != right[index].lossCauses.size()) return false;
    for (size_t cause = 0; cause < left[index].lossCauses.size(); ++cause) {
      const auto& l = left[index].lossCauses[cause];
      const auto& r = right[index].lossCauses[cause];
      if (l.kind != r.kind || l.event != r.event || l.path.owner != r.path.owner ||
          !samePlace(l.path.place, r.path.place)) {
        return false;
      }
    }
  }
  return true;
}

/// \brief Runs one block's forward transfer from `states` (its entry state),
/// mutating `states` into the block exit state.
///
/// When `facts` is non-null, initialization facts are appended at every point
/// (entry emission pass); when null, the block is transferred silently (the
/// convergence fixpoint). The two modes share one body so the emitted facts of
/// an acyclic function are identical to a single topological pass.
bool transferBlock(const mir::MirFunction& function, const MovePathFunction& paths,
                   const mir::MirBasicBlock& block, zc::Vector<InitializationPathState>& states,
                   zc::Vector<InitializationFact>* facts) {
  for (size_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
    if (facts != nullptr &&
        !appendFacts(*facts, MirPoint::beforeStatement(block.id, static_cast<uint32_t>(ordinal)),
                     function, paths, states.asPtr())) {
      return false;
    }
    if (!applyStatement(function, paths, block.statements[ordinal], block.id,
                        static_cast<uint32_t>(ordinal), states)) {
      return false;
    }
    if (facts != nullptr &&
        !appendFacts(*facts, MirPoint::afterStatement(block.id, static_cast<uint32_t>(ordinal)),
                     function, paths, states.asPtr())) {
      return false;
    }
  }
  if (facts != nullptr &&
      !appendFacts(*facts, MirPoint::beforeTerminator(block.id), function, paths, states.asPtr())) {
    return false;
  }
  if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
    ZC_IF_SOME(value, block.terminator.returnValue().value) {
      if (!applyOperand(function, paths, value,
                        eventAt(function.owner, MirPoint::beforeTerminator(block.id), 0), states)) {
        if (value.kind() == mir::MirOperandKind::Constant ||
            !validLocalPlace(function, value.place())) {
          return false;
        }
      }
    }
    if (facts != nullptr && !appendFacts(*facts, MirPoint::exit(block.id, MirExitKind::Return),
                                         function, paths, states.asPtr())) {
      return false;
    }
    return true;
  }
  if (block.terminator.kind() == mir::MirTerminatorKind::Goto) {
    if (facts != nullptr &&
        !appendFacts(*facts, MirPoint::edge(block.id, 0, block.terminator.gotoValue().target),
                     function, paths, states.asPtr())) {
      return false;
    }
    return true;
  }
  if (block.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
    const auto& switchInt = block.terminator.switchIntValue();
    if (!applyOperand(function, paths, switchInt.discriminant,
                      eventAt(function.owner, MirPoint::beforeTerminator(block.id), 0), states)) {
      return false;
    }
    if (facts != nullptr) {
      for (uint32_t ordinal = 0; ordinal < switchInt.arms.size(); ++ordinal) {
        if (!appendFacts(*facts, MirPoint::edge(block.id, ordinal, switchInt.arms[ordinal].target),
                         function, paths, states.asPtr())) {
          return false;
        }
      }
      if (!appendFacts(*facts,
                       MirPoint::edge(block.id, static_cast<uint32_t>(switchInt.arms.size()),
                                      switchInt.defaultTarget),
                       function, paths, states.asPtr())) {
        return false;
      }
    }
    return true;
  }
  if (block.terminator.kind() != mir::MirTerminatorKind::Call) return false;
  const auto& call = block.terminator.callValue();
  if (call.unwindTarget != zc::none) return false;
  for (uint32_t ordinal = 0; ordinal < call.arguments.size(); ++ordinal) {
    if (!applyOperand(function, paths, call.arguments[ordinal],
                      eventAt(function.owner, MirPoint::beforeTerminator(block.id), ordinal),
                      states)) {
      return false;
    }
  }
  if (!initialize(function, paths, call.destination, states, false)) return false;
  if (facts != nullptr && !appendFacts(*facts, MirPoint::edge(block.id, 0, call.normalTarget),
                                       function, paths, states.asPtr())) {
    return false;
  }
  return true;
}

/// \brief Seeds the entry-block incoming state: parameters are initialized,
/// every other move path is dead with a NeverInitialized loss cause.
zc::Maybe<zc::Vector<InitializationPathState>> seedEntryStates(const mir::MirFunction& function,
                                                               const MovePathFunction& paths) {
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
                       cloneKey(path.key))});
    }
  }
  return states;
}

/// \brief Derives the initialization facts for one MIR function.
///
/// The admitted CFG subset is reducible and may carry loop back edges, so the
/// analysis converges the per-block incoming state through a monotone worklist
/// fixpoint (mirroring the flow region-membership dataflow) instead of a single
/// topological pass: each block's incoming state is the join of its
/// predecessors' exit states, and a successor is re-enqueued whenever its
/// incoming state grows up the finite lattice or its canonical loss-cause set
/// enlarges. Once the incoming states are stable, a single deterministic
/// emission pass over the reachable blocks (ascending index order) replays the
/// forward transfer from each converged incoming state and appends every fact,
/// so an acyclic function emits exactly what a topological pass would.
zc::Maybe<InitializationFunction> deriveFunction(const mir::MirFunction& function,
                                                 const FlowFunction& flow,
                                                 const MovePathFunction& paths) {
  if (function.blocks.size() == 0) return zc::none;
  if (!isAdmittedFlowSubset(function)) return zc::none;

  auto seeded = seedEntryStates(function, paths);
  if (seeded == zc::none) return zc::none;

  auto order = reachableBlocks(function);
  if (order == zc::none) return zc::none;
  const auto& reachable = ZC_ASSERT_NONNULL(order);

  // Converge every block's exit state through a monotone worklist fixpoint,
  // without emitting facts. The entry block starts from the parameter/never-init
  // seeds; every other block's incoming state is the join of its already-computed
  // predecessor exit states (a partial join that skips predecessors not yet
  // reached — the join lattice is monotone and idempotent, so re-enqueueing on
  // growth converges to the same fixpoint regardless of visit order).
  zc::Vector<zc::Maybe<zc::Vector<InitializationPathState>>> blockExitStates;
  for (size_t index = 0; index < function.blocks.size(); ++index) blockExitStates.add(zc::none);

  zc::Vector<size_t> worklist;
  zc::Vector<bool> pending;
  pending.resize(function.blocks.size());
  for (size_t index = 0; index < function.blocks.size(); ++index) pending[index] = false;
  for (size_t position = reachable.size(); position != 0; --position) {
    worklist.add(reachable[position - 1]);
    pending[reachable[position - 1]] = true;
  }
  while (worklist.size() != 0) {
    const size_t current = worklist[worklist.size() - 1];
    worklist.removeLast();
    pending[current] = false;

    zc::Vector<InitializationPathState> incoming;
    if (current == 0) {
      incoming = cloneStates(ZC_ASSERT_NONNULL(seeded).asPtr());
    } else {
      auto joined = joinComputedPredecessors(function, flow, function.blocks[current].id,
                                             blockExitStates.asPtr());
      if (joined == zc::none) continue;  // No predecessor exit computed yet.
      incoming = zc::mv(ZC_ASSERT_NONNULL(joined));
    }
    if (!transferBlock(function, paths, function.blocks[current], incoming, nullptr)) {
      return zc::none;
    }
    bool grew = blockExitStates[current] == zc::none ||
                !statesEqual(ZC_ASSERT_NONNULL(blockExitStates[current]).asPtr(), incoming.asPtr());
    if (!grew) continue;
    blockExitStates[current] = cloneStates(incoming.asPtr());
    for (const auto successor : terminatorSuccessors(function.blocks[current].terminator)) {
      auto successorIndex = blockIndex(function, successor);
      if (successorIndex == zc::none) return zc::none;
      const size_t next = ZC_ASSERT_NONNULL(successorIndex);
      if (!pending[next]) {
        pending[next] = true;
        worklist.add(next);
      }
    }
  }

  // Emit facts from the converged states in a deterministic block-index order.
  // Non-entry blocks recompute their incoming state through the strict
  // predecessor join (every reachable predecessor now has a converged exit
  // state), so an acyclic function emits exactly what a single topological pass
  // over the same joins would.
  zc::Vector<InitializationFact> facts;
  if (!appendFacts(facts, MirPoint::entry(), function, paths, ZC_ASSERT_NONNULL(seeded).asPtr())) {
    return zc::none;
  }
  for (const size_t blockIndexValue : reachable) {
    const auto& block = function.blocks[blockIndexValue];
    zc::Vector<InitializationPathState> states;
    if (blockIndexValue == 0) {
      states = cloneStates(ZC_ASSERT_NONNULL(seeded).asPtr());
    } else {
      auto joined = joinPredecessorStates(function, flow, block.id, blockExitStates.asPtr());
      if (joined == zc::none) return zc::none;
      states = zc::mv(ZC_ASSERT_NONNULL(joined));
    }
    if (!transferBlock(function, paths, block, states, &facts)) return zc::none;
  }
  return InitializationFunction{function.owner, zc::mv(facts)};
}

bool lessEvent(const MirEventKey& left, const MirEventKey& right) {
  if (left.location.point < right.location.point) return true;
  if (right.location.point < left.location.point) return false;
  return left.operandOrdinal < right.operandOrdinal;
}

bool lessProjection(const mir::MirProjection& left, const mir::MirProjection& right) {
  if (left.kind() != right.kind()) {
    return static_cast<uint8_t>(left.kind()) < static_cast<uint8_t>(right.kind());
  }
  switch (left.kind()) {
    case mir::MirProjectionKind::Field:
      // Field identities are context handles without a public ordinal; an
      // admitted cause set never ties two distinct fields at one projection
      // position, so validity is a deterministic structural fallback.
      return left.fieldValue().field.isValid() < right.fieldValue().field.isValid();
    case mir::MirProjectionKind::Index:
      return left.indexValue().index.ordinal() < right.indexValue().index.ordinal();
    case mir::MirProjectionKind::Dereference:
      return false;
    case mir::MirProjectionKind::Downcast:
      return left.downcastValue().variant.isValid() < right.downcastValue().variant.isValid();
    case mir::MirProjectionKind::Subslice:
      if (left.subsliceValue().first != right.subsliceValue().first) {
        return left.subsliceValue().first < right.subsliceValue().first;
      }
      return left.subsliceValue().pastLast < right.subsliceValue().pastLast;
  }
  return false;
}

bool lessPlace(const mir::MirPlace& left, const mir::MirPlace& right) {
  if (left.local() != right.local()) return left.local().ordinal() < right.local().ordinal();
  const auto shared = zc::min(left.projections().size(), right.projections().size());
  for (size_t index = 0; index < shared; ++index) {
    if (lessProjection(left.projections()[index], right.projections()[index])) return true;
    if (lessProjection(right.projections()[index], left.projections()[index])) return false;
  }
  return left.projections().size() < right.projections().size();
}

/// \brief Canonical loss-cause ordering: kind, then event, then path.
///
/// Cause sets are function-local, so every path shares one owner; the owner
/// check defends the invariant and the place structure is the effective key.
/// Two causes equal under this ordering are duplicates.
bool lessLossCause(const InitializationLossCause& left, const InitializationLossCause& right) {
  if (left.kind != right.kind) {
    return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
  }
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  if (left.path.owner != right.path.owner) return false;
  return lessPlace(left.path.place, right.path.place);
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

bool allFunctionsAdmitted(const mir::VerifiedBuiltMir& builtMir) {
  for (const auto& function : builtMir.functions()) {
    if (!isAdmittedFlowSubset(function)) return false;
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

bool sameMarkerUseKey(const OwnershipMarkerUseKey& left, const OwnershipMarkerUseKey& right) {
  return left.event == right.event && left.marker == right.marker &&
         left.subject == right.subject &&
         left.markerPolicyRevision.digest() == right.markerPolicyRevision.digest() &&
         left.coherenceRevision.digest() == right.coherenceRevision.digest();
}

/// \brief Returns the places carrying a Positive Linear obligation, derived
/// from the overlay's logical-drop plans and their marker decisions.
///
/// Each drop-plan component records the marker-use key queried for its Linear
/// obligation; the place is Linear-positive exactly when that key resolves to
/// an `OwnershipMarkerDecisionPositive` in the overlay's marker-use inventory.
zc::Vector<mir::MirPlace> linearPositivePlaces(const OwnershipFunctionEventOverlay& overlay) {
  zc::Vector<mir::MirPlace> places;
  for (const auto& plan : overlay.logicalDropPlans) {
    for (const auto& component : plan.components) {
      for (const auto& use : overlay.markerUses) {
        if (!sameMarkerUseKey(use.key, component.linearDecision)) continue;
        if (use.decision.is<OwnershipMarkerDecisionPositive>()) {
          places.add(component.place.clone());
        }
        break;
      }
    }
  }
  return places;
}

bool isLinearPositive(const mir::MirPlace& place, const zc::Vector<mir::MirPlace>& linearPlaces) {
  for (const auto& linear : linearPlaces) {
    if (samePlace(place, linear)) return true;
  }
  return false;
}

bool appendSourceFailure(const mir::MirFunction& function,
                         const OwnershipFunctionEventOverlay& overlay,
                         const InitializationFunction& initialization, const MirPoint& point,
                         const mir::MirOperand& operand, const identity::SourceSpan& useSpan,
                         uint32_t operandOrdinal, uint32_t& traversalOrdinal,
                         zc::Vector<OwnershipSourceFailure>& failures,
                         const zc::Vector<mir::MirPlace>& linearPlaces) {
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
    zc::Vector<InitializationFailureCause> causes;
    causes.reserve(state.lossCauses.size());
    for (const auto& cause : state.lossCauses) {
      auto span = sourceSpanFor(function, overlay, cause.event);
      if (span == zc::none) return false;
      causes.add(
          InitializationFailureCause{cause.kind, cause.event, zc::mv(ZC_ASSERT_NONNULL(span))});
    }
    auto place = MovePathKey{function.owner, operand.place().clone()};
    if (allMoves) {
      // Rule 3: a second consumption of a Positive Linear obligation emits
      // LinearConsumedTwice with every sorted reaching first-consumption cause.
      // The suppression pass removes the UseAfterMove cascade at this event.
      if (operand.kind() == mir::MirOperandKind::Move &&
          isLinearPositive(operand.place(), linearPlaces)) {
        zc::Vector<LinearConsumptionCause> consumptionCauses;
        consumptionCauses.reserve(causes.size());
        for (const auto& cause : causes) {
          consumptionCauses.add(LinearConsumptionCause{cause.event, cause.span.clone()});
        }
        ZC_IREQUIRE(traversalOrdinal != UINT32_MAX, "ownership source traversal overflow");
        failures.add(LinearConsumedTwiceFailure{function.owner, primary, useSpan.clone(),
                                                cloneKey(place), traversalOrdinal++,
                                                zc::mv(consumptionCauses)});
      }
      ZC_IREQUIRE(traversalOrdinal != UINT32_MAX, "ownership source traversal overflow");
      failures.add(UseAfterMoveFailure{function.owner, primary, useSpan.clone(), zc::mv(place),
                                       traversalOrdinal++, zc::mv(causes)});
    } else {
      ZC_IREQUIRE(traversalOrdinal != UINT32_MAX, "ownership source traversal overflow");
      failures.add(UninitializedPlaceUseFailure{function.owner, primary, useSpan.clone(),
                                                zc::mv(place), traversalOrdinal++, zc::mv(causes)});
    }
    return true;
  }
  ZC_UNREACHABLE
}

bool appendSourceFailure(const mir::MirFunction& function,
                         const OwnershipFunctionEventOverlay& overlay,
                         const InitializationFunction& initialization, const MirPoint& point,
                         const mir::MirPlace& place, const identity::SourceSpan& useSpan,
                         uint32_t operandOrdinal, uint32_t& traversalOrdinal,
                         zc::Vector<OwnershipSourceFailure>& failures,
                         const zc::Vector<mir::MirPlace>& linearPlaces) {
  auto operand = mir::MirOperand::copy(place.clone());
  return appendSourceFailure(function, overlay, initialization, point, operand, useSpan,
                             operandOrdinal, traversalOrdinal, failures, linearPlaces);
}

/// \brief Rule 7: each missing unsafe acknowledgement emits the failure for its
/// complete boundary key.
///
/// Acknowledged occurrences are skipped. Unsafe primaries are independent: they
/// neither suppress another unsafe occurrence nor any independent safe
/// ownership failure, so the caller retains every emitted failure and the
/// suppression pass leaves them untouched.
bool appendUnsafeAcknowledgementFailures(zc::Vector<OwnershipSourceFailure>& failures,
                                         const mir::VerifiedBuiltMir& builtMir,
                                         const VerifiedOwnershipEventOverlay& overlay,
                                         uint32_t& traversalOrdinal) {
  for (const auto& function : builtMir.functions()) {
    auto functionOverlay = overlayFor(overlay, function.owner);
    if (functionOverlay == zc::none) return false;
    ZC_IF_SOME(slice, functionOverlay) {
      for (const auto& occurrence : slice.unsafeOccurrences) {
        if (occurrence.acknowledgement != zc::none) continue;
        ZC_IREQUIRE(traversalOrdinal != UINT32_MAX, "ownership source traversal overflow");
        failures.add(RawPointerBoundaryRequiresUnsafeFailure{
            occurrence.key.event.location.owner, occurrence.key.event,
            occurrence.sourceSpan.clone(), traversalOrdinal++, occurrence.key});
      }
    }
  }
  return true;
}

zc::Maybe<zc::Vector<OwnershipSourceFailure>> sourceFailures(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedInitializationFacts& initialization) {
  zc::Vector<OwnershipSourceFailure> failures;
  uint32_t traversalOrdinal = 0;
  for (const auto& function : builtMir.functions()) {
    auto facts = initializationFor(initialization, function.owner);
    auto functionOverlay = overlayFor(overlay, function.owner);
    if (facts == zc::none || functionOverlay == zc::none) return zc::none;
    const auto linearPlaces = linearPositivePlaces(ZC_ASSERT_NONNULL(functionOverlay));
    for (const auto& block : function.blocks) {
      for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        const auto& statement = block.statements[ordinal];
        const auto point = MirPoint::beforeStatement(block.id, ordinal);
        switch (statement.kind()) {
          case mir::MirStatementKind::Assign:
            if (statement.assignmentValue().value.kind() != mir::MirRvalueKind::Use) break;
            if (!appendSourceFailure(
                    function, ZC_ASSERT_NONNULL(functionOverlay), ZC_ASSERT_NONNULL(facts), point,
                    statement.assignmentValue().value.useValue().operand, statement.sourceSpan(), 0,
                    traversalOrdinal, failures, linearPlaces)) {
              return zc::none;
            }
            break;
          case mir::MirStatementKind::BorrowCreation:
            if (!appendSourceFailure(function, ZC_ASSERT_NONNULL(functionOverlay),
                                     ZC_ASSERT_NONNULL(facts), point,
                                     statement.borrowCreationValue().source, statement.sourceSpan(),
                                     0, traversalOrdinal, failures, linearPlaces)) {
              return zc::none;
            }
            break;
          case mir::MirStatementKind::StorageLive:
          case mir::MirStatementKind::StorageDead:
          case mir::MirStatementKind::SetDiscriminant:
          case mir::MirStatementKind::Deinitialize:
          case mir::MirStatementKind::UnsafeScopeBoundary:
            break;
        }
      }
      const auto point = MirPoint::beforeTerminator(block.id);
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        ZC_IF_SOME(value, block.terminator.returnValue().value) {
          if (!appendSourceFailure(function, ZC_ASSERT_NONNULL(functionOverlay),
                                   ZC_ASSERT_NONNULL(facts), point, value,
                                   block.terminator.sourceSpan(), 0, traversalOrdinal, failures,
                                   linearPlaces)) {
            return zc::none;
          }
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
        uint32_t argumentOrdinal = 0;
        for (const auto& argument : block.terminator.callValue().arguments) {
          if (!appendSourceFailure(function, ZC_ASSERT_NONNULL(functionOverlay),
                                   ZC_ASSERT_NONNULL(facts), point, argument,
                                   block.terminator.sourceSpan(), argumentOrdinal++,
                                   traversalOrdinal, failures, linearPlaces)) {
            return zc::none;
          }
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
        if (!appendSourceFailure(
                function, ZC_ASSERT_NONNULL(functionOverlay), ZC_ASSERT_NONNULL(facts), point,
                block.terminator.switchIntValue().discriminant, block.terminator.sourceSpan(), 0,
                traversalOrdinal, failures, linearPlaces)) {
          return zc::none;
        }
      }
    }
  }
  if (!appendUnsafeAcknowledgementFailures(failures, builtMir, overlay, traversalOrdinal)) {
    return zc::none;
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
    auto facts = deriveFunction(function, flow.functions()[index], movePaths.functions()[index]);
    if (facts == zc::none) return zc::none;
    ZC_IF_SOME(value, facts) {
      if (!factsUseFlow(value, flow.functions()[index])) return zc::none;
      functions.add(zc::mv(value));
    }
  }
  return functions;
}

}  // namespace

zc::Vector<InitializationLossCause> InitializationLattice::mergeLossCauses(
    zc::ArrayPtr<const InitializationLossCause> left,
    zc::ArrayPtr<const InitializationLossCause> right) {
  zc::Vector<InitializationLossCause> merged;
  merged.reserve(left.size() + right.size());
  for (const auto& cause : left) {
    merged.add(InitializationLossCause{cause.kind, cloneEvent(cause.event),
                                       MovePathKey{cause.path.owner, cause.path.place.clone()}});
  }
  for (const auto& cause : right) {
    bool duplicate = false;
    for (const auto& existing : merged) {
      if (existing.kind == cause.kind && existing.event == cause.event &&
          existing.path.owner == cause.path.owner &&
          samePlace(existing.path.place, cause.path.place)) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      merged.add(InitializationLossCause{cause.kind, cloneEvent(cause.event),
                                         MovePathKey{cause.path.owner, cause.path.place.clone()}});
    }
  }
  // Canonicalize the union: loss causes arrive in arbitrary predecessor order,
  // so the published vector is sorted by (kind, event, path) independent of
  // input order. The dedup above collapsed equal causes to one entry, so the
  // sort is stable for the remaining distinct causes.
  for (size_t index = 1; index < merged.size(); ++index) {
    auto current = zc::mv(merged[index]);
    size_t insertion = index;
    while (insertion != 0 && lessLossCause(current, merged[insertion - 1])) {
      merged[insertion] = zc::mv(merged[insertion - 1]);
      --insertion;
    }
    merged[insertion] = zc::mv(current);
  }
  return merged;
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
    auto suppressed = SourceSuppression::suppress(zc::mv(values));
    auto deduplicated = OwnershipSourceFailureOrdering::deduplicate(zc::mv(suppressed));
    auto sorted =
        ir::SortedSourceFailureFacts<OwnershipSourceFailure, OwnershipSourceFailureOrdering>::from(
            zc::mv(deduplicated));
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

zc::Maybe<InitializationFunction> InitializationBuilder::deriveFunctionForTesting(
    const mir::MirFunction& function, const FlowFunction& flow, const MovePathFunction& paths) {
  return deriveFunction(function, flow, paths);
}

ir::IrOperationResult<InitializationCandidate> InitializationBuilder::build(
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay,
    const VerifiedFlow& flow, const VerifiedMovePaths& movePaths) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(builtMir, overlay, flow, movePaths)) {
    return rejectInvariant<InitializationCandidate>(builtMir, identities,
                                                    ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  if (!allFunctionsAdmitted(builtMir)) {
    return rejectInvariant<InitializationCandidate>(builtMir, identities,
                                                    ir::IrFailureKind::InvalidControlFlow, 2);
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
  if (!allFunctionsAdmitted(builtMir)) {
    return rejectInvariant<VerifiedInitializationFacts>(builtMir, identities,
                                                        ir::IrFailureKind::InvalidControlFlow, 2);
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
