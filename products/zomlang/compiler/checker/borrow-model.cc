// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/checker/borrow-model.h"

#include "zc/core/map.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/generated/node-traverse.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/symbol/symbol-id.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/compiler/type/type.h"

namespace zomlang {
namespace compiler {
namespace checker {

PlaceProjection PlaceProjection::field(zc::StringPtr name) {
  return PlaceProjection{PlaceProjectionKind::Field, name, 0};
}

PlaceProjection PlaceProjection::deref() {
  return PlaceProjection{PlaceProjectionKind::Deref, ""_zc, 0};
}

PlaceProjection PlaceProjection::indexProjection(uint32_t index) {
  return PlaceProjection{PlaceProjectionKind::Index, ""_zc, index};
}

bool PlaceProjection::equals(const PlaceProjection& other) const {
  return kind == other.kind && name == other.name && index == other.index;
}

Place::Place(PlaceId id, PlaceRootKind rootKind, uint32_t rootId, type::TypeId typeId)
    : id(id), rootKind(rootKind), rootId(rootId), typeId(typeId) {}

Place Place::local(PlaceId id, uint32_t localId, type::TypeId typeId) {
  return Place(id, PlaceRootKind::Local, localId, typeId);
}

Place Place::parameter(PlaceId id, uint32_t parameterId, type::TypeId typeId) {
  return Place(id, PlaceRootKind::Parameter, parameterId, typeId);
}

Place Place::temporary(PlaceId id, uint32_t temporaryId, type::TypeId typeId) {
  return Place(id, PlaceRootKind::Temporary, temporaryId, typeId);
}

Place Place::closureCapture(PlaceId id, uint32_t captureId, type::TypeId typeId) {
  return Place(id, PlaceRootKind::ClosureCapture, captureId, typeId);
}

Place Place::returnSlot(PlaceId id, type::TypeId typeId) {
  return Place(id, PlaceRootKind::ReturnSlot, 0, typeId);
}

PlaceId Place::getId() const { return id; }

PlaceRootKind Place::getRootKind() const { return rootKind; }

uint32_t Place::getRootId() const { return rootId; }

type::TypeId Place::getTypeId() const { return typeId; }

zc::ArrayPtr<const PlaceProjection> Place::getProjections() const { return projections.asPtr(); }

void Place::addFieldProjection(zc::StringPtr name) {
  projections.add(PlaceProjection::field(name));
}

void Place::addDerefProjection() { projections.add(PlaceProjection::deref()); }

void Place::addIndexProjection(uint32_t index) {
  projections.add(PlaceProjection::indexProjection(index));
}

bool Place::sameRoot(const Place& other) const {
  return rootKind == other.rootKind && rootId == other.rootId;
}

bool Place::equals(const Place& other) const {
  if (!sameRoot(other)) { return false; }
  auto lhsProjections = getProjections();
  auto rhsProjections = other.getProjections();
  if (lhsProjections.size() != rhsProjections.size()) { return false; }
  for (size_t i = 0; i < lhsProjections.size(); ++i) {
    if (!lhsProjections[i].equals(rhsProjections[i])) { return false; }
  }
  return true;
}

Region Region::make(RegionId id, RegionKind kind, RegionId parent) {
  return Region{id, kind, parent};
}

bool Region::hasParent() const { return parent.isValid(); }

Loan Loan::make(LoanId id, PlaceId place, LoanKind kind, RegionId region, ast::NodeId origin) {
  return Loan{id, place, kind, region, origin};
}

Move Move::make(MoveId id, PlaceId place, ast::NodeId origin) { return Move{id, place, origin}; }

struct BorrowModel::Impl {
  zc::Vector<Place> places;
  zc::Vector<Region> regions;
  zc::Vector<Loan> loans;
  zc::Vector<Move> moves;
};

struct BorrowPlaceBuilder::Impl {
  BorrowModel& model;
  const ast::Tree& tree;
  const type::TypeEnv& typeEnv;
  zc::Maybe<const ast::BindingMetadata&> metadata;
  zc::HashMap<uint32_t, PlaceId> ownedNodePlaces;
  zc::HashMap<uint32_t, PlaceId>* nodePlaces;

  Impl(BorrowModel& model, const ast::Tree& tree, const type::TypeEnv& typeEnv,
       zc::Maybe<const ast::BindingMetadata&> metadata)
      : model(model),
        tree(tree),
        typeEnv(typeEnv),
        metadata(metadata),
        nodePlaces(&ownedNodePlaces) {}
};

struct BorrowPlaceCollectionResult::Impl {
  BorrowModel model;
  zc::HashMap<uint32_t, PlaceId> nodePlaces;
};

struct BorrowCfg::Impl {
  struct Node {
    BorrowCfgNodeKind kind;
    ast::NodeId astNode;
  };

  zc::Vector<Node> nodes;
  zc::Vector<BorrowCfgEdge> edges;
  BorrowCfgNodeId entry;
  BorrowCfgNodeId exit;
};

struct BorrowMoveFact {
  BorrowCfgNodeId node;
  PlaceId place;
};

struct BorrowCheckerResult::Impl {
  struct FunctionSummary {
    ast::NodeId functionDecl;
    BorrowCfg cfg;
    zc::Vector<BorrowMoveFact> moves;
    zc::Vector<BorrowMoveFact> reinitializes;
    zc::Vector<BorrowUseAfterMoveReport> useAfterMoveReports;
    zc::Vector<BorrowMoveOutOfBorrowReport> moveOutOfBorrowReports;
    zc::Vector<BorrowConflictReport> borrowConflictReports;
    zc::Vector<BorrowRegionEscapeReport> regionEscapeReports;
    zc::Vector<BorrowRawPointerBoundaryReport> rawPointerBoundaryReports;
  };

  BorrowPlaceCollectionResult places;
  zc::Vector<FunctionSummary> functions;
};

struct BorrowCheckerPhase::Impl {
  const ast::Tree& tree;
  const type::TypeEnv& typeEnv;
  zc::Maybe<const ast::BindingMetadata&> metadata;

  Impl(const ast::Tree& tree, const type::TypeEnv& typeEnv,
       zc::Maybe<const ast::BindingMetadata&> metadata) noexcept
      : tree(tree), typeEnv(typeEnv), metadata(metadata) {}
};

namespace {

struct MoveOrigin {
  PlaceId place;
  BorrowCfgNodeId origin;
};

}  // namespace

struct BorrowMoveState::Impl {
  const BorrowCfg& cfg;
  zc::Vector<zc::Vector<PlaceId>> explicitMoves;
  zc::Vector<zc::Vector<PlaceId>> reinitializes;
  zc::Vector<zc::Vector<PlaceId>> movedIn;
  zc::Vector<zc::Vector<PlaceId>> movedAt;
  zc::Vector<zc::Vector<MoveOrigin>> originIn;
  zc::Vector<zc::Vector<MoveOrigin>> originOut;

  explicit Impl(const BorrowCfg& cfg) : cfg(cfg) {
    explicitMoves.resize(cfg.nodeCount() + 1);
    reinitializes.resize(cfg.nodeCount() + 1);
    movedIn.resize(cfg.nodeCount() + 1);
    movedAt.resize(cfg.nodeCount() + 1);
    originIn.resize(cfg.nodeCount() + 1);
    originOut.resize(cfg.nodeCount() + 1);
  }
};

struct BorrowLinearState::Impl {
  const BorrowCfg& cfg;
  zc::Vector<zc::Vector<PlaceId>> initializes;
  zc::Vector<zc::Vector<PlaceId>> consumes;
  zc::Vector<zc::Vector<PlaceId>> outstandingIn;
  zc::Vector<zc::Vector<PlaceId>> outstandingOut;
  zc::Vector<zc::Vector<MoveOrigin>> originIn;
  zc::Vector<zc::Vector<MoveOrigin>> originOut;
  zc::Vector<zc::Vector<PlaceId>> consumedIn;
  zc::Vector<zc::Vector<PlaceId>> consumedOut;
  zc::Vector<zc::Vector<MoveOrigin>> consumeOriginIn;
  zc::Vector<zc::Vector<MoveOrigin>> consumeOriginOut;

  explicit Impl(const BorrowCfg& cfg) : cfg(cfg) {
    initializes.resize(cfg.nodeCount() + 1);
    consumes.resize(cfg.nodeCount() + 1);
    outstandingIn.resize(cfg.nodeCount() + 1);
    outstandingOut.resize(cfg.nodeCount() + 1);
    originIn.resize(cfg.nodeCount() + 1);
    originOut.resize(cfg.nodeCount() + 1);
    consumedIn.resize(cfg.nodeCount() + 1);
    consumedOut.resize(cfg.nodeCount() + 1);
    consumeOriginIn.resize(cfg.nodeCount() + 1);
    consumeOriginOut.resize(cfg.nodeCount() + 1);
  }
};

struct BorrowLoanState::Impl {
  const BorrowCfg& cfg;
  const BorrowModel& model;
  zc::Vector<zc::Vector<LoanId>> explicitLoans;
  zc::Vector<zc::Vector<LoanId>> endLoans;
  zc::Vector<zc::Vector<LoanId>> suspendLoans;
  zc::Vector<zc::Vector<LoanId>> resumeLoans;
  zc::Vector<zc::Vector<LoanId>> activeIn;
  zc::Vector<zc::Vector<LoanId>> activeOut;
  zc::Vector<zc::Vector<LoanId>> suspendedIn;
  zc::Vector<zc::Vector<LoanId>> suspendedOut;

  Impl(const BorrowCfg& cfg, const BorrowModel& model) : cfg(cfg), model(model) {
    explicitLoans.resize(cfg.nodeCount() + 1);
    endLoans.resize(cfg.nodeCount() + 1);
    suspendLoans.resize(cfg.nodeCount() + 1);
    resumeLoans.resize(cfg.nodeCount() + 1);
    activeIn.resize(cfg.nodeCount() + 1);
    activeOut.resize(cfg.nodeCount() + 1);
    suspendedIn.resize(cfg.nodeCount() + 1);
    suspendedOut.resize(cfg.nodeCount() + 1);
  }
};

struct BorrowLoanBuilder::Impl {
  BorrowModel& model;
  const ast::Tree& tree;
  const BorrowPlaceCollectionResult& places;
  zc::Vector<ast::NodeId> mutableBorrowExprs;

