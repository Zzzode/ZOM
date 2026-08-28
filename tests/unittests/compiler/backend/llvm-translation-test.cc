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

// RFC 0021 O5/KR5.2 first widening slice: the backend already parameterizes on
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

}  // namespace
}  // namespace zomlang::compiler::backend::llvm
