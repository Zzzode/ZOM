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

#include "zc/ztest/test.h"
#include "zomlang/compiler/mir/built-mir.h"
#include "zomlang/compiler/ownership/facts/escape.h"
#include "zomlang/compiler/ownership/facts/raw-provenance.h"
#include "zomlang/compiler/ownership/facts/region-key.h"
#include "zomlang/tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang::compiler::ownership::facts {
namespace {

using type::SemanticTypeId;

MirEventKey makeEventKey(uint32_t operandOrdinal = 0) {
  return MirEventKey{MirLocation{identity::DefId{}, MirPoint::entry()}, operandOrdinal};
}

mir::MirPlace makePlace(uint32_t localOrdinal) {
  auto local = mir::MirLocalId::fromOrdinal(localOrdinal);
  ZC_REQUIRE(local != zc::none);
  ZC_IF_SOME(id, local) {
    zc::Vector<mir::MirProjection> projections;
    return mir::MirPlace(id, tests::testSemanticType(), zc::mv(projections),
                         tests::testSemanticType(1));
  }
  ZC_UNREACHABLE
}

MovePathKey makeMovePathKey(uint32_t localOrdinal) {
  return MovePathKey{identity::DefId{}, makePlace(localOrdinal)};
}

LoanKey makeLoanKey(uint32_t operandOrdinal = 0) { return LoanKey{makeEventKey(operandOrdinal)}; }

RawProvenanceCarrierKey makeCarrierKey(uint32_t localOrdinal, uint32_t operandOrdinal = 0) {
  return RawProvenanceCarrierKey{makeEventKey(operandOrdinal), makeMovePathKey(localOrdinal)};
}

RegionKey makeLoanRegion(uint32_t operandOrdinal = 0) {
  return RegionKey::loanRegion(makeLoanKey(operandOrdinal));
}

ReferenceRoot makeReferenceRoot(uint32_t localOrdinal, uint32_t eventOrdinal = 0) {
  return ReferenceRoot{makeLoanRegion(), makeMovePathKey(localOrdinal), makeEventKey(eventOrdinal)};
}

ReferenceOrigin makeReferenceOrigin(uint32_t localOrdinal, uint32_t eventOrdinal = 0,
                                    uint32_t activationOrdinal = 1) {
  return ReferenceOrigin{makeReferenceRoot(localOrdinal, eventOrdinal), makeLoanRegion(),
                         makeEventKey(activationOrdinal)};
}

ReferenceOrigin makeStaticReferenceOrigin(uint32_t localOrdinal, uint32_t eventOrdinal = 0,
                                          uint32_t activationOrdinal = 1) {
  auto staticRegion = RegionKey::staticRegion(identity::DefId{});
  return ReferenceOrigin{ReferenceRoot{staticRegion.clone(), makeMovePathKey(localOrdinal),
                                       makeEventKey(eventOrdinal)},
                         zc::mv(staticRegion), makeEventKey(activationOrdinal)};
}

RegionMembership makeMembership(RegionKey region, OwnershipPoint point) {
  return RegionMembership{zc::mv(region), zc::mv(point)};
}

// ---------------------------------------------------------------------------
// BorrowInputKey
// ---------------------------------------------------------------------------

ZC_TEST("BorrowInputKeyTest.ReceiverEquality") {
  auto first = BorrowInputKey::receiver();
  auto second = BorrowInputKey::receiver();
  ZC_EXPECT(first == second);
  ZC_EXPECT(!(first != second));
}

ZC_TEST("BorrowInputKeyTest.ParameterEquality") {
  auto first = BorrowInputKey::parameter(0);
  auto second = BorrowInputKey::parameter(0);
  auto other = BorrowInputKey::parameter(1);
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
}

ZC_TEST("BorrowInputKeyTest.ReceiverDiffersFromParameter") {
  auto receiver = BorrowInputKey::receiver();
  auto parameter = BorrowInputKey::parameter(0);
  ZC_EXPECT(receiver != parameter);
  ZC_EXPECT(!(receiver == parameter));
}

ZC_TEST("BorrowInputKeyTest.Ordering") {
  auto receiver = BorrowInputKey::receiver();
  auto param0 = BorrowInputKey::parameter(0);
  auto param1 = BorrowInputKey::parameter(1);
  ZC_EXPECT(receiver < param0);
  ZC_EXPECT(param0 < param1);
  ZC_EXPECT(!(param0 < receiver));
  ZC_EXPECT(!(param1 < param0));
}

// ---------------------------------------------------------------------------
// RegionKey
// ---------------------------------------------------------------------------

ZC_TEST("RegionKeyTest.StaticEquality") {
  auto first = RegionKey::staticRegion(identity::DefId{});
  auto second = RegionKey::staticRegion(identity::DefId{});
  ZC_EXPECT(first == second);
  ZC_EXPECT(first.isStatic());
  ZC_EXPECT(!first.isInput());
}

ZC_TEST("RegionKeyTest.InputEquality") {
  auto first = RegionKey::inputRegion(identity::DefId{}, BorrowInputKey::receiver());
  auto second = RegionKey::inputRegion(identity::DefId{}, BorrowInputKey::receiver());
  auto other = RegionKey::inputRegion(identity::DefId{}, BorrowInputKey::parameter(0));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isInput());
}

