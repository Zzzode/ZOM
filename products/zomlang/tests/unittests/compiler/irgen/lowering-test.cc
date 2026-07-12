// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/irgen/lowering.h"

#include "zc/core/io.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/irgen/ir-dump.h"
#include "zomlang/compiler/type/function-type.h"
#include "zomlang/compiler/type/named-type.h"
#include "zomlang/compiler/type/primitive-type.h"
#include "zomlang/compiler/type/union-type.h"
#include "zomlang/tests/unittests/compiler/test-ast-builder.h"
#include "zomlang/tests/unittests/compiler/test-semantic-identities.h"
#include "zomlang/tests/unittests/compiler/test-semantic-type-context.h"

namespace zomlang {
namespace compiler {
namespace irgen {

namespace {

struct CheckedFunction final {
  ast::Tree tree;
  ast::BindingMetadata metadata;
  tests::TestTypeEnv typeEnv;
};

CheckedFunction makeCheckedIntegerFunction(zc::Own<type::Type> raisesType, bool propagate = false,
                                           bool freezeDispatch = true,
                                           bool coercibleReturnMismatch = false,
                                           bool emptyReturn = false) {
  tests::TestFixture fixture;
  auto parameters = fixture.makeFunctionParamList(ast::NodeList());
  auto literal = fixture.makeIntLiteral(42);
  auto value = propagate
                   ? fixture.makePostfixExpr(ast::PostfixOperatorKind::ErrorPropagate, literal)
                   : literal;
  auto returnStatement = fixture.makeReturnStmt(emptyReturn ? ast::NodeId() : value);
  zc::Vector<ast::NodeId> bodyItems;
  bodyItems.add(fixture.makeStatementListItem(returnStatement));
  auto body = fixture.makeBlockStmt(fixture.makeNodeList(bodyItems.asPtr()));
  auto function =
      fixture.makeFunctionDecl("answer"_zc, body, parameters, fixture.makeNamedTypeExpr("i32"_zc),
                               fixture.makeNamedTypeExpr("Error"_zc));
  zc::Vector<ast::NodeId> declarations;
  declarations.add(function);
  auto tree = fixture.buildSourceFile("test"_zc, declarations.asPtr());

  ast::BindingMetadata metadata;
  metadata.resizeFor(tree);
  metadata.setDefinition(function, tests::makeTestDefinitionIds(1)[0]);

  tests::TestTypeEnv typeEnv;
  zc::Vector<zc::Own<type::Type>> parameterTypes;
  auto functionType =
      zc::heap<type::FunctionType>(zc::mv(parameterTypes), type::PrimitiveType::createI32());
  functionType->setRaisesType(zc::mv(raisesType));
  typeEnv.setType(function, zc::mv(functionType));
  typeEnv.setType(literal, coercibleReturnMismatch ? type::PrimitiveType::createI16()
                                                   : type::PrimitiveType::createI32());
  if (propagate) { typeEnv.setType(value, type::PrimitiveType::createI32()); }
  if (freezeDispatch) { typeEnv.freezeDispatch(); }
  return CheckedFunction{zc::mv(tree), zc::mv(metadata), zc::mv(typeEnv)};
}

type::SemanticTypeId addDirectI32Layout(Module& module) {
  auto success = type::PrimitiveType::createI32();
  const auto valueType = module.getSemanticTypeStore().intern(*success);
  auto layout = computeErrorUnionLayout(module.getSemanticTypeStore(), module.getTarget(), *success,
                                        *success);
  const auto layoutIndex = module.addErrorUnionLayout(zc::mv(layout));
  ZC_ASSERT(layoutIndex != zc::none);
  return valueType;
}

void addSingleBlockFunction(Module& module, type::SemanticTypeId checkedSignature,
                            type::SemanticTypeId abiReturnType, uint32_t layoutIndex,
                            Terminator terminator) {
  zc::Vector<Instruction> instructions;
  instructions.add(Instruction(ValueId(1), IntegerConstant{abiReturnType, zc::str("42")}));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(BlockId(1), zc::none, zc::mv(instructions), zc::mv(terminator)));
  module.addFunction(Function(zc::str("answer"), tests::makeTestDefinitionIds(1)[0],
                              checkedSignature, abiReturnType, layoutIndex, zc::mv(blocks)));
}

CheckedFunction makeCheckedPropagationFunctions(
    bool multipleSourceErrors = false, bool externalTarget = false,
    ast::PostfixOperatorKind operatorKind = ast::PostfixOperatorKind::ErrorPropagate,
    bool directAnswerLayout = false, bool omitDispatch = false,
    bool invalidDispatchSymbol = false) {
  tests::TestFixture fixture;

  auto fetchParameters = fixture.makeFunctionParamList(ast::NodeList());
  auto fetchLiteral = fixture.makeIntLiteral(42);
  auto fetchReturn = fixture.makeReturnStmt(fetchLiteral);
  zc::Vector<ast::NodeId> fetchBodyItems;
  fetchBodyItems.add(fixture.makeStatementListItem(fetchReturn));
  auto fetchBody = fixture.makeBlockStmt(fixture.makeNodeList(fetchBodyItems.asPtr()));
  auto fetch = fixture.makeFunctionDecl("fetch"_zc, fetchBody, fetchParameters,
                                        fixture.makeNamedTypeExpr("i32"_zc),
                                        fixture.makeNamedTypeExpr("str"_zc));

  auto answerParameters = fixture.makeFunctionParamList(ast::NodeList());
  auto callee = fixture.makeIdentExpr("fetch"_zc);
  auto call = fixture.makeCallExpr(callee, ast::NodeList());
  auto propagation = fixture.makePostfixExpr(operatorKind, call);
  auto answerReturn = fixture.makeReturnStmt(propagation);
  zc::Vector<ast::NodeId> answerBodyItems;
  answerBodyItems.add(fixture.makeStatementListItem(answerReturn));
  auto answerBody = fixture.makeBlockStmt(fixture.makeNodeList(answerBodyItems.asPtr()));
  auto answer = fixture.makeFunctionDecl("answer"_zc, answerBody, answerParameters,
                                         fixture.makeNamedTypeExpr("i32"_zc),
                                         fixture.makeNamedTypeExpr("Error"_zc));

  zc::Vector<ast::NodeId> declarations;
  declarations.add(fetch);
  declarations.add(answer);
  auto tree = fixture.buildSourceFile("test"_zc, declarations.asPtr());

  auto definitions = tests::makeTestDefinitionIds(3);
  const auto fetchSymbol = definitions[0];
  const auto answerSymbol = definitions[1];
  ast::BindingMetadata metadata;
  metadata.resizeFor(tree);
  metadata.setDefinition(fetch, fetchSymbol);
  metadata.setDefinition(answer, answerSymbol);

  tests::TestTypeEnv typeEnv;
  zc::Vector<zc::Own<type::Type>> fetchParameterTypes;
  auto fetchType =
      zc::heap<type::FunctionType>(zc::mv(fetchParameterTypes), type::PrimitiveType::createI32());
  if (multipleSourceErrors) {
    zc::Vector<zc::Own<type::Type>> fetchErrors;
    fetchErrors.add(type::PrimitiveType::createBool());
    fetchErrors.add(type::PrimitiveType::createStr());
    fetchType->setRaisesType(zc::heap<type::UnionType>(zc::mv(fetchErrors)));
  } else {
    fetchType->setRaisesType(type::PrimitiveType::createStr());
  }
  typeEnv.setType(fetch, zc::mv(fetchType));
  typeEnv.setType(fetchLiteral, type::PrimitiveType::createI32());

  zc::Vector<zc::Own<type::Type>> answerParameterTypes;
  auto answerType =
      zc::heap<type::FunctionType>(zc::mv(answerParameterTypes), type::PrimitiveType::createI32());
  if (directAnswerLayout) {
    answerType->setRaisesType(type::PrimitiveType::createNever());
  } else {
    zc::Vector<zc::Own<type::Type>> answerErrors;
    answerErrors.add(type::PrimitiveType::createBool());
    answerErrors.add(type::PrimitiveType::createStr());
    answerType->setRaisesType(zc::heap<type::UnionType>(zc::mv(answerErrors)));
  }
  typeEnv.setType(answer, zc::mv(answerType));

  zc::Vector<zc::Own<type::Type>> callAlternatives;
  callAlternatives.add(type::PrimitiveType::createI32());
  if (multipleSourceErrors) { callAlternatives.add(type::PrimitiveType::createBool()); }
  callAlternatives.add(type::PrimitiveType::createStr());
  typeEnv.setType(call, zc::heap<type::UnionType>(zc::mv(callAlternatives)));
  typeEnv.setType(propagation, type::PrimitiveType::createI32());
  type::CallDispatchRecord dispatch;
  dispatch.targetKind = type::CallTargetKind::FreeFunction;
  dispatch.targetDefinition =
      invalidDispatchSymbol ? identity::DefId() : (externalTarget ? definitions[2] : fetchSymbol);
  dispatch.resultType = typeEnv.getSemanticTypeId(call);
  if (!omitDispatch) { typeEnv.setDispatch(call, zc::mv(dispatch)); }
  typeEnv.freezeDispatch();

  return CheckedFunction{zc::mv(tree), zc::mv(metadata), zc::mv(typeEnv)};
}

}  // namespace

ZC_TEST("IrLowering.ConstructsCanonicalSuccessAlternative") {
  auto checked = makeCheckedIntegerFunction(type::PrimitiveType::createStr());
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<Module>());
  const auto& module = result.get<Module>();
  ZC_ASSERT(module.getErrorUnionLayouts().size() == 1);
  ZC_EXPECT(module.getErrorUnionLayouts()[0].kind == ErrorUnionLayoutKind::TaggedUnion);
  ZC_ASSERT(module.getFunctions().size() == 1);
  const auto& block = module.getFunctions()[0].blocks[0];
  ZC_ASSERT(block.instructions.size() == 2);
  ZC_EXPECT(block.instructions[0].data.is<IntegerConstant>());
  ZC_EXPECT(block.instructions[1].data.is<ErrorUnionConstruct>());
  ZC_EXPECT(block.instructions[1].data.get<ErrorUnionConstruct>().tag == 0);
  ZC_ASSERT(block.terminator.is<ReturnTerminator>());
  ZC_EXPECT(block.terminator.get<ReturnTerminator>().value == ValueId(2));
}

ZC_TEST("IrLowering.PassesThroughDirectSuccessLayout") {
  auto checked = makeCheckedIntegerFunction(type::PrimitiveType::createNever());
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::ilp32());

