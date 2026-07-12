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

#include "zomlang/compiler/irgen/ir-dump.h"

namespace zomlang {
namespace compiler {
namespace irgen {

namespace {

zc::StringPtr layoutKindName(ErrorUnionLayoutKind kind) {
  switch (kind) {
    case ErrorUnionLayoutKind::DirectSuccess:
      return "direct_success"_zc;
    case ErrorUnionLayoutKind::TaggedUnion:
      return "tagged_union"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr tagTypeName(ErrorUnionTagType type) {
  switch (type) {
    case ErrorUnionTagType::U8:
      return "u8"_zc;
    case ErrorUnionTagType::U16:
      return "u16"_zc;
    case ErrorUnionTagType::U32:
      return "u32"_zc;
    case ErrorUnionTagType::U64:
      return "u64"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr alternativeKindName(ErrorUnionAlternativeKind kind) {
  switch (kind) {
    case ErrorUnionAlternativeKind::Success:
      return "success"_zc;
    case ErrorUnionAlternativeKind::Error:
      return "error"_zc;
  }
  ZC_UNREACHABLE;
}

zc::StringPtr payloadStateName(ErrorUnionPayloadLayoutState state) {
  switch (state) {
    case ErrorUnionPayloadLayoutState::Known:
      return "known"_zc;
    case ErrorUnionPayloadLayoutState::Unknown:
      return "unknown"_zc;
  }
  ZC_UNREACHABLE;
}

void writeValue(zc::OutputStream& output, ValueId value) {
  output.write(zc::str("%", static_cast<uint64_t>(value.value)).asBytes());
}

zc::StringPtr resolveFunctionName(const Module& module, identity::DefId definition) {
  for (const auto& function : module.getFunctions()) {
    if (function.definition == definition) { return function.name; }
  }
  ZC_UNREACHABLE;
}

IrDumpFailure dumpFailure(IrDumpFailureKind kind, IrDumpVerifierSite site,
                          identity::DefId definition = {}, BlockId block = BlockId(),
                          ValueId value = ValueId(),
                          type::SemanticTypeId type = type::SemanticTypeId(), uint32_t index = 0) {
  return IrDumpFailure{kind, site, definition, block, value, type, index};
}

bool isPowerOfTwo(uint64_t value) { return value != 0 && (value & (value - 1)) == 0; }

bool isValidLayoutKind(ErrorUnionLayoutKind kind) {
  switch (kind) {
    case ErrorUnionLayoutKind::DirectSuccess:
    case ErrorUnionLayoutKind::TaggedUnion:
      return true;
  }
  return false;
}

bool isValidTagType(ErrorUnionTagType tagType) {
  switch (tagType) {
    case ErrorUnionTagType::U8:
    case ErrorUnionTagType::U16:
    case ErrorUnionTagType::U32:
    case ErrorUnionTagType::U64:
      return true;
  }
  return false;
}

bool hasFunctionDefinition(const Module& module, identity::DefId definition) {
  for (const auto& function : module.getFunctions()) {
    if (function.definition == definition) { return true; }
  }
  return false;
}

zc::Maybe<const ErrorUnionAlternativeLayout&> findAlternative(const ErrorUnionLayout& layout,
                                                              uint64_t tag) {
  for (const auto& alternative : layout.alternatives) {
    if (alternative.tag == tag) { return alternative; }
  }
  return zc::none;
}

zc::Maybe<const BasicBlock&> findBlock(const Function& function, BlockId id) {
  for (const auto& block : function.blocks) {
    if (block.id == id) { return block; }
  }
  return zc::none;
}

type::SemanticTypeId instructionResultType(const Instruction& instruction) {
  if (instruction.data.is<IntegerConstant>()) {
    return instruction.data.get<IntegerConstant>().resultType;
  }
  if (instruction.data.is<RaisingCall>()) { return instruction.data.get<RaisingCall>().resultType; }
  if (instruction.data.is<ErrorUnionConstruct>()) {
    return instruction.data.get<ErrorUnionConstruct>().resultType;
  }
  if (instruction.data.is<ErrorUnionMovePayload>()) {
    return instruction.data.get<ErrorUnionMovePayload>().resultType;
  }
  ZC_UNREACHABLE;
}

struct ValueDefinition final {
  ValueId value;
  type::SemanticTypeId type;
};

zc::Maybe<type::SemanticTypeId> findValueType(const zc::Vector<ValueDefinition>& definitions,
                                              ValueId value) {
  for (const auto& definition : definitions) {
    if (definition.value == value) { return definition.type; }
  }
  return zc::none;
}

bool hasBlockId(const zc::Vector<BlockId>& blocks, BlockId block) {
  for (const auto candidate : blocks) {
    if (candidate == block) { return true; }
  }
  return false;
}

IrDumpResult verifyLayout(const Module& module, const ErrorUnionLayout& layout,
                          uint32_t layoutIndex) {
  const auto& semanticTypes = module.getSemanticTypeStore();
  if (!semanticTypes.contains(layout.semanticTypeId)) {
    return dumpFailure(IrDumpFailureKind::InvalidTypeReference, IrDumpVerifierSite::Layout,
                       identity::DefId(), BlockId(), ValueId(), layout.semanticTypeId, layoutIndex);
  }
  if (!isValidLayoutKind(layout.kind) || !isValidTagType(layout.tagType) ||
      layout.alternatives.empty() || !isPowerOfTwo(layout.payloadAlign) ||
      !isPowerOfTwo(layout.align) ||
      layout.payloadLayoutState != ErrorUnionPayloadLayoutState::Known) {
    return dumpFailure(IrDumpFailureKind::InvalidLayout, IrDumpVerifierSite::Layout,
                       identity::DefId(), BlockId(), ValueId(), layout.semanticTypeId, layoutIndex);
  }
  if (layout.kind == ErrorUnionLayoutKind::DirectSuccess && layout.alternatives.size() != 1) {
    return dumpFailure(IrDumpFailureKind::InvalidLayout, IrDumpVerifierSite::Layout,
                       identity::DefId(), BlockId(), ValueId(), layout.semanticTypeId, layoutIndex);
  }
  if (layout.kind == ErrorUnionLayoutKind::TaggedUnion && layout.alternatives.size() < 2) {
    return dumpFailure(IrDumpFailureKind::InvalidLayout, IrDumpVerifierSite::Layout,
                       identity::DefId(), BlockId(), ValueId(), layout.semanticTypeId, layoutIndex);
  }

  for (size_t i = 0; i < layout.alternatives.size(); ++i) {
    const auto& alternative = layout.alternatives[i];
    const auto expectedKind =
        i == 0 ? ErrorUnionAlternativeKind::Success : ErrorUnionAlternativeKind::Error;
    if (!semanticTypes.contains(alternative.semanticTypeId)) {
      return dumpFailure(IrDumpFailureKind::InvalidTypeReference, IrDumpVerifierSite::Layout,
                         identity::DefId(), BlockId(), ValueId(), alternative.semanticTypeId,
                         layoutIndex);
    }
    if (alternative.tag != i || alternative.kind != expectedKind ||
        !isPowerOfTwo(alternative.payloadAlign) ||
        alternative.payloadLayoutState != ErrorUnionPayloadLayoutState::Known) {
      return dumpFailure(IrDumpFailureKind::InvalidLayout, IrDumpVerifierSite::Layout,
                         identity::DefId(), BlockId(), ValueId(), alternative.semanticTypeId,
                         layoutIndex);
    }
    for (size_t j = i + 1; j < layout.alternatives.size(); ++j) {
      if (alternative.semanticTypeId == layout.alternatives[j].semanticTypeId) {
        return dumpFailure(IrDumpFailureKind::InvalidLayout, IrDumpVerifierSite::Layout,
                           identity::DefId(), BlockId(), ValueId(), alternative.semanticTypeId,
                           layoutIndex);
      }
    }
  }

  if (layout.kind == ErrorUnionLayoutKind::DirectSuccess &&
      layout.semanticTypeId != layout.alternatives[0].semanticTypeId) {
    return dumpFailure(IrDumpFailureKind::InvalidLayout, IrDumpVerifierSite::Layout,
                       identity::DefId(), BlockId(), ValueId(), layout.semanticTypeId, layoutIndex);
  }
  return zc::none;
}

IrDumpResult verifyInstruction(const Module& module, const Function& function,
                               const BasicBlock& block, const Instruction& instruction,
                               const zc::Vector<ValueDefinition>& definitions) {
  const auto layouts = module.getErrorUnionLayouts();
  if (instruction.data.is<IntegerConstant>()) {
    if (instruction.data.get<IntegerConstant>().value.size() == 0) {
      return dumpFailure(IrDumpFailureKind::InvalidInstruction, IrDumpVerifierSite::Instruction,
                         function.definition, block.id, instruction.result);
    }
    return zc::none;
  }

  if (instruction.data.is<RaisingCall>()) {
    const auto target = instruction.data.get<RaisingCall>().target;
    if (!target.isValid() || !hasFunctionDefinition(module, target)) {
      return dumpFailure(IrDumpFailureKind::UnresolvedCallTarget, IrDumpVerifierSite::Instruction,
                         target, block.id, instruction.result);
    }
    return zc::none;
  }

  if (instruction.data.is<ErrorUnionConstruct>()) {
    const auto& construct = instruction.data.get<ErrorUnionConstruct>();
    if (construct.layoutIndex >= layouts.size()) {
      return dumpFailure(IrDumpFailureKind::InvalidLayoutReference, IrDumpVerifierSite::Instruction,
                         function.definition, block.id, instruction.result, construct.resultType,
                         construct.layoutIndex);
    }
    const auto& layout = layouts[construct.layoutIndex];
    const auto alternative = findAlternative(layout, construct.tag);
    const auto payloadType = findValueType(definitions, construct.payload);
    if (alternative == zc::none || payloadType == zc::none ||
        construct.resultType != layout.semanticTypeId ||
        layout.kind != ErrorUnionLayoutKind::TaggedUnion) {
      return dumpFailure(IrDumpFailureKind::InvalidInstruction, IrDumpVerifierSite::Instruction,
                         function.definition, block.id, construct.payload, construct.resultType,
                         construct.layoutIndex);
    }
    bool matches = false;
    ZC_IF_SOME(foundAlternative, alternative) {
      ZC_IF_SOME(foundPayloadType, payloadType) {
        matches = foundAlternative.semanticTypeId == foundPayloadType;
      }
    }
    if (!matches) {
      return dumpFailure(IrDumpFailureKind::InvalidInstruction, IrDumpVerifierSite::Instruction,
                         function.definition, block.id, construct.payload, construct.resultType,
                         construct.layoutIndex);
    }
    return zc::none;
  }

  if (instruction.data.is<ErrorUnionMovePayload>()) {
    const auto& move = instruction.data.get<ErrorUnionMovePayload>();
    if (move.layoutIndex >= layouts.size()) {
      return dumpFailure(IrDumpFailureKind::InvalidLayoutReference, IrDumpVerifierSite::Instruction,
                         function.definition, block.id, instruction.result, move.resultType,
                         move.layoutIndex);
    }
    const auto& layout = layouts[move.layoutIndex];
    const auto alternative = findAlternative(layout, move.tag);
    const auto sourceType = findValueType(definitions, move.source);
    if (alternative == zc::none || sourceType == zc::none ||
        layout.kind != ErrorUnionLayoutKind::TaggedUnion) {
      return dumpFailure(IrDumpFailureKind::InvalidValueReference, IrDumpVerifierSite::Instruction,
                         function.definition, block.id, move.source, move.resultType,
                         move.layoutIndex);
    }
    bool matches = false;
    ZC_IF_SOME(foundAlternative, alternative) {
      ZC_IF_SOME(foundSourceType, sourceType) {
        matches = foundSourceType == layout.semanticTypeId &&
                  move.resultType == foundAlternative.semanticTypeId;
      }
    }
    if (!matches) {
      return dumpFailure(IrDumpFailureKind::InvalidInstruction, IrDumpVerifierSite::Instruction,
                         function.definition, block.id, move.source, move.resultType,
                         move.layoutIndex);
    }
    return zc::none;
  }

  ZC_UNREACHABLE;
}

IrDumpResult verifyTerminator(const Module& module, const Function& function,
                              const BasicBlock& block,
                              const zc::Vector<ValueDefinition>& definitions) {
  const auto layouts = module.getErrorUnionLayouts();
  if (block.terminator.is<ReturnTerminator>()) {
    const auto& terminator = block.terminator.get<ReturnTerminator>();
    const auto valueType = findValueType(definitions, terminator.value);
    bool matches = false;
    ZC_IF_SOME(foundType, valueType) {
      matches = foundType == terminator.valueType && foundType == function.abiReturnType;
    }
    if (!matches) {
      return dumpFailure(IrDumpFailureKind::InvalidTerminator, IrDumpVerifierSite::Terminator,
                         function.definition, block.id, terminator.value, terminator.valueType);
    }
    return zc::none;
  }

  if (block.terminator.is<JumpTerminator>()) {
    const auto& terminator = block.terminator.get<JumpTerminator>();
    const auto target = findBlock(function, terminator.target);
    const auto argumentType = findValueType(definitions, terminator.argument);
    if (target == zc::none || argumentType == zc::none) {
      return dumpFailure(IrDumpFailureKind::InvalidBlockReference, IrDumpVerifierSite::Terminator,
                         function.definition, terminator.target, terminator.argument);
    }
    bool matches = false;
    ZC_IF_SOME(targetBlock, target) {
      ZC_IF_SOME(parameter, targetBlock.parameter) {
        ZC_IF_SOME(foundArgumentType, argumentType) {
          matches = parameter.type == foundArgumentType;
        }
      }
    }
    if (!matches) {
      return dumpFailure(IrDumpFailureKind::InvalidTerminator, IrDumpVerifierSite::Terminator,
                         function.definition, block.id, terminator.argument);
    }
    return zc::none;
  }

  if (block.terminator.is<ErrorUnionBranchTerminator>()) {
    const auto& terminator = block.terminator.get<ErrorUnionBranchTerminator>();
    if (terminator.layoutIndex >= layouts.size() ||
        findBlock(function, terminator.successTarget) == zc::none ||
        findBlock(function, terminator.errorTarget) == zc::none ||
        terminator.successTarget == terminator.errorTarget) {
      return dumpFailure(IrDumpFailureKind::InvalidBlockReference, IrDumpVerifierSite::Terminator,
                         function.definition, block.id, terminator.value, type::SemanticTypeId(),
                         terminator.layoutIndex);
    }
    if (layouts[terminator.layoutIndex].kind != ErrorUnionLayoutKind::TaggedUnion) {
      return dumpFailure(IrDumpFailureKind::InvalidLayoutReference, IrDumpVerifierSite::Terminator,
                         function.definition, block.id, terminator.value, type::SemanticTypeId(),
                         terminator.layoutIndex);
    }
    const auto valueType = findValueType(definitions, terminator.value);
    bool matches = false;
    ZC_IF_SOME(foundType, valueType) {
      matches = foundType == layouts[terminator.layoutIndex].semanticTypeId;
    }
    if (!matches) {
      return dumpFailure(IrDumpFailureKind::InvalidTerminator, IrDumpVerifierSite::Terminator,
                         function.definition, block.id, terminator.value, type::SemanticTypeId(),
                         terminator.layoutIndex);
    }
    return zc::none;
  }

  if (block.terminator.is<ForcedUnwrapPanicTerminator>()) {
    const auto& terminator = block.terminator.get<ForcedUnwrapPanicTerminator>();
    if (terminator.layoutIndex >= layouts.size()) {
      return dumpFailure(IrDumpFailureKind::InvalidLayoutReference, IrDumpVerifierSite::Terminator,
                         function.definition, block.id, terminator.value,
                         terminator.metadata.payloadType, terminator.layoutIndex);
    }
    const auto& layout = layouts[terminator.layoutIndex];
    const auto alternative = findAlternative(layout, terminator.tag);
    const auto valueType = findValueType(definitions, terminator.value);
    bool matches = false;
    ZC_IF_SOME(foundAlternative, alternative) {
      ZC_IF_SOME(foundValueType, valueType) {
        matches = foundAlternative.kind == ErrorUnionAlternativeKind::Error &&
                  foundAlternative.semanticTypeId == terminator.metadata.payloadType &&
                  foundValueType == layout.semanticTypeId;
      }
    }
    if (!matches || layout.kind != ErrorUnionLayoutKind::TaggedUnion ||
        terminator.metadata.file.size() == 0 || terminator.metadata.line == 0 ||
        terminator.metadata.column == 0 ||
        terminator.metadata.byteStart >= terminator.metadata.byteEnd) {
      return dumpFailure(IrDumpFailureKind::InvalidTerminator, IrDumpVerifierSite::Terminator,
                         function.definition, block.id, terminator.value,
                         terminator.metadata.payloadType, terminator.layoutIndex);
    }
    return zc::none;
  }

  ZC_UNREACHABLE;
}

IrDumpResult verifyFunction(const Module& module, const Function& function) {
  const auto& semanticTypes = module.getSemanticTypeStore();
  const auto layouts = module.getErrorUnionLayouts();
  if (!function.definition.isValid() || function.name.size() == 0 || function.blocks.empty()) {
    return dumpFailure(IrDumpFailureKind::InvalidFunction, IrDumpVerifierSite::Function,
                       function.definition);
  }
  if (!semanticTypes.contains(function.checkedSignature)) {
    return dumpFailure(IrDumpFailureKind::InvalidTypeReference, IrDumpVerifierSite::Function,
                       function.definition, BlockId(), ValueId(), function.checkedSignature);
  }
  if (!semanticTypes.contains(function.abiReturnType)) {
    return dumpFailure(IrDumpFailureKind::InvalidTypeReference, IrDumpVerifierSite::Function,
                       function.definition, BlockId(), ValueId(), function.abiReturnType);
  }
  if (function.errorUnionLayout >= layouts.size()) {
    return dumpFailure(IrDumpFailureKind::InvalidLayoutReference, IrDumpVerifierSite::Function,
                       function.definition, BlockId(), ValueId(), type::SemanticTypeId(),
                       function.errorUnionLayout);
  }

  const auto& functionLayout = layouts[function.errorUnionLayout];
  const auto expectedReturnType = functionLayout.kind == ErrorUnionLayoutKind::DirectSuccess
                                      ? functionLayout.alternatives[0].semanticTypeId
                                      : functionLayout.semanticTypeId;
  if (function.abiReturnType != expectedReturnType) {
    return dumpFailure(IrDumpFailureKind::InvalidFunction, IrDumpVerifierSite::Function,
                       function.definition, BlockId(), ValueId(), function.abiReturnType,
                       function.errorUnionLayout);
  }

  zc::Vector<BlockId> blockIds;
  zc::Vector<ValueDefinition> definitions;
  for (const auto& block : function.blocks) {
    if (!block.id.isValid()) {
      return dumpFailure(IrDumpFailureKind::InvalidBlockReference, IrDumpVerifierSite::Block,
                         function.definition, block.id);
    }
    if (hasBlockId(blockIds, block.id)) {
      return dumpFailure(IrDumpFailureKind::DuplicateBlock, IrDumpVerifierSite::Block,
                         function.definition, block.id);
    }
    blockIds.add(block.id);

    ZC_IF_SOME(parameter, block.parameter) {
      if (!parameter.value.isValid() || !semanticTypes.contains(parameter.type)) {
        return dumpFailure(IrDumpFailureKind::InvalidTypeReference, IrDumpVerifierSite::Block,
                           function.definition, block.id, parameter.value, parameter.type);
      }
      if (findValueType(definitions, parameter.value) != zc::none) {
        return dumpFailure(IrDumpFailureKind::DuplicateValue, IrDumpVerifierSite::Block,
                           function.definition, block.id, parameter.value, parameter.type);
      }
      definitions.add(ValueDefinition{parameter.value, parameter.type});
    }

    for (const auto& instruction : block.instructions) {
      const auto resultType = instructionResultType(instruction);
      if (!instruction.result.isValid() || !semanticTypes.contains(resultType)) {
        return dumpFailure(IrDumpFailureKind::InvalidTypeReference, IrDumpVerifierSite::Instruction,
                           function.definition, block.id, instruction.result, resultType);
      }
      if (findValueType(definitions, instruction.result) != zc::none) {
        return dumpFailure(IrDumpFailureKind::DuplicateValue, IrDumpVerifierSite::Instruction,
                           function.definition, block.id, instruction.result, resultType);
      }
      definitions.add(ValueDefinition{instruction.result, resultType});
    }
  }

  for (const auto& block : function.blocks) {
    for (const auto& instruction : block.instructions) {
      ZC_IF_SOME(failure, verifyInstruction(module, function, block, instruction, definitions)) {
        return failure;
      }
    }
    ZC_IF_SOME(failure, verifyTerminator(module, function, block, definitions)) { return failure; }
  }
  return zc::none;
}

IrDumpResult verifyDumpableModule(const Module& module) {
  const auto layouts = module.getErrorUnionLayouts();
  if (layouts.size() == 0) {
    return dumpFailure(IrDumpFailureKind::InvalidLayout, IrDumpVerifierSite::Module);
  }
  for (size_t i = 0; i < layouts.size(); ++i) {
    ZC_IF_SOME(failure, verifyLayout(module, layouts[i], static_cast<uint32_t>(i))) {
      return failure;
    }
  }

  const auto functions = module.getFunctions();
  if (functions.size() == 0) {
    return dumpFailure(IrDumpFailureKind::InvalidFunction, IrDumpVerifierSite::Module);
  }
  for (size_t i = 0; i < functions.size(); ++i) {
    for (size_t j = i + 1; j < functions.size(); ++j) {
      if (functions[i].definition == functions[j].definition) {
        return dumpFailure(IrDumpFailureKind::DuplicateFunctionSymbol, IrDumpVerifierSite::Module,
                           functions[i].definition);
      }
    }
    ZC_IF_SOME(failure, verifyFunction(module, functions[i])) { return failure; }
  }
  return zc::none;
}

void dumpInstruction(zc::OutputStream& output, const Module& module,
                     const Instruction& instruction) {
  const auto& semanticTypes = module.getSemanticTypeStore();
  writeValue(output, instruction.result);
  if (instruction.data.is<IntegerConstant>()) {
    const auto& constant = instruction.data.get<IntegerConstant>();
    output.write(zc::str(" = integer.constant ", constant.value,
                         " type=", semanticTypes.getCanonicalKey(constant.resultType), "\n")
                     .asBytes());
    return;
  }
  if (instruction.data.is<RaisingCall>()) {
    const auto& call = instruction.data.get<RaisingCall>();
    output.write(zc::str(" = call.raising @", resolveFunctionName(module, call.target),
                         " type=", semanticTypes.getCanonicalKey(call.resultType), "\n")
                     .asBytes());
    return;
  }
  if (instruction.data.is<ErrorUnionConstruct>()) {
    const auto& construct = instruction.data.get<ErrorUnionConstruct>();
    output.write(" = error_union.construct "_zcb);
    writeValue(output, construct.payload);
    output.write(zc::str(" layout=", static_cast<uint64_t>(construct.layoutIndex),
                         " tag=", construct.tag,
                         " type=", semanticTypes.getCanonicalKey(construct.resultType), "\n")
                     .asBytes());
    return;
  }
  if (instruction.data.is<ErrorUnionMovePayload>()) {
    const auto& move = instruction.data.get<ErrorUnionMovePayload>();
    output.write(" = error_union.move_payload "_zcb);
    writeValue(output, move.source);
    output.write(zc::str(" layout=", static_cast<uint64_t>(move.layoutIndex), " tag=", move.tag,
                         " type=", semanticTypes.getCanonicalKey(move.resultType), "\n")
                     .asBytes());
    return;
  }
  ZC_UNREACHABLE;
}

void dumpTerminator(zc::OutputStream& output, const Module& module, const Terminator& terminator) {
  if (terminator.is<ReturnTerminator>()) {
    const auto& returnValue = terminator.get<ReturnTerminator>();
    output.write("return "_zcb);
    writeValue(output, returnValue.value);
    output.write(zc::str(" type=",
                         module.getSemanticTypeStore().getCanonicalKey(returnValue.valueType), "\n")
                     .asBytes());
    return;
  }
  if (terminator.is<JumpTerminator>()) {
    const auto& jump = terminator.get<JumpTerminator>();
    output.write(zc::str("jump ^bb", static_cast<uint64_t>(jump.target.value), "(").asBytes());
    writeValue(output, jump.argument);
    output.write(")\n"_zcb);
    return;
  }
  if (terminator.is<ErrorUnionBranchTerminator>()) {
    const auto& branch = terminator.get<ErrorUnionBranchTerminator>();
    output.write("error_union.branch "_zcb);
    writeValue(output, branch.value);
    output.write(zc::str(" layout=", static_cast<uint64_t>(branch.layoutIndex), " success=^bb",
                         static_cast<uint64_t>(branch.successTarget.value), " error=^bb",
                         static_cast<uint64_t>(branch.errorTarget.value), "\n")
                     .asBytes());
    return;
  }
  if (terminator.is<ForcedUnwrapPanicTerminator>()) {
    const auto& panic = terminator.get<ForcedUnwrapPanicTerminator>();
    output.write("panic.forced_unwrap "_zcb);
    writeValue(output, panic.value);
    output.write(zc::str(" layout=", static_cast<uint64_t>(panic.layoutIndex), " tag=", panic.tag,
                         " payload_type=",
                         module.getSemanticTypeStore().getCanonicalKey(panic.metadata.payloadType),
                         " file=\"", panic.metadata.file, "\" line=", panic.metadata.line,
                         " column=", panic.metadata.column, " byte_start=",
                         panic.metadata.byteStart, " byte_end=", panic.metadata.byteEnd, "\n")
                     .asBytes());
    return;
  }
  ZC_UNREACHABLE;
}

}  // namespace

IrDumpResult dumpModule(zc::OutputStream& output, const Module& module) {
  ZC_IF_SOME(failure, verifyDumpableModule(module)) { return failure; }

  output.write("zom.ir.v0\n"_zcb);
  output.write(zc::str("target pointer_size=", module.getTarget().getPointerSize(),
                       " pointer_align=", module.getTarget().getPointerAlignment(), "\n")
                   .asBytes());

  const auto layouts = module.getErrorUnionLayouts();
  for (size_t i = 0; i < layouts.size(); ++i) {
    const auto& layout = layouts[i];
    output.write(
        zc::str("layout ", static_cast<uint64_t>(i),
                " type=", module.getSemanticTypeStore().getCanonicalKey(layout.semanticTypeId),
                " kind=", layoutKindName(layout.kind), " tag=", tagTypeName(layout.tagType),
                " tag_offset=", layout.tagOffset,
                " payload_state=", payloadStateName(layout.payloadLayoutState),
                " payload_offset=", layout.payloadOffset, " payload_size=", layout.payloadSize,
                " payload_align=", layout.payloadAlign, " size=", layout.size,
                " align=", layout.align, "\n")
            .asBytes());
    for (const auto& alternative : layout.alternatives) {
      output.write(
          zc::str("  alternative tag=", alternative.tag,
                  " kind=", alternativeKindName(alternative.kind), " type=",
                  module.getSemanticTypeStore().getCanonicalKey(alternative.semanticTypeId),
                  " payload_state=", payloadStateName(alternative.payloadLayoutState),
                  " payload_size=", alternative.payloadSize,
                  " payload_align=", alternative.payloadAlign, "\n")
              .asBytes());
    }
  }

  for (const auto& function : module.getFunctions()) {
    output.write(
        zc::str(
            "function @", function.name,
            " signature=", module.getSemanticTypeStore().getCanonicalKey(function.checkedSignature),
            " abi_return=", module.getSemanticTypeStore().getCanonicalKey(function.abiReturnType),
            " layout=", static_cast<uint64_t>(function.errorUnionLayout), "\n")
            .asBytes());
    for (const auto& block : function.blocks) {
      output.write(zc::str("  ^bb", static_cast<uint64_t>(block.id.value)).asBytes());
      ZC_IF_SOME(parameter, block.parameter) {
        output.write("("_zcb);
        writeValue(output, parameter.value);
        output.write(
            zc::str(":", module.getSemanticTypeStore().getCanonicalKey(parameter.type), ")")
                .asBytes());
      }
      output.write(":\n"_zcb);
      for (const auto& instruction : block.instructions) {
        output.write("    "_zcb);
        dumpInstruction(output, module, instruction);
      }
      output.write("    "_zcb);
      dumpTerminator(output, module, block.terminator);
    }
  }
  return zc::none;
}

}  // namespace irgen
}  // namespace compiler
}  // namespace zomlang
