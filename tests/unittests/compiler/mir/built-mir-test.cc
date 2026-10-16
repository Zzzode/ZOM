// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "compiler/mir/built-mir.h"

#include "compiler/driver/package/manifest-parser.h"
#include "compiler/driver/package/source-record.h"
#include "compiler/driver/session/compiler-session.h"
#include "compiler/identity/crypto/sha256.h"
#include "compiler/identity/source/source-snapshot.h"
#include "compiler/ir/target/target-registry.h"
#include "tests/unittests/compiler/driver/core/core-library-test-fixture.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"
#include "zc/core/encoding.h"
#include "zc/ztest/test.h"

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

  // The Erase rvalue erases a concrete operand to an existential target type.
  // It retains the source operand, source type, and target type through clone.
  const auto sourceType = tests::testSemanticType(2);
  const auto targetType = tests::testSemanticType(3);
  auto erase = MirRvalue::erase(MirOperand::copy(place(firstLocal, type, field, variant)),
                                sourceType, targetType);
  ZC_EXPECT(erase.kind() == MirRvalueKind::Erase);
  ZC_EXPECT(erase.eraseValue().source.kind() == MirOperandKind::Copy);
  ZC_EXPECT(erase.eraseValue().sourceType == sourceType);
  ZC_EXPECT(erase.eraseValue().targetType == targetType);
  auto clonedErase = erase.clone();
  ZC_EXPECT(clonedErase.kind() == MirRvalueKind::Erase);
  ZC_EXPECT(clonedErase.eraseValue().source.kind() == MirOperandKind::Copy);
  ZC_EXPECT(clonedErase.eraseValue().sourceType == sourceType);
  ZC_EXPECT(clonedErase.eraseValue().targetType == targetType);

  // The ErrorUnion rvalue constructs an error-union value (RFC 0006: a tag
  // constant plus a payload operand) and records the union, success, and
  // residual semantic types. Tag 0 is the success alternative; tag 1 is the
  // residual alternative. It retains every field through clone.
  const auto unionType = tests::testSemanticType(4);
  const auto successType = tests::testSemanticType(5);
  const auto residualType = tests::testSemanticType(6);
  auto errorUnion =
      MirRvalue::errorUnion(unionType, successType, residualType, 0,
                            MirOperand::copy(place(firstLocal, type, field, variant)));
  ZC_EXPECT(errorUnion.kind() == MirRvalueKind::ErrorUnion);
  ZC_EXPECT(errorUnion.errorUnionValue().unionType == unionType);
  ZC_EXPECT(errorUnion.errorUnionValue().successType == successType);
  ZC_EXPECT(errorUnion.errorUnionValue().residualType == residualType);
  ZC_EXPECT(errorUnion.errorUnionValue().tag == 0);
  ZC_EXPECT(errorUnion.errorUnionValue().payload.kind() == MirOperandKind::Copy);
  auto clonedErrorUnion = errorUnion.clone();
  ZC_EXPECT(clonedErrorUnion.kind() == MirRvalueKind::ErrorUnion);
  ZC_EXPECT(clonedErrorUnion.errorUnionValue().unionType == unionType);
  ZC_EXPECT(clonedErrorUnion.errorUnionValue().successType == successType);
  ZC_EXPECT(clonedErrorUnion.errorUnionValue().residualType == residualType);
  ZC_EXPECT(clonedErrorUnion.errorUnionValue().tag == 0);
  ZC_EXPECT(clonedErrorUnion.errorUnionValue().payload.kind() == MirOperandKind::Copy);

  // The residual alternative uses tag 1 and carries a constant payload.
  auto residual = MirRvalue::errorUnion(
      unionType, successType, residualType, 1,
      MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(false)));
  ZC_EXPECT(residual.kind() == MirRvalueKind::ErrorUnion);
  ZC_EXPECT(residual.errorUnionValue().tag == 1);
  ZC_EXPECT(residual.errorUnionValue().payload.kind() == MirOperandKind::Constant);
  auto clonedResidual = residual.clone();
  ZC_EXPECT(clonedResidual.kind() == MirRvalueKind::ErrorUnion);
  ZC_EXPECT(clonedResidual.errorUnionValue().tag == 1);
  ZC_EXPECT(clonedResidual.errorUnionValue().payload.kind() == MirOperandKind::Constant);

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

// ---------------------------------------------------------------------------
// Identity algebras
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR local identity admits positive ordinals and rejects zero") {
  ZC_EXPECT(MirLocalId::fromOrdinal(0) == zc::none);
  ZC_REQUIRE(MirLocalId::fromOrdinal(1) != zc::none);
  ZC_REQUIRE(MirLocalId::fromOrdinal(UINT32_MAX) != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(MirLocalId::fromOrdinal(1)).ordinal() == 1);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(MirLocalId::fromOrdinal(UINT32_MAX)).ordinal() == UINT32_MAX);
}

ZC_TEST("Built MIR local identity default construction is invalid") {
  MirLocalId invalid;
  ZC_EXPECT(!invalid.isValid());
  ZC_EXPECT(invalid.ordinal() == 0);
}

ZC_TEST("Built MIR local identity equality distinguishes ordinals") {
  const auto first = local(1);
  const auto second = local(2);
  ZC_EXPECT(first == local(1));
  ZC_EXPECT(first != second);
  ZC_EXPECT(!(first == second));
  ZC_EXPECT(!(first != local(1)));
}

ZC_TEST("Built MIR source-scope identity admits positive ordinals and rejects zero") {
  ZC_EXPECT(MirSourceScopeId::fromOrdinal(0) == zc::none);
  ZC_REQUIRE(MirSourceScopeId::fromOrdinal(1) != zc::none);
  ZC_REQUIRE(MirSourceScopeId::fromOrdinal(UINT32_MAX) != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(MirSourceScopeId::fromOrdinal(7)).ordinal() == 7);
}

ZC_TEST("Built MIR source-scope identity default construction is invalid") {
  MirSourceScopeId invalid;
  ZC_EXPECT(!invalid.isValid());
  ZC_EXPECT(invalid.ordinal() == 0);
}

ZC_TEST("Built MIR source-scope identity equality distinguishes ordinals") {
  const auto first = sourceScope(1);
  const auto second = sourceScope(2);
  ZC_EXPECT(first == sourceScope(1));
  ZC_EXPECT(first != second);
  ZC_EXPECT(!(first == second));
}

ZC_TEST("Built MIR block identity admits positive ordinals and rejects zero") {
  ZC_EXPECT(MirBlockId::fromOrdinal(0) == zc::none);
  ZC_REQUIRE(MirBlockId::fromOrdinal(1) != zc::none);
  ZC_REQUIRE(MirBlockId::fromOrdinal(UINT32_MAX) != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(MirBlockId::fromOrdinal(3)).ordinal() == 3);
}

ZC_TEST("Built MIR block identity default construction is invalid") {
  MirBlockId invalid;
  ZC_EXPECT(!invalid.isValid());
  ZC_EXPECT(invalid.ordinal() == 0);
}

ZC_TEST("Built MIR block identity equality distinguishes ordinals") {
  const auto first = ZC_REQUIRE_NONNULL(MirBlockId::fromOrdinal(1));
  const auto second = ZC_REQUIRE_NONNULL(MirBlockId::fromOrdinal(2));
  ZC_EXPECT(first == ZC_REQUIRE_NONNULL(MirBlockId::fromOrdinal(1)));
  ZC_EXPECT(first != second);
  ZC_EXPECT(!(first == second));
}

// ---------------------------------------------------------------------------
// Projection algebra
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR field projection retains field identity and type endpoints") {
  const auto field = tests::testDefinition(0);
  const auto input = tests::testSemanticType(0);
  const auto result = tests::testSemanticType(1);
  auto projection = MirProjection::field(field, input, result);
  ZC_EXPECT(projection.kind() == MirProjectionKind::Field);
  ZC_EXPECT(projection.fieldValue().field == field);
  ZC_EXPECT(projection.inputType() == input);
  ZC_EXPECT(projection.resultType() == result);
  ZC_EXPECT(projection.isStructurallyValid());
}

ZC_TEST("Built MIR index projection retains index local and type endpoints") {
  const auto index = local(4);
  const auto input = tests::testSemanticType(0);
  const auto result = tests::testSemanticType(1);
  auto projection = MirProjection::index(index, input, result);
  ZC_EXPECT(projection.kind() == MirProjectionKind::Index);
  ZC_EXPECT(projection.indexValue().index == index);
  ZC_EXPECT(projection.inputType() == input);
  ZC_EXPECT(projection.resultType() == result);
  ZC_EXPECT(projection.isStructurallyValid());
}