  Impl(BorrowModel& model, const ast::Tree& tree, const BorrowPlaceCollectionResult& places)
      : model(model), tree(tree), places(places) {}
};

namespace {

enum class ProjectionRelation {
  Same,
  DivergedDisjoint,
  MayAlias,
};

ProjectionRelation compareProjection(const PlaceProjection& lhs, const PlaceProjection& rhs,
                                     FieldOverlapMode fieldMode) {
  if (lhs.equals(rhs)) { return ProjectionRelation::Same; }

  if (lhs.kind == PlaceProjectionKind::Field && rhs.kind == PlaceProjectionKind::Field) {
    return fieldMode == FieldOverlapMode::ProvenDisjoint ? ProjectionRelation::DivergedDisjoint
                                                         : ProjectionRelation::MayAlias;
  }

  return ProjectionRelation::MayAlias;
}

bool isValidIndex(uint32_t idValue, size_t size) {
  return idValue != 0 && static_cast<size_t>(idValue - 1) < size;
}

ast::NodeId payloadNode(const ast::Node& node, uint32_t word) {
  return ast::NodeId(node.payload.words[word]);
}

ast::NodeList payloadList(const ast::Node& node, uint32_t firstWord, uint32_t sizeWord) {
  return ast::NodeList{node.payload.words[firstWord], node.payload.words[sizeWord]};
}

}  // namespace

bool placesOverlap(const Place& lhs, const Place& rhs, FieldOverlapMode fieldMode) {
  if (!lhs.sameRoot(rhs)) { return false; }

  auto lhsProjections = lhs.getProjections();
  auto rhsProjections = rhs.getProjections();
  size_t sharedLength =
      lhsProjections.size() < rhsProjections.size() ? lhsProjections.size() : rhsProjections.size();

  for (size_t i = 0; i < sharedLength; ++i) {
    auto relation = compareProjection(lhsProjections[i], rhsProjections[i], fieldMode);
    if (relation == ProjectionRelation::DivergedDisjoint) { return false; }
    if (relation == ProjectionRelation::MayAlias) { return true; }
  }

  return true;
}

BorrowModel::BorrowModel() : impl(zc::heap<Impl>()) {}

BorrowModel::~BorrowModel() noexcept(false) = default;

BorrowModel::BorrowModel(BorrowModel&& other) noexcept = default;

BorrowModel& BorrowModel::operator=(BorrowModel&& other) noexcept = default;

PlaceId BorrowModel::addLocalPlace(uint32_t localId, type::TypeId typeId) {
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  impl->places.add(Place::local(id, localId, typeId));
  return id;
}

PlaceId BorrowModel::addParameterPlace(uint32_t parameterId, type::TypeId typeId) {
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  impl->places.add(Place::parameter(id, parameterId, typeId));
  return id;
}

PlaceId BorrowModel::addTemporaryPlace(uint32_t temporaryId, type::TypeId typeId) {
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  impl->places.add(Place::temporary(id, temporaryId, typeId));
  return id;
}

PlaceId BorrowModel::addClosureCapturePlace(uint32_t captureId, type::TypeId typeId) {
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  impl->places.add(Place::closureCapture(id, captureId, typeId));
  return id;
}

PlaceId BorrowModel::addReturnSlotPlace(type::TypeId typeId) {
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  impl->places.add(Place::returnSlot(id, typeId));
  return id;
}

PlaceId BorrowModel::addFieldPlace(PlaceId base, zc::StringPtr fieldName) {
  ZC_IREQUIRE(isValidIndex(base.value, impl->places.size()),
              "BorrowModel::addFieldPlace: invalid base place id");
  const auto& basePlace = impl->places[base.value - 1];
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  Place result(id, basePlace.getRootKind(), basePlace.getRootId(), basePlace.getTypeId());
  auto projections = basePlace.getProjections();
  for (size_t i = 0; i < projections.size(); ++i) {
    const auto& projection = projections[i];
    switch (projection.kind) {
      case PlaceProjectionKind::Field:
        result.addFieldProjection(projection.name);
        break;
      case PlaceProjectionKind::Deref:
        result.addDerefProjection();
        break;
      case PlaceProjectionKind::Index:
        result.addIndexProjection(projection.index);
        break;
    }
  }
  result.addFieldProjection(fieldName);
  impl->places.add(zc::mv(result));
  return id;
}

PlaceId BorrowModel::addDerefPlace(PlaceId base) {
  ZC_IREQUIRE(isValidIndex(base.value, impl->places.size()),
              "BorrowModel::addDerefPlace: invalid base place id");
  const auto& basePlace = impl->places[base.value - 1];
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  Place result(id, basePlace.getRootKind(), basePlace.getRootId(), basePlace.getTypeId());
  auto projections = basePlace.getProjections();
  for (size_t i = 0; i < projections.size(); ++i) {
    const auto& projection = projections[i];
    switch (projection.kind) {
      case PlaceProjectionKind::Field:
        result.addFieldProjection(projection.name);
        break;
      case PlaceProjectionKind::Deref:
        result.addDerefProjection();
        break;
      case PlaceProjectionKind::Index:
        result.addIndexProjection(projection.index);
        break;
    }
  }
  result.addDerefProjection();
  impl->places.add(zc::mv(result));
  return id;
}

PlaceId BorrowModel::addIndexPlace(PlaceId base, uint32_t index) {
  ZC_IREQUIRE(isValidIndex(base.value, impl->places.size()),
              "BorrowModel::addIndexPlace: invalid base place id");
  const auto& basePlace = impl->places[base.value - 1];
  auto id = PlaceId(static_cast<uint32_t>(impl->places.size() + 1));
  Place result(id, basePlace.getRootKind(), basePlace.getRootId(), basePlace.getTypeId());
  auto projections = basePlace.getProjections();
  for (size_t i = 0; i < projections.size(); ++i) {
    const auto& projection = projections[i];
    switch (projection.kind) {
      case PlaceProjectionKind::Field:
        result.addFieldProjection(projection.name);
        break;
      case PlaceProjectionKind::Deref:
        result.addDerefProjection();
        break;
      case PlaceProjectionKind::Index:
        result.addIndexProjection(projection.index);
        break;
    }
  }
  result.addIndexProjection(index);
  impl->places.add(zc::mv(result));
  return id;
}

void BorrowModel::addFieldProjection(PlaceId id, zc::StringPtr name) {
  ZC_IREQUIRE(isValidIndex(id.value, impl->places.size()),
              "BorrowModel::addFieldProjection: invalid place id");
  impl->places[id.value - 1].addFieldProjection(name);
}

void BorrowModel::addDerefProjection(PlaceId id) {
  ZC_IREQUIRE(isValidIndex(id.value, impl->places.size()),
              "BorrowModel::addDerefProjection: invalid place id");
  impl->places[id.value - 1].addDerefProjection();
}

void BorrowModel::addIndexProjection(PlaceId id, uint32_t index) {
  ZC_IREQUIRE(isValidIndex(id.value, impl->places.size()),
              "BorrowModel::addIndexProjection: invalid place id");
  impl->places[id.value - 1].addIndexProjection(index);
}

zc::Maybe<const Place&> BorrowModel::getPlace(PlaceId id) const {
  if (!isValidIndex(id.value, impl->places.size())) { return zc::none; }
  return impl->places[id.value - 1];
}

size_t BorrowModel::placeCount() const { return impl->places.size(); }

RegionId BorrowModel::addRegion(RegionKind kind, RegionId parent) {
  auto id = RegionId(static_cast<uint32_t>(impl->regions.size() + 1));
  impl->regions.add(Region::make(id, kind, parent));
  return id;
}

zc::Maybe<const Region&> BorrowModel::getRegion(RegionId id) const {
  if (!isValidIndex(id.value, impl->regions.size())) { return zc::none; }
  return impl->regions[id.value - 1];
}

size_t BorrowModel::regionCount() const { return impl->regions.size(); }

bool BorrowModel::regionOutlives(RegionId longer, RegionId shorter) const {
  if (!longer.isValid() || !shorter.isValid()) { return false; }
  auto current = shorter;
  while (current.isValid()) {
    if (current == longer) { return true; }
    ZC_IF_SOME(region, getRegion(current)) { current = region.parent; }
    else { return false; }
  }
  return false;
}

zc::Maybe<BorrowRegionEscapeReport> BorrowModel::checkRegionEscape(RegionId targetRegion,
                                                                   RegionId referentRegion,
                                                                   ast::NodeId useNode,
                                                                   ast::NodeId referentNode) const {
  if (regionOutlives(referentRegion, targetRegion)) { return zc::none; }
  return BorrowRegionEscapeReport{targetRegion, referentRegion, useNode, referentNode};
}

zc::Maybe<BorrowScopedTaskCaptureReport> BorrowModel::checkScopedTaskCapture(
    RegionId taskRegion, RegionId referentRegion, ast::NodeId captureNode,
    ast::NodeId referentNode) const {
  if (regionOutlives(referentRegion, taskRegion)) { return zc::none; }
  return BorrowScopedTaskCaptureReport{taskRegion, referentRegion, captureNode, referentNode};
}

zc::Maybe<BorrowRawPointerBoundaryReport> BorrowModel::checkRawPointerBoundary(
    ast::NodeId boundaryNode, bool unsafeAcknowledged) const {
  if (unsafeAcknowledged) { return zc::none; }
  return BorrowRawPointerBoundaryReport{boundaryNode};
}

LoanId BorrowModel::addLoan(PlaceId place, LoanKind kind, RegionId region, ast::NodeId origin) {
  auto id = LoanId(static_cast<uint32_t>(impl->loans.size() + 1));
  impl->loans.add(Loan::make(id, place, kind, region, origin));
  return id;
}

zc::Maybe<const Loan&> BorrowModel::getLoan(LoanId id) const {
  if (!isValidIndex(id.value, impl->loans.size())) { return zc::none; }
  return impl->loans[id.value - 1];
}

size_t BorrowModel::loanCount() const { return impl->loans.size(); }

zc::Maybe<const Loan&> BorrowModel::findConflictingLoan(PlaceId place, LoanKind requestedKind,
                                                        FieldOverlapMode fieldMode) const {
  for (size_t i = 0; i < impl->loans.size(); ++i) {
    const auto& existing = impl->loans[i];
    if (!placesOverlap(place, existing.place, fieldMode)) { continue; }
    if (requestedKind == LoanKind::Mutable || existing.kind == LoanKind::Mutable) {
      return existing;
    }
  }
  return zc::none;
}

MoveId BorrowModel::addMove(PlaceId place, ast::NodeId origin) {
  auto id = MoveId(static_cast<uint32_t>(impl->moves.size() + 1));
  impl->moves.add(Move::make(id, place, origin));
  return id;
}

zc::Maybe<const Move&> BorrowModel::getMove(MoveId id) const {
  if (!isValidIndex(id.value, impl->moves.size())) { return zc::none; }
  return impl->moves[id.value - 1];
}

size_t BorrowModel::moveCount() const { return impl->moves.size(); }

bool BorrowModel::placesOverlap(PlaceId lhs, PlaceId rhs, FieldOverlapMode fieldMode) const {
  auto lhsPlace = getPlace(lhs);
  auto rhsPlace = getPlace(rhs);
  if (lhsPlace == zc::none || rhsPlace == zc::none) { return false; }
  ZC_IF_SOME(l, lhsPlace) {
    ZC_IF_SOME(r, rhsPlace) { return checker::placesOverlap(l, r, fieldMode); }
  }
  return false;
}

BorrowPlaceBuilder::BorrowPlaceBuilder(BorrowModel& model, const ast::Tree& tree,
                                       const type::TypeEnv& typeEnv,
                                       zc::Maybe<const ast::BindingMetadata&> metadata) noexcept
    : impl(zc::heap<Impl>(model, tree, typeEnv, metadata)) {}

BorrowPlaceBuilder::~BorrowPlaceBuilder() noexcept(false) = default;

namespace {

void mapNodePlace(zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId node, PlaceId place) {
  nodePlaces.upsert(node.value, place);
}

uint32_t rootIdForNode(zc::Maybe<const ast::BindingMetadata&> metadata, ast::NodeId node,
                       uint32_t fallback) {
  ZC_IF_SOME(meta, metadata) {
    auto symbolId = meta.symbol(node);
    if (symbolId.isValid()) { return static_cast<uint32_t>(symbolId.getRaw()); }
  }
  return fallback;
}

uint32_t rootIdForLocalPattern(zc::Maybe<const ast::BindingMetadata&> metadata, ast::NodeId pattern,
                               ast::NodeId declarator) {
  ZC_IF_SOME(meta, metadata) {
    auto patternSymbol = meta.symbol(pattern);
    if (patternSymbol.isValid()) { return static_cast<uint32_t>(patternSymbol.getRaw()); }
    if (declarator) {
      auto declaratorSymbol = meta.symbol(declarator);
      if (declaratorSymbol.isValid()) { return static_cast<uint32_t>(declaratorSymbol.getRaw()); }
    }
  }
  return pattern.value;
}

zc::StringPtr localPatternName(const ast::Tree& tree, const ast::Node& node) {
  if (node.kind == ast::SyntaxKind::BindingPattern) {
    return tree.ident(ast::IdentId(node.payload.words[ast::kBindingPatternNameWord]));
  }
  if (node.kind == ast::SyntaxKind::IdentifierPattern) {
    return tree.ident(ast::IdentId(node.payload.words[ast::kIdentifierPatternNameWord]));
  }
  return ""_zc;
}

void buildParameterPlace(BorrowModel& model, const type::TypeEnv& typeEnv,
                         zc::Maybe<const ast::BindingMetadata&> metadata,
                         zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId param,
                         uint32_t index) {
  if (!param || !typeEnv.hasType(param)) { return; }
  auto place =
      model.addParameterPlace(rootIdForNode(metadata, param, index), typeEnv.getTypeId(param));
  mapNodePlace(nodePlaces, param, place);
}

void buildLocalPatternPlace(BorrowModel& model, const ast::Tree& tree, const type::TypeEnv& typeEnv,
                            zc::Maybe<const ast::BindingMetadata&> metadata,
                            zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId pattern,
                            ast::NodeId declarator = ast::NodeId()) {
  if (!pattern) { return; }
  const auto& node = tree.node(pattern);
  if (localPatternName(tree, node).size() == 0) { return; }
  type::TypeId typeId;
  if (typeEnv.hasType(pattern)) {
    typeId = typeEnv.getTypeId(pattern);
  } else if (declarator && typeEnv.hasType(declarator)) {
    typeId = typeEnv.getTypeId(declarator);
  } else {
    return;
  }
  auto place = model.addLocalPlace(rootIdForLocalPattern(metadata, pattern, declarator), typeId);
  mapNodePlace(nodePlaces, pattern, place);
}

struct MappedBindingPlace {
  ast::NodeId binding;
  PlaceId place;
};

zc::Maybe<MappedBindingPlace> findMappedBindingByName(
    const ast::Tree& tree, const zc::HashMap<uint32_t, PlaceId>& nodePlaces, zc::StringPtr name) {
  auto nodes = tree.nodes();
  for (size_t offset = nodes.size(); offset > 0; --offset) {
    auto i = offset - 1;
    auto bindingName = localPatternName(tree, nodes[i]);
    if (bindingName.size() == 0) { continue; }
    if (bindingName != name) { continue; }
    auto binding = ast::NodeId(static_cast<uint32_t>(i + 1));
    auto place = nodePlaces.find(binding.value);
    ZC_IF_SOME(placeId, place) { return MappedBindingPlace{binding, placeId}; }
  }
  return zc::none;
}

zc::Maybe<PlaceId> findMappedBindingPlaceByName(const ast::Tree& tree,
                                                const zc::HashMap<uint32_t, PlaceId>& nodePlaces,
                                                zc::StringPtr name) {
  ZC_IF_SOME(binding, findMappedBindingByName(tree, nodePlaces, name)) { return binding.place; }
  return zc::none;
}

bool containsNode(zc::ArrayPtr<const ast::NodeId> nodes, ast::NodeId node) {
  for (auto candidate : nodes) {
    if (candidate == node) { return true; }
  }
  return false;
}

zc::Maybe<PlaceId> buildClosureCaptureExpressionPlace(
    BorrowModel& model, const ast::Tree& tree, const type::TypeEnv& typeEnv,
    zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId expr,
    zc::ArrayPtr<const ast::NodeId> localBindings) {
  if (!expr || !tree.contains(expr)) { return zc::none; }
  auto existing = nodePlaces.find(expr.value);
  ZC_IF_SOME(place, existing) { return place; }
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::IdentExpr) {
    auto name = tree.ident(ast::IdentId(exprNode.payload.words[ast::kIdentExprNameWord]));
    ZC_IF_SOME(binding, findMappedBindingByName(tree, nodePlaces, name)) {
      if (containsNode(localBindings, binding.binding)) {
        mapNodePlace(nodePlaces, expr, binding.place);
        return binding.place;
      }
      ZC_IF_SOME(bindingPlace, model.getPlace(binding.place)) {
        auto place = model.addClosureCapturePlace(binding.binding.value, bindingPlace.getTypeId());
        mapNodePlace(nodePlaces, expr, place);
        return place;
      }
    }
    return zc::none;
  }
  if (!typeEnv.hasType(expr)) { return zc::none; }
  if (exprNode.kind == ast::SyntaxKind::MemberExpression) {
    auto object = payloadNode(exprNode, ast::kMemberExpressionObjectWord);
    ZC_IF_SOME(basePlace, buildClosureCaptureExpressionPlace(model, tree, typeEnv, nodePlaces,
                                                             object, localBindings)) {
      auto property =
          tree.ident(ast::IdentId(exprNode.payload.words[ast::kMemberExpressionPropertyWord]));
      auto place = model.addFieldPlace(basePlace, property);
      mapNodePlace(nodePlaces, expr, place);
      return place;
    }
    return zc::none;
  }
  if (exprNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto op =
        static_cast<ast::UnaryOperatorKind>(exprNode.payload.words[ast::kUnaryExpressionOpWord]);
    if (op != ast::UnaryOperatorKind::Deref) { return zc::none; }
    auto operand = payloadNode(exprNode, ast::kUnaryExpressionOperandWord);
    ZC_IF_SOME(basePlace, buildClosureCaptureExpressionPlace(model, tree, typeEnv, nodePlaces,
                                                             operand, localBindings)) {
      auto place = model.addDerefPlace(basePlace);
      mapNodePlace(nodePlaces, expr, place);
      return place;
    }
    return zc::none;
  }
  if (exprNode.kind == ast::SyntaxKind::IndexExpression) {
    auto object = payloadNode(exprNode, ast::kIndexExpressionObjectWord);
    ZC_IF_SOME(basePlace, buildClosureCaptureExpressionPlace(model, tree, typeEnv, nodePlaces,
                                                             object, localBindings)) {
      auto place = model.addIndexPlace(basePlace, 0);
      mapNodePlace(nodePlaces, expr, place);
      return place;
    }
  }
  return zc::none;
}

void buildDirectClosureCaptureExpressionPlace(BorrowModel& model, const ast::Tree& tree,
                                              const type::TypeEnv& typeEnv,
                                              zc::HashMap<uint32_t, PlaceId>& nodePlaces,
                                              ast::NodeId expr) {
  if (!expr || !tree.contains(expr) || !typeEnv.hasType(expr)) { return; }
  if (nodePlaces.find(expr.value) != zc::none) { return; }
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind != ast::SyntaxKind::IdentExpr) { return; }
  auto name = tree.ident(ast::IdentId(exprNode.payload.words[ast::kIdentExprNameWord]));
  ZC_IF_SOME(binding, findMappedBindingByName(tree, nodePlaces, name)) {
    auto place = model.addClosureCapturePlace(binding.binding.value, typeEnv.getTypeId(expr));
    mapNodePlace(nodePlaces, expr, place);
  }
}

void buildClosureCapturePlacesInBlock(BorrowModel& model, const ast::Tree& tree,
                                      const type::TypeEnv& typeEnv,
                                      zc::HashMap<uint32_t, PlaceId>& nodePlaces,
                                      ast::NodeId block) {
  if (!block || !tree.contains(block)) { return; }
  const auto& blockNode = tree.node(block);
  if (blockNode.kind != ast::SyntaxKind::BlockStmt) { return; }
  auto stmts = tree.list(
      payloadList(blockNode, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord));
  zc::Vector<ast::NodeId> localBindings;
  for (size_t i = 0; i < stmts.size(); ++i) {
    auto item = stmts[i];
    if (!item || !tree.contains(item)) { continue; }
    const auto& itemNode = tree.node(item);
    if (itemNode.kind == ast::SyntaxKind::StatementListItem) {
      item = payloadNode(itemNode, ast::kStatementListItemItemWord);
    }
    if (!item || !tree.contains(item)) { continue; }
    const auto& stmtNode = tree.node(item);
    if (stmtNode.kind != ast::SyntaxKind::LetStmt) { continue; }
    auto declListId = payloadNode(stmtNode, ast::kLetStmtDeclarationsWord);
    if (!declListId || !tree.contains(declListId)) { continue; }
    const auto& declListNode = tree.node(declListId);
    if (declListNode.kind != ast::SyntaxKind::VariableDeclaratorList) { continue; }
    auto decls = tree.list(payloadList(declListNode, ast::kVariableDeclaratorListDeclsFirstWord,
                                       ast::kVariableDeclaratorListDeclsSizeWord));
    for (size_t j = 0; j < decls.size(); ++j) {
      if (!decls[j] || !tree.contains(decls[j])) { continue; }
      const auto& declNode = tree.node(decls[j]);
      if (declNode.kind != ast::SyntaxKind::VariableDeclarator) { continue; }
      auto pattern = payloadNode(declNode, ast::kVariableDeclaratorPatternWord);
      buildLocalPatternPlace(model, tree, typeEnv, zc::none, nodePlaces, pattern);
      if (pattern) { localBindings.add(pattern); }
    }
  }
  for (size_t i = 0; i < stmts.size(); ++i) {
    auto item = stmts[i];
    if (!item || !tree.contains(item)) { continue; }
    const auto& itemNode = tree.node(item);
    if (itemNode.kind == ast::SyntaxKind::StatementListItem) {
      item = payloadNode(itemNode, ast::kStatementListItemItemWord);
    }
    if (!item || !tree.contains(item)) { continue; }
    const auto& stmtNode = tree.node(item);
    if (stmtNode.kind != ast::SyntaxKind::ExpressionStatement) { continue; }
    buildClosureCaptureExpressionPlace(
        model, tree, typeEnv, nodePlaces,
        payloadNode(stmtNode, ast::kExpressionStatementExpressionWord), localBindings.asPtr());
  }
}

void buildClosureCapturePlaces(BorrowModel& model, const ast::Tree& tree,
                               const type::TypeEnv& typeEnv,
                               zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId expr) {
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::FunctionExpression) {
    buildClosureCapturePlacesInBlock(model, tree, typeEnv, nodePlaces,
                                     payloadNode(exprNode, ast::kFunctionExpressionBodyWord));
  }
  if (exprNode.kind == ast::SyntaxKind::LambdaExpression) {
    buildClosureCapturePlacesInBlock(model, tree, typeEnv, nodePlaces,
                                     payloadNode(exprNode, ast::kLambdaExpressionBodyWord));
    buildDirectClosureCaptureExpressionPlace(
        model, tree, typeEnv, nodePlaces,
        payloadNode(exprNode, ast::kLambdaExpressionExprBodyWord));
  }
}

zc::Maybe<PlaceId> buildExpressionPlace(BorrowModel& model, const ast::Tree& tree,
                                        const type::TypeEnv& typeEnv,
                                        zc::HashMap<uint32_t, PlaceId>& nodePlaces,
                                        ast::NodeId expr);

void buildExpressionPlacesInList(BorrowModel& model, const ast::Tree& tree,
                                 const type::TypeEnv& typeEnv,
                                 zc::HashMap<uint32_t, PlaceId>& nodePlaces,
                                 ast::NodeList expressions) {
  auto exprs = tree.list(expressions);
  for (size_t i = 0; i < exprs.size(); ++i) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces, exprs[i]);
  }
}

void buildBlockPlaces(BorrowModel& model, const ast::Tree& tree, const type::TypeEnv& typeEnv,
                      zc::Maybe<const ast::BindingMetadata&> metadata,
                      zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId block);

zc::Maybe<PlaceId> buildExpressionPlace(BorrowModel& model, const ast::Tree& tree,
                                        const type::TypeEnv& typeEnv,
                                        zc::HashMap<uint32_t, PlaceId>& nodePlaces,
                                        ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return zc::none; }
  auto existing = nodePlaces.find(expr.value);
  ZC_IF_SOME(place, existing) { return place; }

  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::IdentExpr) {
    auto name = tree.ident(ast::IdentId(exprNode.payload.words[ast::kIdentExprNameWord]));
    return findMappedBindingPlaceByName(tree, nodePlaces, name);
  }
  if (!typeEnv.hasType(expr)) { return zc::none; }
  if (exprNode.kind == ast::SyntaxKind::MemberExpression) {
    auto object = payloadNode(exprNode, ast::kMemberExpressionObjectWord);
    ZC_IF_SOME(basePlace, buildExpressionPlace(model, tree, typeEnv, nodePlaces, object)) {
      auto property =
          tree.ident(ast::IdentId(exprNode.payload.words[ast::kMemberExpressionPropertyWord]));
      auto place = model.addFieldPlace(basePlace, property);
      mapNodePlace(nodePlaces, expr, place);
      return place;
    }
    return zc::none;
  }
  if (exprNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto op =
        static_cast<ast::UnaryOperatorKind>(exprNode.payload.words[ast::kUnaryExpressionOpWord]);
    if (op != ast::UnaryOperatorKind::Deref) { return zc::none; }
    auto operand = payloadNode(exprNode, ast::kUnaryExpressionOperandWord);
    ZC_IF_SOME(basePlace, buildExpressionPlace(model, tree, typeEnv, nodePlaces, operand)) {
      auto place = model.addDerefPlace(basePlace);
      mapNodePlace(nodePlaces, expr, place);
      return place;
    }
    return zc::none;
  }
  if (exprNode.kind == ast::SyntaxKind::IndexExpression) {
    auto object = payloadNode(exprNode, ast::kIndexExpressionObjectWord);
    ZC_IF_SOME(basePlace, buildExpressionPlace(model, tree, typeEnv, nodePlaces, object)) {
      auto place = model.addIndexPlace(basePlace, 0);
      mapNodePlace(nodePlaces, expr, place);
      return place;
    }
  }
  if (exprNode.kind == ast::SyntaxKind::CallExpression) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(exprNode, ast::kCallExpressionCalleeWord));
    buildExpressionPlacesInList(
        model, tree, typeEnv, nodePlaces,
        payloadList(exprNode, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord));
  }
  if (exprNode.kind == ast::SyntaxKind::AssignmentExpr) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(exprNode, ast::kAssignmentExprLhsWord));
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(exprNode, ast::kAssignmentExprRhsWord));
  }
  if (exprNode.kind == ast::SyntaxKind::FunctionExpression ||
      exprNode.kind == ast::SyntaxKind::LambdaExpression) {
    buildClosureCapturePlaces(model, tree, typeEnv, nodePlaces, expr);
  }
  auto place = model.addTemporaryPlace(expr.value, typeEnv.getTypeId(expr));
  mapNodePlace(nodePlaces, expr, place);
  return place;
}

void buildExpressionPlacesInStatement(BorrowModel& model, const ast::Tree& tree,
                                      const type::TypeEnv& typeEnv,
                                      zc::Maybe<const ast::BindingMetadata&> metadata,
                                      zc::HashMap<uint32_t, PlaceId>& nodePlaces,
                                      ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind == ast::SyntaxKind::ExpressionStatement) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(statementNode, ast::kExpressionStatementExpressionWord));
  }
  if (statementNode.kind == ast::SyntaxKind::ReturnStmt) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(statementNode, ast::kReturnStmtValueWord));
  }
  if (statementNode.kind == ast::SyntaxKind::IfStmt) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(statementNode, ast::kIfStmtCondWord));
    buildBlockPlaces(model, tree, typeEnv, metadata, nodePlaces,
                     payloadNode(statementNode, ast::kIfStmtThenStmtWord));
    buildBlockPlaces(model, tree, typeEnv, metadata, nodePlaces,
                     payloadNode(statementNode, ast::kIfStmtElseStmtWord));
  }
  if (statementNode.kind == ast::SyntaxKind::WhileStmt) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(statementNode, ast::kWhileStmtCondWord));
    buildBlockPlaces(model, tree, typeEnv, metadata, nodePlaces,
                     payloadNode(statementNode, ast::kWhileStmtBodyWord));
  }
  if (statementNode.kind == ast::SyntaxKind::MatchStmt) {
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(statementNode, ast::kMatchStmtScrutineeWord));
    auto arms = tree.list(
        payloadList(statementNode, ast::kMatchStmtArmsFirstWord, ast::kMatchStmtArmsSizeWord));
    for (size_t i = 0; i < arms.size(); ++i) {
      if (!arms[i] || !tree.contains(arms[i])) { continue; }
      const auto& armNode = tree.node(arms[i]);
      if (armNode.kind != ast::SyntaxKind::MatchArmStmt) { continue; }
      buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                           payloadNode(armNode, ast::kMatchArmStmtGuardWord));
      buildBlockPlaces(model, tree, typeEnv, metadata, nodePlaces,
                       payloadNode(armNode, ast::kMatchArmStmtBodyWord));
    }
  }
}

