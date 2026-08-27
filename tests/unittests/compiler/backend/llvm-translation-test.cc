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

#include "zc/ztest/test.h"
#include "compiler/backend/llvm/llvm-translator.h"
#include "compiler/checker/facts/signature-facts.h"
#include "compiler/identity/source-snapshot.h"
#include "compiler/lir/mir-to-lir.h"
#include "compiler/mir/built-mir.h"
#include "compiler/type/semantic-type-data.h"
#include "tests/unittests/compiler/test-semantic-identities.h"
#include "tests/unittests/compiler/test-semantic-type-context.h"

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
