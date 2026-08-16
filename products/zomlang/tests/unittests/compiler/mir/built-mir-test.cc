// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/mir/built-mir.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/compiler/identity/source-snapshot.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"
#include "zomlang/tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang::compiler::mir {
namespace {

identity::Sha256Digest repeatedDigest(uint8_t byte) {
  uint8_t bytes[32];
  for (auto& value : bytes) value = byte;
  ZC_IF_SOME(digest, identity::Sha256Digest::fromBytes(zc::arrayPtr(bytes))) { return digest; }
  ZC_FAIL_REQUIRE("invalid Built MIR digest fixture");
}

zc::Vector<zc::Array<uint8_t>> oneFunctionRecord() {
  zc::Vector<uint8_t> bytes;
  bytes.add(0xb3);
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(bytes.releaseAsArray());
  return functions;
}

MirLocalId local(uint32_t ordinal) {
  auto result = MirLocalId::fromOrdinal(ordinal);
  ZC_REQUIRE(result != zc::none);
  return ZC_REQUIRE_NONNULL(result);
}

identity::SourceSpan sourceSpan() {
  auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                          zc::heapArray<uint8_t>(8, uint8_t{0}));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_IF_SOME(value, snapshot) {
    auto span = value.span(1, 7);
    ZC_IF_SOME(admitted, span) { return zc::mv(admitted); }
  }
  ZC_FAIL_REQUIRE("invalid Built MIR source span fixture");
}

MirPlace place(MirLocalId localValue, identity::SemanticTypeId type, identity::DefId field,
               identity::DefId variant) {
  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::field(field, type, type));
  projections.add(MirProjection::index(local(2), type, type));
  projections.add(MirProjection::dereference(type, type));
  projections.add(MirProjection::downcast(variant, type, type));
  auto subslice = MirProjection::subslice(1, 3, type, type);
  ZC_REQUIRE(subslice != zc::none);
  projections.add(zc::mv(ZC_REQUIRE_NONNULL(subslice)));
  return MirPlace(localValue, type, zc::mv(projections), type);
}

void expectOracle(zc::Vector<zc::Array<uint8_t>>&& functions, zc::StringPtr expectedPreimage,
                  zc::StringPtr expectedDigest) {
  const uint8_t module[] = {0xa1};
  auto encoded = MirRevisionCodec::encodeBuiltFramed(repeatedDigest(0x00), zc::arrayPtr(module),
                                                     repeatedDigest(0x22), repeatedDigest(0x33),
                                                     repeatedDigest(0x44), functions.asPtr());
  ZC_REQUIRE(encoded != zc::none);
  ZC_IF_SOME(bytes, encoded) {
    ZC_EXPECT(zc::encodeHex(bytes.asPtr()) == expectedPreimage);
    auto digest = identity::sha256(bytes.asPtr());
    ZC_REQUIRE(digest != zc::none);
    ZC_IF_SOME(value, digest) { ZC_EXPECT(zc::encodeHex(value.bytes()) == expectedDigest); }
  }
}

ZC_TEST("Built MIR revision matches the canonical non-empty oracle") {
  expectOracle(
      oneFunctionRecord(),
      "7a6f6d2e6d69722d7265766973696f6e0000000000000000000000000000000000000000000000000000000000000000000000000000000001a122222222222222222222222222222222222222222222222222222222222222223333333333333333333333333333333333333333333333333333333333333333444444444444444444444444444444444444444444444444444444444444444400000000000000010000000000000001b3"_zc,
      "9f8de0ad0794e63ee7ed8d8ab777683956d5d9ca9bf151987bd0a60dbaad7985"_zc);
}

ZC_TEST("Built MIR revision matches the canonical empty oracle") {
  zc::Vector<zc::Array<uint8_t>> functions;
  expectOracle(
      zc::mv(functions),
      "7a6f6d2e6d69722d7265766973696f6e0000000000000000000000000000000000000000000000000000000000000000000000000000000001a12222222222222222222222222222222222222222222222222222222222222222333333333333333333333333333333333333333333333333333333333333333344444444444444444444444444444444444444444444444444444444444444440000000000000000"_zc,
      "b9a8988df033e7ce07c6708a6e2ce42e6bac1067231c8c86128e494b3238cbc9"_zc);
}