ZC_TEST("RegionKeyTest.LoanEquality") {
  auto first = RegionKey::loanRegion(makeLoanKey(0));
  auto second = RegionKey::loanRegion(makeLoanKey(0));
  auto other = RegionKey::loanRegion(makeLoanKey(1));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isLoan());
}

ZC_TEST("RegionKeyTest.StorageEquality") {
  auto first = RegionKey::storageRegion(makeEventKey(0), makeMovePathKey(1));
  auto second = RegionKey::storageRegion(makeEventKey(0), makeMovePathKey(1));
  auto other = RegionKey::storageRegion(makeEventKey(1), makeMovePathKey(1));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isStorage());
}

ZC_TEST("RegionKeyTest.LocalValueEquality") {
  auto first = RegionKey::localValueRegion(makeEventKey(0), makeMovePathKey(1));
  auto second = RegionKey::localValueRegion(makeEventKey(0), makeMovePathKey(1));
  auto other = RegionKey::localValueRegion(makeEventKey(0), makeMovePathKey(2));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isLocalValue());
}

ZC_TEST("RegionKeyTest.ClosureValueEquality") {
  auto first = RegionKey::closureValueRegion(makeEventKey(0), makeMovePathKey(1));
  auto second = RegionKey::closureValueRegion(makeEventKey(0), makeMovePathKey(1));
  auto other = RegionKey::closureValueRegion(makeEventKey(1), makeMovePathKey(1));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isClosureValue());
}

ZC_TEST("RegionKeyTest.DifferentVariantsAreNotEqual") {
  auto staticRegion = RegionKey::staticRegion(identity::DefId{});
  auto inputRegion = RegionKey::inputRegion(identity::DefId{}, BorrowInputKey::receiver());
  auto loanRegion = RegionKey::loanRegion(makeLoanKey());
  auto storageRegion = RegionKey::storageRegion(makeEventKey(), makeMovePathKey(1));
  auto localValueRegion = RegionKey::localValueRegion(makeEventKey(), makeMovePathKey(1));
  auto closureValueRegion = RegionKey::closureValueRegion(makeEventKey(), makeMovePathKey(1));
  ZC_EXPECT(staticRegion != inputRegion);
  ZC_EXPECT(inputRegion != loanRegion);
  ZC_EXPECT(loanRegion != storageRegion);
  ZC_EXPECT(storageRegion != localValueRegion);
  ZC_EXPECT(localValueRegion != closureValueRegion);
}

ZC_TEST("RegionKeyTest.TagOrdering") {
  auto staticRegion = RegionKey::staticRegion(identity::DefId{});
  auto inputRegion = RegionKey::inputRegion(identity::DefId{}, BorrowInputKey::receiver());
  auto loanRegion = RegionKey::loanRegion(makeLoanKey());
  auto storageRegion = RegionKey::storageRegion(makeEventKey(), makeMovePathKey(1));
  auto localValueRegion = RegionKey::localValueRegion(makeEventKey(), makeMovePathKey(1));
  auto closureValueRegion = RegionKey::closureValueRegion(makeEventKey(), makeMovePathKey(1));
  ZC_EXPECT(staticRegion < inputRegion);
  ZC_EXPECT(inputRegion < loanRegion);
  ZC_EXPECT(loanRegion < storageRegion);
  ZC_EXPECT(storageRegion < localValueRegion);
  ZC_EXPECT(localValueRegion < closureValueRegion);
}

