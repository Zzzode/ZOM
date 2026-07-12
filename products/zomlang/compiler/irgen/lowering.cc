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

#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/irgen/error-union-layout.h"
#include "zomlang/compiler/type/function-type.h"

namespace zomlang {
namespace compiler {
namespace irgen {

namespace {

LoweringFailure failure(LoweringFailureKind kind, LoweringPhase phase, ast::NodeId node) {
  return LoweringFailure{kind, phase, node};
}

struct SourceOffsets final {
  uint32_t start = 0;
  uint32_t end = 0;
};

zc::Maybe<SourceOffsets> sourceOffsetsFor(const LoweringSourceContext& context,
                                          source::SourceRange range) {
  if (range.isInvalid()) { return zc::none; }
  const auto startBuffer = context.manager.findBufferContainingLoc(range.getStart());
  const auto endBuffer = context.manager.findBufferContainingLoc(range.getEnd());
  if (startBuffer == zc::none || endBuffer == zc::none) { return zc::none; }

  bool startMatches = false;
  bool endMatches = false;
  ZC_IF_SOME(buffer, startBuffer) { startMatches = buffer == context.buffer; }
  ZC_IF_SOME(buffer, endBuffer) { endMatches = buffer == context.buffer; }
  if (!startMatches || !endMatches) { return zc::none; }

  const auto start = context.manager.getLocOffsetInBuffer(range.getStart(), context.buffer);
  const auto end = context.manager.getLocOffsetInBuffer(range.getEnd(), context.buffer);
  if (end < start) { return zc::none; }
  return SourceOffsets{start, end};
}

bool getBlockStatements(const ast::Tree& tree, ast::NodeId body, ast::NodeList& statements) {
  if (!body || !tree.contains(body)) { return false; }
  const auto& block = tree.node(body);
  if (block.kind != ast::SyntaxKind::BlockStmt) { return false; }
  statements.first = block.payload.words[ast::kBlockStmtStmtsFirstWord];
  statements.size = block.payload.words[ast::kBlockStmtStmtsSizeWord];
  return tree.contains(statements);
}

ast::NodeId unwrapStatement(const ast::Tree& tree, ast::NodeId wrapperId) {
  if (!tree.contains(wrapperId)) { return ast::NodeId(); }
  const auto& wrapper = tree.node(wrapperId);
  if (wrapper.kind != ast::SyntaxKind::StatementListItem) { return ast::NodeId(); }
  const ast::NodeId item(wrapper.payload.words[ast::kStatementListItemItemWord]);
  return tree.contains(item) ? item : ast::NodeId();
}

zc::Maybe<uint64_t> findTagByCanonicalKey(const Module& module, const ErrorUnionLayout& layout,
                                          type::SemanticTypeId semanticTypeId) {
  const auto key = module.getSemanticTypeStore().getCanonicalKey(semanticTypeId);
  for (const auto& alternative : layout.alternatives) {
    if (module.getSemanticTypeStore().getCanonicalKey(alternative.semanticTypeId) == key) {
      return alternative.tag;
    }
  }
  return zc::none;
}

bool hasFunctionDefinition(const Module& module, identity::DefId definition) {
  for (const auto& function : module.getFunctions()) {
    if (function.definition == definition) { return true; }
  }
  return false;
}

bool allCallTargetsResolve(const Module& module) {
  for (const auto& function : module.getFunctions()) {
    for (const auto& block : function.blocks) {
      for (const auto& instruction : block.instructions) {
        if (instruction.data.is<RaisingCall>() &&
            !hasFunctionDefinition(module, instruction.data.get<RaisingCall>().target)) {
          return false;
        }
      }
    }
  }
  return true;
}

bool functionSymbolsAreUnique(const Module& module) {
  const auto functions = module.getFunctions();
  for (size_t i = 0; i < functions.size(); ++i) {
    for (size_t j = i + 1; j < functions.size(); ++j) {
      if (functions[i].definition == functions[j].definition) { return false; }
    }
  }
  return true;
}

zc::Maybe<LoweringFailure> lowerDirectReturn(Module& module, const ast::Tree& tree,
                                             const type::TypeEnv& typeEnv,
                                             const type::FunctionType& functionType,
                                             ast::NodeId value, ErrorUnionLayoutKind layoutKind,
                                             type::SemanticTypeId layoutType, uint32_t layoutIndex,
                                             type::SemanticTypeId successType, uint32_t& nextValue,
                                             zc::Vector<BasicBlock>& blocks) {
  if (!value || !tree.contains(value) || tree.node(value).kind != ast::SyntaxKind::IntLiteral) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::Expression, value);
  }
  if (!typeEnv.hasType(value)) {
    return failure(LoweringFailureKind::MissingTypeFact, LoweringPhase::Expression, value);
  }
  const auto& valueType = typeEnv.find(typeEnv.getType(value));
  if (!valueType.equals(functionType.getReturnType())) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::Expression, value);
  }