ZC_TEST("Built MIR value algebras clone every supported projection statement and terminator") {
  const auto field = tests::testDefinition(0);
  const auto variant = tests::testDefinition(1);
  const auto firstLocal = local(1);

  const auto type = tests::testSemanticType();
  auto invalidSubslice = MirProjection::subslice(4, 3, type, type);
  ZC_EXPECT(invalidSubslice == zc::none);
  auto projectionPlace = place(firstLocal, type, field, variant);
  ZC_REQUIRE(projectionPlace.projections().size() == 5);
  const auto projections = projectionPlace.projections();
  ZC_EXPECT(projections[0].kind() == MirProjectionKind::Field);
  ZC_EXPECT(projections[0].fieldValue().field == field);
  ZC_EXPECT(projections[0].inputType() == type);
  ZC_EXPECT(projections[0].resultType() == type);
  ZC_EXPECT(projections[1].kind() == MirProjectionKind::Index);
  ZC_EXPECT(projections[1].indexValue().index == local(2));
  ZC_EXPECT(projections[2].kind() == MirProjectionKind::Dereference);
  ZC_EXPECT(projections[3].kind() == MirProjectionKind::Downcast);
  ZC_EXPECT(projections[3].downcastValue().variant == variant);
  ZC_EXPECT(projections[4].kind() == MirProjectionKind::Subslice);
  ZC_EXPECT(projections[4].subsliceValue().first == 1);
  ZC_EXPECT(projections[4].subsliceValue().pastLast == 3);
  for (const auto& projection : projections) { ZC_EXPECT(projection.isStructurallyValid()); }
  auto clonedPlace = projectionPlace.clone();
  ZC_EXPECT(clonedPlace.local() == firstLocal);
  ZC_EXPECT(clonedPlace.rootType() == type);
  ZC_EXPECT(clonedPlace.projections().size() == projections.size());
  ZC_EXPECT(clonedPlace.resultType() == type);
  ZC_EXPECT(clonedPlace.hasConsistentTypeChain());
  zc::Vector<MirProjection> noProjections;
  auto mismatchedPlace =
      MirPlace(firstLocal, type, zc::mv(noProjections), tests::testSemanticType(1));
  ZC_EXPECT(!mismatchedPlace.hasConsistentTypeChain());

  auto copy = MirOperand::copy(place(firstLocal, type, field, variant));
  auto move = MirOperand::move(place(firstLocal, type, field, variant));
  ZC_EXPECT(copy.kind() == MirOperandKind::Copy);
  ZC_EXPECT(move.kind() == MirOperandKind::Move);
  ZC_EXPECT(copy.place().local() == firstLocal);
  ZC_EXPECT(move.place().local() == firstLocal);
  auto clonedCopy = copy.clone();
  auto clonedMove = move.clone();
  ZC_EXPECT(clonedCopy.kind() == MirOperandKind::Copy);
  ZC_EXPECT(clonedMove.kind() == MirOperandKind::Move);

  zc::Vector<MirNominalAggregateElement> aggregateElements;
  aggregateElements.add(MirNominalAggregateElement{
      field, MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(true))});
  aggregateElements.add(MirNominalAggregateElement{
      variant, MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(false))});
  auto aggregate = MirRvalue::nominalAggregate(variant, type, zc::mv(aggregateElements));
  ZC_EXPECT(aggregate.kind() == MirRvalueKind::NominalAggregate);
  ZC_EXPECT(aggregate.nominalAggregateValue().definition == variant);
  ZC_EXPECT(aggregate.nominalAggregateValue().type == type);
  ZC_REQUIRE(aggregate.nominalAggregateValue().elements.size() == 2);
  ZC_EXPECT(aggregate.nominalAggregateValue().elements[0].field == field);
  ZC_EXPECT(aggregate.nominalAggregateValue().elements[0].operand.kind() ==
            MirOperandKind::Constant);
  auto clonedAggregate = aggregate.clone();
  ZC_EXPECT(clonedAggregate.kind() == MirRvalueKind::NominalAggregate);
  ZC_REQUIRE(clonedAggregate.nominalAggregateValue().elements.size() == 2);
  ZC_EXPECT(clonedAggregate.nominalAggregateValue().elements[1].field == variant);
  ZC_EXPECT(clonedAggregate.nominalAggregateValue().elements[1].operand.kind() ==
            MirOperandKind::Constant);

  auto assignment = MirStatement::assign(
      place(firstLocal, type, field, variant),
      MirRvalue::use(MirOperand::copy(place(firstLocal, type, field, variant))),
      MirInitializationKind::Initialize, sourceSpan());
  auto storageLive = MirStatement::storageLive(firstLocal, sourceSpan());
  auto storageDead = MirStatement::storageDead(firstLocal, sourceSpan());
  auto borrow =
      MirStatement::borrowCreation(place(firstLocal, type, field, variant), MirBorrowKind::Mutable,
                                   place(firstLocal, type, field, variant), sourceSpan());
  auto discriminant =
      MirStatement::setDiscriminant(place(firstLocal, type, field, variant), variant, sourceSpan());
  auto deinitialize =
      MirStatement::deinitialize(place(firstLocal, type, field, variant), sourceSpan());
  ZC_EXPECT(assignment.kind() == MirStatementKind::Assign);
  ZC_EXPECT(assignment.assignmentValue().initialization == MirInitializationKind::Initialize);
  ZC_EXPECT(storageLive.storageLocal() == firstLocal);
  ZC_EXPECT(storageDead.storageLocal() == firstLocal);
  ZC_EXPECT(borrow.borrowCreationValue().kind == MirBorrowKind::Mutable);
  ZC_EXPECT(discriminant.setDiscriminantValue().variant == variant);
  ZC_EXPECT(deinitialize.deinitializeValue().destination.local() == firstLocal);
  ZC_EXPECT(assignment.sourceSpan().byteStart() == 1);
  ZC_EXPECT(assignment.clone().sourceSpan().byteEnd() == 7);
  ZC_EXPECT(assignment.clone().kind() == MirStatementKind::Assign);
  ZC_EXPECT(storageLive.clone().kind() == MirStatementKind::StorageLive);
  ZC_EXPECT(storageDead.clone().kind() == MirStatementKind::StorageDead);
  ZC_EXPECT(borrow.clone().kind() == MirStatementKind::BorrowCreation);
  ZC_EXPECT(discriminant.clone().kind() == MirStatementKind::SetDiscriminant);
  ZC_EXPECT(deinitialize.clone().kind() == MirStatementKind::Deinitialize);

  auto returning = MirTerminator::returnValue(
      MirOperand::move(place(firstLocal, type, field, variant)), sourceSpan());
  auto voidReturn = MirTerminator::returnVoid(sourceSpan());
  auto unreachable = MirTerminator::unreachable(sourceSpan());
  ZC_EXPECT(returning.kind() == MirTerminatorKind::Return);
  ZC_REQUIRE(returning.returnValue().value != zc::none);
  ZC_EXPECT(voidReturn.returnValue().value == zc::none);
  ZC_EXPECT(unreachable.kind() == MirTerminatorKind::Unreachable);
  ZC_EXPECT(returning.sourceSpan().byteStart() == 1);
  ZC_EXPECT(returning.clone().sourceSpan().byteEnd() == 7);
  ZC_EXPECT(returning.clone().kind() == MirTerminatorKind::Return);
  ZC_EXPECT(voidReturn.clone().kind() == MirTerminatorKind::Return);
  ZC_EXPECT(unreachable.clone().kind() == MirTerminatorKind::Unreachable);
}