  ZC_ASSERT(result.is<Module>());
  const auto& module = result.get<Module>();
  ZC_EXPECT(module.getErrorUnionLayouts()[0].kind == ErrorUnionLayoutKind::DirectSuccess);
  const auto& block = module.getFunctions()[0].blocks[0];
  ZC_ASSERT(block.instructions.size() == 1);
  ZC_EXPECT(block.instructions[0].data.is<IntegerConstant>());
  ZC_ASSERT(block.terminator.is<ReturnTerminator>());
  ZC_EXPECT(block.terminator.get<ReturnTerminator>().value == ValueId(1));
  ZC_EXPECT(block.terminator.get<ReturnTerminator>().valueType ==
            block.instructions[0].data.get<IntegerConstant>().resultType);
}

ZC_TEST("IrLowering.RejectsUnknownTargetLayout") {
  auto checked = makeCheckedIntegerFunction(zc::heap<type::NamedType>("IoError"_zc));
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::UnknownTargetLayout);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::TargetLayout);
}

ZC_TEST("IrLowering.ReportsUnsupportedCoercionWithoutInvariantFailure") {
  auto checked = makeCheckedIntegerFunction(type::PrimitiveType::createNever(), false, true, true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::UnsupportedExpression);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::Expression);
}

ZC_TEST("IrLowering.ReportsDirectLayoutForcedUnwrapAsUnsupported") {
  auto checked =
      makeCheckedPropagationFunctions(false, false, ast::PostfixOperatorKind::ErrorUnwrap, true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::UnsupportedExpression);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::ForcedUnwrap);
}