ZC_TEST("Built MIR dereference projection retains type endpoints") {
  const auto input = tests::testSemanticType(0);
  const auto result = tests::testSemanticType(1);
  auto projection = MirProjection::dereference(input, result);
  ZC_EXPECT(projection.kind() == MirProjectionKind::Dereference);
  ZC_EXPECT(projection.inputType() == input);
  ZC_EXPECT(projection.resultType() == result);
  ZC_EXPECT(projection.isStructurallyValid());
}

ZC_TEST("Built MIR downcast projection retains variant identity and type endpoints") {
  const auto variant = tests::testDefinition(1);
  const auto input = tests::testSemanticType(0);
  const auto result = tests::testSemanticType(1);
  auto projection = MirProjection::downcast(variant, input, result);
  ZC_EXPECT(projection.kind() == MirProjectionKind::Downcast);
  ZC_EXPECT(projection.downcastValue().variant == variant);
  ZC_EXPECT(projection.inputType() == input);
  ZC_EXPECT(projection.resultType() == result);
  ZC_EXPECT(projection.isStructurallyValid());
}

ZC_TEST("Built MIR subslice projection retains bounds and rejects inverted ranges") {
  const auto type = tests::testSemanticType();
  ZC_EXPECT(MirProjection::subslice(3, 1, type, type) == zc::none);
  ZC_REQUIRE(MirProjection::subslice(0, 0, type, type) != zc::none);
  ZC_REQUIRE(MirProjection::subslice(2, 5, type, type) != zc::none);
  const auto projection = ZC_REQUIRE_NONNULL(MirProjection::subslice(2, 5, type, type));
  ZC_EXPECT(projection.kind() == MirProjectionKind::Subslice);
  ZC_EXPECT(projection.subsliceValue().first == 2);
  ZC_EXPECT(projection.subsliceValue().pastLast == 5);
  ZC_EXPECT(projection.isStructurallyValid());
}

ZC_TEST("Built MIR projection structural validity rejects invalid embedded identities") {
  const auto type = tests::testSemanticType();
  const auto validField = tests::testDefinition(0);
  // A default-constructed DefId is not interned and therefore invalid.
  ZC_EXPECT(!MirProjection::field(identity::DefId{}, type, type).isStructurallyValid());
  ZC_EXPECT(MirProjection::field(validField, type, type).isStructurallyValid());
  // A default-constructed local identity is invalid.
  ZC_EXPECT(!MirProjection::index(MirLocalId{}, type, type).isStructurallyValid());
  ZC_EXPECT(MirProjection::index(local(1), type, type).isStructurallyValid());
  ZC_EXPECT(!MirProjection::downcast(identity::DefId{}, type, type).isStructurallyValid());
  ZC_EXPECT(MirProjection::downcast(validField, type, type).isStructurallyValid());
  // Dereference carries no embedded identity and is always structurally valid.
  ZC_EXPECT(MirProjection::dereference(type, type).isStructurallyValid());
}

ZC_TEST("Built MIR projection clone preserves every kind and payload") {
  const auto type = tests::testSemanticType();
  const auto field = tests::testDefinition(0);
  const auto variant = tests::testDefinition(1);
  const auto index = local(2);
  {
    auto cloned = MirProjection::field(field, type, type).clone();
    ZC_EXPECT(cloned.kind() == MirProjectionKind::Field);
    ZC_EXPECT(cloned.fieldValue().field == field);
  }
  {
    auto cloned = MirProjection::index(index, type, type).clone();
    ZC_EXPECT(cloned.kind() == MirProjectionKind::Index);
    ZC_EXPECT(cloned.indexValue().index == index);
  }
  {
    auto cloned = MirProjection::dereference(type, type).clone();
    ZC_EXPECT(cloned.kind() == MirProjectionKind::Dereference);
  }
  {
    auto cloned = MirProjection::downcast(variant, type, type).clone();
    ZC_EXPECT(cloned.kind() == MirProjectionKind::Downcast);
    ZC_EXPECT(cloned.downcastValue().variant == variant);
  }
  {
    const auto subslice = ZC_REQUIRE_NONNULL(MirProjection::subslice(1, 4, type, type));
    auto cloned = subslice.clone();
    ZC_EXPECT(cloned.kind() == MirProjectionKind::Subslice);
    ZC_EXPECT(cloned.subsliceValue().first == 1);
    ZC_EXPECT(cloned.subsliceValue().pastLast == 4);
  }
}

// ---------------------------------------------------------------------------
// Place algebra
// ---------------------------------------------------------------------------

MirPlace barePlace(MirLocalId localValue, identity::SemanticTypeId type) {
  zc::Vector<MirProjection> projections;
  return MirPlace(localValue, type, zc::mv(projections), type);
}

ZC_TEST("Built MIR place retains local root type projections and result type") {
  const auto type = tests::testSemanticType();
  const auto localId = local(7);
  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::dereference(type, type));
  auto placeValue = MirPlace(localId, type, zc::mv(projections), type);
  ZC_EXPECT(placeValue.local() == localId);
  ZC_EXPECT(placeValue.rootType() == type);
  ZC_EXPECT(placeValue.resultType() == type);
  ZC_REQUIRE(placeValue.projections().size() == 1);
  ZC_EXPECT(placeValue.projections()[0].kind() == MirProjectionKind::Dereference);
}

ZC_TEST("Built MIR place type chain accepts matching projection endpoints") {
  const auto root = tests::testSemanticType(0);
  const auto middle = tests::testSemanticType(1);
  const auto leaf = tests::testSemanticType(2);
  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::field(tests::testDefinition(0), root, middle));
  projections.add(MirProjection::field(tests::testDefinition(1), middle, leaf));
  auto placeValue = MirPlace(local(1), root, zc::mv(projections), leaf);
  ZC_EXPECT(placeValue.hasConsistentTypeChain());
}

ZC_TEST("Built MIR place type chain rejects a broken intermediate link") {
  const auto root = tests::testSemanticType(0);
  const auto middle = tests::testSemanticType(1);
  const auto leaf = tests::testSemanticType(2);
  zc::Vector<MirProjection> projections;
  // The second projection's input type does not match the first result type.
  projections.add(MirProjection::field(tests::testDefinition(0), root, middle));
  projections.add(MirProjection::field(tests::testDefinition(1), root, leaf));
  auto broken = MirPlace(local(1), root, zc::mv(projections), leaf);
  ZC_EXPECT(!broken.hasConsistentTypeChain());
}

ZC_TEST("Built MIR place type chain rejects a mismatched final result type") {
  const auto root = tests::testSemanticType(0);
  const auto middle = tests::testSemanticType(1);
  const auto leaf = tests::testSemanticType(2);
  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::field(tests::testDefinition(0), root, middle));
  // The declared result type is leaf but the chain ends at middle.
  auto broken = MirPlace(local(1), root, zc::mv(projections), leaf);
  ZC_EXPECT(!broken.hasConsistentTypeChain());
}

ZC_TEST("Built MIR place empty projections require result type equal to root type") {
  const auto root = tests::testSemanticType(0);
  const auto other = tests::testSemanticType(1);
  zc::Vector<MirProjection> empty;
  ZC_EXPECT(MirPlace(local(1), root, zc::mv(empty), root).hasConsistentTypeChain());
  zc::Vector<MirProjection> emptyAgain;
  ZC_EXPECT(!MirPlace(local(1), root, zc::mv(emptyAgain), other).hasConsistentTypeChain());
}

ZC_TEST("Built MIR place clone deep-copies the projection path") {
  const auto root = tests::testSemanticType(0);
  const auto leaf = tests::testSemanticType(1);
  const auto localId = local(3);
  const auto field = tests::testDefinition(0);
  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::field(field, root, leaf));
  auto original = MirPlace(localId, root, zc::mv(projections), leaf);
  auto cloned = original.clone();
  ZC_EXPECT(cloned.local() == localId);
  ZC_EXPECT(cloned.rootType() == root);
  ZC_EXPECT(cloned.resultType() == leaf);
  ZC_REQUIRE(cloned.projections().size() == 1);
  ZC_EXPECT(cloned.projections()[0].kind() == MirProjectionKind::Field);
  ZC_EXPECT(cloned.projections()[0].fieldValue().field == field);
  ZC_EXPECT(cloned.hasConsistentTypeChain());
}

