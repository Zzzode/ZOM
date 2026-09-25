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
// See the License for the specific language governing permissions and limitations
// under the License.

// RFC 0053 slice 1: the structural LIR verifier accepts well-formed instances
// of every admitted module shape and rejects one minimal mutation per closed
// fault tag. These tests construct modules directly through the public LIR
// algebra; no MIR input is involved at the structural stage.

#include "compiler/lir/verify/lir-verifier.h"

#include "compiler/ir/ir-identity.h"
#include "compiler/lir/lir-module.h"
#include "compiler/lir/lir-store.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lir {
namespace {

ValueType carrier(IntegerBitWidth width) {
  auto value = ValueType::integer(width);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

IntegerConstant constant(ValueType valueType, uint64_t bits) {
  auto value = IntegerConstant::from(valueType, bits);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

LirBlockId blockId(uint32_t ordinal) {
  auto id = LirBlockId::fromOrdinal(ordinal);
  ZC_REQUIRE(id != zc::none);
  return ZC_REQUIRE_NONNULL(id);
}

// A well-formed scalar module: one function, one block returning an i32
// constant. This is the LIR shape of the scalar module initializer.
Module validScalarModule() {
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1),
                        Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 0))));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.module_init"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

// A well-formed two-function call module: the caller stores a zero-argument
// call result into its one local and returns it; the callee returns an i32
// constant.
Module validCallModule() {
  zc::Vector<Function> functions;
  {
    zc::Vector<BasicBlock> blocks;
    zc::Vector<Statement> entryStatements;
    zc::Vector<Operand> noArguments;
    blocks.add(BasicBlock(
        blockId(1), zc::mv(entryStatements),
        ZC_ASSERT_NONNULL(Terminator::callFunction(
            /*calleeIndex=*/1, /*destinationOrdinal=*/1, zc::mv(noArguments), blockId(2)))));
    zc::Vector<Statement> continuationStatements;
    blocks.add(BasicBlock(blockId(2), zc::mv(continuationStatements),
                          Terminator::returnLocal(/*localOrdinal=*/1)));
    zc::Vector<Local> parameters;
    zc::Vector<Local> locals;
    locals.add(Local(1, carrier(IntegerBitWidth::Bit32)));
    functions.add(Function(tests::testDefinition(0), zc::heapString("zom.caller"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  {
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(blockId(1),
                          Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 7))));
    functions.add(Function(tests::testDefinition(0), zc::heapString("zom.callee"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  }
  return Module(zc::mv(functions));
}

// A well-formed four-block comparison diamond: one i1 parameter, an i32
// result local and an i1 comparison temporary, an entry comparing two i32
// constants and branching, two assignment arms, and a join return.
Module validConditionalModule() {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::compare(
        /*destinationOrdinal=*/3, ComparisonOp::Eq,
        Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 1)),
        Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 2))));
    blocks.add(BasicBlock(blockId(1), zc::mv(statements),
                          Terminator::condBranch(/*conditionOrdinal=*/3, blockId(2), blockId(3))));
  }
  {
    zc::Vector<Statement> statements;
    statements.add(
        Statement::assign(2, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 5))));
    blocks.add(BasicBlock(blockId(2), zc::mv(statements), Terminator::gotoBlock(blockId(4))));
  }
  {
    zc::Vector<Statement> statements;
    statements.add(
        Statement::assign(2, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 7))));
    blocks.add(BasicBlock(blockId(3), zc::mv(statements), Terminator::gotoBlock(blockId(4))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(4), zc::mv(statements), Terminator::returnLocal(2)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, carrier(IntegerBitWidth::Bit1)));
  zc::Vector<Local> locals;
  locals.add(Local(2, carrier(IntegerBitWidth::Bit32)));
  locals.add(Local(3, carrier(IntegerBitWidth::Bit1)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.conditional_cmp"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  return Module(zc::mv(functions));
}

// A well-formed one-block arithmetic module: one i32 parameter, an i32 result
// local, and one arithmetic statement adding the parameter to an i32 constant,
// followed by a return of the result.
Module validArithmeticModule(ArithmeticOp op = ArithmeticOp::Add) {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::arithmetic(
        /*destinationOrdinal=*/2, op, Operand::localUse(/*localOrdinal=*/1),
        Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 1))));
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Local> locals;
  locals.add(Local(2, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("zom.arithmetic"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  return Module(zc::mv(functions));
}

// A well-formed shared-receiver call module: the caller folds its one-field
// owner slot, takes its address into a pointer temporary, calls the pointer-
// parameter callee with that address, and returns the result; the callee
// returns an i32 constant.
Module validReceiverCallModule() {
  zc::Vector<Function> functions;
  {
    zc::Vector<Statement> statements;
    statements.add(
        Statement::assign(1, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 0))));
    statements.add(Statement::takeAddress(2, 1));
    zc::Vector<Operand> arguments;
    arguments.add(Operand::localUse(2));
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(
        blockId(1), zc::mv(statements),
        ZC_ASSERT_NONNULL(Terminator::callFunction(
            /*calleeIndex=*/1, /*destinationOrdinal=*/3, zc::mv(arguments), blockId(2)))));
    blocks.add(BasicBlock(blockId(2), zc::Vector<Statement>(), Terminator::returnLocal(3)));
    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(1, carrier(IntegerBitWidth::Bit32)));
    locals.add(Local(2, ValueType::pointer(0)));
    locals.add(Local(3, carrier(IntegerBitWidth::Bit32)));
    functions.add(Function(tests::testDefinition(0), zc::heapString("zom.module_init"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(noParameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  {
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(blockId(1),
                          Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 7))));
    zc::Vector<Local> parameters;
    parameters.add(Local(1, ValueType::pointer(0)));
    zc::Vector<Local> noLocals;
    functions.add(Function(tests::testDefinition(1), zc::heapString("zom.callee"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(noLocals),
                           zc::mv(blocks)));
  }
  return Module(zc::mv(functions));
}

ZC_TEST("LIR structural verifier accepts the admitted scalar, call, and diamond modules") {
  {
    Module module = validScalarModule();
    ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
  }
  {
    Module module = validCallModule();
    ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
  }
  {
    Module module = validConditionalModule();
    ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
  }
  {
    Module module = validArithmeticModule();
    ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
  }
}

ZC_TEST("LIR structural verifier accepts a shared-receiver take-address call module") {
  Module module = validReceiverCallModule();
  ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
}

// Builds the receiver field-read module with the callee's load field tuned:
// the base and destination carriers, byte offset, and base ordinal are all
// parameterized so malformed carriers/ordinals can be exercised.
Module fieldReadModule(ValueType baseCarrier, ValueType destinationCarrier,
                       uint32_t basePointerOrdinal, uint32_t fieldOffsetBytes) {
  zc::Vector<Function> functions;
  {
    zc::Vector<Statement> statements;
    statements.add(
        Statement::assign(1, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 42))));
    statements.add(Statement::takeAddress(2, 1));
    zc::Vector<Operand> arguments;
    arguments.add(Operand::localUse(2));
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(
        blockId(1), zc::mv(statements),
        ZC_ASSERT_NONNULL(Terminator::callFunction(
            /*calleeIndex=*/1, /*destinationOrdinal=*/3, zc::mv(arguments), blockId(2)))));
    blocks.add(BasicBlock(blockId(2), zc::Vector<Statement>(), Terminator::returnLocal(3)));
    zc::Vector<Local> noParameters;
    zc::Vector<Local> locals;
    locals.add(Local(1, carrier(IntegerBitWidth::Bit32)));
    locals.add(Local(2, ValueType::pointer(0)));
    locals.add(Local(3, carrier(IntegerBitWidth::Bit32)));
    functions.add(Function(tests::testDefinition(0), zc::heapString("zom.module_init"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(noParameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::loadField(2, basePointerOrdinal, fieldOffsetBytes));
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
    zc::Vector<Local> parameters;
    parameters.add(Local(1, baseCarrier));
    zc::Vector<Local> locals;
    locals.add(Local(2, destinationCarrier));
    functions.add(Function(tests::testDefinition(1), zc::heapString("zom.callee"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  return Module(zc::mv(functions));
}

ZC_TEST("LIR structural verifier accepts an offset-zero receiver load-field callee") {
  Module module = fieldReadModule(ValueType::pointer(0), carrier(IntegerBitWidth::Bit32),
                                  /*basePointerOrdinal=*/1, /*fieldOffsetBytes=*/0);
  ZC_EXPECT(LirStructuralVerifier::verify(module) == zc::none);
}

ZC_TEST("LIR structural verifier rejects a non-offset-zero load field") {
  Module module = fieldReadModule(ValueType::pointer(0), carrier(IntegerBitWidth::Bit32), 1, 4);
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects a load field whose destination is a pointer") {
  Module module = fieldReadModule(ValueType::pointer(0), ValueType::pointer(0), 1, 0);
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects a load field through a non-pointer base") {
  Module module =
      fieldReadModule(carrier(IntegerBitWidth::Bit32), carrier(IntegerBitWidth::Bit32), 1, 0);
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects a load field through an undeclared base") {
  Module module = fieldReadModule(ValueType::pointer(0), carrier(IntegerBitWidth::Bit32),
                                  /*basePointerOrdinal=*/3, /*fieldOffsetBytes=*/0);
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::UndeclaredLocalSlot);
}

ZC_TEST("LIR structural verifier rejects take-address into a non-pointer slot") {
  zc::Vector<Function> functions;
  {
    zc::Vector<Statement> statements;
    statements.add(
        Statement::assign(1, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 0))));
    statements.add(Statement::takeAddress(2, 1));
    zc::Vector<Operand> arguments;
    arguments.add(Operand::localUse(2));
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(
        blockId(1), zc::mv(statements),
        ZC_ASSERT_NONNULL(Terminator::callFunction(1, 3, zc::mv(arguments), blockId(2)))));
    blocks.add(BasicBlock(blockId(2), zc::Vector<Statement>(), Terminator::returnLocal(3)));
    zc::Vector<Local> parameters;
    zc::Vector<Local> locals;
    locals.add(Local(1, carrier(IntegerBitWidth::Bit32)));
    // Slot 2 must be a pointer for TakeAddress; i32 is the mismatch under test.
    locals.add(Local(2, carrier(IntegerBitWidth::Bit32)));
    locals.add(Local(3, carrier(IntegerBitWidth::Bit32)));
    functions.add(Function(tests::testDefinition(0), zc::heapString("zom.module_init"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  {
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(blockId(1),
                          Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 7))));
    zc::Vector<Local> parameters;
    parameters.add(Local(1, ValueType::pointer(0)));
    zc::Vector<Local> noLocals;
    functions.add(Function(tests::testDefinition(1), zc::heapString("zom.callee"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(noLocals),
                           zc::mv(blocks)));
  }
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects an empty function symbol") {
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1),
                        Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 0))));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString(""),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::EmptyOrDuplicateSymbol);
}

