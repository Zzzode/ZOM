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

#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/symbol/symbol-id.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/raw-pointer-type.h"
#include "zomlang/compiler/type/reference-type.h"
#include "zomlang/compiler/type/type-env.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"

namespace zomlang {
namespace compiler {
namespace checker {

namespace {

using tests::TestFixture;

Place makeLocal(uint32_t placeId, uint32_t localId = 1) {
  return Place::local(PlaceId(placeId), localId, type::TypeId(7));
}

class CapturingDiagnosticConsumer final : public diagnostics::DiagnosticConsumer {
public:
  zc::Vector<diagnostics::DiagID> ids;
  zc::Vector<diagnostics::DiagID> childIds;

  void handleDiagnostic(const source::SourceManager&,
                        const diagnostics::Diagnostic& diagnostic) override {
    ids.add(diagnostic.getId());
    for (const auto& child : diagnostic.getChildDiagnostics()) { childIds.add(child->getId()); }
  }
};

bool containsDiagnosticId(zc::ArrayPtr<const diagnostics::DiagID> ids, diagnostics::DiagID id) {
  for (auto emitted : ids) {
    if (emitted == id) return true;
  }
  return false;
}

zc::Own<type::Type> makeConstRawI32PointerType() {
  return zc::heap<type::RawPointerType>(type::PrimitiveType::createI32(), type::Mutability::Const);
}

zc::Own<type::Type> makeSharedI32ReferenceType() {
  return zc::heap<type::ReferenceType>(type::PrimitiveType::createI32(), type::Mutability::Const);
}

}  // namespace

ZC_TEST("BorrowModel.DefaultIdsAreInvalid") {
  ZC_EXPECT(!PlaceId().isValid());
  ZC_EXPECT(!RegionId().isValid());
  ZC_EXPECT(!LoanId().isValid());
  ZC_EXPECT(!MoveId().isValid());

  ZC_EXPECT(PlaceId(1).isValid());
  ZC_EXPECT(RegionId(1).isValid());
  ZC_EXPECT(LoanId(1).isValid());
  ZC_EXPECT(MoveId(1).isValid());
}

ZC_TEST("BorrowModel.PlaceStoresRootAndType") {
  auto place = Place::parameter(PlaceId(1), 3, type::TypeId(9));

  ZC_EXPECT(place.getId() == PlaceId(1));
  ZC_EXPECT(place.getRootKind() == PlaceRootKind::Parameter);
  ZC_EXPECT(place.getRootId() == 3);
  ZC_EXPECT(place.getTypeId() == type::TypeId(9));
  ZC_EXPECT(place.getProjections().size() == 0);
}

ZC_TEST("BorrowModel.PlaceEqualityIncludesProjectionPath") {
  auto lhs = makeLocal(1);
  lhs.addFieldProjection("head"_zc);
  lhs.addIndexProjection(0);

  auto rhs = makeLocal(2);
  rhs.addFieldProjection("head"_zc);
  rhs.addIndexProjection(0);

  auto other = makeLocal(3);
  other.addFieldProjection("tail"_zc);
  other.addIndexProjection(0);

  ZC_EXPECT(lhs.equals(rhs));
  ZC_EXPECT(!lhs.equals(other));
}

ZC_TEST("BorrowModel.DifferentRootsDoNotOverlap") {
  auto lhs = makeLocal(1, 1);
  auto rhs = makeLocal(2, 2);

  ZC_EXPECT(!placesOverlap(lhs, rhs));
}

ZC_TEST("BorrowModel.SameRootOverlaps") {
  auto lhs = makeLocal(1);
  auto rhs = makeLocal(2);

  ZC_EXPECT(placesOverlap(lhs, rhs));
}

ZC_TEST("BorrowModel.FieldPrefixOverlapsNestedField") {
  auto parent = makeLocal(1);
  parent.addFieldProjection("payload"_zc);

  auto child = makeLocal(2);
  child.addFieldProjection("payload"_zc);
  child.addFieldProjection("count"_zc);

  ZC_EXPECT(placesOverlap(parent, child));
  ZC_EXPECT(placesOverlap(child, parent));
}

ZC_TEST("BorrowModel.ProvenDisjointSiblingFieldsDoNotOverlap") {
  auto lhs = makeLocal(1);
  lhs.addFieldProjection("left"_zc);

  auto rhs = makeLocal(2);
  rhs.addFieldProjection("right"_zc);

  ZC_EXPECT(!placesOverlap(lhs, rhs, FieldOverlapMode::ProvenDisjoint));
}

ZC_TEST("BorrowModel.ConservativeSiblingFieldsMayOverlap") {
  auto lhs = makeLocal(1);
  lhs.addFieldProjection("left"_zc);

  auto rhs = makeLocal(2);
  rhs.addFieldProjection("right"_zc);

  ZC_EXPECT(placesOverlap(lhs, rhs, FieldOverlapMode::Conservative));
}

ZC_TEST("BorrowModel.DerefDivergenceIsConservative") {
  auto lhs = makeLocal(1);
  lhs.addDerefProjection();
  lhs.addFieldProjection("left"_zc);

  auto rhs = makeLocal(2);
  rhs.addFieldProjection("left"_zc);

  ZC_EXPECT(placesOverlap(lhs, rhs));
}

ZC_TEST("BorrowModel.IndexDivergenceIsConservative") {
  auto lhs = makeLocal(1);
  lhs.addIndexProjection(0);

  auto rhs = makeLocal(2);
  rhs.addIndexProjection(1);

  ZC_EXPECT(placesOverlap(lhs, rhs));
}

ZC_TEST("BorrowModel.RegionRecordsParent") {
  auto root = Region::make(RegionId(1), RegionKind::Lexical);
  auto nested = Region::make(RegionId(2), RegionKind::Temporary, root.id);

  ZC_EXPECT(!root.hasParent());
  ZC_EXPECT(nested.hasParent());
  ZC_EXPECT(nested.parent == RegionId(1));
}

ZC_TEST("BorrowModel.LoanRecordsOriginAndPermission") {
  auto loan = Loan::make(LoanId(1), PlaceId(2), LoanKind::Mutable, RegionId(3), ast::NodeId(4));

  ZC_EXPECT(loan.id == LoanId(1));
  ZC_EXPECT(loan.place == PlaceId(2));
  ZC_EXPECT(loan.kind == LoanKind::Mutable);
  ZC_EXPECT(loan.region == RegionId(3));
  ZC_EXPECT(loan.origin == ast::NodeId(4));
}

ZC_TEST("BorrowModel.MoveRecordsOrigin") {
  auto move = Move::make(MoveId(1), PlaceId(2), ast::NodeId(3));

  ZC_EXPECT(move.id == MoveId(1));
  ZC_EXPECT(move.place == PlaceId(2));
  ZC_EXPECT(move.origin == ast::NodeId(3));
}

ZC_TEST("BorrowModel.OwnerAllocatesAndLooksUpPlaces") {
  BorrowModel model;

  auto local = model.addLocalPlace(10, type::TypeId(1));
  auto parameter = model.addParameterPlace(20, type::TypeId(2));
  auto temporary = model.addTemporaryPlace(30, type::TypeId(3));
  auto capture = model.addClosureCapturePlace(40, type::TypeId(4));
  auto ret = model.addReturnSlotPlace(type::TypeId(5));

  ZC_EXPECT(model.placeCount() == 5);
  ZC_EXPECT(local == PlaceId(1));
  ZC_EXPECT(parameter == PlaceId(2));
  ZC_EXPECT(temporary == PlaceId(3));
  ZC_EXPECT(capture == PlaceId(4));
  ZC_EXPECT(ret == PlaceId(5));

  auto place = model.getPlace(parameter);
  ZC_EXPECT(place != zc::none);
  ZC_IF_SOME(p, place) {
    ZC_EXPECT(p.getRootKind() == PlaceRootKind::Parameter);
    ZC_EXPECT(p.getRootId() == 20);
    ZC_EXPECT(p.getTypeId() == type::TypeId(2));
  }

  ZC_EXPECT(model.getPlace(PlaceId(99)) == zc::none);
}

ZC_TEST("BorrowModel.OwnerAppliesProjectionMutations") {
  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));

  model.addFieldProjection(place, "payload"_zc);
  model.addDerefProjection(place);
  model.addIndexProjection(place, 3);

  auto stored = model.getPlace(place);
  ZC_EXPECT(stored != zc::none);
  ZC_IF_SOME(p, stored) {
    auto projections = p.getProjections();
    ZC_EXPECT(projections.size() == 3);
    ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
    ZC_EXPECT(projections[0].name == "payload"_zc);
    ZC_EXPECT(projections[1].kind == PlaceProjectionKind::Deref);
    ZC_EXPECT(projections[2].kind == PlaceProjectionKind::Index);
    ZC_EXPECT(projections[2].index == 3);
  }
}

ZC_TEST("BorrowModel.OwnerAllocatesRegionsLoansAndMoves") {
  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));

  auto rootRegion = model.addRegion(RegionKind::Lexical);
  auto nestedRegion = model.addRegion(RegionKind::Temporary, rootRegion);
  auto loan = model.addLoan(place, LoanKind::Shared, nestedRegion, ast::NodeId(8));
  auto move = model.addMove(place, ast::NodeId(9));

  ZC_EXPECT(model.regionCount() == 2);
  ZC_EXPECT(model.loanCount() == 1);
  ZC_EXPECT(model.moveCount() == 1);

  auto region = model.getRegion(nestedRegion);
  ZC_EXPECT(region != zc::none);
  ZC_IF_SOME(r, region) {
    ZC_EXPECT(r.kind == RegionKind::Temporary);
    ZC_EXPECT(r.parent == rootRegion);
  }

  auto storedLoan = model.getLoan(loan);
  ZC_EXPECT(storedLoan != zc::none);
  ZC_IF_SOME(l, storedLoan) {
    ZC_EXPECT(l.place == place);
    ZC_EXPECT(l.kind == LoanKind::Shared);
    ZC_EXPECT(l.region == nestedRegion);
    ZC_EXPECT(l.origin == ast::NodeId(8));
  }

  auto storedMove = model.getMove(move);
  ZC_EXPECT(storedMove != zc::none);
  ZC_IF_SOME(m, storedMove) {
    ZC_EXPECT(m.place == place);
    ZC_EXPECT(m.origin == ast::NodeId(9));
  }

  ZC_EXPECT(model.getRegion(RegionId(99)) == zc::none);
  ZC_EXPECT(model.getLoan(LoanId(99)) == zc::none);
  ZC_EXPECT(model.getMove(MoveId(99)) == zc::none);
}

ZC_TEST("BorrowModel.RegionOutlivesDirectAndTransitiveChildren") {
  BorrowModel model;
  auto root = model.addRegion(RegionKind::Lexical);
  auto nested = model.addRegion(RegionKind::Temporary, root);
  auto leaf = model.addRegion(RegionKind::Closure, nested);

  ZC_EXPECT(model.regionOutlives(root, root));
  ZC_EXPECT(model.regionOutlives(root, nested));
  ZC_EXPECT(model.regionOutlives(root, leaf));
  ZC_EXPECT(model.regionOutlives(nested, leaf));
  ZC_EXPECT(!model.regionOutlives(leaf, root));
  ZC_EXPECT(!model.regionOutlives(nested, root));
  ZC_EXPECT(!model.regionOutlives(RegionId(99), leaf));
  ZC_EXPECT(!model.regionOutlives(root, RegionId(99)));
}

ZC_TEST("BorrowModel.ReportsRegionEscapeWhenReferentDoesNotOutliveTarget") {
  BorrowModel model;
  auto outer = model.addRegion(RegionKind::Lexical);
  auto inner = model.addRegion(RegionKind::Temporary, outer);

  auto escape = model.checkRegionEscape(outer, inner, ast::NodeId(12), ast::NodeId(34));
  ZC_EXPECT(escape != zc::none);
  ZC_IF_SOME(report, escape) {
    ZC_EXPECT(report.targetRegion == outer);
    ZC_EXPECT(report.referentRegion == inner);
    ZC_EXPECT(report.useNode == ast::NodeId(12));
    ZC_EXPECT(report.referentNode == ast::NodeId(34));
  }

  auto contained = model.checkRegionEscape(inner, outer, ast::NodeId(56), ast::NodeId(78));
  ZC_EXPECT(contained == zc::none);
}

ZC_TEST("BorrowModel.EmitsRegionEscapeDiagnostic") {
  TestFixture fix;
  auto useExpr = fix.makeIdentExpr("escaped"_zc);
  auto referentExpr = fix.makeIdentExpr("local"_zc);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(fix.makeExpressionStatement(useExpr));
  stmts.add(fix.makeExpressionStatement(referentExpr));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  BorrowRegionEscapeReport report{RegionId(1), RegionId(2), useExpr, referentExpr};
  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diags(sourceManager);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diags.addConsumer(zc::mv(consumer));

  emitBorrowRegionEscapeDiagnostic(tree, report, diags);

  ZC_EXPECT(containsDiagnosticId(consumerPtr->ids.asPtr(),
                                 diagnostics::DiagID::BorrowDoesNotLiveLongEnough));
  ZC_EXPECT(
      containsDiagnosticId(consumerPtr->childIds.asPtr(), diagnostics::DiagID::BorrowReferentHere));
  ZC_EXPECT(diags.hasErrors());
}

ZC_TEST("BorrowModel.ReportsScopedTaskCaptureWhenReferentDoesNotOutliveTask") {
  BorrowModel model;
  auto outer = model.addRegion(RegionKind::Lexical);
  auto task = model.addRegion(RegionKind::TaskScope, outer);
  auto local = model.addRegion(RegionKind::Temporary, task);

  auto escape = model.checkScopedTaskCapture(task, local, ast::NodeId(12), ast::NodeId(34));
  ZC_EXPECT(escape != zc::none);
  ZC_IF_SOME(report, escape) {
    ZC_EXPECT(report.taskRegion == task);
    ZC_EXPECT(report.referentRegion == local);
    ZC_EXPECT(report.captureNode == ast::NodeId(12));
    ZC_EXPECT(report.referentNode == ast::NodeId(34));
  }

  auto contained = model.checkScopedTaskCapture(task, outer, ast::NodeId(56), ast::NodeId(78));
  ZC_EXPECT(contained == zc::none);
}

ZC_TEST("BorrowModel.EmitsScopedTaskCaptureDiagnostic") {
  TestFixture fix;
  auto captureExpr = fix.makeIdentExpr("captured"_zc);
  auto referentExpr = fix.makeIdentExpr("local"_zc);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(fix.makeExpressionStatement(captureExpr));
  stmts.add(fix.makeExpressionStatement(referentExpr));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  BorrowScopedTaskCaptureReport report{RegionId(1), RegionId(2), captureExpr, referentExpr};
  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diags(sourceManager);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diags.addConsumer(zc::mv(consumer));

  emitBorrowScopedTaskCaptureDiagnostic(tree, report, diags);

  ZC_EXPECT(
      containsDiagnosticId(consumerPtr->ids.asPtr(), diagnostics::DiagID::ScopedTaskBorrowEscapes));
  ZC_EXPECT(containsDiagnosticId(consumerPtr->childIds.asPtr(),
                                 diagnostics::DiagID::ScopedTaskReferentHere));
  ZC_EXPECT(diags.hasErrors());
}

ZC_TEST("BorrowModel.ReportsUnacknowledgedRawPointerSafeBoundary") {
  BorrowModel model;

  auto missing = model.checkRawPointerBoundary(ast::NodeId(12), false);
  ZC_EXPECT(missing != zc::none);
  ZC_IF_SOME(report, missing) { ZC_EXPECT(report.boundaryNode == ast::NodeId(12)); }

  auto acknowledged = model.checkRawPointerBoundary(ast::NodeId(34), true);
  ZC_EXPECT(acknowledged == zc::none);
}