void buildLetPlaces(BorrowModel& model, const ast::Tree& tree, const type::TypeEnv& typeEnv,
                    zc::Maybe<const ast::BindingMetadata&> metadata,
                    zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId letStmt) {
  const auto& letNode = tree.node(letStmt);
  auto declListId = payloadNode(letNode, ast::kLetStmtDeclarationsWord);
  if (!declListId || !tree.contains(declListId)) { return; }

  const auto& declListNode = tree.node(declListId);
  if (declListNode.kind != ast::SyntaxKind::VariableDeclaratorList) { return; }

  auto decls = tree.list(payloadList(declListNode, ast::kVariableDeclaratorListDeclsFirstWord,
                                     ast::kVariableDeclaratorListDeclsSizeWord));
  for (size_t i = 0; i < decls.size(); ++i) {
    const auto& declNode = tree.node(decls[i]);
    if (declNode.kind != ast::SyntaxKind::VariableDeclarator) { continue; }
    buildLocalPatternPlace(model, tree, typeEnv, metadata, nodePlaces,
                           payloadNode(declNode, ast::kVariableDeclaratorPatternWord), decls[i]);
    buildExpressionPlace(model, tree, typeEnv, nodePlaces,
                         payloadNode(declNode, ast::kVariableDeclaratorInitWord));
  }
}

void buildBlockPlaces(BorrowModel& model, const ast::Tree& tree, const type::TypeEnv& typeEnv,
                      zc::Maybe<const ast::BindingMetadata&> metadata,
                      zc::HashMap<uint32_t, PlaceId>& nodePlaces, ast::NodeId block) {
  if (!block || !tree.contains(block)) { return; }
  const auto& blockNode = tree.node(block);
  if (blockNode.kind != ast::SyntaxKind::BlockStmt) { return; }

  auto stmts = tree.list(
      payloadList(blockNode, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord));
  for (size_t i = 0; i < stmts.size(); ++i) {
    ast::NodeId item = stmts[i];
    if (!item || !tree.contains(item)) { continue; }
    const auto& itemNode = tree.node(item);
    if (itemNode.kind == ast::SyntaxKind::StatementListItem) {
      item = payloadNode(itemNode, ast::kStatementListItemItemWord);
    }
    if (!item || !tree.contains(item)) { continue; }
    const auto& stmtNode = tree.node(item);
    if (stmtNode.kind == ast::SyntaxKind::LetStmt) {
      buildLetPlaces(model, tree, typeEnv, metadata, nodePlaces, item);
    }
    buildExpressionPlacesInStatement(model, tree, typeEnv, metadata, nodePlaces, item);
  }
}

}  // namespace

void BorrowPlaceBuilder::buildFunctionPlaces(ast::NodeId functionDecl) {
  if (!functionDecl || !impl->tree.contains(functionDecl)) { return; }
  const auto& fnNode = impl->tree.node(functionDecl);
  if (fnNode.kind != ast::SyntaxKind::FunctionDecl) { return; }

  if (impl->typeEnv.hasType(functionDecl)) {
    auto place = impl->model.addReturnSlotPlace(impl->typeEnv.getTypeId(functionDecl));
    mapNodePlace(*impl->nodePlaces, functionDecl, place);
  }

  auto paramsId = payloadNode(fnNode, ast::kFunctionDeclParamsIdWord);
  if (paramsId && impl->tree.contains(paramsId)) {
    const auto& paramsNode = impl->tree.node(paramsId);
    if (paramsNode.kind == ast::SyntaxKind::FunctionParameterList) {
      auto params =
          impl->tree.list(payloadList(paramsNode, ast::kFunctionParameterListParamsFirstWord,
                                      ast::kFunctionParameterListParamsSizeWord));
      for (size_t i = 0; i < params.size(); ++i) {
        buildParameterPlace(impl->model, impl->typeEnv, impl->metadata, *impl->nodePlaces,
                            params[i], static_cast<uint32_t>(i));
      }
    }
  }

  buildBlockPlaces(impl->model, impl->tree, impl->typeEnv, impl->metadata, *impl->nodePlaces,
                   payloadNode(fnNode, ast::kFunctionDeclBodyWord));
}

zc::Maybe<PlaceId> BorrowPlaceBuilder::getPlaceForNode(ast::NodeId node) const {
  auto it = impl->nodePlaces->find(node.value);
  ZC_IF_SOME(entry, it) { return entry; }
  return zc::none;
}

size_t BorrowPlaceBuilder::mappedNodeCount() const { return impl->nodePlaces->size(); }

BorrowPlaceCollectionResult::BorrowPlaceCollectionResult() : impl(zc::heap<Impl>()) {}

BorrowPlaceCollectionResult::~BorrowPlaceCollectionResult() noexcept(false) = default;

BorrowPlaceCollectionResult::BorrowPlaceCollectionResult(
    BorrowPlaceCollectionResult&& other) noexcept = default;

BorrowPlaceCollectionResult& BorrowPlaceCollectionResult::operator=(
    BorrowPlaceCollectionResult&& other) noexcept = default;

BorrowModel& BorrowPlaceCollectionResult::getModel() { return impl->model; }

const BorrowModel& BorrowPlaceCollectionResult::getModel() const { return impl->model; }

void BorrowPlaceCollectionResult::setPlaceForNode(ast::NodeId node, PlaceId place) {
  impl->nodePlaces.upsert(node.value, place);
}

zc::Maybe<PlaceId> BorrowPlaceCollectionResult::getPlaceForNode(ast::NodeId node) const {
  auto it = impl->nodePlaces.find(node.value);
  ZC_IF_SOME(entry, it) { return entry; }
  return zc::none;
}

size_t BorrowPlaceCollectionResult::mappedNodeCount() const { return impl->nodePlaces.size(); }

BorrowPlaceCollectionResult collectBorrowPlaces(const ast::Tree& tree, const type::TypeEnv& typeEnv,
                                                zc::Maybe<const ast::BindingMetadata&> metadata) {
  BorrowPlaceCollectionResult result;
  BorrowPlaceBuilder builder(result.getModel(), tree, typeEnv, metadata);

  auto nodes = tree.nodes();
  for (size_t i = 0; i < nodes.size(); ++i) {
    auto nodeId = ast::NodeId(static_cast<uint32_t>(i + 1));
    if (nodes[i].kind == ast::SyntaxKind::FunctionDecl) { builder.buildFunctionPlaces(nodeId); }
  }

  for (size_t i = 0; i < nodes.size(); ++i) {
    auto nodeId = ast::NodeId(static_cast<uint32_t>(i + 1));
    auto place = builder.getPlaceForNode(nodeId);
    ZC_IF_SOME(placeId, place) { result.setPlaceForNode(nodeId, placeId); }
  }

  return result;
}

BorrowCfg::BorrowCfg() : impl(zc::heap<Impl>()) {}

BorrowCfg::~BorrowCfg() noexcept(false) = default;

BorrowCfg::BorrowCfg(BorrowCfg&& other) noexcept = default;

BorrowCfg& BorrowCfg::operator=(BorrowCfg&& other) noexcept = default;

BorrowCfgNodeId BorrowCfg::addNode(BorrowCfgNodeKind kind, ast::NodeId astNode) {
  auto id = BorrowCfgNodeId(static_cast<uint32_t>(impl->nodes.size() + 1));
  impl->nodes.add(Impl::Node{kind, astNode});
  if (kind == BorrowCfgNodeKind::Entry) { impl->entry = id; }
  if (kind == BorrowCfgNodeKind::Exit) { impl->exit = id; }
  return id;
}

void BorrowCfg::addEdge(BorrowCfgNodeId from, BorrowCfgNodeId to) {
  ZC_IREQUIRE(from.isValid(), "BorrowCfg::addEdge: invalid source node");
  ZC_IREQUIRE(to.isValid(), "BorrowCfg::addEdge: invalid target node");
  impl->edges.add(BorrowCfgEdge{from, to});
}

size_t BorrowCfg::nodeCount() const { return impl->nodes.size(); }

size_t BorrowCfg::edgeCount() const { return impl->edges.size(); }

BorrowCfgNodeKind BorrowCfg::getNodeKind(BorrowCfgNodeId id) const {
  ZC_IREQUIRE(isValidIndex(id.value, impl->nodes.size()), "BorrowCfg::getNodeKind: invalid node");
  return impl->nodes[id.value - 1].kind;
}

ast::NodeId BorrowCfg::getNodeAst(BorrowCfgNodeId id) const {
  ZC_IREQUIRE(isValidIndex(id.value, impl->nodes.size()), "BorrowCfg::getNodeAst: invalid node");
  return impl->nodes[id.value - 1].astNode;
}

const BorrowCfgEdge& BorrowCfg::getEdge(size_t index) const {
  ZC_IREQUIRE(index < impl->edges.size(), "BorrowCfg::getEdge: invalid edge index");
  return impl->edges[index];
}

BorrowCfgNodeId BorrowCfg::getEntry() const { return impl->entry; }

BorrowCfgNodeId BorrowCfg::getExit() const { return impl->exit; }

namespace {

ast::NodeId unwrapStatementListItem(const ast::Tree& tree, ast::NodeId node) {
  if (!node || !tree.contains(node)) { return ast::NodeId(); }
  const auto& astNode = tree.node(node);
  if (astNode.kind == ast::SyntaxKind::StatementListItem) {
    return payloadNode(astNode, ast::kStatementListItemItemWord);
  }
  return node;
}

ast::NodeId functionBody(const ast::Tree& tree, ast::NodeId functionDecl) {
  if (!functionDecl || !tree.contains(functionDecl)) { return ast::NodeId(); }
  const auto& fnNode = tree.node(functionDecl);
  if (fnNode.kind == ast::SyntaxKind::FunctionDecl) {
    return payloadNode(fnNode, ast::kFunctionDeclBodyWord);
  }
  if (fnNode.kind == ast::SyntaxKind::FunctionExpression) {
    return payloadNode(fnNode, ast::kFunctionExpressionBodyWord);
  }
  if (fnNode.kind == ast::SyntaxKind::LambdaExpression) {
    return payloadNode(fnNode, ast::kLambdaExpressionBodyWord);
  }
  return ast::NodeId();
}

ast::NodeId lambdaExpressionBody(const ast::Tree& tree, ast::NodeId functionDecl) {
  if (!functionDecl || !tree.contains(functionDecl)) { return ast::NodeId(); }
  const auto& fnNode = tree.node(functionDecl);
  if (fnNode.kind != ast::SyntaxKind::LambdaExpression) { return ast::NodeId(); }
  return payloadNode(fnNode, ast::kLambdaExpressionExprBodyWord);
}

bool isFunctionLikeNode(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::FunctionDecl || kind == ast::SyntaxKind::FunctionExpression ||
         kind == ast::SyntaxKind::LambdaExpression;
}

zc::Maybe<PlaceId> findBindingPlaceByName(const ast::Tree& tree,
                                          const BorrowPlaceCollectionResult& places,
                                          zc::StringPtr name) {
  auto nodes = tree.nodes();
  for (size_t i = 0; i < nodes.size(); ++i) {
    auto bindingName = localPatternName(tree, nodes[i]);
    if (bindingName.size() == 0) { continue; }
    if (bindingName != name) { continue; }
    auto nodeId = ast::NodeId(static_cast<uint32_t>(i + 1));
    ZC_IF_SOME(place, places.getPlaceForNode(nodeId)) { return place; }
  }
  return zc::none;
}

zc::Maybe<PlaceId> referencedPlaceForSharedBorrow(BorrowModel& model, const ast::Tree& tree,
                                                  const BorrowPlaceCollectionResult& places,
                                                  ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return zc::none; }
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind != ast::SyntaxKind::UnaryExpression) { return zc::none; }
  auto op =
      static_cast<ast::UnaryOperatorKind>(exprNode.payload.words[ast::kUnaryExpressionOpWord]);
  if (op != ast::UnaryOperatorKind::Ref && op != ast::UnaryOperatorKind::RefMut) {
    return zc::none;
  }
  auto operand = payloadNode(exprNode, ast::kUnaryExpressionOperandWord);
  if (!operand || !tree.contains(operand)) { return zc::none; }
  const auto& operandNode = tree.node(operand);
  if (operandNode.kind == ast::SyntaxKind::IdentExpr) {
    auto name = tree.ident(ast::IdentId(operandNode.payload.words[ast::kIdentExprNameWord]));
    return findBindingPlaceByName(tree, places, name);
  }
  if (operandNode.kind == ast::SyntaxKind::MemberExpression) {
    auto object = payloadNode(operandNode, ast::kMemberExpressionObjectWord);
    if (!object || !tree.contains(object)) { return zc::none; }
    const auto& objectNode = tree.node(object);
    if (objectNode.kind != ast::SyntaxKind::IdentExpr) { return zc::none; }
    auto objectName = tree.ident(ast::IdentId(objectNode.payload.words[ast::kIdentExprNameWord]));
    ZC_IF_SOME(basePlace, findBindingPlaceByName(tree, places, objectName)) {
      auto property =
          tree.ident(ast::IdentId(operandNode.payload.words[ast::kMemberExpressionPropertyWord]));
      return model.addFieldPlace(basePlace, property);
    }
  }
  if (operandNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto operandOp =
        static_cast<ast::UnaryOperatorKind>(operandNode.payload.words[ast::kUnaryExpressionOpWord]);
    if (operandOp != ast::UnaryOperatorKind::Deref) { return zc::none; }
    auto derefOperand = payloadNode(operandNode, ast::kUnaryExpressionOperandWord);
    if (!derefOperand || !tree.contains(derefOperand)) { return zc::none; }
    const auto& derefOperandNode = tree.node(derefOperand);
    if (derefOperandNode.kind != ast::SyntaxKind::IdentExpr) { return zc::none; }
    auto name = tree.ident(ast::IdentId(derefOperandNode.payload.words[ast::kIdentExprNameWord]));
    ZC_IF_SOME(basePlace, findBindingPlaceByName(tree, places, name)) {
      return model.addDerefPlace(basePlace);
    }
  }
  if (operandNode.kind == ast::SyntaxKind::IndexExpression) {
    auto object = payloadNode(operandNode, ast::kIndexExpressionObjectWord);
    if (!object || !tree.contains(object)) { return zc::none; }
    const auto& objectNode = tree.node(object);
    if (objectNode.kind != ast::SyntaxKind::IdentExpr) { return zc::none; }
    auto objectName = tree.ident(ast::IdentId(objectNode.payload.words[ast::kIdentExprNameWord]));
    ZC_IF_SOME(basePlace, findBindingPlaceByName(tree, places, objectName)) {
      return model.addIndexPlace(basePlace, 0);
    }
  }
  return zc::none;
}

bool expressionContainsErrorPropagate(const ast::Tree& tree, ast::NodeId expr);

bool isErrorPropagateStatement(const ast::Tree& tree, ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return false; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind != ast::SyntaxKind::ExpressionStatement) { return false; }
  return expressionContainsErrorPropagate(
      tree, payloadNode(statementNode, ast::kExpressionStatementExpressionWord));
}

bool expressionContainsErrorPropagate(const ast::Tree& tree, ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return false; }
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::PostfixExpression) {
    auto op = static_cast<ast::PostfixOperatorKind>(
        exprNode.payload.words[ast::kPostfixExpressionOpWord]);
    if (op == ast::PostfixOperatorKind::ErrorPropagate) { return true; }
    return expressionContainsErrorPropagate(
        tree, payloadNode(exprNode, ast::kPostfixExpressionOperandWord));
  }
  if (exprNode.kind == ast::SyntaxKind::CallExpression) {
    if (expressionContainsErrorPropagate(tree,
                                         payloadNode(exprNode, ast::kCallExpressionCalleeWord))) {
      return true;
    }
    auto args = tree.list(
        payloadList(exprNode, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord));
    for (size_t i = 0; i < args.size(); ++i) {
      if (expressionContainsErrorPropagate(tree, args[i])) { return true; }
    }
  }
  if (exprNode.kind == ast::SyntaxKind::BinaryExpr) {
    return expressionContainsErrorPropagate(tree, payloadNode(exprNode, ast::kBinaryExprLhsWord)) ||
           expressionContainsErrorPropagate(tree, payloadNode(exprNode, ast::kBinaryExprRhsWord));
  }
  if (exprNode.kind == ast::SyntaxKind::AssignmentExpr) {
    return expressionContainsErrorPropagate(tree,
                                            payloadNode(exprNode, ast::kAssignmentExprLhsWord)) ||
           expressionContainsErrorPropagate(tree,
                                            payloadNode(exprNode, ast::kAssignmentExprRhsWord));
  }
  return false;
}

bool letStatementContainsErrorPropagate(const ast::Tree& tree, ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return false; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind != ast::SyntaxKind::LetStmt) { return false; }
  auto declListId = payloadNode(statementNode, ast::kLetStmtDeclarationsWord);
  if (!declListId || !tree.contains(declListId)) { return false; }
  const auto& declListNode = tree.node(declListId);
  if (declListNode.kind != ast::SyntaxKind::VariableDeclaratorList) { return false; }
  auto decls = tree.list(payloadList(declListNode, ast::kVariableDeclaratorListDeclsFirstWord,
                                     ast::kVariableDeclaratorListDeclsSizeWord));
  for (size_t i = 0; i < decls.size(); ++i) {
    if (!decls[i] || !tree.contains(decls[i])) { continue; }
    const auto& declNode = tree.node(decls[i]);
    if (declNode.kind != ast::SyntaxKind::VariableDeclarator) { continue; }
    auto init = payloadNode(declNode, ast::kVariableDeclaratorInitWord);
    if (expressionContainsErrorPropagate(tree, init)) { return true; }
  }
  return false;
}

struct CfgTail final {
  BorrowCfgNodeId node;
  bool fallsThrough = false;
};

struct LoopTarget final {
  BorrowCfgNodeId continueTarget;
  BorrowCfgNodeId breakTarget;
};

using LoopTargetStack = zc::Vector<LoopTarget>;

void addSequentialEdge(BorrowCfg& cfg, CfgTail tail, BorrowCfgNodeId next) {
  if (tail.fallsThrough) { cfg.addEdge(tail.node, next); }
}

CfgTail buildStatementCfg(BorrowCfg& cfg, const ast::Tree& tree, ast::NodeId statement,
                          BorrowCfgNodeId predecessor, LoopTargetStack& loopTargets);

CfgTail buildBlockCfg(BorrowCfg& cfg, const ast::Tree& tree, ast::NodeId block,
                      BorrowCfgNodeId predecessor, LoopTargetStack& loopTargets) {
  CfgTail tail{predecessor, true};
  if (!block || !tree.contains(block)) { return tail; }
  const auto& blockNode = tree.node(block);
  if (blockNode.kind != ast::SyntaxKind::BlockStmt) {
    return buildStatementCfg(cfg, tree, block, predecessor, loopTargets);
  }

  auto statements = tree.list(
      payloadList(blockNode, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord));
  for (size_t i = 0; i < statements.size(); ++i) {
    if (!tail.fallsThrough) { break; }
    auto statement = unwrapStatementListItem(tree, statements[i]);
    if (!statement || !tree.contains(statement)) { continue; }
    tail = buildStatementCfg(cfg, tree, statement, tail.node, loopTargets);
  }
  return tail;
}

CfgTail buildIfCfg(BorrowCfg& cfg, const ast::Tree& tree, ast::NodeId ifStmt,
                   BorrowCfgNodeId predecessor, LoopTargetStack& loopTargets) {
  const auto& ifNode = tree.node(ifStmt);
  auto branch = cfg.addNode(BorrowCfgNodeKind::Branch, ifStmt);
  cfg.addEdge(predecessor, branch);
  auto join = cfg.addNode(BorrowCfgNodeKind::Join);

  auto thenStmt = payloadNode(ifNode, ast::kIfStmtThenStmtWord);
  auto thenTail = buildStatementCfg(cfg, tree, thenStmt, branch, loopTargets);
  if (thenTail.fallsThrough) { cfg.addEdge(thenTail.node, join); }

  auto elseStmt = payloadNode(ifNode, ast::kIfStmtElseStmtWord);
  if (elseStmt && tree.contains(elseStmt)) {
    auto elseTail = buildStatementCfg(cfg, tree, elseStmt, branch, loopTargets);
    if (elseTail.fallsThrough) { cfg.addEdge(elseTail.node, join); }
  } else {
    cfg.addEdge(branch, join);
  }

  return CfgTail{join, true};
}

