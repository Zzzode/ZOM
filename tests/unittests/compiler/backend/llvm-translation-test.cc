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
// See the License for the specific language governing permissions and limitations under
// the License.

// RFC 0021 O2/KR2.4 first vertical slice: build one verified scalar module
// initializer Built MIR shape, lower it to LIR, translate it to an LLVM module,
// and assert llvm::verifyModule reports no broken module (success) and the IR
// carries the expected `ret i32 42`. This test links LLVM and is built ONLY when
// ZOM_ENABLE_LLVM_BACKEND is ON.

#include "compiler/backend/llvm/llvm-translator.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/identity/source-snapshot.h"
#include "compiler/lir/lir-module.h"
#include "compiler/lir/lir-store.h"
#include "compiler/lir/mir-to-lir.h"
#include "compiler/mir/built-mir.h"
#include "compiler/type/semantic-type-data.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"
#include "zc/ztest/test.h"

namespace zomlang::compiler::backend::llvm {
namespace {

mir::MirLocalId localId(uint32_t ordinal) {
  auto value = mir::MirLocalId::fromOrdinal(ordinal);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

mir::MirSourceScopeId scopeId(uint32_t ordinal) {
  auto value = mir::MirSourceScopeId::fromOrdinal(ordinal);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

mir::MirBlockId blockId(uint32_t ordinal) {
  auto value = mir::MirBlockId::fromOrdinal(ordinal);
  ZC_REQUIRE(value != zc::none);
  return ZC_REQUIRE_NONNULL(value);
}

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

// A big-endian canonical magnitude for a small non-negative integer literal.
checker::signature::CanonicalConstValue integerConstant(uint8_t value) {
  auto magnitude = zc::heapArray<uint8_t>(1);
  magnitude[0] = value;
  return checker::checked::CanonicalConstValue::integer(checker::signature::CanonicalInteger{
      checker::signature::IntegerSign::NonNegative, zc::mv(magnitude)});
}

mir::MirPlace resultPlace(mir::MirLocalId local, identity::SemanticTypeId type) {
  zc::Vector<mir::MirProjection> projections;
  return mir::MirPlace(local, type, zc::mv(projections), type);
}

// Build the exact verified scalar module-initializer MIR shape:
//   scope#1; local#1 = ModuleInitializerResult : i32;
//   block#1: StorageLive(local#1); local#1 = const 42; return move local#1;
mir::MirFunction buildScalarInitializer(identity::DefId owner, identity::SemanticTypeId i32,
                                        uint8_t constantValue) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::ModuleInitializerResult, i32,
                                      scopeId(1), span()});

  zc::Vector<mir::MirStatement> statements;
  statements.add(mir::MirStatement::storageLive(localId(1), span()));
  statements.add(mir::MirStatement::assign(
      resultPlace(localId(1), i32),
      mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(constantValue))),
      mir::MirInitializationKind::Initialize, span()));

  auto terminator =
      mir::MirTerminator::returnValue(mir::MirOperand::move(resultPlace(localId(1), i32)), span());

  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements), zc::mv(terminator)});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::ModuleInitializer,
                          identity::DefinitionKind::Static,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

ZC_TEST("Scalar module initializer lowers MIR -> LIR -> verified LLVM ret i32 42") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  auto function = buildScalarInitializer(owner, i32, 42);

  // MIR -> LIR.
  auto lir = lir::MirToLirLowering::lowerScalarInitializer(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 1);
  ZC_EXPECT(lirModule.functions()[0].returnCarrier().integerWidth() == lir::IntegerBitWidth::Bit32);

  // LIR -> LLVM, with mandatory verifyModule.
  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("ret i32 42"_zc));
  ZC_EXPECT(ir.contains("zom.module_init"_zc));

  // RFC 0021 ObjectEmission: the verified module lowers to a non-empty native
  // object file. On this Linux host the bytes begin with the ELF magic
  // (0x7f 'E' 'L' 'F'); the emission path is the same host TargetMachine that
  // produced the data layout, so the object matches the verified IR.
  const auto object = result.objectCode();
  ZC_EXPECT(object.size() > 0);
  ZC_REQUIRE(object.size() >= 4);
  ZC_EXPECT(object[0] == 0x7f);
  ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
  ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
  ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
}

// RFC 0021 aggregate first slice: a verified struct-local field-return function
// (`fn f() -> i32 { let p = P{42, 7}; return p.x; }`) lowers MIR -> LIR by
// folding the selected constant field to the existing single-block integer
// return, then translates to verified LLVM `ret i32 42`. No struct is
// materialized in LIR or LLVM. `structType` stands in for the struct's own
// semantic type: the field read is folded, so its layout is never inspected.
mir::MirFunction buildAggregateFieldFunction(identity::DefId owner,
                                             identity::SemanticTypeId structType,
                                             identity::SemanticTypeId i32,
                                             identity::DefId aggregate, identity::DefId fieldX,
                                             identity::DefId fieldY, uint8_t valueX,
                                             uint8_t valueY) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::UserLocal, structType,
                                      scopeId(1), span()});

  zc::Vector<mir::MirNominalAggregateElement> elements;
  elements.add(mir::MirNominalAggregateElement{
      fieldX, mir::MirOperand::constant(i32, integerConstant(valueX))});
  elements.add(mir::MirNominalAggregateElement{
      fieldY, mir::MirOperand::constant(i32, integerConstant(valueY))});

  zc::Vector<mir::MirStatement> statements;
  statements.add(mir::MirStatement::storageLive(localId(1), span()));
  statements.add(mir::MirStatement::assign(
      resultPlace(localId(1), structType),
      mir::MirRvalue::nominalAggregate(aggregate, structType, zc::mv(elements)),
      mir::MirInitializationKind::Initialize, span()));

  // return copy local#1.x : the field projection selects the constant element.
  zc::Vector<mir::MirProjection> projections;
  projections.add(mir::MirProjection::field(fieldX, structType, i32));
  auto fieldPlace = mir::MirPlace(localId(1), structType, zc::mv(projections), i32);
  auto terminator =
      mir::MirTerminator::returnValue(mir::MirOperand::copy(zc::mv(fieldPlace)), span());

  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements), zc::mv(terminator)});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

// Builds the whole-struct constant-return shape:
//   fn f() -> P { let p = P{valueX, valueY}; return p; }
// identical to buildAggregateFieldFunction except the return copies the whole
// struct local (zero projections) and the function result type is the struct.
mir::MirFunction buildAggregateReturnFunction(identity::DefId owner,
                                              identity::SemanticTypeId structType,
                                              identity::SemanticTypeId i32,
                                              identity::DefId aggregate, identity::DefId fieldX,
                                              identity::DefId fieldY, uint8_t valueX,
                                              uint8_t valueY) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::UserLocal, structType,
                                      scopeId(1), span()});

  zc::Vector<mir::MirNominalAggregateElement> elements;
  elements.add(mir::MirNominalAggregateElement{
      fieldX, mir::MirOperand::constant(i32, integerConstant(valueX))});
  elements.add(mir::MirNominalAggregateElement{
      fieldY, mir::MirOperand::constant(i32, integerConstant(valueY))});

  zc::Vector<mir::MirStatement> statements;
  statements.add(mir::MirStatement::storageLive(localId(1), span()));
  statements.add(mir::MirStatement::assign(
      resultPlace(localId(1), structType),
      mir::MirRvalue::nominalAggregate(aggregate, structType, zc::mv(elements)),
      mir::MirInitializationKind::Initialize, span()));

  // return copy local#1 : the whole struct, no projections.
  auto terminator = mir::MirTerminator::returnValue(
      mir::MirOperand::copy(resultPlace(localId(1), structType)), span());

  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements), zc::mv(terminator)});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          structType,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

