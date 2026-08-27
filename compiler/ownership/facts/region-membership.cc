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

#include "compiler/ownership/facts/region-membership.h"

#include "compiler/ir/ir-diagnostic-adapter.h"

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
  ZC_IREQUIRE(fallback != zc::none, "Region membership failure fallback must be legal");
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

// ---------------------------------------------------------------------------
// Comparison helpers
// ---------------------------------------------------------------------------

/// \brief Orders two ownership points by kind then canonical location.
bool lessPoint(const OwnershipPoint& left, const OwnershipPoint& right) noexcept {
  if (left.kind() != right.kind()) {
    return static_cast<uint8_t>(left.kind()) < static_cast<uint8_t>(right.kind());
  }
  switch (left.kind()) {
    case OwnershipPointKind::Cfg:
      return left.cfgValue().point < right.cfgValue().point;
    case OwnershipPointKind::BeforeEvent:
      return lessEventKey(left.beforeEventValue().event, right.beforeEventValue().event);
    case OwnershipPointKind::AfterEvent:
      return lessEventKey(left.afterEventValue().event, right.afterEventValue().event);
  }
  return false;
}

/// \brief Orders two memberships canonically by region then point.
bool lessMembership(const RegionMembership& left, const RegionMembership& right) noexcept {
  if (left.region != right.region) return left.region < right.region;
  return lessPoint(left.point, right.point);
}

/// \brief Sorts memberships into canonical (region, point) order via insertion sort.
void sortMemberships(zc::Vector<RegionMembership>& memberships) {
  for (size_t index = 1; index < memberships.size(); ++index) {
    auto current = zc::mv(memberships[index]);
    size_t insertion = index;
    while (insertion > 0 && lessMembership(current, memberships[insertion - 1])) {
      memberships[insertion] = zc::mv(memberships[insertion - 1]);
      --insertion;
    }
    memberships[insertion] = zc::mv(current);
  }
}

// ---------------------------------------------------------------------------
// Region set operations (small-set linear scan; the admitted subset carries
// only a handful of live regions per point)
// ---------------------------------------------------------------------------

void addRegion(zc::Vector<RegionKey>& set, const RegionKey& key) {
  for (const auto& existing : set) {
    if (existing == key) return;
  }
  set.add(key.clone());
}

void unionInto(zc::Vector<RegionKey>& dst, zc::ArrayPtr<const RegionKey> src) {
  for (const auto& key : src) { addRegion(dst, key); }
}

zc::Vector<RegionKey> subtractSets(zc::ArrayPtr<const RegionKey> set,
                                   zc::ArrayPtr<const RegionKey> killed) {
  zc::Vector<RegionKey> result;
  for (const auto& key : set) {
    bool isKilled = false;
    for (const auto& dead : killed) {
      if (key == dead) {
        isKilled = true;
        break;
      }
    }
    if (!isKilled) result.add(key.clone());
  }
  return result;
}