ZC_TEST("RegionKeyTest.TagsAreCanonical") {
  ZC_EXPECT(RegionKey::staticRegion(identity::DefId{}).tag() == 0x01);
  ZC_EXPECT(RegionKey::inputRegion(identity::DefId{}, BorrowInputKey::receiver()).tag() == 0x02);
  ZC_EXPECT(RegionKey::loanRegion(makeLoanKey()).tag() == 0x03);
  ZC_EXPECT(RegionKey::storageRegion(makeEventKey(), makeMovePathKey(1)).tag() == 0x04);
  ZC_EXPECT(RegionKey::localValueRegion(makeEventKey(), makeMovePathKey(1)).tag() == 0x05);
  ZC_EXPECT(RegionKey::closureValueRegion(makeEventKey(), makeMovePathKey(1)).tag() == 0x06);
}

ZC_TEST("RegionKeyTest.CloneProducesEqualRegion") {
  auto original = RegionKey::storageRegion(makeEventKey(0), makeMovePathKey(1));
  auto cloned = original.clone();
  ZC_EXPECT(original == cloned);
}

// ---------------------------------------------------------------------------
// ReferenceRoot and ReferenceOrigin
// ---------------------------------------------------------------------------

ZC_TEST("ReferenceRootTest.Equality") {
  auto first = makeReferenceRoot(1, 0);
  auto second = makeReferenceRoot(1, 0);
  auto other = makeReferenceRoot(2, 0);
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
}

ZC_TEST("ReferenceRootTest.CloneProducesEqualRoot") {
  auto original = makeReferenceRoot(1, 0);
  auto cloned = original.clone();
  ZC_EXPECT(original == cloned);
}

ZC_TEST("ReferenceOriginTest.Equality") {
  auto first = makeReferenceOrigin(1, 0, 1);
  auto second = makeReferenceOrigin(1, 0, 1);
  auto other = makeReferenceOrigin(1, 0, 2);
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
}

ZC_TEST("ReferenceOriginTest.CloneProducesEqualOrigin") {
  auto original = makeReferenceOrigin(1, 0, 1);
  auto cloned = original.clone();
  ZC_EXPECT(original == cloned);
}

// ---------------------------------------------------------------------------
// EscapeKind
// ---------------------------------------------------------------------------

ZC_TEST("EscapeKindTest.ReturnEquality") {
  auto first = EscapeKind::returnEscape();
  auto second = EscapeKind::returnEscape();
  ZC_EXPECT(first == second);
  ZC_EXPECT(first.isReturn());
  ZC_EXPECT(!first.isStore());
  ZC_EXPECT(!first.isClosureCapture());
}

ZC_TEST("EscapeKindTest.StoreEquality") {
  auto first =
      EscapeKind::storeEscape(makeMovePathKey(1), RegionKey::staticRegion(identity::DefId{}));
  auto second =
      EscapeKind::storeEscape(makeMovePathKey(1), RegionKey::staticRegion(identity::DefId{}));
  auto other =
      EscapeKind::storeEscape(makeMovePathKey(2), RegionKey::staticRegion(identity::DefId{}));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isStore());
}

ZC_TEST("EscapeKindTest.ClosureCaptureEquality") {
  auto first = EscapeKind::closureCaptureEscape(makeMovePathKey(1),
                                                RegionKey::staticRegion(identity::DefId{}));
  auto second = EscapeKind::closureCaptureEscape(makeMovePathKey(1),
                                                 RegionKey::staticRegion(identity::DefId{}));
  auto other = EscapeKind::closureCaptureEscape(makeMovePathKey(2),
                                                RegionKey::staticRegion(identity::DefId{}));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isClosureCapture());
}