ZC_TEST("IrLowering.RejectsForcedUnwrapRangeOutsideSourceContext") {
  auto checked =
      makeCheckedPropagationFunctions(false, false, ast::PostfixOperatorKind::ErrorUnwrap);
  source::SourceManager sourceManager;
  const auto buffer =
      sourceManager.addMemBufferCopy(zc::str("fn answer() {}"_zc).asBytes(), "test.zom"_zc);
  LoweringSourceContext sourceContext(sourceManager, buffer);
  auto result = lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv,
                                 TargetDataLayout::lp64(), sourceContext);

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::InvalidSourceRange);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::ForcedUnwrap);
}

ZC_TEST("IrLowering.RejectsUnfrozenDispatchFacts") {
  auto checked = makeCheckedIntegerFunction(type::PrimitiveType::createStr(), false, false);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::DispatchNotFrozen);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::CheckedInput);
}

ZC_TEST("IrLowering.RejectsBindingMetadataWithWrongCapacity") {
  auto checked = makeCheckedIntegerFunction(type::PrimitiveType::createStr());
  ast::BindingMetadata emptyMetadata;
  auto result =
      lowerCheckedTree(checked.tree, emptyMetadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::InvalidBindingMetadata);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::CheckedInput);
}

ZC_TEST("IrLowering.ClassifiesEmptyReturnAsUnsupportedSourceShape") {
  auto checked =
      makeCheckedIntegerFunction(type::PrimitiveType::createStr(), false, true, false, true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::UnsupportedSourceShape);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::FunctionBody);
}