CfgTail buildWhileCfg(BorrowCfg& cfg, const ast::Tree& tree, ast::NodeId whileStmt,
                      BorrowCfgNodeId predecessor, LoopTargetStack& loopTargets) {
  const auto& whileNode = tree.node(whileStmt);
  auto branch = cfg.addNode(BorrowCfgNodeKind::Branch, whileStmt);
  cfg.addEdge(predecessor, branch);
  auto join = cfg.addNode(BorrowCfgNodeKind::Join);
  cfg.addEdge(branch, join);

  auto body = payloadNode(whileNode, ast::kWhileStmtBodyWord);
  loopTargets.add(LoopTarget{branch, join});
  auto bodyTail = buildStatementCfg(cfg, tree, body, branch, loopTargets);
  loopTargets.removeLast();
  if (bodyTail.fallsThrough) { cfg.addEdge(bodyTail.node, branch); }

  return CfgTail{join, true};
}

CfgTail buildMatchCfg(BorrowCfg& cfg, const ast::Tree& tree, ast::NodeId matchStmt,
                      BorrowCfgNodeId predecessor, LoopTargetStack& loopTargets) {
  const auto& matchNode = tree.node(matchStmt);
  auto branch = cfg.addNode(BorrowCfgNodeKind::Branch, matchStmt);
  cfg.addEdge(predecessor, branch);
  auto join = cfg.addNode(BorrowCfgNodeKind::Join);

  auto arms =
      tree.list(payloadList(matchNode, ast::kMatchStmtArmsFirstWord, ast::kMatchStmtArmsSizeWord));
  if (arms.size() == 0) { cfg.addEdge(branch, join); }

  for (size_t i = 0; i < arms.size(); ++i) {
    if (!arms[i] || !tree.contains(arms[i])) { continue; }
    const auto& armNode = tree.node(arms[i]);
    if (armNode.kind != ast::SyntaxKind::MatchArmStmt) { continue; }
    auto armBody = payloadNode(armNode, ast::kMatchArmStmtBodyWord);
    auto armTail = buildStatementCfg(cfg, tree, armBody, branch, loopTargets);
    if (armTail.fallsThrough) { cfg.addEdge(armTail.node, join); }
  }

  return CfgTail{join, true};
}

CfgTail buildStatementCfg(BorrowCfg& cfg, const ast::Tree& tree, ast::NodeId statement,
                          BorrowCfgNodeId predecessor, LoopTargetStack& loopTargets) {
  if (!statement || !tree.contains(statement)) { return CfgTail{predecessor, true}; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind == ast::SyntaxKind::IfStmt) {
    return buildIfCfg(cfg, tree, statement, predecessor, loopTargets);
  }
  if (statementNode.kind == ast::SyntaxKind::WhileStmt) {
    return buildWhileCfg(cfg, tree, statement, predecessor, loopTargets);
  }
  if (statementNode.kind == ast::SyntaxKind::MatchStmt) {
    return buildMatchCfg(cfg, tree, statement, predecessor, loopTargets);
  }
  if (statementNode.kind == ast::SyntaxKind::BlockStmt) {
    return buildBlockCfg(cfg, tree, statement, predecessor, loopTargets);
  }

  auto kind = statementNode.kind == ast::SyntaxKind::ReturnStmt ? BorrowCfgNodeKind::Return
                                                                : BorrowCfgNodeKind::Statement;
  auto cfgNode = cfg.addNode(kind, statement);
  cfg.addEdge(predecessor, cfgNode);
  if (kind == BorrowCfgNodeKind::Return) {
    cfg.addEdge(cfgNode, cfg.getExit());
    return CfgTail{cfgNode, false};
  }
  if (statementNode.kind == ast::SyntaxKind::BreakStmt) {
    if (!loopTargets.empty()) {
      cfg.addEdge(cfgNode, loopTargets.back().breakTarget);
    } else {
      cfg.addEdge(cfgNode, cfg.getExit());
    }
    return CfgTail{cfgNode, false};
  }
  if (statementNode.kind == ast::SyntaxKind::ContinueStatement) {
    if (!loopTargets.empty()) {
      cfg.addEdge(cfgNode, loopTargets.back().continueTarget);
    } else {
      cfg.addEdge(cfgNode, cfg.getExit());
    }
    return CfgTail{cfgNode, false};
  }
  if (isErrorPropagateStatement(tree, statement) ||
      letStatementContainsErrorPropagate(tree, statement)) {
    cfg.addEdge(cfgNode, cfg.getExit());
  }
  return CfgTail{cfgNode, true};
}

}  // namespace

BorrowCfg buildStraightLineBorrowCfg(const ast::Tree& tree, ast::NodeId functionDecl) {
  BorrowCfg cfg;
  auto entry = cfg.addNode(BorrowCfgNodeKind::Entry);
  auto exit = cfg.addNode(BorrowCfgNodeKind::Exit);

  auto body = functionBody(tree, functionDecl);
  if (!body || !tree.contains(body)) {
    auto exprBody = lambdaExpressionBody(tree, functionDecl);
    if (exprBody && tree.contains(exprBody)) {
      auto statement = cfg.addNode(BorrowCfgNodeKind::Statement, exprBody);
      cfg.addEdge(entry, statement);
      cfg.addEdge(statement, exit);
      return cfg;
    }
    cfg.addEdge(entry, exit);
    return cfg;
  }

  LoopTargetStack loopTargets;
  auto tail = buildBlockCfg(cfg, tree, body, entry, loopTargets);
  addSequentialEdge(cfg, tail, exit);
  return cfg;
}

BorrowCheckerResult::BorrowCheckerResult() : impl(zc::heap<Impl>()) {}

BorrowCheckerResult::~BorrowCheckerResult() noexcept(false) = default;

BorrowCheckerResult::BorrowCheckerResult(BorrowCheckerResult&& other) noexcept = default;

BorrowCheckerResult& BorrowCheckerResult::operator=(BorrowCheckerResult&& other) noexcept = default;

BorrowPlaceCollectionResult& BorrowCheckerResult::getPlaces() { return impl->places; }

const BorrowPlaceCollectionResult& BorrowCheckerResult::getPlaces() const { return impl->places; }

void BorrowCheckerResult::setPlaces(BorrowPlaceCollectionResult places) {
  impl->places = zc::mv(places);
}

void BorrowCheckerResult::addFunctionSummary(ast::NodeId functionDecl, BorrowCfg cfg) {
  Impl::FunctionSummary summary;
  summary.functionDecl = functionDecl;
  summary.cfg = zc::mv(cfg);
  impl->functions.add(zc::mv(summary));
}

size_t BorrowCheckerResult::functionCount() const { return impl->functions.size(); }

ast::NodeId BorrowCheckerResult::getFunctionDecl(size_t index) const {
  ZC_IREQUIRE(index < impl->functions.size(), "BorrowCheckerResult::getFunctionDecl: bad index");
  return impl->functions[index].functionDecl;
}

const BorrowCfg& BorrowCheckerResult::getFunctionCfg(size_t index) const {
  ZC_IREQUIRE(index < impl->functions.size(), "BorrowCheckerResult::getFunctionCfg: bad index");
  return impl->functions[index].cfg;
}

void BorrowCheckerResult::addFunctionMoveFact(size_t index, BorrowCfgNodeId node, PlaceId place) {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::addFunctionMoveFact: bad index");
  impl->functions[index].moves.add(BorrowMoveFact{node, place});
}

void BorrowCheckerResult::addFunctionReinitializeFact(size_t index, BorrowCfgNodeId node,
                                                      PlaceId place) {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::addFunctionReinitializeFact: bad index");
  impl->functions[index].reinitializes.add(BorrowMoveFact{node, place});
}

namespace {

BorrowMoveState buildMoveStateFromFacts(zc::ArrayPtr<const BorrowMoveFact> moveFacts,
                                        zc::ArrayPtr<const BorrowMoveFact> reinitializeFacts,
                                        const BorrowCfg& cfg) {
  BorrowMoveState moves(cfg);
  for (auto fact : moveFacts) { moves.addMove(fact.node, fact.place); }
  for (auto fact : reinitializeFacts) { moves.addReinitialize(fact.node, fact.place); }
  moves.propagate();
  return moves;
}

}  // namespace

bool BorrowCheckerResult::isFunctionPlaceMovedAt(size_t index, BorrowCfgNodeId node,
                                                 PlaceId place) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::isFunctionPlaceMovedAt: bad index");
  const auto& fn = impl->functions[index];
  auto moves = buildMoveStateFromFacts(fn.moves.asPtr(), fn.reinitializes.asPtr(), fn.cfg);
  return moves.isMovedAt(node, place);
}

zc::Maybe<BorrowCfgNodeId> BorrowCheckerResult::getFunctionMoveOriginBefore(size_t index,
                                                                            BorrowCfgNodeId node,
                                                                            PlaceId place) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionMoveOriginBefore: bad index");
  const auto& fn = impl->functions[index];
  auto moves = buildMoveStateFromFacts(fn.moves.asPtr(), fn.reinitializes.asPtr(), fn.cfg);
  return moves.getMoveOriginBefore(node, place);
}

zc::Maybe<BorrowCfgNodeId> BorrowCheckerResult::getFunctionOverlappingMoveOriginBefore(
    size_t index, BorrowCfgNodeId node, PlaceId place) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionOverlappingMoveOriginBefore: bad index");
  const auto& fn = impl->functions[index];
  auto moves = buildMoveStateFromFacts(fn.moves.asPtr(), fn.reinitializes.asPtr(), fn.cfg);
  const auto& model = impl->places.getModel();

  ZC_IF_SOME(requested, model.getPlace(place)) {
    for (size_t i = 1; i <= model.placeCount(); ++i) {
      auto candidateId = PlaceId(static_cast<uint32_t>(i));
      ZC_IF_SOME(candidate, model.getPlace(candidateId)) {
        if (!placesOverlap(requested, candidate, FieldOverlapMode::Conservative)) { continue; }
        ZC_IF_SOME(origin, moves.getMoveOriginBefore(node, candidateId)) { return origin; }
      }
    }
  }
  return zc::none;
}

zc::Maybe<BorrowCfgNodeId> BorrowCheckerResult::getFunctionMoveOrigin(size_t index,
                                                                      BorrowCfgNodeId node,
                                                                      PlaceId place) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionMoveOrigin: bad index");
  const auto& fn = impl->functions[index];
  auto moves = buildMoveStateFromFacts(fn.moves.asPtr(), fn.reinitializes.asPtr(), fn.cfg);
  return moves.getMoveOrigin(node, place);
}

void BorrowCheckerResult::addFunctionUseAfterMoveReport(size_t index,
                                                        BorrowUseAfterMoveReport report) {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::addFunctionUseAfterMoveReport: bad index");
  impl->functions[index].useAfterMoveReports.add(report);
}

size_t BorrowCheckerResult::functionUseAfterMoveReportCount(size_t index) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::functionUseAfterMoveReportCount: bad index");
  return impl->functions[index].useAfterMoveReports.size();
}

const BorrowUseAfterMoveReport& BorrowCheckerResult::getFunctionUseAfterMoveReport(
    size_t index, size_t reportIndex) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionUseAfterMoveReport: bad index");
  ZC_IREQUIRE(reportIndex < impl->functions[index].useAfterMoveReports.size(),
              "BorrowCheckerResult::getFunctionUseAfterMoveReport: bad report index");
  return impl->functions[index].useAfterMoveReports[reportIndex];
}

void BorrowCheckerResult::addFunctionMoveOutOfBorrowReport(size_t index,
                                                           BorrowMoveOutOfBorrowReport report) {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::addFunctionMoveOutOfBorrowReport: bad index");
  impl->functions[index].moveOutOfBorrowReports.add(report);
}

size_t BorrowCheckerResult::functionMoveOutOfBorrowReportCount(size_t index) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::functionMoveOutOfBorrowReportCount: bad index");
  return impl->functions[index].moveOutOfBorrowReports.size();
}

const BorrowMoveOutOfBorrowReport& BorrowCheckerResult::getFunctionMoveOutOfBorrowReport(
    size_t index, size_t reportIndex) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionMoveOutOfBorrowReport: bad index");
  ZC_IREQUIRE(reportIndex < impl->functions[index].moveOutOfBorrowReports.size(),
              "BorrowCheckerResult::getFunctionMoveOutOfBorrowReport: bad report index");
  return impl->functions[index].moveOutOfBorrowReports[reportIndex];
}

void BorrowCheckerResult::addFunctionBorrowConflictReport(size_t index,
                                                          BorrowConflictReport report) {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::addFunctionBorrowConflictReport: bad index");
  impl->functions[index].borrowConflictReports.add(report);
}

size_t BorrowCheckerResult::functionBorrowConflictReportCount(size_t index) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::functionBorrowConflictReportCount: bad index");
  return impl->functions[index].borrowConflictReports.size();
}

const BorrowConflictReport& BorrowCheckerResult::getFunctionBorrowConflictReport(
    size_t index, size_t reportIndex) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionBorrowConflictReport: bad index");
  ZC_IREQUIRE(reportIndex < impl->functions[index].borrowConflictReports.size(),
              "BorrowCheckerResult::getFunctionBorrowConflictReport: bad report index");
  return impl->functions[index].borrowConflictReports[reportIndex];
}

void BorrowCheckerResult::addFunctionRegionEscapeReport(size_t index,
                                                        BorrowRegionEscapeReport report) {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::addFunctionRegionEscapeReport: bad index");
  impl->functions[index].regionEscapeReports.add(report);
}

size_t BorrowCheckerResult::functionRegionEscapeReportCount(size_t index) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::functionRegionEscapeReportCount: bad index");
  return impl->functions[index].regionEscapeReports.size();
}

const BorrowRegionEscapeReport& BorrowCheckerResult::getFunctionRegionEscapeReport(
    size_t index, size_t reportIndex) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionRegionEscapeReport: bad index");
  ZC_IREQUIRE(reportIndex < impl->functions[index].regionEscapeReports.size(),
              "BorrowCheckerResult::getFunctionRegionEscapeReport: bad report index");
  return impl->functions[index].regionEscapeReports[reportIndex];
}

void BorrowCheckerResult::addFunctionRawPointerBoundaryReport(
    size_t index, BorrowRawPointerBoundaryReport report) {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::addFunctionRawPointerBoundaryReport: bad index");
  impl->functions[index].rawPointerBoundaryReports.add(report);
}

size_t BorrowCheckerResult::functionRawPointerBoundaryReportCount(size_t index) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::functionRawPointerBoundaryReportCount: bad index");
  return impl->functions[index].rawPointerBoundaryReports.size();
}

const BorrowRawPointerBoundaryReport& BorrowCheckerResult::getFunctionRawPointerBoundaryReport(
    size_t index, size_t reportIndex) const {
  ZC_IREQUIRE(index < impl->functions.size(),
              "BorrowCheckerResult::getFunctionRawPointerBoundaryReport: bad index");
  ZC_IREQUIRE(reportIndex < impl->functions[index].rawPointerBoundaryReports.size(),
              "BorrowCheckerResult::getFunctionRawPointerBoundaryReport: bad report index");
  return impl->functions[index].rawPointerBoundaryReports[reportIndex];
}

namespace {

bool isImplicitlyCopyableBorrowType(const type::Type& ty) {
  switch (ty.getKind()) {
    case type::TypeKind::Primitive:
    case type::TypeKind::Reference:
    case type::TypeKind::RawPointer:
    case type::TypeKind::Function:
      return true;
    case type::TypeKind::Tuple:
    case type::TypeKind::Object:
    case type::TypeKind::Array:
    case type::TypeKind::Named:
    case type::TypeKind::TypeVar:
    case type::TypeKind::Error:
    case type::TypeKind::Interface:
    case type::TypeKind::Union:
    case type::TypeKind::Intersection:
    case type::TypeKind::Existential:
    case type::TypeKind::Associated:
      return false;
  }
  return false;
}

bool shouldMoveExpression(const type::TypeEnv& typeEnv, ast::NodeId expr) {
  if (!expr || !typeEnv.hasType(expr)) { return false; }
  return !isImplicitlyCopyableBorrowType(typeEnv.getType(expr));
}

zc::Maybe<PlaceId> placeForBorrowExpression(const ast::Tree& tree,
                                            const BorrowPlaceCollectionResult& places,
                                            ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return zc::none; }
  ZC_IF_SOME(place, places.getPlaceForNode(expr)) { return place; }

  const auto& exprNode = tree.node(expr);
  if (exprNode.kind != ast::SyntaxKind::IdentExpr) { return zc::none; }
  auto name = tree.ident(ast::IdentId(exprNode.payload.words[ast::kIdentExprNameWord]));
  return findBindingPlaceByName(tree, places, name);
}

bool isConsumablePlaceExpression(const ast::Tree& tree, ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return false; }
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::IdentExpr) { return true; }
  if (exprNode.kind == ast::SyntaxKind::MemberExpression) { return true; }
  if (exprNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto op =
        static_cast<ast::UnaryOperatorKind>(exprNode.payload.words[ast::kUnaryExpressionOpWord]);
    return op == ast::UnaryOperatorKind::Deref;
  }
  if (exprNode.kind == ast::SyntaxKind::IndexExpression) { return true; }
  return false;
}

bool shouldTraverseUnaryOperandForMoveUse(const ast::Tree& tree, ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return false; }
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind != ast::SyntaxKind::UnaryExpression) { return false; }
  auto op =
      static_cast<ast::UnaryOperatorKind>(exprNode.payload.words[ast::kUnaryExpressionOpWord]);
  return op != ast::UnaryOperatorKind::Ref && op != ast::UnaryOperatorKind::RefMut;
}

void addMoveFactForExpression(BorrowCheckerResult& result, size_t functionIndex,
                              BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                              const type::TypeEnv& typeEnv, ast::NodeId expr);

void addMoveFactsForExpressionList(BorrowCheckerResult& result, size_t functionIndex,
                                   BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                   const type::TypeEnv& typeEnv, ast::NodeList expressions) {
  auto exprs = tree.list(expressions);
  for (size_t i = 0; i < exprs.size(); ++i) {
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv, exprs[i]);
  }
}

void addMoveFactForPlaceExpression(BorrowCheckerResult& result, size_t functionIndex,
                                   BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                   const type::TypeEnv& typeEnv, ast::NodeId expr) {
  if (!shouldMoveExpression(typeEnv, expr)) { return; }
  ZC_IF_SOME(place, placeForBorrowExpression(tree, result.getPlaces(), expr)) {
    result.addFunctionMoveFact(functionIndex, cfgNode, place);
  }
}

void addMoveFactForExpression(BorrowCheckerResult& result, size_t functionIndex,
                              BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                              const type::TypeEnv& typeEnv, ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return; }
  if (isConsumablePlaceExpression(tree, expr)) {
    addMoveFactForPlaceExpression(result, functionIndex, cfgNode, tree, typeEnv, expr);
  }

  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::CallExpression) {
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                             payloadNode(exprNode, ast::kCallExpressionCalleeWord));
    addMoveFactsForExpressionList(
        result, functionIndex, cfgNode, tree, typeEnv,
        payloadList(exprNode, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord));
  }
  if (exprNode.kind == ast::SyntaxKind::BinaryExpr) {
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                             payloadNode(exprNode, ast::kBinaryExprLhsWord));
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                             payloadNode(exprNode, ast::kBinaryExprRhsWord));
  }
  if (exprNode.kind == ast::SyntaxKind::IndexExpression) {
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                             payloadNode(exprNode, ast::kIndexExpressionObjectWord));
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                             payloadNode(exprNode, ast::kIndexExpressionIndexWord));
  }
  if (exprNode.kind == ast::SyntaxKind::UnaryExpression) {
    if (shouldTraverseUnaryOperandForMoveUse(tree, expr)) {
      addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                               payloadNode(exprNode, ast::kUnaryExpressionOperandWord));
    }
  }
  if (exprNode.kind == ast::SyntaxKind::AssignmentExpr) {
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                             payloadNode(exprNode, ast::kAssignmentExprRhsWord));
  }
}