// ---------------------------------------------------------------------------
// Operand algebra
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR copy and move operands retain their places") {
  const auto type = tests::testSemanticType();
  const auto localId = local(5);
  auto copy = MirOperand::copy(barePlace(localId, type));
  ZC_EXPECT(copy.kind() == MirOperandKind::Copy);
  ZC_EXPECT(copy.place().local() == localId);
  auto move = MirOperand::move(barePlace(localId, type));
  ZC_EXPECT(move.kind() == MirOperandKind::Move);
  ZC_EXPECT(move.place().local() == localId);
}

ZC_TEST("Built MIR constant operand retains type and value") {
  const auto type = tests::testSemanticType();
  auto constant = MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(true));
  ZC_EXPECT(constant.kind() == MirOperandKind::Constant);
  ZC_EXPECT(constant.constantValue().type == type);
  ZC_REQUIRE(constant.constantValue().value.booleanValue() != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(constant.constantValue().value.booleanValue()));
}

ZC_TEST("Built MIR operand clone preserves copy move and constant kinds") {
  const auto type = tests::testSemanticType();
  const auto localId = local(9);
  {
    auto copy = MirOperand::copy(barePlace(localId, type));
    auto cloned = copy.clone();
    ZC_EXPECT(cloned.kind() == MirOperandKind::Copy);
    ZC_EXPECT(cloned.place().local() == localId);
  }
  {
    auto move = MirOperand::move(barePlace(localId, type));
    auto cloned = move.clone();
    ZC_EXPECT(cloned.kind() == MirOperandKind::Move);
    ZC_EXPECT(cloned.place().local() == localId);
  }
  {
    auto constant =
        MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(false));
    auto cloned = constant.clone();
    ZC_EXPECT(cloned.kind() == MirOperandKind::Constant);
    ZC_REQUIRE(cloned.constantValue().value.booleanValue() != zc::none);
    ZC_EXPECT(!ZC_REQUIRE_NONNULL(cloned.constantValue().value.booleanValue()));
  }
}

// ---------------------------------------------------------------------------
// Rvalue algebra
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR use rvalue retains its operand") {
  const auto type = tests::testSemanticType();
  auto rvalue =
      MirRvalue::use(MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()));
  ZC_EXPECT(rvalue.kind() == MirRvalueKind::Use);
  ZC_EXPECT(rvalue.useValue().operand.kind() == MirOperandKind::Constant);
}

ZC_TEST("Built MIR nominal-aggregate rvalue retains definition type and elements") {
  const auto type = tests::testSemanticType();
  const auto definition = tests::testDefinition(0);
  const auto firstField = tests::testDefinition(1);
  const auto secondField = tests::testDefinition(2);
  zc::Vector<MirNominalAggregateElement> elements;
  elements.add(MirNominalAggregateElement{
      firstField,
      MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(true))});
  elements.add(MirNominalAggregateElement{
      secondField,
      MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(false))});
  auto rvalue = MirRvalue::nominalAggregate(definition, type, zc::mv(elements));
  ZC_EXPECT(rvalue.kind() == MirRvalueKind::NominalAggregate);
  ZC_EXPECT(rvalue.nominalAggregateValue().definition == definition);
  ZC_EXPECT(rvalue.nominalAggregateValue().type == type);
  ZC_REQUIRE(rvalue.nominalAggregateValue().elements.size() == 2);
  ZC_EXPECT(rvalue.nominalAggregateValue().elements[0].field == firstField);
  ZC_EXPECT(rvalue.nominalAggregateValue().elements[1].field == secondField);
}

ZC_TEST("Built MIR comparison rvalue retains operator operands and result type") {
  const auto operandType = tests::testSemanticType(0);
  const auto resultType = tests::testSemanticType(1);
  auto rvalue = MirRvalue::comparison(
      MirComparisonOperator::Ge,
      MirOperand::constant(operandType, checker::checked::CanonicalConstValue::boolean(true)),
      MirOperand::constant(operandType, checker::checked::CanonicalConstValue::boolean(false)),
      resultType);
  ZC_EXPECT(rvalue.kind() == MirRvalueKind::Comparison);
  ZC_EXPECT(rvalue.comparisonValue().op == MirComparisonOperator::Ge);
  ZC_EXPECT(rvalue.comparisonValue().resultType == resultType);
  ZC_EXPECT(rvalue.comparisonValue().left.kind() == MirOperandKind::Constant);
  ZC_EXPECT(rvalue.comparisonValue().right.kind() == MirOperandKind::Constant);
}

ZC_TEST("Built MIR arithmetic rvalue retains operator operands and result type") {
  const auto operandType = tests::testSemanticType(0);
  const auto resultType = tests::testSemanticType(1);
  auto rvalue = MirRvalue::arithmetic(
      MirArithmeticOperator::BitXor,
      MirOperand::constant(operandType, checker::checked::CanonicalConstValue::boolean(true)),
      MirOperand::constant(operandType, checker::checked::CanonicalConstValue::boolean(false)),
      resultType);
  ZC_EXPECT(rvalue.kind() == MirRvalueKind::Arithmetic);
  ZC_EXPECT(rvalue.arithmeticValue().op == MirArithmeticOperator::BitXor);
  ZC_EXPECT(rvalue.arithmeticValue().resultType == resultType);
  ZC_EXPECT(rvalue.arithmeticValue().left.kind() == MirOperandKind::Constant);
  ZC_EXPECT(rvalue.arithmeticValue().right.kind() == MirOperandKind::Constant);
}

ZC_TEST("Built MIR erase rvalue retains source operand and type endpoints") {
  const auto sourceType = tests::testSemanticType(0);
  const auto targetType = tests::testSemanticType(1);
  auto rvalue = MirRvalue::erase(
      MirOperand::constant(sourceType, checker::checked::CanonicalConstValue::unit()), sourceType,
      targetType);
  ZC_EXPECT(rvalue.kind() == MirRvalueKind::Erase);
  ZC_EXPECT(rvalue.eraseValue().source.kind() == MirOperandKind::Constant);
  ZC_EXPECT(rvalue.eraseValue().sourceType == sourceType);
  ZC_EXPECT(rvalue.eraseValue().targetType == targetType);
}

ZC_TEST("Built MIR error-union rvalue retains tag payload and all three types") {
  const auto unionType = tests::testSemanticType(0);
  const auto successType = tests::testSemanticType(1);
  const auto residualType = tests::testSemanticType(2);
  auto success = MirRvalue::errorUnion(
      unionType, successType, residualType, 0,
      MirOperand::constant(successType, checker::checked::CanonicalConstValue::unit()));
  ZC_EXPECT(success.kind() == MirRvalueKind::ErrorUnion);
  ZC_EXPECT(success.errorUnionValue().unionType == unionType);
  ZC_EXPECT(success.errorUnionValue().successType == successType);
  ZC_EXPECT(success.errorUnionValue().residualType == residualType);
  ZC_EXPECT(success.errorUnionValue().tag == 0);
  ZC_EXPECT(success.errorUnionValue().payload.kind() == MirOperandKind::Constant);
  auto residual = MirRvalue::errorUnion(
      unionType, successType, residualType, 1,
      MirOperand::constant(residualType, checker::checked::CanonicalConstValue::unit()));
  ZC_EXPECT(residual.errorUnionValue().tag == 1);
}

ZC_TEST("Built MIR extract-field rvalue retains source field index and result type") {
  const auto sourceType = tests::testSemanticType(0);
  const auto resultType = tests::testSemanticType(1);
  auto rvalue = MirRvalue::extractField(
      MirOperand::constant(sourceType, checker::checked::CanonicalConstValue::unit()), 3,
      resultType);
  ZC_EXPECT(rvalue.kind() == MirRvalueKind::ExtractField);
  ZC_EXPECT(rvalue.extractFieldValue().fieldIndex == 3);
  ZC_EXPECT(rvalue.extractFieldValue().resultType == resultType);
  ZC_EXPECT(rvalue.extractFieldValue().source.kind() == MirOperandKind::Constant);
}

