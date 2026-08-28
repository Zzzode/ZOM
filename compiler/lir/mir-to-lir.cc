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

#include "compiler/lir/mir-to-lir.h"

#include "compiler/checker/facts/signature-facts.h"
#include "compiler/mir/built-mir.h"
#include "compiler/type/semantic-type-data.h"
#include "compiler/type/semantic-type-store.h"

namespace zomlang::compiler::lir {
namespace {

/// \brief Maps a primitive kind to its closed LIR integer carrier width.
///
/// Only the fixed-width signed and unsigned integer primitives lower in this
/// slice. Pointer-width `isize`/`usize`, floats, bool, char, and every
/// non-integer primitive are outside the scalar-integer initializer slice.
zc::Maybe<IntegerBitWidth> integerWidthFor(type::semantic::PrimitiveKind kind) noexcept {
  switch (kind) {
    case type::semantic::PrimitiveKind::I8:
    case type::semantic::PrimitiveKind::U8:
      return IntegerBitWidth::Bit8;
    case type::semantic::PrimitiveKind::I16:
    case type::semantic::PrimitiveKind::U16:
      return IntegerBitWidth::Bit16;
    case type::semantic::PrimitiveKind::I32:
    case type::semantic::PrimitiveKind::U32:
      return IntegerBitWidth::Bit32;
    case type::semantic::PrimitiveKind::I64:
    case type::semantic::PrimitiveKind::U64:
      return IntegerBitWidth::Bit64;
    default:
      return zc::none;
  }
}

/// \brief Returns the carrier bit count for a closed integer width.
uint32_t bitCountFor(IntegerBitWidth width) noexcept { return static_cast<uint32_t>(width); }

/// \brief Materializes the zero-extended value of a non-negative canonical integer.
///
/// The canonical magnitude is big-endian (most significant byte first). The
/// value is rejected when it is negative, when its magnitude exceeds the carrier
/// width, or when a set bit falls outside the carrier's bit count.
zc::Maybe<uint64_t> zeroExtendedBits(const checker::signature::CanonicalInteger& integer,
                                     IntegerBitWidth width) noexcept {
  if (integer.sign != checker::signature::IntegerSign::NonNegative) { return zc::none; }
  const auto magnitude = integer.magnitude.asPtr();
  if (magnitude.size() > sizeof(uint64_t)) { return zc::none; }
  uint64_t bits = 0;
  for (const auto byte : magnitude) { bits = (bits << 8) | static_cast<uint64_t>(byte); }
  const uint32_t bitCount = bitCountFor(width);
  if (bitCount < 64) {
    const uint64_t limit = (static_cast<uint64_t>(1) << bitCount);
    if (bits >= limit) { return zc::none; }
  }
  return bits;
}

/// \brief Resolves the integer carrier for a semantic type owned by the store.
zc::Maybe<LirValueType> integerCarrierFor(identity::SemanticTypeId type,
                                          const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    ZC_IF_SOME(width, integerWidthFor(primitive)) { return LirValueType::integer(width); }
  }
  return zc::none;
}

/// \brief Resolves the i1 carrier for a Bool semantic type owned by the store.
///
/// The boolean discriminant of the conditional lowers to a one-bit integer
/// carrier. A non-Bool type fails closed.
zc::Maybe<LirValueType> boolCarrierFor(identity::SemanticTypeId type,
                                       const type::SemanticTypeStore& semanticTypes) {
  auto lookup = semanticTypes.get(type);
  if (!lookup.is<type::SemanticTypeLookup>()) { return zc::none; }
  const auto& data = lookup.get<type::SemanticTypeLookup>().data();
  ZC_IF_SOME(primitive, data.primitiveKind()) {
    if (primitive == type::semantic::PrimitiveKind::Bool) {
      return LirValueType::integer(IntegerBitWidth::Bit1);
    }
  }
  return zc::none;
}

/// \brief Maps a MIR relational operator to its LIR comparison operator.
LirComparisonOp lirComparisonOpFor(mir::MirComparisonOperator op) noexcept {
  switch (op) {
    case mir::MirComparisonOperator::Eq:
      return LirComparisonOp::Eq;
    case mir::MirComparisonOperator::Ne:
      return LirComparisonOp::Ne;
    case mir::MirComparisonOperator::Lt:
      return LirComparisonOp::Lt;
    case mir::MirComparisonOperator::Le:
      return LirComparisonOp::Le;
    case mir::MirComparisonOperator::Gt:
      return LirComparisonOp::Gt;
    case mir::MirComparisonOperator::Ge:
      return LirComparisonOp::Ge;
  }
  return LirComparisonOp::Eq;
}

/// \brief Lowers a MIR operand (integer constant or parameter place-use) to a
/// LIR operand of the given integer carrier.
/// \return The operand, or none for a non-integer constant or a projected place.
zc::Maybe<LirOperand> lirOperandFor(const mir::MirOperand& operand, LirValueType carrier) {
  if (operand.kind() == mir::MirOperandKind::Constant) {
    const auto integer = operand.constantValue().value.integerValue();
    if (integer == zc::none) { return zc::none; }
    auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), carrier.integerWidth());
    if (bits == zc::none) { return zc::none; }
    auto constant = LirIntegerConstant::from(carrier, ZC_REQUIRE_NONNULL(bits));
    if (constant == zc::none) { return zc::none; }
    return LirOperand::constant(ZC_REQUIRE_NONNULL(constant));
  }
  if (operand.place().projections().size() != 0) { return zc::none; }
  return LirOperand::localUse(operand.place().local().ordinal());
}

}  // namespace