ZC_TEST("Whole-struct return lowers MIR -> LIR -> verified LLVM literal struct") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto structType = typeContext.internPrimitive(type::semantic::PrimitiveKind::I64);
  const auto owner = tests::testDefinition(0);
  const auto aggregate = tests::testDefinition(1);
  const auto fieldX = tests::testDefinition(2);
  const auto fieldY = tests::testDefinition(3);

  auto function =
      buildAggregateReturnFunction(owner, structType, i32, aggregate, fieldX, fieldY, 42, 7);

  // MIR -> LIR: the whole struct lowers to a two-slot aggregate return.
  auto lir = lir::MirToLirLowering::lowerAggregateReturn(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_REQUIRE(lirModule.functions().size() == 1);
  ZC_REQUIRE(lirModule.functions()[0].blocks().size() == 1);
  const auto& terminator = lirModule.functions()[0].blocks()[0].terminator();
  ZC_EXPECT(terminator.kind() == lir::LirTerminatorKind::ReturnAggregate);
  ZC_REQUIRE(terminator.returnAggregateSlots().size() == 2);
  ZC_EXPECT(terminator.returnAggregateSlots()[0].bits() == 42);
  ZC_EXPECT(terminator.returnAggregateSlots()[1].bits() == 7);

  // LIR -> LLVM: end-to-end verified literal struct.
  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }
  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("{ i32, i32 }"_zc));
  ZC_EXPECT(ir.contains("insertvalue"_zc));
  ZC_EXPECT(ir.contains("ret { i32, i32 }"_zc));
}

ZC_TEST("Whole-struct return slots follow source-literal element order, not a sort") {
  // The emitted slot order is the source struct-literal property order preserved
  // through HIR and MIR, NOT the nominal declared field order (which the
  // signature facts discard by a digest sort) and NOT any value/DefId sort. Build
  // a literal whose first element's value is greater than the second's; the slots
  // must appear in that literal order, so slot[0] is the first element (100), not
  // the numerically smaller one.
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto structType = typeContext.internPrimitive(type::semantic::PrimitiveKind::I64);
  const auto owner = tests::testDefinition(0);
  const auto aggregate = tests::testDefinition(1);
  const auto fieldX = tests::testDefinition(2);
  const auto fieldY = tests::testDefinition(3);

  auto function =
      buildAggregateReturnFunction(owner, structType, i32, aggregate, fieldX, fieldY, 100, 3);

  auto lir = lir::MirToLirLowering::lowerAggregateReturn(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& terminator = ZC_REQUIRE_NONNULL(lir).functions()[0].blocks()[0].terminator();
  ZC_REQUIRE(terminator.returnAggregateSlots().size() == 2);
  // Slot order is the literal element order: the first element (100) stays first.
  ZC_EXPECT(terminator.returnAggregateSlots()[0].bits() == 100);
  ZC_EXPECT(terminator.returnAggregateSlots()[1].bits() == 3);
}

ZC_TEST("Whole-struct return lowering rejects the field-return shape") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto structType = typeContext.internPrimitive(type::semantic::PrimitiveKind::I64);
  const auto owner = tests::testDefinition(0);
  const auto aggregate = tests::testDefinition(1);
  const auto fieldX = tests::testDefinition(2);
  const auto fieldY = tests::testDefinition(3);

  // A field-return function (return place has one Field projection) is not the
  // whole-struct shape.
  auto field =
      buildAggregateFieldFunction(owner, structType, i32, aggregate, fieldX, fieldY, 42, 7);
  ZC_EXPECT(lir::MirToLirLowering::lowerAggregateReturn(field, typeContext.semanticTypes()) ==
            zc::none);
}

ZC_TEST("Aggregate field lowering rejects the whole-struct return shape") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto structType = typeContext.internPrimitive(type::semantic::PrimitiveKind::I64);
  const auto owner = tests::testDefinition(0);
  const auto aggregate = tests::testDefinition(1);
  const auto fieldX = tests::testDefinition(2);
  const auto fieldY = tests::testDefinition(3);

  // The whole-struct return (return place has zero projections) is not the
  // field-return shape.
  auto whole =
      buildAggregateReturnFunction(owner, structType, i32, aggregate, fieldX, fieldY, 42, 7);
  ZC_EXPECT(lir::MirToLirLowering::lowerAggregateFieldInitializer(
                whole, typeContext.semanticTypes()) == zc::none);
}

ZC_TEST("Whole-struct return lowering rejects a scalar initializer") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  auto scalar = buildScalarInitializer(owner, i32, 42);
  ZC_EXPECT(lir::MirToLirLowering::lowerAggregateReturn(scalar, typeContext.semanticTypes()) ==
            zc::none);
}

ZC_TEST("Struct-local field return lowers MIR -> LIR -> verified LLVM ret i32 42") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto structType = typeContext.internPrimitive(type::semantic::PrimitiveKind::I64);
  const auto owner = tests::testDefinition(0);
  const auto aggregate = tests::testDefinition(1);
  const auto fieldX = tests::testDefinition(2);
  const auto fieldY = tests::testDefinition(3);

  auto function =
      buildAggregateFieldFunction(owner, structType, i32, aggregate, fieldX, fieldY, 42, 7);

  // MIR -> LIR: the field read is folded to the scalar constant 42.
  auto lir =
      lir::MirToLirLowering::lowerAggregateFieldInitializer(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 1);
  ZC_EXPECT(lirModule.functions()[0].returnCarrier().integerWidth() == lir::IntegerBitWidth::Bit32);

  // LIR -> LLVM, with mandatory verifyModule.
  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }
  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("ret i32 42"_zc));
}

ZC_TEST("Aggregate field lowering fails closed outside the verified shape") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  // A scalar module initializer is not the aggregate-field shape.
  auto scalar = buildScalarInitializer(owner, i32, 42);
  ZC_EXPECT(lir::MirToLirLowering::lowerAggregateFieldInitializer(
                scalar, typeContext.semanticTypes()) == zc::none);
}

// the LIR integer carrier width (I8/I16/I32/I64 and their unsigned peers), but
// only i32 was covered. Prove a narrow (i16) and a wide (i64) scalar module
// initializer lower MIR -> LIR -> verified LLVM IR -> a native ELF object, so
// the proven shape is no longer i32-only.
ZC_TEST("Scalar module initializers of non-i32 integer widths lower to a verified object") {
  struct WidthCase {
    type::semantic::PrimitiveKind kind;
    uint8_t value;
    zc::StringPtr expectedReturn;
  };
  const WidthCase cases[] = {
      {type::semantic::PrimitiveKind::I16, 7, "ret i16 7"_zc},
      {type::semantic::PrimitiveKind::I64, 9, "ret i64 9"_zc},
  };

  for (const auto& widthCase : cases) {
    tests::TestSemanticTypeContext typeContext;
    const auto carrier = typeContext.internPrimitive(widthCase.kind);
    const auto owner = tests::testDefinition(0);

    auto function = buildScalarInitializer(owner, carrier, widthCase.value);

    auto lir = lir::MirToLirLowering::lowerScalarInitializer(function, typeContext.semanticTypes());
    ZC_REQUIRE(lir != zc::none);
    const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
    ZC_EXPECT(lirModule.functions().size() == 1);

    LlvmTranslator translator;
    auto result = translator.translate(lirModule);
    ZC_EXPECT(result.verified());
    if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

    ZC_EXPECT(result.textualIr().contains(widthCase.expectedReturn));

    const auto object = result.objectCode();
    ZC_EXPECT(object.size() > 0);
    ZC_REQUIRE(object.size() >= 4);
    ZC_EXPECT(object[0] == 0x7f);
    ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
    ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
    ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
  }
}