ZC_TEST("IrLowering.RejectsNonCallErrorPropagationOperand") {
  auto checked = makeCheckedIntegerFunction(type::PrimitiveType::createStr(), true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::UnsupportedExpression);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::ErrorPropagation);
}

ZC_TEST("IrLowering.RejectsMissingDispatchFactPrecisely") {
  auto checked = makeCheckedPropagationFunctions(
      false, false, ast::PostfixOperatorKind::ErrorPropagate, false, true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::MissingDispatchFact);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::ErrorPropagation);
}

ZC_TEST("IrLowering.RejectsInvalidDispatchFactPrecisely") {
  auto checked = makeCheckedPropagationFunctions(
      false, false, ast::PostfixOperatorKind::ErrorPropagate, false, false, true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::InvalidDispatchFact);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::ErrorPropagation);
}

ZC_TEST("IrLowering.RemapsResidualAndUsesSharedCleanupReturnBlock") {
  auto checked = makeCheckedPropagationFunctions();
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<Module>());
  const auto& module = result.get<Module>();
  ZC_ASSERT(module.getFunctions().size() == 2);
  const auto& answer = module.getFunctions()[1];
  ZC_ASSERT(answer.blocks.size() == 4);
  ZC_EXPECT(answer.blocks[0].terminator.is<ErrorUnionBranchTerminator>());
  ZC_EXPECT(answer.blocks[1].terminator.is<JumpTerminator>());
  ZC_EXPECT(answer.blocks[2].terminator.is<JumpTerminator>());
  ZC_EXPECT(answer.blocks[1].terminator.get<JumpTerminator>().target == BlockId(4));
  ZC_EXPECT(answer.blocks[2].terminator.get<JumpTerminator>().target == BlockId(4));
  ZC_ASSERT(answer.blocks[2].instructions.size() == 2);
  ZC_ASSERT(answer.blocks[2].instructions[1].data.is<ErrorUnionConstruct>());
  ZC_EXPECT(answer.blocks[2].instructions[1].data.get<ErrorUnionConstruct>().tag == 2);
  ZC_EXPECT(answer.blocks[3].parameter != zc::none);
  ZC_EXPECT(answer.blocks[3].terminator.is<ReturnTerminator>());
}