ZC_TEST("Built MIR rvalue clone preserves every kind") {
  const auto type = tests::testSemanticType();
  {
    auto rvalue =
        MirRvalue::use(MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()));
    ZC_EXPECT(rvalue.clone().kind() == MirRvalueKind::Use);
  }
  {
    const auto definition = tests::testDefinition(0);
    zc::Vector<MirNominalAggregateElement> elements;
    auto rvalue = MirRvalue::nominalAggregate(definition, type, zc::mv(elements));
    auto cloned = rvalue.clone();
    ZC_EXPECT(cloned.kind() == MirRvalueKind::NominalAggregate);
    ZC_EXPECT(cloned.nominalAggregateValue().definition == definition);
  }
  {
    auto rvalue = MirRvalue::comparison(
        MirComparisonOperator::Lt,
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()),
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()), type);
    auto cloned = rvalue.clone();
    ZC_EXPECT(cloned.kind() == MirRvalueKind::Comparison);
    ZC_EXPECT(cloned.comparisonValue().op == MirComparisonOperator::Lt);
  }
  {
    auto rvalue = MirRvalue::arithmetic(
        MirArithmeticOperator::Add,
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()),
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()), type);
    auto cloned = rvalue.clone();
    ZC_EXPECT(cloned.kind() == MirRvalueKind::Arithmetic);
    ZC_EXPECT(cloned.arithmeticValue().op == MirArithmeticOperator::Add);
  }
  {
    auto rvalue = MirRvalue::erase(
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()), type, type);
    auto cloned = rvalue.clone();
    ZC_EXPECT(cloned.kind() == MirRvalueKind::Erase);
    ZC_EXPECT(cloned.eraseValue().sourceType == type);
  }
  {
    auto rvalue = MirRvalue::errorUnion(
        type, type, type, 0,
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()));
    auto cloned = rvalue.clone();
    ZC_EXPECT(cloned.kind() == MirRvalueKind::ErrorUnion);
    ZC_EXPECT(cloned.errorUnionValue().tag == 0);
  }
  {
    auto rvalue = MirRvalue::extractField(
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()), 0, type);
    auto cloned = rvalue.clone();
    ZC_EXPECT(cloned.kind() == MirRvalueKind::ExtractField);
    ZC_EXPECT(cloned.extractFieldValue().fieldIndex == 0);
  }
}

// ---------------------------------------------------------------------------
// Statement algebra
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR assign statement retains destination value initialization and span") {
  const auto type = tests::testSemanticType();
  const auto localId = local(4);
  auto statement = MirStatement::assign(
      barePlace(localId, type),
      MirRvalue::use(MirOperand::constant(type, checker::checked::CanonicalConstValue::unit())),
      MirInitializationKind::Overwrite, sourceSpan());
  ZC_EXPECT(statement.kind() == MirStatementKind::Assign);
  ZC_EXPECT(statement.assignmentValue().initialization == MirInitializationKind::Overwrite);
  ZC_EXPECT(statement.assignmentValue().destination.local() == localId);
  ZC_EXPECT(statement.assignmentValue().value.kind() == MirRvalueKind::Use);
  ZC_EXPECT(statement.sourceSpan().byteStart() == 1);
}

ZC_TEST("Built MIR storage statements retain their local") {
  const auto localId = local(6);
  auto live = MirStatement::storageLive(localId, sourceSpan());
  ZC_EXPECT(live.kind() == MirStatementKind::StorageLive);
  ZC_EXPECT(live.storageLocal() == localId);
  auto dead = MirStatement::storageDead(localId, sourceSpan());
  ZC_EXPECT(dead.kind() == MirStatementKind::StorageDead);
  ZC_EXPECT(dead.storageLocal() == localId);
}

ZC_TEST("Built MIR borrow-creation statement retains kind destination and source") {
  const auto type = tests::testSemanticType();
  const auto destination = local(1);
  const auto source = local(2);
  auto statement = MirStatement::borrowCreation(barePlace(destination, type), MirBorrowKind::Shared,
                                                barePlace(source, type), sourceSpan());
  ZC_EXPECT(statement.kind() == MirStatementKind::BorrowCreation);
  ZC_EXPECT(statement.borrowCreationValue().kind == MirBorrowKind::Shared);
  ZC_EXPECT(statement.borrowCreationValue().destination.local() == destination);
  ZC_EXPECT(statement.borrowCreationValue().source.local() == source);
}

ZC_TEST("Built MIR set-discriminant statement retains destination and variant") {
  const auto type = tests::testSemanticType();
  const auto localId = local(1);
  const auto variant = tests::testDefinition(1);
  auto statement = MirStatement::setDiscriminant(barePlace(localId, type), variant, sourceSpan());
  ZC_EXPECT(statement.kind() == MirStatementKind::SetDiscriminant);
  ZC_EXPECT(statement.setDiscriminantValue().variant == variant);
  ZC_EXPECT(statement.setDiscriminantValue().destination.local() == localId);
}

ZC_TEST("Built MIR deinitialize statement retains destination") {
  const auto type = tests::testSemanticType();
  const auto localId = local(1);
  auto statement = MirStatement::deinitialize(barePlace(localId, type), sourceSpan());
  ZC_EXPECT(statement.kind() == MirStatementKind::Deinitialize);
  ZC_EXPECT(statement.deinitializeValue().destination.local() == localId);
}

ZC_TEST("Built MIR statement clone preserves every kind") {
  const auto type = tests::testSemanticType();
  const auto localId = local(1);
  const auto variant = tests::testDefinition(1);
  {
    auto statement = MirStatement::assign(
        barePlace(localId, type),
        MirRvalue::use(MirOperand::constant(type, checker::checked::CanonicalConstValue::unit())),
        MirInitializationKind::Initialize, sourceSpan());
    auto cloned = statement.clone();
    ZC_EXPECT(cloned.kind() == MirStatementKind::Assign);
    ZC_EXPECT(cloned.assignmentValue().initialization == MirInitializationKind::Initialize);
  }
  ZC_EXPECT(MirStatement::storageLive(localId, sourceSpan()).clone().kind() ==
            MirStatementKind::StorageLive);
  ZC_EXPECT(MirStatement::storageDead(localId, sourceSpan()).clone().kind() ==
            MirStatementKind::StorageDead);
  {
    auto statement = MirStatement::borrowCreation(barePlace(localId, type), MirBorrowKind::Mutable,
                                                  barePlace(localId, type), sourceSpan());
    auto cloned = statement.clone();
    ZC_EXPECT(cloned.kind() == MirStatementKind::BorrowCreation);
    ZC_EXPECT(cloned.borrowCreationValue().kind == MirBorrowKind::Mutable);
  }
  {
    auto statement = MirStatement::setDiscriminant(barePlace(localId, type), variant, sourceSpan());
    auto cloned = statement.clone();
    ZC_EXPECT(cloned.kind() == MirStatementKind::SetDiscriminant);
    ZC_EXPECT(cloned.setDiscriminantValue().variant == variant);
  }
  ZC_EXPECT(MirStatement::deinitialize(barePlace(localId, type), sourceSpan()).clone().kind() ==
            MirStatementKind::Deinitialize);
  {
    auto statement = MirStatement::unsafeScopeBoundary(MirUnsafeScopeBoundaryKind::Enter,
                                                       sourceScope(1), sourceSpan());
    auto cloned = statement.clone();
    ZC_EXPECT(cloned.kind() == MirStatementKind::UnsafeScopeBoundary);
    ZC_EXPECT(cloned.unsafeScopeBoundaryValue().kind == MirUnsafeScopeBoundaryKind::Enter);
    ZC_EXPECT(cloned.unsafeScopeBoundaryValue().scope == sourceScope(1));
  }
}

// ---------------------------------------------------------------------------
// Terminator algebra
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR return terminators distinguish value presence") {
  const auto type = tests::testSemanticType();
  auto withValue = MirTerminator::returnValue(
      MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()), sourceSpan());
  ZC_EXPECT(withValue.kind() == MirTerminatorKind::Return);
  ZC_REQUIRE(withValue.returnValue().value != zc::none);
  auto voidReturn = MirTerminator::returnVoid(sourceSpan());
  ZC_EXPECT(voidReturn.kind() == MirTerminatorKind::Return);
  ZC_EXPECT(voidReturn.returnValue().value == zc::none);
}

ZC_TEST("Built MIR unreachable terminator retains kind and span") {
  auto terminator = MirTerminator::unreachable(sourceSpan());
  ZC_EXPECT(terminator.kind() == MirTerminatorKind::Unreachable);
  ZC_EXPECT(terminator.sourceSpan().byteEnd() == 7);
  ZC_EXPECT(terminator.clone().kind() == MirTerminatorKind::Unreachable);
}