zc::Maybe<LirModule> MirToLirLowering::lowerScalarInitializer(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified scalar module-initializer shape. This mirrors the
  // structural facts that mir::validScalarFunction already checked; we re-check
  // them here so lowering is total over its declared input and fail-closed on
  // anything else.
  if (function.kind != mir::MirFunctionKind::ModuleInitializer ||
      function.sourceScopes.size() != 1 || function.locals.size() != 1 ||
      function.blocks.size() != 1) {
    return zc::none;
  }

  const auto& local = function.locals[0];
  const auto& block = function.blocks[0];
  if (local.kind != mir::MirLocalKind::ModuleInitializerResult ||
      local.type != function.resultType || block.statements.size() != 2 ||
      block.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      block.statements[1].kind() != mir::MirStatementKind::Assign) {
    return zc::none;
  }

  const auto& assignment = block.statements[1].assignmentValue();
  if (assignment.destination.local() != local.id ||
      assignment.value.kind() != mir::MirRvalueKind::Use ||
      assignment.value.useValue().operand.kind() != mir::MirOperandKind::Constant) {
    return zc::none;
  }
  const auto& constant = assignment.value.useValue().operand.constantValue();
  if (constant.type != function.resultType) { return zc::none; }

  if (block.terminator.kind() != mir::MirTerminatorKind::Return) { return zc::none; }
  const auto& returnValue = block.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsResultLocal = false;
  ZC_IF_SOME(value, returnValue) {
    returnsResultLocal = value.kind() != mir::MirOperandKind::Constant &&
                         value.place().local() == local.id &&
                         value.place().projections().size() == 0;
  }
  if (!returnsResultLocal) { return zc::none; }

  // Resolve the integer carrier and constant value.
  auto carrier = integerCarrierFor(function.resultType, semanticTypes);
  if (carrier == zc::none) { return zc::none; }
  const auto carrierValue = ZC_REQUIRE_NONNULL(carrier);

  const auto integer = constant.value.integerValue();
  if (integer == zc::none) { return zc::none; }
  auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), carrierValue.integerWidth());
  if (bits == zc::none) { return zc::none; }

  auto lirConstant = LirIntegerConstant::from(carrierValue, ZC_REQUIRE_NONNULL(bits));
  if (lirConstant == zc::none) { return zc::none; }

  // Build the single entry block returning the constant.
  auto entryId = LirBlockId::fromOrdinal(1);
  if (entryId == zc::none) { return zc::none; }
  zc::Vector<LirBasicBlock> blocks;
  blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(entryId),
                           LirTerminator::returnInteger(ZC_REQUIRE_NONNULL(lirConstant))));

  // A module initializer has no user symbol name in this slice; use the stable
  // reserved ASCII runtime symbol for the module initializer entry.
  zc::Vector<LirFunction> functions;
  functions.add(LirFunction(zc::heapString("zom.module_init"), carrierValue, zc::mv(blocks)));
  return LirModule(zc::mv(functions));
}