ZC_TEST("BorrowModel.EmitsRawPointerBoundaryDiagnostic") {
  TestFixture fix;
  auto boundaryExpr = fix.makeIdentExpr("ptr"_zc);
  auto stmt = fix.makeExpressionStatement(boundaryExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(stmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  BorrowRawPointerBoundaryReport report{boundaryExpr};
  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diags(sourceManager);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diags.addConsumer(zc::mv(consumer));

  emitBorrowRawPointerBoundaryDiagnostic(tree, report, diags);

  ZC_EXPECT(containsDiagnosticId(consumerPtr->ids.asPtr(),
                                 diagnostics::DiagID::RawPointerBoundaryRequiresUnsafe));
  ZC_EXPECT(diags.hasErrors());
}

ZC_TEST("BorrowModel.OwnerChecksOverlapByPlaceId") {
  BorrowModel model;
  auto lhs = model.addLocalPlace(1, type::TypeId(1));
  auto rhs = model.addLocalPlace(1, type::TypeId(1));

  model.addFieldProjection(lhs, "left"_zc);
  model.addFieldProjection(rhs, "right"_zc);

  ZC_EXPECT(!model.placesOverlap(lhs, rhs, FieldOverlapMode::ProvenDisjoint));
  ZC_EXPECT(model.placesOverlap(lhs, rhs, FieldOverlapMode::Conservative));
  ZC_EXPECT(!model.placesOverlap(lhs, PlaceId(99)));
}

ZC_TEST("BorrowModel.SharedLoanDoesNotConflictWithSharedLoan") {
  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  model.addLoan(place, LoanKind::Shared, region, ast::NodeId(1));

  ZC_EXPECT(model.findConflictingLoan(place, LoanKind::Shared) == zc::none);
}

ZC_TEST("BorrowModel.MutableLoanConflictsWithSharedLoan") {
  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto loan = model.addLoan(place, LoanKind::Shared, region, ast::NodeId(1));

  auto conflict = model.findConflictingLoan(place, LoanKind::Mutable);
  ZC_EXPECT(conflict != zc::none);
  ZC_IF_SOME(l, conflict) { ZC_EXPECT(l.id == loan); }
}

ZC_TEST("BorrowModel.SharedLoanConflictsWithMutableLoan") {
  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto loan = model.addLoan(place, LoanKind::Mutable, region, ast::NodeId(1));

  auto conflict = model.findConflictingLoan(place, LoanKind::Shared);
  ZC_EXPECT(conflict != zc::none);
  ZC_IF_SOME(l, conflict) { ZC_EXPECT(l.id == loan); }
}

ZC_TEST("BorrowModel.DisjointFieldLoanDoesNotConflict") {
  BorrowModel model;
  auto lhs = model.addLocalPlace(1, type::TypeId(1));
  auto rhs = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);

  model.addFieldProjection(lhs, "left"_zc);
  model.addFieldProjection(rhs, "right"_zc);
  model.addLoan(lhs, LoanKind::Mutable, region, ast::NodeId(1));

  ZC_EXPECT(model.findConflictingLoan(rhs, LoanKind::Mutable, FieldOverlapMode::ProvenDisjoint) ==
            zc::none);
  ZC_EXPECT(model.findConflictingLoan(rhs, LoanKind::Mutable, FieldOverlapMode::Conservative) !=
            zc::none);
}

ZC_TEST("BorrowPlaceBuilder.BuildsTypedParameterAndLocalPlaces") {
  TestFixture fix;
  auto paramA = fix.makeFunctionParamDecl("a"_zc);
  auto paramB = fix.makeFunctionParamDecl("b"_zc);
  zc::Vector<ast::NodeId> params;
  params.add(paramA);
  params.add(paramB);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));

  auto localX = fix.makeBindingPattern("x"_zc);
  auto localY = fix.makeBindingPattern("y"_zc);
  auto declX = fix.makeVariableDeclarator(localX);
  auto declY = fix.makeVariableDeclarator(localY);
  zc::Vector<ast::NodeId> decls;
  decls.add(declX);
  decls.add(declY);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, paramList);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(paramA, type::PrimitiveType::createI32());
  typeEnv.setType(paramB, type::PrimitiveType::createBool());
  typeEnv.setType(localX, type::PrimitiveType::createI32());
  typeEnv.setType(localY, type::PrimitiveType::createBool());
  typeEnv.setType(fn, type::PrimitiveType::createUnit());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  ZC_EXPECT(model.placeCount() == 5);
  ZC_EXPECT(builder.mappedNodeCount() == 5);

  auto returnPlaceId = builder.getPlaceForNode(fn);
  ZC_EXPECT(returnPlaceId != zc::none);
  ZC_IF_SOME(id, returnPlaceId) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::ReturnSlot);
      ZC_EXPECT(p.getRootId() == 0);
      ZC_EXPECT(p.getTypeId().isValid());
    }
  }

  auto aPlaceId = builder.getPlaceForNode(paramA);
  ZC_EXPECT(aPlaceId != zc::none);
  ZC_IF_SOME(id, aPlaceId) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::Parameter);
      ZC_EXPECT(p.getRootId() == 0);
      ZC_EXPECT(p.getTypeId().isValid());
    }
  }

  auto yPlaceId = builder.getPlaceForNode(localY);
  ZC_EXPECT(yPlaceId != zc::none);
  ZC_IF_SOME(id, yPlaceId) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::Local);
      ZC_EXPECT(p.getRootId() == localY.value);
      ZC_EXPECT(p.getTypeId().isValid());
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.SkipsUntypedBindings") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("x"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  ZC_EXPECT(model.placeCount() == 0);
  ZC_EXPECT(builder.mappedNodeCount() == 0);
  ZC_EXPECT(builder.getPlaceForNode(local) == zc::none);
}

