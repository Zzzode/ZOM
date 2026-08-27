// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/built-mir.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/source-snapshot.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"

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

MirSourceScopeId sourceScope(uint32_t ordinal) {
  auto result = MirSourceScopeId::fromOrdinal(ordinal);
  ZC_REQUIRE(result != zc::none);
  return ZC_REQUIRE_NONNULL(result);
}

zc::Array<uint8_t> decoded(zc::StringPtr hex) {
  auto bytes = zc::decodeHex(hex);
  ZC_REQUIRE(bytes != zc::none);
  return zc::mv(ZC_REQUIRE_NONNULL(bytes));
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

  // The Comparison rvalue produces a bool result; the Arithmetic rvalue produces
  // the operand type. Both retain their operator and operands through clone.
  auto comparison = MirRvalue::comparison(
      MirComparisonOperator::Lt, MirOperand::copy(place(firstLocal, type, field, variant)),
      MirOperand::copy(place(firstLocal, type, field, variant)), type);
  ZC_EXPECT(comparison.kind() == MirRvalueKind::Comparison);
  ZC_EXPECT(comparison.comparisonValue().op == MirComparisonOperator::Lt);
  ZC_EXPECT(comparison.comparisonValue().resultType == type);
  auto clonedComparison = comparison.clone();
  ZC_EXPECT(clonedComparison.kind() == MirRvalueKind::Comparison);
  ZC_EXPECT(clonedComparison.comparisonValue().op == MirComparisonOperator::Lt);

  auto arithmetic = MirRvalue::arithmetic(
      MirArithmeticOperator::Add, MirOperand::copy(place(firstLocal, type, field, variant)),
      MirOperand::copy(place(firstLocal, type, field, variant)), type);
  ZC_EXPECT(arithmetic.kind() == MirRvalueKind::Arithmetic);
  ZC_EXPECT(arithmetic.arithmeticValue().op == MirArithmeticOperator::Add);
  ZC_EXPECT(arithmetic.arithmeticValue().resultType == type);
  ZC_EXPECT(arithmetic.arithmeticValue().left.kind() == MirOperandKind::Copy);
  auto clonedArithmetic = arithmetic.clone();
  ZC_EXPECT(clonedArithmetic.kind() == MirRvalueKind::Arithmetic);
  ZC_EXPECT(clonedArithmetic.arithmeticValue().op == MirArithmeticOperator::Add);
  ZC_EXPECT(clonedArithmetic.arithmeticValue().resultType == type);

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

  const auto gotoTarget = MirBlockId::fromOrdinal(2);
  ZC_REQUIRE(gotoTarget != zc::none);
  auto gotoTerminator = MirTerminator::gotoTarget(ZC_REQUIRE_NONNULL(gotoTarget), sourceSpan());
  ZC_EXPECT(gotoTerminator.kind() == MirTerminatorKind::Goto);
  ZC_EXPECT(gotoTerminator.gotoValue().target == ZC_REQUIRE_NONNULL(gotoTarget));
  auto clonedGoto = gotoTerminator.clone();
  ZC_EXPECT(clonedGoto.kind() == MirTerminatorKind::Goto);
  ZC_EXPECT(clonedGoto.gotoValue().target == ZC_REQUIRE_NONNULL(gotoTarget));
  ZC_EXPECT(clonedGoto.sourceSpan().byteEnd() == 7);

  zc::Vector<MirSwitchIntArm> arms;
  const auto armTarget = MirBlockId::fromOrdinal(3);
  ZC_REQUIRE(armTarget != zc::none);
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                           ZC_REQUIRE_NONNULL(armTarget)});
  const auto defaultTarget = MirBlockId::fromOrdinal(4);
  ZC_REQUIRE(defaultTarget != zc::none);
  auto switchIntTerminator =
      MirTerminator::switchInt(MirOperand::move(place(firstLocal, type, field, variant)),
                               zc::mv(arms), ZC_REQUIRE_NONNULL(defaultTarget), sourceSpan());
  ZC_EXPECT(switchIntTerminator.kind() == MirTerminatorKind::SwitchInt);
  const auto& switchIntValue = switchIntTerminator.switchIntValue();
  ZC_EXPECT(switchIntValue.discriminant.kind() == MirOperandKind::Move);
  ZC_REQUIRE(switchIntValue.arms.size() == 1);
  ZC_EXPECT(switchIntValue.arms[0].target == ZC_REQUIRE_NONNULL(armTarget));
  ZC_EXPECT(switchIntValue.defaultTarget == ZC_REQUIRE_NONNULL(defaultTarget));
  auto clonedSwitchInt = switchIntTerminator.clone();
  ZC_EXPECT(clonedSwitchInt.kind() == MirTerminatorKind::SwitchInt);
  const auto& clonedSwitchIntValue = clonedSwitchInt.switchIntValue();
  ZC_EXPECT(clonedSwitchIntValue.discriminant.kind() == MirOperandKind::Move);
  ZC_REQUIRE(clonedSwitchIntValue.arms.size() == 1);
  ZC_EXPECT(clonedSwitchIntValue.arms[0].target == ZC_REQUIRE_NONNULL(armTarget));
  ZC_EXPECT(clonedSwitchIntValue.defaultTarget == ZC_REQUIRE_NONNULL(defaultTarget));
  ZC_EXPECT(clonedSwitchInt.sourceSpan().byteStart() == 1);
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