ZC_TEST("EscapeKindTest.DifferentKindsAreNotEqual") {
  auto returnKind = EscapeKind::returnEscape();
  auto storeKind =
      EscapeKind::storeEscape(makeMovePathKey(1), RegionKey::staticRegion(identity::DefId{}));
  auto closureKind = EscapeKind::closureCaptureEscape(makeMovePathKey(1),
                                                      RegionKey::staticRegion(identity::DefId{}));
  ZC_EXPECT(returnKind != storeKind);
  ZC_EXPECT(storeKind != closureKind);
  ZC_EXPECT(returnKind != closureKind);
}

ZC_TEST("EscapeKindTest.TagsAreCanonical") {
  ZC_EXPECT(EscapeKind::returnEscape().tag() == 0x01);
  ZC_EXPECT(EscapeKind::storeEscape(makeMovePathKey(1), RegionKey::staticRegion(identity::DefId{}))
                .tag() == 0x02);
  ZC_EXPECT(EscapeKind::closureCaptureEscape(makeMovePathKey(1),
                                             RegionKey::staticRegion(identity::DefId{}))
                .tag() == 0x03);
}

ZC_TEST("EscapeKindTest.CloneProducesEqualKind") {
  auto original =
      EscapeKind::storeEscape(makeMovePathKey(1), RegionKey::staticRegion(identity::DefId{}));
  auto cloned = original.clone();
  ZC_EXPECT(original == cloned);
}

ZC_TEST("EscapeKindTest.CloneProducesEqualClosureCaptureKind") {
  auto original = EscapeKind::closureCaptureEscape(
      makeMovePathKey(1), RegionKey::closureValueRegion(makeEventKey(0), makeMovePathKey(1)));
  auto cloned = original.clone();
  ZC_EXPECT(original == cloned);
  ZC_EXPECT(cloned.isClosureCapture());
}

// ---------------------------------------------------------------------------
// EscapeOriginRoute
// ---------------------------------------------------------------------------

ZC_TEST("EscapeOriginRouteTest.DirectEquality") {
  auto first = EscapeOriginRoute::direct();
  auto second = EscapeOriginRoute::direct();
  ZC_EXPECT(first == second);
  ZC_EXPECT(first.isDirect());
  ZC_EXPECT(!first.isRawCarrier());
}

ZC_TEST("EscapeOriginRouteTest.RawCarrierEquality") {
  auto first = EscapeOriginRoute::rawCarrier(makeCarrierKey(1));
  auto second = EscapeOriginRoute::rawCarrier(makeCarrierKey(1));
  auto other = EscapeOriginRoute::rawCarrier(makeCarrierKey(2));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isRawCarrier());
}

ZC_TEST("EscapeOriginRouteTest.DirectDiffersFromRawCarrier") {
  auto direct = EscapeOriginRoute::direct();
  auto rawCarrier = EscapeOriginRoute::rawCarrier(makeCarrierKey(1));
  ZC_EXPECT(direct != rawCarrier);
}

ZC_TEST("EscapeOriginRouteTest.TagsAreCanonical") {
  ZC_EXPECT(EscapeOriginRoute::direct().tag() == 0x01);
  ZC_EXPECT(EscapeOriginRoute::rawCarrier(makeCarrierKey(1)).tag() == 0x02);
}

ZC_TEST("EscapeOriginRouteTest.CloneProducesEqualRoute") {
  auto original = EscapeOriginRoute::rawCarrier(makeCarrierKey(1));
  auto cloned = original.clone();
  ZC_EXPECT(original == cloned);
}

// ---------------------------------------------------------------------------
// EscapeProof
// ---------------------------------------------------------------------------

ZC_TEST("EscapeProofTest.OwnedEquality") {
  auto first = EscapeProof::owned();
  auto second = EscapeProof::owned();
  ZC_EXPECT(first == second);
  ZC_EXPECT(first.isOwned());
}

ZC_TEST("EscapeProofTest.StaticEquality") {
  auto first = EscapeProof::staticProof();
  auto second = EscapeProof::staticProof();
  ZC_EXPECT(first == second);
  ZC_EXPECT(first.isStatic());
}

ZC_TEST("EscapeProofTest.DirectInputEquality") {
  auto first = EscapeProof::directInput(BorrowInputKey::receiver());
  auto second = EscapeProof::directInput(BorrowInputKey::receiver());
  auto other = EscapeProof::directInput(BorrowInputKey::parameter(0));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isDirectInput());
}