void inferMoveFactsFromLet(BorrowCheckerResult& result, size_t functionIndex,
                           BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                           const type::TypeEnv& typeEnv, ast::NodeId letStmt) {
  if (!letStmt || !tree.contains(letStmt)) { return; }
  const auto& letNode = tree.node(letStmt);
  if (letNode.kind != ast::SyntaxKind::LetStmt) { return; }
  auto declListId = payloadNode(letNode, ast::kLetStmtDeclarationsWord);
  if (!declListId || !tree.contains(declListId)) { return; }
  const auto& declListNode = tree.node(declListId);
  if (declListNode.kind != ast::SyntaxKind::VariableDeclaratorList) { return; }
  auto decls = tree.list(payloadList(declListNode, ast::kVariableDeclaratorListDeclsFirstWord,
                                     ast::kVariableDeclaratorListDeclsSizeWord));
  for (size_t i = 0; i < decls.size(); ++i) {
    if (!decls[i] || !tree.contains(decls[i])) { continue; }
    const auto& declNode = tree.node(decls[i]);
    if (declNode.kind != ast::SyntaxKind::VariableDeclarator) { continue; }
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                             payloadNode(declNode, ast::kVariableDeclaratorInitWord));
  }
}

void inferMoveFactsFromAssignment(BorrowCheckerResult& result, size_t functionIndex,
                                  BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                  const type::TypeEnv& typeEnv, ast::NodeId assignment) {
  if (!assignment || !tree.contains(assignment)) { return; }
  const auto& assignmentNode = tree.node(assignment);
  if (assignmentNode.kind != ast::SyntaxKind::AssignmentExpr) { return; }
  auto op = static_cast<ast::AssignmentOperatorKind>(
      assignmentNode.payload.words[ast::kAssignmentExprOpWord]);
  if (op != ast::AssignmentOperatorKind::Assign) { return; }

  auto lhs = payloadNode(assignmentNode, ast::kAssignmentExprLhsWord);
  ZC_IF_SOME(place, placeForBorrowExpression(tree, result.getPlaces(), lhs)) {
    result.addFunctionReinitializeFact(functionIndex, cfgNode, place);
  }

  addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                           payloadNode(assignmentNode, ast::kAssignmentExprRhsWord));
}

void inferMoveFactsFromExpressionStatement(BorrowCheckerResult& result, size_t functionIndex,
                                           BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                           const type::TypeEnv& typeEnv, ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind != ast::SyntaxKind::ExpressionStatement) { return; }
  auto expr = payloadNode(statementNode, ast::kExpressionStatementExpressionWord);
  inferMoveFactsFromAssignment(result, functionIndex, cfgNode, tree, typeEnv, expr);
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind != ast::SyntaxKind::AssignmentExpr) {
    addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv, expr);
  }
}

void inferMoveFactsFromReturnStatement(BorrowCheckerResult& result, size_t functionIndex,
                                       BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                       const type::TypeEnv& typeEnv, ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind != ast::SyntaxKind::ReturnStmt) { return; }
  addMoveFactForExpression(result, functionIndex, cfgNode, tree, typeEnv,
                           payloadNode(statementNode, ast::kReturnStmtValueWord));
}

void inferFunctionMoveFacts(BorrowCheckerResult& result, size_t functionIndex,
                            const ast::Tree& tree, const type::TypeEnv& typeEnv) {
  const auto& cfg = result.getFunctionCfg(functionIndex);
  for (size_t i = 1; i <= cfg.nodeCount(); ++i) {
    auto cfgNode = BorrowCfgNodeId(static_cast<uint32_t>(i));
    auto astNode = cfg.getNodeAst(cfgNode);
    inferMoveFactsFromLet(result, functionIndex, cfgNode, tree, typeEnv, astNode);
    inferMoveFactsFromExpressionStatement(result, functionIndex, cfgNode, tree, typeEnv, astNode);
    inferMoveFactsFromReturnStatement(result, functionIndex, cfgNode, tree, typeEnv, astNode);
  }
}

void addUseAfterMoveReportForExpression(BorrowCheckerResult& result, size_t functionIndex,
                                        BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                        ast::NodeId expr);

void addUseAfterMoveReportForExpressionList(BorrowCheckerResult& result, size_t functionIndex,
                                            BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                            ast::NodeList expressions) {
  auto exprs = tree.list(expressions);
  for (size_t i = 0; i < exprs.size(); ++i) {
    addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree, exprs[i]);
  }
}

void addUseAfterMoveReportForPlaceExpression(BorrowCheckerResult& result, size_t functionIndex,
                                             BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                             ast::NodeId expr) {
  ZC_IF_SOME(place, placeForBorrowExpression(tree, result.getPlaces(), expr)) {
    ZC_IF_SOME(origin,
               result.getFunctionOverlappingMoveOriginBefore(functionIndex, cfgNode, place)) {
      result.addFunctionUseAfterMoveReport(functionIndex,
                                           BorrowUseAfterMoveReport{cfgNode, place, origin});
    }
  }
}

void addUseAfterMoveReportForExpression(BorrowCheckerResult& result, size_t functionIndex,
                                        BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                        ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return; }
  if (isConsumablePlaceExpression(tree, expr)) {
    addUseAfterMoveReportForPlaceExpression(result, functionIndex, cfgNode, tree, expr);
  }

  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::CallExpression) {
    addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                       payloadNode(exprNode, ast::kCallExpressionCalleeWord));
    addUseAfterMoveReportForExpressionList(
        result, functionIndex, cfgNode, tree,
        payloadList(exprNode, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord));
  }
  if (exprNode.kind == ast::SyntaxKind::BinaryExpr) {
    addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                       payloadNode(exprNode, ast::kBinaryExprLhsWord));
    addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                       payloadNode(exprNode, ast::kBinaryExprRhsWord));
  }
  if (exprNode.kind == ast::SyntaxKind::IndexExpression) {
    addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                       payloadNode(exprNode, ast::kIndexExpressionObjectWord));
    addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                       payloadNode(exprNode, ast::kIndexExpressionIndexWord));
  }
  if (exprNode.kind == ast::SyntaxKind::UnaryExpression) {
    if (shouldTraverseUnaryOperandForMoveUse(tree, expr)) {
      addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                         payloadNode(exprNode, ast::kUnaryExpressionOperandWord));
    }
  }
  if (exprNode.kind == ast::SyntaxKind::AssignmentExpr) {
    addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                       payloadNode(exprNode, ast::kAssignmentExprRhsWord));
  }
}

void inferUseAfterMoveFromExpressionStatement(BorrowCheckerResult& result, size_t functionIndex,
                                              BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                              ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind != ast::SyntaxKind::ExpressionStatement) { return; }
  auto expr = payloadNode(statementNode, ast::kExpressionStatementExpressionWord);
  addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree, expr);
}

void inferUseAfterMoveFromReturnStatement(BorrowCheckerResult& result, size_t functionIndex,
                                          BorrowCfgNodeId cfgNode, const ast::Tree& tree,
                                          ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind != ast::SyntaxKind::ReturnStmt) { return; }
  addUseAfterMoveReportForExpression(result, functionIndex, cfgNode, tree,
                                     payloadNode(statementNode, ast::kReturnStmtValueWord));
}

void inferFunctionUseAfterMoveReports(BorrowCheckerResult& result, size_t functionIndex,
                                      const ast::Tree& tree) {
  const auto& cfg = result.getFunctionCfg(functionIndex);
  for (size_t i = 1; i <= cfg.nodeCount(); ++i) {
    auto cfgNode = BorrowCfgNodeId(static_cast<uint32_t>(i));
    auto astNode = cfg.getNodeAst(cfgNode);
    inferUseAfterMoveFromExpressionStatement(result, functionIndex, cfgNode, tree, astNode);
    inferUseAfterMoveFromReturnStatement(result, functionIndex, cfgNode, tree, astNode);
  }
}

bool subtreeContainsNode(const ast::Tree& tree, ast::NodeId root, ast::NodeId target) {
  if (!root || !target || !tree.contains(root)) { return false; }
  bool found = false;
  ast::visitTreePreOrder(tree, root, [&](ast::NodeId nodeId, const ast::Node&) {
    if (nodeId == target) { found = true; }
  });
  return found;
}

zc::Maybe<BorrowCfgNodeId> cfgNodeForOrigin(const BorrowCfg& cfg, const ast::Tree& tree,
                                            ast::NodeId origin) {
  for (size_t i = 1; i <= cfg.nodeCount(); ++i) {
    auto cfgNode = BorrowCfgNodeId(static_cast<uint32_t>(i));
    auto astNode = cfg.getNodeAst(cfgNode);
    if (!astNode || !tree.contains(astNode)) { continue; }
    if (astNode == origin || subtreeContainsNode(tree, astNode, origin)) { return cfgNode; }
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> nearestEnclosingBlock(const ast::Tree& tree, ast::NodeId node) {
  if (!node || !tree.contains(node)) { return zc::none; }
  zc::Maybe<ast::NodeId> best = zc::none;
  size_t bestSize = 0;
  auto nodes = tree.nodes();
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].kind != ast::SyntaxKind::BlockStmt) { continue; }
    auto candidate = ast::NodeId(static_cast<uint32_t>(i + 1));
    if (!subtreeContainsNode(tree, candidate, node)) { continue; }
    size_t size = 0;
    ast::visitTreePreOrder(tree, candidate, [&](ast::NodeId, const ast::Node&) { ++size; });
    if (best == zc::none || size < bestSize) {
      best = candidate;
      bestSize = size;
    }
  }
  return best;
}

zc::Maybe<ast::NodeId> nearestEnclosingCall(const ast::Tree& tree, ast::NodeId node) {
  if (!node || !tree.contains(node)) { return zc::none; }
  zc::Maybe<ast::NodeId> best = zc::none;
  size_t bestSize = 0;
  auto nodes = tree.nodes();
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].kind != ast::SyntaxKind::CallExpression) { continue; }
    auto candidate = ast::NodeId(static_cast<uint32_t>(i + 1));
    if (!subtreeContainsNode(tree, candidate, node)) { continue; }
    size_t size = 0;
    ast::visitTreePreOrder(tree, candidate, [&](ast::NodeId, const ast::Node&) { ++size; });
    if (best == zc::none || size < bestSize) {
      best = candidate;
      bestSize = size;
    }
  }
  return best;
}

bool loanIsInScopeAtNode(const BorrowCfg& cfg, const ast::Tree& tree, const Loan& loan,
                         BorrowCfgNodeId node) {
  auto astNode = cfg.getNodeAst(node);
  if (!astNode || !tree.contains(astNode)) { return false; }
  ZC_IF_SOME(call, nearestEnclosingCall(tree, loan.origin)) {
    ZC_IF_SOME(callNode, cfgNodeForOrigin(cfg, tree, call)) { return callNode == node; }
    return false;
  }
  ZC_IF_SOME(block, nearestEnclosingBlock(tree, loan.origin)) {
    return subtreeContainsNode(tree, block, astNode);
  }
  return true;
}

void inferFunctionBorrowConflictReports(BorrowCheckerResult& result, size_t functionIndex,
                                        const ast::Tree& tree) {
  const BorrowCfg& cfg = result.getFunctionCfg(functionIndex);
  const BorrowModel& model = result.getPlaces().getModel();
  zc::Vector<LoanId> activeLoans;

  for (size_t i = 1; i <= model.loanCount(); ++i) {
    auto currentId = LoanId(static_cast<uint32_t>(i));
    auto currentMaybe = model.getLoan(currentId);
    if (currentMaybe == zc::none) { continue; }
    ZC_IF_SOME(current, currentMaybe) {
      auto currentNodeMaybe = cfgNodeForOrigin(cfg, tree, current.origin);
      if (currentNodeMaybe == zc::none) { continue; }

      ZC_IF_SOME(requestedPlace, model.getPlace(current.place)) {
        for (auto existingId : activeLoans) {
          auto existingMaybe = model.getLoan(existingId);
          if (existingMaybe == zc::none) { continue; }
          ZC_IF_SOME(existing, existingMaybe) {
            auto existingNode = cfgNodeForOrigin(cfg, tree, existing.origin);
            if (existingNode == zc::none) { continue; }
            ZC_IF_SOME(currentNode, currentNodeMaybe) {
              if (!loanIsInScopeAtNode(cfg, tree, existing, currentNode)) { continue; }
            }
            ZC_IF_SOME(existingPlace, model.getPlace(existing.place)) {
              if (!placesOverlap(requestedPlace, existingPlace, FieldOverlapMode::ProvenDisjoint)) {
                continue;
              }
              if (current.kind != LoanKind::Mutable && existing.kind != LoanKind::Mutable) {
                continue;
              }
              ZC_IF_SOME(currentNode, currentNodeMaybe) {
                result.addFunctionBorrowConflictReport(
                    functionIndex, BorrowConflictReport{currentNode, current.place, current.kind,
                                                        existingId, existing.place, existing.kind,
                                                        existing.region, existing.origin});
                break;
              }
            }
          }
        }
      }

      ZC_IF_SOME(currentNode, currentNodeMaybe) {
        if (loanIsInScopeAtNode(cfg, tree, current, currentNode)) { activeLoans.add(currentId); }
      }
    }
  }
}

void inferFunctionMoveOutOfBorrowReports(BorrowCheckerResult& result, size_t functionIndex,
                                         const ast::Tree& tree) {
  const BorrowCfg& cfg = result.getFunctionCfg(functionIndex);
  const BorrowModel& model = result.getPlaces().getModel();
  zc::Vector<LoanId> activeLoans;

  for (size_t nodeIndex = 1; nodeIndex <= cfg.nodeCount(); ++nodeIndex) {
    auto cfgNode = BorrowCfgNodeId(static_cast<uint32_t>(nodeIndex));

    for (size_t loanIndex = 1; loanIndex <= model.loanCount(); ++loanIndex) {
      auto loanId = LoanId(static_cast<uint32_t>(loanIndex));
      auto loanMaybe = model.getLoan(loanId);
      if (loanMaybe == zc::none) { continue; }
      ZC_IF_SOME(loan, loanMaybe) {
        auto originNode = cfgNodeForOrigin(cfg, tree, loan.origin);
        if (originNode != zc::none) {
          ZC_IF_SOME(origin, originNode) {
            if (origin == cfgNode && loanIsInScopeAtNode(cfg, tree, loan, cfgNode)) {
              activeLoans.add(loanId);
            }
          }
        }
      }
    }

    for (size_t placeIndex = 1; placeIndex <= model.placeCount(); ++placeIndex) {
      auto placeId = PlaceId(static_cast<uint32_t>(placeIndex));
      auto moveOrigin = result.getFunctionMoveOrigin(functionIndex, cfgNode, placeId);
      if (moveOrigin == zc::none) { continue; }
      ZC_IF_SOME(origin, moveOrigin) {
        if (origin != cfgNode) { continue; }
      }
      ZC_IF_SOME(movedPlace, model.getPlace(placeId)) {
        for (auto loanId : activeLoans) {
          ZC_IF_SOME(loan, model.getLoan(loanId)) {
            if (!loanIsInScopeAtNode(cfg, tree, loan, cfgNode)) { continue; }
            ZC_IF_SOME(loanPlace, model.getPlace(loan.place)) {
              if (!placesOverlap(movedPlace, loanPlace, FieldOverlapMode::Conservative)) {
                continue;
              }
              result.addFunctionMoveOutOfBorrowReport(
                  functionIndex, BorrowMoveOutOfBorrowReport{cfgNode, placeId, loan.origin});
              break;
            }
          }
        }
      }
    }
  }
}

bool isRawPointerTypedExpression(const type::TypeEnv& typeEnv, ast::NodeId expr) {
  if (!expr || !typeEnv.hasType(expr)) { return false; }
  return type::isRawPointer(typeEnv.find(typeEnv.getType(expr)));
}

void inferRawPointerBoundaryReportsFromExpression(BorrowCheckerResult& result, size_t functionIndex,
                                                  const ast::Tree& tree,
                                                  const type::TypeEnv& typeEnv, ast::NodeId expr,
                                                  bool unsafeAcknowledged) {
  if (!expr || !tree.contains(expr)) { return; }
  const auto& exprNode = tree.node(expr);

  if (exprNode.kind == ast::SyntaxKind::UnsafeBlockExpr) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv, payloadNode(exprNode, ast::kUnsafeBlockExprBodyWord),
        true);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::BlockStmt) {
    auto stmts = tree.list(
        payloadList(exprNode, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord));
    for (size_t i = 0; i < stmts.size(); ++i) {
      inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv, stmts[i],
                                                   unsafeAcknowledged);
    }
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::ExpressionStatement) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv,
        payloadNode(exprNode, ast::kExpressionStatementExpressionWord), unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::LetStmt) {
    auto declListId = payloadNode(exprNode, ast::kLetStmtDeclarationsWord);
    if (!declListId || !tree.contains(declListId)) { return; }
    const auto& declListNode = tree.node(declListId);
    if (declListNode.kind != ast::SyntaxKind::VariableDeclaratorList) { return; }
    auto decls = tree.list(payloadList(declListNode, ast::kVariableDeclaratorListDeclsFirstWord,
                                       ast::kVariableDeclaratorListDeclsSizeWord));
    for (size_t i = 0; i < decls.size(); ++i) {
      if (!decls[i] || !tree.contains(decls[i])) { continue; }
      const auto& declNode = tree.node(decls[i]);
      if (declNode.kind != ast::SyntaxKind::VariableDeclarator) { continue; }
      inferRawPointerBoundaryReportsFromExpression(
          result, functionIndex, tree, typeEnv,
          payloadNode(declNode, ast::kVariableDeclaratorInitWord), unsafeAcknowledged);
    }
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::ReturnStmt) {
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                 payloadNode(exprNode, ast::kReturnStmtValueWord),
                                                 unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto op =
        static_cast<ast::UnaryOperatorKind>(exprNode.payload.words[ast::kUnaryExpressionOpWord]);
    auto operand = payloadNode(exprNode, ast::kUnaryExpressionOperandWord);
    if (op == ast::UnaryOperatorKind::Deref && isRawPointerTypedExpression(typeEnv, operand)) {
      ZC_IF_SOME(report,
                 result.getPlaces().getModel().checkRawPointerBoundary(expr, unsafeAcknowledged)) {
        result.addFunctionRawPointerBoundaryReport(functionIndex, report);
      }
    }
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv, operand,
                                                 unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::CallExpression) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv, payloadNode(exprNode, ast::kCallExpressionCalleeWord),
        unsafeAcknowledged);
    auto args = tree.list(
        payloadList(exprNode, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord));
    for (size_t i = 0; i < args.size(); ++i) {
      inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv, args[i],
                                                   unsafeAcknowledged);
    }
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::BinaryExpr) {
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                 payloadNode(exprNode, ast::kBinaryExprLhsWord),
                                                 unsafeAcknowledged);
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                 payloadNode(exprNode, ast::kBinaryExprRhsWord),
                                                 unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::ConditionalExpr) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv, payloadNode(exprNode, ast::kConditionalExprCondWord),
        unsafeAcknowledged);
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv,
        payloadNode(exprNode, ast::kConditionalExprThenExprWord), unsafeAcknowledged);
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv,
        payloadNode(exprNode, ast::kConditionalExprElseExprWord), unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::NullCoalesceExpr) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv,
        payloadNode(exprNode, ast::kNullCoalesceExprPrimaryWord), unsafeAcknowledged);
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv,
        payloadNode(exprNode, ast::kNullCoalesceExprFallbackWord), unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::IsExpression) {
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                 payloadNode(exprNode, ast::kIsExpressionExprWord),
                                                 unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::CastExpression) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv, payloadNode(exprNode, ast::kCastExpressionExprWord),
        unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::AssignmentExpr) {
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                 payloadNode(exprNode, ast::kAssignmentExprLhsWord),
                                                 unsafeAcknowledged);
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                 payloadNode(exprNode, ast::kAssignmentExprRhsWord),
                                                 unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::IndexExpression) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv,
        payloadNode(exprNode, ast::kIndexExpressionObjectWord), unsafeAcknowledged);
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv, payloadNode(exprNode, ast::kIndexExpressionIndexWord),
        unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::MemberExpression) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv,
        payloadNode(exprNode, ast::kMemberExpressionObjectWord), unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::ArrayLiteral) {
    auto elems = tree.list(
        payloadList(exprNode, ast::kArrayLiteralElemsFirstWord, ast::kArrayLiteralElemsSizeWord));
    for (size_t i = 0; i < elems.size(); ++i) {
      inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv, elems[i],
                                                   unsafeAcknowledged);
    }
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::TupleLiteral) {
    auto elems = tree.list(
        payloadList(exprNode, ast::kTupleLiteralElemsFirstWord, ast::kTupleLiteralElemsSizeWord));
    for (size_t i = 0; i < elems.size(); ++i) {
      inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv, elems[i],
                                                   unsafeAcknowledged);
    }
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::TupleLiteral1) {
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                 payloadNode(exprNode, ast::kTupleLiteral1ElemWord),
                                                 unsafeAcknowledged);
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::ObjectLiteralExpr) {
    auto properties = tree.list(payloadList(exprNode, ast::kObjectLiteralExprPropertiesFirstWord,
                                            ast::kObjectLiteralExprPropertiesSizeWord));
    for (size_t i = 0; i < properties.size(); ++i) {
      inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                   properties[i], unsafeAcknowledged);
    }
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::StructLiteralExpr) {
    auto properties = tree.list(payloadList(exprNode, ast::kStructLiteralExprPropertiesFirstWord,
                                            ast::kStructLiteralExprPropertiesSizeWord));
    for (size_t i = 0; i < properties.size(); ++i) {
      inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv,
                                                   properties[i], unsafeAcknowledged);
    }
    return;
  }

  if (exprNode.kind == ast::SyntaxKind::ObjectProperty) {
    inferRawPointerBoundaryReportsFromExpression(
        result, functionIndex, tree, typeEnv, payloadNode(exprNode, ast::kObjectPropertyValueWord),
        unsafeAcknowledged);
    return;
  }
}

