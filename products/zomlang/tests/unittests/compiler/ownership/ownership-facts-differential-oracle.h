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

#pragma once

#include <cstdint>

#include "zc/core/array.h"
#include "zc/core/common.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/driver/interface/borrow-evidence.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/flow.h"
#include "zomlang/compiler/ownership/facts/init.h"
#include "zomlang/compiler/ownership/facts/loans.h"
#include "zomlang/compiler/ownership/facts/paths.h"
#include "zomlang/compiler/ownership/facts/refs.h"
#include "zomlang/compiler/ownership/facts/regions.h"
#include "zomlang/compiler/ownership/facts/resources.h"
#include "zomlang/compiler/ownership/facts/states.h"
#include "zomlang/compiler/ownership/ownership-event-overlay.h"

namespace zomlang::compiler::ownership {
namespace test_oracle {

/// \file
/// Differential oracle for RFC 0007 ownership facts.
///
/// Every inventory in `facts/` is published by a builder and independently
/// reconstructed by a verifier that shares the builder's file-local
/// derivation. This oracle is a third implementation: it recomputes every
/// inventory directly from Built MIR, the event overlay, and the borrow
/// evidence using deliberately different drivers, then compares the result
/// against the production derivation as sets. It never calls a production
/// builder or verifier and shares no derivation code with them; only the
/// public fact data structures are reused.
///
/// The independent drivers are:
/// - move paths: reverse-order MIR walk into an insertion-ordered set with an
///   oracle-local conflict predicate, versus the production forward walk and
///   identity-authority sort;
/// - flow: declarative per-block location chains, versus the production DFS;
/// - initialization: demand-driven recursive memoized state computation with a
///   cycle guard, versus the production linear block walk;
/// - loans: overlay slot-driven reconstruction, versus the production MIR
///   statement scan;
/// - references: return-terminator-driven reconstruction, versus the
///   production loan scan;
/// - regions: iterative reachability over the oracle flow, versus the
///   production recursive reachability;
/// - states: emitted directly from reference live points, versus the
///   production region-member projection; and
/// - resources: MIR-initialization-driven reconstruction, versus the
///   production overlay-plan two-pass walk.

// ---------------------------------------------------------------------------
// Structural equality helpers (oracle-local, independent of production).
// ---------------------------------------------------------------------------

inline bool sameProjection(const mir::MirProjection& left, const mir::MirProjection& right) {
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

inline bool samePlace(const mir::MirPlace& left, const mir::MirPlace& right) {
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

inline bool sameEventKey(const MirEventKey& left, const MirEventKey& right) {
  return left.location.owner == right.location.owner &&
         left.location.point == right.location.point && left.operandOrdinal == right.operandOrdinal;
}

inline bool sameOwnershipPoint(const facts::OwnershipPoint& left,
                               const facts::OwnershipPoint& right) {
  if (left.kind() != right.kind()) return false;
  switch (left.kind()) {
    case facts::OwnershipPointKind::Cfg:
      return left.cfgValue().point == right.cfgValue().point;
    case facts::OwnershipPointKind::BeforeEvent:
      return sameEventKey(left.beforeEventValue().event, right.beforeEventValue().event);
    case facts::OwnershipPointKind::AfterEvent:
      return sameEventKey(left.afterEventValue().event, right.afterEventValue().event);
  }
  return false;
}

inline bool sameMovePathKey(const facts::MovePathKey& left, const facts::MovePathKey& right) {
  return left.owner == right.owner && samePlace(left.place, right.place);
}

inline bool sameLossCause(const facts::InitializationLossCause& left,
                          const facts::InitializationLossCause& right) {
  return left.kind == right.kind && sameEventKey(left.event, right.event) &&
         sameMovePathKey(left.path, right.path);
}

// Oracle-local canonical loss-cause ordering, mirroring the production
// `lessLossCause` in facts/init.cc. The oracle shares no derivation code with
// production, so the comparator is duplicated here; both must agree because
// `sameInitializationFact` compares cause vectors index-wise.
inline bool lessEvent(const MirEventKey& left, const MirEventKey& right) {
  if (left.location.point < right.location.point) return true;
  if (right.location.point < left.location.point) return false;
  return left.operandOrdinal < right.operandOrdinal;
}

inline bool lessProjection(const mir::MirProjection& left, const mir::MirProjection& right) {
  if (left.kind() != right.kind()) {
    return static_cast<uint8_t>(left.kind()) < static_cast<uint8_t>(right.kind());
  }
  switch (left.kind()) {
    case mir::MirProjectionKind::Field:
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

inline bool lessPlace(const mir::MirPlace& left, const mir::MirPlace& right) {
  if (left.local() != right.local()) return left.local().ordinal() < right.local().ordinal();
  const auto shared = zc::min(left.projections().size(), right.projections().size());
  for (size_t index = 0; index < shared; ++index) {
    if (lessProjection(left.projections()[index], right.projections()[index])) return true;
    if (lessProjection(right.projections()[index], left.projections()[index])) return false;
  }
  return left.projections().size() < right.projections().size();
}

inline bool lessLossCause(const facts::InitializationLossCause& left,
                          const facts::InitializationLossCause& right) {
  if (left.kind != right.kind) {
    return static_cast<uint8_t>(left.kind) < static_cast<uint8_t>(right.kind);
  }
  if (lessEvent(left.event, right.event)) return true;
  if (lessEvent(right.event, left.event)) return false;
  if (left.path.owner != right.path.owner) return false;
  return lessPlace(left.path.place, right.path.place);
}

inline bool sameDropAction(const zc::Maybe<LogicalDropAction>& left,
                           const zc::Maybe<LogicalDropAction>& right) {
  if (left == zc::none) return right == zc::none;
  if (right == zc::none) return false;
  ZC_IF_SOME(leftAction, left) {
    ZC_IF_SOME(rightAction, right) {
      if (leftAction.is<LogicalDropDeclaredAction>() !=
          rightAction.is<LogicalDropDeclaredAction>()) {
        return false;
      }
      if (leftAction.is<LogicalDropDeclaredAction>()) {
        return leftAction.get<LogicalDropDeclaredAction>().deinitializer ==
               rightAction.get<LogicalDropDeclaredAction>().deinitializer;
      }
      if (leftAction.is<LogicalDropBuiltinAction>()) {
        return leftAction.get<LogicalDropBuiltinAction>().ownerType ==
               rightAction.get<LogicalDropBuiltinAction>().ownerType;
      }
      return leftAction.get<LogicalDropDynamicAction>().existentialType ==
             rightAction.get<LogicalDropDynamicAction>().existentialType;
    }
  }
  return false;
}

/// \brief Oracle-local place-conflict predicate (distinct sibling fields and
/// downcast variants are disjoint; subslices overlap on range intersection;
/// indices, dereferences, and mixed kinds may alias; a projection prefix
/// contains its descendant).
inline bool oraclePlacesConflict(const mir::MirPlace& first, const mir::MirPlace& second) {
  if (first.local() != second.local()) return false;
  const size_t shared = zc::min(first.projections().size(), second.projections().size());
  for (size_t index = 0; index < shared; ++index) {
    const auto& firstProjection = first.projections()[index];
    const auto& secondProjection = second.projections()[index];
    if (sameProjection(firstProjection, secondProjection)) continue;
    if (firstProjection.kind() == mir::MirProjectionKind::Field &&
        secondProjection.kind() == mir::MirProjectionKind::Field) {
      return false;
    }
    if (firstProjection.kind() == mir::MirProjectionKind::Downcast &&
        secondProjection.kind() == mir::MirProjectionKind::Downcast) {
      return false;
    }
    if (firstProjection.kind() == mir::MirProjectionKind::Subslice &&
        secondProjection.kind() == mir::MirProjectionKind::Subslice) {
      const uint32_t overlapStart =
          zc::max(firstProjection.subsliceValue().first, secondProjection.subsliceValue().first);
      const uint32_t overlapEnd = zc::min(firstProjection.subsliceValue().pastLast,
                                          secondProjection.subsliceValue().pastLast);
      return overlapStart < overlapEnd;
    }
    return true;
  }
  return true;
}

/// \brief Removes the first element of `remaining` equal to `value`
/// under `equal`. Returns false when no element matches.
template <typename T, typename Equal>
bool takeMatch(const T& value, zc::Vector<const T*>& remaining, Equal equal) {
  for (size_t index = 0; index < remaining.size(); ++index) {
    if (equal(value, *remaining[index])) {
      remaining[index] = remaining[remaining.size() - 1];
      remaining.removeLast();
      return true;
    }
  }
  return false;
}

template <typename T, typename Equal>
bool sameRecordSet(zc::ArrayPtr<const T> left, zc::ArrayPtr<const T> right, Equal equal) {
  if (left.size() != right.size()) return false;
  zc::Vector<const T*> remaining;
  for (const auto& value : right) remaining.add(&value);
  for (const auto& value : left) {
    if (!takeMatch(value, remaining, equal)) return false;
  }
  return remaining.size() == 0;
}

// ---------------------------------------------------------------------------
// The differential oracle.
// ---------------------------------------------------------------------------

class OwnershipFactsOracle final {
public:
  OwnershipFactsOracle(const mir::VerifiedBuiltMir& builtMir,
                       const VerifiedOwnershipEventOverlay& overlay,
                       const driver::borrow_evidence::VerifiedBorrowEvidence& evidence)
      : builtMir(builtMir), overlay(overlay), evidence(evidence) {}

  /// \brief Independently recomputes the move-path inventory.
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::MovePathFunction>> movePaths() const {
    zc::Vector<facts::MovePathFunction> functions;
    for (const auto& function : builtMir.functions()) {
      auto derived = deriveMovePaths(function);
      if (derived == zc::none) return zc::none;
      ZC_IF_SOME(value, derived) { functions.add(zc::mv(value)); }
    }
    return functions;
  }

  /// \brief Independently recomputes the flow inventory.
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::FlowFunction>> flow() const {
    zc::Vector<facts::FlowFunction> functions;
    for (const auto& function : builtMir.functions()) {
      auto functionOverlay = overlayFunction(function.owner);
      if (functionOverlay == zc::none) return zc::none;
      ZC_IF_SOME(value, functionOverlay) {
        auto derived = deriveFlow(function, value);
        if (derived == zc::none) return zc::none;
        ZC_IF_SOME(flowValue, derived) { functions.add(zc::mv(flowValue)); }
      }
    }
    return functions;
  }

  /// \brief Independently recomputes initialization facts from oracle-owned
  /// move paths (never the production inventory).
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::InitializationFunction>> initialization() const {
    auto paths = movePaths();
    if (paths == zc::none) return zc::none;
    zc::Vector<facts::InitializationFunction> functions;
    ZC_IF_SOME(pathValues, paths) {
      if (pathValues.size() != builtMir.functions().size()) return zc::none;
      for (size_t index = 0; index < builtMir.functions().size(); ++index) {
        auto derived = deriveInitialization(builtMir.functions()[index], pathValues[index]);
        if (derived == zc::none) return zc::none;
        ZC_IF_SOME(value, derived) { functions.add(zc::mv(value)); }
      }
    }
    return functions;
  }

  /// \brief Independently recomputes the loan inventory from overlay issue
  /// slots (the production derivation scans MIR statements).
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::LoanFact>> loans() const {
    auto paths = movePaths();
    if (paths == zc::none) return zc::none;
    zc::Vector<facts::LoanFact> loans;
    ZC_IF_SOME(pathValues, paths) {
      for (const auto& function : builtMir.functions()) {
        auto functionOverlay = overlayFunction(function.owner);
        if (functionOverlay == zc::none) return zc::none;
        ZC_IF_SOME(value, functionOverlay) {
          for (const auto& slot : value.slots) {
            if (!hasRole(slot, OwnershipEventRole::BorrowIssue)) continue;
            auto loan = deriveLoan(function, value, pathValues, slot);
            if (loan == zc::none) return zc::none;
            ZC_IF_SOME(loanValue, loan) { loans.add(zc::mv(loanValue)); }
          }
        }
      }
    }
    return loans;
  }

  /// \brief Independently recomputes reference definitions from return
  /// terminators (the production derivation scans loans).
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::ReferenceDefinition>> references() const {
    auto paths = movePaths();
    auto loanValues = loans();
    if (paths == zc::none || loanValues == zc::none) return zc::none;
    zc::Vector<facts::ReferenceDefinition> definitions;
    ZC_IF_SOME(pathValues, paths) {
      ZC_IF_SOME(loansValue, loanValues) {
        for (const auto& function : builtMir.functions()) {
          auto functionOverlay = overlayFunction(function.owner);
          if (functionOverlay == zc::none) return zc::none;
          ZC_IF_SOME(value, functionOverlay) {
            auto derived = deriveReferences(function, value, pathValues, loansValue);
            if (derived == zc::none) return zc::none;
            ZC_IF_SOME(definitionsValue, derived) {
              for (auto& definition : definitionsValue) definitions.add(zc::mv(definition));
            }
          }
        }
      }
    }
    return definitions;
  }

  /// \brief Independently recomputes reborrow regions with iterative
  /// reachability over the oracle flow.
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::ReborrowRegion>> regions() const {
    auto flowValues = flow();
    auto loanValues = loans();
    auto referenceValues = references();
    if (flowValues == zc::none || loanValues == zc::none || referenceValues == zc::none) {
      return zc::none;
    }
    zc::Vector<facts::ReborrowRegion> regions;
    ZC_IF_SOME(flows, flowValues) {
      ZC_IF_SOME(loans, loanValues) {
        ZC_IF_SOME(references, referenceValues) {
          for (const auto& reference : references) {
            auto region = deriveRegion(flows, loans, reference);
            if (region == zc::none) return zc::none;
            ZC_IF_SOME(value, region) { regions.add(zc::mv(value)); }
          }
        }
      }
    }
    return regions;
  }

  /// \brief Independently recomputes reference states directly from reference
  /// live points (the production derivation projects region members).
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::ReborrowState>> states() const {
    auto referenceValues = references();
    if (referenceValues == zc::none) return zc::none;
    zc::Vector<facts::ReborrowState> states;
    ZC_IF_SOME(references, referenceValues) {
      for (const auto& reference : references) {
        const facts::OwnershipPoint points[] = {
            facts::OwnershipPoint(reference.livePoints.afterCommit),
            facts::OwnershipPoint(reference.livePoints.afterCommitCfg),
            facts::OwnershipPoint(reference.livePoints.beforeReturnCfg),
            facts::OwnershipPoint(reference.livePoints.beforeReturn),
            facts::OwnershipPoint(reference.livePoints.afterReturn)};
        for (const auto& point : points) {
          states.add(facts::ReborrowState{reference.owner, facts::OwnershipPoint(point),
                                          reference.loan, reference.origin.detail,
                                          facts::MovePathKey{reference.destination.owner,
                                                             reference.destination.place.clone()}});
        }
      }
    }
    return states;
  }

  /// \brief Independently recomputes logical resources from MIR
  /// initializations (the production derivation walks overlay plans).
  ZC_NODISCARD zc::Maybe<zc::Vector<facts::OwnershipResourceFunction>> resources() const {
    auto paths = movePaths();
    if (paths == zc::none) return zc::none;
    zc::Vector<facts::OwnershipResourceFunction> functions;
    ZC_IF_SOME(pathValues, paths) {
      if (pathValues.size() != builtMir.functions().size()) return zc::none;
      for (size_t index = 0; index < builtMir.functions().size(); ++index) {
        auto functionOverlay = overlayFunction(builtMir.functions()[index].owner);
        if (functionOverlay == zc::none) return zc::none;
        ZC_IF_SOME(value, functionOverlay) {
          auto derived = deriveResources(builtMir.functions()[index], pathValues[index], value);
          if (derived == zc::none) return zc::none;
          ZC_IF_SOME(resourceValue, derived) { functions.add(zc::mv(resourceValue)); }
        }
      }
    }
    return functions;
  }

private:
  // ---- shared lookups -----------------------------------------------------

  ZC_NODISCARD zc::Maybe<const OwnershipFunctionEventOverlay&> overlayFunction(
      identity::DefId owner) const {
    for (const auto& function : overlay.functions()) {
      if (function.owner == owner) return function;
    }
    return zc::none;
  }

  ZC_NODISCARD static zc::Maybe<size_t> localIndex(const mir::MirFunction& function,
                                                   mir::MirLocalId local) {
    for (size_t index = 0; index < function.locals.size(); ++index) {
      if (function.locals[index].id == local) return index;
    }
    return zc::none;
  }

  ZC_NODISCARD static zc::Maybe<size_t> blockIndex(const mir::MirFunction& function,
                                                   mir::MirBlockId block) {
    for (size_t index = 0; index < function.blocks.size(); ++index) {
      if (function.blocks[index].id == block) return index;
    }
    return zc::none;
  }

  static bool hasRole(const MirEventSlot& slot, OwnershipEventRole role) {
    for (const auto candidate : slot.roles) {
      if (candidate == role) return true;
    }
    return false;
  }

  ZC_NODISCARD static bool validLocalPlace(const mir::MirFunction& function,
                                           const mir::MirPlace& place) {
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
    return false;
  }

  ZC_NODISCARD static bool isPathWithin(const mir::MirPlace& root, const mir::MirPlace& candidate) {
    if (root.local() != candidate.local() || root.rootType() != candidate.rootType() ||
        root.projections().size() > candidate.projections().size()) {
      return false;
    }
    for (size_t index = 0; index < root.projections().size(); ++index) {
      if (!sameProjection(root.projections()[index], candidate.projections()[index])) return false;
    }
    return true;
  }

  // ---- move paths ----------------------------------------------------------

  static void collectPlace(zc::Vector<mir::MirPlace>& paths, const mir::MirPlace& place) {
    if (!place.hasConsistentTypeChain()) return;
    for (const auto& projection : place.projections()) {
      if (!projection.isStructurallyValid()) return;
    }
    for (size_t length = 0; length <= place.projections().size(); ++length) {
      zc::Vector<mir::MirProjection> projections;
      for (size_t index = 0; index < length; ++index) {
        projections.add(place.projections()[index].clone());
      }
      const auto resultType =
          length == 0 ? place.rootType() : place.projections()[length - 1].resultType();
      auto prefix = mir::MirPlace(place.local(), place.rootType(), zc::mv(projections), resultType);
      bool found = false;
      for (const auto& existing : paths) {
        if (samePlace(existing, prefix)) {
          found = true;
          break;
        }
      }
      if (!found) paths.add(zc::mv(prefix));
    }
  }

  ZC_NODISCARD zc::Maybe<facts::MovePathFunction> deriveMovePaths(
      const mir::MirFunction& function) const {
    // Deliberately reverse traversal order relative to the production walk:
    // terminators first, then statements back to front, then local roots.
    zc::Vector<mir::MirPlace> paths;
    for (size_t blockIndex = function.blocks.size(); blockIndex != 0; --blockIndex) {
      const auto& block = function.blocks[blockIndex - 1];
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        ZC_IF_SOME(operand, block.terminator.returnValue().value) {
          if (operand.kind() != mir::MirOperandKind::Constant) {
            collectPlace(paths, operand.place());
          }
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
        const auto& call = block.terminator.callValue();
        collectPlace(paths, call.destination);
        for (const auto& argument : call.arguments) {
          if (argument.kind() != mir::MirOperandKind::Constant)
            collectPlace(paths, argument.place());
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
        const auto& discriminant = block.terminator.switchIntValue().discriminant;
        if (discriminant.kind() != mir::MirOperandKind::Constant) {
          collectPlace(paths, discriminant.place());
        }
      }
      for (size_t ordinal = block.statements.size(); ordinal != 0; --ordinal) {
        const auto& statement = block.statements[ordinal - 1];
        switch (statement.kind()) {
          case mir::MirStatementKind::Assign: {
            const auto& assignment = statement.assignmentValue();
            collectPlace(paths, assignment.destination);
            if (assignment.value.kind() == mir::MirRvalueKind::Use) {
              const auto& operand = assignment.value.useValue().operand;
              if (operand.kind() != mir::MirOperandKind::Constant)
                collectPlace(paths, operand.place());
            }
            break;
          }
          case mir::MirStatementKind::BorrowCreation:
            collectPlace(paths, statement.borrowCreationValue().destination);
            collectPlace(paths, statement.borrowCreationValue().source);
            break;
          case mir::MirStatementKind::SetDiscriminant:
            collectPlace(paths, statement.setDiscriminantValue().destination);
            break;
          case mir::MirStatementKind::Deinitialize:
            collectPlace(paths, statement.deinitializeValue().destination);
            break;
          case mir::MirStatementKind::StorageLive:
          case mir::MirStatementKind::StorageDead:
          case mir::MirStatementKind::UnsafeScopeBoundary:
            break;
        }
      }
    }
    for (const auto& local : function.locals) {
      zc::Vector<mir::MirProjection> projections;
      collectPlace(paths, mir::MirPlace(local.id, local.type, zc::mv(projections), local.type));
    }

    zc::Vector<facts::MovePathFact> facts;
    for (const auto& path : paths) {
      zc::Maybe<facts::MovePathKey> parent;
      if (path.projections().size() != 0) {
        zc::Vector<mir::MirProjection> parentProjections;
        for (size_t index = 0; index + 1 < path.projections().size(); ++index) {
          parentProjections.add(path.projections()[index].clone());
        }
        const auto parentType =
            path.projections().size() == 1
                ? path.rootType()
                : path.projections()[path.projections().size() - 2].resultType();
        auto parentPlace =
            mir::MirPlace(path.local(), path.rootType(), zc::mv(parentProjections), parentType);
        bool found = false;
        for (const auto& existing : paths) {
          if (samePlace(existing, parentPlace)) {
            found = true;
            break;
          }
        }
        if (!found) return zc::none;
        parent = facts::MovePathKey{function.owner, zc::mv(parentPlace)};
      }
      facts.add(
          facts::MovePathFact{facts::MovePathKey{function.owner, path.clone()}, zc::mv(parent)});
    }
    zc::Vector<facts::MovePathPair> conflicts;
    for (size_t first = 0; first < facts.size(); ++first) {
      for (size_t second = first + 1; second < facts.size(); ++second) {
        if (!oraclePlacesConflict(facts[first].key.place, facts[second].key.place)) continue;
        conflicts.add(facts::MovePathPair{
            facts::MovePathKey{facts[first].key.owner, facts[first].key.place.clone()},
            facts::MovePathKey{facts[second].key.owner, facts[second].key.place.clone()}});
      }
    }
    return facts::MovePathFunction{function.owner, zc::mv(facts), zc::mv(conflicts)};
  }

  // ---- flow ----------------------------------------------------------------

  struct FlowChain final {
    zc::Vector<facts::OwnershipPoint> points;
    zc::Vector<facts::FlowEdge> edges;
  };

  static bool flowContains(zc::ArrayPtr<const facts::OwnershipPoint> points,
                           const facts::OwnershipPoint& point) {
    for (const auto& candidate : points) {
      if (sameOwnershipPoint(candidate, point)) return true;
    }
    return false;
  }

  static void chainLocation(FlowChain& flow, const OwnershipFunctionEventOverlay& functionOverlay,
                            MirPoint point, zc::Maybe<facts::OwnershipPoint>& current) {
    facts::OwnershipPoint cfg = facts::OwnershipPoint::cfg(MirPoint(point));
    if (!flowContains(flow.points.asPtr(), cfg)) flow.points.add(facts::OwnershipPoint(cfg));
    ZC_IF_SOME(currentValue, current) {
      if (!sameOwnershipPoint(currentValue, cfg)) {
        bool duplicate = false;
        for (const auto& edge : flow.edges) {
          if (sameOwnershipPoint(edge.from, currentValue) && sameOwnershipPoint(edge.to, cfg)) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) flow.edges.add(facts::FlowEdge{facts::OwnershipPoint(currentValue), cfg});
      }
    }
    current = facts::OwnershipPoint(cfg);
    for (const auto& slot : functionOverlay.slots) {
      if (slot.key.location.point != point) continue;
      facts::OwnershipPoint before = facts::OwnershipPoint::beforeEvent(MirEventKey(slot.key));
      facts::OwnershipPoint after = facts::OwnershipPoint::afterEvent(MirEventKey(slot.key));
      if (!flowContains(flow.points.asPtr(), before))
        flow.points.add(facts::OwnershipPoint(before));
      if (!flowContains(flow.points.asPtr(), after)) flow.points.add(facts::OwnershipPoint(after));
      bool duplicate = false;
      for (const auto& edge : flow.edges) {
        if (sameOwnershipPoint(edge.from, ZC_ASSERT_NONNULL(current)) &&
            sameOwnershipPoint(edge.to, before)) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        flow.edges.add(facts::FlowEdge{facts::OwnershipPoint(ZC_ASSERT_NONNULL(current)),
                                       facts::OwnershipPoint(before)});
      }
      duplicate = false;
      for (const auto& edge : flow.edges) {
        if (sameOwnershipPoint(edge.from, before) && sameOwnershipPoint(edge.to, after)) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        flow.edges.add(
            facts::FlowEdge{facts::OwnershipPoint(before), facts::OwnershipPoint(after)});
      }
      current = facts::OwnershipPoint(after);
    }
  }

  ZC_NODISCARD zc::Maybe<facts::FlowFunction> deriveFlow(
      const mir::MirFunction& function,
      const OwnershipFunctionEventOverlay& functionOverlay) const {
    if (function.blocks.size() == 0) return zc::none;
    FlowChain flow;
    zc::Maybe<facts::OwnershipPoint> current;
    chainLocation(flow, functionOverlay, MirPoint::entry(), current);
    for (size_t index = 0; index < function.blocks.size(); ++index) {
      const auto& block = function.blocks[index];
      // Each block chains its own locations; a Call terminator chains the edge
      // point to its normal-edge successor below. The flow no longer assumes a
      // linear block layout, so it admits multi-predecessor joins once branch
      // terminators land.
      for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        chainLocation(flow, functionOverlay, MirPoint::beforeStatement(block.id, ordinal), current);
        chainLocation(flow, functionOverlay, MirPoint::afterStatement(block.id, ordinal), current);
      }
      chainLocation(flow, functionOverlay, MirPoint::beforeTerminator(block.id), current);
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        chainLocation(flow, functionOverlay, MirPoint::exit(block.id, MirExitKind::Return),
                      current);
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Unreachable) {
        chainLocation(flow, functionOverlay, MirPoint::exit(block.id, MirExitKind::Unreachable),
                      current);
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
        const auto& call = block.terminator.callValue();
        if (call.unwindTarget != zc::none) return zc::none;
        chainLocation(flow, functionOverlay, MirPoint::edge(block.id, 0, call.normalTarget),
                      current);
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Goto) {
        chainLocation(flow, functionOverlay,
                      MirPoint::edge(block.id, 0, block.terminator.gotoValue().target), current);
      } else if (block.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
        const auto& switchInt = block.terminator.switchIntValue();
        // Every edge fans out from the beforeTerminator point, matching the
        // production CFG derivation.
        zc::Maybe<facts::OwnershipPoint> branchPoint = current;
        for (uint32_t ordinal = 0; ordinal < switchInt.arms.size(); ++ordinal) {
          current = branchPoint;
          chainLocation(flow, functionOverlay,
                        MirPoint::edge(block.id, ordinal, switchInt.arms[ordinal].target), current);
        }
        current = branchPoint;
        chainLocation(flow, functionOverlay,
                      MirPoint::edge(block.id, static_cast<uint32_t>(switchInt.arms.size()),
                                     switchInt.defaultTarget),
                      current);
      } else {
        return zc::none;
      }
    }
    for (const auto& slot : functionOverlay.slots) {
      const facts::OwnershipPoint before =
          facts::OwnershipPoint::beforeEvent(MirEventKey(slot.key));
      const facts::OwnershipPoint after = facts::OwnershipPoint::afterEvent(MirEventKey(slot.key));
      if (!flowContains(flow.points.asPtr(), before) || !flowContains(flow.points.asPtr(), after)) {
        return zc::none;
      }
    }
    return facts::FlowFunction{function.owner, zc::mv(flow.points), zc::mv(flow.edges)};
  }

  // ---- initialization --------------------------------------------------------

  struct PathState final {
    facts::InitializationState state;
    zc::Vector<facts::InitializationLossCause> causes;
  };

  struct InitializationOracle final {
    const mir::MirFunction& function;
    const facts::MovePathFunction& paths;

    struct MemoEntry final {
      MirPoint point;
      zc::Vector<PathState> states;
    };
    zc::Vector<MemoEntry> memo;
    zc::Vector<MirPoint> inProgress;

    static facts::InitializationState joinState(facts::InitializationState left,
                                                facts::InitializationState right) {
      return facts::InitializationState{left.storageLive && right.storageLive,
                                        left.mayBeInitialized || right.mayBeInitialized,
                                        left.mustBeInitialized && right.mustBeInitialized};
    }

    static zc::Vector<facts::InitializationLossCause> mergeCauses(
        zc::ArrayPtr<const facts::InitializationLossCause> left,
        zc::ArrayPtr<const facts::InitializationLossCause> right) {
      zc::Vector<facts::InitializationLossCause> merged;
      for (const auto& cause : left) {
        merged.add(facts::InitializationLossCause{
            cause.kind, MirEventKey(cause.event),
            facts::MovePathKey{cause.path.owner, cause.path.place.clone()}});
      }
      for (const auto& cause : right) {
        bool duplicate = false;
        for (const auto& existing : merged) {
          if (existing.kind == cause.kind && sameEventKey(existing.event, cause.event) &&
              sameMovePathKey(existing.path, cause.path)) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate) {
          merged.add(facts::InitializationLossCause{
              cause.kind, MirEventKey(cause.event),
              facts::MovePathKey{cause.path.owner, cause.path.place.clone()}});
        }
      }
      // Canonical order must match the production `mergeLossCauses` sort so the
      // differential comparison sees identical cause vectors at every fact.
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

    static zc::Vector<PathState> cloneStates(zc::ArrayPtr<const PathState> states) {
      zc::Vector<PathState> cloned;
      for (const auto& state : states) {
        zc::Vector<facts::InitializationLossCause> causes;
        for (const auto& cause : state.causes) {
          causes.add(facts::InitializationLossCause{
              cause.kind, MirEventKey(cause.event),
              facts::MovePathKey{cause.path.owner, cause.path.place.clone()}});
        }
        cloned.add(PathState{state.state, zc::mv(causes)});
      }
      return cloned;
    }

    zc::Maybe<size_t> pathIndex(const mir::MirPlace& place) const {
      for (size_t index = 0; index < paths.facts.size(); ++index) {
        if (samePlace(paths.facts[index].key.place, place)) return index;
      }
      return zc::none;
    }

    bool inProgressContains(const MirPoint& point) const {
      for (const auto& candidate : inProgress) {
        if (candidate == point) return true;
      }
      return false;
    }

    bool memoContains(const MirPoint& point) const {
      for (const auto& entry : memo) {
        if (entry.point == point) return true;
      }
      return false;
    }

    zc::Maybe<zc::Vector<PathState>> memoized(const MirPoint& point) const {
      for (const auto& entry : memo) {
        if (entry.point == point) return cloneStates(entry.states.asPtr());
      }
      return zc::none;
    }

    void remember(MirPoint point, zc::Vector<PathState> states) {
      memo.add(MemoEntry{zc::mv(point), zc::mv(states)});
    }

    static MirEventKey event(identity::DefId owner, MirPoint point, uint32_t ordinal) {
      return MirEventKey{MirLocation{owner, zc::mv(point)}, ordinal};
    }

    static facts::InitializationLossCause cause(facts::InitializationLossKind kind,
                                                MirEventKey event, facts::MovePathKey path) {
      return facts::InitializationLossCause{kind, zc::mv(event), zc::mv(path)};
    }

    zc::Maybe<zc::Vector<PathState>> initialStates() const {
      zc::Vector<PathState> states;
      for (const auto& path : paths.facts) {
        auto index = localIndex(function, path.key.place.local());
        if (index == zc::none) return zc::none;
        ZC_IF_SOME(localIndexValue, index) {
          const auto& local = function.locals[localIndexValue];
          if (local.kind == mir::MirLocalKind::Parameter) {
            states.add(PathState{facts::InitializationState::initialized(),
                                 zc::Vector<facts::InitializationLossCause>()});
          } else {
            zc::Vector<facts::InitializationLossCause> causes;
            causes.add(cause(
                facts::InitializationLossKind::NeverInitialized,
                event(function.owner, MirPoint::entry(), static_cast<uint32_t>(localIndexValue)),
                facts::MovePathKey{path.key.owner, path.key.place.clone()}));
            states.add(PathState{facts::InitializationState::dead(), zc::mv(causes)});
          }
        }
      }
      return states;
    }

    bool setUnavailable(zc::Vector<PathState>& states, const mir::MirPlace& place,
                        facts::InitializationLossKind kind, MirEventKey event,
                        facts::InitializationState unavailable) const {
      if (states.size() != paths.facts.size()) return false;
      bool found = false;
      for (size_t index = 0; index < paths.facts.size(); ++index) {
        const auto& path = paths.facts[index].key.place;
        if (!isPathWithin(place, path) && !isPathWithin(path, place)) continue;
        zc::Vector<facts::InitializationLossCause> causes;
        causes.add(
            cause(kind, MirEventKey(event), facts::MovePathKey{function.owner, place.clone()}));
        states[index] = PathState{unavailable, zc::mv(causes)};
        found = true;
      }
      return found;
    }

    bool setInitialized(zc::Vector<PathState>& states, const mir::MirPlace& place) const {
      if (states.size() != paths.facts.size()) return false;
      bool found = false;
      for (size_t index = 0; index < paths.facts.size(); ++index) {
        if (!isPathWithin(place, paths.facts[index].key.place)) continue;
        states[index] = PathState{facts::InitializationState::initialized(),
                                  zc::Vector<facts::InitializationLossCause>()};
        found = true;
      }
      return found;
    }

    bool initialize(zc::Vector<PathState>& states, const mir::MirPlace& place,
                    bool overwrite) const {
      if (!validLocalPlace(function, place)) return false;
      auto index = pathIndex(place);
      if (index == zc::none) return false;
      ZC_IF_SOME(value, index) {
        const auto state = states[value].state;
        if (!state.storageLive || (overwrite ? !state.mustBeInitialized : state.mayBeInitialized)) {
          return false;
        }
        return setInitialized(states, place);
      }
      return false;
    }

    bool applyOperand(zc::Vector<PathState>& states, const mir::MirOperand& operand,
                      MirEventKey event) const {
      if (operand.kind() == mir::MirOperandKind::Constant) return true;
      if (!validLocalPlace(function, operand.place())) return false;
      if (operand.kind() == mir::MirOperandKind::Move) {
        return setUnavailable(states, operand.place(), facts::InitializationLossKind::Moved,
                              zc::mv(event), facts::InitializationState::uninitialized());
      }
      return true;
    }

    bool applyStatement(zc::Vector<PathState>& states, const mir::MirStatement& statement,
                        mir::MirBlockId block, uint32_t ordinal) const {
      auto statementEvent = [&]() {
        return event(function.owner, MirPoint::beforeStatement(block, ordinal), 0);
      };
      switch (statement.kind()) {
        case mir::MirStatementKind::Assign: {
          const auto& assignment = statement.assignmentValue();
          if (assignment.value.kind() == mir::MirRvalueKind::Use) {
            if (!applyOperand(states, assignment.value.useValue().operand, statementEvent())) {
              return false;
            }
          } else {
            const auto& aggregate = assignment.value.nominalAggregateValue();
            if (!aggregate.definition.isValid() || !aggregate.type.isValid()) return false;
            for (const auto& element : aggregate.elements) {
              if (!element.field.isValid() ||
                  element.operand.kind() != mir::MirOperandKind::Constant) {
                return false;
              }
            }
          }
          return initialize(states, assignment.destination,
                            assignment.initialization == mir::MirInitializationKind::Overwrite);
        }
        case mir::MirStatementKind::StorageLive: {
          auto index = localIndex(function, statement.storageLocal());
          if (index == zc::none) return false;
          const auto& local = function.locals[ZC_ASSERT_NONNULL(index)];
          zc::Vector<mir::MirProjection> projections;
          auto root = mir::MirPlace(local.id, local.type, zc::mv(projections), local.type);
          for (size_t pathIndex = 0; pathIndex < paths.facts.size(); ++pathIndex) {
            if (isPathWithin(root, paths.facts[pathIndex].key.place) &&
                states[pathIndex].state != facts::InitializationState::dead()) {
              return false;
            }
          }
          return setUnavailable(states, root, facts::InitializationLossKind::NeverInitialized,
                                statementEvent(), facts::InitializationState::uninitialized());
        }
        case mir::MirStatementKind::StorageDead: {
          auto index = localIndex(function, statement.storageLocal());
          if (index == zc::none) return false;
          const auto& local = function.locals[ZC_ASSERT_NONNULL(index)];
          zc::Vector<mir::MirProjection> projections;
          auto root = mir::MirPlace(local.id, local.type, zc::mv(projections), local.type);
          for (size_t pathIndex = 0; pathIndex < paths.facts.size(); ++pathIndex) {
            if (isPathWithin(root, paths.facts[pathIndex].key.place) &&
                states[pathIndex].state != facts::InitializationState::uninitialized()) {
              return false;
            }
          }
          return setUnavailable(states, root, facts::InitializationLossKind::StorageEnded,
                                statementEvent(), facts::InitializationState::dead());
        }
        case mir::MirStatementKind::BorrowCreation: {
          const auto& borrow = statement.borrowCreationValue();
          return validLocalPlace(function, borrow.source) &&
                 initialize(states, borrow.destination, false);
        }
        case mir::MirStatementKind::SetDiscriminant:
          return validLocalPlace(function, statement.setDiscriminantValue().destination);
        case mir::MirStatementKind::Deinitialize: {
          const auto& deinitialization = statement.deinitializeValue();
          if (!validLocalPlace(function, deinitialization.destination)) return false;
          return setUnavailable(states, deinitialization.destination,
                                facts::InitializationLossKind::Deinitialized, statementEvent(),
                                facts::InitializationState::uninitialized());
        }
        case mir::MirStatementKind::UnsafeScopeBoundary:
          return true;
      }
      return false;
    }

    zc::Maybe<zc::Vector<PathState>> joinPredecessors(mir::MirBlockId target) {
      zc::Maybe<zc::Vector<PathState>> joined;
      for (size_t index = 0; index < function.blocks.size(); ++index) {
        const auto& block = function.blocks[index];
        const auto& terminator = block.terminator;
        zc::Maybe<uint32_t> edgeOrdinal;
        if (terminator.kind() == mir::MirTerminatorKind::Call) {
          if (terminator.callValue().normalTarget == target) edgeOrdinal = uint32_t{0};
        } else if (terminator.kind() == mir::MirTerminatorKind::Goto) {
          if (terminator.gotoValue().target == target) edgeOrdinal = uint32_t{0};
        } else if (terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
          const auto& switchInt = terminator.switchIntValue();
          for (uint32_t ordinal = 0; ordinal < switchInt.arms.size(); ++ordinal) {
            if (switchInt.arms[ordinal].target == target) {
              edgeOrdinal = ordinal;
              break;
            }
          }
          if (edgeOrdinal == zc::none && switchInt.defaultTarget == target) {
            edgeOrdinal = static_cast<uint32_t>(switchInt.arms.size());
          }
        }
        if (edgeOrdinal == zc::none) continue;
        auto edge = stateAt(MirPoint::edge(block.id, ZC_ASSERT_NONNULL(edgeOrdinal), target));
        if (edge == zc::none) return zc::none;
        ZC_IF_SOME(edgeStates, edge) {
          if (joined == zc::none) {
            joined = cloneStates(edgeStates.asPtr());
          } else {
            zc::Vector<PathState> merged;
            const auto& current = ZC_ASSERT_NONNULL(joined);
            if (current.size() != edgeStates.size()) return zc::none;
            for (size_t stateIndex = 0; stateIndex < current.size(); ++stateIndex) {
              merged.add(
                  PathState{joinState(current[stateIndex].state, edgeStates[stateIndex].state),
                            mergeCauses(current[stateIndex].causes.asPtr(),
                                        edgeStates[stateIndex].causes.asPtr())});
            }
            joined = zc::mv(merged);
          }
        }
      }
      return joined;
    }

    /// \brief Demand-driven recursive state computation. Each published point
    /// is computed once and memoized; a recursion cycle is unsupported and
    /// rejects the oracle comparison for that function.
    zc::Maybe<zc::Vector<PathState>> stateAt(const MirPoint& point) {
      auto cached = memoized(point);
      if (cached != zc::none) return cached;
      if (inProgressContains(point)) return zc::none;
      inProgress.add(MirPoint(point));

      zc::Maybe<zc::Vector<PathState>> result;
      if (point.kind() == MirPointKind::Entry) {
        result = initialStates();
      } else if (point.kind() == MirPointKind::BeforeStatement) {
        const auto& location = point.beforeStatementValue();
        auto block = blockIndex(function, location.block);
        if (block == zc::none ||
            location.ordinal >= function.blocks[ZC_ASSERT_NONNULL(block)].statements.size()) {
          result = zc::none;
        } else if (location.ordinal == 0) {
          if (ZC_ASSERT_NONNULL(block) == 0) {
            result = stateAt(MirPoint::entry());
          } else {
            result = joinPredecessors(location.block);
          }
        } else {
          result = stateAt(MirPoint::afterStatement(location.block, location.ordinal - 1));
        }
      } else if (point.kind() == MirPointKind::AfterStatement) {
        const auto& location = point.afterStatementValue();
        auto before = stateAt(MirPoint::beforeStatement(location.block, location.ordinal));
        if (before == zc::none) {
          result = zc::none;
        } else {
          ZC_IF_SOME(states, before) {
            auto index = blockIndex(function, location.block);
            if (index == zc::none ||
                location.ordinal >= function.blocks[ZC_ASSERT_NONNULL(index)].statements.size()) {
              result = zc::none;
            } else {
              auto next = cloneStates(states.asPtr());
              if (applyStatement(
                      next, function.blocks[ZC_ASSERT_NONNULL(index)].statements[location.ordinal],
                      location.block, location.ordinal)) {
                result = zc::mv(next);
              } else {
                result = zc::none;
              }
            }
          }
        }
      } else if (point.kind() == MirPointKind::BeforeTerminator) {
        const auto& location = point.beforeTerminatorValue();
        auto index = blockIndex(function, location.block);
        if (index == zc::none) {
          result = zc::none;
        } else {
          const auto& block = function.blocks[ZC_ASSERT_NONNULL(index)];
          if (block.statements.size() == 0) {
            if (ZC_ASSERT_NONNULL(index) == 0) {
              result = stateAt(MirPoint::entry());
            } else {
              result = joinPredecessors(location.block);
            }
          } else {
            result = stateAt(MirPoint::afterStatement(
                location.block, static_cast<uint32_t>(block.statements.size() - 1)));
          }
        }
      } else if (point.kind() == MirPointKind::Exit) {
        const auto& location = point.exitValue();
        if (location.kind != MirExitKind::Return) {
          result = zc::none;
        } else {
          auto before = stateAt(MirPoint::beforeTerminator(location.block));
          if (before == zc::none) {
            result = zc::none;
          } else {
            ZC_IF_SOME(states, before) {
              auto index = blockIndex(function, location.block);
              if (index == zc::none) {
                result = zc::none;
              } else {
                const auto& terminator = function.blocks[ZC_ASSERT_NONNULL(index)].terminator;
                if (terminator.kind() != mir::MirTerminatorKind::Return) {
                  result = zc::none;
                } else {
                  auto next = cloneStates(states.asPtr());
                  bool applied = true;
                  ZC_IF_SOME(operand, terminator.returnValue().value) {
                    if (!applyOperand(
                            next, operand,
                            event(function.owner, MirPoint::beforeTerminator(location.block), 0))) {
                      applied = false;
                    }
                  }
                  if (applied) {
                    result = zc::mv(next);
                  } else {
                    result = zc::none;
                  }
                }
              }
            }
          }
        }
      } else if (point.kind() == MirPointKind::Edge) {
        const auto& location = point.edgeValue();
        auto before = stateAt(MirPoint::beforeTerminator(location.from));
        if (before == zc::none) {
          result = zc::none;
        } else {
          ZC_IF_SOME(states, before) {
            auto index = blockIndex(function, location.from);
            if (index == zc::none) {
              result = zc::none;
            } else {
              const auto& terminator = function.blocks[ZC_ASSERT_NONNULL(index)].terminator;
              auto next = cloneStates(states.asPtr());
              bool applied = true;
              if (terminator.kind() == mir::MirTerminatorKind::Call) {
                const auto& call = terminator.callValue();
                for (uint32_t ordinal = 0; ordinal < call.arguments.size(); ++ordinal) {
                  if (!applyOperand(next, call.arguments[ordinal],
                                    event(function.owner, MirPoint::beforeTerminator(location.from),
                                          ordinal))) {
                    applied = false;
                    break;
                  }
                }
                if (applied && !initialize(next, call.destination, false)) applied = false;
              } else if (terminator.kind() == mir::MirTerminatorKind::Goto) {
                // No operands: the edge state is the beforeTerminator state.
              } else if (terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
                const auto& switchInt = terminator.switchIntValue();
                if (!applyOperand(
                        next, switchInt.discriminant,
                        event(function.owner, MirPoint::beforeTerminator(location.from), 0))) {
                  applied = false;
                }
              } else {
                applied = false;
              }
              if (applied) {
                result = zc::mv(next);
              } else {
                result = zc::none;
              }
            }
          }
        }
      } else {
        result = zc::none;
      }

      for (size_t index = 0; index < inProgress.size(); ++index) {
        if (inProgress[index] == point) {
          inProgress[index] = inProgress[inProgress.size() - 1];
          inProgress.removeLast();
          break;
        }
      }
      ZC_IF_SOME(value, result) {
        if (!memoContains(point)) remember(MirPoint(point), cloneStates(value.asPtr()));
      }
      return result;
    }
  };

  ZC_NODISCARD zc::Maybe<facts::InitializationFunction> deriveInitialization(
      const mir::MirFunction& function, const facts::MovePathFunction& paths) const {
    if (function.blocks.size() == 0) return zc::none;
    for (const auto& path : paths.facts) {
      if (path.key.owner != function.owner || !validLocalPlace(function, path.key.place)) {
        return zc::none;
      }
    }
    InitializationOracle oracle{function, paths, {}, {}};
    zc::Vector<facts::InitializationFact> facts;
    auto append = [&](const MirPoint& point) -> bool {
      auto states = oracle.stateAt(MirPoint(point));
      if (states == zc::none) return false;
      ZC_IF_SOME(values, states) {
        if (values.size() != paths.facts.size()) return false;
        for (size_t index = 0; index < paths.facts.size(); ++index) {
          zc::Vector<facts::InitializationLossCause> causes;
          for (const auto& cause : values[index].causes) {
            causes.add(facts::InitializationLossCause{
                cause.kind, MirEventKey(cause.event),
                facts::MovePathKey{cause.path.owner, cause.path.place.clone()}});
          }
          facts.add(
              facts::InitializationFact{MirPoint(point),
                                        facts::MovePathKey{paths.facts[index].key.owner,
                                                           paths.facts[index].key.place.clone()},
                                        values[index].state, zc::mv(causes)});
        }
      }
      return true;
    };

    if (!append(MirPoint::entry())) return zc::none;
    for (const auto& block : function.blocks) {
      for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        if (!append(MirPoint::beforeStatement(block.id, ordinal))) return zc::none;
        if (!append(MirPoint::afterStatement(block.id, ordinal))) return zc::none;
      }
      if (!append(MirPoint::beforeTerminator(block.id))) return zc::none;
      if (block.terminator.kind() == mir::MirTerminatorKind::Return) {
        if (!append(MirPoint::exit(block.id, MirExitKind::Return))) return zc::none;
        // The production derivation publishes one linear walk and returns at
        // the first return terminator; the oracle matches that boundary.
        return facts::InitializationFunction{function.owner, zc::mv(facts)};
      }
      if (block.terminator.kind() == mir::MirTerminatorKind::Call) {
        if (!append(MirPoint::edge(block.id, 0, block.terminator.callValue().normalTarget))) {
          return zc::none;
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::Goto) {
        if (!append(MirPoint::edge(block.id, 0, block.terminator.gotoValue().target))) {
          return zc::none;
        }
      } else if (block.terminator.kind() == mir::MirTerminatorKind::SwitchInt) {
        const auto& switchInt = block.terminator.switchIntValue();
        for (uint32_t ordinal = 0; ordinal < switchInt.arms.size(); ++ordinal) {
          if (!append(MirPoint::edge(block.id, ordinal, switchInt.arms[ordinal].target))) {
            return zc::none;
          }
        }
        if (!append(MirPoint::edge(block.id, static_cast<uint32_t>(switchInt.arms.size()),
                                   switchInt.defaultTarget))) {
          return zc::none;
        }
      } else {
        return zc::none;
      }
    }
    return zc::none;
  }

  // ---- loans -----------------------------------------------------------------

  ZC_NODISCARD zc::Maybe<facts::MovePathKey> findMovePath(const facts::MovePathFunction& paths,
                                                          const mir::MirPlace& place) const {
    zc::Maybe<facts::MovePathKey> result;
    for (const auto& fact : paths.facts) {
      if (!samePlace(fact.key.place, place)) continue;
      if (result != zc::none) return zc::none;
      result = facts::MovePathKey{fact.key.owner, fact.key.place.clone()};
    }
    return result;
  }

  ZC_NODISCARD zc::Maybe<facts::LoanFact> deriveLoan(
      const mir::MirFunction& function, const OwnershipFunctionEventOverlay& functionOverlay,
      const zc::Vector<facts::MovePathFunction>& paths, const MirEventSlot& slot) const {
    if (slot.key.location.point.kind() != MirPointKind::BeforeStatement) return zc::none;
    const auto& point = slot.key.location.point.beforeStatementValue();
    auto index = blockIndex(function, point.block);
    if (index == zc::none ||
        point.ordinal >= function.blocks[ZC_ASSERT_NONNULL(index)].statements.size()) {
      return zc::none;
    }
    const auto& statement = function.blocks[ZC_ASSERT_NONNULL(index)].statements[point.ordinal];
    if (statement.kind() != mir::MirStatementKind::BorrowCreation) return zc::none;
    const auto& borrow = statement.borrowCreationValue();
    if (!borrow.source.hasConsistentTypeChain() || !borrow.destination.hasConsistentTypeChain()) {
      return zc::none;
    }
    const facts::MovePathFunction* functionPaths = nullptr;
    for (const auto& candidate : paths) {
      if (candidate.owner == function.owner) functionPaths = &candidate;
    }
    if (functionPaths == nullptr) return zc::none;
    auto source = findMovePath(*functionPaths, borrow.source);
    auto destination = findMovePath(*functionPaths, borrow.destination);
    if (source == zc::none || destination == zc::none) return zc::none;
    const MirEventKey issue{
        MirLocation{function.owner, MirPoint::beforeStatement(point.block, point.ordinal)}, 1};
    zc::Maybe<MirEventKey> activation;
    for (const auto& fact : functionOverlay.deferredActivations) {
      if (fact.loan.issue != issue) continue;
      if (activation != zc::none) return zc::none;
      activation = MirEventKey(fact.activation);
    }
    facts::OwnershipPoint activeFrom = facts::OwnershipPoint::afterEvent(MirEventKey(issue));
    ZC_IF_SOME(event, activation) {
      activeFrom = facts::OwnershipPoint::afterEvent(MirEventKey(event));
    }
    ZC_IF_SOME(sourcePath, source) {
      ZC_IF_SOME(destinationPath, destination) {
        return facts::LoanFact{
            function.owner,
            MirEventKey(issue),
            MirEventKey{
                MirLocation{function.owner, MirPoint::beforeStatement(point.block, point.ordinal)},
                2},
            borrow.kind,
            facts::OwnershipPoint(activeFrom),
            facts::MovePathKey{sourcePath.owner, sourcePath.place.clone()},
            facts::MovePathKey{destinationPath.owner, destinationPath.place.clone()}};
      }
    }
    return zc::none;
  }

  // ---- references --------------------------------------------------------------

  static bool hasEntryRoot(const OwnershipFunctionEventOverlay& functionOverlay, uint32_t ordinal) {
    size_t matches = 0;
    for (const auto& slot : functionOverlay.slots) {
      if (slot.key.location.point.kind() != MirPointKind::Entry ||
          slot.key.operandOrdinal != ordinal || slot.stage != OwnershipEventStage::Commit ||
          slot.roles.size() != 1 || slot.roles[0] != OwnershipEventRole::EntryRoot) {
        continue;
      }
      ++matches;
    }
    return matches == 1;
  }

  bool hasDirectRootParameter(identity::DefId owner, uint32_t parameter) const {
    size_t matches = 0;
    for (const auto& summary : evidence.localSummaries()) {
      if (summary.callable != owner) continue;
      if (summary.returnRelation.tag() != checker::borrow::BorrowReturnRelationTag::DirectRoot ||
          summary.returnRelation.source().tag() !=
              checker::borrow::BorrowInputRegionTag::Parameter ||
          summary.returnRelation.source().parameterIndex() != parameter) {
        return false;
      }
      bool directInput = false;
      for (const auto& input : summary.directInputs) {
        if (input.tag() == checker::borrow::BorrowInputRegionTag::Parameter &&
            input.parameterIndex() == parameter) {
          directInput = true;
        }
      }
      if (!directInput) return false;
      ++matches;
    }
    return matches == 1;
  }

  ZC_NODISCARD zc::Maybe<MirEventKey> returnedFrom(
      const mir::MirFunction& function, const mir::MirPlace& destination,
      const OwnershipFunctionEventOverlay& functionOverlay) const {
    zc::Maybe<MirEventKey> result;
    for (const auto& block : function.blocks) {
      if (block.terminator.kind() != mir::MirTerminatorKind::Return ||
          block.terminator.returnValue().value == zc::none) {
        continue;
      }
      ZC_IF_SOME(value, block.terminator.returnValue().value) {
        if ((value.kind() != mir::MirOperandKind::Copy &&
             value.kind() != mir::MirOperandKind::Move) ||
            !samePlace(value.place(), destination)) {
          continue;
        }
        const auto transferRole = value.kind() == mir::MirOperandKind::Copy
                                      ? OwnershipEventRole::OperandCopy
                                      : OwnershipEventRole::OperandMove;
        const MirEventKey event{MirLocation{function.owner, MirPoint::beforeTerminator(block.id)},
                                0};
        bool matches = false;
        size_t slots = 0;
        for (const auto& slot : functionOverlay.slots) {
          if (slot.key != event || slot.stage != OwnershipEventStage::Source ||
              slot.roles.size() != 2 || slot.roles[0] != OwnershipEventRole::OperandRead ||
              slot.roles[1] != transferRole) {
            continue;
          }
          ++slots;
        }
        matches = slots == 1;
        if (!matches || result != zc::none) return zc::none;
        result = MirEventKey(event);
      }
    }
    return result;
  }

  ZC_NODISCARD static zc::Maybe<uint32_t> parameterOrigin(const mir::MirFunction& function,
                                                          const mir::MirPlace& source) {
    zc::Maybe<const mir::MirLocalDeclaration&> sourceLocal;
    for (const auto& local : function.locals) {
      if (local.id != source.local()) continue;
      if (sourceLocal != zc::none) return zc::none;
      sourceLocal = local;
    }
    if (sourceLocal == zc::none) return zc::none;
    ZC_IF_SOME(local, sourceLocal) {
      if (local.kind == mir::MirLocalKind::Parameter) {
        for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
          if (function.locals[ordinal].id == local.id) return ordinal;
        }
        return zc::none;
      }
      if (local.kind != mir::MirLocalKind::UserLocal) return zc::none;
    }
    zc::Maybe<uint32_t> origin;
    for (const auto& block : function.blocks) {
      for (const auto& statement : block.statements) {
        if (statement.kind() != mir::MirStatementKind::Assign) continue;
        const auto& assignment = statement.assignmentValue();
        if (assignment.initialization != mir::MirInitializationKind::Initialize ||
            assignment.destination.local() != source.local() ||
            assignment.destination.projections().size() != 0 ||
            assignment.value.kind() != mir::MirRvalueKind::Use) {
          continue;
        }
        const auto& operand = assignment.value.useValue().operand;
        if ((operand.kind() != mir::MirOperandKind::Copy &&
             operand.kind() != mir::MirOperandKind::Move) ||
            operand.place().projections().size() != 0) {
          return zc::none;
        }
        zc::Maybe<uint32_t> parameter;
        for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
          const auto& candidate = function.locals[ordinal];
          if (candidate.id == operand.place().local() &&
              candidate.kind == mir::MirLocalKind::Parameter) {
            parameter = ordinal;
          }
        }
        if (parameter == zc::none || origin != zc::none) return zc::none;
        origin = parameter;
      }
    }
    return origin;
  }

  ZC_NODISCARD static zc::Maybe<MirEventKey> localOrigin(const mir::MirFunction& function,
                                                         const mir::MirPlace& source) {
    if (source.projections().size() != 0) return zc::none;
    zc::Maybe<const mir::MirLocalDeclaration&> sourceLocal;
    for (const auto& local : function.locals) {
      if (local.id != source.local()) continue;
      if (sourceLocal != zc::none) return zc::none;
      sourceLocal = local;
    }
    if (sourceLocal == zc::none) return zc::none;
    ZC_IF_SOME(local, sourceLocal) {
      if (local.kind != mir::MirLocalKind::UserLocal) return zc::none;
    }
    zc::Maybe<MirEventKey> origin;
    for (const auto& block : function.blocks) {
      for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        const auto& statement = block.statements[ordinal];
        if (statement.kind() != mir::MirStatementKind::StorageLive ||
            statement.storageLocal() != source.local()) {
          continue;
        }
        if (origin != zc::none) return zc::none;
        origin = MirEventKey{
            MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 0};
      }
    }
    return origin;
  }

  ZC_NODISCARD zc::Maybe<zc::Vector<facts::ReferenceDefinition>> deriveReferences(
      const mir::MirFunction& function, const OwnershipFunctionEventOverlay& functionOverlay,
      const zc::Vector<facts::MovePathFunction>& paths,
      zc::ArrayPtr<const facts::LoanFact> loans) const {
    zc::Vector<facts::ReferenceDefinition> definitions;
    for (const auto& block : function.blocks) {
      if (block.terminator.kind() != mir::MirTerminatorKind::Return ||
          block.terminator.returnValue().value == zc::none) {
        continue;
      }
      ZC_IF_SOME(operand, block.terminator.returnValue().value) {
        if (operand.kind() == mir::MirOperandKind::Constant) continue;
        zc::Maybe<const facts::LoanFact&> loan;
        for (const auto& candidate : loans) {
          if (candidate.owner != function.owner ||
              !samePlace(candidate.destination.place, operand.place())) {
            continue;
          }
          if (loan != zc::none) return zc::none;
          loan = candidate;
        }
        if (loan == zc::none) continue;
        ZC_IF_SOME(loanValue, loan) {
          auto returned = returnedFrom(function, loanValue.destination.place, functionOverlay);
          if (returned == zc::none) return zc::none;
          const bool isParameterReborrow =
              loanValue.source.place.projections().size() == 1 &&
              loanValue.source.place.projections()[0].kind() == mir::MirProjectionKind::Dereference;
          const bool isLocalBorrow = loanValue.source.place.projections().size() == 0;
          if (!isParameterReborrow && !isLocalBorrow) return zc::none;
          if (loanValue.destination.place.projections().size() != 0) return zc::none;
          MirEventKey entryEvent{MirLocation{function.owner, MirPoint::entry()}, 0};
          zc::OneOf<facts::ParameterReferenceOrigin, facts::LocalReferenceOrigin> detail{
              facts::LocalReferenceOrigin{}};
          if (isParameterReborrow) {
            auto originOrdinal = parameterOrigin(function, loanValue.source.place);
            if (originOrdinal == zc::none) return zc::none;
            ZC_IF_SOME(ordinal, originOrdinal) {
              if (!hasEntryRoot(functionOverlay, ordinal) ||
                  !hasDirectRootParameter(function.owner, ordinal)) {
                return zc::none;
              }
              entryEvent = MirEventKey{MirLocation{function.owner, MirPoint::entry()}, ordinal};
              detail = facts::ParameterReferenceOrigin{ordinal};
            }
          } else {
            auto localEntry = localOrigin(function, loanValue.source.place);
            if (localEntry == zc::none) return zc::none;
            ZC_IF_SOME(event, localEntry) { entryEvent = event; }
          }
          ZC_IF_SOME(returnEvent, returned) {
            if (loanValue.commit.location.point.kind() != MirPointKind::BeforeStatement ||
                returnEvent.location.point.kind() != MirPointKind::BeforeTerminator) {
              return zc::none;
            }
            const auto& commit = loanValue.commit.location.point.beforeStatementValue();
            const auto& returnedPoint = returnEvent.location.point.beforeTerminatorValue();
            facts::ReferenceLivePoints livePoints{
                facts::OwnershipPoint::afterEvent(MirEventKey(loanValue.commit)),
                facts::OwnershipPoint::cfg(MirPoint::afterStatement(commit.block, commit.ordinal)),
                facts::OwnershipPoint::cfg(MirPoint::beforeTerminator(returnedPoint.block)),
                facts::OwnershipPoint::beforeEvent(MirEventKey(returnEvent)),
                facts::OwnershipPoint::afterEvent(MirEventKey(returnEvent))};
            definitions.add(facts::ReferenceDefinition{
                function.owner, MirEventKey(loanValue.commit), MirEventKey(loanValue.issue),
                facts::ReferenceInputOrigin{
                    entryEvent, facts::OwnershipPoint(loanValue.activeFrom), detail,
                    facts::MovePathKey{loanValue.source.owner, loanValue.source.place.clone()}},
                MirEventKey(returnEvent),
                facts::MovePathKey{loanValue.destination.owner,
                                   loanValue.destination.place.clone()},
                zc::mv(livePoints)});
          }
        }
      }
    }
    return definitions;
  }

  // ---- regions ------------------------------------------------------------------

  static bool flowHasPoint(const facts::FlowFunction& flow, const facts::OwnershipPoint& point) {
    for (const auto& candidate : flow.points) {
      if (sameOwnershipPoint(candidate, point)) return true;
    }
    return false;
  }

  /// \brief Iterative ordered reachability (the production derivation is
  /// recursive).
  static bool reaches(const facts::FlowFunction& flow, const facts::OwnershipPoint& from,
                      const facts::OwnershipPoint& to) {
    zc::Vector<const facts::OwnershipPoint*> pending;
    zc::Vector<const facts::OwnershipPoint*> visited;
    const facts::OwnershipPoint* start = nullptr;
    for (const auto& point : flow.points) {
      if (sameOwnershipPoint(point, from)) {
        start = &point;
        break;
      }
    }
    if (start == nullptr) return false;
    pending.add(start);
    while (pending.size() != 0) {
      const facts::OwnershipPoint* current = pending[pending.size() - 1];
      pending.removeLast();
      if (sameOwnershipPoint(*current, to)) return true;
      bool alreadyVisited = false;
      for (const auto* point : visited) {
        if (sameOwnershipPoint(*point, *current)) {
          alreadyVisited = true;
          break;
        }
      }
      if (alreadyVisited) continue;
      visited.add(current);
      for (const auto& edge : flow.edges) {
        if (!sameOwnershipPoint(edge.from, *current)) continue;
        for (const auto& point : flow.points) {
          if (sameOwnershipPoint(point, edge.to)) {
            pending.add(&point);
            break;
          }
        }
      }
    }
    return false;
  }

  ZC_NODISCARD zc::Maybe<facts::ReborrowRegion> deriveRegion(
      const zc::Vector<facts::FlowFunction>& flows, const zc::Vector<facts::LoanFact>& loans,
      const facts::ReferenceDefinition& reference) const {
    zc::Maybe<const facts::LoanFact&> loan;
    for (const auto& candidate : loans) {
      if (candidate.owner != reference.owner || candidate.issue != reference.loan ||
          candidate.commit != reference.introduction) {
        continue;
      }
      if (loan != zc::none) return zc::none;
      loan = candidate;
    }
    if (loan == zc::none) return zc::none;
    ZC_IF_SOME(loanValue, loan) {
      const bool entryShapeValid =
          reference.origin.entry.location.owner == reference.owner &&
          reference.origin.entry.operandOrdinal == 0 &&
          ((reference.origin.detail.is<facts::ParameterReferenceOrigin>() &&
            reference.origin.entry.location.point.kind() == MirPointKind::Entry) ||
           (reference.origin.detail.is<facts::LocalReferenceOrigin>() &&
            reference.origin.entry.location.point.kind() == MirPointKind::BeforeStatement));
      if (!sameOwnershipPoint(loanValue.activeFrom, reference.origin.activation) ||
          !entryShapeValid) {
        return zc::none;
      }
      const facts::FlowFunction* flow = nullptr;
      for (const auto& candidate : flows) {
        if (candidate.owner == reference.owner) flow = &candidate;
      }
      if (flow == nullptr) return zc::none;
      zc::Vector<facts::OwnershipPoint> members;
      members.add(facts::OwnershipPoint(loanValue.activeFrom));
      members.add(facts::OwnershipPoint(reference.livePoints.afterCommit));
      members.add(facts::OwnershipPoint(reference.livePoints.afterCommitCfg));
      members.add(facts::OwnershipPoint(reference.livePoints.beforeReturnCfg));
      members.add(facts::OwnershipPoint(reference.livePoints.beforeReturn));
      members.add(facts::OwnershipPoint(reference.livePoints.afterReturn));
      for (const auto& member : members) {
        if (!flowHasPoint(*flow, member)) return zc::none;
      }
      for (size_t index = 1; index < members.size(); ++index) {
        if (!reaches(*flow, members[index - 1], members[index])) return zc::none;
      }
      return facts::ReborrowRegion{reference.owner, MirEventKey(reference.origin.entry),
                                   MirEventKey(reference.loan), reference.origin.detail,
                                   zc::mv(members)};
    }
    return zc::none;
  }

  // ---- resources ---------------------------------------------------------------

  static bool positive(const OwnershipMarkerUseKey& key,
                       const OwnershipFunctionEventOverlay& functionOverlay) {
    for (const auto& use : functionOverlay.markerUses) {
      if (use.key.event != key.event || use.key.marker != key.marker ||
          use.key.subject != key.subject ||
          use.key.markerPolicyRevision.digest() != key.markerPolicyRevision.digest() ||
          use.key.coherenceRevision.digest() != key.coherenceRevision.digest()) {
        continue;
      }
      return use.decision.is<OwnershipMarkerDecisionPositive>();
    }
    return false;
  }

  ZC_NODISCARD static zc::Maybe<facts::DropRequirement> requirement(
      const LogicalDropPlanComponent& component,
      const OwnershipFunctionEventOverlay& functionOverlay) {
    const bool copy = positive(component.copyDecision, functionOverlay);
    const bool linear = positive(component.linearDecision, functionOverlay);
    if (component.dropAction != zc::none && copy) return zc::none;
    if (!copy && !linear) return facts::DropRequirement::Logical;
    if (copy && linear) return facts::DropRequirement::Linear;
    if (!copy && linear) return facts::DropRequirement::LinearLogical;
    return zc::none;
  }

  static bool containsMovePath(const facts::MovePathFunction& paths, identity::DefId owner,
                               const mir::MirPlace& place) {
    for (const auto& fact : paths.facts) {
      if (fact.key.owner == owner && samePlace(fact.key.place, place)) return true;
    }
    return false;
  }

  struct TransferInitialization final {
    MirEventKey event;
    mir::MirPlace source;
  };

  ZC_NODISCARD static zc::Maybe<TransferInitialization> moveTransferInitialization(
      const mir::MirFunction& function, const LogicalDropPlan& plan) {
    if (plan.initialization.location.owner != function.owner ||
        plan.initialization.location.point.kind() != MirPointKind::BeforeStatement ||
        plan.initialization.operandOrdinal != 2) {
      return zc::none;
    }
    const auto& point = plan.initialization.location.point.beforeStatementValue();
    for (const auto& block : function.blocks) {
      if (block.id != point.block || point.ordinal >= block.statements.size()) continue;
      const auto& statement = block.statements[point.ordinal];
      if (statement.kind() != mir::MirStatementKind::Assign) return zc::none;
      const auto& assignment = statement.assignmentValue();
      if (assignment.value.kind() != mir::MirRvalueKind::Use ||
          assignment.value.useValue().operand.kind() != mir::MirOperandKind::Move ||
          !samePlace(assignment.destination, plan.root)) {
        return zc::none;
      }
      return TransferInitialization{
          MirEventKey{
              MirLocation{function.owner, MirPoint::beforeStatement(block.id, point.ordinal)}, 0},
          assignment.value.useValue().operand.place().clone()};
    }
    return zc::none;
  }

  ZC_NODISCARD static zc::Maybe<uint32_t> parameterEntryOrdinal(const mir::MirFunction& function,
                                                                const mir::MirPlace& place) {
    if (place.projections().size() != 0) return zc::none;
    for (uint32_t ordinal = 0; ordinal < function.locals.size(); ++ordinal) {
      const auto& local = function.locals[ordinal];
      if (local.id == place.local() && local.kind == mir::MirLocalKind::Parameter &&
          local.type == place.rootType() && local.type == place.resultType()) {
        return ordinal;
      }
    }
    return zc::none;
  }

  static bool isParameterRootTransfer(const LogicalDropPlan& plan,
                                      const LogicalDropPlanComponent& component,
                                      const TransferInitialization& transfer) {
    return transfer.source.projections().size() == 0 && samePlace(plan.root, component.place) &&
           component.valueType == transfer.source.resultType();
  }

  ZC_NODISCARD static zc::Maybe<uint32_t> resourceAt(
      const zc::Vector<facts::OwnershipResourceFact>& resourceFacts,
      const zc::Vector<facts::DropTransfer>& transfers,
      const zc::Vector<facts::CastResourceRoute>& castRoutes, const facts::MovePathKey& place) {
    for (uint32_t ordinal = 0; ordinal < resourceFacts.size(); ++ordinal) {
      facts::MovePathKey current{resourceFacts[ordinal].subject.origin.owner,
                                 resourceFacts[ordinal].subject.origin.place.clone()};
      for (const auto& transfer : transfers) {
        if (current.owner == transfer.from.owner && samePlace(current.place, transfer.from.place)) {
          current = facts::MovePathKey{transfer.to.owner, transfer.to.place.clone()};
        }
      }
      for (const auto& route : castRoutes) {
        if (current.owner == route.from.owner && samePlace(current.place, route.from.place)) {
          current = facts::MovePathKey{route.to.owner, route.to.place.clone()};
        }
      }
      if (current.owner == place.owner && samePlace(current.place, place.place)) return ordinal;
    }
    return zc::none;
  }

  ZC_NODISCARD zc::Maybe<facts::OwnershipResourceFunction> deriveResources(
      const mir::MirFunction& function, const facts::MovePathFunction& paths,
      const OwnershipFunctionEventOverlay& functionOverlay) const {
    zc::Vector<facts::OwnershipResourceFact> resourceFacts;
    zc::Vector<facts::DropTransfer> transfers;
    zc::Vector<facts::CastResourceRoute> castRoutes;
    zc::Vector<facts::DropPlan> dropPlans;

    // MIR-driven plan enumeration: every assignment initialization and every
    // normal-edge call destination maps to exactly one overlay plan. Plans
    // are applied in MIR statement order because a move transfer may consume
    // a resource introduced by an earlier fresh plan in the same function.
    struct PendingPlan final {
      const LogicalDropPlan* plan;
      bool isTransfer;
    };
    zc::Vector<PendingPlan> pending;
    for (const auto& block : function.blocks) {
      for (uint32_t ordinal = 0; ordinal < block.statements.size(); ++ordinal) {
        const auto& statement = block.statements[ordinal];
        if (statement.kind() != mir::MirStatementKind::Assign) continue;
        const MirEventKey initialization{
            MirLocation{function.owner, MirPoint::beforeStatement(block.id, ordinal)}, 2};
        for (const auto& plan : functionOverlay.logicalDropPlans) {
          if (plan.initialization != initialization ||
              !samePlace(plan.root, statement.assignmentValue().destination)) {
            continue;
          }
          auto transfer = moveTransferInitialization(function, plan);
          pending.add(PendingPlan{&plan, transfer != zc::none});
        }
      }
      if (block.terminator.kind() != mir::MirTerminatorKind::Call) continue;
      const auto& call = block.terminator.callValue();
      const MirEventKey initialization{
          MirLocation{function.owner, MirPoint::edge(block.id, 0, call.normalTarget)}, 0};
      for (const auto& plan : functionOverlay.logicalDropPlans) {
        if (plan.initialization == initialization && samePlace(plan.root, call.destination)) {
          pending.add(PendingPlan{&plan, false});
        }
      }
    }

    auto applyPlan = [&](const LogicalDropPlan& plan, bool isTransfer) -> bool {
      if (plan.initialization.location.owner != function.owner ||
          !containsMovePath(paths, function.owner, plan.root)) {
        return false;
      }
      zc::Maybe<TransferInitialization> transfer;
      if (isTransfer) {
        transfer = moveTransferInitialization(function, plan);
        if (transfer == zc::none) return false;
      }
      zc::Maybe<uint32_t> parameterOrdinal;
      ZC_IF_SOME(transferValue, transfer) {
        parameterOrdinal = parameterEntryOrdinal(function, transferValue.source);
      }
      zc::Vector<facts::DropPlanComponent> planComponents;
      zc::Maybe<facts::DropResourceSubject> rootSubject;
      for (const auto& component : plan.components) {
        if (!containsMovePath(paths, function.owner, component.place)) return false;
        auto componentRequirement = requirement(component, functionOverlay);
        if (componentRequirement == zc::none) return false;
        ZC_IF_SOME(expectedRequirement, componentRequirement) {
          auto introduction = plan.initialization;
          auto origin = facts::MovePathKey{function.owner, component.place.clone()};
          zc::Maybe<identity::SemanticTypeId> castOriginType;
          zc::Maybe<uint32_t> factOrdinal;
          if (transfer != zc::none) {
            ZC_IF_SOME(transferValue, transfer) {
              const auto source = facts::MovePathKey{function.owner, transferValue.source.clone()};
              if (isParameterRootTransfer(plan, component, transferValue) &&
                  parameterOrdinal != zc::none) {
                ZC_IF_SOME(entryOrdinal, parameterOrdinal) {
                  introduction =
                      MirEventKey{MirLocation{function.owner, MirPoint::entry()}, entryOrdinal};
                  origin = facts::MovePathKey{source.owner, source.place.clone()};
                  transfers.add(facts::DropTransfer{
                      facts::MovePathKey{function.owner, transferValue.source.clone()},
                      facts::MovePathKey{function.owner, component.place.clone()},
                      MirEventKey(transferValue.event)});
                }
              } else {
                auto resource = resourceAt(resourceFacts, transfers, castRoutes, source);
                if (resource == zc::none) {
                  // A parameter root moved through a type-changing cast
                  // introduces the resource at the parameter entry and
                  // preserves its subject across the cast.
                  if (parameterOrdinal != zc::none &&
                      transferValue.source.projections().size() == 0 &&
                      samePlace(plan.root, component.place)) {
                    ZC_IF_SOME(entryOrdinal, parameterOrdinal) {
                      introduction =
                          MirEventKey{MirLocation{function.owner, MirPoint::entry()}, entryOrdinal};
                      origin = facts::MovePathKey{source.owner, source.place.clone()};
                      castOriginType = transferValue.source.resultType();
                      castRoutes.add(facts::CastResourceRoute{
                          facts::DropResourceSubject{
                              MirEventKey(introduction),
                              facts::MovePathKey{source.owner, source.place.clone()},
                              transferValue.source.resultType()},
                          facts::MovePathKey{function.owner, transferValue.source.clone()},
                          facts::MovePathKey{function.owner, component.place.clone()},
                          MirEventKey(transferValue.event)});
                    }
                  } else {
                    continue;
                  }
                } else {
                  ZC_IF_SOME(resourceOrdinal, resource) {
                    const auto& sourceFact = resourceFacts[resourceOrdinal];
                    if (sourceFact.requirement != expectedRequirement ||
                        !sameDropAction(sourceFact.dropAction, component.dropAction)) {
                      return false;
                    }
                    if (sourceFact.subject.originType != component.valueType) {
                      // Type-changing cast: preserve the resource subject
                      // across the cast and record the exact route.
                      castRoutes.add(facts::CastResourceRoute{
                          sourceFact.subject.clone(),
                          facts::MovePathKey{function.owner, transferValue.source.clone()},
                          facts::MovePathKey{function.owner, component.place.clone()},
                          MirEventKey(transferValue.event)});
                    } else {
                      transfers.add(facts::DropTransfer{
                          facts::MovePathKey{function.owner, transferValue.source.clone()},
                          facts::MovePathKey{function.owner, component.place.clone()},
                          MirEventKey(transferValue.event)});
                    }
                    factOrdinal = resourceOrdinal;
                  }
                }
              }
            }
          }
          if (factOrdinal == zc::none) {
            factOrdinal = static_cast<uint32_t>(resourceFacts.size());
            identity::SemanticTypeId subjectType = component.valueType;
            if (castOriginType != zc::none) subjectType = ZC_ASSERT_NONNULL(castOriginType);
            resourceFacts.add(facts::OwnershipResourceFact{
                facts::DropResourceSubject{MirEventKey(introduction),
                                           facts::MovePathKey{origin.owner, origin.place.clone()},
                                           subjectType},
                expectedRequirement, component.dropAction, component.declarationOrdinal});
          }
          ZC_IF_SOME(ordinal, factOrdinal) {
            if (rootSubject == zc::none && samePlace(component.place, plan.root)) {
              rootSubject = resourceFacts[ordinal].subject.clone();
            }
            planComponents.add(facts::DropPlanComponent{ordinal, component.dropAction});
          }
        }
      }
      ZC_IF_SOME(subject, rootSubject) {
        dropPlans.add(
            facts::DropPlan{zc::mv(subject), facts::DropPlanMode::Closed, zc::mv(planComponents)});
      }
      return true;
    };

    for (const auto& plan : pending) {
      if (!applyPlan(*plan.plan, plan.isTransfer)) return zc::none;
    }
    return facts::OwnershipResourceFunction{function.owner,
                                            zc::mv(resourceFacts),
                                            zc::mv(transfers),
                                            zc::mv(castRoutes),
                                            zc::mv(dropPlans),
                                            {},
                                            {},
                                            {},
                                            {},
                                            {}};
  }

  const mir::VerifiedBuiltMir& builtMir;
  const VerifiedOwnershipEventOverlay& overlay;
  const driver::borrow_evidence::VerifiedBorrowEvidence& evidence;
};

// ---------------------------------------------------------------------------
// Set comparisons between the oracle and a production (or candidate) inventory.
// ---------------------------------------------------------------------------

inline bool sameMovePathFact(const facts::MovePathFact& left, const facts::MovePathFact& right) {
  if (!sameMovePathKey(left.key, right.key) ||
      (left.parent == zc::none) != (right.parent == zc::none)) {
    return false;
  }
  if (left.parent == zc::none) return true;
  ZC_IF_SOME(leftParent, left.parent) {
    ZC_IF_SOME(rightParent, right.parent) { return sameMovePathKey(leftParent, rightParent); }
  }
  return false;
}

inline bool sameMovePathPair(const facts::MovePathPair& left, const facts::MovePathPair& right) {
  return (sameMovePathKey(left.first, right.first) && sameMovePathKey(left.second, right.second)) ||
         (sameMovePathKey(left.first, right.second) && sameMovePathKey(left.second, right.first));
}

inline bool matchesMovePaths(zc::ArrayPtr<const facts::MovePathFunction> oracle,
                             zc::ArrayPtr<const facts::MovePathFunction> production) {
  if (oracle.size() != production.size()) return false;
  for (size_t index = 0; index < oracle.size(); ++index) {
    if (oracle[index].owner != production[index].owner ||
        !sameRecordSet(oracle[index].facts.asPtr(), production[index].facts.asPtr(),
                       sameMovePathFact) ||
        !sameRecordSet(oracle[index].conflicts.asPtr(), production[index].conflicts.asPtr(),
                       sameMovePathPair)) {
      return false;
    }
  }
  return true;
}

inline bool sameFlowPoint(const facts::OwnershipPoint& left, const facts::OwnershipPoint& right) {
  return sameOwnershipPoint(left, right);
}

inline bool sameFlowEdge(const facts::FlowEdge& left, const facts::FlowEdge& right) {
  return sameOwnershipPoint(left.from, right.from) && sameOwnershipPoint(left.to, right.to);
}

inline bool matchesFlow(zc::ArrayPtr<const facts::FlowFunction> oracle,
                        zc::ArrayPtr<const facts::FlowFunction> production) {
  if (oracle.size() != production.size()) return false;
  for (size_t index = 0; index < oracle.size(); ++index) {
    if (oracle[index].owner != production[index].owner ||
        !sameRecordSet(oracle[index].points.asPtr(), production[index].points.asPtr(),
                       sameFlowPoint) ||
        !sameRecordSet(oracle[index].edges.asPtr(), production[index].edges.asPtr(),
                       sameFlowEdge)) {
      return false;
    }
  }
  return true;
}

inline bool sameInitializationFact(const facts::InitializationFact& left,
                                   const facts::InitializationFact& right) {
  if (left.point != right.point || !sameMovePathKey(left.key, right.key) ||
      left.state != right.state || left.lossCauses.size() != right.lossCauses.size()) {
    return false;
  }
  for (size_t index = 0; index < left.lossCauses.size(); ++index) {
    if (!sameLossCause(left.lossCauses[index], right.lossCauses[index])) return false;
  }
  return true;
}

inline bool matchesInitialization(zc::ArrayPtr<const facts::InitializationFunction> oracle,
                                  zc::ArrayPtr<const facts::InitializationFunction> production) {
  if (oracle.size() != production.size()) return false;
  for (size_t index = 0; index < oracle.size(); ++index) {
    if (oracle[index].owner != production[index].owner ||
        !sameRecordSet(oracle[index].facts.asPtr(), production[index].facts.asPtr(),
                       sameInitializationFact)) {
      return false;
    }
  }
  return true;
}

inline bool sameLoan(const facts::LoanFact& left, const facts::LoanFact& right) {
  return left.owner == right.owner && left.kind == right.kind &&
         sameEventKey(left.issue, right.issue) && sameEventKey(left.commit, right.commit) &&
         sameOwnershipPoint(left.activeFrom, right.activeFrom) &&
         sameMovePathKey(left.source, right.source) &&
         sameMovePathKey(left.destination, right.destination);
}

inline bool matchesLoans(zc::ArrayPtr<const facts::LoanFact> oracle,
                         zc::ArrayPtr<const facts::LoanFact> production) {
  return sameRecordSet(oracle, production, sameLoan);
}

inline bool sameReference(const facts::ReferenceDefinition& left,
                          const facts::ReferenceDefinition& right) {
  return left.owner == right.owner && sameEventKey(left.introduction, right.introduction) &&
         sameEventKey(left.loan, right.loan) &&
         sameEventKey(left.origin.entry, right.origin.entry) &&
         sameOwnershipPoint(left.origin.activation, right.origin.activation) &&
         left.origin.detail == right.origin.detail &&
         sameMovePathKey(left.origin.referent, right.origin.referent) &&
         sameEventKey(left.returned, right.returned) &&
         sameMovePathKey(left.destination, right.destination) &&
         sameOwnershipPoint(left.livePoints.afterCommit, right.livePoints.afterCommit) &&
         sameOwnershipPoint(left.livePoints.afterCommitCfg, right.livePoints.afterCommitCfg) &&
         sameOwnershipPoint(left.livePoints.beforeReturnCfg, right.livePoints.beforeReturnCfg) &&
         sameOwnershipPoint(left.livePoints.beforeReturn, right.livePoints.beforeReturn) &&
         sameOwnershipPoint(left.livePoints.afterReturn, right.livePoints.afterReturn);
}

inline bool matchesReferences(zc::ArrayPtr<const facts::ReferenceDefinition> oracle,
                              zc::ArrayPtr<const facts::ReferenceDefinition> production) {
  return sameRecordSet(oracle, production, sameReference);
}

inline bool sameRegion(const facts::ReborrowRegion& left, const facts::ReborrowRegion& right) {
  if (left.owner != right.owner || !sameEventKey(left.entry, right.entry) ||
      !sameEventKey(left.loan, right.loan) || left.origin != right.origin ||
      left.members.size() != right.members.size()) {
    return false;
  }
  for (size_t index = 0; index < left.members.size(); ++index) {
    if (!sameOwnershipPoint(left.members[index], right.members[index])) return false;
  }
  return true;
}

inline bool matchesRegions(zc::ArrayPtr<const facts::ReborrowRegion> oracle,
                           zc::ArrayPtr<const facts::ReborrowRegion> production) {
  return sameRecordSet(oracle, production, sameRegion);
}

inline bool sameState(const facts::ReborrowState& left, const facts::ReborrowState& right) {
  return left.owner == right.owner && sameOwnershipPoint(left.point, right.point) &&
         sameEventKey(left.loan, right.loan) && left.origin == right.origin &&
         sameMovePathKey(left.destination, right.destination);
}

inline bool matchesStates(zc::ArrayPtr<const facts::ReborrowState> oracle,
                          zc::ArrayPtr<const facts::ReborrowState> production) {
  return sameRecordSet(oracle, production, sameState);
}

inline bool sameResourceSubject(const facts::DropResourceSubject& left,
                                const facts::DropResourceSubject& right) {
  return sameEventKey(left.introduction, right.introduction) &&
         sameMovePathKey(left.origin, right.origin) && left.originType == right.originType;
}

inline bool sameResourceFact(const facts::OwnershipResourceFact& left,
                             const facts::OwnershipResourceFact& right) {
  return sameResourceSubject(left.subject, right.subject) &&
         left.requirement == right.requirement &&
         sameDropAction(left.dropAction, right.dropAction) &&
         left.declarationOrdinal == right.declarationOrdinal;
}

inline bool sameTransfer(const facts::DropTransfer& left, const facts::DropTransfer& right) {
  return sameMovePathKey(left.from, right.from) && sameMovePathKey(left.to, right.to) &&
         sameEventKey(left.event, right.event);
}

inline bool sameCastRoute(const facts::CastResourceRoute& left,
                          const facts::CastResourceRoute& right) {
  return sameResourceSubject(left.subject, right.subject) &&
         sameMovePathKey(left.from, right.from) && sameMovePathKey(left.to, right.to) &&
         sameEventKey(left.event, right.event);
}

inline bool sameDropPlan(const facts::DropPlan& left, const facts::DropPlan& right,
                         zc::ArrayPtr<const facts::OwnershipResourceFact> leftFacts,
                         zc::ArrayPtr<const facts::OwnershipResourceFact> rightFacts) {
  if (!sameResourceSubject(left.subject, right.subject) || left.mode != right.mode ||
      left.components.size() != right.components.size()) {
    return false;
  }
  for (size_t index = 0; index < left.components.size(); ++index) {
    if (left.components[index].factOrdinal >= leftFacts.size() ||
        right.components[index].factOrdinal >= rightFacts.size()) {
      return false;
    }
    const auto& leftFact = leftFacts[left.components[index].factOrdinal];
    const auto& rightFact = rightFacts[right.components[index].factOrdinal];
    if (!sameResourceSubject(leftFact.subject, rightFact.subject) ||
        !sameDropAction(left.components[index].action, right.components[index].action)) {
      return false;
    }
  }
  return true;
}

inline bool matchesResources(zc::ArrayPtr<const facts::OwnershipResourceFunction> oracle,
                             zc::ArrayPtr<const facts::OwnershipResourceFunction> production) {
  if (oracle.size() != production.size()) return false;
  for (size_t index = 0; index < oracle.size(); ++index) {
    if (oracle[index].owner != production[index].owner ||
        !sameRecordSet(oracle[index].facts.asPtr(), production[index].facts.asPtr(),
                       sameResourceFact) ||
        !sameRecordSet(oracle[index].transfers.asPtr(), production[index].transfers.asPtr(),
                       sameTransfer) ||
        !sameRecordSet(oracle[index].castRoutes.asPtr(), production[index].castRoutes.asPtr(),
                       sameCastRoute) ||
        !sameRecordSet(oracle[index].dropPlans.asPtr(), production[index].dropPlans.asPtr(),
                       [&](const facts::DropPlan& left, const facts::DropPlan& right) {
                         return sameDropPlan(left, right, oracle[index].facts.asPtr(),
                                             production[index].facts.asPtr());
                       })) {
      return false;
    }
  }
  return true;
}

}  // namespace test_oracle
}  // namespace zomlang::compiler::ownership