ZC_TEST("IrLowering.RejectsMultipleSourceResidualAlternatives") {
  auto checked = makeCheckedPropagationFunctions(true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind == LoweringFailureKind::UnsupportedExpression);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::ErrorPropagation);
}

ZC_TEST("IrLowering.RejectsCallTargetOutsideSourceModule") {
  auto checked = makeCheckedPropagationFunctions(false, true);
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());

  ZC_ASSERT(result.is<LoweringFailure>());
  ZC_EXPECT(result.get<LoweringFailure>().kind ==
            LoweringFailureKind::UnsupportedCrossSourceTarget);
  ZC_EXPECT(result.get<LoweringFailure>().phase == LoweringPhase::ModuleVerification);
}

ZC_TEST("IrLowering.DumpIsDeterministicAndIndependentOfAstNodeIds") {
  auto checked = makeCheckedIntegerFunction(type::PrimitiveType::createStr());
  auto result =
      lowerCheckedTree(checked.tree, checked.metadata, checked.typeEnv, TargetDataLayout::lp64());
  ZC_ASSERT(result.is<Module>());

  zc::VectorOutputStream first;
  zc::VectorOutputStream second;
  ZC_EXPECT(dumpModule(first, result.get<Module>()) == zc::none);
  ZC_EXPECT(dumpModule(second, result.get<Module>()) == zc::none);
  const auto firstText = zc::str(first.getArray().asChars());
  const auto secondText = zc::str(second.getArray().asChars());
  ZC_EXPECT(firstText == secondText);
  ZC_EXPECT(!firstText.contains("node="_zc));
  ZC_EXPECT(firstText.contains("integer.constant 42 type=i32"_zc));
}

ZC_TEST("IrModule.RejectsInvalidLayoutAccessWithoutDebugAssertions") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());

  ZC_EXPECT(module.addErrorUnionLayout(ErrorUnionLayout()) == zc::none);
  ZC_EXPECT(module.getErrorUnionLayout(0) == zc::none);
}

ZC_TEST("IrDump.RejectsUnresolvedCallTargetBeforeWritingOutput") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());
  auto success = type::PrimitiveType::createI32();
  const auto valueType = module.getSemanticTypeStore().intern(*success);
  auto layout = computeErrorUnionLayout(module.getSemanticTypeStore(), module.getTarget(), *success,
                                        *success);
  const auto layoutIndex = module.addErrorUnionLayout(zc::mv(layout));
  ZC_ASSERT(layoutIndex != zc::none);

  zc::Vector<Instruction> instructions;
  auto definitions = tests::makeTestDefinitionIds(2);
  instructions.add(Instruction(ValueId(1), RaisingCall{valueType, definitions[1]}));
  zc::Vector<BasicBlock> blocks;
  blocks.add(BasicBlock(BlockId(1), zc::none, zc::mv(instructions),
                        ReturnTerminator{ValueId(1), valueType}));
  module.addFunction(
      Function(zc::str("broken"), definitions[0], valueType, valueType, 0, zc::mv(blocks)));

  zc::VectorOutputStream output;
  const auto dumpResult = dumpModule(output, module);
  ZC_ASSERT(dumpResult != zc::none);
  ZC_IF_SOME(dumpFailure, dumpResult) {
    ZC_EXPECT(dumpFailure.kind == IrDumpFailureKind::UnresolvedCallTarget);
    ZC_EXPECT(dumpFailure.site == IrDumpVerifierSite::Instruction);
    ZC_EXPECT(dumpFailure.definition == definitions[1]);
  }
  ZC_EXPECT(output.getArray().size() == 0);
}

ZC_TEST("IrDump.RejectsInvalidTypeReferenceBeforeWritingOutput") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());
  const auto valueType = addDirectI32Layout(module);
  addSingleBlockFunction(module, tests::testSemanticType(99), valueType, 0,
                         ReturnTerminator{ValueId(1), valueType});

  zc::VectorOutputStream output;
  const auto result = dumpModule(output, module);
  ZC_ASSERT(result != zc::none);
  ZC_IF_SOME(failure, result) {
    ZC_EXPECT(failure.kind == IrDumpFailureKind::InvalidTypeReference);
  }
  ZC_EXPECT(output.getArray().size() == 0);
}