void inferFunctionRawPointerBoundaryReports(BorrowCheckerResult& result, size_t functionIndex,
                                            const ast::Tree& tree, const type::TypeEnv& typeEnv) {
  const auto& cfg = result.getFunctionCfg(functionIndex);
  for (size_t i = 1; i <= cfg.nodeCount(); ++i) {
    auto astNode = cfg.getNodeAst(BorrowCfgNodeId(static_cast<uint32_t>(i)));
    inferRawPointerBoundaryReportsFromExpression(result, functionIndex, tree, typeEnv, astNode,
                                                 false);
  }
}

zc::Maybe<ast::NodeId> localBindingPatternForReturnedLocalReference(
    const ast::Tree& tree, const BorrowPlaceCollectionResult& places, ast::NodeId value,
    uint32_t depth);

zc::Maybe<ast::NodeId> localBindingPatternForRefOperand(const ast::Tree& tree,
                                                        const BorrowPlaceCollectionResult& places,
                                                        ast::NodeId operand) {
  if (!operand || !tree.contains(operand)) { return zc::none; }
  const auto& operandNode = tree.node(operand);
  if (operandNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto op =
        static_cast<ast::UnaryOperatorKind>(operandNode.payload.words[ast::kUnaryExpressionOpWord]);
    if (op == ast::UnaryOperatorKind::Deref) {
      ZC_IF_SOME(binding,
                 localBindingPatternForReturnedLocalReference(
                     tree, places, payloadNode(operandNode, ast::kUnaryExpressionOperandWord), 0)) {
        return binding;
      }
    }
  }
  ZC_IF_SOME(placeId, placeForBorrowExpression(tree, places, operand)) {
    ZC_IF_SOME(place, places.getModel().getPlace(placeId)) {
      if (place.getRootKind() == PlaceRootKind::Local) {
        auto nodes = tree.nodes();
        for (size_t i = 0; i < nodes.size(); ++i) {
          if (localPatternName(tree, nodes[i]).size() == 0) { continue; }
          auto binding = ast::NodeId(static_cast<uint32_t>(i + 1));
          ZC_IF_SOME(bindingPlaceId, places.getPlaceForNode(binding)) {
            if (bindingPlaceId == placeId) { return binding; }
            ZC_IF_SOME(bindingPlace, places.getModel().getPlace(bindingPlaceId)) {
              if (bindingPlace.equals(place)) { return binding; }
            }
          }
        }
        auto fallbackBinding = ast::NodeId(place.getRootId());
        if (tree.contains(fallbackBinding)) { return fallbackBinding; }
      }
    }
  }
  if (operandNode.kind == ast::SyntaxKind::IdentExpr) {
    auto name = tree.ident(ast::IdentId(operandNode.payload.words[ast::kIdentExprNameWord]));
    auto nodes = tree.nodes();
    for (size_t offset = nodes.size(); offset > 0; --offset) {
      auto i = offset - 1;
      auto bindingName = localPatternName(tree, nodes[i]);
      if (bindingName == name) { return ast::NodeId(static_cast<uint32_t>(i + 1)); }
    }
  }
  if (operandNode.kind == ast::SyntaxKind::MemberExpression) {
    auto object = payloadNode(operandNode, ast::kMemberExpressionObjectWord);
    return localBindingPatternForRefOperand(tree, places, object);
  }
  if (operandNode.kind == ast::SyntaxKind::IndexExpression) {
    auto object = payloadNode(operandNode, ast::kIndexExpressionObjectWord);
    return localBindingPatternForRefOperand(tree, places, object);
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> declaratorInitializerForBindingPattern(const ast::Tree& tree,
                                                              ast::NodeId binding) {
  auto nodes = tree.nodes();
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].kind != ast::SyntaxKind::VariableDeclarator) { continue; }
    auto pattern = payloadNode(nodes[i], ast::kVariableDeclaratorPatternWord);
    if (pattern != binding) { continue; }
    return payloadNode(nodes[i], ast::kVariableDeclaratorInitWord);
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> latestPlainAssignmentRhsForBindingName(const ast::Tree& tree,
                                                              zc::StringPtr name) {
  auto nodes = tree.nodes();
  for (size_t offset = nodes.size(); offset > 0; --offset) {
    auto i = offset - 1;
    if (nodes[i].kind != ast::SyntaxKind::AssignmentExpr) { continue; }
    auto op = static_cast<ast::AssignmentOperatorKind>(
        nodes[i].payload.words[ast::kAssignmentExprOpWord]);
    if (op != ast::AssignmentOperatorKind::Assign) { continue; }
    auto lhs = payloadNode(nodes[i], ast::kAssignmentExprLhsWord);
    if (!lhs || !tree.contains(lhs)) { continue; }
    const auto& lhsNode = tree.node(lhs);
    if (lhsNode.kind != ast::SyntaxKind::IdentExpr) { continue; }
    auto lhsName = tree.ident(ast::IdentId(lhsNode.payload.words[ast::kIdentExprNameWord]));
    if (lhsName != name) { continue; }
    return payloadNode(nodes[i], ast::kAssignmentExprRhsWord);
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> localBindingPatternForReferenceSourceExpression(
    const ast::Tree& tree, const BorrowPlaceCollectionResult& places, ast::NodeId expr,
    uint32_t depth);

zc::Maybe<ast::NodeId> localBindingPatternForReferenceSourceList(
    const ast::Tree& tree, const BorrowPlaceCollectionResult& places, ast::NodeList list,
    uint32_t depth) {
  auto nodes = tree.list(list);
  for (size_t i = 0; i < nodes.size(); ++i) {
    ZC_IF_SOME(binding,
               localBindingPatternForReferenceSourceExpression(tree, places, nodes[i], depth + 1)) {
      return binding;
    }
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> objectLikePropertyValue(const ast::Tree& tree, ast::NodeId object,
                                               zc::StringPtr propertyName) {
  if (!object || !tree.contains(object)) { return zc::none; }
  const auto& objectNode = tree.node(object);
  ast::NodeList properties;
  if (objectNode.kind == ast::SyntaxKind::ObjectLiteralExpr) {
    properties = payloadList(objectNode, ast::kObjectLiteralExprPropertiesFirstWord,
                             ast::kObjectLiteralExprPropertiesSizeWord);
  } else if (objectNode.kind == ast::SyntaxKind::StructLiteralExpr) {
    properties = payloadList(objectNode, ast::kStructLiteralExprPropertiesFirstWord,
                             ast::kStructLiteralExprPropertiesSizeWord);
  } else {
    return zc::none;
  }

  auto propertyNodes = tree.list(properties);
  for (size_t i = 0; i < propertyNodes.size(); ++i) {
    if (!propertyNodes[i] || !tree.contains(propertyNodes[i])) { continue; }
    const auto& propertyNode = tree.node(propertyNodes[i]);
    if (propertyNode.kind != ast::SyntaxKind::ObjectProperty) { continue; }
    auto name = tree.ident(ast::IdentId(propertyNode.payload.words[ast::kObjectPropertyNameWord]));
    if (name != propertyName) { continue; }
    return payloadNode(propertyNode, ast::kObjectPropertyValueWord);
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> localBindingPatternForObjectLikeReferenceValue(
    const ast::Tree& tree, const BorrowPlaceCollectionResult& places, ast::NodeId object,
    uint32_t depth) {
  if (!object || !tree.contains(object)) { return zc::none; }
  const auto& objectNode = tree.node(object);
  ast::NodeList properties;
  if (objectNode.kind == ast::SyntaxKind::ObjectLiteralExpr) {
    properties = payloadList(objectNode, ast::kObjectLiteralExprPropertiesFirstWord,
                             ast::kObjectLiteralExprPropertiesSizeWord);
  } else if (objectNode.kind == ast::SyntaxKind::StructLiteralExpr) {
    properties = payloadList(objectNode, ast::kStructLiteralExprPropertiesFirstWord,
                             ast::kStructLiteralExprPropertiesSizeWord);
  } else {
    return zc::none;
  }

  auto propertyNodes = tree.list(properties);
  for (size_t i = 0; i < propertyNodes.size(); ++i) {
    if (!propertyNodes[i] || !tree.contains(propertyNodes[i])) { continue; }
    const auto& propertyNode = tree.node(propertyNodes[i]);
    if (propertyNode.kind != ast::SyntaxKind::ObjectProperty) { continue; }
    auto value = payloadNode(propertyNode, ast::kObjectPropertyValueWord);
    ZC_IF_SOME(binding,
               localBindingPatternForReferenceSourceExpression(tree, places, value, depth + 1)) {
      return binding;
    }
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> localBindingPatternForArrayReferenceValue(
    const ast::Tree& tree, const BorrowPlaceCollectionResult& places, ast::NodeId array,
    uint32_t depth) {
  if (!array || !tree.contains(array)) { return zc::none; }
  const auto& arrayNode = tree.node(array);
  if (arrayNode.kind != ast::SyntaxKind::ArrayLiteral) { return zc::none; }
  auto elems = tree.list(
      payloadList(arrayNode, ast::kArrayLiteralElemsFirstWord, ast::kArrayLiteralElemsSizeWord));
  for (size_t i = 0; i < elems.size(); ++i) {
    ZC_IF_SOME(binding,
               localBindingPatternForReferenceSourceExpression(tree, places, elems[i], depth + 1)) {
      return binding;
    }
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> localBindingPatternForReturnedLocalReference(
    const ast::Tree& tree, const BorrowPlaceCollectionResult& places, ast::NodeId value,
    uint32_t depth) {
  constexpr uint32_t kMaxLocalReferenceTraceDepth = 8;
  if (depth >= kMaxLocalReferenceTraceDepth) { return zc::none; }
  if (!value || !tree.contains(value)) { return zc::none; }
  const auto& valueNode = tree.node(value);
  if (valueNode.kind != ast::SyntaxKind::IdentExpr) { return zc::none; }
  auto name = tree.ident(ast::IdentId(valueNode.payload.words[ast::kIdentExprNameWord]));
  auto nodes = tree.nodes();
  for (size_t offset = nodes.size(); offset > 0; --offset) {
    auto i = offset - 1;
    auto bindingName = localPatternName(tree, nodes[i]);
    if (bindingName != name) { continue; }
    auto binding = ast::NodeId(static_cast<uint32_t>(i + 1));
    ZC_IF_SOME(rhs, latestPlainAssignmentRhsForBindingName(tree, name)) {
      return localBindingPatternForReferenceSourceExpression(tree, places, rhs, depth);
    }
    ZC_IF_SOME(init, declaratorInitializerForBindingPattern(tree, binding)) {
      if (!init || !tree.contains(init)) { return zc::none; }
      return localBindingPatternForReferenceSourceExpression(tree, places, init, depth);
    }
    return zc::none;
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> localBindingPatternForReferenceSourceExpression(
    const ast::Tree& tree, const BorrowPlaceCollectionResult& places, ast::NodeId expr,
    uint32_t depth) {
  if (!expr || !tree.contains(expr)) { return zc::none; }
  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto op =
        static_cast<ast::UnaryOperatorKind>(exprNode.payload.words[ast::kUnaryExpressionOpWord]);
    if (op != ast::UnaryOperatorKind::Ref && op != ast::UnaryOperatorKind::RefMut) {
      return zc::none;
    }
    return localBindingPatternForRefOperand(
        tree, places, payloadNode(exprNode, ast::kUnaryExpressionOperandWord));
  }
  if (exprNode.kind == ast::SyntaxKind::IdentExpr) {
    return localBindingPatternForReturnedLocalReference(tree, places, expr, depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::MemberExpression) {
    auto object = payloadNode(exprNode, ast::kMemberExpressionObjectWord);
    auto propertyName =
        tree.ident(ast::IdentId(exprNode.payload.words[ast::kMemberExpressionPropertyWord]));
    ZC_IF_SOME(value, objectLikePropertyValue(tree, object, propertyName)) {
      return localBindingPatternForReferenceSourceExpression(tree, places, value, depth + 1);
    }
  }
  if (exprNode.kind == ast::SyntaxKind::ObjectLiteralExpr ||
      exprNode.kind == ast::SyntaxKind::StructLiteralExpr) {
    return localBindingPatternForObjectLikeReferenceValue(tree, places, expr, depth);
  }
  if (exprNode.kind == ast::SyntaxKind::ArrayLiteral) {
    return localBindingPatternForArrayReferenceValue(tree, places, expr, depth);
  }
  if (exprNode.kind == ast::SyntaxKind::ConditionalExpr) {
    ZC_IF_SOME(binding, localBindingPatternForReferenceSourceExpression(
                            tree, places, payloadNode(exprNode, ast::kConditionalExprThenExprWord),
                            depth + 1)) {
      return binding;
    }
    return localBindingPatternForReferenceSourceExpression(
        tree, places, payloadNode(exprNode, ast::kConditionalExprElseExprWord), depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::CastExpression) {
    return localBindingPatternForReferenceSourceExpression(
        tree, places, payloadNode(exprNode, ast::kCastExpressionExprWord), depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::NullCoalesceExpr) {
    ZC_IF_SOME(binding, localBindingPatternForReferenceSourceExpression(
                            tree, places, payloadNode(exprNode, ast::kNullCoalesceExprPrimaryWord),
                            depth + 1)) {
      return binding;
    }
    return localBindingPatternForReferenceSourceExpression(
        tree, places, payloadNode(exprNode, ast::kNullCoalesceExprFallbackWord), depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::ErrorDefaultExpr) {
    ZC_IF_SOME(binding, localBindingPatternForReferenceSourceExpression(
                            tree, places, payloadNode(exprNode, ast::kErrorDefaultExprPrimaryWord),
                            depth + 1)) {
      return binding;
    }
    return localBindingPatternForReferenceSourceExpression(
        tree, places, payloadNode(exprNode, ast::kErrorDefaultExprFallbackWord), depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::IndexExpression) {
    ZC_IF_SOME(binding, localBindingPatternForReferenceSourceExpression(
                            tree, places, payloadNode(exprNode, ast::kIndexExpressionObjectWord),
                            depth + 1)) {
      return binding;
    }
    return localBindingPatternForReferenceSourceExpression(
        tree, places, payloadNode(exprNode, ast::kIndexExpressionIndexWord), depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::CallExpression) {
    ZC_IF_SOME(binding, localBindingPatternForReferenceSourceExpression(
                            tree, places, payloadNode(exprNode, ast::kCallExpressionCalleeWord),
                            depth + 1)) {
      return binding;
    }
    return localBindingPatternForReferenceSourceList(
        tree, places,
        payloadList(exprNode, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord),
        depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::NewExpression) {
    ZC_IF_SOME(binding,
               localBindingPatternForReferenceSourceExpression(
                   tree, places, payloadNode(exprNode, ast::kNewExpressionCalleeWord), depth + 1)) {
      return binding;
    }
    return localBindingPatternForReferenceSourceList(
        tree, places,
        payloadList(exprNode, ast::kNewExpressionArgsFirstWord, ast::kNewExpressionArgsSizeWord),
        depth + 1);
  }
  if (exprNode.kind == ast::SyntaxKind::ImportCallExpression) {
    return localBindingPatternForReferenceSourceList(
        tree, places,
        payloadList(exprNode, ast::kImportCallExpressionArgsFirstWord,
                    ast::kImportCallExpressionArgsSizeWord),
        depth + 1);
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> localBindingPatternForReturnValue(const ast::Tree& tree,
                                                         const BorrowPlaceCollectionResult& places,
                                                         ast::NodeId value) {
  if (!value || !tree.contains(value)) { return zc::none; }
  const auto& valueNode = tree.node(value);
  if (valueNode.kind == ast::SyntaxKind::UnaryExpression) {
    auto op =
        static_cast<ast::UnaryOperatorKind>(valueNode.payload.words[ast::kUnaryExpressionOpWord]);
    if (op == ast::UnaryOperatorKind::Ref || op == ast::UnaryOperatorKind::RefMut) {
      return localBindingPatternForRefOperand(
          tree, places, payloadNode(valueNode, ast::kUnaryExpressionOperandWord));
    }
  }
  if (valueNode.kind == ast::SyntaxKind::MemberExpression) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::ObjectLiteralExpr ||
      valueNode.kind == ast::SyntaxKind::StructLiteralExpr) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::ArrayLiteral) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::ConditionalExpr) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::CastExpression) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::NullCoalesceExpr) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::ErrorDefaultExpr) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::IndexExpression) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::CallExpression) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::NewExpression) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  if (valueNode.kind == ast::SyntaxKind::ImportCallExpression) {
    return localBindingPatternForReferenceSourceExpression(tree, places, value, 0);
  }
  return localBindingPatternForReturnedLocalReference(tree, places, value, 0);
}

void inferRegionEscapeFromReturn(BorrowCheckerResult& result, size_t functionIndex,
                                 const ast::Tree& tree, ast::NodeId statement) {
  if (!statement || !tree.contains(statement)) { return; }
  const auto& statementNode = tree.node(statement);
  if (statementNode.kind != ast::SyntaxKind::ReturnStmt) { return; }
  auto value = payloadNode(statementNode, ast::kReturnStmtValueWord);
  if (!value || !tree.contains(value)) { return; }
  ZC_IF_SOME(binding, localBindingPatternForReturnValue(tree, result.getPlaces(), value)) {
    BorrowModel& model = result.getPlaces().getModel();
    auto returnRegion = model.addRegion(RegionKind::Return);
    auto localRegion = model.addRegion(RegionKind::Lexical, returnRegion);
    ZC_IF_SOME(report, model.checkRegionEscape(returnRegion, localRegion, value, binding)) {
      result.addFunctionRegionEscapeReport(functionIndex, report);
    }
  }
}

void inferFunctionRegionEscapeReports(BorrowCheckerResult& result, size_t functionIndex,
                                      const ast::Tree& tree) {
  const auto& cfg = result.getFunctionCfg(functionIndex);
  for (size_t i = 1; i <= cfg.nodeCount(); ++i) {
    auto astNode = cfg.getNodeAst(BorrowCfgNodeId(static_cast<uint32_t>(i)));
    inferRegionEscapeFromReturn(result, functionIndex, tree, astNode);
  }
}

}  // namespace

BorrowCheckerPhase::BorrowCheckerPhase(const ast::Tree& tree, const type::TypeEnv& typeEnv,
                                       zc::Maybe<const ast::BindingMetadata&> metadata) noexcept
    : impl(zc::heap<Impl>(tree, typeEnv, metadata)) {}

BorrowCheckerPhase::~BorrowCheckerPhase() noexcept(false) = default;

BorrowCheckerResult BorrowCheckerPhase::run() const {
  BorrowCheckerResult result;
  result.setPlaces(collectBorrowPlaces(impl->tree, impl->typeEnv, impl->metadata));

  auto nodes = impl->tree.nodes();
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (!isFunctionLikeNode(nodes[i].kind)) { continue; }
    auto functionDecl = ast::NodeId(static_cast<uint32_t>(i + 1));
    BorrowLoanBuilder loans(result.getPlaces().getModel(), impl->tree, result.getPlaces());
    loans.buildFunctionLoans(functionDecl);
    result.addFunctionSummary(functionDecl, buildStraightLineBorrowCfg(impl->tree, functionDecl));
    inferFunctionMoveFacts(result, result.functionCount() - 1, impl->tree, impl->typeEnv);
    inferFunctionUseAfterMoveReports(result, result.functionCount() - 1, impl->tree);
    inferFunctionBorrowConflictReports(result, result.functionCount() - 1, impl->tree);
    inferFunctionMoveOutOfBorrowReports(result, result.functionCount() - 1, impl->tree);
    inferFunctionRawPointerBoundaryReports(result, result.functionCount() - 1, impl->tree,
                                           impl->typeEnv);
    inferFunctionRegionEscapeReports(result, result.functionCount() - 1, impl->tree);
  }

  return result;
}

size_t emitBorrowDiagnostics(const ast::Tree& tree, const BorrowCheckerResult& result,
                             diagnostics::DiagnosticEngine& diags) {
  size_t emitted = 0;
  for (size_t functionIndex = 0; functionIndex < result.functionCount(); ++functionIndex) {
    const BorrowCfg& cfg = result.getFunctionCfg(functionIndex);
    for (size_t reportIndex = 0;
         reportIndex < result.functionUseAfterMoveReportCount(functionIndex); ++reportIndex) {
      const BorrowUseAfterMoveReport& report =
          result.getFunctionUseAfterMoveReport(functionIndex, reportIndex);

      const ast::NodeId useAst = cfg.getNodeAst(report.node);
      source::SourceLoc useLoc;
      if (tree.contains(useAst)) { useLoc = tree.node(useAst).range.getStart(); }

      auto diagnostic = diags.diagnose<diagnostics::DiagID::UseAfterMove>(useLoc);
      const ast::NodeId moveAst = cfg.getNodeAst(report.moveOrigin);
      if (tree.contains(moveAst)) {
        diagnostic.addChild(zc::heap<diagnostics::Diagnostic>(diagnostics::DiagID::ValueMovedHere,
                                                              tree.node(moveAst).range.getStart()));
      }
      ++emitted;
    }
    for (size_t reportIndex = 0;
         reportIndex < result.functionBorrowConflictReportCount(functionIndex); ++reportIndex) {
      emitBorrowConflictDiagnostic(
          tree, cfg, result.getFunctionBorrowConflictReport(functionIndex, reportIndex), diags);
      ++emitted;
    }
    for (size_t reportIndex = 0;
         reportIndex < result.functionMoveOutOfBorrowReportCount(functionIndex); ++reportIndex) {
      emitBorrowMoveOutOfBorrowDiagnostic(
          tree, cfg, result.getFunctionMoveOutOfBorrowReport(functionIndex, reportIndex), diags);
      ++emitted;
    }
    for (size_t reportIndex = 0;
         reportIndex < result.functionRegionEscapeReportCount(functionIndex); ++reportIndex) {
      emitBorrowRegionEscapeDiagnostic(
          tree, result.getFunctionRegionEscapeReport(functionIndex, reportIndex), diags);
      ++emitted;
    }
    for (size_t reportIndex = 0;
         reportIndex < result.functionRawPointerBoundaryReportCount(functionIndex); ++reportIndex) {
      emitBorrowRawPointerBoundaryDiagnostic(
          tree, result.getFunctionRawPointerBoundaryReport(functionIndex, reportIndex), diags);
      ++emitted;
    }
  }
  return emitted;
}

void emitBorrowConflictDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                  const BorrowConflictReport& report,
                                  diagnostics::DiagnosticEngine& diags) {
  const ast::NodeId requestAst = cfg.getNodeAst(report.node);
  source::SourceLoc requestLoc;
  if (tree.contains(requestAst)) { requestLoc = tree.node(requestAst).range.getStart(); }

  auto diagId = diagnostics::DiagID::SharedBorrowConflicts;
  if (report.requestedKind == LoanKind::Mutable) {
    diagId = diagnostics::DiagID::MutableBorrowConflicts;
  }

  auto diagnostic = diagId == diagnostics::DiagID::MutableBorrowConflicts
                        ? diags.diagnose<diagnostics::DiagID::MutableBorrowConflicts>(requestLoc)
                        : diags.diagnose<diagnostics::DiagID::SharedBorrowConflicts>(requestLoc);
  if (tree.contains(report.origin)) {
    diagnostic.addChild(zc::heap<diagnostics::Diagnostic>(
        diagnostics::DiagID::BorrowOriginHere, tree.node(report.origin).range.getStart()));
  }
}

void emitBorrowMoveOutOfBorrowDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                         const BorrowMoveOutOfBorrowReport& report,
                                         diagnostics::DiagnosticEngine& diags) {
  const ast::NodeId moveAst = cfg.getNodeAst(report.moveNode);
  source::SourceLoc moveLoc;
  if (tree.contains(moveAst)) { moveLoc = tree.node(moveAst).range.getStart(); }

  auto diagnostic = diags.diagnose<diagnostics::DiagID::MoveOutOfBorrow>(moveLoc);
  if (tree.contains(report.borrowOrigin)) {
    diagnostic.addChild(zc::heap<diagnostics::Diagnostic>(
        diagnostics::DiagID::BorrowOriginHere, tree.node(report.borrowOrigin).range.getStart()));
  }
}

void emitBorrowRegionEscapeDiagnostic(const ast::Tree& tree, const BorrowRegionEscapeReport& report,
                                      diagnostics::DiagnosticEngine& diags) {
  source::SourceLoc useLoc;
  if (tree.contains(report.useNode)) { useLoc = tree.node(report.useNode).range.getStart(); }

  auto diagnostic = diags.diagnose<diagnostics::DiagID::BorrowDoesNotLiveLongEnough>(useLoc);
  if (tree.contains(report.referentNode)) {
    diagnostic.addChild(zc::heap<diagnostics::Diagnostic>(
        diagnostics::DiagID::BorrowReferentHere, tree.node(report.referentNode).range.getStart()));
  }
}

void emitBorrowMissingConsumeDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                        const BorrowMissingConsumeReport& report,
                                        diagnostics::DiagnosticEngine& diags) {
  const ast::NodeId nodeAst = cfg.getNodeAst(report.node);
  source::SourceLoc loc;
  if (tree.contains(nodeAst)) { loc = tree.node(nodeAst).range.getStart(); }

  auto diagnostic = diags.diagnose<diagnostics::DiagID::LinearNotConsumed>(loc);
  const ast::NodeId originAst = cfg.getNodeAst(report.initializeOrigin);
  if (tree.contains(originAst)) {
    diagnostic.addChild(zc::heap<diagnostics::Diagnostic>(
        diagnostics::DiagID::LinearInitializedHere, tree.node(originAst).range.getStart()));
  }
}

void emitBorrowDoubleConsumeDiagnostic(const ast::Tree& tree, const BorrowCfg& cfg,
                                       const BorrowDoubleConsumeReport& report,
                                       diagnostics::DiagnosticEngine& diags) {
  const ast::NodeId nodeAst = cfg.getNodeAst(report.node);
  source::SourceLoc loc;
  if (tree.contains(nodeAst)) { loc = tree.node(nodeAst).range.getStart(); }

  auto diagnostic = diags.diagnose<diagnostics::DiagID::LinearConsumedTwice>(loc);
  const ast::NodeId originAst = cfg.getNodeAst(report.consumeOrigin);
  if (tree.contains(originAst)) {
    diagnostic.addChild(zc::heap<diagnostics::Diagnostic>(
        diagnostics::DiagID::LinearFirstConsumedHere, tree.node(originAst).range.getStart()));
  }
}

void emitBorrowScopedTaskCaptureDiagnostic(const ast::Tree& tree,
                                           const BorrowScopedTaskCaptureReport& report,
                                           diagnostics::DiagnosticEngine& diags) {
  source::SourceLoc captureLoc;
  if (tree.contains(report.captureNode)) {
    captureLoc = tree.node(report.captureNode).range.getStart();
  }

  auto diagnostic = diags.diagnose<diagnostics::DiagID::ScopedTaskBorrowEscapes>(captureLoc);
  if (tree.contains(report.referentNode)) {
    diagnostic.addChild(
        zc::heap<diagnostics::Diagnostic>(diagnostics::DiagID::ScopedTaskReferentHere,
                                          tree.node(report.referentNode).range.getStart()));
  }
}

void emitBorrowRawPointerBoundaryDiagnostic(const ast::Tree& tree,
                                            const BorrowRawPointerBoundaryReport& report,
                                            diagnostics::DiagnosticEngine& diags) {
  source::SourceLoc boundaryLoc;
  if (tree.contains(report.boundaryNode)) {
    boundaryLoc = tree.node(report.boundaryNode).range.getStart();
  }

  diags.diagnose<diagnostics::DiagID::RawPointerBoundaryRequiresUnsafe>(boundaryLoc);
}

namespace {

bool isMarkedMutableBorrow(zc::ArrayPtr<const ast::NodeId> mutableBorrowExprs,
                           ast::NodeId borrowExpr) {
  for (auto marked : mutableBorrowExprs) {
    if (marked == borrowExpr) { return true; }
  }
  return false;
}

void buildLoanForBorrowExpression(BorrowModel& model, const ast::Tree& tree,
                                  const BorrowPlaceCollectionResult& places,
                                  zc::ArrayPtr<const ast::NodeId> mutableBorrowExprs,
                                  ast::NodeId borrowExpr) {
  ZC_IF_SOME(place, referencedPlaceForSharedBorrow(model, tree, places, borrowExpr)) {
    auto kind = LoanKind::Shared;
    if (tree.contains(borrowExpr)) {
      const auto& borrowNode = tree.node(borrowExpr);
      if (borrowNode.kind == ast::SyntaxKind::UnaryExpression) {
        auto op = static_cast<ast::UnaryOperatorKind>(
            borrowNode.payload.words[ast::kUnaryExpressionOpWord]);
        if (op == ast::UnaryOperatorKind::RefMut) { kind = LoanKind::Mutable; }
      }
    }
    if (isMarkedMutableBorrow(mutableBorrowExprs, borrowExpr)) { kind = LoanKind::Mutable; }
    model.addLoan(place, kind, RegionId(), borrowExpr);
  }
}

void buildLoansInExpression(BorrowModel& model, const ast::Tree& tree,
                            const BorrowPlaceCollectionResult& places,
                            zc::ArrayPtr<const ast::NodeId> mutableBorrowExprs, ast::NodeId expr);

void buildLoansInExpressionList(BorrowModel& model, const ast::Tree& tree,
                                const BorrowPlaceCollectionResult& places,
                                zc::ArrayPtr<const ast::NodeId> mutableBorrowExprs,
                                ast::NodeList exprList) {
  auto exprs = tree.list(exprList);
  for (size_t i = 0; i < exprs.size(); ++i) {
    buildLoansInExpression(model, tree, places, mutableBorrowExprs, exprs[i]);
  }
}

void buildLoansInExpression(BorrowModel& model, const ast::Tree& tree,
                            const BorrowPlaceCollectionResult& places,
                            zc::ArrayPtr<const ast::NodeId> mutableBorrowExprs, ast::NodeId expr) {
  if (!expr || !tree.contains(expr)) { return; }
  buildLoanForBorrowExpression(model, tree, places, mutableBorrowExprs, expr);

  const auto& exprNode = tree.node(expr);
  if (exprNode.kind == ast::SyntaxKind::CallExpression) {
    buildLoansInExpression(model, tree, places, mutableBorrowExprs,
                           payloadNode(exprNode, ast::kCallExpressionCalleeWord));
    buildLoansInExpressionList(
        model, tree, places, mutableBorrowExprs,
        payloadList(exprNode, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord));
  }
}

void buildLoansInLet(BorrowModel& model, const ast::Tree& tree,
                     const BorrowPlaceCollectionResult& places,
                     zc::ArrayPtr<const ast::NodeId> mutableBorrowExprs, ast::NodeId letStmt) {
  if (!letStmt || !tree.contains(letStmt)) { return; }
  const auto& letNode = tree.node(letStmt);
  if (letNode.kind != ast::SyntaxKind::LetStmt) { return; }
  auto declListId = payloadNode(letNode, ast::kLetStmtDeclarationsWord);
  if (!declListId || !tree.contains(declListId)) { return; }
  const auto& declListNode = tree.node(declListId);
  if (declListNode.kind != ast::SyntaxKind::VariableDeclaratorList) { return; }

  auto decls = tree.list(payloadList(declListNode, ast::kVariableDeclaratorListDeclsFirstWord,
                                     ast::kVariableDeclaratorListDeclsSizeWord));
  for (size_t i = 0; i < decls.size(); ++i) {
    if (!decls[i] || !tree.contains(decls[i])) { continue; }
    const auto& declNode = tree.node(decls[i]);
    if (declNode.kind != ast::SyntaxKind::VariableDeclarator) { continue; }
    auto init = payloadNode(declNode, ast::kVariableDeclaratorInitWord);
    buildLoansInExpression(model, tree, places, mutableBorrowExprs, init);
  }
}

void buildLoansInBlock(BorrowModel& model, const ast::Tree& tree,
                       const BorrowPlaceCollectionResult& places,
                       zc::ArrayPtr<const ast::NodeId> mutableBorrowExprs, ast::NodeId block) {
  if (!block || !tree.contains(block)) { return; }
  const auto& blockNode = tree.node(block);
  if (blockNode.kind != ast::SyntaxKind::BlockStmt) { return; }
  auto statements = tree.list(
      payloadList(blockNode, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord));
  for (size_t i = 0; i < statements.size(); ++i) {
    auto statement = unwrapStatementListItem(tree, statements[i]);
    if (!statement || !tree.contains(statement)) { continue; }
    const auto& statementNode = tree.node(statement);
    if (statementNode.kind == ast::SyntaxKind::LetStmt) {
      buildLoansInLet(model, tree, places, mutableBorrowExprs, statement);
    }
    if (statementNode.kind == ast::SyntaxKind::ExpressionStatement) {
      buildLoansInExpression(model, tree, places, mutableBorrowExprs,
                             payloadNode(statementNode, ast::kExpressionStatementExpressionWord));
    }
    if (statementNode.kind == ast::SyntaxKind::BlockStmt) {
      buildLoansInBlock(model, tree, places, mutableBorrowExprs, statement);
    }
    if (statementNode.kind == ast::SyntaxKind::IfStmt) {
      buildLoansInBlock(model, tree, places, mutableBorrowExprs,
                        payloadNode(statementNode, ast::kIfStmtThenStmtWord));
      buildLoansInBlock(model, tree, places, mutableBorrowExprs,
                        payloadNode(statementNode, ast::kIfStmtElseStmtWord));
    }
    if (statementNode.kind == ast::SyntaxKind::WhileStmt) {
      buildLoansInBlock(model, tree, places, mutableBorrowExprs,
                        payloadNode(statementNode, ast::kWhileStmtBodyWord));
    }
    if (statementNode.kind == ast::SyntaxKind::MatchStmt) {
      auto arms = tree.list(
          payloadList(statementNode, ast::kMatchStmtArmsFirstWord, ast::kMatchStmtArmsSizeWord));
      for (size_t armIndex = 0; armIndex < arms.size(); ++armIndex) {
        if (!arms[armIndex] || !tree.contains(arms[armIndex])) { continue; }
        const auto& armNode = tree.node(arms[armIndex]);
        if (armNode.kind != ast::SyntaxKind::MatchArmStmt) { continue; }
        buildLoansInBlock(model, tree, places, mutableBorrowExprs,
                          payloadNode(armNode, ast::kMatchArmStmtBodyWord));
      }
    }
  }
}

bool containsPlace(zc::ArrayPtr<const PlaceId> places, PlaceId place) {
  for (auto existing : places) {
    if (existing == place) { return true; }
  }
  return false;
}

bool addPlaceIfMissing(zc::Vector<PlaceId>& places, PlaceId place) {
  if (containsPlace(places.asPtr(), place)) { return false; }
  places.add(place);
  return true;
}

bool containsLoan(zc::ArrayPtr<const LoanId> loans, LoanId loan) {
  for (auto existing : loans) {
    if (existing == loan) { return true; }
  }
  return false;
}

bool addLoanIfMissing(zc::Vector<LoanId>& loans, LoanId loan) {
  if (containsLoan(loans.asPtr(), loan)) { return false; }
  loans.add(loan);
  return true;
}

bool removeLoanIfPresent(zc::Vector<LoanId>& loans, LoanId loan) {
  for (size_t i = 0; i < loans.size(); ++i) {
    if (loans[i] == loan) {
      loans[i] = loans.back();
      loans.removeLast();
      return true;
    }
  }
  return false;
}

bool sameLoanSet(zc::ArrayPtr<const LoanId> lhs, zc::ArrayPtr<const LoanId> rhs) {
  if (lhs.size() != rhs.size()) { return false; }
  for (auto loan : lhs) {
    if (!containsLoan(rhs, loan)) { return false; }
  }
  return true;
}

zc::Vector<LoanId> applyActiveLoanTransfer(zc::ArrayPtr<const LoanId> activeInput,
                                           zc::ArrayPtr<const LoanId> suspendedInput,
                                           zc::ArrayPtr<const LoanId> endLoans,
                                           zc::ArrayPtr<const LoanId> suspendLoans,
                                           zc::ArrayPtr<const LoanId> resumeLoans,
                                           zc::ArrayPtr<const LoanId> explicitLoans) {
  zc::Vector<LoanId> result;
  for (auto loan : activeInput) { addLoanIfMissing(result, loan); }
  for (auto loan : endLoans) { removeLoanIfPresent(result, loan); }
  for (auto loan : suspendLoans) { removeLoanIfPresent(result, loan); }
  for (auto loan : resumeLoans) {
    if (containsLoan(suspendedInput, loan)) { addLoanIfMissing(result, loan); }
  }
  for (auto loan : explicitLoans) { addLoanIfMissing(result, loan); }
  return result;
}

zc::Vector<LoanId> applySuspendedLoanTransfer(zc::ArrayPtr<const LoanId> activeInput,
                                              zc::ArrayPtr<const LoanId> suspendedInput,
                                              zc::ArrayPtr<const LoanId> endLoans,
                                              zc::ArrayPtr<const LoanId> suspendLoans,
                                              zc::ArrayPtr<const LoanId> resumeLoans) {
  zc::Vector<LoanId> result;
  for (auto loan : suspendedInput) { addLoanIfMissing(result, loan); }
  for (auto loan : endLoans) { removeLoanIfPresent(result, loan); }
  for (auto loan : resumeLoans) { removeLoanIfPresent(result, loan); }
  for (auto loan : suspendLoans) {
    if (containsLoan(activeInput, loan)) { addLoanIfMissing(result, loan); }
  }
  return result;
}

bool removePlaceIfPresent(zc::Vector<PlaceId>& places, PlaceId place) {
  for (size_t i = 0; i < places.size(); ++i) {
    if (places[i] == place) {
      places[i] = places.back();
      places.removeLast();
      return true;
    }
  }
  return false;
}

zc::Maybe<BorrowCfgNodeId> findOrigin(zc::ArrayPtr<const MoveOrigin> origins, PlaceId place) {
  for (auto origin : origins) {
    if (origin.place == place) { return origin.origin; }
  }
  return zc::none;
}

bool setOriginIfMissing(zc::Vector<MoveOrigin>& origins, PlaceId place, BorrowCfgNodeId origin) {
  if (findOrigin(origins.asPtr(), place) != zc::none) { return false; }
  origins.add(MoveOrigin{place, origin});
  return true;
}

bool removeOriginIfPresent(zc::Vector<MoveOrigin>& origins, PlaceId place) {
  for (size_t i = 0; i < origins.size(); ++i) {
    if (origins[i].place == place) {
      origins[i] = origins.back();
      origins.removeLast();
      return true;
    }
  }
  return false;
}

bool sameOriginSet(zc::ArrayPtr<const MoveOrigin> lhs, zc::ArrayPtr<const MoveOrigin> rhs) {
  if (lhs.size() != rhs.size()) { return false; }
  for (auto origin : lhs) {
    ZC_IF_SOME(rhsOrigin, findOrigin(rhs, origin.place)) {
      if (rhsOrigin != origin.origin) { return false; }
    }
    else { return false; }
  }
  return true;
}

bool samePlaceSet(zc::ArrayPtr<const PlaceId> lhs, zc::ArrayPtr<const PlaceId> rhs) {
  if (lhs.size() != rhs.size()) { return false; }
  for (auto place : lhs) {
    if (!containsPlace(rhs, place)) { return false; }
  }
  return true;
}

zc::Vector<PlaceId> applyMoveTransfer(zc::ArrayPtr<const PlaceId> input,
                                      zc::ArrayPtr<const PlaceId> reinitializes,
                                      zc::ArrayPtr<const PlaceId> explicitMoves) {
  zc::Vector<PlaceId> result;
  for (auto place : input) { addPlaceIfMissing(result, place); }
  for (auto place : reinitializes) { removePlaceIfPresent(result, place); }
  for (auto place : explicitMoves) { addPlaceIfMissing(result, place); }
  return result;
}

zc::Vector<MoveOrigin> applyOriginTransfer(zc::ArrayPtr<const MoveOrigin> input,
                                           zc::ArrayPtr<const PlaceId> reinitializes,
                                           zc::ArrayPtr<const PlaceId> explicitMoves,
                                           BorrowCfgNodeId node) {
  zc::Vector<MoveOrigin> result;
  for (auto origin : input) { setOriginIfMissing(result, origin.place, origin.origin); }
  for (auto place : reinitializes) { removeOriginIfPresent(result, place); }
  for (auto place : explicitMoves) { setOriginIfMissing(result, place, node); }
  return result;
}

zc::Vector<PlaceId> applyConsumedTransfer(zc::ArrayPtr<const PlaceId> input,
                                          zc::ArrayPtr<const PlaceId> initializes,
                                          zc::ArrayPtr<const PlaceId> consumes) {
  zc::Vector<PlaceId> result;
  for (auto place : input) { addPlaceIfMissing(result, place); }
  for (auto place : initializes) { removePlaceIfPresent(result, place); }
  for (auto place : consumes) { addPlaceIfMissing(result, place); }
  return result;
}

zc::Vector<MoveOrigin> applyConsumeOriginTransfer(zc::ArrayPtr<const MoveOrigin> input,
                                                  zc::ArrayPtr<const PlaceId> initializes,
                                                  zc::ArrayPtr<const PlaceId> consumes,
                                                  BorrowCfgNodeId node) {
  zc::Vector<MoveOrigin> result;
  for (auto origin : input) { setOriginIfMissing(result, origin.place, origin.origin); }
  for (auto place : initializes) { removeOriginIfPresent(result, place); }
  for (auto place : consumes) { setOriginIfMissing(result, place, node); }
  return result;
}

}  // namespace

BorrowMoveState::BorrowMoveState(const BorrowCfg& cfg) : impl(zc::heap<Impl>(cfg)) {}

BorrowMoveState::~BorrowMoveState() noexcept(false) = default;

BorrowMoveState::BorrowMoveState(BorrowMoveState&& other) noexcept = default;

BorrowMoveState& BorrowMoveState::operator=(BorrowMoveState&& other) noexcept = default;

void BorrowMoveState::addMove(BorrowCfgNodeId node, PlaceId place) {
  ZC_IREQUIRE(node.isValid(), "BorrowMoveState::addMove: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->explicitMoves.size(),
              "BorrowMoveState::addMove: CFG node out of range");
  addPlaceIfMissing(impl->explicitMoves[node.value], place);
}

void BorrowMoveState::addReinitialize(BorrowCfgNodeId node, PlaceId place) {
  ZC_IREQUIRE(node.isValid(), "BorrowMoveState::addReinitialize: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->reinitializes.size(),
              "BorrowMoveState::addReinitialize: CFG node out of range");
  addPlaceIfMissing(impl->reinitializes[node.value], place);
}

void BorrowMoveState::propagate() {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t node = 1; node < impl->movedAt.size(); ++node) {
      auto nextOut =
          applyMoveTransfer(impl->movedIn[node].asPtr(), impl->reinitializes[node].asPtr(),
                            impl->explicitMoves[node].asPtr());
      if (!samePlaceSet(nextOut.asPtr(), impl->movedAt[node].asPtr())) {
        impl->movedAt[node] = zc::mv(nextOut);
        changed = true;
      }
      auto nextOrigins = applyOriginTransfer(
          impl->originIn[node].asPtr(), impl->reinitializes[node].asPtr(),
          impl->explicitMoves[node].asPtr(), BorrowCfgNodeId(static_cast<uint32_t>(node)));
      if (!sameOriginSet(nextOrigins.asPtr(), impl->originOut[node].asPtr())) {
        impl->originOut[node] = zc::mv(nextOrigins);
        changed = true;
      }
    }

    for (size_t i = 0; i < impl->cfg.edgeCount(); ++i) {
      const auto& edge = impl->cfg.getEdge(i);
      for (auto place : impl->movedAt[edge.from.value]) {
        if (addPlaceIfMissing(impl->movedIn[edge.to.value], place)) { changed = true; }
      }
      for (auto origin : impl->originOut[edge.from.value]) {
        if (setOriginIfMissing(impl->originIn[edge.to.value], origin.place, origin.origin)) {
          changed = true;
        }
      }
    }
  }
}

bool BorrowMoveState::isMovedBefore(BorrowCfgNodeId node, PlaceId place) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->movedIn.size()) { return false; }
  return containsPlace(impl->movedIn[node.value].asPtr(), place);
}

bool BorrowMoveState::isMovedAt(BorrowCfgNodeId node, PlaceId place) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->movedAt.size()) { return false; }
  return containsPlace(impl->movedAt[node.value].asPtr(), place);
}

zc::Maybe<BorrowCfgNodeId> BorrowMoveState::getMoveOriginBefore(BorrowCfgNodeId node,
                                                                PlaceId place) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->originIn.size()) {
    return zc::none;
  }
  if (!isMovedBefore(node, place)) { return zc::none; }
  return findOrigin(impl->originIn[node.value].asPtr(), place);
}

