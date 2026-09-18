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

// RFC 0053 slice 2: the translation validator accepts the LIR the producer
// emits for a scalar constant return and a comparison diamond, and rejects one
// minimal semantic mutation per translation fault tag. The MIR functions are
// constructed directly through the Built MIR algebra; the LIR modules are
// constructed directly so each mutation changes exactly one correspondence.

#include "compiler/checker/facts/signature-facts.h"
#include "compiler/ir/ir-identity.h"
#include "compiler/lir/lir-module.h"
#include "compiler/lir/lir-store.h"
#include "compiler/lir/verify/translation-validator.h"
#include "compiler/mir/built-mir.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::lir {
namespace {

using tests::testDefinition;
using tests::TestSemanticTypeContext;

identity::SourceSpan span() {
  auto snapshot = identity::ImmutableSourceSnapshot::from(tests::test_identity_detail::source(),
                                                          zc::heapArray<uint8_t>(8, uint8_t{0}));
  ZC_REQUIRE(snapshot != zc::none);
  ZC_IF_SOME(value, snapshot) {
    auto admitted = value.span(1, 7);
    ZC_IF_SOME(result, admitted) { return zc::mv(result); }
  }
  ZC_FAIL_REQUIRE("invalid source span fixture");
}

checker::checked::CanonicalConstValue integerConstant(uint8_t value) {
  auto magnitude = zc::heapArray<uint8_t>(1);
  magnitude[0] = value;
  return checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
      checker::signature::IntegerSign::NonNegative, zc::mv(magnitude)});
}

mir::MirLocalId mirLocal(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(mir::MirLocalId::fromOrdinal(ordinal));
}
mir::MirBlockId mirBlock(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(mir::MirBlockId::fromOrdinal(ordinal));
}
mir::MirSourceScopeId mirScope(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(mir::MirSourceScopeId::fromOrdinal(ordinal));
}
LirBlockId lirBlock(uint32_t ordinal) {
  return ZC_REQUIRE_NONNULL(LirBlockId::fromOrdinal(ordinal));
}

mir::MirPlace place(mir::MirLocalId local, identity::SemanticTypeId type) {
  zc::Vector<mir::MirProjection> projections;
  return mir::MirPlace(local, type, zc::mv(projections), type);
}

ValueType i32Carrier() { return ZC_REQUIRE_NONNULL(ValueType::integer(IntegerBitWidth::Bit32)); }
ValueType bit1Carrier() { return ZC_REQUIRE_NONNULL(ValueType::integer(IntegerBitWidth::Bit1)); }
IntegerConstant i32Const(uint64_t bits) {
  return ZC_REQUIRE_NONNULL(IntegerConstant::from(i32Carrier(), bits));
}