ZC_TEST("IrDump.RejectsInvalidLayoutReferenceBeforeWritingOutput") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());
  const auto valueType = addDirectI32Layout(module);
  addSingleBlockFunction(module, valueType, valueType, 99, ReturnTerminator{ValueId(1), valueType});

  zc::VectorOutputStream output;
  const auto result = dumpModule(output, module);
  ZC_ASSERT(result != zc::none);
  ZC_IF_SOME(failure, result) {
    ZC_EXPECT(failure.kind == IrDumpFailureKind::InvalidLayoutReference);
  }
  ZC_EXPECT(output.getArray().size() == 0);
}

ZC_TEST("IrDump.RejectsInvalidBlockReferenceBeforeWritingOutput") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());
  const auto valueType = addDirectI32Layout(module);
  addSingleBlockFunction(module, valueType, valueType, 0, JumpTerminator{BlockId(99), ValueId(1)});

  zc::VectorOutputStream output;
  const auto result = dumpModule(output, module);
  ZC_ASSERT(result != zc::none);
  ZC_IF_SOME(failure, result) {
    ZC_EXPECT(failure.kind == IrDumpFailureKind::InvalidBlockReference);
  }
  ZC_EXPECT(output.getArray().size() == 0);
}

ZC_TEST("IrDump.RejectsInvalidValueReferenceBeforeWritingOutput") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());
  const auto valueType = addDirectI32Layout(module);
  addSingleBlockFunction(module, valueType, valueType, 0, ReturnTerminator{ValueId(99), valueType});

  zc::VectorOutputStream output;
  const auto result = dumpModule(output, module);
  ZC_ASSERT(result != zc::none);
  ZC_IF_SOME(failure, result) { ZC_EXPECT(failure.kind == IrDumpFailureKind::InvalidTerminator); }
  ZC_EXPECT(output.getArray().size() == 0);
}

ZC_TEST("IrDump.RejectsInvalidLayoutKindBeforeWritingOutput") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());
  auto success = type::PrimitiveType::createI32();
  auto layout = computeErrorUnionLayout(module.getSemanticTypeStore(), module.getTarget(), *success,
                                        *success);
  layout.kind = static_cast<ErrorUnionLayoutKind>(255);
  ZC_ASSERT(module.addErrorUnionLayout(zc::mv(layout)) != zc::none);

  zc::VectorOutputStream output;
  const auto result = dumpModule(output, module);
  ZC_ASSERT(result != zc::none);
  ZC_IF_SOME(failure, result) {
    ZC_EXPECT(failure.kind == IrDumpFailureKind::InvalidLayout);
    ZC_EXPECT(failure.site == IrDumpVerifierSite::Layout);
  }
  ZC_EXPECT(output.getArray().size() == 0);
}

ZC_TEST("IrDump.RejectsInvalidTagTypeBeforeWritingOutput") {
  tests::TestSemanticTypeContext semanticContext;
  Module module(semanticContext.semanticTypes(), TargetDataLayout::lp64());
  auto success = type::PrimitiveType::createI32();
  auto error = type::PrimitiveType::createStr();
  auto layout = computeFunctionErrorUnionLayout(module.getSemanticTypeStore(), module.getTarget(),
                                                *success, *error);
  layout.tagType = static_cast<ErrorUnionTagType>(255);
  ZC_ASSERT(module.addErrorUnionLayout(zc::mv(layout)) != zc::none);

  zc::VectorOutputStream output;
  const auto result = dumpModule(output, module);
  ZC_ASSERT(result != zc::none);
  ZC_IF_SOME(failure, result) { ZC_EXPECT(failure.kind == IrDumpFailureKind::InvalidLayout); }
  ZC_EXPECT(output.getArray().size() == 0);
}

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