// Build the verified four-block boolean-conditional return MIR shape:
//   fun f(cond: bool) -> i32 { if cond { return thenValue } else { return elseValue } }
//   local#1 = cond : bool (Parameter); local#2 = result : i32 (FunctionResult)
//   bb1: StorageLive(local#2); SwitchInt(copy local#1) [true -> bb2, false -> bb3], default bb3
//   bb2: local#2 = const thenValue; Goto(bb4)
//   bb3: local#2 = const elseValue; Goto(bb4)
//   bb4: return move local#2
mir::MirFunction buildConditionalReturn(identity::DefId owner, identity::SemanticTypeId boolType,
                                        identity::SemanticTypeId i32, uint8_t thenValue,
                                        uint8_t elseValue) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, boolType,
                                      scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(2), mir::MirLocalKind::FunctionResult, i32,
                                      scopeId(1), span()});

  // Entry block: StorageLive(result) then SwitchInt on the boolean parameter.
  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(2), span()));
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  auto entryTerminator = mir::MirTerminator::switchInt(
      mir::MirOperand::copy(resultPlace(localId(1), boolType)), zc::mv(arms), blockId(3), span());
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(
      mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements), zc::mv(entryTerminator)});

  // Then/else blocks: assign the result constant, then jump to the join.
  auto armBlock = [&](uint32_t id, uint8_t value) {
    zc::Vector<mir::MirStatement> statements;
    statements.add(mir::MirStatement::assign(
        resultPlace(localId(2), i32),
        mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(value))),
        mir::MirInitializationKind::Initialize, span()));
    return mir::MirBasicBlock{blockId(id), scopeId(1), zc::mv(statements),
                              mir::MirTerminator::gotoTarget(blockId(4), span())};
  };
  blocks.add(armBlock(2, thenValue));
  blocks.add(armBlock(3, elseValue));

  // Join block: return the result local.
  zc::Vector<mir::MirStatement> joinStatements;
  blocks.add(mir::MirBasicBlock{blockId(4), scopeId(1), zc::mv(joinStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(2), i32)), span())});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

// O5/KR5.2 multi-block widening: a boolean-conditional `if cond { return A } else
// { return B }` diamond lowers MIR -> LIR -> verified LLVM IR -> a native ELF
// object. The backend grows from one block to a four-block diamond: an i1
// parameter, an alloca for the result, a conditional branch, a store per arm,
// and a load + return at the join.
ZC_TEST("Boolean-conditional diamond lowers to a verified multi-block LLVM function") {
  tests::TestSemanticTypeContext typeContext;
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  auto function = buildConditionalReturn(owner, boolType, i32, 7, 9);

  auto lir = lir::MirToLirLowering::lowerConditionalReturn(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 1);
  ZC_EXPECT(lirModule.functions()[0].blocks().size() == 4);
  ZC_EXPECT(lirModule.functions()[0].parameters().size() == 1);

  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  // The diamond emits an i1 parameter, a conditional branch, an alloca, and the
  // two arm constants stored into it.
  ZC_EXPECT(ir.contains("i1 "_zc));
  ZC_EXPECT(ir.contains("br i1"_zc));
  ZC_EXPECT(ir.contains("alloca"_zc));
  ZC_EXPECT(ir.contains("store i32 7"_zc));
  ZC_EXPECT(ir.contains("store i32 9"_zc));
  ZC_EXPECT(ir.contains("ret i32"_zc));

  const auto object = result.objectCode();
  ZC_EXPECT(object.size() > 0);
  ZC_REQUIRE(object.size() >= 4);
  ZC_EXPECT(object[0] == 0x7f);
  ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
  ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
  ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
}

// Build the verified reducible four-block while-loop return MIR shape:
//   fun f(cond: bool) -> i32 { while cond {} return exitValue }
//   local#1 = cond : bool (Parameter); local#2 = result : i32 (FunctionResult)
//   bb1 entry:  StorageLive(local#2); Goto(bb2)
//   bb2 header: SwitchInt(copy local#1) [true -> bb3], default bb4
//   bb3 body:   Goto(bb2)   (reducible back-edge)
//   bb4 exit:   local#2 = const exitValue; return move local#2
mir::MirFunction buildLoopReturn(identity::DefId owner, identity::SemanticTypeId boolType,
                                 identity::SemanticTypeId i32, uint8_t exitValue) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, boolType,
                                      scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(2), mir::MirLocalKind::FunctionResult, i32,
                                      scopeId(1), span()});

  zc::Vector<mir::MirBasicBlock> blocks;

  // Entry: StorageLive(result); Goto(header).
  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(2), span()));
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements),
                                mir::MirTerminator::gotoTarget(blockId(2), span())});

  // Header: SwitchInt(cond) [true -> body], default = exit.
  zc::Vector<mir::MirStatement> headerStatements;
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  blocks.add(mir::MirBasicBlock{
      blockId(2), scopeId(1), zc::mv(headerStatements),
      mir::MirTerminator::switchInt(mir::MirOperand::copy(resultPlace(localId(1), boolType)),
                                    zc::mv(arms), blockId(4), span())});

  // Body: Goto(header) reducible back-edge.
  zc::Vector<mir::MirStatement> bodyStatements;
  blocks.add(mir::MirBasicBlock{blockId(3), scopeId(1), zc::mv(bodyStatements),
                                mir::MirTerminator::gotoTarget(blockId(2), span())});

  // Exit: assign the result constant, then return it.
  zc::Vector<mir::MirStatement> exitStatements;
  exitStatements.add(mir::MirStatement::assign(
      resultPlace(localId(2), i32),
      mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(exitValue))),
      mir::MirInitializationKind::Initialize, span()));
  blocks.add(mir::MirBasicBlock{blockId(4), scopeId(1), zc::mv(exitStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(2), i32)), span())});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

// O5/KR5.2 multi-block widening (loop shape): a reducible four-block while-loop
// lowers MIR -> LIR -> verified LLVM IR -> a native ELF object through the same
// generic multi-block emitter as the diamond. The loop exercises the back-edge
// (body Goto header) and the single-arm header SwitchInt -> CondBranch.
ZC_TEST("Reducible while-loop lowers to a verified multi-block LLVM function") {
  tests::TestSemanticTypeContext typeContext;
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  auto function = buildLoopReturn(owner, boolType, i32, 5);

  auto lir = lir::MirToLirLowering::lowerLoopReturn(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 1);
  ZC_EXPECT(lirModule.functions()[0].blocks().size() == 4);

  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("br i1"_zc));
  ZC_EXPECT(ir.contains("alloca"_zc));
  ZC_EXPECT(ir.contains("store i32 5"_zc));
  ZC_EXPECT(ir.contains("ret i32"_zc));

  const auto object = result.objectCode();
  ZC_EXPECT(object.size() > 0);
  ZC_REQUIRE(object.size() >= 4);
  ZC_EXPECT(object[0] == 0x7f);
  ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
  ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
  ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
}