ZC_TEST("Built MIR call terminator retains callee arguments effect destination and targets") {
  const auto type = tests::testSemanticType();
  const auto callee = tests::testDefinition(2);
  const auto destination = local(4);
  const auto normal = MirBlockId::fromOrdinal(2);
  ZC_REQUIRE(normal != zc::none);
  const auto unwind = MirBlockId::fromOrdinal(3);
  ZC_REQUIRE(unwind != zc::none);
  zc::Vector<MirOperand> arguments;
  arguments.add(MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()));
  zc::Maybe<MirBlockId> unwindTarget = ZC_REQUIRE_NONNULL(unwind);
  auto terminator = MirTerminator::call(callee, zc::mv(arguments), MirCallEffect::noActivation(),
                                        barePlace(destination, type), ZC_REQUIRE_NONNULL(normal),
                                        zc::mv(unwindTarget), sourceSpan());
  ZC_EXPECT(terminator.kind() == MirTerminatorKind::Call);
  const auto& value = terminator.callValue();
  ZC_EXPECT(value.callee == callee);
  ZC_REQUIRE(value.arguments.size() == 1);
  ZC_EXPECT(value.arguments[0].kind() == MirOperandKind::Constant);
  ZC_EXPECT(value.effect.kind() == MirCallEffectKind::NoActivation);
  ZC_EXPECT(value.destination.local() == destination);
  ZC_EXPECT(value.normalTarget == ZC_REQUIRE_NONNULL(normal));
  ZC_REQUIRE(value.unwindTarget != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(value.unwindTarget) == ZC_REQUIRE_NONNULL(unwind));
}

ZC_TEST("Built MIR goto terminator retains target") {
  const auto target = MirBlockId::fromOrdinal(5);
  ZC_REQUIRE(target != zc::none);
  auto terminator = MirTerminator::gotoTarget(ZC_REQUIRE_NONNULL(target), sourceSpan());
  ZC_EXPECT(terminator.kind() == MirTerminatorKind::Goto);
  ZC_EXPECT(terminator.gotoValue().target == ZC_REQUIRE_NONNULL(target));
}

ZC_TEST("Built MIR switch-int terminator retains discriminant arms and default") {
  const auto type = tests::testSemanticType();
  zc::Vector<MirSwitchIntArm> arms;
  const auto trueTarget = MirBlockId::fromOrdinal(2);
  ZC_REQUIRE(trueTarget != zc::none);
  const auto falseTarget = MirBlockId::fromOrdinal(3);
  ZC_REQUIRE(falseTarget != zc::none);
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                           ZC_REQUIRE_NONNULL(trueTarget)});
  arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false),
                           ZC_REQUIRE_NONNULL(falseTarget)});
  const auto defaultTarget = MirBlockId::fromOrdinal(4);
  ZC_REQUIRE(defaultTarget != zc::none);
  auto terminator = MirTerminator::switchInt(
      MirOperand::constant(type, checker::checked::CanonicalConstValue::boolean(true)),
      zc::mv(arms), ZC_REQUIRE_NONNULL(defaultTarget), sourceSpan());
  ZC_EXPECT(terminator.kind() == MirTerminatorKind::SwitchInt);
  const auto& value = terminator.switchIntValue();
  ZC_EXPECT(value.discriminant.kind() == MirOperandKind::Constant);
  ZC_REQUIRE(value.arms.size() == 2);
  ZC_EXPECT(value.arms[0].target == ZC_REQUIRE_NONNULL(trueTarget));
  ZC_EXPECT(value.arms[1].target == ZC_REQUIRE_NONNULL(falseTarget));
  ZC_EXPECT(value.defaultTarget == ZC_REQUIRE_NONNULL(defaultTarget));
}

ZC_TEST("Built MIR indirect-call terminator retains interface method receiver and arguments") {
  const auto type = tests::testSemanticType();
  const auto interface = tests::testDefinition(3);
  const auto method = tests::testDefinition(4);
  const auto destination = local(5);
  const auto normal = MirBlockId::fromOrdinal(2);
  ZC_REQUIRE(normal != zc::none);
  zc::Vector<MirOperand> arguments;
  arguments.add(MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()));
  zc::Maybe<MirBlockId> noUnwind;
  auto terminator = MirTerminator::indirectCall(
      interface, method, MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()),
      zc::mv(arguments), MirCallEffect::noActivation(), barePlace(destination, type),
      ZC_REQUIRE_NONNULL(normal), zc::mv(noUnwind), sourceSpan());
  ZC_EXPECT(terminator.kind() == MirTerminatorKind::IndirectCall);
  const auto& value = terminator.indirectCallValue();
  ZC_EXPECT(value.interface == interface);
  ZC_EXPECT(value.method == method);
  ZC_EXPECT(value.receiver.kind() == MirOperandKind::Constant);
  ZC_REQUIRE(value.arguments.size() == 1);
  ZC_EXPECT(value.destination.local() == destination);
  ZC_EXPECT(value.normalTarget == ZC_REQUIRE_NONNULL(normal));
  ZC_EXPECT(value.unwindTarget == zc::none);
}

ZC_TEST("Built MIR terminator clone preserves every kind") {
  const auto type = tests::testSemanticType();
  {
    auto terminator = MirTerminator::returnVoid(sourceSpan());
    ZC_EXPECT(terminator.clone().kind() == MirTerminatorKind::Return);
  }
  ZC_EXPECT(MirTerminator::unreachable(sourceSpan()).clone().kind() ==
            MirTerminatorKind::Unreachable);
  {
    const auto target = MirBlockId::fromOrdinal(2);
    ZC_REQUIRE(target != zc::none);
    auto terminator = MirTerminator::gotoTarget(ZC_REQUIRE_NONNULL(target), sourceSpan());
    auto cloned = terminator.clone();
    ZC_EXPECT(cloned.kind() == MirTerminatorKind::Goto);
    ZC_EXPECT(cloned.gotoValue().target == ZC_REQUIRE_NONNULL(target));
  }
  {
    zc::Vector<MirSwitchIntArm> arms;
    const auto armTarget = MirBlockId::fromOrdinal(3);
    ZC_REQUIRE(armTarget != zc::none);
    arms.add(MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true),
                             ZC_REQUIRE_NONNULL(armTarget)});
    const auto defaultTarget = MirBlockId::fromOrdinal(4);
    ZC_REQUIRE(defaultTarget != zc::none);
    auto terminator = MirTerminator::switchInt(
        MirOperand::constant(type, checker::checked::CanonicalConstValue::unit()), zc::mv(arms),
        ZC_REQUIRE_NONNULL(defaultTarget), sourceSpan());
    auto cloned = terminator.clone();
    ZC_EXPECT(cloned.kind() == MirTerminatorKind::SwitchInt);
    ZC_REQUIRE(cloned.switchIntValue().arms.size() == 1);
    ZC_EXPECT(cloned.switchIntValue().defaultTarget == ZC_REQUIRE_NONNULL(defaultTarget));
  }
}

// ---------------------------------------------------------------------------
// Call effect algebra
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR no-activation call effect never commits") {
  auto effect = MirCallEffect::noActivation();
  ZC_EXPECT(effect.kind() == MirCallEffectKind::NoActivation);
  ZC_EXPECT(!effect.commitsOnNormalEdge());
  ZC_EXPECT(effect.activatedMutableReceiver() == zc::none);
  ZC_EXPECT(effect.clone().kind() == MirCallEffectKind::NoActivation);
}

ZC_TEST("Built MIR mutable-receiver call effect commits on the normal edge") {
  const auto temporary = local(8);
  auto effect = MirCallEffect::activateMutableReceiver(temporary);
  ZC_EXPECT(effect.kind() == MirCallEffectKind::ActivateMutableReceiver);
  ZC_EXPECT(effect.commitsOnNormalEdge());
  ZC_REQUIRE(effect.activatedMutableReceiver() != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(effect.activatedMutableReceiver()) == temporary);
  ZC_EXPECT(effect.clone().commitsOnNormalEdge());
}