ZC_TEST("EscapeProofTest.ContainedEquality") {
  zc::Vector<OwnershipPoint> points;
  points.add(OwnershipPoint::cfg(MirPoint::entry()));
  auto first = EscapeProof::contained(zc::mv(points));
  zc::Vector<OwnershipPoint> pointsCopy;
  pointsCopy.add(OwnershipPoint::cfg(MirPoint::entry()));
  auto second = EscapeProof::contained(zc::mv(pointsCopy));
  zc::Vector<OwnershipPoint> otherPoints;
  otherPoints.add(OwnershipPoint::cfg(MirPoint::entry()));
  otherPoints.add(OwnershipPoint::cfg(MirPoint::entry()));
  auto other = EscapeProof::contained(zc::mv(otherPoints));
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
  ZC_EXPECT(first.isContained());
}

ZC_TEST("EscapeProofTest.AddressOnlyEquality") {
  auto first = EscapeProof::addressOnly();
  auto second = EscapeProof::addressOnly();
  ZC_EXPECT(first == second);
  ZC_EXPECT(first.isAddressOnly());
}

ZC_TEST("EscapeProofTest.DifferentProofsAreNotEqual") {
  auto owned = EscapeProof::owned();
  auto staticProof = EscapeProof::staticProof();
  auto directInput = EscapeProof::directInput(BorrowInputKey::receiver());
  zc::Vector<OwnershipPoint> points;
  points.add(OwnershipPoint::cfg(MirPoint::entry()));
  auto contained = EscapeProof::contained(zc::mv(points));
  auto addressOnly = EscapeProof::addressOnly();
  ZC_EXPECT(owned != staticProof);
  ZC_EXPECT(staticProof != directInput);
  ZC_EXPECT(directInput != contained);
  ZC_EXPECT(contained != addressOnly);
  ZC_EXPECT(owned != addressOnly);
}

ZC_TEST("EscapeProofTest.TagsAreCanonical") {
  ZC_EXPECT(EscapeProof::owned().tag() == 0x01);
  ZC_EXPECT(EscapeProof::staticProof().tag() == 0x02);
  ZC_EXPECT(EscapeProof::directInput(BorrowInputKey::receiver()).tag() == 0x03);
  zc::Vector<OwnershipPoint> points;
  points.add(OwnershipPoint::cfg(MirPoint::entry()));
  ZC_EXPECT(EscapeProof::contained(zc::mv(points)).tag() == 0x04);
  ZC_EXPECT(EscapeProof::addressOnly().tag() == 0x05);
}

// ---------------------------------------------------------------------------
// staticEscapeProofAdmissible
// ---------------------------------------------------------------------------

ZC_TEST("StaticEscapeProofAdmissibleTest.AcceptsStaticOriginsWithMembership") {
  zc::Vector<EscapeOriginCause> origins;
  origins.add(EscapeOriginCause{makeStaticReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()});
  zc::Vector<RegionMembership> memberships;
  memberships.add(makeMembership(RegionKey::staticRegion(identity::DefId{}),
                                 OwnershipPoint::beforeEvent(makeEventKey(0))));
  ZC_EXPECT(staticEscapeProofAdmissible(origins.asPtr(), memberships.asPtr(), makeEventKey(0)));
}

ZC_TEST("StaticEscapeProofAdmissibleTest.RejectsEmptyOrigins") {
  zc::Vector<EscapeOriginCause> origins;
  zc::Vector<RegionMembership> memberships;
  memberships.add(makeMembership(RegionKey::staticRegion(identity::DefId{}),
                                 OwnershipPoint::beforeEvent(makeEventKey(0))));
  ZC_EXPECT(!staticEscapeProofAdmissible(origins.asPtr(), memberships.asPtr(), makeEventKey(0)));
}

ZC_TEST("StaticEscapeProofAdmissibleTest.RejectsNonStaticRoot") {
  zc::Vector<EscapeOriginCause> origins;
  origins.add(EscapeOriginCause{makeReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()});
  zc::Vector<RegionMembership> memberships;
  memberships.add(makeMembership(makeLoanRegion(), OwnershipPoint::beforeEvent(makeEventKey(0))));
  ZC_EXPECT(!staticEscapeProofAdmissible(origins.asPtr(), memberships.asPtr(), makeEventKey(0)));
}