// Build the verified four-block comparison-driven conditional return MIR shape:
//   fun f(a: i32, b: i32) -> i32 { if a == b { return thenValue } else { return elseValue } }
//   local#1 = a : i32 (Parameter); local#2 = b : i32 (Parameter)
//   local#3 = result : i32 (FunctionResult); local#4 = temp : bool (Temporary)
//   bb1 entry: StorageLive(#3); StorageLive(#4); #4 = (copy #1 == copy #2);
//              SwitchInt(copy #4) [true -> bb2], default bb3
//   bb2 then:  #3 = const thenValue; Goto(bb4)
//   bb3 else:  #3 = const elseValue; Goto(bb4)
//   bb4 join:  return move #3
mir::MirFunction buildEqualityConditionalReturn(identity::DefId owner,
                                                identity::SemanticTypeId boolType,
                                                identity::SemanticTypeId i32, uint8_t thenValue,
                                                uint8_t elseValue) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(
      mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, i32, scopeId(1), span()});
  locals.add(
      mir::MirLocalDeclaration{localId(2), mir::MirLocalKind::Parameter, i32, scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(3), mir::MirLocalKind::FunctionResult, i32,
                                      scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(4), mir::MirLocalKind::Temporary, boolType,
                                      scopeId(1), span()});

  zc::Vector<mir::MirBasicBlock> blocks;

  // Entry: StorageLive(result), StorageLive(temp), temp = (a == b), SwitchInt(temp).
  auto tempPlace = [&]() {
    return mir::MirPlace(localId(4), boolType, zc::Vector<mir::MirProjection>(), boolType);
  };
  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(3), span()));
  entryStatements.add(mir::MirStatement::storageLive(localId(4), span()));
  entryStatements.add(mir::MirStatement::assign(
      tempPlace(),
      mir::MirRvalue::comparison(mir::MirComparisonOperator::Eq,
                                 mir::MirOperand::copy(resultPlace(localId(1), i32)),
                                 mir::MirOperand::copy(resultPlace(localId(2), i32)), boolType),
      mir::MirInitializationKind::Initialize, span()));
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements),
                                mir::MirTerminator::switchInt(mir::MirOperand::copy(tempPlace()),
                                                              zc::mv(arms), blockId(3), span())});

  auto armBlock = [&](uint32_t id, uint8_t value) {
    zc::Vector<mir::MirStatement> statements;
    statements.add(mir::MirStatement::assign(
        resultPlace(localId(3), i32),
        mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(value))),
        mir::MirInitializationKind::Initialize, span()));
    return mir::MirBasicBlock{blockId(id), scopeId(1), zc::mv(statements),
                              mir::MirTerminator::gotoTarget(blockId(4), span())};
  };
  blocks.add(armBlock(2, thenValue));
  blocks.add(armBlock(3, elseValue));

  zc::Vector<mir::MirStatement> joinStatements;
  blocks.add(mir::MirBasicBlock{blockId(4), scopeId(1), zc::mv(joinStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(3), i32)), span())});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

// O5/KR5.2 multi-block widening (comparison-driven condition): an
// `if a == b { return X } else { return Y }` diamond lowers MIR -> LIR ->
// verified LLVM IR -> a native ELF object. This exercises the LIR Compare
// statement (-> LLVM icmp), integer value parameters threaded as LLVM args, and
// a CondBranch on a computed boolean temporary (not a parameter).
ZC_TEST("Comparison-driven conditional lowers to a verified multi-block LLVM function") {
  tests::TestSemanticTypeContext typeContext;
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  auto function = buildEqualityConditionalReturn(owner, boolType, i32, 3, 4);

  auto lir =
      lir::MirToLirLowering::lowerEqualityConditionalReturn(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 1);
  ZC_EXPECT(lirModule.functions()[0].parameters().size() == 2);

  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("icmp eq"_zc));
  ZC_EXPECT(ir.contains("br i1"_zc));
  ZC_EXPECT(ir.contains("store i32 3"_zc));
  ZC_EXPECT(ir.contains("store i32 4"_zc));
  ZC_EXPECT(ir.contains("ret i32"_zc));

  const auto object = result.objectCode();
  ZC_EXPECT(object.size() > 0);
  ZC_REQUIRE(object.size() >= 4);
  ZC_EXPECT(object[0] == 0x7f);
  ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
  ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
  ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
}

// Build the verified four-block conditional whose then-arm returns a parameter:
//   fun f(cond: bool, v: i32) -> i32 { if cond { return v } else { return elseValue } }
//   local#1 = cond : bool (Parameter); local#2 = v : i32 (Parameter)
//   local#3 = result : i32 (FunctionResult)
//   bb1: StorageLive(#3); SwitchInt(copy #1) [true -> bb2], default bb3
//   bb2: #3 = copy #2 (parameter place-use); Goto(bb4)
//   bb3: #3 = const elseValue; Goto(bb4)
//   bb4: return move #3
mir::MirFunction buildParameterArmConditional(identity::DefId owner,
                                              identity::SemanticTypeId boolType,
                                              identity::SemanticTypeId i32, uint8_t elseValue) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});

  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, boolType,
                                      scopeId(1), span()});
  locals.add(
      mir::MirLocalDeclaration{localId(2), mir::MirLocalKind::Parameter, i32, scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(3), mir::MirLocalKind::FunctionResult, i32,
                                      scopeId(1), span()});

  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(3), span()));
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{
      blockId(1), scopeId(1), zc::mv(entryStatements),
      mir::MirTerminator::switchInt(mir::MirOperand::copy(resultPlace(localId(1), boolType)),
                                    zc::mv(arms), blockId(3), span())});

  // Then arm returns the integer parameter v (a place-use).
  zc::Vector<mir::MirStatement> thenStatements;
  thenStatements.add(mir::MirStatement::assign(
      resultPlace(localId(3), i32),
      mir::MirRvalue::use(mir::MirOperand::copy(resultPlace(localId(2), i32))),
      mir::MirInitializationKind::Initialize, span()));
  blocks.add(mir::MirBasicBlock{blockId(2), scopeId(1), zc::mv(thenStatements),
                                mir::MirTerminator::gotoTarget(blockId(4), span())});

  // Else arm returns a constant.
  zc::Vector<mir::MirStatement> elseStatements;
  elseStatements.add(mir::MirStatement::assign(
      resultPlace(localId(3), i32),
      mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(elseValue))),
      mir::MirInitializationKind::Initialize, span()));
  blocks.add(mir::MirBasicBlock{blockId(3), scopeId(1), zc::mv(elseStatements),
                                mir::MirTerminator::gotoTarget(blockId(4), span())});

  zc::Vector<mir::MirStatement> joinStatements;
  blocks.add(mir::MirBasicBlock{blockId(4), scopeId(1), zc::mv(joinStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(3), i32)), span())});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