// ---------------------------------------------------------------------------
// Function and block structures
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR function retains owner kind result type scopes locals and blocks") {
  const auto owner = tests::testDefinition(0);
  const auto resultType = tests::testSemanticType();
  const auto scopeId = sourceScope(1);
  const auto localId = local(1);
  const auto blockId = MirBlockId::fromOrdinal(1);
  ZC_REQUIRE(blockId != zc::none);

  zc::Vector<MirSourceScope> sourceScopes;
  sourceScopes.add(MirSourceScope{scopeId, zc::none, sourceSpan()});

  zc::Vector<MirLocalDeclaration> locals;
  locals.add(
      MirLocalDeclaration{localId, MirLocalKind::UserLocal, resultType, scopeId, sourceSpan()});

  zc::Vector<MirStatement> statements;
  statements.add(MirStatement::storageLive(localId, sourceSpan()));
  zc::Vector<MirBasicBlock> blocks;
  blocks.add(MirBasicBlock{ZC_REQUIRE_NONNULL(blockId), scopeId, zc::mv(statements),
                           MirTerminator::returnVoid(sourceSpan())});

  auto function = MirFunction{owner,
                              MirFunctionKind::Function,
                              identity::DefinitionKind::Function,
                              resultType,
                              sourceSpan(),
                              zc::mv(sourceScopes),
                              zc::mv(locals),
                              zc::mv(blocks)};
  ZC_EXPECT(function.owner == owner);
  ZC_EXPECT(function.kind == MirFunctionKind::Function);
  ZC_EXPECT(function.sourceDefinitionKind == identity::DefinitionKind::Function);
  ZC_EXPECT(function.resultType == resultType);
  ZC_REQUIRE(function.sourceScopes.size() == 1);
  ZC_EXPECT(function.sourceScopes[0].id == scopeId);
  ZC_EXPECT(function.sourceScopes[0].parent == zc::none);
  ZC_REQUIRE(function.locals.size() == 1);
  ZC_EXPECT(function.locals[0].id == localId);
  ZC_EXPECT(function.locals[0].kind == MirLocalKind::UserLocal);
  ZC_EXPECT(function.locals[0].type == resultType);
  ZC_EXPECT(function.locals[0].sourceScope == scopeId);
  ZC_REQUIRE(function.blocks.size() == 1);
  ZC_EXPECT(function.blocks[0].id == ZC_REQUIRE_NONNULL(blockId));
  ZC_EXPECT(function.blocks[0].sourceScope == scopeId);
  ZC_REQUIRE(function.blocks[0].statements.size() == 1);
  ZC_EXPECT(function.blocks[0].statements[0].kind() == MirStatementKind::StorageLive);
  ZC_EXPECT(function.blocks[0].terminator.kind() == MirTerminatorKind::Return);
}

ZC_TEST("Built MIR basic block retains id scope statements and terminator") {
  const auto scopeId = sourceScope(2);
  const auto blockId = MirBlockId::fromOrdinal(7);
  ZC_REQUIRE(blockId != zc::none);
  const auto localId = local(3);

  zc::Vector<MirStatement> statements;
  statements.add(MirStatement::storageLive(localId, sourceSpan()));
  statements.add(MirStatement::storageDead(localId, sourceSpan()));
  auto block = MirBasicBlock{ZC_REQUIRE_NONNULL(blockId), scopeId, zc::mv(statements),
                             MirTerminator::unreachable(sourceSpan())};
  ZC_EXPECT(block.id == ZC_REQUIRE_NONNULL(blockId));
  ZC_EXPECT(block.sourceScope == scopeId);
  ZC_REQUIRE(block.statements.size() == 2);
  ZC_EXPECT(block.statements[0].kind() == MirStatementKind::StorageLive);
  ZC_EXPECT(block.statements[1].kind() == MirStatementKind::StorageDead);
  ZC_EXPECT(block.terminator.kind() == MirTerminatorKind::Unreachable);
}

// ---------------------------------------------------------------------------
// Revision identity and codec
// ---------------------------------------------------------------------------

ZC_TEST("Built MIR revision identity retains its digest bytes") {
  const auto digest = repeatedDigest(0x77);
  const auto revision = MirRevisionId::fromDigest(digest);
  ZC_EXPECT(revision.digest() == digest);
  ZC_EXPECT(MirRevisionId::fromDigest(repeatedDigest(0x77)).digest() == revision.digest());
  ZC_EXPECT(!(MirRevisionId::fromDigest(repeatedDigest(0x78)).digest() == revision.digest()));
}

zc::String revisionDigest(uint8_t contextByte, uint8_t moduleByte, uint8_t checkedByte,
                          uint8_t dispatchByte, uint8_t borrowByte,
                          zc::ArrayPtr<const zc::Array<uint8_t>> functions) {
  const uint8_t module[] = {moduleByte};
  auto encoded = MirRevisionCodec::encodeBuiltFramed(
      repeatedDigest(contextByte), zc::arrayPtr(module), repeatedDigest(checkedByte),
      repeatedDigest(dispatchByte), repeatedDigest(borrowByte), functions);
  ZC_REQUIRE(encoded != zc::none);
  auto digest = identity::sha256(ZC_REQUIRE_NONNULL(encoded).asPtr());
  ZC_REQUIRE(digest != zc::none);
  return zc::encodeHex(ZC_REQUIRE_NONNULL(digest).bytes());
}

ZC_TEST("Built MIR revision codec is deterministic for identical inputs") {
  const uint8_t module[] = {0xa1};
  const auto functions = oneFunctionRecord();
  auto first = MirRevisionCodec::encodeBuiltFramed(repeatedDigest(0x00), zc::arrayPtr(module),
                                                   repeatedDigest(0x22), repeatedDigest(0x33),
                                                   repeatedDigest(0x44), functions.asPtr());
  auto second = MirRevisionCodec::encodeBuiltFramed(repeatedDigest(0x00), zc::arrayPtr(module),
                                                    repeatedDigest(0x22), repeatedDigest(0x33),
                                                    repeatedDigest(0x44), functions.asPtr());
  ZC_REQUIRE(first != zc::none);
  ZC_REQUIRE(second != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(first).asPtr() == ZC_REQUIRE_NONNULL(second).asPtr());
}

ZC_TEST("Built MIR revision digest changes when the function record changes") {
  const auto baseline = revisionDigest(0x00, 0xa1, 0x22, 0x33, 0x44, oneFunctionRecord().asPtr());
  zc::Vector<uint8_t> otherBytes;
  otherBytes.add(0xb4);
  zc::Vector<zc::Array<uint8_t>> other;
  other.add(otherBytes.releaseAsArray());
  ZC_EXPECT(revisionDigest(0x00, 0xa1, 0x22, 0x33, 0x44, other.asPtr()) != baseline);
}

ZC_TEST("Built MIR revision digest changes when any input revision changes") {
  const auto functions = oneFunctionRecord();
  const auto baseline = revisionDigest(0x00, 0xa1, 0x22, 0x33, 0x44, functions.asPtr());
  ZC_EXPECT(revisionDigest(0x01, 0xa1, 0x22, 0x33, 0x44, functions.asPtr()) != baseline);
  ZC_EXPECT(revisionDigest(0x00, 0xa2, 0x22, 0x33, 0x44, functions.asPtr()) != baseline);
  ZC_EXPECT(revisionDigest(0x00, 0xa1, 0x23, 0x33, 0x44, functions.asPtr()) != baseline);
  ZC_EXPECT(revisionDigest(0x00, 0xa1, 0x22, 0x34, 0x44, functions.asPtr()) != baseline);
  ZC_EXPECT(revisionDigest(0x00, 0xa1, 0x22, 0x33, 0x45, functions.asPtr()) != baseline);
}

ZC_TEST("Built MIR revision digest distinguishes empty and non-empty function sets") {
  zc::Vector<zc::Array<uint8_t>> empty;
  const auto emptyDigest = revisionDigest(0x00, 0xa1, 0x22, 0x33, 0x44, empty.asPtr());
  const auto nonEmptyDigest =
      revisionDigest(0x00, 0xa1, 0x22, 0x33, 0x44, oneFunctionRecord().asPtr());
  ZC_EXPECT(emptyDigest != nonEmptyDigest);
}

// ---------------------------------------------------------------------------
// End-to-end builder and verifier publication
//
// The candidate-level rejection paths of BuiltMirVerifier require a
// session-owned BodyCheckingInput that is not publicly constructible in a
// unit test, so "invalid modules fail" is covered by the exposed structural
// validators (MirProjection::isStructurallyValid and
// MirPlace::hasConsistentTypeChain) tested above. The test below drives the
// production builder and verifier through a CompilerSession and asserts the
// published VerifiedBuiltMir shape, proving a valid module is accepted.
// ---------------------------------------------------------------------------

namespace package = driver::package;

template <typename Scalar>
Scalar mirFixtureScalar(zc::StringPtr text) {
  auto result = Scalar::fromCanonical(text);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture scalar");
}

identity::SortedFeatureSet mirEmptyFeatures() {
  zc::Vector<identity::FeatureName> values;
  auto result = identity::SortedFeatureSet::from(zc::mv(values));
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture feature set");
}

identity::CanonicalPackageSource mirPackageSource() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  return identity::CanonicalPackageSource::localPath(
      identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(segments)));
}

identity::PackageBaseKey mirPackageBase() {
  return identity::PackageBaseKey::from(mirPackageSource(),
                                        mirFixtureScalar<identity::PackageName>("app"_zc),
                                        mirFixtureScalar<identity::ResolvedVersion>("1.0.0"_zc));
}

identity::PackageKey mirPackageKey() {
  return identity::PackageKey::from(
      mirPackageSource(), mirFixtureScalar<identity::PackageName>("app"_zc),
      mirFixtureScalar<identity::ResolvedVersion>("1.0.0"_zc), mirEmptyFeatures());
}