ZC_TEST("LIR structural verifier rejects duplicate function symbols") {
  zc::Vector<Function> functions;
  {
    zc::Vector<BasicBlock> blocks;
    {
      zc::Vector<Statement> statements;
      zc::Vector<Operand> noArguments;
      blocks.add(BasicBlock(
          blockId(1), zc::mv(statements),
          ZC_ASSERT_NONNULL(Terminator::callFunction(1, 1, zc::mv(noArguments), blockId(2)))));
    }
    {
      zc::Vector<Statement> statements;
      blocks.add(BasicBlock(blockId(2), zc::mv(statements), Terminator::returnLocal(1)));
    }
    zc::Vector<Local> parameters;
    zc::Vector<Local> locals;
    locals.add(Local(1, carrier(IntegerBitWidth::Bit32)));
    functions.add(Function(tests::testDefinition(0), zc::heapString("shared.symbol"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  {
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(blockId(1),
                          Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 7))));
    functions.add(Function(tests::testDefinition(0), zc::heapString("shared.symbol"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  }
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::EmptyOrDuplicateSymbol);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).functionIndex == 1);
}

ZC_TEST("LIR structural verifier rejects a function without blocks") {
  zc::Vector<BasicBlock> blocks;
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("empty.body"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::MissingEntryBlock);
}

ZC_TEST("LIR structural verifier rejects duplicate block ordinals") {
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1),
                        Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 0))));
  blocks.add(BasicBlock(blockId(1), Terminator::gotoBlock(blockId(1))));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("dup.block"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::DuplicateBlockOrdinal);
}