ZC_TEST("BorrowPlaceBuilder.UsesBindingMetadataSymbolIdsForPlaceRoots") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl("value"_zc);
  zc::Vector<ast::NodeId> params;
  params.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));

  auto local = fix.makeBindingPattern("local"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, paramList);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  ast::BindingMetadata metadata;
  metadata.resizeFor(tree);
  metadata.setSymbol(param, symbol::SymbolId::create(41));
  metadata.setSymbol(local, symbol::SymbolId::create(42));

  type::TypeEnv typeEnv;
  typeEnv.setType(param, type::PrimitiveType::createI32());
  typeEnv.setType(local, type::PrimitiveType::createBool());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv, metadata);
  builder.buildFunctionPlaces(fn);

  auto paramPlaceId = builder.getPlaceForNode(param);
  ZC_EXPECT(paramPlaceId != zc::none);
  ZC_IF_SOME(id, paramPlaceId) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::Parameter);
      ZC_EXPECT(p.getRootId() == 41);
    }
  }

  auto localPlaceId = builder.getPlaceForNode(local);
  ZC_EXPECT(localPlaceId != zc::none);
  ZC_IF_SOME(id, localPlaceId) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::Local);
      ZC_EXPECT(p.getRootId() == 42);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlacesForExpressions") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(ptrPattern);
  auto arrPattern = fix.makeBindingPattern("arr"_zc);
  auto arrDecl = fix.makeVariableDeclarator(arrPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  decls.add(ptrDecl);
  decls.add(arrDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);

  auto memberExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto derefExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, fix.makeIdentExpr("ptr"_zc));
  auto indexExpr = fix.makeIndexExpr(fix.makeIdentExpr("arr"_zc), fix.makeIntLiteral(0));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(fix.makeExpressionStatement(memberExpr));
  stmts.add(fix.makeExpressionStatement(derefExpr));
  stmts.add(fix.makeExpressionStatement(indexExpr));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(ptrPattern, type::PrimitiveType::createI32());
  typeEnv.setType(arrPattern, type::PrimitiveType::createI32());
  typeEnv.setType(memberExpr, type::PrimitiveType::createI32());
  typeEnv.setType(derefExpr, type::PrimitiveType::createI32());
  typeEnv.setType(indexExpr, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto memberPlace = builder.getPlaceForNode(memberExpr);
  ZC_EXPECT(memberPlace != zc::none);
  ZC_IF_SOME(id, memberPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }

  auto derefPlace = builder.getPlaceForNode(derefExpr);
  ZC_EXPECT(derefPlace != zc::none);
  ZC_IF_SOME(id, derefPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Deref);
    }
  }

  auto indexPlace = builder.getPlaceForNode(indexExpr);
  ZC_EXPECT(indexPlace != zc::none);
  ZC_IF_SOME(id, indexPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Index);
      ZC_EXPECT(projections[0].index == 0);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsNestedProjectionPlacesForExpressions") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(ptrPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  decls.add(ptrDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);

  auto fieldExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto nestedField = fix.makeMemberExpr(fieldExpr, "child"_zc);
  auto derefExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, fix.makeIdentExpr("ptr"_zc));
  auto derefField = fix.makeMemberExpr(derefExpr, "field"_zc);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(fix.makeExpressionStatement(nestedField));
  stmts.add(fix.makeExpressionStatement(derefField));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(ptrPattern, type::PrimitiveType::createI32());
  typeEnv.setType(fieldExpr, type::PrimitiveType::createI32());
  typeEnv.setType(nestedField, type::PrimitiveType::createI32());
  typeEnv.setType(derefExpr, type::PrimitiveType::createI32());
  typeEnv.setType(derefField, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto nestedPlace = builder.getPlaceForNode(nestedField);
  ZC_EXPECT(nestedPlace != zc::none);
  ZC_IF_SOME(id, nestedPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 2);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
      ZC_EXPECT(projections[1].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[1].name == "child"_zc);
    }
  }

  auto derefFieldPlace = builder.getPlaceForNode(derefField);
  ZC_EXPECT(derefFieldPlace != zc::none);
  ZC_IF_SOME(id, derefFieldPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 2);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Deref);
      ZC_EXPECT(projections[1].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[1].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsTemporaryPlaceForTypedRvalueExpression") {
  TestFixture fix;
  auto expr = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, fix.makeIntLiteral(1),
                                 fix.makeIntLiteral(2));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(fix.makeExpressionStatement(expr));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(expr, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto placeId = builder.getPlaceForNode(expr);
  ZC_EXPECT(placeId != zc::none);
  ZC_IF_SOME(id, placeId) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::Temporary);
      ZC_EXPECT(p.getRootId() == expr.value);
      ZC_EXPECT(p.getTypeId().isValid());
      ZC_EXPECT(p.getProjections().size() == 0);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsClosureCapturePlaceForOuterBindingUse") {
  TestFixture fix;
  auto outerPattern = fix.makeBindingPattern("outer"_zc);
  auto outerDecl = fix.makeVariableDeclarator(outerPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(outerDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);

  auto capturedUse = fix.makeIdentExpr("outer"_zc);
  zc::Vector<ast::NodeId> closureStmts;
  closureStmts.add(fix.makeExpressionStatement(capturedUse));
  auto closureBody = fix.makeBlockStmt(fix.makeNodeList(closureStmts.asPtr()));
  auto closure = fix.makeFunctionExpr(closureBody);

  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(fix.makeExpressionStatement(closure));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(outerPattern, type::PrimitiveType::createI32());
  typeEnv.setType(capturedUse, type::PrimitiveType::createI32());
  typeEnv.setType(closure, type::PrimitiveType::createUnit());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto capturePlace = builder.getPlaceForNode(capturedUse);
  ZC_EXPECT(capturePlace != zc::none);
  ZC_IF_SOME(id, capturePlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::ClosureCapture);
      ZC_EXPECT(p.getRootId() == outerPattern.value);
      ZC_EXPECT(p.getTypeId().isValid());
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsClosureCaptureProjectionPlaceForOuterBindingUse") {
  TestFixture fix;
  auto outerPattern = fix.makeBindingPattern("outer"_zc);
  auto outerDecl = fix.makeVariableDeclarator(outerPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(outerDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);

  auto capturedField = fix.makeMemberExpr(fix.makeIdentExpr("outer"_zc), "field"_zc);
  zc::Vector<ast::NodeId> closureStmts;
  closureStmts.add(fix.makeExpressionStatement(capturedField));
  auto closureBody = fix.makeBlockStmt(fix.makeNodeList(closureStmts.asPtr()));
  auto closure = fix.makeFunctionExpr(closureBody);

  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(fix.makeExpressionStatement(closure));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(outerPattern, type::PrimitiveType::createI32());
  typeEnv.setType(capturedField, type::PrimitiveType::createI32());
  typeEnv.setType(closure, type::PrimitiveType::createUnit());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto capturePlace = builder.getPlaceForNode(capturedField);
  ZC_EXPECT(capturePlace != zc::none);
  ZC_IF_SOME(id, capturePlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::ClosureCapture);
      ZC_EXPECT(p.getRootId() == outerPattern.value);
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.DoesNotCaptureShadowedClosureLocal") {
  TestFixture fix;
  auto outerPattern = fix.makeBindingPattern("outer"_zc);
  auto outerDecl = fix.makeVariableDeclarator(outerPattern);
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(outerDecl);
  auto outerDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr()));
  auto outerLet = fix.makeLetStmt(outerDeclList);

  auto innerPattern = fix.makeBindingPattern("outer"_zc);
  auto innerDecl = fix.makeVariableDeclarator(innerPattern);
  zc::Vector<ast::NodeId> innerDecls;
  innerDecls.add(innerDecl);
  auto innerDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(innerDecls.asPtr()));
  auto innerLet = fix.makeLetStmt(innerDeclList);
  auto innerUse = fix.makeIdentExpr("outer"_zc);
  zc::Vector<ast::NodeId> closureStmts;
  closureStmts.add(innerLet);
  closureStmts.add(fix.makeExpressionStatement(innerUse));
  auto closureBody = fix.makeBlockStmt(fix.makeNodeList(closureStmts.asPtr()));
  auto closure = fix.makeFunctionExpr(closureBody);

  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(fix.makeExpressionStatement(closure));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(outerPattern, type::PrimitiveType::createI32());
  typeEnv.setType(innerPattern, type::PrimitiveType::createI32());
  typeEnv.setType(innerUse, type::PrimitiveType::createI32());
  typeEnv.setType(closure, type::PrimitiveType::createUnit());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto maybePlace = builder.getPlaceForNode(innerUse);
  ZC_EXPECT(maybePlace != zc::none);
  ZC_IF_SOME(id, maybePlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      ZC_EXPECT(p.getRootKind() == PlaceRootKind::Local);
      ZC_EXPECT(p.getRootId() == innerPattern.value);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlaceForLetInitializerExpression") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  auto initExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), initExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  decls.add(valueDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(initExpr, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto initPlace = builder.getPlaceForNode(initExpr);
  ZC_EXPECT(initPlace != zc::none);
  ZC_IF_SOME(id, initPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlaceForReturnExpression") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto returnStmt = fix.makeReturnStmt(returnExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(returnExpr, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto returnPlace = builder.getPlaceForNode(returnExpr);
  ZC_EXPECT(returnPlace != zc::none);
  ZC_IF_SOME(id, returnPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlaceForCallArgumentExpression") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto argExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  zc::Vector<ast::NodeId> args;
  args.add(argExpr);
  auto callExpr = fix.makeCallExpr(fix.makeIdentExpr("consume"_zc), fix.makeNodeList(args.asPtr()));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(fix.makeExpressionStatement(callExpr));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(argExpr, type::PrimitiveType::createI32());
  typeEnv.setType(callExpr, type::PrimitiveType::createUnit());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto argPlace = builder.getPlaceForNode(argExpr);
  ZC_EXPECT(argPlace != zc::none);
  ZC_IF_SOME(id, argPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlaceForAssignmentExpression") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  auto targetPattern = fix.makeBindingPattern("target"_zc, true);
  auto targetDecl = fix.makeVariableDeclarator(targetPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  decls.add(targetDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto rhsExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto assignment = fix.makeAssignmentExpr(fix.makeIdentExpr("target"_zc), rhsExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(fix.makeExpressionStatement(assignment));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(targetPattern, type::PrimitiveType::createI32());
  typeEnv.setType(rhsExpr, type::PrimitiveType::createI32());
  typeEnv.setType(assignment, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto rhsPlace = builder.getPlaceForNode(rhsExpr);
  ZC_EXPECT(rhsPlace != zc::none);
  ZC_IF_SOME(id, rhsPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlaceInsideIfBranch") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto branchExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  zc::Vector<ast::NodeId> branchStmts;
  branchStmts.add(fix.makeExpressionStatement(branchExpr));
  auto branchBlock = fix.makeBlockStmt(fix.makeNodeList(branchStmts.asPtr()));
  auto ifStmt = fix.makeIfStmt(fix.makeBoolLiteral(true), branchBlock);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(ifStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(branchExpr, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto branchPlace = builder.getPlaceForNode(branchExpr);
  ZC_EXPECT(branchPlace != zc::none);
  ZC_IF_SOME(id, branchPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlaceInsideWhileBody") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto loopExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  zc::Vector<ast::NodeId> loopStmts;
  loopStmts.add(fix.makeExpressionStatement(loopExpr));
  auto loopBlock = fix.makeBlockStmt(fix.makeNodeList(loopStmts.asPtr()));
  auto whileStmt = fix.makeWhileStmt(fix.makeBoolLiteral(true), loopBlock);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(whileStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(loopExpr, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto loopPlace = builder.getPlaceForNode(loopExpr);
  ZC_EXPECT(loopPlace != zc::none);
  ZC_IF_SOME(id, loopPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceBuilder.BuildsProjectionPlaceInsideMatchArmBody") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto armExpr = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  zc::Vector<ast::NodeId> armStmts;
  armStmts.add(fix.makeExpressionStatement(armExpr));
  auto armBlock = fix.makeBlockStmt(fix.makeNodeList(armStmts.asPtr()));
  auto arm = fix.makeMatchArmStmt(fix.makeWildcardPattern(), armBlock);
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  auto matchStmt = fix.makeMatchStmt(fix.makeBoolLiteral(true), fix.makeNodeList(arms.asPtr()));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(matchStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, type::PrimitiveType::createI32());
  typeEnv.setType(armExpr, type::PrimitiveType::createI32());

  BorrowModel model;
  BorrowPlaceBuilder builder(model, tree, typeEnv);
  builder.buildFunctionPlaces(fn);

  auto armPlace = builder.getPlaceForNode(armExpr);
  ZC_EXPECT(armPlace != zc::none);
  ZC_IF_SOME(id, armPlace) {
    auto place = model.getPlace(id);
    ZC_EXPECT(place != zc::none);
    ZC_IF_SOME(p, place) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
  }
}

ZC_TEST("BorrowPlaceCollection.CollectsTopLevelFunctionPlaces") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl("value"_zc);
  zc::Vector<ast::NodeId> params;
  params.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));

  auto local = fix.makeBindingPattern("local"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, paramList);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, type::PrimitiveType::createUnit());
  typeEnv.setType(param, type::PrimitiveType::createI32());
  typeEnv.setType(local, type::PrimitiveType::createBool());

  auto result = collectBorrowPlaces(tree, typeEnv);

  ZC_EXPECT(result.getModel().placeCount() == 3);
  ZC_EXPECT(result.mappedNodeCount() == 3);
  ZC_EXPECT(result.getPlaceForNode(fn) != zc::none);
  ZC_EXPECT(result.getPlaceForNode(param) != zc::none);
  ZC_EXPECT(result.getPlaceForNode(local) != zc::none);
}

ZC_TEST("BorrowCheckerPhase.CollectsPlacesAndFunctionCfgs") {
  TestFixture fix;
  auto param = fix.makeFunctionParamDecl("value"_zc);
  zc::Vector<ast::NodeId> params;
  params.add(param);
  auto paramList = fix.makeFunctionParamList(fix.makeNodeList(params.asPtr()));

  auto local = fix.makeBindingPattern("local"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, paramList);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, type::PrimitiveType::createUnit());
  typeEnv.setType(param, type::PrimitiveType::createI32());
  typeEnv.setType(local, type::PrimitiveType::createBool());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().placeCount() == 3);
  ZC_EXPECT(result.getPlaces().getPlaceForNode(fn) != zc::none);
  ZC_EXPECT(result.functionCount() == 1);
  ZC_EXPECT(result.getFunctionDecl(0) == fn);
  ZC_EXPECT(result.getFunctionCfg(0).nodeCount() == 4);
}

ZC_TEST("BorrowCheckerPhase.BuildsInitialLoansForReferenceInitializers") {
  TestFixture fix;
  auto sourcePattern = fix.makeBindingPattern("value"_zc);
  auto sourceDecl = fix.makeVariableDeclarator(sourcePattern);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("value"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(sourceDecl);
  decls.add(refDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(sourcePattern, type::PrimitiveType::createI32());
  typeEnv.setType(refPattern, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().loanCount() == 1);
  auto loan = result.getPlaces().getModel().getLoan(LoanId(1));
  ZC_EXPECT(loan != zc::none);
  ZC_IF_SOME(l, loan) {
    ZC_EXPECT(l.kind == LoanKind::Shared);
    ZC_IF_SOME(sourcePlace, result.getPlaces().getPlaceForNode(sourcePattern)) {
      ZC_EXPECT(l.place == sourcePlace);
    }
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsMutableBorrowConflictFromRefMutInitializer") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto sharedBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("value"_zc));
  auto sharedPattern = fix.makeBindingPattern("shared"_zc);
  auto sharedDecl =
      fix.makeVariableDeclarator(sharedPattern, fix.makeNamedTypeExpr("&i32"_zc), sharedBorrow);
  auto mutableBorrow =
      fix.makeUnaryExpr(ast::UnaryOperatorKind::RefMut, fix.makeIdentExpr("value"_zc));
  auto mutablePattern = fix.makeBindingPattern("exclusive"_zc);
  auto mutableDecl = fix.makeVariableDeclarator(
      mutablePattern, fix.makeNamedTypeExpr("&mut i32"_zc), mutableBorrow);

  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  decls.add(sharedDecl);
  decls.add(mutableDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(valueDecl, type::PrimitiveType::createI32());
  typeEnv.setType(sharedDecl, zc::heap<type::NamedType>("RefI32"_zc));
  typeEnv.setType(sharedBorrow, zc::heap<type::NamedType>("RefI32"_zc));
  typeEnv.setType(mutableDecl, zc::heap<type::NamedType>("MutRefI32"_zc));
  typeEnv.setType(mutableBorrow, zc::heap<type::NamedType>("MutRefI32"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionBorrowConflictReportCount(0) == 1);
  const auto& report = result.getFunctionBorrowConflictReport(0, 0);
  ZC_EXPECT(report.requestedKind == LoanKind::Mutable);
  ZC_EXPECT(report.loanKind == LoanKind::Shared);
  ZC_EXPECT(report.origin == sharedBorrow);
}

ZC_TEST("BorrowCheckerPhase.EndsBlockScopedBorrowBeforeLaterMutableBorrow") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> valueDecls;
  valueDecls.add(valueDecl);
  auto valueLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(valueDecls.asPtr())));

  auto sharedBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("value"_zc));
  auto sharedPattern = fix.makeBindingPattern("shared"_zc);
  auto sharedDecl = fix.makeVariableDeclarator(sharedPattern, ast::NodeId(), sharedBorrow);
  zc::Vector<ast::NodeId> innerDecls;
  innerDecls.add(sharedDecl);
  auto innerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(innerDecls.asPtr())));
  zc::Vector<ast::NodeId> innerStmts;
  innerStmts.add(innerLet);
  auto innerBlock = fix.makeBlockStmt(fix.makeNodeList(innerStmts.asPtr()));

  auto mutableBorrow =
      fix.makeUnaryExpr(ast::UnaryOperatorKind::RefMut, fix.makeIdentExpr("value"_zc));
  auto mutablePattern = fix.makeBindingPattern("exclusive"_zc);
  auto mutableDecl = fix.makeVariableDeclarator(mutablePattern, ast::NodeId(), mutableBorrow);
  zc::Vector<ast::NodeId> mutableDecls;
  mutableDecls.add(mutableDecl);
  auto mutableLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(mutableDecls.asPtr())));

  zc::Vector<ast::NodeId> stmts;
  stmts.add(valueLet);
  stmts.add(innerBlock);
  stmts.add(mutableLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(sharedPattern, makeSharedI32ReferenceType());
  typeEnv.setType(sharedBorrow, makeSharedI32ReferenceType());
  typeEnv.setType(mutablePattern, makeSharedI32ReferenceType());
  typeEnv.setType(mutableBorrow, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().loanCount() == 2);
  ZC_EXPECT(result.functionBorrowConflictReportCount(0) == 0);
}

ZC_TEST("BorrowCheckerPhase.ReportsMoveOutOfBorrowFromInitializer") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("owned"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(refDecl);
  decls.add(sinkDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionMoveOutOfBorrowReportCount(0) == 1);
  const auto& report = result.getFunctionMoveOutOfBorrowReport(0, 0);
  ZC_EXPECT(report.moveNode == BorrowCfgNodeId(3));
  ZC_EXPECT(report.borrowOrigin == borrowExpr);
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(report.place == ownedPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.EndsBlockScopedBorrowBeforeOuterMove") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(ownedDecl);
  auto outerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr())));

  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("owned"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> innerDecls;
  innerDecls.add(refDecl);
  auto innerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(innerDecls.asPtr())));
  zc::Vector<ast::NodeId> innerStmts;
  innerStmts.add(innerLet);
  auto innerBlock = fix.makeBlockStmt(fix.makeNodeList(innerStmts.asPtr()));

  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> sinkDecls;
  sinkDecls.add(sinkDecl);
  auto sinkLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(sinkDecls.asPtr())));

  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(innerBlock);
  stmts.add(sinkLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().loanCount() == 1);
  ZC_EXPECT(result.functionMoveOutOfBorrowReportCount(0) == 0);
}

ZC_TEST("BorrowCheckerPhase.EndsIfBranchBorrowBeforeOuterMove") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(ownedDecl);
  auto outerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr())));

  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("owned"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> branchDecls;
  branchDecls.add(refDecl);
  auto branchLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(branchDecls.asPtr())));
  zc::Vector<ast::NodeId> branchStmts;
  branchStmts.add(branchLet);
  auto branchBlock = fix.makeBlockStmt(fix.makeNodeList(branchStmts.asPtr()));
  auto ifStmt = fix.makeIfStmt(fix.makeBoolLiteral(true), branchBlock);

  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> sinkDecls;
  sinkDecls.add(sinkDecl);
  auto sinkLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(sinkDecls.asPtr())));

  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(ifStmt);
  stmts.add(sinkLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().loanCount() == 1);
  ZC_EXPECT(result.functionMoveOutOfBorrowReportCount(0) == 0);
}

ZC_TEST("BorrowCheckerPhase.EndsWhileBodyBorrowBeforeOuterMove") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(ownedDecl);
  auto outerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr())));

  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("owned"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> loopDecls;
  loopDecls.add(refDecl);
  auto loopLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(loopDecls.asPtr())));
  zc::Vector<ast::NodeId> loopStmts;
  loopStmts.add(loopLet);
  auto loopBlock = fix.makeBlockStmt(fix.makeNodeList(loopStmts.asPtr()));
  auto whileStmt = fix.makeWhileStmt(fix.makeBoolLiteral(true), loopBlock);

  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> sinkDecls;
  sinkDecls.add(sinkDecl);
  auto sinkLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(sinkDecls.asPtr())));

  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(whileStmt);
  stmts.add(sinkLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().loanCount() == 1);
  ZC_EXPECT(result.functionMoveOutOfBorrowReportCount(0) == 0);
}

ZC_TEST("BorrowCheckerPhase.EndsMatchArmBorrowBeforeOuterMove") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(ownedDecl);
  auto outerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr())));

  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("owned"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> armDecls;
  armDecls.add(refDecl);
  auto armLet = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(armDecls.asPtr())));
  zc::Vector<ast::NodeId> armStmts;
  armStmts.add(armLet);
  auto armBlock = fix.makeBlockStmt(fix.makeNodeList(armStmts.asPtr()));
  auto arm = fix.makeMatchArmStmt(fix.makeWildcardPattern(), armBlock);
  zc::Vector<ast::NodeId> arms;
  arms.add(arm);
  auto matchStmt = fix.makeMatchStmt(fix.makeBoolLiteral(true), fix.makeNodeList(arms.asPtr()));

  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> sinkDecls;
  sinkDecls.add(sinkDecl);
  auto sinkLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(sinkDecls.asPtr())));

  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(matchStmt);
  stmts.add(sinkLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().loanCount() == 1);
  ZC_EXPECT(result.functionMoveOutOfBorrowReportCount(0) == 0);
}

ZC_TEST("BorrowCheckerPhase.EndsCallArgumentBorrowBeforeOuterMove") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(ownedDecl);
  auto outerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr())));

  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("owned"_zc));
  zc::Vector<ast::NodeId> args;
  args.add(borrowExpr);
  auto call = fix.makeCallExpr(fix.makeIdentExpr("use_ref"_zc), fix.makeNodeList(args.asPtr()));
  auto callStmt = fix.makeExpressionStatement(call);

  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> sinkDecls;
  sinkDecls.add(sinkDecl);
  auto sinkLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(sinkDecls.asPtr())));

  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(callStmt);
  stmts.add(sinkLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(call, type::PrimitiveType::createUnit());
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.getPlaces().getModel().loanCount() == 1);
  ZC_EXPECT(result.functionMoveOutOfBorrowReportCount(0) == 0);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefOutsideUnsafe") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  auto derefStmt = fix.makeExpressionStatement(deref);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(derefStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInLetInitializer") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), deref);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInReturnValue") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  auto returnStmt = fix.makeReturnStmt(deref);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, ast::NodeId(), fix.makeNamedTypeExpr("i32"_zc));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, type::PrimitiveType::createI32());
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInConditionalExpression") {
  TestFixture fix;
  auto flagPattern = fix.makeBindingPattern("flag"_zc);
  auto flagDecl = fix.makeVariableDeclarator(flagPattern, fix.makeNamedTypeExpr("bool"_zc));
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto flagUse = fix.makeIdentExpr("flag"_zc);
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  auto conditional = fix.makeConditionalExpr(flagUse, deref, fix.makeIntLiteral(0));
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), conditional);

  zc::Vector<ast::NodeId> decls;
  decls.add(flagDecl);
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(flagPattern, type::PrimitiveType::createBool());
  typeEnv.setType(flagUse, type::PrimitiveType::createBool());
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());
  typeEnv.setType(conditional, type::PrimitiveType::createI32());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInNullCoalesceExpression") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto fallbackPattern = fix.makeBindingPattern("fallback"_zc);
  auto fallbackDecl = fix.makeVariableDeclarator(fallbackPattern, fix.makeNamedTypeExpr("i32"_zc));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto fallbackUse = fix.makeIdentExpr("fallback"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  auto coalesce = fix.makeNullCoalesceExpr(deref, fallbackUse);
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), coalesce);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(fallbackDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(fallbackPattern, type::PrimitiveType::createI32());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(fallbackUse, type::PrimitiveType::createI32());
  typeEnv.setType(deref, type::PrimitiveType::createI32());
  typeEnv.setType(coalesce, type::PrimitiveType::createI32());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInIsExpression") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  auto isExpr = fix.makeIsExpr(deref, fix.makeNamedTypeExpr("i32"_zc));
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), isExpr);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());
  typeEnv.setType(isExpr, type::PrimitiveType::createBool());
  typeEnv.setType(valuePattern, type::PrimitiveType::createBool());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInArrayLiteral") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  zc::Vector<ast::NodeId> elems;
  elems.add(deref);
  auto array = fix.makeArrayLiteral(fix.makeNodeList(elems.asPtr()));
  auto valuePattern = fix.makeBindingPattern("values"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), array);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInTupleLiteral") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  zc::Vector<ast::NodeId> elems;
  elems.add(deref);
  elems.add(fix.makeIntLiteral(1));
  auto tuple = fix.makeTupleLiteral(fix.makeNodeList(elems.asPtr()));
  auto valuePattern = fix.makeBindingPattern("values"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), tuple);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInCastExpression") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  auto cast = fix.makeCastExpr(deref, fix.makeNamedTypeExpr("i32"_zc));
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), cast);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());
  typeEnv.setType(cast, type::PrimitiveType::createI32());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInObjectLiteral") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("item"_zc, deref));
  auto object = fix.makeObjectLiteral(fix.makeNodeList(props.asPtr()));
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), object);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInStructLiteral") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("x"_zc, deref));
  auto object =
      fix.makeStructLiteralExpr(fix.makeNamedTypeExpr("Point"_zc), fix.makeNodeList(props.asPtr()));
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), object);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.ReportsRawPointerDerefInMemberExpressionObject") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("field"_zc, deref));
  auto object = fix.makeObjectLiteral(fix.makeNodeList(props.asPtr()));
  auto member = fix.makeMemberExpr(object, "field"_zc);
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, ast::NodeId(), member);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 1);
  const auto& report = result.getFunctionRawPointerBoundaryReport(0, 0);
  ZC_EXPECT(report.boundaryNode == deref);
}