ZC_TEST("Built MIR unsafe scope boundary statement retains kind scope and clone") {
  const auto enter = MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Enter,
                                                       sourceScope(1), sourceSpan());
  ZC_EXPECT(enter.kind() == MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(enter.unsafeScopeBoundaryValue().kind == MirUnsafeScopeBoundaryKind::Enter);
  ZC_EXPECT(enter.unsafeScopeBoundaryValue().scope == sourceScope(1));
  ZC_EXPECT(enter.sourceSpan().byteStart() == 1);
  const auto exit = MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Exit,
                                                      sourceScope(2), sourceSpan());
  ZC_EXPECT(exit.unsafeScopeBoundaryValue().kind == MirUnsafeScopeBoundaryKind::Exit);
  ZC_EXPECT(exit.unsafeScopeBoundaryValue().scope == sourceScope(2));
  const auto clonedEnter = enter.clone();
  ZC_EXPECT(clonedEnter.kind() == MirStatementKind::UnsafeScopeBoundary);
  ZC_EXPECT(clonedEnter.unsafeScopeBoundaryValue().kind == MirUnsafeScopeBoundaryKind::Enter);
  ZC_EXPECT(clonedEnter.unsafeScopeBoundaryValue().scope == sourceScope(1));
  ZC_EXPECT(clonedEnter.sourceSpan().byteEnd() == 7);
}

zc::String framedFunctionDigest(zc::ArrayPtr<const uint8_t> functionRecord) {
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(zc::heapArray(functionRecord));
  const uint8_t module[] = {0xa1};
  auto encoded = MirRevisionCodec::encodeBuiltFramed(repeatedDigest(0x00), zc::arrayPtr(module),
                                                     repeatedDigest(0x22), repeatedDigest(0x33),
                                                     repeatedDigest(0x44), functions.asPtr());
  ZC_REQUIRE(encoded != zc::none);
  auto digest = identity::sha256(ZC_REQUIRE_NONNULL(encoded).asPtr());
  ZC_REQUIRE(digest != zc::none);
  return zc::encodeHex(ZC_REQUIRE_NONNULL(digest).bytes());
}

ZC_TEST("Built MIR revision matches the canonical 283-byte unsafe-scope oracle") {
  // RFC 0007: one 113-byte component-test canonical function record with one
  // root source scope, no locals, and one block whose statements are exactly
  // UnsafeScopeBoundary(Enter, scope=1) then UnsafeScopeBoundary(Exit, scope=1),
  // followed by Return(None).
  auto record = decoded(
      "0000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000020701000000010702000000010100"_zc);
  ZC_EXPECT(record.size() == 113);
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(zc::mv(record));
  expectOracle(
      zc::mv(functions),
      "7a6f6d2e6d69722d7265766973696f6e0000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222233333333333333333333333333333333333333333333333333333333333333334444444444444444444444444444444444444444444444444444444444444444000000000000000100000000000000710000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000020701000000010702000000010100"_zc,
      "c49976b9fc841ecf6cd2e2d62af3442d36a22571b52291a0601e60ea92f71aa0"_zc);
}

ZC_TEST("Built MIR unsafe-scope oracle changes when any boundary byte is mutated") {
  auto record = decoded(
      "0000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000020701000000010702000000010100"_zc);
  ZC_REQUIRE(record.size() == 113);
  // The enter statement occupies the six bytes before the exit statement:
  // outer tag 0x07, inner kind 0x01, uint32 scope ordinal 0x00000001.
  const size_t enterTag = record.size() - 14;
  const size_t enterKind = record.size() - 13;
  const size_t enterScope = record.size() - 9;
  ZC_REQUIRE(record[enterTag] == 0x07);
  ZC_REQUIRE(record[enterKind] == 0x01);
  ZC_REQUIRE(record[enterScope] == 0x01);
  const auto baseline = framedFunctionDigest(record.asPtr());
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[enterTag] = 0x08;
    ZC_EXPECT(framedFunctionDigest(mutated.asPtr()) != baseline);
  }
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[enterKind] = 0x03;
    ZC_EXPECT(framedFunctionDigest(mutated.asPtr()) != baseline);
  }
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[enterScope] = 0x02;
    ZC_EXPECT(framedFunctionDigest(mutated.asPtr()) != baseline);
  }
}