ZC_TEST("LIR structural verifier rejects non-dense block ordinals") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::gotoBlock(blockId(3))));
  }
  blocks.add(BasicBlock(blockId(3),
                        Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 0))));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("sparse.block"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::NonDenseBlockOrdinals);
}

ZC_TEST("LIR structural verifier rejects an unreachable block") {
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1),
                        Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 0))));
  blocks.add(BasicBlock(blockId(2),
                        Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 1))));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("unreachable.block"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::UnreachableBlock);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).blockOrdinal == 2);
}

ZC_TEST("LIR structural verifier rejects non-dense parameter and local slots") {
  // The same diamond as validConditionalModule but declaring its result local
  // at ordinal 4 instead of the dense 2, leaving a gap after the parameter.
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(1), zc::mv(statements),
                          Terminator::condBranch(1, blockId(2), blockId(3))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(2), zc::mv(statements), Terminator::gotoBlock(blockId(4))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(3), zc::mv(statements), Terminator::gotoBlock(blockId(4))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(4), zc::mv(statements), Terminator::returnLocal(4)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, carrier(IntegerBitWidth::Bit1)));
  zc::Vector<Local> locals;
  locals.add(Local(4, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("sparse.local"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  Module sparse(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(sparse);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::NonDenseLocalSlots);
}

ZC_TEST("LIR structural verifier rejects a dangling block target") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::gotoBlock(blockId(5))));
  }
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("dangling.target"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::DanglingBlockTarget);
}