ZC_TEST("StaticEscapeProofAdmissibleTest.RejectsMissingMembership") {
  zc::Vector<EscapeOriginCause> origins;
  origins.add(EscapeOriginCause{makeStaticReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()});
  zc::Vector<RegionMembership> memberships;
  ZC_EXPECT(!staticEscapeProofAdmissible(origins.asPtr(), memberships.asPtr(), makeEventKey(0)));
}

ZC_TEST("StaticEscapeProofAdmissibleTest.RejectsMembershipAtWrongEvent") {
  zc::Vector<EscapeOriginCause> origins;
  origins.add(EscapeOriginCause{makeStaticReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()});
  zc::Vector<RegionMembership> memberships;
  memberships.add(makeMembership(RegionKey::staticRegion(identity::DefId{}),
                                 OwnershipPoint::beforeEvent(makeEventKey(1))));
  ZC_EXPECT(!staticEscapeProofAdmissible(origins.asPtr(), memberships.asPtr(), makeEventKey(0)));
}

// ---------------------------------------------------------------------------
// addressOnlyEscapeProofAdmissible
// ---------------------------------------------------------------------------

ZC_TEST("AddressOnlyEscapeProofAdmissibleTest.AcceptsEmptyOriginsWithCarriers") {
  zc::Vector<EscapeOriginCause> origins;
  zc::Vector<RawProvenanceCarrierKey> carriers;
  carriers.add(makeCarrierKey(1));
  ZC_EXPECT(addressOnlyEscapeProofAdmissible(origins.asPtr(), carriers.asPtr()));
}

ZC_TEST("AddressOnlyEscapeProofAdmissibleTest.RejectsNonEmptyOrigins") {
  zc::Vector<EscapeOriginCause> origins;
  origins.add(EscapeOriginCause{makeReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()});
  zc::Vector<RawProvenanceCarrierKey> carriers;
  carriers.add(makeCarrierKey(1));
  ZC_EXPECT(!addressOnlyEscapeProofAdmissible(origins.asPtr(), carriers.asPtr()));
}

ZC_TEST("AddressOnlyEscapeProofAdmissibleTest.RejectsEmptyCarriers") {
  zc::Vector<EscapeOriginCause> origins;
  zc::Vector<RawProvenanceCarrierKey> carriers;
  ZC_EXPECT(!addressOnlyEscapeProofAdmissible(origins.asPtr(), carriers.asPtr()));
}

// ---------------------------------------------------------------------------
// EscapeOriginCause
// ---------------------------------------------------------------------------

ZC_TEST("EscapeOriginCauseTest.Equality") {
  auto first = EscapeOriginCause{makeReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()};
  auto second = EscapeOriginCause{makeReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()};
  auto other = EscapeOriginCause{makeReferenceOrigin(1, 0, 1),
                                 EscapeOriginRoute::rawCarrier(makeCarrierKey(1))};
  ZC_EXPECT(first == second);
  ZC_EXPECT(first != other);
}

ZC_TEST("EscapeOriginCauseTest.CloneProducesEqualCause") {
  auto original = EscapeOriginCause{makeReferenceOrigin(1, 0, 1), EscapeOriginRoute::direct()};
  auto cloned = original.clone();
  ZC_EXPECT(original == cloned);
}

// ---------------------------------------------------------------------------
// EscapeFact
// ---------------------------------------------------------------------------

ZC_TEST("EscapeFactTest.Construction") {
  zc::Vector<EscapeOriginCause> origins;
  zc::Vector<RawProvenanceCarrierKey> carriers;
  auto fact = EscapeFact{makeEventKey(0), makeMovePathKey(1), EscapeKind::returnEscape(),
                         zc::mv(origins), zc::mv(carriers),   EscapeProof::owned()};
  ZC_EXPECT(fact.key == makeEventKey(0));
  ZC_EXPECT(fact.kind.isReturn());
  ZC_EXPECT(fact.proof.isOwned());
  ZC_EXPECT(fact.origins.empty());
  ZC_EXPECT(fact.rawCarriers.empty());
}

}  // namespace
}  // namespace zomlang::compiler::ownership::facts