ZC_TEST("BorrowCheckerPhase.AcceptsRawPointerDerefInsideUnsafe") {
  TestFixture fix;
  auto ptrPattern = fix.makeBindingPattern("ptr"_zc);
  auto ptrDecl = fix.makeVariableDeclarator(
      ptrPattern, fix.makeRawPointerTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto ptrUse = fix.makeIdentExpr("ptr"_zc);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, ptrUse);
  zc::Vector<ast::NodeId> unsafeStmts;
  unsafeStmts.add(fix.makeExpressionStatement(deref));
  auto unsafeBody = fix.makeBlockStmt(fix.makeNodeList(unsafeStmts.asPtr()));
  auto unsafeExpr = fix.makeUnsafeBlockExpr(unsafeBody);
  auto unsafeStmt = fix.makeExpressionStatement(unsafeExpr);

  zc::Vector<ast::NodeId> decls;
  decls.add(ptrDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(unsafeStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ptrPattern, makeConstRawI32PointerType());
  typeEnv.setType(ptrUse, makeConstRawI32PointerType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());
  typeEnv.setType(unsafeExpr, type::PrimitiveType::createUnit());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRawPointerBoundaryReportCount(0) == 0);
}

ZC_TEST("BorrowCheckerPhase.InfersMoveAndReinitializeFactsFromTypedAssignments") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc, true);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto replacementPattern = fix.makeBindingPattern("replacement"_zc);
  auto replacementDecl = fix.makeVariableDeclarator(replacementPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(replacementDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto replacementUse = fix.makeIdentExpr("replacement"_zc);
  auto assignment =
      fix.makeAssignmentExpr(fix.makeIdentExpr("owned"_zc), replacementUse,
                             static_cast<uint8_t>(ast::AssignmentOperatorKind::Assign));
  auto assignmentStmt = fix.makeExpressionStatement(assignment);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(assignmentStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(replacementPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(replacementUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(assignment, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionCount() == 1);
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(3), ownedPlace));
    ZC_EXPECT(!result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), ownedPlace));
    ZC_EXPECT(!result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), ownedPlace));
  }
  ZC_IF_SOME(replacementPlace, result.getPlaces().getPlaceForNode(replacementPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), replacementPlace));
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), replacementPlace));
    auto origin = result.getFunctionMoveOrigin(0, BorrowCfgNodeId(5), replacementPlace);
    ZC_EXPECT(origin != zc::none);
    ZC_IF_SOME(node, origin) { ZC_EXPECT(node == BorrowCfgNodeId(4)); }
  }
}

ZC_TEST("BorrowCheckerPhase.InfersMoveFactFromReturnValue") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnUse = fix.makeIdentExpr("owned"_zc);
  auto returnStmt = fix.makeReturnStmt(returnUse);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(returnUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(fn, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), ownedPlace));
    auto origin = result.getFunctionMoveOrigin(0, BorrowCfgNodeId(4), ownedPlace);
    ZC_EXPECT(origin != zc::none);
    ZC_IF_SOME(node, origin) { ZC_EXPECT(node == BorrowCfgNodeId(4)); }
  }
}

ZC_TEST("BorrowCheckerPhase.InfersMoveFactFromCallArgument") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto argUse = fix.makeIdentExpr("owned"_zc);
  zc::Vector<ast::NodeId> args;
  args.add(argUse);
  auto call = fix.makeCallExpr(fix.makeIdentExpr("consume"_zc), fix.makeNodeList(args.asPtr()));
  auto callStmt = fix.makeExpressionStatement(call);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(callStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(argUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(call, type::PrimitiveType::createUnit());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), ownedPlace));
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), ownedPlace));
    auto origin = result.getFunctionMoveOrigin(0, BorrowCfgNodeId(5), ownedPlace);
    ZC_EXPECT(origin != zc::none);
    ZC_IF_SOME(node, origin) { ZC_EXPECT(node == BorrowCfgNodeId(4)); }
  }
}

ZC_TEST("BorrowCheckerPhase.InfersMoveFactFromBinaryOperand") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto otherPattern = fix.makeBindingPattern("other"_zc);
  auto otherDecl = fix.makeVariableDeclarator(otherPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(otherDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto lhsUse = fix.makeIdentExpr("owned"_zc);
  auto rhsUse = fix.makeIdentExpr("other"_zc);
  auto binary = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, lhsUse, rhsUse);
  auto binaryStmt = fix.makeExpressionStatement(binary);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(binaryStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(otherPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(lhsUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(rhsUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(binary, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), ownedPlace));
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), ownedPlace));
    auto origin = result.getFunctionMoveOrigin(0, BorrowCfgNodeId(5), ownedPlace);
    ZC_EXPECT(origin != zc::none);
    ZC_IF_SOME(node, origin) { ZC_EXPECT(node == BorrowCfgNodeId(4)); }
  }
  ZC_IF_SOME(otherPlace, result.getPlaces().getPlaceForNode(otherPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), otherPlace));
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), otherPlace));
  }
}

ZC_TEST("BorrowCheckerPhase.InfersMoveFactFromIndexOperand") {
  TestFixture fix;
  auto arrPattern = fix.makeBindingPattern("arr"_zc);
  auto arrDecl = fix.makeVariableDeclarator(arrPattern);
  auto indexPattern = fix.makeBindingPattern("index"_zc);
  auto indexDecl = fix.makeVariableDeclarator(indexPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(arrDecl);
  decls.add(indexDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto indexUse = fix.makeIdentExpr("index"_zc);
  auto indexed = fix.makeIndexExpr(fix.makeIdentExpr("arr"_zc), indexUse);
  auto indexStmt = fix.makeExpressionStatement(indexed);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(indexStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(arrPattern, zc::heap<type::NamedType>("ArrayOwner"_zc));
  typeEnv.setType(indexPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(indexUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(indexed, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_IF_SOME(indexPlace, result.getPlaces().getPlaceForNode(indexPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), indexPlace));
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), indexPlace));
    auto origin = result.getFunctionMoveOrigin(0, BorrowCfgNodeId(5), indexPlace);
    ZC_EXPECT(origin != zc::none);
    ZC_IF_SOME(node, origin) { ZC_EXPECT(node == BorrowCfgNodeId(4)); }
  }
}

ZC_TEST("BorrowCheckerPhase.InfersMoveFactFromUnaryOperand") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto operand = fix.makeIdentExpr("owned"_zc);
  auto unary = fix.makeUnaryExpr(ast::UnaryOperatorKind::Minus, operand);
  auto unaryStmt = fix.makeExpressionStatement(unary);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(unaryStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(operand, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(unary, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), ownedPlace));
    ZC_EXPECT(result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), ownedPlace));
    auto origin = result.getFunctionMoveOrigin(0, BorrowCfgNodeId(5), ownedPlace);
    ZC_EXPECT(origin != zc::none);
    ZC_IF_SOME(node, origin) { ZC_EXPECT(node == BorrowCfgNodeId(4)); }
  }
}