// MIR: scope#1, local#1 module-initializer result i32, block#1:
// StorageLive(1); 1 = const 42; return move 1.
mir::MirFunction scalarMir(identity::DefId owner, identity::SemanticTypeId i32) {
  zc::Vector<mir::MirSourceScope> scopes;
  zc::Maybe<mir::MirSourceScopeId> noParent;
  scopes.add(mir::MirSourceScope{mirScope(1), zc::mv(noParent), span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{mirLocal(1), mir::MirLocalKind::ModuleInitializerResult, i32,
                                      mirScope(1), span()});
  zc::Vector<mir::MirStatement> statements;
  statements.add(mir::MirStatement::storageLive(mirLocal(1), span()));
  statements.add(mir::MirStatement::assign(
      place(mirLocal(1), i32),
      mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(42))),
      mir::MirInitializationKind::Initialize, span()));
  auto terminator =
      mir::MirTerminator::returnValue(mir::MirOperand::move(place(mirLocal(1), i32)), span());
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{mirBlock(1), mirScope(1), zc::mv(statements), zc::mv(terminator)});
  return mir::MirFunction{owner,
                          mir::MirFunctionKind::ModuleInitializer,
                          identity::DefinitionKind::Static,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

// LIR corresponding to scalarMir folded: one block returning an i32 constant.
Module scalarLir(uint64_t bits) {
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(lirBlock(1), Terminator::returnInteger(i32Const(bits))));
  zc::Vector<Function> functions;
  functions.add(Function(zc::heapString("zom.module_init"), i32Carrier(), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

// MIR comparison diamond: param#1 bool, result#2 i32, temp#3 bool; entry
// computes temp = (const 1 Eq const 2) and switches on it, arms assign i32
// constants 5 and 7, join returns result.
mir::MirFunction diamondMir(identity::DefId owner, identity::SemanticTypeId i32,
                            identity::SemanticTypeId boolType, mir::MirComparisonOperator op) {
  zc::Vector<mir::MirSourceScope> scopes;
  zc::Maybe<mir::MirSourceScopeId> noParent;
  scopes.add(mir::MirSourceScope{mirScope(1), zc::mv(noParent), span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{mirLocal(1), mir::MirLocalKind::Parameter, boolType,
                                      mirScope(1), span()});
  locals.add(mir::MirLocalDeclaration{mirLocal(2), mir::MirLocalKind::FunctionResult, i32,
                                      mirScope(1), span()});
  locals.add(mir::MirLocalDeclaration{mirLocal(3), mir::MirLocalKind::Temporary, boolType,
                                      mirScope(1), span()});
  zc::Vector<mir::MirBasicBlock> blocks;
  {
    zc::Vector<mir::MirStatement> statements;
    statements.add(mir::MirStatement::storageLive(mirLocal(2), span()));
    statements.add(mir::MirStatement::storageLive(mirLocal(3), span()));
    statements.add(mir::MirStatement::assign(
        place(mirLocal(3), boolType),
        mir::MirRvalue::comparison(op, mir::MirOperand::constant(i32, integerConstant(1)),
                                   mir::MirOperand::constant(i32, integerConstant(2)), boolType),
        mir::MirInitializationKind::Initialize, span()));
    zc::Vector<mir::MirSwitchIntArm> arms;
    arms.add(
        mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), mirBlock(2)});
    auto terminator = mir::MirTerminator::switchInt(
        mir::MirOperand::copy(place(mirLocal(3), boolType)), zc::mv(arms), mirBlock(3), span());
    blocks.add(
        mir::MirBasicBlock{mirBlock(1), mirScope(1), zc::mv(statements), zc::mv(terminator)});
  }
  for (uint32_t armBlock = 2; armBlock <= 3; ++armBlock) {
    const uint8_t value = armBlock == 2 ? 5 : 7;
    zc::Vector<mir::MirStatement> statements;
    statements.add(mir::MirStatement::assign(
        place(mirLocal(2), i32),
        mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(value))),
        mir::MirInitializationKind::Initialize, span()));
    auto terminator = mir::MirTerminator::gotoTarget(mirBlock(4), span());
    blocks.add(mir::MirBasicBlock{mirBlock(armBlock), mirScope(1), zc::mv(statements),
                                  zc::mv(terminator)});
  }
  {
    zc::Vector<mir::MirStatement> statements;
    auto terminator =
        mir::MirTerminator::returnValue(mir::MirOperand::copy(place(mirLocal(2), i32)), span());
    blocks.add(
        mir::MirBasicBlock{mirBlock(4), mirScope(1), zc::mv(statements), zc::mv(terminator)});
  }
  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

struct DiamondMutation final {
  ComparisonOp op = ComparisonOp::Eq;
  bool polaritySwap = false;
  uint64_t thenBits = 5;
  uint64_t elseBits = 7;
  bool extraLocal = false;
  bool dropThenAssign = false;
  uint32_t blockCount = 4;
  uint32_t conditionOrdinal = 3;
  uint32_t returnOrdinal = 2;
};

// LIR for diamondMir with each correspondence independently mutable.
Module diamondLir(const DiamondMutation& mutation) {
  zc::Vector<BasicBlock> blocks;
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::compare(mutation.conditionOrdinal, mutation.op,
                                      Operand::constant(i32Const(1)),
                                      Operand::constant(i32Const(2))));
    const LirBlockId trueTarget = lirBlock(mutation.polaritySwap ? 3 : 2);
    const LirBlockId falseTarget = lirBlock(mutation.polaritySwap ? 2 : 3);
    blocks.add(
        BasicBlock(lirBlock(1), zc::mv(statements),
                   Terminator::condBranch(mutation.conditionOrdinal, trueTarget, falseTarget)));
  }
  {
    zc::Vector<Statement> statements;
    if (!mutation.dropThenAssign) {
      statements.add(Statement::assign(2, Operand::constant(i32Const(mutation.thenBits))));
    }
    blocks.add(BasicBlock(lirBlock(2), zc::mv(statements), Terminator::gotoBlock(lirBlock(4))));
  }
  {
    zc::Vector<Statement> statements;
    statements.add(Statement::assign(2, Operand::constant(i32Const(mutation.elseBits))));
    blocks.add(BasicBlock(lirBlock(3), zc::mv(statements), Terminator::gotoBlock(lirBlock(4))));
  }
  if (mutation.blockCount == 4) {
    zc::Vector<Statement> statements;
    blocks.add(BasicBlock(lirBlock(4), zc::mv(statements),
                          Terminator::returnLocal(mutation.returnOrdinal)));
  }
  zc::Vector<Local> parameters;
  parameters.add(Local(1, bit1Carrier()));
  zc::Vector<Local> locals;
  locals.add(Local(2, i32Carrier()));
  locals.add(Local(3, bit1Carrier()));
  if (mutation.extraLocal) locals.add(Local(4, i32Carrier()));
  zc::Vector<Function> functions;
  functions.add(Function(zc::heapString("zom.conditional_cmp"), i32Carrier(), zc::mv(parameters),
                         zc::mv(locals), zc::mv(blocks)));
  return Module(zc::mv(functions));
}