ZC_TEST("Built MIR revision matches the canonical 274-byte goto oracle") {
  // One root source scope, no locals, and one block with no statements whose
  // terminator is Goto(target=2). The identity and source-span bytes reuse
  // the unsafe-scope oracle fixture; only the statement count and terminator
  // encoding differ.
  auto record = decoded(
      "0000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000000400000002"_zc);
  ZC_EXPECT(record.size() == 104);
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(zc::mv(record));
  expectOracle(
      zc::mv(functions),
      "7a6f6d2e6d69722d7265766973696f6e0000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222233333333333333333333333333333333333333333333333333333333333333334444444444444444444444444444444444444444444444444444444444444444000000000000000100000000000000680000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000000400000002"_zc,
      "abd657b45b17565cdf47bc2ae9b134991152959b965d237729b3416067df8988"_zc);
}

ZC_TEST("Built MIR revision matches the canonical 316-byte switch-int oracle") {
  // One root source scope, no locals, and one block with no statements whose
  // terminator is SwitchInt with a constant bool discriminant, one arm
  // (bool=true -> block 3), and a default target of block 4. The identity
  // and source-span bytes reuse the unsafe-scope oracle fixture.
  auto record = decoded(
      "0000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c100000000000000000000000000000000000000000000000000000000000000010000000100000001000000000000000005030000000000000001d1000000000000000203010000000000000001000000000000000203010000000300000004"_zc);
  ZC_EXPECT(record.size() == 146);
  zc::Vector<zc::Array<uint8_t>> functions;
  functions.add(zc::mv(record));
  expectOracle(
      zc::mv(functions),
      "7a6f6d2e6d69722d7265766973696f6e0000000000000000000000000000000000000000000000000000000000000000000000000000000001a1222222222222222222222222222222222222222222222222222222222222222233333333333333333333333333333333333333333333333333333333333333334444444444444444444444444444444444444444444444444444444444444444000000000000000100000000000000920000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c100000000000000000000000000000000000000000000000000000000000000010000000100000001000000000000000005030000000000000001d1000000000000000203010000000000000001000000000000000203010000000300000004"_zc,
      "4ce0d6a06e820c4a2481e5bb86dee7019b2f845b401985cbb963371aed45ae4f"_zc);
}

ZC_TEST("Built MIR goto oracle changes when the terminator tag or target is mutated") {
  auto record = decoded(
      "0000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c10000000000000000000000000000000000000000000000000000000000000001000000010000000100000000000000000400000002"_zc);
  ZC_REQUIRE(record.size() == 104);
  // The terminator occupies the last five bytes: outer tag 0x04, uint32
  // target ordinal 0x00000002.
  const size_t tag = record.size() - 5;
  const size_t target = record.size() - 1;
  ZC_REQUIRE(record[tag] == 0x04);
  ZC_REQUIRE(record[target] == 0x02);
  const auto baseline = framedFunctionDigest(record.asPtr());
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[tag] = 0x05;
    ZC_EXPECT(framedFunctionDigest(mutated.asPtr()) != baseline);
  }
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[target] = 0x03;
    ZC_EXPECT(framedFunctionDigest(mutated.asPtr()) != baseline);
  }
}

ZC_TEST("Built MIR switch-int oracle changes when the terminator tag or default is mutated") {
  auto record = decoded(
      "0000000000000001b101020000000000000001d1c10000000000000000000000000000000000000000000000010000000100c100000000000000000000000000000000000000000000000000000000000000010000000100000001000000000000000005030000000000000001d1000000000000000203010000000000000001000000000000000203010000000300000004"_zc);
  ZC_REQUIRE(record.size() == 146);
  // The terminator occupies the last 47 bytes: outer tag 0x05, constant
  // discriminant operand, one arm (bool=true -> block 3), default block 4.
  const size_t tag = record.size() - 47;
  const size_t defaultTarget = record.size() - 1;
  ZC_REQUIRE(record[tag] == 0x05);
  ZC_REQUIRE(record[defaultTarget] == 0x04);
  const auto baseline = framedFunctionDigest(record.asPtr());
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[tag] = 0x04;
    ZC_EXPECT(framedFunctionDigest(mutated.asPtr()) != baseline);
  }
  {
    auto mutated = zc::heapArray(record.asPtr());
    mutated[defaultTarget] = 0x05;
    ZC_EXPECT(framedFunctionDigest(mutated.asPtr()) != baseline);
  }
}

}  // namespace
}  // namespace zomlang::compiler::mir