// O5/KR5.2 multi-block widening (parameter-valued arm): a conditional whose arm
// returns an integer parameter lowers MIR -> LIR -> verified LLVM -> native ELF
// object. This exercises the `localUse` operand loading a parameter argument's
// alloca slot in an arm store.
ZC_TEST("Conditional with a parameter-returning arm lowers to a verified function") {
  tests::TestSemanticTypeContext typeContext;
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  auto function = buildParameterArmConditional(owner, boolType, i32, 8);

  auto lir = lir::MirToLirLowering::lowerConditionalReturn(function, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 1);
  ZC_EXPECT(lirModule.functions()[0].parameters().size() == 2);

  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("br i1"_zc));
  ZC_EXPECT(ir.contains("store i32 8"_zc));
  ZC_EXPECT(ir.contains("ret i32"_zc));

  const auto object = result.objectCode();
  ZC_EXPECT(object.size() > 0);
  ZC_REQUIRE(object.size() >= 4);
  ZC_EXPECT(object[0] == 0x7f);
  ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
  ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
  ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
}

// Build a verified callee (scalar constant-return) and caller (two-block
// Call+Return) pair for a same-module zero-argument direct call:
//   fun g() -> i32 { return calleeValue }
//   fun f() -> i32 { let x = g(); return x }
mir::MirFunction buildScalarReturnCallee(identity::DefId owner, identity::SemanticTypeId i32,
                                         uint8_t calleeValue) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;  // no locals
  zc::Vector<mir::MirStatement> statements;     // no statements
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{
      blockId(1), scopeId(1), zc::mv(statements),
      mir::MirTerminator::returnValue(mir::MirOperand::constant(i32, integerConstant(calleeValue)),
                                      span())});
  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

mir::MirFunction buildLocalCallCaller(identity::DefId owner, identity::DefId callee,
                                      identity::SemanticTypeId i32) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(
      mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::UserLocal, i32, scopeId(1), span()});

  zc::Vector<mir::MirBasicBlock> blocks;
  // Entry: StorageLive(local); Call(g, [], dest=local, normalTarget=bb2).
  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(1), span()));
  blocks.add(mir::MirBasicBlock{
      blockId(1), scopeId(1), zc::mv(entryStatements),
      mir::MirTerminator::call(callee, zc::Vector<mir::MirOperand>(),
                               mir::MirCallEffect::noActivation(), resultPlace(localId(1), i32),
                               blockId(2), zc::none, span())});
  // Continuation: return the result local.
  zc::Vector<mir::MirStatement> contStatements;
  blocks.add(mir::MirBasicBlock{blockId(2), scopeId(1), zc::mv(contStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(1), i32)), span())});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

// O5/KR5.2 multi-block widening (same-module call): a zero-argument direct call
// `fun f() -> i32 { let x = g(); return x }` with a defined callee
// `fun g() -> i32 { return 5 }` lowers to a two-function LIR module that
// translates to verified LLVM IR (a `call` to a module-local defined function)
// and a native ELF object. Both functions are defined; no external/synthetic
// callee symbol is invented.
ZC_TEST("Same-module direct call lowers to a verified two-function LLVM module") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto callerOwner = tests::testDefinition(0);
  const auto calleeOwner = tests::testDefinition(1);

  auto callee = buildScalarReturnCallee(calleeOwner, i32, 5);
  auto caller = buildLocalCallCaller(callerOwner, calleeOwner, i32);

  auto lir = lir::MirToLirLowering::lowerCallModule(caller, callee, typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 2);

  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("call i32"_zc));
  ZC_EXPECT(ir.contains("zom.caller"_zc));
  ZC_EXPECT(ir.contains("zom.callee"_zc));
  ZC_EXPECT(ir.contains("ret i32 5"_zc));

  const auto object = result.objectCode();
  ZC_EXPECT(object.size() > 0);
  ZC_REQUIRE(object.size() >= 4);
  ZC_EXPECT(object[0] == 0x7f);
  ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
  ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
  ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
}

// O5/KR5.2 argument-carrying call: a same-module direct call passing one integer
// constant to a one-parameter callee that returns its parameter
// `fun id(x: i32) -> i32 { return x }`, `fun f() -> i32 { let r = id(9); return
// r }`, lowers to a two-function LIR module that translates to verified LLVM IR
// (a `call i32 @zom.callee(i32 9)`) and a native ELF object.
mir::MirFunction buildParameterReturnCallee(identity::DefId owner, identity::SemanticTypeId i32) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(
      mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, i32, scopeId(1), span()});
  zc::Vector<mir::MirStatement> statements;  // no statements
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(1), i32)), span())});
  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

mir::MirFunction buildArgumentCallCaller(identity::DefId owner, identity::DefId callee,
                                         identity::SemanticTypeId i32, uint8_t argumentValue) {
  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(
      mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::UserLocal, i32, scopeId(1), span()});

  zc::Vector<mir::MirBasicBlock> blocks;
  // Entry: StorageLive(local); Call(id, [const argumentValue], dest=local, bb2).
  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(1), span()));
  zc::Vector<mir::MirOperand> arguments;
  arguments.add(mir::MirOperand::constant(i32, integerConstant(argumentValue)));
  blocks.add(mir::MirBasicBlock{
      blockId(1), scopeId(1), zc::mv(entryStatements),
      mir::MirTerminator::call(callee, zc::mv(arguments), mir::MirCallEffect::noActivation(),
                               resultPlace(localId(1), i32), blockId(2), zc::none, span())});
  // Continuation: return the result local.
  zc::Vector<mir::MirStatement> contStatements;
  blocks.add(mir::MirBasicBlock{blockId(2), scopeId(1), zc::mv(contStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(1), i32)), span())});

  return mir::MirFunction{owner,
                          mir::MirFunctionKind::Function,
                          identity::DefinitionKind::Function,
                          i32,
                          span(),
                          zc::mv(scopes),
                          zc::mv(locals),
                          zc::mv(blocks)};
}

ZC_TEST("Same-module call with one integer argument lowers to a verified LLVM module") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto callerOwner = tests::testDefinition(0);
  const auto calleeOwner = tests::testDefinition(1);

  auto callee = buildParameterReturnCallee(calleeOwner, i32);
  auto caller = buildArgumentCallCaller(callerOwner, calleeOwner, i32, 9);

  auto lir = lir::MirToLirLowering::lowerCallModuleWithArgument(caller, callee,
                                                                typeContext.semanticTypes());
  ZC_REQUIRE(lir != zc::none);
  const auto& lirModule = ZC_REQUIRE_NONNULL(lir);
  ZC_EXPECT(lirModule.functions().size() == 2);

  LlvmTranslator translator;
  auto result = translator.translate(lirModule);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("call i32"_zc));
  ZC_EXPECT(ir.contains("i32 9"_zc));
  ZC_EXPECT(ir.contains("zom.caller"_zc));
  ZC_EXPECT(ir.contains("zom.callee"_zc));

  const auto object = result.objectCode();
  ZC_REQUIRE(object.size() >= 4);
  ZC_EXPECT(object[0] == 0x7f);
  ZC_EXPECT(object[1] == static_cast<uint8_t>('E'));
  ZC_EXPECT(object[2] == static_cast<uint8_t>('L'));
  ZC_EXPECT(object[3] == static_cast<uint8_t>('F'));
}