identity::CanonicalRelativePath mirMainPath() {
  zc::Vector<identity::CanonicalPathSegment> segments;
  segments.add(mirFixtureScalar<identity::CanonicalPathSegment>("src"_zc));
  segments.add(mirFixtureScalar<identity::CanonicalPathSegment>("main.zom"_zc));
  return identity::CanonicalRelativePath::from(zc::mv(segments));
}

ir::TargetRegistrySnapshot mirTargetRegistry() {
  zc::Vector<ir::CanonicalTargetFeature> targetFeatures;
  auto targetSpec = ir::CanonicalTargetSpec::from(
      "x86_64-zom-none"_zc, "e-p:64:64"_zc, "generic"_zc, zc::mv(targetFeatures), "zom"_zc,
      ir::BackendPanicStrategy::Unwind, ir::ObjectFormat::Elf);
  ZC_REQUIRE(targetSpec != zc::none);
  zc::Vector<identity::TargetFeatureName> semanticFeatures;
  zc::Vector<ir::CanonicalTargetSpec> specifications;
  ZC_IF_SOME(value, targetSpec) { specifications.add(zc::mv(value)); }
  auto profile = ir::RegisteredTargetProfileRecord::from(
      []() {
        auto result = package::RegisteredTargetProfileName::from("host"_zc);
        ZC_REQUIRE(result != zc::none);
        ZC_IF_SOME(value, result) { return zc::mv(value); }
        ZC_FAIL_REQUIRE("invalid Built MIR fixture target profile name");
      }(),
      []() {
        zc::Vector<identity::TargetFeatureName> features;
        auto sorted = identity::SortedTargetFeatureSet::from(zc::mv(features));
        ZC_REQUIRE(sorted != zc::none);
        ZC_IF_SOME(values, sorted) {
          auto result = identity::CanonicalTargetSpecificationKey::from(
              mirFixtureScalar<identity::TargetComponentName>("x86_64"_zc),
              mirFixtureScalar<identity::TargetComponentName>("zom"_zc),
              mirFixtureScalar<identity::TargetComponentName>("none"_zc),
              mirFixtureScalar<identity::TargetComponentName>("unknown"_zc),
              mirFixtureScalar<identity::TargetComponentName>("zom"_zc), 64,
              identity::Endianness::Little, zc::mv(values));
          ZC_IF_SOME(value, result) { return zc::mv(value); }
        }
        ZC_FAIL_REQUIRE("invalid Built MIR fixture target projection");
      }(),
      zc::mv(semanticFeatures), zc::mv(specifications));
  ZC_REQUIRE(profile != zc::none);
  zc::Vector<ir::RegisteredTargetProfileRecord> profiles;
  ZC_IF_SOME(value, profile) { profiles.add(zc::mv(value)); }
  auto registryName = package::RegisteredTargetProfileName::from("host"_zc);
  ZC_REQUIRE(registryName != zc::none);
  auto registry =
      ir::TargetRegistrySnapshot::from(zc::mv(ZC_REQUIRE_NONNULL(registryName)), zc::mv(profiles));
  ZC_IF_SOME(value, registry) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture target registry");
}

package::RegisteredTargetSelection mirTargetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto service = registry.packageTargetService();
  ZC_REQUIRE(service != zc::none);
  ZC_IF_SOME(targets, service) {
    auto result = targets.select(zc::none, package::PackagePanicStrategy::Unwind);
    ZC_IF_SOME(value, result) { return zc::mv(value); }
  }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture target selection");
}

ir::VerifiedTargetSelection mirVerifiedTargetSelection(const ir::TargetRegistrySnapshot& registry) {
  auto result = registry.verify(mirTargetSelection(registry));
  ZC_REQUIRE(result.is<ir::VerifiedTargetSelection>());
  return zc::mv(result.get<ir::VerifiedTargetSelection>());
}

package::VerifiedPackageCompilationRequest mirCompilationRequest(
    const ir::TargetRegistrySnapshot& registry) {
  zc::Vector<package::VerifiedCompilationRoot> roots;
  roots.add(package::VerifiedCompilationRoot::from(
      mirPackageKey(), identity::CrateTargetKind::Binary,
      mirFixtureScalar<identity::TargetName>("app"_zc), 2026, false, mirMainPath()));
  auto result = package::VerifiedPackageCompilationRequest::from(
      zc::mv(roots), mirTargetSelection(registry), mirTargetSelection(registry),
      package::SelectedLanguageOptions{}, package::PackageLockMode::PreferLocked);
  ZC_IF_SOME(value, result) { return zc::mv(value); }
  ZC_FAIL_REQUIRE("invalid Built MIR fixture compilation request");
}

class MirMemoryFreshDirectory final : public package::FreshSourceDirectory {
public:
  MirMemoryFreshDirectory() : rootValue(zc::newInMemoryDirectory(zc::nullClock())) {}
  ~MirMemoryFreshDirectory() noexcept override = default;
  const zc::Directory& root() const override { return *rootValue; }
  zc::Maybe<package::MaterializationIssue> finish() override { return zc::none; }

private:
  zc::Own<zc::Directory> rootValue;
};

class MirMemoryFreshDirectoryFactory final : public package::FreshSourceDirectoryFactory {
public:
  package::FreshSourceDirectoryResult create() override {
    zc::Own<package::FreshSourceDirectory> result = zc::heap<MirMemoryFreshDirectory>();
    return zc::mv(result);
  }
};

package::DigestVerifiedSourceSnapshot mirSourceSnapshot(zc::StringPtr sourceText) {
  auto sourceDirectory = zc::newInMemoryDirectory(zc::nullClock());
  sourceDirectory
      ->openFile(zc::Path({"src"_zc, "main.zom"_zc}),
                 zc::WriteMode::CREATE | zc::WriteMode::CREATE_PARENT)
      ->writeAll(sourceText);
  MirMemoryFreshDirectoryFactory factory;
  package::SourceDirectoryMaterializer materializer;
  auto result = materializer.materialize(*sourceDirectory, factory);
  ZC_REQUIRE(result.is<package::DigestVerifiedSourceSnapshot>());
  return zc::mv(result.get<package::DigestVerifiedSourceSnapshot>());
}

package::ResolutionOutput mirResolution(zc::MemoryResource& resource, zc::StringPtr sourceText) {
  package::ManifestParser parser;
  zc::Vector<identity::CanonicalRelativePath> files;
  auto inventory = package::PackageSourceInventory::from(zc::mv(files));
  ZC_REQUIRE(inventory != zc::none);
  package::NormalizedManifest normalized = [&]() {
    ZC_IF_SOME(sourceInventory, inventory) {
      zc::Vector<identity::CanonicalPathSegment> documentSegments;
      documentSegments.add(mirFixtureScalar<identity::CanonicalPathSegment>("Zom.toml"_zc));
      auto parsed = parser.parseWorkspaceManifest(
          identity::CanonicalWorkspaceRelativePath::from(0, zc::mv(documentSegments)),
          "[package]\nname = \"app\"\nversion = \"1.0.0\"\nedition = \"2026\"\n"_zc,
          sourceInventory);
      ZC_REQUIRE(parsed.is<package::NormalizedManifest>());
      return zc::mv(parsed.get<package::NormalizedManifest>());
    }
    ZC_UNREACHABLE
  }();
  auto record = package::LocalPackageRecord::from(mirPackageBase(), zc::mv(normalized),
                                                  mirSourceSnapshot(sourceText));
  ZC_REQUIRE(record != zc::none);
  zc::Vector<package::ResolverRelease> releases;
  ZC_IF_SOME(value, record) { releases.add(package::ResolverRelease::fromLocal(value)); }
  zc::Vector<package::ResolverRoot> roots;
  roots.add(package::ResolverRoot::from(mirPackageBase(), mirEmptyFeatures(), false, false));
  auto result = package::PackageResolver::resolve(resource, roots, releases);
  ZC_REQUIRE(result.is<package::ResolutionOutput>());
  return zc::mv(result.get<package::ResolutionOutput>());
}

zc::Vector<package::ResolvedPackageSourceSnapshot> mirResolvedSnapshots(zc::StringPtr sourceText) {
  zc::Vector<package::ResolvedPackageSourceSnapshot> snapshots;
  snapshots.add(package::ResolvedPackageSourceSnapshot::from(mirPackageBase(),
                                                             mirSourceSnapshot(sourceText)));
  return snapshots;
}