  const auto constantValue = ValueId(nextValue++);
  const auto literalId = ast::BigIntId(tree.node(value).payload.words[ast::kIntLiteralValueWord]);
  zc::Vector<Instruction> instructions;
  instructions.add(
      Instruction(constantValue, IntegerConstant{successType, zc::str(tree.bigInt(literalId))}));

  auto returnValue = constantValue;
  auto abiReturnType = successType;
  if (layoutKind == ErrorUnionLayoutKind::TaggedUnion) {
    returnValue = ValueId(nextValue++);
    abiReturnType = layoutType;
    instructions.add(
        Instruction(returnValue, ErrorUnionConstruct{layoutType, constantValue, layoutIndex, 0}));
  }

  blocks.add(BasicBlock(BlockId(1), zc::none, zc::mv(instructions),
                        ReturnTerminator{returnValue, abiReturnType}));
  return zc::none;
}

zc::Maybe<LoweringFailure> lowerPropagationReturn(
    Module& module, const ast::Tree& tree, const type::TypeEnv& typeEnv,
    const type::FunctionType& functionType, ast::NodeId postfix,
    const ErrorUnionLayout& enclosingLayout, uint32_t enclosingLayoutIndex, uint32_t& nextValue,
    zc::Vector<BasicBlock>& blocks) {
  if (enclosingLayout.kind != ErrorUnionLayoutKind::TaggedUnion) {
    return failure(LoweringFailureKind::ErrorUnionLayoutMismatch, LoweringPhase::ErrorPropagation,
                   postfix);
  }
  const auto& postfixNode = tree.node(postfix);
  const ast::NodeId call(postfixNode.payload.words[ast::kPostfixExpressionOperandWord]);
  if (!tree.contains(call) || tree.node(call).kind != ast::SyntaxKind::CallExpression) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ErrorPropagation,
                   postfix);
  }
  const auto& callNode = tree.node(call);
  if (callNode.payload.words[ast::kCallExpressionTypeArgsSizeWord] != 0 ||
      callNode.payload.words[ast::kCallExpressionArgsSizeWord] != 0) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ErrorPropagation,
                   call);
  }
  if (!typeEnv.hasType(call) || !typeEnv.hasType(postfix)) {
    return failure(LoweringFailureKind::MissingTypeFact, LoweringPhase::ErrorPropagation, call);
  }
  if (!typeEnv.hasDispatch(call)) {
    return failure(LoweringFailureKind::MissingDispatchFact, LoweringPhase::ErrorPropagation, call);
  }
  const auto& dispatch = typeEnv.getDispatch(call);
  if (dispatch.targetKind != type::CallTargetKind::FreeFunction) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ErrorPropagation,
                   call);
  }
  if (!dispatch.targetDefinition.isValid()) {
    return failure(LoweringFailureKind::InvalidDispatchFact, LoweringPhase::ErrorPropagation, call);
  }
  if (dispatch.resultType != typeEnv.getSemanticTypeId(call)) {
    return failure(LoweringFailureKind::InvalidDispatchFact, LoweringPhase::ErrorPropagation, call);
  }

  const auto& callType = typeEnv.find(typeEnv.getType(call));
  const auto& successTypeValue = typeEnv.find(typeEnv.getType(postfix));
  if (!type::isUnion(callType)) {
    return failure(LoweringFailureKind::InvalidTypeFact, LoweringPhase::ErrorPropagation, call);
  }
  if (!successTypeValue.equals(functionType.getReturnType())) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ErrorPropagation,
                   postfix);
  }

  auto callLayout = computeErrorUnionLayout(module.getSemanticTypeStore(), module.getTarget(),
                                            callType, successTypeValue);
  if (callLayout.kind != ErrorUnionLayoutKind::TaggedUnion ||
      callLayout.payloadLayoutState == ErrorUnionPayloadLayoutState::Unknown ||
      callLayout.alternatives.size() != 2) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ErrorPropagation,
                   call);
  }
  const auto callLayoutType = callLayout.semanticTypeId;
  const auto enclosingLayoutType = enclosingLayout.semanticTypeId;
  const auto callSuccessType = callLayout.alternatives[0].semanticTypeId;
  const auto callErrorType = callLayout.alternatives[1].semanticTypeId;
  const auto callErrorTag = callLayout.alternatives[1].tag;
  auto destinationErrorTag = findTagByCanonicalKey(module, enclosingLayout, callErrorType);
  if (destinationErrorTag == zc::none) {
    return failure(LoweringFailureKind::MissingErrorAlternative, LoweringPhase::ErrorPropagation,
                   call);
  }
  uint64_t enclosingErrorTag = 0;
  ZC_IF_SOME(tag, destinationErrorTag) { enclosingErrorTag = tag; }
  const auto maybeCallLayoutIndex = module.addErrorUnionLayout(zc::mv(callLayout));
  if (maybeCallLayoutIndex == zc::none) {
    return failure(LoweringFailureKind::ErrorUnionLayoutMismatch, LoweringPhase::ErrorPropagation,
                   call);
  }
  uint32_t callLayoutIndex = 0;
  ZC_IF_SOME(index, maybeCallLayoutIndex) { callLayoutIndex = index; }

  const auto callValue = ValueId(nextValue++);
  zc::Vector<Instruction> entryInstructions;
  entryInstructions.add(
      Instruction(callValue, RaisingCall{callLayoutType, dispatch.targetDefinition}));
  blocks.add(
      BasicBlock(BlockId(1), zc::none, zc::mv(entryInstructions),
                 ErrorUnionBranchTerminator{callValue, callLayoutIndex, BlockId(2), BlockId(3)}));

  zc::Vector<Instruction> successInstructions;
  const auto successPayload = ValueId(nextValue++);
  successInstructions.add(Instruction(
      successPayload, ErrorUnionMovePayload{callSuccessType, callValue, callLayoutIndex, 0}));
  const auto successResult = ValueId(nextValue++);
  successInstructions.add(Instruction(
      successResult,
      ErrorUnionConstruct{enclosingLayoutType, successPayload, enclosingLayoutIndex, 0}));
  blocks.add(BasicBlock(BlockId(2), zc::none, zc::mv(successInstructions),
                        JumpTerminator{BlockId(4), successResult}));

  zc::Vector<Instruction> errorInstructions;
  const auto errorPayload = ValueId(nextValue++);
  errorInstructions.add(
      Instruction(errorPayload,
                  ErrorUnionMovePayload{callErrorType, callValue, callLayoutIndex, callErrorTag}));
  const auto errorResult = ValueId(nextValue++);
  errorInstructions.add(
      Instruction(errorResult, ErrorUnionConstruct{enclosingLayoutType, errorPayload,
                                                   enclosingLayoutIndex, enclosingErrorTag}));
  blocks.add(BasicBlock(BlockId(3), zc::none, zc::mv(errorInstructions),
                        JumpTerminator{BlockId(4), errorResult}));

  const auto cleanupValue = ValueId(nextValue++);
  zc::Vector<Instruction> noInstructions;
  blocks.add(BasicBlock(BlockId(4), BlockParameter{cleanupValue, enclosingLayoutType},
                        zc::mv(noInstructions),
                        ReturnTerminator{cleanupValue, enclosingLayoutType}));
  return zc::none;
}