ZC_TEST("Zero-argument call lowering rejects a callee whose owner differs from the call target") {
  // Regression: the caller's call targets a DIFFERENT definition than the callee
  // function being lowered. Wiring it to LIR function index 1 anyway would call
  // the wrong function. lowerCallModule must reject the mismatch.
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto callerOwner = tests::testDefinition(0);
  const auto calleeOwner = tests::testDefinition(1);
  const auto wrongCallee = tests::testDefinition(2);

  auto callee = buildScalarReturnCallee(calleeOwner, i32, 5);
  // Caller's call.callee is wrongCallee, not calleeOwner.
  auto caller = buildLocalCallCaller(callerOwner, wrongCallee, i32);

  auto lir = lir::MirToLirLowering::lowerCallModule(caller, callee, typeContext.semanticTypes());
  ZC_EXPECT(lir == zc::none);
}

ZC_TEST("Argument call lowering rejects a callee whose owner differs from the call target") {
  tests::TestSemanticTypeContext typeContext;
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto callerOwner = tests::testDefinition(0);
  const auto calleeOwner = tests::testDefinition(1);
  const auto wrongCallee = tests::testDefinition(2);

  auto callee = buildParameterReturnCallee(calleeOwner, i32);
  auto caller = buildArgumentCallCaller(callerOwner, wrongCallee, i32, 9);

  auto lir = lir::MirToLirLowering::lowerCallModuleWithArgument(caller, callee,
                                                                typeContext.semanticTypes());
  ZC_EXPECT(lir == zc::none);
}

ZC_TEST("MIR -> LIR lowering fails closed on a non-integer scalar initializer") {
  tests::TestSemanticTypeContext typeContext;
  // Bool is a scalar but not an integer carrier in this slice.
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto owner = tests::testDefinition(0);

  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::ModuleInitializerResult,
                                      boolType, scopeId(1), span()});
  zc::Vector<mir::MirStatement> statements;
  statements.add(mir::MirStatement::storageLive(localId(1), span()));
  statements.add(mir::MirStatement::assign(
      resultPlace(localId(1), boolType),
      mir::MirRvalue::use(mir::MirOperand::constant(
          boolType, checker::checked::CanonicalConstValue::boolean(true))),
      mir::MirInitializationKind::Initialize, span()));
  auto terminator = mir::MirTerminator::returnValue(
      mir::MirOperand::move(resultPlace(localId(1), boolType)), span());
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(statements), zc::mv(terminator)});
  mir::MirFunction function{owner,
                            mir::MirFunctionKind::ModuleInitializer,
                            identity::DefinitionKind::Static,
                            boolType,
                            span(),
                            zc::mv(scopes),
                            zc::mv(locals),
                            zc::mv(blocks)};

  auto lir = lir::MirToLirLowering::lowerScalarInitializer(function, typeContext.semanticTypes());
  ZC_EXPECT(lir == zc::none);
}

// Builds a single-block LIR function that returns a multi-slot integer bundle
// directly (the RFC 0021 carrier-bundle shape). The slots are i32 constants in
// the given order.
lir::LirModule buildAggregateReturnModule(zc::ArrayPtr<const uint64_t> slotValues) {
  auto i32 = lir::LirValueType::integer(lir::IntegerBitWidth::Bit32);
  ZC_REQUIRE(i32 != zc::none);
  const auto carrier = ZC_REQUIRE_NONNULL(i32);

  zc::Vector<lir::LirIntegerConstant> slots(slotValues.size());
  for (const auto value : slotValues) {
    auto constant = lir::LirIntegerConstant::from(carrier, value);
    ZC_REQUIRE(constant != zc::none);
    slots.add(ZC_REQUIRE_NONNULL(constant));
  }
  auto terminator = lir::LirTerminator::returnAggregate(zc::mv(slots));
  ZC_REQUIRE(terminator != zc::none);

  auto blockId = lir::LirBlockId::fromOrdinal(1);
  ZC_REQUIRE(blockId != zc::none);
  zc::Vector<lir::LirBasicBlock> blocks;
  blocks.add(
      lir::LirBasicBlock(ZC_REQUIRE_NONNULL(blockId), ZC_REQUIRE_NONNULL(zc::mv(terminator))));

  zc::Vector<lir::LirFunction> functions;
  functions.add(lir::LirFunction(zc::heapString("zom.module_init"), carrier, zc::mv(blocks)));
  return lir::LirModule(zc::mv(functions));
}

ZC_TEST("Multi-slot aggregate return lowers to a verified LLVM literal struct") {
  const uint64_t values[] = {42, 7};
  auto module = buildAggregateReturnModule(zc::arrayPtr(values));

  LlvmTranslator translator;
  auto result = translator.translate(module);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  // Assert the structural shape (a literal two-field i32 struct built by two
  // insertvalue instructions and returned) rather than one brittle string.
  const auto ir = result.textualIr();
  ZC_EXPECT(ir.contains("{ i32, i32 }"_zc));
  ZC_EXPECT(ir.contains("insertvalue"_zc));
  ZC_EXPECT(ir.contains("i32 42, 0"_zc));
  ZC_EXPECT(ir.contains("i32 7, 1"_zc));
  ZC_EXPECT(ir.contains("ret { i32, i32 }"_zc));
}

ZC_TEST("Single-slot aggregate return lowers to a verified one-field struct") {
  const uint64_t values[] = {99};
  auto module = buildAggregateReturnModule(zc::arrayPtr(values));

  LlvmTranslator translator;
  auto result = translator.translate(module);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }
  ZC_EXPECT(result.textualIr().contains("{ i32 }"_zc));
  ZC_EXPECT(result.textualIr().contains("insertvalue"_zc));
}

ZC_TEST("Scalar integer return is unchanged by the aggregate return path") {
  // A single-block ReturnInteger function still returns a bare i32, proving the
  // literal-struct path does not perturb the scalar return.
  auto i32 = lir::LirValueType::integer(lir::IntegerBitWidth::Bit32);
  ZC_REQUIRE(i32 != zc::none);
  const auto carrier = ZC_REQUIRE_NONNULL(i32);
  auto constant = lir::LirIntegerConstant::from(carrier, 5);
  ZC_REQUIRE(constant != zc::none);
  auto blockId = lir::LirBlockId::fromOrdinal(1);
  ZC_REQUIRE(blockId != zc::none);
  zc::Vector<lir::LirBasicBlock> blocks;
  blocks.add(lir::LirBasicBlock(ZC_REQUIRE_NONNULL(blockId),
                                lir::LirTerminator::returnInteger(ZC_REQUIRE_NONNULL(constant))));
  zc::Vector<lir::LirFunction> functions;
  functions.add(lir::LirFunction(zc::heapString("zom.module_init"), carrier, zc::mv(blocks)));
  lir::LirModule module(zc::mv(functions));

  LlvmTranslator translator;
  auto result = translator.translate(module);
  ZC_EXPECT(result.verified());
  ZC_EXPECT(result.textualIr().contains("ret i32 5"_zc));
  ZC_EXPECT(!result.textualIr().contains("insertvalue"_zc));
}

// KR1.4 fail-closed coverage: each production multi-block lowering must reject a
// structurally near-miss MIR function that violates exactly one of its own shape
// gates. The near-miss is otherwise a complete four-block function, so these
// prove genuine shape rejection rather than trivial empty-input handling.