ZC_TEST("LIR structural verifier rejects a use of an undeclared slot") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(
        Statement::assign(9, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 1))));
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(9)));
  }
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("undeclared.slot"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::UndeclaredLocalSlot);
}

ZC_TEST("LIR structural verifier rejects an assignment with a mismatched carrier") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(
        Statement::assign(1, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 1))));
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(1)));
  }
  zc::Vector<Local> parameters;
  zc::Vector<Local> locals;
  locals.add(Local(1, carrier(IntegerBitWidth::Bit8)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("carrier.mismatch"),
                         carrier(IntegerBitWidth::Bit8), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects a branch condition that is not one bit") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(1), zc::mv(statements),
                          Terminator::condBranch(1, blockId(2), blockId(3))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(2), zc::mv(statements), Terminator::gotoBlock(blockId(4))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(3), zc::mv(statements), Terminator::gotoBlock(blockId(4))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(4), zc::mv(statements), Terminator::returnLocal(2)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Local> locals;
  locals.add(Local(2, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("wide.condition"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::ConditionNotBit1);
}

ZC_TEST("LIR structural verifier rejects a call index outside the module range") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    zc::Vector<Operand> noArguments;
    blocks.add(BasicBlock(blockId(1), zc::mv(statements),
                          ZC_ASSERT_NONNULL(Terminator::callFunction(
                              /*calleeIndex=*/9, 1, zc::mv(noArguments), blockId(2)))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(2), zc::mv(statements), Terminator::returnLocal(1)));
  }
  zc::Vector<Local> parameters;
  zc::Vector<Local> locals;
  locals.add(Local(1, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("bad.callee.index"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CalleeIndexOutOfRange);
}

ZC_TEST("LIR structural verifier rejects call argument count arity mismatch") {
  zc::Vector<Function> functions;
  {
    zc::Vector<BasicBlock> blocks;
    {
      zc::Vector<Statement> statements;
      zc::Vector<Operand> noArguments;
      blocks.add(BasicBlock(blockId(1), zc::mv(statements),
                            ZC_ASSERT_NONNULL(Terminator::callFunction(
                                /*calleeIndex=*/1, 1, zc::mv(noArguments), blockId(2)))));
    }
    {
      zc::Vector<Statement> statements;
      blocks.add(BasicBlock(blockId(2), zc::mv(statements), Terminator::returnLocal(1)));
    }
    zc::Vector<Local> parameters;
    zc::Vector<Local> locals;
    locals.add(Local(1, carrier(IntegerBitWidth::Bit32)));
    functions.add(Function(tests::testDefinition(0), zc::heapString("arity.caller"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  {
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(blockId(1),
                          Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 0))));
    zc::Vector<Local> parameters;
    parameters.add(Local(1, carrier(IntegerBitWidth::Bit32)));
    zc::Vector<Local> locals;
    functions.add(Function(tests::testDefinition(0), zc::heapString("arity.callee"),
                           carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                           zc::mv(blocks)));
  }
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::TerminatorArity);
}

ZC_TEST("LIR structural verifier rejects a return constant with the wrong carrier") {
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(blockId(1),
                        Terminator::returnInteger(constant(carrier(IntegerBitWidth::Bit32), 0))));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("return.carrier"),
                         carrier(IntegerBitWidth::Bit8), zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::ReturnCarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects a compare whose operands disagree on carrier") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::compare(
        3, ComparisonOp::Eq, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 1)),
        Operand::constant(constant(carrier(IntegerBitWidth::Bit8), 2))));
    blocks.add(BasicBlock(blockId(1), zc::mv(statements),
                          Terminator::condBranch(3, blockId(2), blockId(2))));
  }
  {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(blockId(2), zc::mv(statements), Terminator::returnLocal(2)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, carrier(IntegerBitWidth::Bit1)));
  zc::Vector<Local> locals;
  locals.add(Local(2, carrier(IntegerBitWidth::Bit32)));
  locals.add(Local(3, carrier(IntegerBitWidth::Bit1)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("compare.carrier"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects an arithmetic result whose operands disagree on carrier") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::arithmetic(
        2, ArithmeticOp::Add, Operand::constant(constant(carrier(IntegerBitWidth::Bit32), 1)),
        Operand::constant(constant(carrier(IntegerBitWidth::Bit8), 2))));
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Local> locals;
  locals.add(Local(2, carrier(IntegerBitWidth::Bit32)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("arithmetic.carrier"),
                         carrier(IntegerBitWidth::Bit32), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

ZC_TEST("LIR structural verifier rejects arithmetic into a one-bit destination") {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::arithmetic(
        2, ArithmeticOp::Add, Operand::constant(constant(carrier(IntegerBitWidth::Bit1), 1)),
        Operand::constant(constant(carrier(IntegerBitWidth::Bit1), 0))));
    blocks.add(BasicBlock(blockId(1), zc::mv(statements), Terminator::returnLocal(2)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, carrier(IntegerBitWidth::Bit1)));
  zc::Vector<Local> locals;
  locals.add(Local(2, carrier(IntegerBitWidth::Bit1)));
  zc::Vector<Function> functions;
  functions.add(Function(tests::testDefinition(0), zc::heapString("arithmetic.bit1"),
                         carrier(IntegerBitWidth::Bit1), zc::mv(parameters), zc::mv(locals),
                         zc::mv(blocks)));
  Module module(zc::mv(functions));
  auto finding = LirStructuralVerifier::verify(module);
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == LirVerificationFaultKind::CarrierMismatch);
}

}  // namespace
}  // namespace zomlang::compiler::lir