zc::Maybe<LirModule> MirToLirLowering::lowerConditionalReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified four-block boolean-conditional return shape. This
  // re-checks the structure that mir::validConditionalReturnFunction validated
  // so lowering is total over its declared input and fail-closed on anything
  // else. This slice handles exactly one boolean parameter and one result local.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 4) {
    return zc::none;
  }

  const auto& parameter = function.locals[0];
  const auto& result = function.locals[1];
  if (parameter.kind != mir::MirLocalKind::Parameter ||
      result.kind != mir::MirLocalKind::FunctionResult || result.type != function.resultType) {
    return zc::none;
  }

  // The parameter is the boolean discriminant; the result carries the integer
  // return. Resolve both carriers, failing closed outside the supported set.
  auto parameterCarrier = boolCarrierFor(parameter.type, semanticTypes);
  if (parameterCarrier == zc::none) { return zc::none; }
  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);

  const uint32_t parameterOrdinal = parameter.id.ordinal();
  const uint32_t resultOrdinal = result.id.ordinal();

  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];

  // Entry: StorageLive(result); SwitchInt(bool param) -> then (true), else (false).
  if (entry.statements.size() != 1 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != result.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != parameter.id ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 2) {
    return zc::none;
  }
  // The true arm targets the then block; the false arm and default target the
  // else block, matching the verified diamond.
  const auto trueTargetOrdinal = switchInt.arms[0].target.ordinal();
  const auto falseTargetOrdinal = switchInt.arms[1].target.ordinal();
  if (trueTargetOrdinal != thenBlock.id.ordinal() || falseTargetOrdinal != elseBlock.id.ordinal() ||
      switchInt.defaultTarget != elseBlock.id) {
    return zc::none;
  }

  // Each arm assigns the result local then jumps to the join.
  auto lowerArm = [&](const mir::MirBasicBlock& branch, zc::Vector<LirStatement>& out) -> bool {
    if (branch.statements.size() != 1 ||
        branch.statements[0].kind() != mir::MirStatementKind::Assign ||
        branch.terminator.kind() != mir::MirTerminatorKind::Goto ||
        branch.terminator.gotoValue().target != joinBlock.id) {
      return false;
    }
    const auto& assignment = branch.statements[0].assignmentValue();
    if (assignment.destination.local() != result.id ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != mir::MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    if (operand.kind() == mir::MirOperandKind::Constant) {
      const auto& constant = operand.constantValue();
      if (constant.type != function.resultType) { return false; }
      const auto integer = constant.value.integerValue();
      if (integer == zc::none) { return false; }
      auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), resultCarrierValue.integerWidth());
      if (bits == zc::none) { return false; }
      auto lirConstant = LirIntegerConstant::from(resultCarrierValue, ZC_REQUIRE_NONNULL(bits));
      if (lirConstant == zc::none) { return false; }
      out.add(LirStatement::assign(resultOrdinal,
                                   LirOperand::constant(ZC_REQUIRE_NONNULL(lirConstant))));
      return true;
    }
    // A parameter place-use arm would reference the sole boolean parameter as an
    // integer result, a width mismatch this single-parameter slice cannot lower.
    // Constant arms only; fail closed on anything else.
    return false;
  };

  zc::Vector<LirStatement> thenStatements;
  zc::Vector<LirStatement> elseStatements;
  if (!lowerArm(thenBlock, thenStatements) || !lowerArm(elseBlock, elseStatements)) {
    return zc::none;
  }

  // Join: no statements; Return(place-use result).
  if (joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& returnValue = joinBlock.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, returnValue) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == result.id && value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  // Build the four LIR blocks.
  auto entryId = LirBlockId::fromOrdinal(1);
  auto thenId = LirBlockId::fromOrdinal(2);
  auto elseId = LirBlockId::fromOrdinal(3);
  auto joinId = LirBlockId::fromOrdinal(4);
  if (entryId == zc::none || thenId == zc::none || elseId == zc::none || joinId == zc::none) {
    return zc::none;
  }

  zc::Vector<LirBasicBlock> blocks;
  {
    zc::Vector<LirStatement> none;
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(none),
                             LirTerminator::condBranch(parameterOrdinal, ZC_REQUIRE_NONNULL(thenId),
                                                       ZC_REQUIRE_NONNULL(elseId))));
  }
  blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(thenId), zc::mv(thenStatements),
                           LirTerminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(elseId), zc::mv(elseStatements),
                           LirTerminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  {
    zc::Vector<LirStatement> none;
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(joinId), zc::mv(none),
                             LirTerminator::returnLocal(resultOrdinal)));
  }

  zc::Vector<LirLocal> parameters;
  parameters.add(LirLocal(parameterOrdinal, ZC_REQUIRE_NONNULL(parameterCarrier)));
  zc::Vector<LirLocal> locals;
  locals.add(LirLocal(resultOrdinal, resultCarrierValue));

  zc::Vector<LirFunction> functions;
  functions.add(LirFunction(zc::heapString("zom.conditional"), resultCarrierValue,
                            zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  return LirModule(zc::mv(functions));
}

zc::Maybe<LirModule> MirToLirLowering::lowerLoopReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified reducible four-block while-loop return shape,
  // re-checking the structure that mir::validLoopReturnFunction validated. This
  // slice handles exactly one boolean parameter and one result local.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.locals.size() != 2 || function.blocks.size() != 4) {
    return zc::none;
  }

  const auto& parameter = function.locals[0];
  const auto& result = function.locals[1];
  if (parameter.kind != mir::MirLocalKind::Parameter ||
      result.kind != mir::MirLocalKind::FunctionResult || result.type != function.resultType) {
    return zc::none;
  }

  auto parameterCarrier = boolCarrierFor(parameter.type, semanticTypes);
  if (parameterCarrier == zc::none) { return zc::none; }
  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);

  const uint32_t parameterOrdinal = parameter.id.ordinal();
  const uint32_t resultOrdinal = result.id.ordinal();

  const auto& entry = function.blocks[0];
  const auto& header = function.blocks[1];
  const auto& body = function.blocks[2];
  const auto& exit = function.blocks[3];

  // Entry: StorageLive(result); Goto(header).
  if (entry.statements.size() != 1 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != result.id ||
      entry.terminator.kind() != mir::MirTerminatorKind::Goto ||
      entry.terminator.gotoValue().target != header.id) {
    return zc::none;
  }

  // Header: no statements; SwitchInt(bool param) [true -> body], default = exit.
  if (header.statements.size() != 0 ||
      header.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  const auto& switchInt = header.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != parameter.id ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 1 ||
      switchInt.arms[0].target != body.id || switchInt.defaultTarget != exit.id) {
    return zc::none;
  }

  // Body: no statements; Goto(header) reducible back-edge.
  if (body.statements.size() != 0 || body.terminator.kind() != mir::MirTerminatorKind::Goto ||
      body.terminator.gotoValue().target != header.id) {
    return zc::none;
  }

  // Exit: Assign(result = integer literal); Return(place-use result).
  if (exit.statements.size() != 1 || exit.statements[0].kind() != mir::MirStatementKind::Assign ||
      exit.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& assignment = exit.statements[0].assignmentValue();
  if (assignment.destination.local() != result.id ||
      assignment.destination.projections().size() != 0 ||
      assignment.value.kind() != mir::MirRvalueKind::Use) {
    return zc::none;
  }
  const auto& operand = assignment.value.useValue().operand;
  if (operand.kind() != mir::MirOperandKind::Constant ||
      operand.constantValue().type != function.resultType) {
    return zc::none;
  }
  const auto integer = operand.constantValue().value.integerValue();
  if (integer == zc::none) { return zc::none; }
  auto bits = zeroExtendedBits(ZC_REQUIRE_NONNULL(integer), resultCarrierValue.integerWidth());
  if (bits == zc::none) { return zc::none; }
  auto exitConstant = LirIntegerConstant::from(resultCarrierValue, ZC_REQUIRE_NONNULL(bits));
  if (exitConstant == zc::none) { return zc::none; }

  const auto& returnValue = exit.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, returnValue) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == result.id && value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  auto entryId = LirBlockId::fromOrdinal(1);
  auto headerId = LirBlockId::fromOrdinal(2);
  auto bodyId = LirBlockId::fromOrdinal(3);
  auto exitId = LirBlockId::fromOrdinal(4);
  if (entryId == zc::none || headerId == zc::none || bodyId == zc::none || exitId == zc::none) {
    return zc::none;
  }

  zc::Vector<LirBasicBlock> blocks;
  {
    zc::Vector<LirStatement> none;
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(none),
                             LirTerminator::gotoBlock(ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<LirStatement> none;
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(headerId), zc::mv(none),
                             LirTerminator::condBranch(parameterOrdinal, ZC_REQUIRE_NONNULL(bodyId),
                                                       ZC_REQUIRE_NONNULL(exitId))));
  }
  {
    zc::Vector<LirStatement> none;
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(bodyId), zc::mv(none),
                             LirTerminator::gotoBlock(ZC_REQUIRE_NONNULL(headerId))));
  }
  {
    zc::Vector<LirStatement> exitStatements;
    exitStatements.add(LirStatement::assign(
        resultOrdinal, LirOperand::constant(ZC_REQUIRE_NONNULL(exitConstant))));
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(exitId), zc::mv(exitStatements),
                             LirTerminator::returnLocal(resultOrdinal)));
  }

  zc::Vector<LirLocal> parameters;
  parameters.add(LirLocal(parameterOrdinal, ZC_REQUIRE_NONNULL(parameterCarrier)));
  zc::Vector<LirLocal> locals;
  locals.add(LirLocal(resultOrdinal, resultCarrierValue));

  zc::Vector<LirFunction> functions;
  functions.add(LirFunction(zc::heapString("zom.loop"), resultCarrierValue, zc::mv(parameters),
                            zc::mv(locals), zc::mv(blocks)));
  return LirModule(zc::mv(functions));
}

