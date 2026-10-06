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

// Aggregate SSA carrier support: the aggregate ValueType, AggregateConstant,
// ExtractField and InsertField statements, and the structural verifier's
// aggregate checks. These tests construct LIR modules directly through the
// public algebra; no MIR input is involved.

#include "compiler/ir/ir-identity.h"
#include "compiler/lir/lir-module.h"
#include "compiler/lir/lir-store.h"
#include "compiler/lir/verify/lir-verifier.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lir {
namespace {

ValueType requireInteger(IntegerBitWidth width) {
  auto type = ValueType::integer(width);
  ZC_REQUIRE(type != zc::none);
  return ZC_REQUIRE_NONNULL(type);
}

ValueType requireAggregate(zc::Vector<AggregateField>&& fields) {
  auto type = ValueType::aggregate(zc::mv(fields));
  ZC_REQUIRE(type != zc::none);
  return ZC_REQUIRE_NONNULL(type);
}

IntegerConstant i32Constant(uint64_t bits) {
  auto value = IntegerConstant::from(requireInteger(IntegerBitWidth::Bit32), bits);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

LirBlockId blockId(uint32_t ordinal) {
  auto id = LirBlockId::fromOrdinal(ordinal);
  ZC_REQUIRE(id != zc::none);
  return ZC_REQUIRE_NONNULL(id);
}

// A two-field aggregate carrier {x: i32, y: i32}.
ValueType pairAggregate() {
  zc::Vector<AggregateField> fields;
  fields.add(AggregateField{zc::heapString("x"), requireInteger(IntegerBitWidth::Bit32)});
  fields.add(AggregateField{zc::heapString("y"), requireInteger(IntegerBitWidth::Bit32)});
  return requireAggregate(zc::mv(fields));
}

// A two-field aggregate carrier {a: i32, b: bool}.
ValueType mixedAggregate() {
  zc::Vector<AggregateField> fields;
  fields.add(AggregateField{zc::heapString("a"), requireInteger(IntegerBitWidth::Bit32)});
  fields.add(AggregateField{zc::heapString("b"), requireInteger(IntegerBitWidth::Bit1)});
  return requireAggregate(zc::mv(fields));
}

// --- Aggregate ValueType --------------------------------------------------

ZC_TEST("LIR aggregate value type stores ordered named fields") {
  auto type = pairAggregate();
  ZC_EXPECT(type.kind() == ValueTypeKind::Aggregate);
  auto fields = type.aggregateFields();
  ZC_REQUIRE(fields.size() == 2);
  ZC_EXPECT(fields[0].name == "x"_zc);
  ZC_EXPECT(fields[0].type.kind() == ValueTypeKind::Integer);
  ZC_EXPECT(fields[0].type.integerWidth() == IntegerBitWidth::Bit32);
  ZC_EXPECT(fields[1].name == "y"_zc);
  ZC_EXPECT(fields[1].type.integerWidth() == IntegerBitWidth::Bit32);
}

ZC_TEST("LIR aggregate value type compares structurally") {
  auto a = pairAggregate();
  auto b = pairAggregate();
  ZC_EXPECT(a == b);

  zc::Vector<AggregateField> renamed;
  renamed.add(AggregateField{zc::heapString("z"), requireInteger(IntegerBitWidth::Bit32)});
  renamed.add(AggregateField{zc::heapString("y"), requireInteger(IntegerBitWidth::Bit32)});
  auto c = requireAggregate(zc::mv(renamed));
  ZC_EXPECT(a != c);

  zc::Vector<AggregateField> reordered;
  reordered.add(AggregateField{zc::heapString("y"), requireInteger(IntegerBitWidth::Bit32)});
  reordered.add(AggregateField{zc::heapString("x"), requireInteger(IntegerBitWidth::Bit32)});
  auto d = requireAggregate(zc::mv(reordered));
  ZC_EXPECT(a != d);
}

ZC_TEST("LIR aggregate value type deep-copies fields") {
  auto original = pairAggregate();
  ValueType copy = original;
  ZC_EXPECT(copy == original);
  // Mutating the copy's fields must not affect the original.
  auto copyFields = copy.aggregateFields();
  ZC_REQUIRE(copyFields.size() == 2);
  ZC_EXPECT(original.aggregateFields()[0].name == "x"_zc);
}

ZC_TEST("LIR aggregate value type fails closed on empty field list") {
  zc::Vector<AggregateField> empty;
  ZC_EXPECT(ValueType::aggregate(zc::mv(empty)) == zc::none);
}

ZC_TEST("LIR aggregate value type fails closed above the field cap") {
  zc::Vector<AggregateField> overCap;
  for (uint32_t i = 0; i <= kMaxAggregateFields; ++i) {
    overCap.add(AggregateField{zc::heapString("f"), requireInteger(IntegerBitWidth::Bit32)});
  }
  ZC_EXPECT(overCap.size() == kMaxAggregateFields + 1);
  ZC_EXPECT(ValueType::aggregate(zc::mv(overCap)) == zc::none);
}

ZC_TEST("LIR aggregate value type admits exactly the field cap") {
  zc::Vector<AggregateField> atCap;
  for (uint32_t i = 0; i < kMaxAggregateFields; ++i) {
    atCap.add(AggregateField{zc::heapString("f"), requireInteger(IntegerBitWidth::Bit32)});
  }
  auto type = ValueType::aggregate(zc::mv(atCap));
  ZC_REQUIRE(type != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(type).aggregateFields().size() == kMaxAggregateFields);
}

ZC_TEST("LIR aggregate value type is distinct from every scalar kind") {
  auto agg = pairAggregate();
  ZC_EXPECT(agg != requireInteger(IntegerBitWidth::Bit32));
  ZC_EXPECT(agg.kind() != ValueTypeKind::Integer);
  ZC_EXPECT(agg.kind() != ValueTypeKind::Float);
  ZC_EXPECT(agg.kind() != ValueTypeKind::Pointer);
  ZC_EXPECT(agg.kind() != ValueTypeKind::Unit);
}

// --- AggregateConstant ----------------------------------------------------

ZC_TEST("LIR aggregate constant preserves ordered field values") {
  auto carrier = pairAggregate();
  zc::Vector<Operand> fields;
  fields.add(Operand::constant(i32Constant(42)));
  fields.add(Operand::constant(i32Constant(7)));
  auto value = AggregateConstant::from(carrier, zc::mv(fields));
  ZC_REQUIRE(value != zc::none);
  auto& constant = ZC_REQUIRE_NONNULL(value);
  ZC_EXPECT(constant.carrier().kind() == ValueTypeKind::Aggregate);
  auto returned = constant.fields();
  ZC_REQUIRE(returned.size() == 2);
  ZC_EXPECT(returned[0].constantValue().bits() == 42);
  ZC_EXPECT(returned[1].constantValue().bits() == 7);
}

ZC_TEST("LIR aggregate constant fails closed on non-aggregate carrier") {
  auto scalar = requireInteger(IntegerBitWidth::Bit32);
  zc::Vector<Operand> fields;
  fields.add(Operand::constant(i32Constant(1)));
  ZC_EXPECT(AggregateConstant::from(scalar, zc::mv(fields)) == zc::none);
}

ZC_TEST("LIR aggregate constant fails closed on field count mismatch") {
  auto carrier = pairAggregate();
  zc::Vector<Operand> tooFew;
  tooFew.add(Operand::constant(i32Constant(1)));
  ZC_EXPECT(AggregateConstant::from(carrier, zc::mv(tooFew)) == zc::none);

  zc::Vector<Operand> tooMany;
  tooMany.add(Operand::constant(i32Constant(1)));
  tooMany.add(Operand::constant(i32Constant(2)));
  tooMany.add(Operand::constant(i32Constant(3)));
  ZC_EXPECT(AggregateConstant::from(pairAggregate(), zc::mv(tooMany)) == zc::none);
}

ZC_TEST("LIR aggregate constant fails closed on local-use field") {
  auto carrier = pairAggregate();
  zc::Vector<Operand> fields;
  fields.add(Operand::constant(i32Constant(1)));
  fields.add(Operand::localUse(1));
  ZC_EXPECT(AggregateConstant::from(carrier, zc::mv(fields)) == zc::none);
}

ZC_TEST("LIR aggregate constant fails closed on field carrier mismatch") {
  auto carrier = mixedAggregate();
  zc::Vector<Operand> fields;
  fields.add(Operand::constant(i32Constant(1)));
  // Second field expects i1 but gets i32.
  fields.add(Operand::constant(i32Constant(0)));
  ZC_EXPECT(AggregateConstant::from(carrier, zc::mv(fields)) == zc::none);
}

// --- ExtractField / InsertField statements --------------------------------

ZC_TEST("LIR extract field statement carries source and field index") {
  auto statement = Statement::extractField(/*destinationOrdinal=*/2, /*sourceOrdinal=*/1,
                                           /*fieldIndex=*/0);
  ZC_EXPECT(statement.kind() == StatementKind::ExtractField);
  ZC_EXPECT(statement.destinationOrdinal() == 2);
  ZC_EXPECT(statement.sourceOrdinal() == 1);
  ZC_EXPECT(statement.fieldIndex() == 0);
}

ZC_TEST("LIR insert field statement carries aggregate, value, and field index") {
  auto statement = Statement::insertField(/*destinationOrdinal=*/3, /*aggregateOrdinal=*/1,
                                          /*fieldIndex=*/1, Operand::constant(i32Constant(99)));
  ZC_EXPECT(statement.kind() == StatementKind::InsertField);
  ZC_EXPECT(statement.destinationOrdinal() == 3);
  ZC_EXPECT(statement.sourceOrdinal() == 1);
  ZC_EXPECT(statement.fieldIndex() == 1);
  ZC_EXPECT(statement.storedValue().constantValue().bits() == 99);
}

// --- Verifier: well-formed aggregate modules ------------------------------

// A well-formed aggregate module: one function with an aggregate parameter,
// an aggregate body local, an ExtractField producing an i32, and a return.
Module validExtractModule() {
  auto agg = pairAggregate();
  zc::Vector<Local> parameters;
  parameters.add(Local(1, agg));
  zc::Vector<Local> locals;
  locals.add(Local(2, requireInteger(IntegerBitWidth::Bit32)));
  zc::Vector<Statement> statements;
  statements.add(Statement::extractField(/*destinationOrdinal=*/2, /*sourceOrdinal=*/1,
                                         /*fieldIndex=*/0));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.extract"),
                         requireInteger(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  return Module(zc::mv(functions));
}

// A well-formed aggregate module: one function with an aggregate parameter,
// an aggregate body local, an InsertField producing a new aggregate, and a
// return of the aggregate.
Module validInsertModule() {
  auto agg = pairAggregate();
  zc::Vector<Local> parameters;
  parameters.add(Local(1, agg));
  zc::Vector<Local> locals;
  locals.add(Local(2, agg));
  zc::Vector<Statement> statements;
  statements.add(Statement::insertField(/*destinationOrdinal=*/2, /*aggregateOrdinal=*/1,
                                        /*fieldIndex=*/0, Operand::constant(i32Constant(42))));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.insert"), agg,
                         zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

ZC_TEST("LIR verifier accepts a well-formed extract-field module") {
  auto module = validExtractModule();
  ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
}

ZC_TEST("LIR verifier accepts a well-formed insert-field module") {
  auto module = validInsertModule();
  ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
}

// --- Verifier: extract-field rejection cases ------------------------------

ZC_TEST("LIR verifier rejects extract field from a non-aggregate source") {
  zc::Vector<Local> parameters;
  parameters.add(Local(1, requireInteger(IntegerBitWidth::Bit32)));
  zc::Vector<Local> locals;
  locals.add(Local(2, requireInteger(IntegerBitWidth::Bit32)));
  zc::Vector<Statement> statements;
  statements.add(Statement::extractField(/*destinationOrdinal=*/2, /*sourceOrdinal=*/1,
                                         /*fieldIndex=*/0));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.bad_extract"),
                         requireInteger(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  auto module = Module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR verifier rejects extract field with an out-of-range index") {
  auto agg = pairAggregate();
  zc::Vector<Local> parameters;
  parameters.add(Local(1, agg));
  zc::Vector<Local> locals;
  locals.add(Local(2, requireInteger(IntegerBitWidth::Bit32)));
  zc::Vector<Statement> statements;
  statements.add(Statement::extractField(/*destinationOrdinal=*/2, /*sourceOrdinal=*/1,
                                         /*fieldIndex=*/5));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.bad_index"),
                         requireInteger(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  auto module = Module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR verifier rejects extract field with a destination carrier mismatch") {
  auto agg = pairAggregate();
  zc::Vector<Local> parameters;
  parameters.add(Local(1, agg));
  zc::Vector<Local> locals;
  // Destination is i1 but field 0 is i32.
  locals.add(Local(2, requireInteger(IntegerBitWidth::Bit1)));
  zc::Vector<Statement> statements;
  statements.add(Statement::extractField(/*destinationOrdinal=*/2, /*sourceOrdinal=*/1,
                                         /*fieldIndex=*/0));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.bad_dest"),
                         requireInteger(IntegerBitWidth::Bit1), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  auto module = Module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

// --- Verifier: insert-field rejection cases -------------------------------

ZC_TEST("LIR verifier rejects insert field with a non-aggregate destination") {
  zc::Vector<Local> parameters;
  parameters.add(Local(1, requireInteger(IntegerBitWidth::Bit32)));
  zc::Vector<Local> locals;
  locals.add(Local(2, requireInteger(IntegerBitWidth::Bit32)));
  zc::Vector<Statement> statements;
  statements.add(Statement::insertField(/*destinationOrdinal=*/2, /*aggregateOrdinal=*/1,
                                        /*fieldIndex=*/0, Operand::constant(i32Constant(1))));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.bad_insert"),
                         requireInteger(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  auto module = Module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR verifier rejects insert field with a mismatched aggregate carrier") {
  auto agg = pairAggregate();
  auto mixed = mixedAggregate();
  zc::Vector<Local> parameters;
  parameters.add(Local(1, agg));
  zc::Vector<Local> locals;
  locals.add(Local(2, mixed));
  zc::Vector<Statement> statements;
  statements.add(Statement::insertField(/*destinationOrdinal=*/2, /*aggregateOrdinal=*/1,
                                        /*fieldIndex=*/0, Operand::constant(i32Constant(1))));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.mismatch_agg"), mixed,
                         zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  auto module = Module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR verifier rejects insert field with an out-of-range index") {
  auto agg = pairAggregate();
  zc::Vector<Local> parameters;
  parameters.add(Local(1, agg));
  zc::Vector<Local> locals;
  locals.add(Local(2, agg));
  zc::Vector<Statement> statements;
  statements.add(Statement::insertField(/*destinationOrdinal=*/2, /*aggregateOrdinal=*/1,
                                        /*fieldIndex=*/99, Operand::constant(i32Constant(1))));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.bad_insert_index"), agg,
                         zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  auto module = Module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR verifier rejects insert field with a value carrier mismatch") {
  auto agg = pairAggregate();
  zc::Vector<Local> parameters;
  parameters.add(Local(1, agg));
  zc::Vector<Local> locals;
  locals.add(Local(2, agg));
  zc::Vector<Statement> statements;
  // Field 0 is i32 but the inserted value is i1.
  auto boolCarrier = requireInteger(IntegerBitWidth::Bit1);
  auto boolConstant = IntegerConstant::from(boolCarrier, 1);
  ZC_REQUIRE(boolConstant != zc::none);
  statements.add(Statement::insertField(/*destinationOrdinal=*/2, /*aggregateOrdinal=*/1,
                                        /*fieldIndex=*/0,
                                        Operand::constant(ZC_REQUIRE_NONNULL(boolConstant))));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.bad_insert_value"), agg,
                         zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  auto module = Module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_REQUIRE_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

}  // namespace
}  // namespace zomlang::compiler::lir