ZC_TEST("Built MIR call effects commit mutable receiver activation only on normal edges") {
  const auto type = tests::testSemanticType();
  const auto receiverTemporary = local(3);
  const auto destination = local(4);
  const auto callee = tests::testDefinition(2);
  const auto normal = MirBlockId::fromOrdinal(2);
  ZC_REQUIRE(normal != zc::none);

  auto noActivation = MirCallEffect::noActivation();
  ZC_EXPECT(noActivation.kind() == MirCallEffectKind::NoActivation);
  ZC_EXPECT(!noActivation.commitsOnNormalEdge());
  ZC_EXPECT(noActivation.activatedMutableReceiver() == zc::none);

  auto activation = MirCallEffect::activateMutableReceiver(receiverTemporary);
  ZC_EXPECT(activation.kind() == MirCallEffectKind::ActivateMutableReceiver);
  ZC_EXPECT(activation.commitsOnNormalEdge());
  ZC_REQUIRE(activation.activatedMutableReceiver() != zc::none);
  ZC_IF_SOME(temporary, activation.activatedMutableReceiver()) {
    ZC_EXPECT(temporary == receiverTemporary);
  }
  auto clonedActivation = activation.clone();
  ZC_EXPECT(clonedActivation.kind() == MirCallEffectKind::ActivateMutableReceiver);

  zc::Vector<MirOperand> arguments;
  zc::Vector<MirProjection> projections;
  zc::Maybe<MirBlockId> noUnwind;
  auto call = MirTerminator::call(callee, zc::mv(arguments), zc::mv(clonedActivation),
                                  MirPlace(destination, type, zc::mv(projections), type),
                                  ZC_REQUIRE_NONNULL(normal), zc::mv(noUnwind), sourceSpan());
  ZC_REQUIRE(call.kind() == MirTerminatorKind::Call);
  const auto& value = call.callValue();
  ZC_EXPECT(value.effect.commitsOnNormalEdge());
  ZC_EXPECT(value.normalTarget == ZC_REQUIRE_NONNULL(normal));
  ZC_REQUIRE(value.effect.activatedMutableReceiver() != zc::none);
  ZC_IF_SOME(temporary, value.effect.activatedMutableReceiver()) {
    ZC_EXPECT(temporary == receiverTemporary);
  }
}

}  // namespace
}  // namespace zomlang::compiler::mir