zc::Maybe<LirModule> MirToLirLowering::lowerEqualityConditionalReturn(
    const mir::MirFunction& function, const type::SemanticTypeStore& semanticTypes) {
  // Admit only the verified comparison-driven conditional shape, re-checking the
  // structure that mir::validEqualityConditionalReturnFunction validated: N
  // integer parameters, a result local, and a bool temporary; a four-block
  // diamond that computes the temp from a Comparison and switches on it.
  if (function.kind != mir::MirFunctionKind::Function || function.sourceScopes.size() != 1 ||
      function.blocks.size() != 4 || function.locals.size() < 3) {
    return zc::none;
  }
  const size_t parameterCount = function.locals.size() - 2;
  const auto& resultLocalDecl = function.locals[parameterCount];
  const auto& tempLocalDecl = function.locals[parameterCount + 1];
  if (resultLocalDecl.kind != mir::MirLocalKind::FunctionResult ||
      resultLocalDecl.type != function.resultType ||
      tempLocalDecl.kind != mir::MirLocalKind::Temporary) {
    return zc::none;
  }
  for (size_t i = 0; i < parameterCount; ++i) {
    if (function.locals[i].kind != mir::MirLocalKind::Parameter) { return zc::none; }
  }

  auto resultCarrier = integerCarrierFor(function.resultType, semanticTypes);
  if (resultCarrier == zc::none) { return zc::none; }
  const auto resultCarrierValue = ZC_REQUIRE_NONNULL(resultCarrier);
  auto tempCarrier = boolCarrierFor(tempLocalDecl.type, semanticTypes);
  if (tempCarrier == zc::none) { return zc::none; }
  const auto tempCarrierValue = ZC_REQUIRE_NONNULL(tempCarrier);

  const uint32_t resultOrdinal = resultLocalDecl.id.ordinal();
  const uint32_t tempOrdinal = tempLocalDecl.id.ordinal();

  const auto& entry = function.blocks[0];
  const auto& thenBlock = function.blocks[1];
  const auto& elseBlock = function.blocks[2];
  const auto& joinBlock = function.blocks[3];

  // Entry: StorageLive(result), StorageLive(temp), Assign(temp = Comparison),
  // SwitchInt(copy temp) [true -> then], default = else.
  if (entry.statements.size() != 3 ||
      entry.statements[0].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[0].storageLocal() != resultLocalDecl.id ||
      entry.statements[1].kind() != mir::MirStatementKind::StorageLive ||
      entry.statements[1].storageLocal() != tempLocalDecl.id ||
      entry.statements[2].kind() != mir::MirStatementKind::Assign ||
      entry.terminator.kind() != mir::MirTerminatorKind::SwitchInt) {
    return zc::none;
  }
  const auto& tempAssign = entry.statements[2].assignmentValue();
  if (tempAssign.destination.local() != tempLocalDecl.id ||
      tempAssign.destination.projections().size() != 0 ||
      tempAssign.value.kind() != mir::MirRvalueKind::Comparison) {
    return zc::none;
  }
  const auto& comparison = tempAssign.value.comparisonValue();
  // The comparison operands must resolve to integer carriers. Both operands
  // share the operand type; use the result-less operand carrier from the
  // comparison operand types (each operand is a parameter place-use or constant
  // of the same integer operand type).
  auto operandCarrierFor = [&](const mir::MirOperand& operand) -> zc::Maybe<LirValueType> {
    if (operand.kind() == mir::MirOperandKind::Constant) {
      return integerCarrierFor(operand.constantValue().type, semanticTypes);
    }
    return integerCarrierFor(operand.place().rootType(), semanticTypes);
  };
  auto leftCarrier = operandCarrierFor(comparison.left);
  auto rightCarrier = operandCarrierFor(comparison.right);
  if (leftCarrier == zc::none || rightCarrier == zc::none) { return zc::none; }
  auto lirLeft = lirOperandFor(comparison.left, ZC_REQUIRE_NONNULL(leftCarrier));
  auto lirRight = lirOperandFor(comparison.right, ZC_REQUIRE_NONNULL(rightCarrier));
  if (lirLeft == zc::none || lirRight == zc::none) { return zc::none; }

  const auto& switchInt = entry.terminator.switchIntValue();
  if (switchInt.discriminant.kind() != mir::MirOperandKind::Copy ||
      switchInt.discriminant.place().local() != tempLocalDecl.id ||
      switchInt.discriminant.place().projections().size() != 0 || switchInt.arms.size() != 2 ||
      switchInt.arms[0].target != thenBlock.id || switchInt.arms[1].target != elseBlock.id ||
      switchInt.defaultTarget != elseBlock.id) {
    return zc::none;
  }

  // Each arm assigns the result an integer constant, then jumps to the join.
  auto lowerArm = [&](const mir::MirBasicBlock& branch, zc::Vector<LirStatement>& out) -> bool {
    if (branch.statements.size() != 1 ||
        branch.statements[0].kind() != mir::MirStatementKind::Assign ||
        branch.terminator.kind() != mir::MirTerminatorKind::Goto ||
        branch.terminator.gotoValue().target != joinBlock.id) {
      return false;
    }
    const auto& assignment = branch.statements[0].assignmentValue();
    if (assignment.destination.local() != resultLocalDecl.id ||
        assignment.destination.projections().size() != 0 ||
        assignment.value.kind() != mir::MirRvalueKind::Use) {
      return false;
    }
    const auto& operand = assignment.value.useValue().operand;
    if (operand.kind() != mir::MirOperandKind::Constant ||
        operand.constantValue().type != function.resultType) {
      return false;
    }
    auto lowered = lirOperandFor(operand, resultCarrierValue);
    if (lowered == zc::none) { return false; }
    out.add(LirStatement::assign(resultOrdinal, ZC_REQUIRE_NONNULL(lowered)));
    return true;
  };
  zc::Vector<LirStatement> thenStatements;
  zc::Vector<LirStatement> elseStatements;
  if (!lowerArm(thenBlock, thenStatements) || !lowerArm(elseBlock, elseStatements)) {
    return zc::none;
  }

  // Join: no statements; Return(place-use result).
  if (joinBlock.statements.size() != 0 ||
      joinBlock.terminator.kind() != mir::MirTerminatorKind::Return) {
    return zc::none;
  }
  const auto& returnValue = joinBlock.terminator.returnValue().value;
  if (returnValue == zc::none) { return zc::none; }
  bool returnsResult = false;
  ZC_IF_SOME(value, returnValue) {
    returnsResult = value.kind() != mir::MirOperandKind::Constant &&
                    value.place().local() == resultLocalDecl.id &&
                    value.place().projections().size() == 0;
  }
  if (!returnsResult) { return zc::none; }

  auto entryId = LirBlockId::fromOrdinal(1);
  auto thenId = LirBlockId::fromOrdinal(2);
  auto elseId = LirBlockId::fromOrdinal(3);
  auto joinId = LirBlockId::fromOrdinal(4);
  if (entryId == zc::none || thenId == zc::none || elseId == zc::none || joinId == zc::none) {
    return zc::none;
  }

  zc::Vector<LirBasicBlock> blocks;
  {
    zc::Vector<LirStatement> entryStatements;
    entryStatements.add(LirStatement::compare(tempOrdinal, lirComparisonOpFor(comparison.op),
                                              ZC_REQUIRE_NONNULL(lirLeft),
                                              ZC_REQUIRE_NONNULL(lirRight)));
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(entryId), zc::mv(entryStatements),
                             LirTerminator::condBranch(tempOrdinal, ZC_REQUIRE_NONNULL(thenId),
                                                       ZC_REQUIRE_NONNULL(elseId))));
  }
  blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(thenId), zc::mv(thenStatements),
                           LirTerminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(elseId), zc::mv(elseStatements),
                           LirTerminator::gotoBlock(ZC_REQUIRE_NONNULL(joinId))));
  {
    zc::Vector<LirStatement> none;
    blocks.add(LirBasicBlock(ZC_REQUIRE_NONNULL(joinId), zc::mv(none),
                             LirTerminator::returnLocal(resultOrdinal)));
  }

  // Declare every integer parameter local plus the result and temp body locals.
  zc::Vector<LirLocal> parameters;
  for (size_t i = 0; i < parameterCount; ++i) {
    auto carrier = integerCarrierFor(function.locals[i].type, semanticTypes);
    if (carrier == zc::none) { return zc::none; }
    parameters.add(LirLocal(function.locals[i].id.ordinal(), ZC_REQUIRE_NONNULL(carrier)));
  }
  zc::Vector<LirLocal> locals;
  locals.add(LirLocal(resultOrdinal, resultCarrierValue));
  locals.add(LirLocal(tempOrdinal, tempCarrierValue));

  zc::Vector<LirFunction> functions;
  functions.add(LirFunction(zc::heapString("zom.conditional_cmp"), resultCarrierValue,
                            zc::mv(parameters), zc::mv(locals), zc::mv(blocks)));
  return LirModule(zc::mv(functions));
}

}  // namespace zomlang::compiler::lir