ZC_TEST("BorrowCheckerPhase.DoesNotInferMoveFactFromReferenceOperand") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto operand = fix.makeIdentExpr("owned"_zc);
  auto reference = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, operand);
  auto refStmt = fix.makeExpressionStatement(reference);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(refStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(operand, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(reference, zc::heap<type::NamedType>("OwnerRef"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(!result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(4), ownedPlace));
    ZC_EXPECT(!result.isFunctionPlaceMovedAt(0, BorrowCfgNodeId(5), ownedPlace));
    ZC_EXPECT(result.getFunctionMoveOrigin(0, BorrowCfgNodeId(5), ownedPlace) == zc::none);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromTypedUseSite") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto useExpr = fix.makeIdentExpr("owned"_zc);
  auto useStmt = fix.makeExpressionStatement(useExpr);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(useStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(useExpr, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(report.place == ownedPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  auto returnStmt = fix.makeReturnStmt(borrowExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == borrowExpr);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningReferenceToLocalField") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern, fix.makeNamedTypeExpr("Owner"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto member = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, member);
  auto returnStmt = fix.makeReturnStmt(borrowExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(objPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(member, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == borrowExpr);
  ZC_EXPECT(report.referentNode == objPattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningReferenceToNestedLocalField") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern, fix.makeNamedTypeExpr("Owner"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto field = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto child = fix.makeMemberExpr(field, "child"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, child);
  auto returnStmt = fix.makeReturnStmt(borrowExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(objPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(field, zc::heap<type::NamedType>("Field"_zc));
  typeEnv.setType(child, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == borrowExpr);
  ZC_EXPECT(report.referentNode == objPattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningLocalReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(
      refPattern, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  decls.add(refDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnValue = fix.makeIdentExpr("ref"_zc);
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningLocalReferenceAliasToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(
      refPattern, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)), borrowExpr);
  auto aliasInit = fix.makeIdentExpr("ref"_zc);
  auto aliasPattern = fix.makeBindingPattern("alias"_zc);
  auto aliasDecl = fix.makeVariableDeclarator(
      aliasPattern, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)), aliasInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  decls.add(refDecl);
  decls.add(aliasDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnValue = fix.makeIdentExpr("alias"_zc);
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(aliasInit, makeSharedI32ReferenceType());
  typeEnv.setType(aliasPattern, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningObjectMemberReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("item"_zc, borrowExpr));
  auto object = fix.makeObjectLiteral(fix.makeNodeList(props.asPtr()));
  auto returnValue = fix.makeMemberExpr(object, "item"_zc);
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningStructLiteralReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  zc::Vector<ast::NodeId> props;
  props.add(fix.makeObjectProperty("item"_zc, borrowExpr));
  auto returnValue =
      fix.makeStructLiteralExpr(fix.makeNamedTypeExpr("Box"_zc), fix.makeNodeList(props.asPtr()));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(), fix.makeNamedTypeExpr("Box"_zc));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, zc::heap<type::NamedType>("Box"_zc));
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, zc::heap<type::NamedType>("Box"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningArrayLiteralReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  zc::Vector<ast::NodeId> elems;
  elems.add(borrowExpr);
  auto returnValue = fix.makeArrayLiteral(fix.makeNodeList(elems.asPtr()));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl(
      "leak"_zc, body, ast::NodeId(),
      fix.makeArrayTypeExpr(fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc))));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningConditionalReferenceToLocal") {
  TestFixture fix;
  auto flagPattern = fix.makeBindingPattern("flag"_zc);
  auto flagDecl = fix.makeVariableDeclarator(flagPattern, fix.makeNamedTypeExpr("bool"_zc));
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> decls;
  decls.add(flagDecl);
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto flagUse = fix.makeIdentExpr("flag"_zc);
  auto thenUse = fix.makeIdentExpr("value"_zc);
  auto elseUse = fix.makeIdentExpr("value"_zc);
  auto thenBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, thenUse);
  auto elseBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, elseUse);
  auto returnValue = fix.makeConditionalExpr(flagUse, thenBorrow, elseBorrow);
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(flagPattern, type::PrimitiveType::createBool());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(flagUse, type::PrimitiveType::createBool());
  typeEnv.setType(thenUse, type::PrimitiveType::createI32());
  typeEnv.setType(elseUse, type::PrimitiveType::createI32());
  typeEnv.setType(thenBorrow, makeSharedI32ReferenceType());
  typeEnv.setType(elseBorrow, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningCastReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  auto referenceTypeExpr = fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc));
  auto returnValue = fix.makeCastExpr(borrowExpr, referenceTypeExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningNullCoalesceReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto primaryUse = fix.makeIdentExpr("value"_zc);
  auto fallbackUse = fix.makeIdentExpr("value"_zc);
  auto primaryBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, primaryUse);
  auto fallbackBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fallbackUse);
  auto returnValue = fix.makeNullCoalesceExpr(primaryBorrow, fallbackBorrow);
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(primaryUse, type::PrimitiveType::createI32());
  typeEnv.setType(fallbackUse, type::PrimitiveType::createI32());
  typeEnv.setType(primaryBorrow, makeSharedI32ReferenceType());
  typeEnv.setType(fallbackBorrow, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningErrorDefaultReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto primaryUse = fix.makeIdentExpr("value"_zc);
  auto fallbackUse = fix.makeIdentExpr("value"_zc);
  auto primaryBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, primaryUse);
  auto fallbackBorrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fallbackUse);
  auto returnValue = fix.makeErrorDefaultExpr(primaryBorrow, fallbackBorrow);
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(primaryUse, type::PrimitiveType::createI32());
  typeEnv.setType(fallbackUse, type::PrimitiveType::createI32());
  typeEnv.setType(primaryBorrow, makeSharedI32ReferenceType());
  typeEnv.setType(fallbackBorrow, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningIndexReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  zc::Vector<ast::NodeId> elems;
  elems.add(borrowExpr);
  auto array = fix.makeArrayLiteral(fix.makeNodeList(elems.asPtr()));
  auto returnValue = fix.makeIndexExpr(array, fix.makeIntLiteral(0));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(array, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningCallReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  zc::Vector<ast::NodeId> args;
  args.add(borrowExpr);
  auto returnValue = fix.makeCallExpr(fix.makeIdentExpr("id"_zc), fix.makeNodeList(args.asPtr()));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningNewExpressionReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  zc::Vector<ast::NodeId> args;
  args.add(borrowExpr);
  auto returnValue = fix.makeNewExpr(fix.makeIdentExpr("Box"_zc), fix.makeNodeList(args.asPtr()));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(), fix.makeNamedTypeExpr("Box"_zc));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningImportCallReferenceToLocal") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  zc::Vector<ast::NodeId> args;
  args.add(fix.makeStrLiteral("x"_zc));
  args.add(borrowExpr);
  auto returnValue = fix.makeImportCallExpr(fix.makeNodeList(args.asPtr()));
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsStoredLocalReferenceEscape") {
  TestFixture fix;
  auto slotPattern = fix.makeBindingPattern("slot"_zc);
  auto slotDecl = fix.makeVariableDeclarator(
      slotPattern, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(slotDecl);
  auto outerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr())),
                      static_cast<uint8_t>(ast::BindingDeclarationKind::Mut));

  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  zc::Vector<ast::NodeId> innerDecls;
  innerDecls.add(valueDecl);
  auto innerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(innerDecls.asPtr())));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  auto assignment = fix.makeAssignmentExpr(fix.makeIdentExpr("slot"_zc), borrowExpr);
  auto assignStmt = fix.makeExpressionStatement(assignment);
  zc::Vector<ast::NodeId> innerStmts;
  innerStmts.add(innerLet);
  innerStmts.add(assignStmt);
  auto innerBlock = fix.makeBlockStmt(fix.makeNodeList(innerStmts.asPtr()));
  auto innerBlockStmt = fix.makeExpressionStatement(innerBlock);

  auto returnValue = fix.makeIdentExpr("slot"_zc);
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(innerBlockStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(slotPattern, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(assignment, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsStoredLocalReferenceAliasEscape") {
  TestFixture fix;
  auto slotPattern = fix.makeBindingPattern("slot"_zc);
  auto slotDecl = fix.makeVariableDeclarator(
      slotPattern, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  zc::Vector<ast::NodeId> outerDecls;
  outerDecls.add(slotDecl);
  auto outerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(outerDecls.asPtr())),
                      static_cast<uint8_t>(ast::BindingDeclarationKind::Mut));

  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(
      refPattern, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)), borrowExpr);
  zc::Vector<ast::NodeId> innerDecls;
  innerDecls.add(valueDecl);
  innerDecls.add(refDecl);
  auto innerLet =
      fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(innerDecls.asPtr())));
  auto slotAssign =
      fix.makeAssignmentExpr(fix.makeIdentExpr("slot"_zc), fix.makeIdentExpr("ref"_zc));
  auto assignStmt = fix.makeExpressionStatement(slotAssign);
  zc::Vector<ast::NodeId> innerStmts;
  innerStmts.add(innerLet);
  innerStmts.add(assignStmt);
  auto innerBlock = fix.makeBlockStmt(fix.makeNodeList(innerStmts.asPtr()));
  auto innerBlockStmt = fix.makeExpressionStatement(innerBlock);

  auto returnValue = fix.makeIdentExpr("slot"_zc);
  auto returnStmt = fix.makeReturnStmt(returnValue);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(outerLet);
  stmts.add(innerBlockStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(slotPattern, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(slotAssign, makeSharedI32ReferenceType());
  typeEnv.setType(returnValue, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == returnValue);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsReturningReborrowOfLocalReference") {
  TestFixture fix;
  auto valuePattern = fix.makeBindingPattern("value"_zc);
  auto valueDecl = fix.makeVariableDeclarator(valuePattern, fix.makeNamedTypeExpr("i32"_zc));
  auto valueUse = fix.makeIdentExpr("value"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, valueUse);
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(
      refPattern, fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(valueDecl);
  decls.add(refDecl);
  auto letStmt = fix.makeLetStmt(fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr())));
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, fix.makeIdentExpr("ref"_zc));
  auto reborrow = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, deref);
  auto returnStmt = fix.makeReturnStmt(reborrow);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("leak"_zc, body, ast::NodeId(),
                                 fix.makeReferenceTypeExpr(fix.makeNamedTypeExpr("i32"_zc)));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, makeSharedI32ReferenceType());
  typeEnv.setType(valuePattern, type::PrimitiveType::createI32());
  typeEnv.setType(valueUse, type::PrimitiveType::createI32());
  typeEnv.setType(borrowExpr, makeSharedI32ReferenceType());
  typeEnv.setType(refPattern, makeSharedI32ReferenceType());
  typeEnv.setType(deref, type::PrimitiveType::createI32());
  typeEnv.setType(reborrow, makeSharedI32ReferenceType());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionRegionEscapeReportCount(0) == 1);
  const auto& report = result.getFunctionRegionEscapeReport(0, 0);
  ZC_EXPECT(report.useNode == reborrow);
  ZC_EXPECT(report.referentNode == valuePattern);
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveWithDeclaratorTypedBindings") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern, fix.makeNamedTypeExpr("Owner"_zc));
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl =
      fix.makeVariableDeclarator(sinkPattern, fix.makeNamedTypeExpr("Owner"_zc), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto useExpr = fix.makeIdentExpr("owned"_zc);
  auto useStmt = fix.makeExpressionStatement(useExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(useStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, ast::NodeId(), fix.makeNamedTypeExpr("unit"_zc));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedDecl, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkDecl, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(useExpr, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveWithIdentifierPatterns") {
  TestFixture fix;
  auto ownedPattern = fix.makeIdentifierPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern, fix.makeNamedTypeExpr("Owner"_zc));
  auto sinkPattern = fix.makeIdentifierPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl =
      fix.makeVariableDeclarator(sinkPattern, fix.makeNamedTypeExpr("Owner"_zc), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto useExpr = fix.makeIdentExpr("owned"_zc);
  auto useStmt = fix.makeExpressionStatement(useExpr);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(useStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body, ast::NodeId(), fix.makeNamedTypeExpr("unit"_zc));
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedDecl, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkDecl, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(useExpr, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
}

ZC_TEST("BorrowCheckerPhase.EmitsUseAfterMoveDiagnosticFromPhaseReport") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto useExpr = fix.makeIdentExpr("owned"_zc);
  auto useStmt = fix.makeExpressionStatement(useExpr);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(useStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(useExpr, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();
  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diags(sourceManager);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diags.addConsumer(zc::mv(consumer));

  const size_t emitted = emitBorrowDiagnostics(tree, result, diags);

  ZC_EXPECT(emitted == 1);
  ZC_EXPECT(containsDiagnosticId(consumerPtr->ids.asPtr(), diagnostics::DiagID::UseAfterMove));
  ZC_EXPECT(
      containsDiagnosticId(consumerPtr->childIds.asPtr(), diagnostics::DiagID::ValueMovedHere));
  ZC_EXPECT(diags.hasErrors());
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromCallArgument") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto argUse = fix.makeIdentExpr("owned"_zc);
  zc::Vector<ast::NodeId> args;
  args.add(argUse);
  auto call = fix.makeCallExpr(fix.makeIdentExpr("consume"_zc), fix.makeNodeList(args.asPtr()));
  auto callStmt = fix.makeExpressionStatement(call);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(callStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(argUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(call, type::PrimitiveType::createUnit());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(report.place == ownedPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromBinaryOperand") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto otherPattern = fix.makeBindingPattern("other"_zc);
  auto otherDecl = fix.makeVariableDeclarator(otherPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(otherDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto lhsUse = fix.makeIdentExpr("owned"_zc);
  auto rhsUse = fix.makeIdentExpr("other"_zc);
  auto binary = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, lhsUse, rhsUse);
  auto binaryStmt = fix.makeExpressionStatement(binary);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(binaryStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(otherPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(lhsUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(rhsUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(binary, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(report.place == ownedPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromIndexOperand") {
  TestFixture fix;
  auto arrPattern = fix.makeBindingPattern("arr"_zc);
  auto arrDecl = fix.makeVariableDeclarator(arrPattern);
  auto indexPattern = fix.makeBindingPattern("index"_zc);
  auto indexDecl = fix.makeVariableDeclarator(indexPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("index"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(arrDecl);
  decls.add(indexDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto indexUse = fix.makeIdentExpr("index"_zc);
  auto indexed = fix.makeIndexExpr(fix.makeIdentExpr("arr"_zc), indexUse);
  auto indexStmt = fix.makeExpressionStatement(indexed);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(indexStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(arrPattern, zc::heap<type::NamedType>("ArrayOwner"_zc));
  typeEnv.setType(indexPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(indexUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(indexed, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(indexPlace, result.getPlaces().getPlaceForNode(indexPattern)) {
    ZC_EXPECT(report.place == indexPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromUnaryOperand") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto operand = fix.makeIdentExpr("owned"_zc);
  auto unary = fix.makeUnaryExpr(ast::UnaryOperatorKind::Minus, operand);
  auto unaryStmt = fix.makeExpressionStatement(unary);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(unaryStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(operand, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(unary, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(report.place == ownedPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromMovedParentMember") {
  TestFixture fix;
  auto objPattern = fix.makeBindingPattern("obj"_zc);
  auto objDecl = fix.makeVariableDeclarator(objPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("obj"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(objDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto memberUse = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto memberStmt = fix.makeExpressionStatement(memberUse);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(memberStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(objPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(memberUse, zc::heap<type::NamedType>("OwnerField"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(memberPlace, result.getPlaces().getPlaceForNode(memberUse)) {
    ZC_EXPECT(report.place == memberPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromReturnValue") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnUse = fix.makeIdentExpr("owned"_zc);
  auto returnStmt = fix.makeReturnStmt(returnUse);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(returnUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(fn, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(report.place == ownedPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.ReportsUseAfterMoveFromAssignmentRhs") {
  TestFixture fix;
  auto ownedPattern = fix.makeBindingPattern("owned"_zc);
  auto ownedDecl = fix.makeVariableDeclarator(ownedPattern);
  auto targetPattern = fix.makeBindingPattern("target"_zc, true);
  auto targetDecl = fix.makeVariableDeclarator(targetPattern);
  auto sinkPattern = fix.makeBindingPattern("sink"_zc);
  auto sinkInit = fix.makeIdentExpr("owned"_zc);
  auto sinkDecl = fix.makeVariableDeclarator(sinkPattern, ast::NodeId(), sinkInit);
  zc::Vector<ast::NodeId> decls;
  decls.add(ownedDecl);
  decls.add(targetDecl);
  decls.add(sinkDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto rhsUse = fix.makeIdentExpr("owned"_zc);
  auto assignment = fix.makeAssignmentExpr(fix.makeIdentExpr("target"_zc), rhsUse);
  auto assignmentStmt = fix.makeExpressionStatement(assignment);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(assignmentStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(ownedPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(targetPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkPattern, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(sinkInit, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(rhsUse, zc::heap<type::NamedType>("Owner"_zc));
  typeEnv.setType(assignment, zc::heap<type::NamedType>("Owner"_zc));

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionUseAfterMoveReportCount(0) == 1);
  const auto& report = result.getFunctionUseAfterMoveReport(0, 0);
  ZC_EXPECT(report.node == BorrowCfgNodeId(4));
  ZC_EXPECT(report.moveOrigin == BorrowCfgNodeId(3));
  ZC_IF_SOME(ownedPlace, result.getPlaces().getPlaceForNode(ownedPattern)) {
    ZC_EXPECT(report.place == ownedPlace);
  }
}

ZC_TEST("BorrowCheckerPhase.BuildsCfgSummaryForFunctionExpressionBody") {
  TestFixture fix;
  auto closureReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> closureStmts;
  closureStmts.add(closureReturn);
  auto closureBody = fix.makeBlockStmt(fix.makeNodeList(closureStmts.asPtr()));
  auto closure = fix.makeFunctionExpr(closureBody);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(fix.makeExpressionStatement(closure));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, type::PrimitiveType::createUnit());
  typeEnv.setType(closure, type::PrimitiveType::createUnit());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  ZC_EXPECT(result.functionCount() == 2);
  bool foundFunction = false;
  bool foundClosure = false;
  for (size_t i = 0; i < result.functionCount(); ++i) {
    if (result.getFunctionDecl(i) == fn) { foundFunction = true; }
    if (result.getFunctionDecl(i) == closure) {
      foundClosure = true;
      ZC_EXPECT(result.getFunctionCfg(i).nodeCount() == 3);
    }
  }
  ZC_EXPECT(foundFunction);
  ZC_EXPECT(foundClosure);
}

ZC_TEST("BorrowCheckerPhase.BuildsCfgSummaryForLambdaExpressionBody") {
  TestFixture fix;
  auto exprBody = fix.makeIntLiteral(1);
  ast::NodePayload payload;
  payload.words[ast::kLambdaExpressionParamsIdWord] = 0;
  payload.words[ast::kLambdaExpressionRetTyWord] = 0;
  payload.words[ast::kLambdaExpressionRaisesTyWord] = 0;
  payload.words[ast::kLambdaExpressionBodyWord] = 0;
  payload.words[ast::kLambdaExpressionExprBodyWord] = exprBody.value;
  auto lambda =
      fix.builder().makeNode(ast::SyntaxKind::LambdaExpression, source::SourceRange(), payload);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(fix.makeExpressionStatement(lambda));
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(fn, type::PrimitiveType::createUnit());
  typeEnv.setType(exprBody, type::PrimitiveType::createI32());
  typeEnv.setType(lambda, type::PrimitiveType::createUnit());

  BorrowCheckerPhase phase(tree, typeEnv);
  auto result = phase.run();

  bool foundLambda = false;
  for (size_t i = 0; i < result.functionCount(); ++i) {
    if (result.getFunctionDecl(i) != lambda) { continue; }
    foundLambda = true;
    ZC_EXPECT(result.getFunctionCfg(i).nodeCount() == 3);
  }
  ZC_EXPECT(foundLambda);
}

ZC_TEST("BorrowCfg.EmptyFunctionEdgesEntryToExit") {
  TestFixture fix;
  zc::Vector<ast::NodeId> stmts;
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  ZC_EXPECT(cfg.nodeCount() == 2);
  ZC_EXPECT(cfg.edgeCount() == 1);
  ZC_EXPECT(cfg.getNodeKind(cfg.getEntry()) == BorrowCfgNodeKind::Entry);
  ZC_EXPECT(cfg.getNodeKind(cfg.getExit()) == BorrowCfgNodeKind::Exit);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.LinearLetThenReturnEdgesToExit") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("x"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  ZC_EXPECT(cfg.nodeCount() == 4);
  ZC_EXPECT(cfg.edgeCount() == 3);
  auto letNode = BorrowCfgNodeId(3);
  auto retNode = BorrowCfgNodeId(4);
  ZC_EXPECT(cfg.getNodeKind(letNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(letNode) == letStmt);
  ZC_EXPECT(cfg.getNodeKind(retNode) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getNodeAst(retNode) == returnStmt);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == letNode);
  ZC_EXPECT(cfg.getEdge(1).from == letNode);
  ZC_EXPECT(cfg.getEdge(1).to == retNode);
  ZC_EXPECT(cfg.getEdge(2).from == retNode);
  ZC_EXPECT(cfg.getEdge(2).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.ReturnTerminatesStraightLineBlock") {
  TestFixture fix;
  auto returnStmt = fix.makeReturnStmt();
  auto local = fix.makeBindingPattern("after"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letAfterReturn = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(returnStmt);
  stmts.add(letAfterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  ZC_EXPECT(cfg.nodeCount() == 3);
  ZC_EXPECT(cfg.edgeCount() == 2);
  auto retNode = BorrowCfgNodeId(3);
  ZC_EXPECT(cfg.getNodeKind(retNode) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getNodeAst(retNode) == returnStmt);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == retNode);
  ZC_EXPECT(cfg.getEdge(1).from == retNode);
  ZC_EXPECT(cfg.getEdge(1).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.IfThenElseCreatesBranchAndJoin") {
  TestFixture fix;
  auto thenLocal = fix.makeBindingPattern("thenValue"_zc);
  auto thenDecl = fix.makeVariableDeclarator(thenLocal);
  zc::Vector<ast::NodeId> thenDecls;
  thenDecls.add(thenDecl);
  auto thenDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(thenDecls.asPtr()));
  auto thenLet = fix.makeLetStmt(thenDeclList);

  auto elseLocal = fix.makeBindingPattern("elseValue"_zc);
  auto elseDecl = fix.makeVariableDeclarator(elseLocal);
  zc::Vector<ast::NodeId> elseDecls;
  elseDecls.add(elseDecl);
  auto elseDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(elseDecls.asPtr()));
  auto elseLet = fix.makeLetStmt(elseDeclList);

  auto cond = fix.makeBoolLiteral(true);
  auto ifStmt = fix.makeIfStmt(cond, thenLet, elseLet);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(ifStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto thenNode = BorrowCfgNodeId(5);
  auto elseNode = BorrowCfgNodeId(6);
  auto retNode = BorrowCfgNodeId(7);

  ZC_EXPECT(cfg.nodeCount() == 7);
  ZC_EXPECT(cfg.edgeCount() == 7);
  ZC_EXPECT(cfg.getNodeKind(branch) == BorrowCfgNodeKind::Branch);
  ZC_EXPECT(cfg.getNodeAst(branch) == ifStmt);
  ZC_EXPECT(cfg.getNodeKind(join) == BorrowCfgNodeKind::Join);
  ZC_EXPECT(cfg.getNodeKind(thenNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(thenNode) == thenLet);
  ZC_EXPECT(cfg.getNodeKind(elseNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(elseNode) == elseLet);
  ZC_EXPECT(cfg.getNodeKind(retNode) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == thenNode);
  ZC_EXPECT(cfg.getEdge(2).from == thenNode);
  ZC_EXPECT(cfg.getEdge(2).to == join);
  ZC_EXPECT(cfg.getEdge(3).from == branch);
  ZC_EXPECT(cfg.getEdge(3).to == elseNode);
  ZC_EXPECT(cfg.getEdge(4).from == elseNode);
  ZC_EXPECT(cfg.getEdge(4).to == join);
  ZC_EXPECT(cfg.getEdge(5).from == join);
  ZC_EXPECT(cfg.getEdge(5).to == retNode);
  ZC_EXPECT(cfg.getEdge(6).from == retNode);
  ZC_EXPECT(cfg.getEdge(6).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.IfWithoutElseFallsThroughToJoin") {
  TestFixture fix;
  auto returnStmt = fix.makeReturnStmt();
  auto cond = fix.makeBoolLiteral(true);
  auto ifStmt = fix.makeIfStmt(cond, returnStmt);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(ifStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto thenReturn = BorrowCfgNodeId(5);
  auto after = BorrowCfgNodeId(6);

  ZC_EXPECT(cfg.nodeCount() == 6);
  ZC_EXPECT(cfg.edgeCount() == 6);
  ZC_EXPECT(cfg.getNodeKind(branch) == BorrowCfgNodeKind::Branch);
  ZC_EXPECT(cfg.getNodeKind(join) == BorrowCfgNodeKind::Join);
  ZC_EXPECT(cfg.getNodeKind(thenReturn) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == thenReturn);
  ZC_EXPECT(cfg.getEdge(2).from == thenReturn);
  ZC_EXPECT(cfg.getEdge(2).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(3).from == branch);
  ZC_EXPECT(cfg.getEdge(3).to == join);
  ZC_EXPECT(cfg.getEdge(4).from == join);
  ZC_EXPECT(cfg.getEdge(4).to == after);
  ZC_EXPECT(cfg.getEdge(5).from == after);
  ZC_EXPECT(cfg.getEdge(5).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.WhileCreatesBackEdgeAndExitJoin") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("item"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto bodyLet = fix.makeLetStmt(declList);
  auto cond = fix.makeBoolLiteral(true);
  auto whileStmt = fix.makeWhileStmt(cond, bodyLet);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(whileStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto bodyNode = BorrowCfgNodeId(5);
  auto after = BorrowCfgNodeId(6);

  ZC_EXPECT(cfg.nodeCount() == 6);
  ZC_EXPECT(cfg.edgeCount() == 6);
  ZC_EXPECT(cfg.getNodeKind(branch) == BorrowCfgNodeKind::Branch);
  ZC_EXPECT(cfg.getNodeAst(branch) == whileStmt);
  ZC_EXPECT(cfg.getNodeKind(join) == BorrowCfgNodeKind::Join);
  ZC_EXPECT(cfg.getNodeKind(bodyNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(bodyNode) == bodyLet);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == join);
  ZC_EXPECT(cfg.getEdge(2).from == branch);
  ZC_EXPECT(cfg.getEdge(2).to == bodyNode);
  ZC_EXPECT(cfg.getEdge(3).from == bodyNode);
  ZC_EXPECT(cfg.getEdge(3).to == branch);
  ZC_EXPECT(cfg.getEdge(4).from == join);
  ZC_EXPECT(cfg.getEdge(4).to == after);
  ZC_EXPECT(cfg.getEdge(5).from == after);
  ZC_EXPECT(cfg.getEdge(5).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.WhileBodyReturnDoesNotCreateBackEdge") {
  TestFixture fix;
  auto bodyReturn = fix.makeReturnStmt();
  auto cond = fix.makeBoolLiteral(true);
  auto whileStmt = fix.makeWhileStmt(cond, bodyReturn);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(whileStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto bodyRet = BorrowCfgNodeId(5);
  auto after = BorrowCfgNodeId(6);

  ZC_EXPECT(cfg.nodeCount() == 6);
  ZC_EXPECT(cfg.edgeCount() == 6);
  ZC_EXPECT(cfg.getNodeKind(branch) == BorrowCfgNodeKind::Branch);
  ZC_EXPECT(cfg.getNodeKind(join) == BorrowCfgNodeKind::Join);
  ZC_EXPECT(cfg.getNodeKind(bodyRet) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == join);
  ZC_EXPECT(cfg.getEdge(2).from == branch);
  ZC_EXPECT(cfg.getEdge(2).to == bodyRet);
  ZC_EXPECT(cfg.getEdge(3).from == bodyRet);
  ZC_EXPECT(cfg.getEdge(3).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(4).from == join);
  ZC_EXPECT(cfg.getEdge(4).to == after);
  ZC_EXPECT(cfg.getEdge(5).from == after);
  ZC_EXPECT(cfg.getEdge(5).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.MatchArmsFlowToJoin") {
  TestFixture fix;
  auto firstLocal = fix.makeBindingPattern("first"_zc);
  auto firstDecl = fix.makeVariableDeclarator(firstLocal);
  zc::Vector<ast::NodeId> firstDecls;
  firstDecls.add(firstDecl);
  auto firstDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(firstDecls.asPtr()));
  auto firstLet = fix.makeLetStmt(firstDeclList);
  auto firstArm = fix.makeMatchArm(fix.makeWildcardPattern(), firstLet);

  auto secondLocal = fix.makeBindingPattern("second"_zc);
  auto secondDecl = fix.makeVariableDeclarator(secondLocal);
  zc::Vector<ast::NodeId> secondDecls;
  secondDecls.add(secondDecl);
  auto secondDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(secondDecls.asPtr()));
  auto secondLet = fix.makeLetStmt(secondDeclList);
  auto secondArm = fix.makeMatchArm(fix.makeWildcardPattern(), secondLet);

  zc::Vector<ast::NodeId> arms;
  arms.add(firstArm);
  arms.add(secondArm);
  auto matchStmt = fix.makeMatchStmt(fix.makeBoolLiteral(true), fix.makeNodeList(arms.asPtr()));
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(matchStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto firstNode = BorrowCfgNodeId(5);
  auto secondNode = BorrowCfgNodeId(6);
  auto after = BorrowCfgNodeId(7);

  ZC_EXPECT(cfg.nodeCount() == 7);
  ZC_EXPECT(cfg.edgeCount() == 7);
  ZC_EXPECT(cfg.getNodeKind(branch) == BorrowCfgNodeKind::Branch);
  ZC_EXPECT(cfg.getNodeAst(branch) == matchStmt);
  ZC_EXPECT(cfg.getNodeKind(join) == BorrowCfgNodeKind::Join);
  ZC_EXPECT(cfg.getNodeKind(firstNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(firstNode) == firstLet);
  ZC_EXPECT(cfg.getNodeKind(secondNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(secondNode) == secondLet);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == firstNode);
  ZC_EXPECT(cfg.getEdge(2).from == firstNode);
  ZC_EXPECT(cfg.getEdge(2).to == join);
  ZC_EXPECT(cfg.getEdge(3).from == branch);
  ZC_EXPECT(cfg.getEdge(3).to == secondNode);
  ZC_EXPECT(cfg.getEdge(4).from == secondNode);
  ZC_EXPECT(cfg.getEdge(4).to == join);
  ZC_EXPECT(cfg.getEdge(5).from == join);
  ZC_EXPECT(cfg.getEdge(5).to == after);
  ZC_EXPECT(cfg.getEdge(6).from == after);
  ZC_EXPECT(cfg.getEdge(6).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.MatchReturnArmDoesNotFlowToJoin") {
  TestFixture fix;
  auto returnArmBody = fix.makeReturnStmt();
  auto returnArm = fix.makeMatchArm(fix.makeWildcardPattern(), returnArmBody);

  auto fallthroughLocal = fix.makeBindingPattern("fallthrough"_zc);
  auto fallthroughDecl = fix.makeVariableDeclarator(fallthroughLocal);
  zc::Vector<ast::NodeId> fallthroughDecls;
  fallthroughDecls.add(fallthroughDecl);
  auto fallthroughDeclList =
      fix.makeVariableDeclaratorList(fix.makeNodeList(fallthroughDecls.asPtr()));
  auto fallthroughLet = fix.makeLetStmt(fallthroughDeclList);
  auto fallthroughArm = fix.makeMatchArm(fix.makeWildcardPattern(), fallthroughLet);

  zc::Vector<ast::NodeId> arms;
  arms.add(returnArm);
  arms.add(fallthroughArm);
  auto matchStmt = fix.makeMatchStmt(fix.makeBoolLiteral(true), fix.makeNodeList(arms.asPtr()));
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(matchStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto returnNode = BorrowCfgNodeId(5);
  auto fallthroughNode = BorrowCfgNodeId(6);
  auto after = BorrowCfgNodeId(7);

  ZC_EXPECT(cfg.nodeCount() == 7);
  ZC_EXPECT(cfg.edgeCount() == 7);
  ZC_EXPECT(cfg.getNodeKind(returnNode) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getNodeAst(returnNode) == returnArmBody);
  ZC_EXPECT(cfg.getNodeKind(fallthroughNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(fallthroughNode) == fallthroughLet);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == returnNode);
  ZC_EXPECT(cfg.getEdge(2).from == returnNode);
  ZC_EXPECT(cfg.getEdge(2).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(3).from == branch);
  ZC_EXPECT(cfg.getEdge(3).to == fallthroughNode);
  ZC_EXPECT(cfg.getEdge(4).from == fallthroughNode);
  ZC_EXPECT(cfg.getEdge(4).to == join);
  ZC_EXPECT(cfg.getEdge(5).from == join);
  ZC_EXPECT(cfg.getEdge(5).to == after);
  ZC_EXPECT(cfg.getEdge(6).from == after);
  ZC_EXPECT(cfg.getEdge(6).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.BreakInWhileTargetsLoopJoin") {
  TestFixture fix;
  ast::NodePayload breakPayload;
  auto breakStmt =
      fix.builder().makeNode(ast::SyntaxKind::BreakStmt, source::SourceRange(), breakPayload);
  auto cond = fix.makeBoolLiteral(true);
  auto whileStmt = fix.makeWhileStmt(cond, breakStmt);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(whileStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto breakNode = BorrowCfgNodeId(5);
  auto after = BorrowCfgNodeId(6);

  ZC_EXPECT(cfg.nodeCount() == 6);
  ZC_EXPECT(cfg.edgeCount() == 6);
  ZC_EXPECT(cfg.getNodeKind(branch) == BorrowCfgNodeKind::Branch);
  ZC_EXPECT(cfg.getNodeKind(join) == BorrowCfgNodeKind::Join);
  ZC_EXPECT(cfg.getNodeKind(breakNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(breakNode) == breakStmt);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == join);
  ZC_EXPECT(cfg.getEdge(2).from == branch);
  ZC_EXPECT(cfg.getEdge(2).to == breakNode);
  ZC_EXPECT(cfg.getEdge(3).from == breakNode);
  ZC_EXPECT(cfg.getEdge(3).to == join);
  ZC_EXPECT(cfg.getEdge(4).from == join);
  ZC_EXPECT(cfg.getEdge(4).to == after);
  ZC_EXPECT(cfg.getEdge(5).from == after);
  ZC_EXPECT(cfg.getEdge(5).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.ContinueInWhileTargetsLoopBranch") {
  TestFixture fix;
  ast::NodePayload continuePayload;
  auto continueStmt = fix.builder().makeNode(ast::SyntaxKind::ContinueStatement,
                                             source::SourceRange(), continuePayload);
  auto cond = fix.makeBoolLiteral(true);
  auto whileStmt = fix.makeWhileStmt(cond, continueStmt);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(whileStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto branch = BorrowCfgNodeId(3);
  auto join = BorrowCfgNodeId(4);
  auto continueNode = BorrowCfgNodeId(5);
  auto after = BorrowCfgNodeId(6);

  ZC_EXPECT(cfg.nodeCount() == 6);
  ZC_EXPECT(cfg.edgeCount() == 6);
  ZC_EXPECT(cfg.getNodeKind(branch) == BorrowCfgNodeKind::Branch);
  ZC_EXPECT(cfg.getNodeKind(join) == BorrowCfgNodeKind::Join);
  ZC_EXPECT(cfg.getNodeKind(continueNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(continueNode) == continueStmt);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == branch);
  ZC_EXPECT(cfg.getEdge(1).from == branch);
  ZC_EXPECT(cfg.getEdge(1).to == join);
  ZC_EXPECT(cfg.getEdge(2).from == branch);
  ZC_EXPECT(cfg.getEdge(2).to == continueNode);
  ZC_EXPECT(cfg.getEdge(3).from == continueNode);
  ZC_EXPECT(cfg.getEdge(3).to == branch);
  ZC_EXPECT(cfg.getEdge(4).from == join);
  ZC_EXPECT(cfg.getEdge(4).to == after);
  ZC_EXPECT(cfg.getEdge(5).from == after);
  ZC_EXPECT(cfg.getEdge(5).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.ErrorPropagateExpressionHasEarlyExitEdge") {
  TestFixture fix;
  auto operand = fix.makeIdentExpr("result"_zc);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, operand);
  auto exprStmt = fix.makeExpressionStatement(propagate);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(exprStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto propagateNode = BorrowCfgNodeId(3);
  auto after = BorrowCfgNodeId(4);

  ZC_EXPECT(cfg.nodeCount() == 4);
  ZC_EXPECT(cfg.edgeCount() == 4);
  ZC_EXPECT(cfg.getNodeKind(propagateNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(propagateNode) == exprStmt);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == propagateNode);
  ZC_EXPECT(cfg.getEdge(1).from == propagateNode);
  ZC_EXPECT(cfg.getEdge(1).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(2).from == propagateNode);
  ZC_EXPECT(cfg.getEdge(2).to == after);
  ZC_EXPECT(cfg.getEdge(3).from == after);
  ZC_EXPECT(cfg.getEdge(3).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.LetInitializerErrorPropagateHasEarlyExitEdge") {
  TestFixture fix;
  auto operand = fix.makeIdentExpr("result"_zc);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, operand);
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local, ast::NodeId(), propagate);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto letNode = BorrowCfgNodeId(3);
  auto after = BorrowCfgNodeId(4);

  ZC_EXPECT(cfg.nodeCount() == 4);
  ZC_EXPECT(cfg.edgeCount() == 4);
  ZC_EXPECT(cfg.getNodeKind(letNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(letNode) == letStmt);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == letNode);
  ZC_EXPECT(cfg.getEdge(1).from == letNode);
  ZC_EXPECT(cfg.getEdge(1).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(2).from == letNode);
  ZC_EXPECT(cfg.getEdge(2).to == after);
  ZC_EXPECT(cfg.getEdge(3).from == after);
  ZC_EXPECT(cfg.getEdge(3).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.CallArgumentErrorPropagateHasEarlyExitEdge") {
  TestFixture fix;
  auto operand = fix.makeIdentExpr("result"_zc);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, operand);
  zc::Vector<ast::NodeId> args;
  args.add(propagate);
  auto call = fix.makeCallExpr(fix.makeIdentExpr("wrap"_zc), fix.makeNodeList(args.asPtr()));
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local, ast::NodeId(), call);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto letNode = BorrowCfgNodeId(3);
  auto after = BorrowCfgNodeId(4);

  ZC_EXPECT(cfg.nodeCount() == 4);
  ZC_EXPECT(cfg.edgeCount() == 4);
  ZC_EXPECT(cfg.getNodeKind(letNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(letNode) == letStmt);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == letNode);
  ZC_EXPECT(cfg.getEdge(1).from == letNode);
  ZC_EXPECT(cfg.getEdge(1).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(2).from == letNode);
  ZC_EXPECT(cfg.getEdge(2).to == after);
  ZC_EXPECT(cfg.getEdge(3).from == after);
  ZC_EXPECT(cfg.getEdge(3).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.BinaryOperandErrorPropagateHasEarlyExitEdge") {
  TestFixture fix;
  auto operand = fix.makeIdentExpr("result"_zc);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, operand);
  auto fallback = fix.makeIntLiteral(1);
  auto binary = fix.makeBinaryExpr(ast::BinaryOperatorKind::Add, propagate, fallback);
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local, ast::NodeId(), binary);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto letNode = BorrowCfgNodeId(3);
  auto after = BorrowCfgNodeId(4);

  ZC_EXPECT(cfg.nodeCount() == 4);
  ZC_EXPECT(cfg.edgeCount() == 4);
  ZC_EXPECT(cfg.getNodeKind(letNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(letNode) == letStmt);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == letNode);
  ZC_EXPECT(cfg.getEdge(1).from == letNode);
  ZC_EXPECT(cfg.getEdge(1).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(2).from == letNode);
  ZC_EXPECT(cfg.getEdge(2).to == after);
  ZC_EXPECT(cfg.getEdge(3).from == after);
  ZC_EXPECT(cfg.getEdge(3).to == cfg.getExit());
}

ZC_TEST("BorrowCfg.AssignmentOperandErrorPropagateHasEarlyExitEdge") {
  TestFixture fix;
  auto target = fix.makeIdentExpr("target"_zc);
  auto operand = fix.makeIdentExpr("result"_zc);
  auto propagate = fix.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, operand);
  auto assignment = fix.makeAssignmentExpr(target, propagate);
  auto assignmentStmt = fix.makeExpressionStatement(assignment);
  auto afterReturn = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(assignmentStmt);
  stmts.add(afterReturn);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto assignmentNode = BorrowCfgNodeId(3);
  auto after = BorrowCfgNodeId(4);

  ZC_EXPECT(cfg.nodeCount() == 4);
  ZC_EXPECT(cfg.edgeCount() == 4);
  ZC_EXPECT(cfg.getNodeKind(assignmentNode) == BorrowCfgNodeKind::Statement);
  ZC_EXPECT(cfg.getNodeAst(assignmentNode) == assignmentStmt);
  ZC_EXPECT(cfg.getNodeKind(after) == BorrowCfgNodeKind::Return);
  ZC_EXPECT(cfg.getEdge(0).from == cfg.getEntry());
  ZC_EXPECT(cfg.getEdge(0).to == assignmentNode);
  ZC_EXPECT(cfg.getEdge(1).from == assignmentNode);
  ZC_EXPECT(cfg.getEdge(1).to == cfg.getExit());
  ZC_EXPECT(cfg.getEdge(2).from == assignmentNode);
  ZC_EXPECT(cfg.getEdge(2).to == after);
  ZC_EXPECT(cfg.getEdge(3).from == after);
  ZC_EXPECT(cfg.getEdge(3).to == cfg.getExit());
}

ZC_TEST("BorrowMoveState.PropagatesMoveToSuccessorNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowMoveState moves(cfg);
  moves.addMove(BorrowCfgNodeId(3), place);
  moves.propagate();

  ZC_EXPECT(moves.isMovedAt(BorrowCfgNodeId(3), place));
  ZC_EXPECT(moves.isMovedAt(BorrowCfgNodeId(4), place));
}

ZC_TEST("BorrowMoveState.DistinguishesMovedBeforeFromMovedAtNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowMoveState moves(cfg);
  moves.addMove(BorrowCfgNodeId(3), place);
  moves.propagate();

  ZC_EXPECT(!moves.isMovedBefore(BorrowCfgNodeId(3), place));
  ZC_EXPECT(moves.isMovedAt(BorrowCfgNodeId(3), place));
  ZC_EXPECT(moves.isMovedBefore(BorrowCfgNodeId(4), place));
}

ZC_TEST("BorrowMoveState.ReportsMoveOriginAtSuccessorNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowMoveState moves(cfg);
  moves.addMove(BorrowCfgNodeId(3), place);
  moves.propagate();

  auto origin = moves.getMoveOrigin(BorrowCfgNodeId(4), place);
  ZC_EXPECT(origin != zc::none);
  ZC_IF_SOME(node, origin) { ZC_EXPECT(node == BorrowCfgNodeId(3)); }
}

ZC_TEST("BorrowMoveState.ReportsUseAfterMoveAtSuccessorNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowMoveState moves(cfg);
  moves.addMove(BorrowCfgNodeId(3), place);
  moves.propagate();

  auto report = moves.checkUseAfterMoveAt(BorrowCfgNodeId(4), place);
  ZC_EXPECT(report != zc::none);
  ZC_IF_SOME(use, report) {
    ZC_EXPECT(use.node == BorrowCfgNodeId(4));
    ZC_EXPECT(use.place == place);
    ZC_EXPECT(use.moveOrigin == BorrowCfgNodeId(3));
  }
}

ZC_TEST("BorrowMoveState.ReinitializeClearsMovedStateForSuccessors") {
  TestFixture fix;
  auto firstLocal = fix.makeBindingPattern("first"_zc);
  auto firstDecl = fix.makeVariableDeclarator(firstLocal);
  zc::Vector<ast::NodeId> firstDecls;
  firstDecls.add(firstDecl);
  auto firstDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(firstDecls.asPtr()));
  auto firstLet = fix.makeLetStmt(firstDeclList);

  auto secondLocal = fix.makeBindingPattern("second"_zc);
  auto secondDecl = fix.makeVariableDeclarator(secondLocal);
  zc::Vector<ast::NodeId> secondDecls;
  secondDecls.add(secondDecl);
  auto secondDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(secondDecls.asPtr()));
  auto secondLet = fix.makeLetStmt(secondDeclList);

  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(firstLet);
  stmts.add(secondLet);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowMoveState moves(cfg);
  moves.addMove(BorrowCfgNodeId(3), place);
  moves.addReinitialize(BorrowCfgNodeId(4), place);
  moves.propagate();

  ZC_EXPECT(moves.isMovedAt(BorrowCfgNodeId(3), place));
  ZC_EXPECT(!moves.isMovedAt(BorrowCfgNodeId(4), place));
  ZC_EXPECT(!moves.isMovedAt(BorrowCfgNodeId(5), place));
}

ZC_TEST("BorrowLinearState.ReportsMissingConsumeAtExitNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowLinearState linear(cfg);
  linear.addInitialize(BorrowCfgNodeId(3), place);
  linear.propagate();

  auto report = linear.checkMissingConsumeAt(cfg.getExit(), place);
  ZC_EXPECT(report != zc::none);
  ZC_IF_SOME(missing, report) {
    ZC_EXPECT(missing.node == cfg.getExit());
    ZC_EXPECT(missing.place == place);
    ZC_EXPECT(missing.initializeOrigin == BorrowCfgNodeId(3));
  }
}

ZC_TEST("BorrowLinearState.EmitsMissingConsumeDiagnostic") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowMissingConsumeReport report{cfg.getExit(), PlaceId(7), BorrowCfgNodeId(3)};
  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diags(sourceManager);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diags.addConsumer(zc::mv(consumer));

  emitBorrowMissingConsumeDiagnostic(tree, cfg, report, diags);

  ZC_EXPECT(containsDiagnosticId(consumerPtr->ids.asPtr(), diagnostics::DiagID::LinearNotConsumed));
  ZC_EXPECT(containsDiagnosticId(consumerPtr->childIds.asPtr(),
                                 diagnostics::DiagID::LinearInitializedHere));
  ZC_EXPECT(diags.hasErrors());
}

ZC_TEST("BorrowLinearState.ConsumeClearsOutstandingObligationAtExitNode") {
  TestFixture fix;
  auto firstLocal = fix.makeBindingPattern("first"_zc);
  auto firstDecl = fix.makeVariableDeclarator(firstLocal);
  zc::Vector<ast::NodeId> firstDecls;
  firstDecls.add(firstDecl);
  auto firstDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(firstDecls.asPtr()));
  auto firstLet = fix.makeLetStmt(firstDeclList);

  auto secondLocal = fix.makeBindingPattern("second"_zc);
  auto secondDecl = fix.makeVariableDeclarator(secondLocal);
  zc::Vector<ast::NodeId> secondDecls;
  secondDecls.add(secondDecl);
  auto secondDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(secondDecls.asPtr()));
  auto secondLet = fix.makeLetStmt(secondDeclList);

  zc::Vector<ast::NodeId> stmts;
  stmts.add(firstLet);
  stmts.add(secondLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowLinearState linear(cfg);
  linear.addInitialize(BorrowCfgNodeId(3), place);
  linear.addConsume(BorrowCfgNodeId(4), place);
  linear.propagate();

  auto report = linear.checkMissingConsumeAt(cfg.getExit(), place);
  ZC_EXPECT(report == zc::none);
}

ZC_TEST("BorrowLinearState.ReportsDoubleConsumeAtSuccessorNode") {
  TestFixture fix;
  auto firstLocal = fix.makeBindingPattern("first"_zc);
  auto firstDecl = fix.makeVariableDeclarator(firstLocal);
  zc::Vector<ast::NodeId> firstDecls;
  firstDecls.add(firstDecl);
  auto firstDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(firstDecls.asPtr()));
  auto firstLet = fix.makeLetStmt(firstDeclList);

  auto secondLocal = fix.makeBindingPattern("second"_zc);
  auto secondDecl = fix.makeVariableDeclarator(secondLocal);
  zc::Vector<ast::NodeId> secondDecls;
  secondDecls.add(secondDecl);
  auto secondDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(secondDecls.asPtr()));
  auto secondLet = fix.makeLetStmt(secondDeclList);

  auto thirdLocal = fix.makeBindingPattern("third"_zc);
  auto thirdDecl = fix.makeVariableDeclarator(thirdLocal);
  zc::Vector<ast::NodeId> thirdDecls;
  thirdDecls.add(thirdDecl);
  auto thirdDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(thirdDecls.asPtr()));
  auto thirdLet = fix.makeLetStmt(thirdDeclList);

  zc::Vector<ast::NodeId> stmts;
  stmts.add(firstLet);
  stmts.add(secondLet);
  stmts.add(thirdLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  auto place = PlaceId(7);
  BorrowLinearState linear(cfg);
  linear.addInitialize(BorrowCfgNodeId(3), place);
  linear.addConsume(BorrowCfgNodeId(4), place);
  linear.addConsume(BorrowCfgNodeId(5), place);
  linear.propagate();

  auto report = linear.checkDoubleConsumeAt(BorrowCfgNodeId(5), place);
  ZC_EXPECT(report != zc::none);
  ZC_IF_SOME(doubleConsume, report) {
    ZC_EXPECT(doubleConsume.node == BorrowCfgNodeId(5));
    ZC_EXPECT(doubleConsume.place == place);
    ZC_EXPECT(doubleConsume.consumeOrigin == BorrowCfgNodeId(4));
  }
}

ZC_TEST("BorrowLinearState.EmitsDoubleConsumeDiagnostic") {
  TestFixture fix;
  auto firstLocal = fix.makeBindingPattern("first"_zc);
  auto firstDecl = fix.makeVariableDeclarator(firstLocal);
  zc::Vector<ast::NodeId> firstDecls;
  firstDecls.add(firstDecl);
  auto firstDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(firstDecls.asPtr()));
  auto firstLet = fix.makeLetStmt(firstDeclList);

  auto secondLocal = fix.makeBindingPattern("second"_zc);
  auto secondDecl = fix.makeVariableDeclarator(secondLocal);
  zc::Vector<ast::NodeId> secondDecls;
  secondDecls.add(secondDecl);
  auto secondDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(secondDecls.asPtr()));
  auto secondLet = fix.makeLetStmt(secondDeclList);

  zc::Vector<ast::NodeId> stmts;
  stmts.add(firstLet);
  stmts.add(secondLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowDoubleConsumeReport report{BorrowCfgNodeId(4), PlaceId(7), BorrowCfgNodeId(3)};
  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diags(sourceManager);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diags.addConsumer(zc::mv(consumer));

  emitBorrowDoubleConsumeDiagnostic(tree, cfg, report, diags);

  ZC_EXPECT(
      containsDiagnosticId(consumerPtr->ids.asPtr(), diagnostics::DiagID::LinearConsumedTwice));
  ZC_EXPECT(containsDiagnosticId(consumerPtr->childIds.asPtr(),
                                 diagnostics::DiagID::LinearFirstConsumedHere));
  ZC_EXPECT(diags.hasErrors());
}

ZC_TEST("BorrowLoanState.PropagatesSharedLoanConflictToSuccessorNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto loan = model.addLoan(place, LoanKind::Shared, region, ast::NodeId(1));

  BorrowLoanState loans(cfg, model);
  loans.addActiveLoan(BorrowCfgNodeId(3), loan);
  loans.propagate();

  auto conflict = loans.findConflictingLoanAt(BorrowCfgNodeId(4), place, LoanKind::Mutable);
  ZC_EXPECT(conflict != zc::none);
  ZC_IF_SOME(activeLoan, conflict) { ZC_EXPECT(activeLoan.id == loan); }
}

ZC_TEST("BorrowLoanState.ReportsConflictLoanOriginAtSuccessorNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto origin = ast::NodeId(42);
  auto loan = model.addLoan(place, LoanKind::Shared, region, origin);

  BorrowLoanState loans(cfg, model);
  loans.addActiveLoan(BorrowCfgNodeId(3), loan);
  loans.propagate();

  auto conflictOrigin =
      loans.findConflictingLoanOriginAt(BorrowCfgNodeId(4), place, LoanKind::Mutable);
  ZC_EXPECT(conflictOrigin != zc::none);
  ZC_IF_SOME(found, conflictOrigin) { ZC_EXPECT(found == origin); }
}

ZC_TEST("BorrowLoanState.ReportsConflictLoanIdAtSuccessorNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto loan = model.addLoan(place, LoanKind::Shared, region, ast::NodeId(1));

  BorrowLoanState loans(cfg, model);
  loans.addActiveLoan(BorrowCfgNodeId(3), loan);
  loans.propagate();

  auto conflictLoan = loans.findConflictingLoanIdAt(BorrowCfgNodeId(4), place, LoanKind::Mutable);
  ZC_EXPECT(conflictLoan != zc::none);
  ZC_IF_SOME(found, conflictLoan) { ZC_EXPECT(found == loan); }
}

ZC_TEST("BorrowLoanState.ReportsStructuredConflictAtSuccessorNode") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto origin = ast::NodeId(42);
  auto loan = model.addLoan(place, LoanKind::Shared, region, origin);

  BorrowLoanState loans(cfg, model);
  loans.addActiveLoan(BorrowCfgNodeId(3), loan);
  loans.propagate();

  auto conflict = loans.checkBorrowConflictAt(BorrowCfgNodeId(4), place, LoanKind::Mutable);
  ZC_EXPECT(conflict != zc::none);
  ZC_IF_SOME(report, conflict) {
    ZC_EXPECT(report.node == BorrowCfgNodeId(4));
    ZC_EXPECT(report.requestedPlace == place);
    ZC_EXPECT(report.requestedKind == LoanKind::Mutable);
    ZC_EXPECT(report.loanId == loan);
    ZC_EXPECT(report.loanPlace == place);
    ZC_EXPECT(report.loanKind == LoanKind::Shared);
    ZC_EXPECT(report.region == region);
    ZC_EXPECT(report.origin == origin);
  }
}

ZC_TEST("BorrowLoanState.EmitsMutableBorrowConflictDiagnostic") {
  TestFixture fix;
  auto local = fix.makeBindingPattern("value"_zc);
  auto decl = fix.makeVariableDeclarator(local);
  zc::Vector<ast::NodeId> decls;
  decls.add(decl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto origin = letStmt;
  auto loan = model.addLoan(place, LoanKind::Shared, region, origin);

  BorrowLoanState loans(cfg, model);
  loans.addActiveLoan(BorrowCfgNodeId(3), loan);
  loans.propagate();

  auto conflict = loans.checkBorrowConflictAt(BorrowCfgNodeId(4), place, LoanKind::Mutable);
  ZC_EXPECT(conflict != zc::none);

  source::SourceManager sourceManager;
  diagnostics::DiagnosticEngine diags(sourceManager);
  auto consumer = zc::heap<CapturingDiagnosticConsumer>();
  auto consumerPtr = consumer.get();
  diags.addConsumer(zc::mv(consumer));

  ZC_IF_SOME(report, conflict) { emitBorrowConflictDiagnostic(tree, cfg, report, diags); }

  ZC_EXPECT(
      containsDiagnosticId(consumerPtr->ids.asPtr(), diagnostics::DiagID::MutableBorrowConflicts));
  ZC_EXPECT(
      containsDiagnosticId(consumerPtr->childIds.asPtr(), diagnostics::DiagID::BorrowOriginHere));
  ZC_EXPECT(diags.hasErrors());
}

ZC_TEST("BorrowLoanState.EndLoanClearsConflictForSuccessorNode") {
  TestFixture fix;
  auto firstLocal = fix.makeBindingPattern("first"_zc);
  auto firstDecl = fix.makeVariableDeclarator(firstLocal);
  zc::Vector<ast::NodeId> firstDecls;
  firstDecls.add(firstDecl);
  auto firstDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(firstDecls.asPtr()));
  auto firstLet = fix.makeLetStmt(firstDeclList);

  auto secondLocal = fix.makeBindingPattern("second"_zc);
  auto secondDecl = fix.makeVariableDeclarator(secondLocal);
  zc::Vector<ast::NodeId> secondDecls;
  secondDecls.add(secondDecl);
  auto secondDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(secondDecls.asPtr()));
  auto secondLet = fix.makeLetStmt(secondDeclList);

  auto returnStmt = fix.makeReturnStmt();
  zc::Vector<ast::NodeId> stmts;
  stmts.add(firstLet);
  stmts.add(secondLet);
  stmts.add(returnStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto loan = model.addLoan(place, LoanKind::Shared, region, ast::NodeId(1));

  BorrowLoanState loans(cfg, model);
  loans.addActiveLoan(BorrowCfgNodeId(3), loan);
  loans.addEndLoan(BorrowCfgNodeId(4), loan);
  loans.propagate();

  auto conflictAtEnd = loans.findConflictingLoanAt(BorrowCfgNodeId(4), place, LoanKind::Mutable);
  auto conflictAfterEnd = loans.findConflictingLoanAt(BorrowCfgNodeId(5), place, LoanKind::Mutable);
  ZC_EXPECT(conflictAtEnd == zc::none);
  ZC_EXPECT(conflictAfterEnd == zc::none);
}

ZC_TEST("BorrowLoanState.SuspendAndResumeLoanUpdatesConflicts") {
  TestFixture fix;
  auto firstLocal = fix.makeBindingPattern("first"_zc);
  auto firstDecl = fix.makeVariableDeclarator(firstLocal);
  zc::Vector<ast::NodeId> firstDecls;
  firstDecls.add(firstDecl);
  auto firstDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(firstDecls.asPtr()));
  auto firstLet = fix.makeLetStmt(firstDeclList);

  auto secondLocal = fix.makeBindingPattern("second"_zc);
  auto secondDecl = fix.makeVariableDeclarator(secondLocal);
  zc::Vector<ast::NodeId> secondDecls;
  secondDecls.add(secondDecl);
  auto secondDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(secondDecls.asPtr()));
  auto secondLet = fix.makeLetStmt(secondDeclList);

  auto thirdLocal = fix.makeBindingPattern("third"_zc);
  auto thirdDecl = fix.makeVariableDeclarator(thirdLocal);
  zc::Vector<ast::NodeId> thirdDecls;
  thirdDecls.add(thirdDecl);
  auto thirdDeclList = fix.makeVariableDeclaratorList(fix.makeNodeList(thirdDecls.asPtr()));
  auto thirdLet = fix.makeLetStmt(thirdDeclList);

  zc::Vector<ast::NodeId> stmts;
  stmts.add(firstLet);
  stmts.add(secondLet);
  stmts.add(thirdLet);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));
  auto cfg = buildStraightLineBorrowCfg(tree, fn);

  BorrowModel model;
  auto place = model.addLocalPlace(1, type::TypeId(1));
  auto region = model.addRegion(RegionKind::Lexical);
  auto loan = model.addLoan(place, LoanKind::Mutable, region, ast::NodeId(1));

  BorrowLoanState loans(cfg, model);
  loans.addActiveLoan(BorrowCfgNodeId(3), loan);
  loans.addSuspendLoan(BorrowCfgNodeId(4), loan);
  loans.addResumeLoan(BorrowCfgNodeId(5), loan);
  loans.propagate();

  auto conflictBeforeSuspend =
      loans.findConflictingLoanAt(BorrowCfgNodeId(3), place, LoanKind::Shared);
  auto conflictWhileSuspended =
      loans.findConflictingLoanAt(BorrowCfgNodeId(4), place, LoanKind::Shared);
  auto conflictAfterResume =
      loans.findConflictingLoanAt(BorrowCfgNodeId(5), place, LoanKind::Shared);
  ZC_EXPECT(conflictBeforeSuspend != zc::none);
  ZC_EXPECT(conflictWhileSuspended == zc::none);
  ZC_EXPECT(conflictAfterResume != zc::none);
}

ZC_TEST("BorrowLoanBuilder.CreatesSharedLoanForReferenceInitializer") {
  TestFixture fix;
  auto sourcePattern = fix.makeBindingPattern("value"_zc);
  auto sourceDecl = fix.makeVariableDeclarator(sourcePattern);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("value"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(sourceDecl);
  decls.add(refDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(sourcePattern, type::PrimitiveType::createI32());
  typeEnv.setType(refPattern, type::PrimitiveType::createI32());

  auto places = collectBorrowPlaces(tree, typeEnv);
  BorrowLoanBuilder builder(places.getModel(), tree, places);
  builder.buildFunctionLoans(fn);

  ZC_EXPECT(places.getModel().loanCount() == 1);
  auto loan = places.getModel().getLoan(LoanId(1));
  ZC_EXPECT(loan != zc::none);
  ZC_IF_SOME(l, loan) {
    ZC_EXPECT(l.kind == LoanKind::Shared);
    ZC_IF_SOME(sourcePlace, places.getPlaceForNode(sourcePattern)) {
      ZC_EXPECT(l.place == sourcePlace);
    }
    ZC_EXPECT(l.origin == borrowExpr);
  }
}

ZC_TEST("BorrowLoanBuilder.CreatesSharedLoanForFieldReferenceInitializer") {
  TestFixture fix;
  auto sourcePattern = fix.makeBindingPattern("obj"_zc);
  auto sourceDecl = fix.makeVariableDeclarator(sourcePattern);
  auto member = fix.makeMemberExpr(fix.makeIdentExpr("obj"_zc), "field"_zc);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, member);
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(sourceDecl);
  decls.add(refDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(sourcePattern, type::PrimitiveType::createI32());
  typeEnv.setType(refPattern, type::PrimitiveType::createI32());

  auto places = collectBorrowPlaces(tree, typeEnv);
  BorrowLoanBuilder builder(places.getModel(), tree, places);
  builder.buildFunctionLoans(fn);

  ZC_EXPECT(places.getModel().loanCount() == 1);
  auto loan = places.getModel().getLoan(LoanId(1));
  ZC_EXPECT(loan != zc::none);
  ZC_IF_SOME(l, loan) {
    auto loanPlace = places.getModel().getPlace(l.place);
    ZC_EXPECT(loanPlace != zc::none);
    ZC_IF_SOME(p, loanPlace) {
      ZC_IF_SOME(sourcePlace, places.getPlaceForNode(sourcePattern)) {
        auto source = places.getModel().getPlace(sourcePlace);
        ZC_EXPECT(source != zc::none);
        ZC_IF_SOME(sourcePlaceValue, source) {
          ZC_EXPECT(p.getRootKind() == sourcePlaceValue.getRootKind());
          ZC_EXPECT(p.getRootId() == sourcePlaceValue.getRootId());
        }
      }
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Field);
      ZC_EXPECT(projections[0].name == "field"_zc);
    }
    ZC_EXPECT(l.kind == LoanKind::Shared);
    ZC_EXPECT(l.origin == borrowExpr);
  }
}

ZC_TEST("BorrowLoanBuilder.CreatesSharedLoanForDerefReferenceInitializer") {
  TestFixture fix;
  auto sourcePattern = fix.makeBindingPattern("ptr"_zc);
  auto sourceDecl = fix.makeVariableDeclarator(sourcePattern);
  auto deref = fix.makeUnaryExpr(ast::UnaryOperatorKind::Deref, fix.makeIdentExpr("ptr"_zc));
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, deref);
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(sourceDecl);
  decls.add(refDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(sourcePattern, type::PrimitiveType::createI32());
  typeEnv.setType(refPattern, type::PrimitiveType::createI32());

  auto places = collectBorrowPlaces(tree, typeEnv);
  BorrowLoanBuilder builder(places.getModel(), tree, places);
  builder.buildFunctionLoans(fn);

  ZC_EXPECT(places.getModel().loanCount() == 1);
  auto loan = places.getModel().getLoan(LoanId(1));
  ZC_EXPECT(loan != zc::none);
  ZC_IF_SOME(l, loan) {
    auto loanPlace = places.getModel().getPlace(l.place);
    ZC_EXPECT(loanPlace != zc::none);
    ZC_IF_SOME(p, loanPlace) {
      ZC_IF_SOME(sourcePlace, places.getPlaceForNode(sourcePattern)) {
        auto source = places.getModel().getPlace(sourcePlace);
        ZC_EXPECT(source != zc::none);
        ZC_IF_SOME(sourcePlaceValue, source) {
          ZC_EXPECT(p.getRootKind() == sourcePlaceValue.getRootKind());
          ZC_EXPECT(p.getRootId() == sourcePlaceValue.getRootId());
        }
      }
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Deref);
    }
    ZC_EXPECT(l.kind == LoanKind::Shared);
    ZC_EXPECT(l.origin == borrowExpr);
  }
}

ZC_TEST("BorrowLoanBuilder.CreatesSharedLoanForIndexReferenceInitializer") {
  TestFixture fix;
  auto sourcePattern = fix.makeBindingPattern("arr"_zc);
  auto sourceDecl = fix.makeVariableDeclarator(sourcePattern);
  auto indexExpr = fix.makeIndexExpr(fix.makeIdentExpr("arr"_zc), fix.makeIdentExpr("i"_zc));
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, indexExpr);
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(sourceDecl);
  decls.add(refDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(sourcePattern, type::PrimitiveType::createI32());
  typeEnv.setType(refPattern, type::PrimitiveType::createI32());

  auto places = collectBorrowPlaces(tree, typeEnv);
  BorrowLoanBuilder builder(places.getModel(), tree, places);
  builder.buildFunctionLoans(fn);

  ZC_EXPECT(places.getModel().loanCount() == 1);
  auto loan = places.getModel().getLoan(LoanId(1));
  ZC_EXPECT(loan != zc::none);
  ZC_IF_SOME(l, loan) {
    auto loanPlace = places.getModel().getPlace(l.place);
    ZC_EXPECT(loanPlace != zc::none);
    ZC_IF_SOME(p, loanPlace) {
      auto projections = p.getProjections();
      ZC_EXPECT(projections.size() == 1);
      ZC_EXPECT(projections[0].kind == PlaceProjectionKind::Index);
    }
    ZC_EXPECT(l.kind == LoanKind::Shared);
    ZC_EXPECT(l.origin == borrowExpr);
  }
}

ZC_TEST("BorrowLoanBuilder.CreatesMutableLoanForMarkedReferenceInitializer") {
  TestFixture fix;
  auto sourcePattern = fix.makeBindingPattern("value"_zc);
  auto sourceDecl = fix.makeVariableDeclarator(sourcePattern);
  auto borrowExpr = fix.makeUnaryExpr(ast::UnaryOperatorKind::Ref, fix.makeIdentExpr("value"_zc));
  auto refPattern = fix.makeBindingPattern("ref"_zc);
  auto refDecl = fix.makeVariableDeclarator(refPattern, ast::NodeId(), borrowExpr);
  zc::Vector<ast::NodeId> decls;
  decls.add(sourceDecl);
  decls.add(refDecl);
  auto declList = fix.makeVariableDeclaratorList(fix.makeNodeList(decls.asPtr()));
  auto letStmt = fix.makeLetStmt(declList);
  zc::Vector<ast::NodeId> stmts;
  stmts.add(letStmt);
  auto body = fix.makeBlockStmt(fix.makeNodeList(stmts.asPtr()));
  auto fn = fix.makeFunctionDecl("main"_zc, body);
  auto tree = fix.buildSourceFile("test"_zc, zc::ArrayPtr<const ast::NodeId>(&fn, 1));

  type::TypeEnv typeEnv;
  typeEnv.setType(sourcePattern, type::PrimitiveType::createI32());
  typeEnv.setType(refPattern, type::PrimitiveType::createI32());

  auto places = collectBorrowPlaces(tree, typeEnv);
  BorrowLoanBuilder builder(places.getModel(), tree, places);
  builder.markMutableBorrow(borrowExpr);
  builder.buildFunctionLoans(fn);

  ZC_EXPECT(places.getModel().loanCount() == 1);
  auto loan = places.getModel().getLoan(LoanId(1));
  ZC_EXPECT(loan != zc::none);
  ZC_IF_SOME(l, loan) {
    ZC_EXPECT(l.kind == LoanKind::Mutable);
    ZC_IF_SOME(sourcePlace, places.getPlaceForNode(sourcePattern)) {
      ZC_EXPECT(l.place == sourcePlace);
    }
    ZC_EXPECT(l.origin == borrowExpr);
  }
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang
