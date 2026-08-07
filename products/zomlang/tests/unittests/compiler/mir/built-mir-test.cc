// Copyright (c) 2026 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "zomlang/compiler/mir/built-mir.h"

#include "zc/core/encoding.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/identity/sha256.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"

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

MirPlace place(MirLocalId localValue, identity::DefId field, identity::DefId variant) {
  zc::Vector<MirProjection> projections;
  projections.add(MirProjection::field(field));
  projections.add(MirProjection::index(local(2)));
  projections.add(MirProjection::dereference());
  projections.add(MirProjection::downcast(variant));
  auto subslice = MirProjection::subslice(1, 3);
  ZC_REQUIRE(subslice != zc::none);
  projections.add(zc::mv(ZC_REQUIRE_NONNULL(subslice)));
  return MirPlace(localValue, zc::mv(projections));
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

  auto invalidSubslice = MirProjection::subslice(4, 3);
  ZC_EXPECT(invalidSubslice == zc::none);
  auto projectionPlace = place(firstLocal, field, variant);
  ZC_REQUIRE(projectionPlace.projections().size() == 5);
  const auto projections = projectionPlace.projections();
  ZC_EXPECT(projections[0].kind() == MirProjectionKind::Field);
  ZC_EXPECT(projections[0].fieldValue().field == field);
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
  ZC_EXPECT(clonedPlace.projections().size() == projections.size());

  auto copy = MirOperand::copy(place(firstLocal, field, variant));
  auto move = MirOperand::move(place(firstLocal, field, variant));
  ZC_EXPECT(copy.kind() == MirOperandKind::Copy);
  ZC_EXPECT(move.kind() == MirOperandKind::Move);
  ZC_EXPECT(copy.place().local() == firstLocal);
  ZC_EXPECT(move.place().local() == firstLocal);
  auto clonedCopy = copy.clone();
  auto clonedMove = move.clone();
  ZC_EXPECT(clonedCopy.kind() == MirOperandKind::Copy);
  ZC_EXPECT(clonedMove.kind() == MirOperandKind::Move);

  auto assignment =
      MirStatement::assign(place(firstLocal, field, variant),
                           MirRvalue::use(MirOperand::copy(place(firstLocal, field, variant))),
                           MirInitializationKind::Initialize);
  auto storageLive = MirStatement::storageLive(firstLocal);
  auto storageDead = MirStatement::storageDead(firstLocal);
  auto borrow = MirStatement::borrowCreation(
      place(firstLocal, field, variant), MirBorrowKind::Mutable, place(firstLocal, field, variant));
  auto discriminant = MirStatement::setDiscriminant(place(firstLocal, field, variant), variant);
  auto deinitialize = MirStatement::deinitialize(place(firstLocal, field, variant));
  ZC_EXPECT(assignment.kind() == MirStatementKind::Assign);
  ZC_EXPECT(assignment.assignmentValue().initialization == MirInitializationKind::Initialize);
  ZC_EXPECT(storageLive.storageLocal() == firstLocal);
  ZC_EXPECT(storageDead.storageLocal() == firstLocal);
  ZC_EXPECT(borrow.borrowCreationValue().kind == MirBorrowKind::Mutable);
  ZC_EXPECT(discriminant.setDiscriminantValue().variant == variant);
  ZC_EXPECT(deinitialize.deinitializeValue().destination.local() == firstLocal);
  ZC_EXPECT(assignment.clone().kind() == MirStatementKind::Assign);
  ZC_EXPECT(storageLive.clone().kind() == MirStatementKind::StorageLive);
  ZC_EXPECT(storageDead.clone().kind() == MirStatementKind::StorageDead);
  ZC_EXPECT(borrow.clone().kind() == MirStatementKind::BorrowCreation);
  ZC_EXPECT(discriminant.clone().kind() == MirStatementKind::SetDiscriminant);
  ZC_EXPECT(deinitialize.clone().kind() == MirStatementKind::Deinitialize);

  auto returning = MirTerminator::returnValue(MirOperand::move(place(firstLocal, field, variant)));
  auto voidReturn = MirTerminator::returnVoid();
  auto unreachable = MirTerminator::unreachable();
  ZC_EXPECT(returning.kind() == MirTerminatorKind::Return);
  ZC_REQUIRE(returning.returnValue().value != zc::none);
  ZC_EXPECT(voidReturn.returnValue().value == zc::none);
  ZC_EXPECT(unreachable.kind() == MirTerminatorKind::Unreachable);
  ZC_EXPECT(returning.clone().kind() == MirTerminatorKind::Return);
  ZC_EXPECT(voidReturn.clone().kind() == MirTerminatorKind::Return);
  ZC_EXPECT(unreachable.clone().kind() == MirTerminatorKind::Unreachable);
}

}  // namespace
}  // namespace zomlang::compiler::mir