TranslationFaultKind validateDiamond(const Module& module, TestSemanticTypeContext& types,
                                     identity::SemanticTypeId i32,
                                     identity::SemanticTypeId boolType) {
  auto finding = TranslationValidator::validate(
      diamondMir(testDefinition(0), i32, boolType, mir::MirComparisonOperator::Eq), module,
      types.semanticTypes());
  ZC_REQUIRE(finding != zc::none);
  return ZC_REQUIRE_NONNULL(finding).fault;
}

ZC_TEST("Translation validator accepts the folded scalar and comparison diamond") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  ZC_EXPECT(TranslationValidator::validate(scalarMir(testDefinition(0), i32), scalarLir(42),
                                           types.semanticTypes()) == zc::none);
  ZC_EXPECT(TranslationValidator::validate(
                diamondMir(testDefinition(0), i32, boolType, mir::MirComparisonOperator::Eq),
                diamondLir(DiamondMutation{}), types.semanticTypes()) == zc::none);
}

ZC_TEST("Translation validator rejects a folded scalar with the wrong constant") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  auto finding = TranslationValidator::validate(scalarMir(testDefinition(0), i32), scalarLir(43),
                                                types.semanticTypes());
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == TranslationFaultKind::ConstantMismatch);
}

ZC_TEST("Translation validator rejects an extra LIR function") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  zc::Vector<Function> functions;
  {
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(lirBlock(1), Terminator::returnInteger(i32Const(42))));
    functions.add(Function(zc::heapString("zom.module_init"), i32Carrier(), zc::mv(blocks)));
  }
  {
    zc::Vector<BasicBlock> blocks;
    blocks.add(BasicBlock(lirBlock(1), Terminator::returnInteger(i32Const(1))));
    functions.add(Function(zc::heapString("zom.extra"), i32Carrier(), zc::mv(blocks)));
  }
  Module module(zc::mv(functions));
  auto finding = TranslationValidator::validate(scalarMir(testDefinition(0), i32), module,
                                                types.semanticTypes());
  ZC_REQUIRE(finding != zc::none);
  ZC_EXPECT(ZC_ASSERT_NONNULL(finding).fault == TranslationFaultKind::FunctionSetMismatch);
}

ZC_TEST("Translation validator rejects a missing join block") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.blockCount = 3;
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::BlockBijectionMismatch);
}

ZC_TEST("Translation validator rejects a dropped arm assignment") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.dropThenAssign = true;
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::EffectMismatch);
}

ZC_TEST("Translation validator rejects an arm with the wrong constant") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.thenBits = 99;
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::ConstantMismatch);
}

ZC_TEST("Translation validator rejects a wrong comparison operator") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.op = ComparisonOp::Ne;
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::OperatorMismatch);
}

ZC_TEST("Translation validator rejects swapped branch polarity") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.polaritySwap = true;
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::EdgeTargetMismatch);
}

ZC_TEST("Translation validator rejects an undeclared-condition branch") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.conditionOrdinal = 9;
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::PlaceMappingMismatch);
}

ZC_TEST("Translation validator rejects an extra declared local slot") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.extraLocal = true;
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::SlotSetMismatch);
}

ZC_TEST("Translation validator rejects a return of the wrong local") {
  TestSemanticTypeContext types;
  const auto i32 = types.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto boolType = types.internPrimitive(type::semantic::PrimitiveKind::Bool);
  DiamondMutation mutation;
  mutation.returnOrdinal = 1;
  // Returning the boolean parameter slot also changes the return carrier to
  // i1, which the structural verifier catches first; build the LIR and expect
  // the translation correspondence to reject the place mapping itself.
  ZC_EXPECT(validateDiamond(diamondLir(mutation), types, i32, boolType) ==
            TranslationFaultKind::PlaceMappingMismatch);
}

}  // namespace
}  // namespace zomlang::compiler::lir