zc::Maybe<LoweringFailure> lowerForcedUnwrapReturn(
    Module& module, const ast::Tree& tree, const type::TypeEnv& typeEnv,
    const type::FunctionType& functionType, ast::NodeId postfix,
    const ErrorUnionLayout& enclosingLayout, uint32_t enclosingLayoutIndex, uint32_t& nextValue,
    zc::Vector<BasicBlock>& blocks, zc::Maybe<const LoweringSourceContext&> sourceContext) {
  if (enclosingLayout.kind != ErrorUnionLayoutKind::TaggedUnion) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ForcedUnwrap,
                   postfix);
  }
  if (sourceContext == zc::none) {
    return failure(LoweringFailureKind::MissingSourceContext, LoweringPhase::ForcedUnwrap, postfix);
  }

  const auto& postfixNode = tree.node(postfix);
  const ast::NodeId call(postfixNode.payload.words[ast::kPostfixExpressionOperandWord]);
  if (!tree.contains(call) || tree.node(call).kind != ast::SyntaxKind::CallExpression) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ForcedUnwrap,
                   postfix);
  }
  const auto& callNode = tree.node(call);
  if (callNode.payload.words[ast::kCallExpressionTypeArgsSizeWord] != 0 ||
      callNode.payload.words[ast::kCallExpressionArgsSizeWord] != 0) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ForcedUnwrap, call);
  }
  if (!typeEnv.hasType(call) || !typeEnv.hasType(postfix)) {
    return failure(LoweringFailureKind::MissingTypeFact, LoweringPhase::ForcedUnwrap, call);
  }
  if (!typeEnv.hasDispatch(call)) {
    return failure(LoweringFailureKind::MissingDispatchFact, LoweringPhase::ForcedUnwrap, call);
  }
  const auto& dispatch = typeEnv.getDispatch(call);
  if (dispatch.targetKind != type::CallTargetKind::FreeFunction) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ForcedUnwrap, call);
  }
  if (!dispatch.targetDefinition.isValid() ||
      dispatch.resultType != typeEnv.getSemanticTypeId(call)) {
    return failure(LoweringFailureKind::InvalidDispatchFact, LoweringPhase::ForcedUnwrap, call);
  }

  const auto& callType = typeEnv.find(typeEnv.getType(call));
  const auto& successTypeValue = typeEnv.find(typeEnv.getType(postfix));
  if (!type::isUnion(callType)) {
    return failure(LoweringFailureKind::InvalidTypeFact, LoweringPhase::ForcedUnwrap, call);
  }
  if (!successTypeValue.equals(functionType.getReturnType())) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ForcedUnwrap,
                   postfix);
  }
  auto callLayout = computeErrorUnionLayout(module.getSemanticTypeStore(), module.getTarget(),
                                            callType, successTypeValue);
  if (callLayout.kind != ErrorUnionLayoutKind::TaggedUnion ||
      callLayout.payloadLayoutState == ErrorUnionPayloadLayoutState::Unknown ||
      callLayout.alternatives.size() != 2) {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::ForcedUnwrap, call);
  }

  const auto callLayoutType = callLayout.semanticTypeId;
  const auto callSuccessType = callLayout.alternatives[0].semanticTypeId;
  const auto callErrorType = callLayout.alternatives[1].semanticTypeId;
  const auto callErrorTag = callLayout.alternatives[1].tag;
  const auto enclosingLayoutType = enclosingLayout.semanticTypeId;
  const auto maybeCallLayoutIndex = module.addErrorUnionLayout(zc::mv(callLayout));
  if (maybeCallLayoutIndex == zc::none) {
    return failure(LoweringFailureKind::ErrorUnionLayoutMismatch, LoweringPhase::ForcedUnwrap,
                   call);
  }
  uint32_t callLayoutIndex = 0;
  ZC_IF_SOME(index, maybeCallLayoutIndex) { callLayoutIndex = index; }

  PanicSourceMetadata panicMetadata;
  ZC_IF_SOME(context, sourceContext) {
    const auto maybeOffsets = sourceOffsetsFor(context, postfixNode.range);
    if (maybeOffsets == zc::none) {
      return failure(LoweringFailureKind::InvalidSourceRange, LoweringPhase::ForcedUnwrap, postfix);
    }
    uint32_t expressionEnd = 0;
    ZC_IF_SOME(offsets, maybeOffsets) { expressionEnd = offsets.end; }
    if (expressionEnd < 2) {
      return failure(LoweringFailureKind::InvalidSourceRange, LoweringPhase::ForcedUnwrap, postfix);
    }
    const auto operatorStart = expressionEnd - 2;
    const auto operatorLoc = context.manager.getLocForOffset(context.buffer, operatorStart);
    const auto lineAndColumn =
        context.manager.getPresumedLineAndColumnForLoc(operatorLoc, context.buffer);
    panicMetadata.file = zc::str(context.manager.getIdentifierForBuffer(context.buffer));
    panicMetadata.line = lineAndColumn.line;
    panicMetadata.column = lineAndColumn.column;
    panicMetadata.byteStart = operatorStart;
    panicMetadata.byteEnd = expressionEnd;
    panicMetadata.payloadType = callErrorType;
  }

  const auto callValue = ValueId(nextValue++);
  zc::Vector<Instruction> entryInstructions;
  entryInstructions.add(
      Instruction(callValue, RaisingCall{callLayoutType, dispatch.targetDefinition}));
  blocks.add(
      BasicBlock(BlockId(1), zc::none, zc::mv(entryInstructions),
                 ErrorUnionBranchTerminator{callValue, callLayoutIndex, BlockId(2), BlockId(3)}));

  zc::Vector<Instruction> successInstructions;
  const auto successPayload = ValueId(nextValue++);
  successInstructions.add(Instruction(
      successPayload, ErrorUnionMovePayload{callSuccessType, callValue, callLayoutIndex, 0}));
  const auto successResult = ValueId(nextValue++);
  successInstructions.add(Instruction(
      successResult,
      ErrorUnionConstruct{enclosingLayoutType, successPayload, enclosingLayoutIndex, 0}));
  blocks.add(BasicBlock(BlockId(2), zc::none, zc::mv(successInstructions),
                        JumpTerminator{BlockId(4), successResult}));

  zc::Vector<Instruction> noErrorInstructions;
  blocks.add(BasicBlock(BlockId(3), zc::none, zc::mv(noErrorInstructions),
                        ForcedUnwrapPanicTerminator{callValue, callLayoutIndex, callErrorTag,
                                                    zc::mv(panicMetadata)}));

  const auto returnValue = ValueId(nextValue++);
  zc::Vector<Instruction> noReturnInstructions;
  blocks.add(BasicBlock(BlockId(4), BlockParameter{returnValue, enclosingLayoutType},
                        zc::mv(noReturnInstructions),
                        ReturnTerminator{returnValue, enclosingLayoutType}));
  return zc::none;
}