zc::Maybe<BorrowCfgNodeId> BorrowMoveState::getMoveOrigin(BorrowCfgNodeId node,
                                                          PlaceId place) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->originOut.size()) {
    return zc::none;
  }
  if (!isMovedAt(node, place)) { return zc::none; }
  return findOrigin(impl->originOut[node.value].asPtr(), place);
}

zc::Maybe<BorrowUseAfterMoveReport> BorrowMoveState::checkUseAfterMoveAt(BorrowCfgNodeId node,
                                                                         PlaceId place) const {
  ZC_IF_SOME(origin, getMoveOrigin(node, place)) {
    return BorrowUseAfterMoveReport{node, place, origin};
  }
  return zc::none;
}

BorrowLinearState::BorrowLinearState(const BorrowCfg& cfg) : impl(zc::heap<Impl>(cfg)) {}

BorrowLinearState::~BorrowLinearState() noexcept(false) = default;

BorrowLinearState::BorrowLinearState(BorrowLinearState&& other) noexcept = default;

BorrowLinearState& BorrowLinearState::operator=(BorrowLinearState&& other) noexcept = default;

void BorrowLinearState::addInitialize(BorrowCfgNodeId node, PlaceId place) {
  ZC_IREQUIRE(node.isValid(), "BorrowLinearState::addInitialize: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->initializes.size(),
              "BorrowLinearState::addInitialize: CFG node out of range");
  addPlaceIfMissing(impl->initializes[node.value], place);
}