// Diamond boundary: the entry block must hold exactly one statement (the result
// StorageLive). A near-miss with a second entry statement -- still a four-block
// SwitchInt diamond -- must be rejected (mir-to-lir.cc entry.statements.size()
// check). One statement is the diamond; three is the comparison shape; two is
// neither.
ZC_TEST("Boolean-conditional lowering fails closed on a two-statement entry block") {
  tests::TestSemanticTypeContext typeContext;
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, boolType,
                                      scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(2), mir::MirLocalKind::FunctionResult, i32,
                                      scopeId(1), span()});

  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(2), span()));
  // The single injected violation: a second entry statement (a duplicate
  // StorageLive) that the one-statement diamond gate must reject.
  entryStatements.add(mir::MirStatement::storageLive(localId(2), span()));
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{
      blockId(1), scopeId(1), zc::mv(entryStatements),
      mir::MirTerminator::switchInt(mir::MirOperand::copy(resultPlace(localId(1), boolType)),
                                    zc::mv(arms), blockId(3), span())});
  auto armBlock = [&](uint32_t id, uint8_t value) {
    zc::Vector<mir::MirStatement> statements;
    statements.add(mir::MirStatement::assign(
        resultPlace(localId(2), i32),
        mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(value))),
        mir::MirInitializationKind::Initialize, span()));
    return mir::MirBasicBlock{blockId(id), scopeId(1), zc::mv(statements),
                              mir::MirTerminator::gotoTarget(blockId(4), span())};
  };
  blocks.add(armBlock(2, 7));
  blocks.add(armBlock(3, 9));
  zc::Vector<mir::MirStatement> joinStatements;
  blocks.add(mir::MirBasicBlock{blockId(4), scopeId(1), zc::mv(joinStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(2), i32)), span())});
  mir::MirFunction function{owner,
                            mir::MirFunctionKind::Function,
                            identity::DefinitionKind::Function,
                            i32,
                            span(),
                            zc::mv(scopes),
                            zc::mv(locals),
                            zc::mv(blocks)};

  ZC_EXPECT(lir::MirToLirLowering::lowerConditionalReturn(function, typeContext.semanticTypes()) ==
            zc::none);
}

// Loop boundary: the body block must be empty (its only role is the reducible
// back-edge). A near-miss with one body statement -- still a four-block loop with
// the correct entry/header/body/exit terminators -- must be rejected
// (mir-to-lir.cc body.statements.size() check).
ZC_TEST("Reducible while-loop lowering fails closed on a non-empty body block") {
  tests::TestSemanticTypeContext typeContext;
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, boolType,
                                      scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(2), mir::MirLocalKind::FunctionResult, i32,
                                      scopeId(1), span()});
  zc::Vector<mir::MirBasicBlock> blocks;

  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(2), span()));
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements),
                                mir::MirTerminator::gotoTarget(blockId(2), span())});
  zc::Vector<mir::MirStatement> headerStatements;
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(3)});
  blocks.add(mir::MirBasicBlock{
      blockId(2), scopeId(1), zc::mv(headerStatements),
      mir::MirTerminator::switchInt(mir::MirOperand::copy(resultPlace(localId(1), boolType)),
                                    zc::mv(arms), blockId(4), span())});
  // The single injected violation: a body statement (a StorageLive) in the block
  // that the loop gate requires to be empty.
  zc::Vector<mir::MirStatement> bodyStatements;
  bodyStatements.add(mir::MirStatement::storageLive(localId(2), span()));
  blocks.add(mir::MirBasicBlock{blockId(3), scopeId(1), zc::mv(bodyStatements),
                                mir::MirTerminator::gotoTarget(blockId(2), span())});
  zc::Vector<mir::MirStatement> exitStatements;
  exitStatements.add(mir::MirStatement::assign(
      resultPlace(localId(2), i32),
      mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(5))),
      mir::MirInitializationKind::Initialize, span()));
  blocks.add(mir::MirBasicBlock{blockId(4), scopeId(1), zc::mv(exitStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(2), i32)), span())});
  mir::MirFunction function{owner,
                            mir::MirFunctionKind::Function,
                            identity::DefinitionKind::Function,
                            i32,
                            span(),
                            zc::mv(scopes),
                            zc::mv(locals),
                            zc::mv(blocks)};

  ZC_EXPECT(lir::MirToLirLowering::lowerLoopReturn(function, typeContext.semanticTypes()) ==
            zc::none);
}

// Comparison boundary: the entry's third statement must assign the boolean
// temporary from a Comparison rvalue. A near-miss that assigns the temporary from
// a plain Use (a copy) -- still a four-block, three-entry-statement diamond with
// the temporary local -- must be rejected (mir-to-lir.cc tempAssign.value.kind()
// check).
ZC_TEST("Comparison-driven conditional lowering fails closed on a non-comparison temporary") {
  tests::TestSemanticTypeContext typeContext;
  const auto boolType = typeContext.internPrimitive(type::semantic::PrimitiveKind::Bool);
  const auto i32 = typeContext.internPrimitive(type::semantic::PrimitiveKind::I32);
  const auto owner = tests::testDefinition(0);

  zc::Vector<mir::MirSourceScope> scopes;
  scopes.add(mir::MirSourceScope{scopeId(1), zc::none, span()});
  zc::Vector<mir::MirLocalDeclaration> locals;
  locals.add(
      mir::MirLocalDeclaration{localId(1), mir::MirLocalKind::Parameter, i32, scopeId(1), span()});
  locals.add(
      mir::MirLocalDeclaration{localId(2), mir::MirLocalKind::Parameter, i32, scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(3), mir::MirLocalKind::FunctionResult, i32,
                                      scopeId(1), span()});
  locals.add(mir::MirLocalDeclaration{localId(4), mir::MirLocalKind::Temporary, boolType,
                                      scopeId(1), span()});
  auto tempPlace = [&]() {
    return mir::MirPlace(localId(4), boolType, zc::Vector<mir::MirProjection>(), boolType);
  };
  zc::Vector<mir::MirStatement> entryStatements;
  entryStatements.add(mir::MirStatement::storageLive(localId(3), span()));
  entryStatements.add(mir::MirStatement::storageLive(localId(4), span()));
  // The single injected violation: the boolean temporary is assigned from a Use
  // (a boolean constant) instead of the required Comparison rvalue. The local
  // layout is otherwise identical to the lowerable comparison shape (two i32
  // parameters, an i32 result, and a bool temporary), so only the rvalue kind
  // differs.
  entryStatements.add(mir::MirStatement::assign(
      tempPlace(),
      mir::MirRvalue::use(mir::MirOperand::constant(
          boolType, checker::checked::CanonicalConstValue::boolean(true))),
      mir::MirInitializationKind::Initialize, span()));
  zc::Vector<mir::MirSwitchIntArm> arms;
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(true), blockId(2)});
  arms.add(mir::MirSwitchIntArm{checker::checked::CanonicalConstValue::boolean(false), blockId(3)});
  zc::Vector<mir::MirBasicBlock> blocks;
  blocks.add(mir::MirBasicBlock{blockId(1), scopeId(1), zc::mv(entryStatements),
                                mir::MirTerminator::switchInt(mir::MirOperand::copy(tempPlace()),
                                                              zc::mv(arms), blockId(3), span())});
  auto armBlock = [&](uint32_t id, uint8_t value) {
    zc::Vector<mir::MirStatement> statements;
    statements.add(mir::MirStatement::assign(
        resultPlace(localId(3), i32),
        mir::MirRvalue::use(mir::MirOperand::constant(i32, integerConstant(value))),
        mir::MirInitializationKind::Initialize, span()));
    return mir::MirBasicBlock{blockId(id), scopeId(1), zc::mv(statements),
                              mir::MirTerminator::gotoTarget(blockId(4), span())};
  };
  blocks.add(armBlock(2, 3));
  blocks.add(armBlock(3, 4));
  zc::Vector<mir::MirStatement> joinStatements;
  blocks.add(mir::MirBasicBlock{blockId(4), scopeId(1), zc::mv(joinStatements),
                                mir::MirTerminator::returnValue(
                                    mir::MirOperand::move(resultPlace(localId(3), i32)), span())});
  mir::MirFunction function{owner,
                            mir::MirFunctionKind::Function,
                            identity::DefinitionKind::Function,
                            i32,
                            span(),
                            zc::mv(scopes),
                            zc::mv(locals),
                            zc::mv(blocks)};

  ZC_EXPECT(lir::MirToLirLowering::lowerEqualityConditionalReturn(
                function, typeContext.semanticTypes()) == zc::none);
}