zc::Maybe<LoweringFailure> lowerFunction(Module& module, const ast::Tree& tree,
                                         const ast::BindingMetadata& metadata,
                                         const type::TypeEnv& typeEnv, ast::NodeId functionId,
                                         zc::Maybe<const LoweringSourceContext&> sourceContext) {
  const auto& functionNode = tree.node(functionId);
  const ast::NodeId parameters(functionNode.payload.words[ast::kFunctionDeclParamsIdWord]);
  const ast::NodeId typeParameters(functionNode.payload.words[ast::kFunctionDeclTypeParamsIdWord]);
  if (!parameters || !tree.contains(parameters) || typeParameters ||
      tree.node(parameters).kind != ast::SyntaxKind::FunctionParameterList ||
      tree.node(parameters).payload.words[ast::kFunctionParameterListNparamsWord] != 0) {
    return failure(LoweringFailureKind::UnsupportedSourceShape, LoweringPhase::FunctionSignature,
                   functionId);
  }
  const auto functionSymbol = metadata.definition(functionId);
  if (!functionSymbol.isValid()) {
    return failure(LoweringFailureKind::MissingBindingSymbol, LoweringPhase::FunctionSignature,
                   functionId);
  }
  if (!typeEnv.hasType(functionId)) {
    return failure(LoweringFailureKind::MissingTypeFact, LoweringPhase::FunctionSignature,
                   functionId);
  }
  const auto& checkedType = typeEnv.find(typeEnv.getType(functionId));
  if (!type::isFunction(checkedType)) {
    return failure(LoweringFailureKind::InvalidTypeFact, LoweringPhase::FunctionSignature,
                   functionId);
  }
  const auto& functionType = static_cast<const type::FunctionType&>(checkedType);
  const auto raisesType = functionType.getRaisesType();
  if (raisesType == zc::none) {
    return failure(LoweringFailureKind::InvalidTypeFact, LoweringPhase::FunctionSignature,
                   functionId);
  }

  ErrorUnionLayout enclosingLayout;
  ZC_IF_SOME(raises, raisesType) {
    enclosingLayout = computeFunctionErrorUnionLayout(
        module.getSemanticTypeStore(), module.getTarget(), functionType.getReturnType(), raises);
  }
  if (enclosingLayout.payloadLayoutState == ErrorUnionPayloadLayoutState::Unknown) {
    return failure(LoweringFailureKind::UnknownTargetLayout, LoweringPhase::TargetLayout,
                   functionId);
  }
  const auto layoutKind = enclosingLayout.kind;
  const auto layoutType = enclosingLayout.semanticTypeId;
  const auto successType = module.getSemanticTypeStore().intern(functionType.getReturnType());
  const auto checkedSignature = module.getSemanticTypeStore().intern(functionType);
  const auto maybeLayoutIndex = module.addErrorUnionLayout(zc::mv(enclosingLayout));
  if (maybeLayoutIndex == zc::none) {
    return failure(LoweringFailureKind::ErrorUnionLayoutMismatch, LoweringPhase::TargetLayout,
                   functionId);
  }
  uint32_t layoutIndex = 0;
  ZC_IF_SOME(index, maybeLayoutIndex) { layoutIndex = index; }
  const auto maybeStoredLayout = module.getErrorUnionLayout(layoutIndex);
  if (maybeStoredLayout == zc::none) {
    return failure(LoweringFailureKind::ErrorUnionLayoutMismatch, LoweringPhase::TargetLayout,
                   functionId);
  }
  const auto& storedLayout = module.getErrorUnionLayouts()[layoutIndex];
  const auto abiReturnType =
      layoutKind == ErrorUnionLayoutKind::DirectSuccess ? successType : layoutType;

  const ast::NodeId body(functionNode.payload.words[ast::kFunctionDeclBodyWord]);
  ast::NodeList statements;
  if (!getBlockStatements(tree, body, statements) || statements.size != 1) {
    return failure(LoweringFailureKind::UnsupportedSourceShape, LoweringPhase::FunctionBody, body);
  }

  uint32_t nextValue = 1;
  const auto statementIds = tree.list(statements);
  const auto returnStatement = unwrapStatement(tree, statementIds.back());
  if (!returnStatement || tree.node(returnStatement).kind != ast::SyntaxKind::ReturnStmt) {
    return failure(LoweringFailureKind::UnsupportedSourceShape, LoweringPhase::FunctionBody,
                   returnStatement);
  }
  const ast::NodeId returnValue(
      tree.node(returnStatement).payload.words[ast::kReturnStmtValueWord]);
  if (!tree.contains(returnValue)) {
    return failure(LoweringFailureKind::UnsupportedSourceShape, LoweringPhase::FunctionBody,
                   returnStatement);
  }

  zc::Vector<BasicBlock> blocks;
  zc::Maybe<LoweringFailure> loweringFailure;
  if (tree.node(returnValue).kind == ast::SyntaxKind::IntLiteral) {
    loweringFailure =
        lowerDirectReturn(module, tree, typeEnv, functionType, returnValue, layoutKind, layoutType,
                          layoutIndex, successType, nextValue, blocks);
  } else if (tree.node(returnValue).kind == ast::SyntaxKind::PostfixExpression &&
             static_cast<ast::PostfixOperatorKind>(
                 tree.node(returnValue).payload.words[ast::kPostfixExpressionOpWord]) ==
                 ast::PostfixOperatorKind::ErrorPropagate) {
    loweringFailure = lowerPropagationReturn(module, tree, typeEnv, functionType, returnValue,
                                             storedLayout, layoutIndex, nextValue, blocks);
  } else if (tree.node(returnValue).kind == ast::SyntaxKind::PostfixExpression &&
             static_cast<ast::PostfixOperatorKind>(
                 tree.node(returnValue).payload.words[ast::kPostfixExpressionOpWord]) ==
                 ast::PostfixOperatorKind::ErrorUnwrap) {
    loweringFailure =
        lowerForcedUnwrapReturn(module, tree, typeEnv, functionType, returnValue, storedLayout,
                                layoutIndex, nextValue, blocks, sourceContext);
  } else {
    return failure(LoweringFailureKind::UnsupportedExpression, LoweringPhase::Expression,
                   returnValue);
  }
  ZC_IF_SOME(foundFailure, loweringFailure) { return zc::mv(foundFailure); }

  const auto name =
      tree.ident(ast::IdentId(functionNode.payload.words[ast::kFunctionDeclNameWord]));
  module.addFunction(Function(zc::str(name), functionSymbol, checkedSignature, abiReturnType,
                              layoutIndex, zc::mv(blocks)));
  return zc::none;
}

}  // namespace