void BorrowLinearState::addConsume(BorrowCfgNodeId node, PlaceId place) {
  ZC_IREQUIRE(node.isValid(), "BorrowLinearState::addConsume: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->consumes.size(),
              "BorrowLinearState::addConsume: CFG node out of range");
  addPlaceIfMissing(impl->consumes[node.value], place);
}

void BorrowLinearState::propagate() {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t node = 1; node < impl->outstandingOut.size(); ++node) {
      auto nextOut =
          applyMoveTransfer(impl->outstandingIn[node].asPtr(), impl->consumes[node].asPtr(),
                            impl->initializes[node].asPtr());
      if (!samePlaceSet(nextOut.asPtr(), impl->outstandingOut[node].asPtr())) {
        impl->outstandingOut[node] = zc::mv(nextOut);
        changed = true;
      }
      auto nextOrigins = applyOriginTransfer(
          impl->originIn[node].asPtr(), impl->consumes[node].asPtr(),
          impl->initializes[node].asPtr(), BorrowCfgNodeId(static_cast<uint32_t>(node)));
      if (!sameOriginSet(nextOrigins.asPtr(), impl->originOut[node].asPtr())) {
        impl->originOut[node] = zc::mv(nextOrigins);
        changed = true;
      }
      auto nextConsumed =
          applyConsumedTransfer(impl->consumedIn[node].asPtr(), impl->initializes[node].asPtr(),
                                impl->consumes[node].asPtr());
      if (!samePlaceSet(nextConsumed.asPtr(), impl->consumedOut[node].asPtr())) {
        impl->consumedOut[node] = zc::mv(nextConsumed);
        changed = true;
      }
      auto nextConsumeOrigins = applyConsumeOriginTransfer(
          impl->consumeOriginIn[node].asPtr(), impl->initializes[node].asPtr(),
          impl->consumes[node].asPtr(), BorrowCfgNodeId(static_cast<uint32_t>(node)));
      if (!sameOriginSet(nextConsumeOrigins.asPtr(), impl->consumeOriginOut[node].asPtr())) {
        impl->consumeOriginOut[node] = zc::mv(nextConsumeOrigins);
        changed = true;
      }
    }

    for (size_t i = 0; i < impl->cfg.edgeCount(); ++i) {
      const auto& edge = impl->cfg.getEdge(i);
      for (auto place : impl->outstandingOut[edge.from.value]) {
        if (addPlaceIfMissing(impl->outstandingIn[edge.to.value], place)) { changed = true; }
      }
      for (auto origin : impl->originOut[edge.from.value]) {
        if (setOriginIfMissing(impl->originIn[edge.to.value], origin.place, origin.origin)) {
          changed = true;
        }
      }
      for (auto place : impl->consumedOut[edge.from.value]) {
        if (addPlaceIfMissing(impl->consumedIn[edge.to.value], place)) { changed = true; }
      }
      for (auto origin : impl->consumeOriginOut[edge.from.value]) {
        if (setOriginIfMissing(impl->consumeOriginIn[edge.to.value], origin.place, origin.origin)) {
          changed = true;
        }
      }
    }
  }
}

bool BorrowLinearState::isOutstandingAt(BorrowCfgNodeId node, PlaceId place) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->outstandingOut.size()) {
    return false;
  }
  return containsPlace(impl->outstandingOut[node.value].asPtr(), place);
}

zc::Maybe<BorrowCfgNodeId> BorrowLinearState::getInitializeOrigin(BorrowCfgNodeId node,
                                                                  PlaceId place) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->originOut.size()) {
    return zc::none;
  }
  if (!isOutstandingAt(node, place)) { return zc::none; }
  return findOrigin(impl->originOut[node.value].asPtr(), place);
}

zc::Maybe<BorrowMissingConsumeReport> BorrowLinearState::checkMissingConsumeAt(
    BorrowCfgNodeId node, PlaceId place) const {
  ZC_IF_SOME(origin, getInitializeOrigin(node, place)) {
    return BorrowMissingConsumeReport{node, place, origin};
  }
  return zc::none;
}

zc::Maybe<BorrowDoubleConsumeReport> BorrowLinearState::checkDoubleConsumeAt(BorrowCfgNodeId node,
                                                                             PlaceId place) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->consumeOriginIn.size()) {
    return zc::none;
  }
  if (!containsPlace(impl->consumes[node.value].asPtr(), place)) { return zc::none; }
  ZC_IF_SOME(origin, findOrigin(impl->consumeOriginIn[node.value].asPtr(), place)) {
    return BorrowDoubleConsumeReport{node, place, origin};
  }
  return zc::none;
}

BorrowLoanState::BorrowLoanState(const BorrowCfg& cfg, const BorrowModel& model)
    : impl(zc::heap<Impl>(cfg, model)) {}

BorrowLoanState::~BorrowLoanState() noexcept(false) = default;

BorrowLoanState::BorrowLoanState(BorrowLoanState&& other) noexcept = default;

BorrowLoanState& BorrowLoanState::operator=(BorrowLoanState&& other) noexcept = default;

void BorrowLoanState::addActiveLoan(BorrowCfgNodeId node, LoanId loan) {
  ZC_IREQUIRE(node.isValid(), "BorrowLoanState::addActiveLoan: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->explicitLoans.size(),
              "BorrowLoanState::addActiveLoan: CFG node out of range");
  addLoanIfMissing(impl->explicitLoans[node.value], loan);
}

void BorrowLoanState::addEndLoan(BorrowCfgNodeId node, LoanId loan) {
  ZC_IREQUIRE(node.isValid(), "BorrowLoanState::addEndLoan: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->endLoans.size(),
              "BorrowLoanState::addEndLoan: CFG node out of range");
  addLoanIfMissing(impl->endLoans[node.value], loan);
}

void BorrowLoanState::addSuspendLoan(BorrowCfgNodeId node, LoanId loan) {
  ZC_IREQUIRE(node.isValid(), "BorrowLoanState::addSuspendLoan: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->suspendLoans.size(),
              "BorrowLoanState::addSuspendLoan: CFG node out of range");
  addLoanIfMissing(impl->suspendLoans[node.value], loan);
}

void BorrowLoanState::addResumeLoan(BorrowCfgNodeId node, LoanId loan) {
  ZC_IREQUIRE(node.isValid(), "BorrowLoanState::addResumeLoan: invalid CFG node");
  ZC_IREQUIRE(static_cast<size_t>(node.value) < impl->resumeLoans.size(),
              "BorrowLoanState::addResumeLoan: CFG node out of range");
  addLoanIfMissing(impl->resumeLoans[node.value], loan);
}

void BorrowLoanState::propagate() {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t node = 1; node < impl->activeOut.size(); ++node) {
      auto nextOut = applyActiveLoanTransfer(
          impl->activeIn[node].asPtr(), impl->suspendedIn[node].asPtr(),
          impl->endLoans[node].asPtr(), impl->suspendLoans[node].asPtr(),
          impl->resumeLoans[node].asPtr(), impl->explicitLoans[node].asPtr());
      if (!sameLoanSet(nextOut.asPtr(), impl->activeOut[node].asPtr())) {
        impl->activeOut[node] = zc::mv(nextOut);
        changed = true;
      }
      auto nextSuspended =
          applySuspendedLoanTransfer(impl->activeIn[node].asPtr(), impl->suspendedIn[node].asPtr(),
                                     impl->endLoans[node].asPtr(), impl->suspendLoans[node].asPtr(),
                                     impl->resumeLoans[node].asPtr());
      if (!sameLoanSet(nextSuspended.asPtr(), impl->suspendedOut[node].asPtr())) {
        impl->suspendedOut[node] = zc::mv(nextSuspended);
        changed = true;
      }
    }
    for (size_t i = 0; i < impl->cfg.edgeCount(); ++i) {
      const auto& edge = impl->cfg.getEdge(i);
      for (auto loan : impl->activeOut[edge.from.value]) {
        if (addLoanIfMissing(impl->activeIn[edge.to.value], loan)) { changed = true; }
      }
      for (auto loan : impl->suspendedOut[edge.from.value]) {
        if (addLoanIfMissing(impl->suspendedIn[edge.to.value], loan)) { changed = true; }
      }
    }
  }
}

zc::Maybe<LoanId> BorrowLoanState::findConflictingLoanIdAt(BorrowCfgNodeId node, PlaceId place,
                                                           LoanKind requestedKind,
                                                           FieldOverlapMode fieldMode) const {
  ZC_IF_SOME(report, checkBorrowConflictAt(node, place, requestedKind, fieldMode)) {
    return report.loanId;
  }
  return zc::none;
}

zc::Maybe<BorrowConflictReport> BorrowLoanState::checkBorrowConflictAt(
    BorrowCfgNodeId node, PlaceId place, LoanKind requestedKind, FieldOverlapMode fieldMode) const {
  if (!node.isValid() || static_cast<size_t>(node.value) >= impl->activeOut.size()) {
    return zc::none;
  }
  ZC_IF_SOME(requestedPlace, impl->model.getPlace(place)) {
    for (auto loanId : impl->activeOut[node.value]) {
      ZC_IF_SOME(loan, impl->model.getLoan(loanId)) {
        ZC_IF_SOME(existingPlace, impl->model.getPlace(loan.place)) {
          if (placesOverlap(requestedPlace, existingPlace, fieldMode) &&
              (requestedKind == LoanKind::Mutable || loan.kind == LoanKind::Mutable)) {
            return BorrowConflictReport{node,       place,     requestedKind, loanId,
                                        loan.place, loan.kind, loan.region,   loan.origin};
          }
        }
      }
    }
  }
  return zc::none;
}

zc::Maybe<const Loan&> BorrowLoanState::findConflictingLoanAt(BorrowCfgNodeId node, PlaceId place,
                                                              LoanKind requestedKind,
                                                              FieldOverlapMode fieldMode) const {
  ZC_IF_SOME(loanId, findConflictingLoanIdAt(node, place, requestedKind, fieldMode)) {
    return impl->model.getLoan(loanId);
  }
  return zc::none;
}

zc::Maybe<ast::NodeId> BorrowLoanState::findConflictingLoanOriginAt(
    BorrowCfgNodeId node, PlaceId place, LoanKind requestedKind, FieldOverlapMode fieldMode) const {
  ZC_IF_SOME(loan, findConflictingLoanAt(node, place, requestedKind, fieldMode)) {
    if (loan.origin) { return loan.origin; }
  }
  return zc::none;
}

BorrowLoanBuilder::BorrowLoanBuilder(BorrowModel& model, const ast::Tree& tree,
                                     const BorrowPlaceCollectionResult& places) noexcept
    : impl(zc::heap<Impl>(model, tree, places)) {}

BorrowLoanBuilder::~BorrowLoanBuilder() noexcept(false) = default;

void BorrowLoanBuilder::markMutableBorrow(ast::NodeId borrowExpr) {
  for (auto marked : impl->mutableBorrowExprs) {
    if (marked == borrowExpr) { return; }
  }
  impl->mutableBorrowExprs.add(borrowExpr);
}

void BorrowLoanBuilder::buildFunctionLoans(ast::NodeId functionDecl) {
  buildLoansInBlock(impl->model, impl->tree, impl->places, impl->mutableBorrowExprs.asPtr(),
                    functionBody(impl->tree, functionDecl));
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