// KR5.2 S-call: a three-function LIR module (a same-module direct-call caller, its
// constant-return callee, and one standalone leaf that no call references)
// translates to a verified LLVM module. This pins, at the translator unit level,
// two properties the object-emission CLI (which only checks ELF magic) cannot:
// the leaf is emitted as its own defined function, and the caller's single call
// targets the callee, never the leaf. The module is built directly to isolate the
// translator from the MIR -> LIR selector. The `calleeIndex` is the callee's
// emission-order position (1), not the MIR array position.
lir::LirModule buildCallWithLeafModule() {
  auto i32 = lir::LirValueType::integer(lir::IntegerBitWidth::Bit32);
  ZC_REQUIRE(i32 != zc::none);
  const auto carrier = ZC_REQUIRE_NONNULL(i32);

  auto callerEntry = lir::LirBlockId::fromOrdinal(1);
  auto callerCont = lir::LirBlockId::fromOrdinal(2);
  auto calleeEntry = lir::LirBlockId::fromOrdinal(1);
  auto leafEntry = lir::LirBlockId::fromOrdinal(1);
  ZC_REQUIRE(callerEntry != zc::none && callerCont != zc::none && calleeEntry != zc::none &&
             leafEntry != zc::none);

  auto calleeConstant = lir::LirIntegerConstant::from(carrier, 42);
  auto leafConstant = lir::LirIntegerConstant::from(carrier, 9);
  ZC_REQUIRE(calleeConstant != zc::none && leafConstant != zc::none);

  zc::Vector<lir::LirFunction> functions;

  // Function 0: the caller. Its entry block calls emission-order index 1 (the
  // callee) storing the result into local ordinal 1, then continues to a return
  // of that local. The leaf at index 2 is never referenced.
  {
    zc::Vector<lir::LirBasicBlock> callerBlocks;
    zc::Vector<lir::LirStatement> entryStatements;
    callerBlocks.add(lir::LirBasicBlock(
        ZC_REQUIRE_NONNULL(callerEntry), zc::mv(entryStatements),
        lir::LirTerminator::callFunction(/*calleeIndex=*/1, /*destinationOrdinal=*/1,
                                         ZC_REQUIRE_NONNULL(callerCont))));
    zc::Vector<lir::LirStatement> contStatements;
    callerBlocks.add(lir::LirBasicBlock(ZC_REQUIRE_NONNULL(callerCont), zc::mv(contStatements),
                                        lir::LirTerminator::returnLocal(1)));
    zc::Vector<lir::LirLocal> parameters;
    zc::Vector<lir::LirLocal> locals;
    locals.add(lir::LirLocal(1, carrier));
    functions.add(lir::LirFunction(zc::heapString("zom.caller"), carrier, zc::mv(parameters),
                                   zc::mv(locals), zc::mv(callerBlocks)));
  }

  // Function 1: the callee, a single block returning its integer constant.
  {
    zc::Vector<lir::LirBasicBlock> calleeBlocks;
    calleeBlocks.add(
        lir::LirBasicBlock(ZC_REQUIRE_NONNULL(calleeEntry),
                           lir::LirTerminator::returnInteger(ZC_REQUIRE_NONNULL(calleeConstant))));
    functions.add(lir::LirFunction(zc::heapString("zom.callee"), carrier, zc::mv(calleeBlocks)));
  }

  // Function 2: the standalone leaf, a single block returning its integer
  // constant. It calls nothing and is called by nothing.
  {
    zc::Vector<lir::LirBasicBlock> leafBlocks;
    leafBlocks.add(
        lir::LirBasicBlock(ZC_REQUIRE_NONNULL(leafEntry),
                           lir::LirTerminator::returnInteger(ZC_REQUIRE_NONNULL(leafConstant))));
    functions.add(lir::LirFunction(zc::heapString("zom.leaf"), carrier, zc::mv(leafBlocks)));
  }

  return lir::LirModule(zc::mv(functions));
}

// Counts non-overlapping occurrences of `needle` in `haystack`.
size_t countOccurrences(zc::StringPtr haystack, zc::StringPtr needle) {
  size_t count = 0;
  size_t from = 0;
  const zc::ArrayPtr<const char> hay = haystack.asArray();
  const zc::ArrayPtr<const char> pin = needle.asArray();
  if (pin.size() == 0 || hay.size() < pin.size()) { return 0; }
  for (size_t i = 0; i + pin.size() <= hay.size(); ++i) {
    if (i < from) { continue; }
    bool match = true;
    for (size_t j = 0; j < pin.size(); ++j) {
      if (hay[i + j] != pin[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      ++count;
      from = i + pin.size();
    }
  }
  return count;
}

ZC_TEST("Three-function call-with-leaf module translates with the leaf uncalled") {
  auto module = buildCallWithLeafModule();

  LlvmTranslator translator;
  auto result = translator.translate(module);
  ZC_EXPECT(result.verified());
  if (!result.verified()) { ZC_FAIL_EXPECT(result.diagnostic().cStr()); }

  const auto ir = result.textualIr();
  // All three functions are defined in the module.
  ZC_EXPECT(ir.contains("zom.caller"_zc));
  ZC_EXPECT(ir.contains("zom.callee"_zc));
  ZC_EXPECT(ir.contains("zom.leaf"_zc));

  // Exactly one call is emitted, and it targets the callee, never the leaf.
  ZC_EXPECT(countOccurrences(ir, "call i32"_zc) == 1);
  ZC_EXPECT(ir.contains("call i32 @zom.callee("_zc));
  ZC_EXPECT(!ir.contains("call i32 @zom.leaf("_zc));

  // The callee and the leaf each return their own integer constant.
  ZC_EXPECT(ir.contains("ret i32 42"_zc));
  ZC_EXPECT(ir.contains("ret i32 9"_zc));
}

}  // namespace
}  // namespace zomlang::compiler::backend::llvm