LoweringResult lowerCheckedTree(const ast::Tree& tree, const ast::BindingMetadata& metadata,
                                const type::TypeEnv& typeEnv, TargetDataLayout target,
                                zc::Maybe<const LoweringSourceContext&> sourceContext) {
  if (!typeEnv.isDispatchFrozen()) {
    return failure(LoweringFailureKind::DispatchNotFrozen, LoweringPhase::CheckedInput,
                   tree.root());
  }
  if (!metadata.isSizedFor(tree)) {
    return failure(LoweringFailureKind::InvalidBindingMetadata, LoweringPhase::CheckedInput,
                   tree.root());
  }
  Module module(typeEnv.getSemanticTypeStore(), target);
  const auto root = tree.root();
  if (!root || !tree.contains(root) || tree.node(root).kind != ast::SyntaxKind::SourceFile) {
    return failure(LoweringFailureKind::InvalidSourceRoot, LoweringPhase::CheckedInput, root);
  }

  const auto& sourceFile = tree.node(root);
  ast::NodeList statements;
  statements.first = sourceFile.payload.words[ast::kSourceFileStatementsFirstWord];
  statements.size = sourceFile.payload.words[ast::kSourceFileStatementsSizeWord];
  if (!tree.contains(statements)) {
    return failure(LoweringFailureKind::InvalidStatementList, LoweringPhase::CheckedInput, root);
  }

  size_t loweredFunctions = 0;
  for (const auto wrapperId : tree.list(statements)) {
    const auto item = unwrapStatement(tree, wrapperId);
    if (!item) {
      return failure(LoweringFailureKind::InvalidStatementList, LoweringPhase::CheckedInput,
                     wrapperId);
    }
    if (tree.node(item).kind != ast::SyntaxKind::FunctionDecl ||
        tree.node(item).payload.words[ast::kFunctionDeclRaisesTyWord] == 0) {
      continue;
    }
    ZC_IF_SOME(functionError, lowerFunction(module, tree, metadata, typeEnv, item, sourceContext)) {
      return zc::mv(functionError);
    }
    ++loweredFunctions;
  }

  if (loweredFunctions == 0) {
    return failure(LoweringFailureKind::UnsupportedSourceShape, LoweringPhase::CheckedInput, root);
  }
  if (!functionSymbolsAreUnique(module)) {
    return failure(LoweringFailureKind::DuplicateFunctionSymbol, LoweringPhase::ModuleVerification,
                   root);
  }
  if (!allCallTargetsResolve(module)) {
    return failure(LoweringFailureKind::UnsupportedCrossSourceTarget,
                   LoweringPhase::ModuleVerification, root);
  }
  return zc::mv(module);
}

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