class MirPipelineFixture final {
public:
  explicit MirPipelineFixture(zc::StringPtr sourceText)
      : session(contextFactory, languageOptions, compilerOptions) {
    auto registry = mirTargetRegistry();
    auto input = driver::VerifiedPackageSessionInput::from(
        mirCompilationRequest(registry), mirVerifiedTargetSelection(registry),
        mirVerifiedTargetSelection(registry),
        mirResolution(session.getPackageResolutionMemoryResource(), sourceText),
        mirResolvedSnapshots(sourceText));
    ZC_REQUIRE(input != zc::none);
    ZC_IF_SOME(value, input) { ZC_REQUIRE(session.installVerifiedPackageInput(zc::mv(value))); }
    driver::core_library_test::installCoreSources(session);
    const auto roots = session.getFinalizedCompilationRoots();
    ZC_REQUIRE(roots.size() == 1);
    ZC_REQUIRE(session.addVerifiedPackageRoot(roots[0]) != zc::none);
    ZC_REQUIRE(session.parseSources());
    ZC_REQUIRE(session.bindSources());
    ZC_REQUIRE(session.checkSources());
    ZC_REQUIRE(!session.hasDiagnosticErrors());
    ZC_REQUIRE(session.getOwnershipCheckedMirModules().size() == 1);
  }

  const VerifiedBuiltMir& builtMir() const {
    return session.getOwnershipCheckedMirModules()[0].builtMir();
  }

private:
  basic::LangOptions languageOptions;
  basic::CompilerOptions compilerOptions;
  identity::SemanticContextFactory contextFactory;
  driver::CompilerSession session;
};

ZC_TEST("Built MIR builder and verifier publish a verified module for a scalar initializer") {
  // The production builder and verifier run inside checkSources(); reaching the
  // published ownership-checked MIR proves the verifier accepted the candidate.
  MirPipelineFixture fixture("let value = 0;"_zc);
  const auto& builtMir = fixture.builtMir();
  ZC_REQUIRE(builtMir.functions().size() == 1);
  const auto& function = builtMir.functions()[0];
  ZC_EXPECT(function.kind == MirFunctionKind::ModuleInitializer);
  ZC_REQUIRE(function.blocks.size() >= 1);
  for (const auto& block : function.blocks) {
    ZC_EXPECT(block.id.isValid());
    ZC_EXPECT(block.sourceScope.isValid());
  }
  for (const auto& local : function.locals) {
    ZC_EXPECT(local.id.isValid());
    ZC_EXPECT(local.sourceScope.isValid());
  }
  for (const auto& scope : function.sourceScopes) { ZC_EXPECT(scope.id.isValid()); }
  ZC_EXPECT(builtMir.canonicalFunctionRecords().size() == builtMir.functions().size());
  // The revision digest is published and retrievable.
  const auto& digest = builtMir.revision().digest();
  ZC_EXPECT(digest.bytes().size() == 32);
}

}  // namespace

// Structural verifier mutation tests (RFC 0048 Phase 4).

namespace {

MirBlockId blockId(uint32_t ordinal) {
  auto result = MirBlockId::fromOrdinal(ordinal);
  ZC_REQUIRE(result != zc::none);
  return ZC_REQUIRE_NONNULL(result);
}

/// \brief Builds a minimal structurally valid MIR function for mutation tests.
MirFunction makeMinimalValidFunction(identity::SemanticTypeId resultType) {
  MirFunction function{
      tests::testDefinition(0),
      MirFunctionKind::Function,
      identity::DefinitionKind::Function,
      resultType,
      sourceSpan(),
      zc::Vector<MirSourceScope>{},
      zc::Vector<MirLocalDeclaration>{},
      zc::Vector<MirBasicBlock>{},
  };
  function.sourceScopes.add(MirSourceScope{sourceScope(1), zc::none, sourceSpan()});
  zc::Vector<MirStatement> statements;
  function.blocks.add(MirBasicBlock{blockId(1), sourceScope(1), zc::mv(statements),
                                    MirTerminator::returnVoid(sourceSpan())});
  return function;
}

}  // namespace

ZC_TEST("Built MIR structural verifier accepts a minimal valid function") {
  const auto type = tests::testSemanticType();
  auto function = makeMinimalValidFunction(type);
  ZC_EXPECT(validateStructuralWellFormedness(function));
}

ZC_TEST("Built MIR structural verifier rejects non-dense block ordinals") {
  const auto type = tests::testSemanticType();
  auto function = makeMinimalValidFunction(type);
  // Mutate: give the sole block ordinal 3, leaving a gap at 2.
  function.blocks[0].id = blockId(3);
  ZC_EXPECT(!validateStructuralWellFormedness(function));
}

ZC_TEST("Built MIR structural verifier rejects an unreachable block") {
  const auto type = tests::testSemanticType();
  auto function = makeMinimalValidFunction(type);
  // Add a second block that no terminator targets.
  zc::Vector<MirStatement> statements;
  function.blocks.add(MirBasicBlock{blockId(2), sourceScope(1), zc::mv(statements),
                                    MirTerminator::returnVoid(sourceSpan())});
  ZC_EXPECT(!validateStructuralWellFormedness(function));
}

ZC_TEST("Built MIR structural verifier rejects an invalid scope reference") {
  const auto type = tests::testSemanticType();
  auto function = makeMinimalValidFunction(type);
  // Mutate: the block references scope 2, but only scope 1 exists.
  function.blocks[0].sourceScope = sourceScope(2);
  ZC_EXPECT(!validateStructuralWellFormedness(function));
}

ZC_TEST("Built MIR structural verifier rejects a return type mismatch") {
  tests::TestSemanticTypeContext context;
  const auto returnType = context.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto wrongType = context.internPrimitive(type::semantic::PrimitiveKind::Bool);
  auto function = makeMinimalValidFunction(returnType);
  // Replace the void return with a boolean-typed value return.
  function.blocks[0].terminator = MirTerminator::returnValue(
      MirOperand::constant(wrongType, checker::checked::CanonicalConstValue::boolean(true)),
      sourceSpan());
  ZC_EXPECT(!validateStructuralWellFormedness(function));
}

ZC_TEST("Built MIR structural verifier rejects an assignment type mismatch") {
  tests::TestSemanticTypeContext context;
  const auto localType = context.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto wrongType = context.internPrimitive(type::semantic::PrimitiveKind::Bool);
  auto function = makeMinimalValidFunction(localType);
  // Add a local of localType.
  function.locals.add(MirLocalDeclaration{local(1), MirLocalKind::UserLocal, localType,
                                          sourceScope(1), sourceSpan()});
  // StorageLive for the local.
  function.blocks[0].statements.add(MirStatement::storageLive(local(1), sourceSpan()));
  // Assign a boolean-typed rvalue to the i32 local.
  zc::Vector<MirProjection> projections;
  function.blocks[0].statements.add(
      MirStatement::assign(MirPlace(local(1), localType, zc::mv(projections), localType),
                           MirRvalue::use(MirOperand::constant(
                               wrongType, checker::checked::CanonicalConstValue::boolean(true))),
                           MirInitializationKind::Initialize, sourceSpan()));
  ZC_EXPECT(!validateStructuralWellFormedness(function));
}

ZC_TEST("Built MIR structural verifier rejects a use without StorageLive") {
  const auto type = tests::testSemanticType();
  auto function = makeMinimalValidFunction(type);
  // Add a local but NO StorageLive.
  function.locals.add(
      MirLocalDeclaration{local(1), MirLocalKind::UserLocal, type, sourceScope(1), sourceSpan()});
  // Assign to the local without StorageLive.
  zc::Vector<MirProjection> projections;
  function.blocks[0].statements.add(MirStatement::assign(
      MirPlace(local(1), type, zc::mv(projections), type),
      MirRvalue::use(MirOperand::constant(type, checker::checked::CanonicalConstValue::unit())),
      MirInitializationKind::Initialize, sourceSpan()));
  ZC_EXPECT(!validateStructuralWellFormedness(function));
}

ZC_TEST("Built MIR structural verifier rejects a parameter after a non-parameter") {
  const auto type = tests::testSemanticType();
  auto function = makeMinimalValidFunction(type);
  // Add a non-parameter local first, then a parameter local.
  function.locals.add(
      MirLocalDeclaration{local(1), MirLocalKind::UserLocal, type, sourceScope(1), sourceSpan()});
  function.locals.add(
      MirLocalDeclaration{local(2), MirLocalKind::Parameter, type, sourceScope(1), sourceSpan()});
  ZC_EXPECT(!validateStructuralWellFormedness(function));
}

}  // namespace zomlang::compiler::mir