/// \brief Returns true when two region sets carry the same keys.
///
/// Both vectors are sets (no duplicates), so equal size plus mutual
/// membership establishes set equality.
bool sameRegionSet(zc::ArrayPtr<const RegionKey> left, zc::ArrayPtr<const RegionKey> right) {
  if (left.size() != right.size()) return false;
  for (const auto& key : left) {
    bool found = false;
    for (const auto& other : right) {
      if (key == other) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Membership and lineage comparison
// ---------------------------------------------------------------------------

bool sameMemberships(zc::ArrayPtr<const RegionMembership> left,
                     zc::ArrayPtr<const RegionMembership> right) {
  if (left.size() != right.size()) return false;
  for (size_t index = 0; index < left.size(); ++index) {
    if (left[index].region != right[index].region || left[index].point != right[index].point) {
      return false;
    }
  }
  return true;
}

bool inputsMatch(const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
                 const mir::VerifiedBuiltMir& builtMir,
                 const VerifiedOwnershipEventOverlay& overlay) {
  return flow.semanticContext() == builtMir.semanticContext() &&
         loans.semanticContext() == builtMir.semanticContext() &&
         flow.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         loans.contextFingerprint().digest() == builtMir.contextFingerprint().digest() &&
         flow.module() == builtMir.module() && loans.module() == builtMir.module() &&
         flow.builtRevision().digest() == builtMir.revision().digest() &&
         loans.builtRevision().digest() == builtMir.revision().digest() &&
         flow.overlayRevision().digest() == overlay.revision().digest() &&
         loans.overlayRevision().digest() == overlay.revision().digest() &&
         loans.borrowEvidenceRevision().digest() == builtMir.borrowEvidenceRevision().digest();
}

// ---------------------------------------------------------------------------
// Function lookup
// ---------------------------------------------------------------------------

zc::Maybe<const OwnershipFunctionEventOverlay&> overlayFor(
    const VerifiedOwnershipEventOverlay& overlay, identity::DefId owner) {
  zc::Maybe<const OwnershipFunctionEventOverlay&> result;
  for (const auto& function : overlay.functions()) {
    if (function.owner != owner) continue;
    if (result != zc::none) return zc::none;
    result = function;
  }
  return result;
}

zc::Maybe<const mir::MirFunction&> mirFunctionFor(const mir::VerifiedBuiltMir& builtMir,
                                                  identity::DefId owner) {
  zc::Maybe<const mir::MirFunction&> result;
  for (const auto& function : builtMir.functions()) {
    if (function.owner != owner) continue;
    if (result != zc::none) return zc::none;
    result = function;
  }
  return result;
}

// ---------------------------------------------------------------------------
// Last-use computation (linear program order; mirrors the event-granular
// liveness used by the borrow-source verifier)
// ---------------------------------------------------------------------------

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
  for (uint32_t index = 0; index < function.blocks.size(); ++index) {
    if (function.blocks[index].id == block) return index;
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
      auto index = findBlockIndex(function, point.beforeStatementValue().block);
      ZC_IF_SOME(value, index) {
        return EventPosition{value, 1 + 2 * point.beforeStatementValue().ordinal};
      }
      return zc::none;
    }
    case MirPointKind::AfterStatement: {
      auto index = findBlockIndex(function, point.afterStatementValue().block);
      ZC_IF_SOME(value, index) {
        return EventPosition{value, 2 + 2 * point.afterStatementValue().ordinal};
      }
      return zc::none;
    }
    case MirPointKind::BeforeTerminator: {
      auto index = findBlockIndex(function, point.beforeTerminatorValue().block);
      ZC_IF_SOME(value, index) {
        return EventPosition{value, 1 + 2 * function.blocks[value].statements.size()};
      }
      return zc::none;
    }
    case MirPointKind::Edge: {
      auto index = findBlockIndex(function, point.edgeValue().from);
      ZC_IF_SOME(value, index) {
        return EventPosition{value, 2 + 2 * function.blocks[value].statements.size()};
      }
      return zc::none;
    }
    case MirPointKind::Exit: {
      auto index = findBlockIndex(function, point.exitValue().block);
      ZC_IF_SOME(value, index) {
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
  auto leftPosition = eventPosition(function, left.location.point);
  auto rightPosition = eventPosition(function, right.location.point);
  ZC_IF_SOME(leftValue, leftPosition) {
    ZC_IF_SOME(rightValue, rightPosition) {
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

// ---------------------------------------------------------------------------
// Forward dataflow
// ---------------------------------------------------------------------------

zc::Maybe<size_t> pointIndex(const FlowFunction& flow, const OwnershipPoint& point) {
  for (size_t index = 0; index < flow.points.size(); ++index) {
    if (flow.points[index] == point) return index;
  }
  return zc::none;
}

/// \brief Seeds Input regions at the function entry CFG point.
///
/// Every parameter local with a verified EntryRoot slot introduces one Input
/// region keyed by its local ordinal. Input regions are never killed: a borrow
/// input is live for the entire function body.
bool seedInputRegions(const FlowFunction& flow, const mir::MirFunction& function,
                      const OwnershipFunctionEventOverlay& overlay,
                      zc::Vector<zc::Vector<RegionKey>>& seeds) {
  zc::Maybe<size_t> entryIndex;
  for (size_t index = 0; index < flow.points.size(); ++index) {
    if (flow.points[index].kind() != OwnershipPointKind::Cfg) continue;
    if (flow.points[index].cfgValue().point.kind() != MirPointKind::Entry) continue;
    if (entryIndex != zc::none) return false;
    entryIndex = index;
  }
  if (entryIndex == zc::none) return false;
  for (const auto& slot : overlay.slots) {
    if (slot.key.location.point.kind() != MirPointKind::Entry) continue;
    if (slot.stage != OwnershipEventStage::Commit) continue;
    if (slot.roles.size() != 1 || slot.roles[0] != OwnershipEventRole::EntryRoot) continue;
    const uint32_t ordinal = slot.key.operandOrdinal;
    if (ordinal >= function.locals.size()) return false;
    if (function.locals[ordinal].kind != mir::MirLocalKind::Parameter) continue;
    addRegion(seeds[ZC_ASSERT_NONNULL(entryIndex)],
              RegionKey::inputRegion(function.owner, BorrowInputKey::parameter(ordinal)));
  }
  return true;
}

/// \brief Seeds Loan regions at their activation point and registers the kill
/// at the AfterEvent of the destination's last use.
///
/// A loan with no recorded last use is never killed: its destination is not
/// read on any path, so the loan remains live through every reachable point.
bool seedLoanRegions(const FlowFunction& flow, const mir::MirFunction& function,
                     zc::ArrayPtr<const LoanFact> allLoans,
                     zc::Vector<zc::Vector<RegionKey>>& seeds,
                     zc::Vector<zc::Vector<RegionKey>>& kills) {
  for (const auto& loan : allLoans) {
    if (loan.owner != function.owner) continue;
    auto activeIndex = pointIndex(flow, loan.activeFrom);
    if (activeIndex == zc::none) return false;
    RegionKey region = RegionKey::loanRegion(LoanKey{loan.issue});
    auto lastUse = lastUseOfDestination(function, loan.destination.place);
    if (lastUse == zc::none) {
      addRegion(seeds[ZC_ASSERT_NONNULL(activeIndex)], region);
      continue;
    }
    ZC_IF_SOME(lastUseEvent, lastUse) {
      auto killIndex = pointIndex(flow, OwnershipPoint::afterEvent(lastUseEvent));
      if (killIndex == zc::none) return false;
      addRegion(seeds[ZC_ASSERT_NONNULL(activeIndex)], region);
      addRegion(kills[ZC_ASSERT_NONNULL(killIndex)], zc::mv(region));
    }
  }
  return true;
}

/// \brief Runs the forward dataflow over one flow function.
///
/// Uses a worklist fixpoint: live-in is the union of predecessor live-out
/// sets and live-out is (live-in ∪ seeds) \ kills. The admitted subset is
/// reducible and may carry loop back edges, so a single topological pass
/// does not suffice. Each point is re-enqueued whenever its live-in set
/// grows, and live-out is recomputed until no point changes. Region sets
/// only grow, so the iteration converges. A region is live at point P iff
/// it is in live-out[P].
zc::Maybe<zc::Vector<RegionMembership>> deriveFunction(const FlowFunction& flow,
                                                       const mir::MirFunction& function,
                                                       const OwnershipFunctionEventOverlay& overlay,
                                                       zc::ArrayPtr<const LoanFact> allLoans) {
  const size_t pointCount = flow.points.size();
  zc::Vector<zc::Vector<size_t>> successors;
  successors.resize(pointCount);
  for (const auto& edge : flow.edges) {
    auto from = pointIndex(flow, edge.from);
    auto to = pointIndex(flow, edge.to);
    if (from == zc::none || to == zc::none) return zc::none;
    successors[ZC_ASSERT_NONNULL(from)].add(ZC_ASSERT_NONNULL(to));
  }

  zc::Vector<zc::Vector<RegionKey>> seeds;
  zc::Vector<zc::Vector<RegionKey>> kills;
  seeds.resize(pointCount);
  kills.resize(pointCount);
  if (!seedInputRegions(flow, function, overlay, seeds)) return zc::none;
  if (!seedLoanRegions(flow, function, allLoans, seeds, kills)) return zc::none;

  zc::Vector<zc::Vector<RegionKey>> liveIn;
  zc::Vector<zc::Vector<RegionKey>> liveOut;
  liveIn.resize(pointCount);
  liveOut.resize(pointCount);
  zc::Vector<size_t> worklist;
  zc::Vector<bool> pending;
  pending.resize(pointCount);
  for (size_t index = 0; index < pointCount; ++index) {
    worklist.add(index);
    pending[index] = true;
  }
  while (worklist.size() != 0) {
    const size_t current = worklist[worklist.size() - 1];
    worklist.removeLast();
    pending[current] = false;

    zc::Vector<RegionKey> merged;
    unionInto(merged, liveIn[current].asPtr());
    unionInto(merged, seeds[current].asPtr());
    auto newOut = subtractSets(merged.asPtr(), kills[current].asPtr());
    if (sameRegionSet(newOut.asPtr(), liveOut[current].asPtr())) continue;
    liveOut[current] = zc::mv(newOut);

    for (const size_t successor : successors[current]) {
      const size_t before = liveIn[successor].size();
      unionInto(liveIn[successor], liveOut[current].asPtr());
      if (liveIn[successor].size() != before && !pending[successor]) {
        pending[successor] = true;
        worklist.add(successor);
      }
    }
  }

  zc::Vector<RegionMembership> memberships;
  for (size_t index = 0; index < pointCount; ++index) {
    for (const auto& region : liveOut[index]) {
      memberships.add(RegionMembership{region.clone(), flow.points[index]});
    }
  }
  return memberships;
}

zc::Maybe<zc::Vector<RegionMembership>> derive(const VerifiedFlow& flow,
                                               const VerifiedLoanFacts& loans,
                                               const mir::VerifiedBuiltMir& builtMir,
                                               const VerifiedOwnershipEventOverlay& overlay) {
  zc::Vector<RegionMembership> memberships;
  for (const auto& flowFunction : flow.functions()) {
    auto mirFunction = mirFunctionFor(builtMir, flowFunction.owner);
    auto functionOverlay = overlayFor(overlay, flowFunction.owner);
    if (mirFunction == zc::none || functionOverlay == zc::none) return zc::none;
    auto functionMemberships = deriveFunction(flowFunction, ZC_ASSERT_NONNULL(mirFunction),
                                              ZC_ASSERT_NONNULL(functionOverlay), loans.loans());
    if (functionMemberships == zc::none) return zc::none;
    ZC_IF_SOME(value, functionMemberships) {
      for (size_t index = 0; index < value.size(); ++index) {
        memberships.add(zc::mv(value[index]));
      }
    }
  }
  sortMemberships(memberships);
  return memberships;
}

}  // namespace

RegionMembershipCandidate::RegionMembershipCandidate(
    identity::SemanticContextBrand semanticContext,
    identity::ContextFingerprint&& contextFingerprint, identity::ModuleId module,
    mir::MirRevisionId builtRevision, OwnershipEventOverlayRevision overlayRevision,
    driver::borrow_evidence::BorrowEvidenceRevision borrowEvidenceRevision,
    zc::Vector<RegionMembership>&& memberships) noexcept
    : semanticContext(semanticContext),
      contextFingerprint(zc::mv(contextFingerprint)),
      module(module),
      builtRevision(builtRevision),
      overlayRevision(overlayRevision),
      borrowEvidenceRevision(borrowEvidenceRevision),
      memberships(zc::mv(memberships)) {}

struct VerifiedRegionMemberships::Impl final {
  explicit Impl(RegionMembershipCandidate&& candidate) noexcept : candidate(zc::mv(candidate)) {}
  RegionMembershipCandidate candidate;
};

VerifiedRegionMemberships::VerifiedRegionMemberships(zc::Own<Impl>&& impl) noexcept
    : impl(zc::mv(impl)) {}
VerifiedRegionMemberships::~VerifiedRegionMemberships() noexcept(false) = default;
VerifiedRegionMemberships::VerifiedRegionMemberships(VerifiedRegionMemberships&&) noexcept =
    default;
VerifiedRegionMemberships& VerifiedRegionMemberships::operator=(
    VerifiedRegionMemberships&&) noexcept = default;
identity::SemanticContextBrand VerifiedRegionMemberships::semanticContext() const noexcept {
  return impl->candidate.semanticContext;
}
const identity::ContextFingerprint& VerifiedRegionMemberships::contextFingerprint() const noexcept {
  return impl->candidate.contextFingerprint;
}
identity::ModuleId VerifiedRegionMemberships::module() const noexcept {
  return impl->candidate.module;
}
const mir::MirRevisionId& VerifiedRegionMemberships::builtRevision() const noexcept {
  return impl->candidate.builtRevision;
}
const OwnershipEventOverlayRevision& VerifiedRegionMemberships::overlayRevision() const noexcept {
  return impl->candidate.overlayRevision;
}
const driver::borrow_evidence::BorrowEvidenceRevision&
VerifiedRegionMemberships::borrowEvidenceRevision() const noexcept {
  return impl->candidate.borrowEvidenceRevision;
}
zc::ArrayPtr<const RegionMembership> VerifiedRegionMemberships::memberships() const noexcept {
  return impl->candidate.memberships.asPtr();
}

ir::IrOperationResult<RegionMembershipCandidate> RegionMembershipBuilder::build(
    const VerifiedFlow& flow, const VerifiedLoanFacts& loans, const mir::VerifiedBuiltMir& builtMir,
    const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (!inputsMatch(flow, loans, builtMir, overlay)) {
    return reject<RegionMembershipCandidate>(builtMir, identities,
                                             ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto memberships = derive(flow, loans, builtMir, overlay);
  if (memberships == zc::none) {
    return reject<RegionMembershipCandidate>(builtMir, identities,
                                             ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  ZC_IF_SOME(value, memberships) {
    return ir::IrOperationResult<RegionMembershipCandidate>::verified(RegionMembershipCandidate(
        builtMir.semanticContext(), builtMir.contextFingerprint().clone(), builtMir.module(),
        builtMir.revision(), overlay.revision(), builtMir.borrowEvidenceRevision(), zc::mv(value)));
  }
  ZC_UNREACHABLE
}

ir::IrOperationResult<VerifiedRegionMemberships> RegionMembershipVerifier::verify(
    RegionMembershipCandidate&& candidate, const VerifiedFlow& flow, const VerifiedLoanFacts& loans,
    const mir::VerifiedBuiltMir& builtMir, const VerifiedOwnershipEventOverlay& overlay) {
  const auto identities = builtMir.retainIdentityAuthority();
  if (candidate.semanticContext != builtMir.semanticContext() ||
      candidate.contextFingerprint.digest() != builtMir.contextFingerprint().digest() ||
      candidate.module != builtMir.module() ||
      candidate.builtRevision.digest() != builtMir.revision().digest() ||
      candidate.overlayRevision.digest() != overlay.revision().digest() ||
      candidate.borrowEvidenceRevision.digest() != builtMir.borrowEvidenceRevision().digest() ||
      !inputsMatch(flow, loans, builtMir, overlay)) {
    return reject<VerifiedRegionMemberships>(builtMir, identities,
                                             ir::IrFailureKind::InputRevisionMismatch, 0);
  }
  auto expected = derive(flow, loans, builtMir, overlay);
  if (expected == zc::none ||
      !sameMemberships(candidate.memberships.asPtr(), ZC_ASSERT_NONNULL(expected).asPtr())) {
    return reject<VerifiedRegionMemberships>(builtMir, identities,
                                             ir::IrFailureKind::InvalidOwnershipProof, 1);
  }
  return ir::IrOperationResult<VerifiedRegionMemberships>::verified(
      VerifiedRegionMemberships(zc::heap<VerifiedRegionMemberships::Impl>(zc::mv(candidate))));
}

}  // namespace zomlang::compiler::ownership::facts
